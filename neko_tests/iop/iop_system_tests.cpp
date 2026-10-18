#include <cstdint>
#include <stdexcept>
#include <vector>

#include "catch.hpp"
#include "iop_interrupt_controller.hpp"
#include "iop_timers.hpp"
#include "neko_system.hpp"

namespace
{
  void instruction(NekoSystem *system, IOPAddress address, IOPWord word)
  {
    REQUIRE(system->iopBus().write32(address, word).status ==
      IOPBusStatus::Completed);
  }

  void interruptProgram(NekoSystem *system)
  {
    instruction(system, 0, 0x34010401); // ori r1, r0, IEc | IM2
    instruction(system, 4, 0x40816000); // mtc0 r1, Status
    instruction(system, 8, 0x24420001); // addiu r2, r2, 1
    system->iopCore().startExecution(0xa0000000);
  }
}

TEST_CASE("IOP system schedules phase-zero instructions at exactly one eighth rate",
  "[iop][system]")
{
  NekoSystem system;
  for (IOPAddress address = 0; address < 64; address += 4)
  {
    instruction(&system, address, 0x24210001); // addiu r1, r1, 1
  }
  system.iopCore().startExecution(0xa0000000);
  system.eeCore().startExecution(0);
  REQUIRE(system.runMasterCycles(0) == 0);
  REQUIRE(system.iopCore().elapsedCycles() == 0);
  for (std::uint64_t cycles = 1; cycles <= 81; ++cycles)
  {
    CAPTURE(cycles);
    system.clockMasterCycle();
    const auto pulses = (cycles + 7) / 8;
    REQUIRE(system.masterClockScheduler().currentCycle() == cycles);
    REQUIRE(system.eeCore().elapsedCycles() == cycles);
    REQUIRE(system.iopCore().elapsedCycles() == pulses);
    REQUIRE(system.iopCore().retiredInstructions() == pulses);
    REQUIRE(system.iopCore().generalRegister(1) == pulses);
    REQUIRE(system.iopTimers().counter(2).count == pulses);
    REQUIRE(system.iopCore().programCounter() == 0xa0000000 + 4 * pulses);
  }
}

TEST_CASE("IOP system advances exact pixel and system clocks with both CPUs halted",
  "[iop][system]")
{
  NekoSystem system;
  system.iopTimers().writeMode(0, IOPTimerMode::ALTERNATE_SOURCE);
  system.runMasterCycles(21);
  REQUIRE(system.iopTimers().counter(0).count == 0);
  REQUIRE(system.iopTimerTiming().pixelPhase() == 7875);
  REQUIRE(system.iopTimers().counter(2).count == 3);
  system.clockMasterCycle();
  REQUIRE(system.iopTimers().counter(0).count == 1);
  REQUIRE(system.iopTimerTiming().pixelPhase() == 58);
  REQUIRE(system.iopTimers().counter(2).count == 3);
  system.runMasterCycles(8192 - 22);
  REQUIRE(system.iopTimers().counter(0).count == 375);
  REQUIRE(system.iopTimerTiming().pixelPhase() == 0);
  REQUIRE(system.iopTimers().counter(2).count == 1024);
  REQUIRE(system.iopCore().executionState() == IOPExecutionState::Halted);
  REQUIRE(system.iopCore().elapsedCycles() == 0);
  REQUIRE(system.eeCore().executionState() == EEExecutionState::Halted);
  REQUIRE(system.eeCore().elapsedCycles() == 0);
}

TEST_CASE("IOP system host resume preserves the existing master clock phase",
  "[iop][system]")
{
  NekoSystem system;
  instruction(&system, 0, 0x24210001);
  instruction(&system, 4, 0x24210001);
  system.iopCore().startExecution(0xa0000000);
  system.clockMasterCycle();
  REQUIRE(system.iopCore().generalRegister(1) == 1);
  system.iopCore().haltExecution();
  system.runMasterCycles(3);
  system.iopCore().resumeExecution();
  system.runMasterCycles(4);
  REQUIRE(system.masterClockScheduler().currentCycle() == 8);
  REQUIRE(system.iopCore().elapsedCycles() == 1);
  REQUIRE(system.iopCore().generalRegister(1) == 1);
  system.clockMasterCycle();
  REQUIRE(system.iopCore().elapsedCycles() == 2);
  REQUIRE(system.iopCore().generalRegister(1) == 2);
  REQUIRE(system.iopTimers().counter(2).count == 2);
}

TEST_CASE("IOP system latches timer requests before same-cycle CPU admission",
  "[iop][system]")
{
  NekoSystem system;
  interruptProgram(&system);
  system.iopInterruptController().writeMask(0x10);
  system.iopInterruptController().writeControl(1);
  system.runMasterCycles(9);
  REQUIRE(system.iopCore().status() ==
    (IOPCOP0Status::TLB_SHUTDOWN | 0x401));
  REQUIRE(system.iopCore().retiredInstructions() == 2);
  system.iopTimers().writeTarget(0, 3);
  system.iopTimers().writeMode(0, IOPTimerMode::INTERRUPT_ON_TARGET);
  system.iopTimers().writeCount(0, 2);
  system.runMasterCycles(7);
  REQUIRE(system.iopInterruptController().status() == 0);
  system.clockMasterCycle();
  REQUIRE(system.iopInterruptController().status() == 0x10);
  REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
  REQUIRE(system.iopCore().exceptionProgramCounter() == 0xa0000008);
  REQUIRE(system.iopCore().programCounter() == 0x80000080);
  REQUIRE(system.iopCore().generalRegister(2) == 0);
  REQUIRE(system.iopCore().retiredInstructions() == 2);
  REQUIRE(system.iopCore().elapsedCycles() == 3);
  REQUIRE(system.iopTimers().counter(0).pulseReleasePending);
  system.runMasterCycles(8);
  REQUIRE_FALSE(system.iopTimers().counter(0).pulseReleasePending);
  REQUIRE(system.iopInterruptController().status() == 0x10);
  REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
  REQUIRE(system.iopCore().retiredInstructions() == 3);
}

TEST_CASE("IOP system samples post-display gates without stealing EE vertical edges",
  "[iop][system]")
{
  NekoSystem system;
  system.gsDisplay().configureTiming({17, 32, {17, 32, 1}});
  interruptProgram(&system);
  system.iopInterruptController().writeMask(0x20);
  system.iopInterruptController().writeControl(1);
  system.iopTimers().writeMode(0, IOPTimerMode::GATE_ENABLE);
  system.iopTimers().writeTarget(1, 1);
  system.iopTimers().writeMode(1,
    IOPTimerMode::ALTERNATE_SOURCE | IOPTimerMode::INTERRUPT_ON_TARGET);
  system.runMasterCycles(16);
  REQUIRE(system.iopTimers().counter(0).count == 2);
  REQUIRE(system.iopTimers().counter(1).count == 0);
  REQUIRE_FALSE(system.iopTimers().gateLevels().horizontalBlank);
  REQUIRE(system.interruptController().status() == 0);
  system.clockMasterCycle();
  REQUIRE(system.gsDisplay().inHorizontalBlank());
  REQUIRE(system.gsDisplay().inVerticalBlank());
  REQUIRE(system.iopTimers().gateLevels().horizontalBlank);
  REQUIRE(system.iopTimers().gateLevels().verticalBlank);
  REQUIRE(system.iopTimers().counter(0).count == 2);
  REQUIRE(system.iopTimers().counter(1).count == 1);
  REQUIRE(system.iopInterruptController().status() == 0x20);
  REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
  REQUIRE(system.iopCore().exceptionProgramCounter() == 0xa0000008);
  REQUIRE(system.interruptController().status() ==
    EEInterruptSource::mask(EEInterruptSource::VBLANK_START));
  REQUIRE_FALSE(system.gsDisplay().takeVerticalBlankStart());
  system.runMasterCycles(15);
  REQUIRE_FALSE(system.iopTimers().gateLevels().horizontalBlank);
  REQUIRE_FALSE(system.iopTimers().gateLevels().verticalBlank);
  REQUIRE(system.interruptController().status() ==
    (EEInterruptSource::mask(EEInterruptSource::VBLANK_START) |
      EEInterruptSource::mask(EEInterruptSource::VBLANK_END)));
}

TEST_CASE("IOP system restores every nonzero master remainder and live continuation",
  "[iop][system]")
{
  for (std::uint64_t remainder = 1; remainder < 8; ++remainder)
  {
    CAPTURE(remainder);
    NekoSystem original;
    instruction(&original, 0, 0x8c010100); // lw r1, 0x100(r0)
    instruction(&original, 4, 0x10000002); // beq r0, r0, 0x10
    instruction(&original, 8, 0x8c020104); // lw r2, 0x104(r0)
    instruction(&original, 0x10, 0x24430001); // addiu r3, r2, 1
    instruction(&original, 0x100, 0x12345678);
    instruction(&original, 0x104, 0x87654321);
    original.iopCore().startExecution(0xa0000000);
    original.iopTimers().writeTarget(0, 4);
    original.iopTimers().writeMode(0, IOPTimerMode::INTERRUPT_ON_TARGET);
    original.runMasterCycles(8 + remainder);
    REQUIRE(original.iopCore().branchContinuation().active);
    NekoSystem restored;
    restored.loadState(original.saveState());
    for (unsigned cycle = 0; cycle < 33; ++cycle)
    {
      original.clockMasterCycle();
      restored.clockMasterCycle();
      REQUIRE(original.iopStateHash() == restored.iopStateHash());
      REQUIRE(original.eeStateHash() == restored.eeStateHash());
    }
    REQUIRE(restored.saveState() == original.saveState());
    REQUIRE(restored.iopCore().generalRegister(1) == 0x12345678);
    REQUIRE(restored.iopCore().generalRegister(2) == 0x87654321);
    REQUIRE(restored.iopCore().generalRegister(3) == 1);
    REQUIRE(restored.iopInterruptController().status() == 0x10);
  }
}

TEST_CASE("IOP system BIOS diagnostic shares immutable bytes but starts only EE",
  "[iop][system]")
{
  NekoSystem system;
  std::vector<std::uint8_t> bytes(BootROMImage::SIZE, 0);
  bytes[BootROMImage::SIZE - 1] = 0xab;
  const auto result = system.runBIOS(bytes, 1);
  REQUIRE(result.masterCycles == 1);
  REQUIRE(result.pendingException == EEException::None);
  REQUIRE(system.iopBus().romIdentity() != nullptr);
  REQUIRE(system.iopBus().romIdentity()->data()[BootROMImage::SIZE - 1] == 0xab);
  bytes[BootROMImage::SIZE - 1] = 0xff;
  REQUIRE(system.iopBus().romIdentity()->data()[BootROMImage::SIZE - 1] == 0xab);
  std::uint8_t byte = 0;
  REQUIRE(system.eeBus().readData8(EEMemoryMap::BOOT_ROM_END - 1, &byte));
  REQUIRE(byte == 0xab);
  REQUIRE(system.iopCore().programCounter() == 0xbfc00000);
  REQUIRE(system.iopCore().executionState() == IOPExecutionState::Halted);
  REQUIRE(system.iopCore().elapsedCycles() == 0);
  system.reset();
  REQUIRE(system.iopBus().romIdentity() != nullptr);
  REQUIRE(system.iopCore().readData8(0xbfffffff).value == 0xab);
  REQUIRE(system.iopTimerTiming().pixelPhase() == 0);
  REQUIRE(system.iopTimers().counter(2).count == 0);
}

TEST_CASE("IOP system BIOS diagnostic rejects a running IOP without mutation",
  "[iop][system]")
{
  NekoSystem system;
  system.iopCore().startExecution(0xa0000000);
  const auto before = system.iopStateHash();
  REQUIRE_THROWS_AS(
    system.runBIOS(std::vector<std::uint8_t>(BootROMImage::SIZE, 0), 1),
    std::logic_error);
  REQUIRE(system.iopStateHash() == before);
  REQUIRE(system.masterClockScheduler().currentCycle() == 0);
}

TEST_CASE("IOP system BIOS validation precedes whole-machine reset",
  "[iop][system]")
{
  NekoSystem system;
  instruction(&system, 0x100, 0x12345678);
  system.runMasterCycles(3);
  const auto before = system.iopStateHash();
  REQUIRE_THROWS_AS(
    system.runBIOS(std::vector<std::uint8_t>(BootROMImage::SIZE - 1, 0), 1),
    std::invalid_argument);
  REQUIRE(system.iopStateHash() == before);
  REQUIRE(system.masterClockScheduler().currentCycle() == 3);
}

TEST_CASE("IOP system installs one shared BIOS without starting or resetting CPUs",
  "[iop][system]")
{
  NekoSystem system;
  system.runMasterCycles(3);
  instruction(&system, 0x100, 0x12345678);
  std::vector<std::uint8_t> bytes(BootROMImage::SIZE, 0);
  bytes[0] = 7;
  bytes[2] = 2;
  bytes[3] = 0x24; // addiu r2, r0, 7
  system.installBootROM(bytes);
  const auto identity = system.eeBus().bootROMIdentity();
  REQUIRE(identity != nullptr);
  REQUIRE(identity == system.iopBus().romIdentity());
  REQUIRE(system.masterClockScheduler().currentCycle() == 3);
  REQUIRE(system.iopTimers().counter(2).count == 1);
  REQUIRE(system.iopBus().read32(0x100).value == 0x12345678);
  REQUIRE(system.eeCore().executionState() == EEExecutionState::Halted);
  REQUIRE(system.iopCore().executionState() == IOPExecutionState::Halted);
  bytes[0] = 0xff;
  REQUIRE(system.eeBus().read32(EEMemoryMap::BOOT_ROM_BASE) == 0x24020007);
  REQUIRE(system.iopCore().readData32(0xbfc00000).value == 0x24020007);
  system.reset();
  REQUIRE(system.eeBus().bootROMIdentity() == identity);
  REQUIRE(system.iopBus().romIdentity() == identity);
  REQUIRE(system.iopTimers().counter(2).count == 0);
  REQUIRE(system.iopBus().read32(0x100).value == 0);
  REQUIRE(system.masterClockScheduler().currentCycle() == 0);
  system.iopCore().startExecution(0xbfc00000);
  system.clockMasterCycle();
  REQUIRE(system.iopCore().generalRegister(2) == 7);
  REQUIRE(system.eeCore().elapsedCycles() == 0);
}

TEST_CASE("IOP system rejects shared BIOS installation atomically",
  "[iop][system]")
{
  NekoSystem system;
  const std::vector<std::uint8_t> bytes(BootROMImage::SIZE, 0);
  system.installBootROM(bytes);
  const auto identity = system.eeBus().bootROMIdentity();
  SECTION("invalid size")
  {
    const auto before = system.saveState();
    REQUIRE_THROWS_AS(system.installBootROM({}), std::invalid_argument);
    const bool unchanged = system.saveState() == before;
    REQUIRE(unchanged);
  }
  SECTION("EE running")
  {
    system.eeCore().startExecution(0);
    const auto before = system.saveState();
    REQUIRE_THROWS_AS(system.installBootROM(bytes), std::logic_error);
    const bool unchanged = system.saveState() == before;
    REQUIRE(unchanged);
    REQUIRE_THROWS_AS(system.runBIOS(bytes, 1), std::logic_error);
  }
  SECTION("IOP running")
  {
    system.iopCore().startExecution(0xa0000000);
    const auto before = system.saveState();
    REQUIRE_THROWS_AS(system.installBootROM(bytes), std::logic_error);
    const bool unchanged = system.saveState() == before;
    REQUIRE(unchanged);
  }
  REQUIRE(system.eeBus().bootROMIdentity() == identity);
  REQUIRE(system.iopBus().romIdentity() == identity);
}

TEST_CASE("IOP system restores shared BIOS configuration into destination identities",
  "[iop][system]")
{
  const std::vector<std::uint8_t> bytes(BootROMImage::SIZE, 0);
  NekoSystem original;
  original.installBootROM(bytes);
  original.iopCore().startExecution(0xbfc00000);
  original.runMasterCycles(13);
  NekoSystem restored;
  restored.installBootROM(bytes);
  const auto identity = restored.eeBus().bootROMIdentity();
  REQUIRE(identity != original.eeBus().bootROMIdentity());
  restored.loadState(original.saveState());
  REQUIRE(restored.eeBus().bootROMIdentity() == identity);
  REQUIRE(restored.iopBus().romIdentity() == identity);
  original.runMasterCycles(21);
  restored.runMasterCycles(21);
  REQUIRE(original.iopStateHash() == restored.iopStateHash());
  REQUIRE(original.saveState() == restored.saveState());
}
