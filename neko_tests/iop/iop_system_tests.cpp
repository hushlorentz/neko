#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "catch.hpp"
#include "iop_interrupt_controller.hpp"
#include "iop_timers.hpp"
#include "neko_system.hpp"

namespace
{
  constexpr std::array<IOPAddress, 6> timerBases = {{
    0x1f801100, 0x1f801110, 0x1f801120,
    0x1f801480, 0x1f801490, 0x1f8014a0
  }};
  constexpr std::array<IOPWord, 6> timerCauses = {{
    0x10, 0x20, 0x40, 0x4000, 0x8000, 0x10000
  }};

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

  void writeTimerRegister(
    NekoSystem *system, std::size_t index, IOPAddress offset, IOPWord value)
  {
    REQUIRE(system->iopBus().write32(timerBases.at(index) + offset, value).status ==
      IOPBusStatus::Completed);
  }

  void prepareTimerService(NekoSystem *system)
  {
    const std::array<IOPWord, 10> program = {{
      0x34010401, // ori r1, r0, IEc | IM2
      0x40816000, // mtc0 r1, Status
      0x3c03bf80, // lui r3, 0xbf80
      0x34631100, // ori r3, r3, 0x1100
      0x3c04bf80, // lui r4, 0xbf80
      0x34841070, // ori r4, r4, 0x1070
      0x3c060001, // lui r6, 1
      0,
      0xac860004, // sw r6, 4(r4): unmask timer 5
      0x24420001  // addiu r2, r2, 1
    }};
    const std::array<IOPWord, 9> handler = {{
      0x8c680004, // lw r8, 4(r3): acknowledge timer 0 mode
      0x8c890004, // lw r9, 4(r4): read INTC mask
      0,
      0x01205027, // nor r10, r9, r0
      0xac8a0000, // sw r10, 0(r4): acknowledge only unmasked causes
      0x401a7000, // mfc0 r26, EPC
      0,
      0x03400008, // jr r26
      0x42000010  // rfe
    }};
    for (std::size_t index = 0; index < program.size(); ++index)
    {
      instruction(system, static_cast<IOPAddress>(4 * index), program[index]);
    }
    for (std::size_t index = 0; index < handler.size(); ++index)
    {
      instruction(system, static_cast<IOPAddress>(0x80 + 4 * index), handler[index]);
    }
    system->gsDisplay().configureTiming({65, 97, {9, 17, 1}});
    REQUIRE(system->iopBus().write32(IOPBus::INTC_MASK, 0x10).status ==
      IOPBusStatus::Completed);
    REQUIRE(system->iopBus().write32(IOPBus::INTC_CTRL, 1).status ==
      IOPBusStatus::Completed);
    system->iopCore().startExecution(0xa0000000);
    system->runMasterCycles(57);
    REQUIRE(system->iopCore().retiredInstructions() == 8);
    REQUIRE(system->iopCore().programCounter() == 0xa0000020);
    REQUIRE(system->iopCore().exception().kind == IOPException::None);
    writeTimerRegister(system, 0, 8, 1);
    writeTimerRegister(system, 0, 4, 0x10);
    writeTimerRegister(system, 5, 8, 1);
    writeTimerRegister(system, 5, 4, 0x90);
    writeTimerRegister(system, 1, 4, 0x107);
    writeTimerRegister(system, 2, 4, 0x200);
    writeTimerRegister(system, 4, 4, 0x6000);
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

TEST_CASE("IOP system coalesces target overflow coincidence for all six timer causes",
  "[iop][system][timer][conformance]")
{
  for (IOPWord enables : {0x10u, 0x20u, 0x30u})
  {
    for (IOPWord requestMode : {0u, 0x40u, 0x80u, 0xc0u})
    {
      CAPTURE(enables);
      CAPTURE(requestMode);
      NekoSystem system;
      interruptProgram(&system);
      system.iopInterruptController().writeMask(0x1c070);
      system.iopInterruptController().writeControl(1);
      system.runMasterCycles(9);
      for (std::size_t index = 0; index < 6; ++index)
      {
        writeTimerRegister(&system, index, 8, 0);
        writeTimerRegister(&system, index, 4, enables | requestMode);
        writeTimerRegister(&system, index, 0,
          index < 3 ? 0xffff : 0xffffffff);
      }
      system.runMasterCycles(7);
      REQUIRE(system.iopInterruptController().status() == 0);
      system.clockMasterCycle();
      REQUIRE(system.iopInterruptController().status() == 0x1c070);
      REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
      REQUIRE(system.iopCore().exceptionProgramCounter() == 0xa0000008);
      REQUIRE(system.iopCore().retiredInstructions() == 2);
      for (std::size_t index = 0; index < 6; ++index)
      {
        CAPTURE(index);
        const auto &counter = system.iopTimers().counter(index);
        REQUIRE(counter.count == 0);
        REQUIRE(counter.mode == (enables | requestMode | 0x1800));
        REQUIRE(counter.oneShotConsumed == ((requestMode & 0x40) == 0));
        REQUIRE(counter.pulseReleasePending == ((requestMode & 0x80) == 0));
      }
      system.runMasterCycles(7);
      REQUIRE(system.iopInterruptController().status() == 0x1c070);
      system.clockMasterCycle();
      for (std::size_t index = 0; index < 6; ++index)
      {
        CAPTURE(index);
        REQUIRE_FALSE(system.iopTimers().counter(index).pulseReleasePending);
        REQUIRE((system.iopTimers().counter(index).mode & 0x400) ==
          ((requestMode & 0x80) == 0 ? 0x400 : 0));
      }
      REQUIRE(system.iopInterruptController().status() == 0x1c070);
    }
  }
}

TEST_CASE("IOP system applies every external gate mode before coincident CPU pulses",
  "[iop][system][timer][conformance]")
{
  const std::array<std::array<IOPWord, 5>, 4> counts = {{
    {{1, 1, 2, 3, 3}},
    {{1, 2, 1, 2, 3}},
    {{0, 1, 0, 0, 1}},
    {{0, 0, 1, 2, 3}}
  }};
  for (std::size_t index : {std::size_t{0}, std::size_t{1}, std::size_t{3}})
  {
    for (IOPWord gate = 0; gate < 4; ++gate)
    {
      CAPTURE(index);
      CAPTURE(gate);
      NekoSystem system;
      system.gsDisplay().configureTiming({9, 17, {9, 17, 1}});
      interruptProgram(&system);
      system.iopInterruptController().writeMask(timerCauses[index]);
      system.iopInterruptController().writeControl(1);
      writeTimerRegister(&system, index, 8, 1);
      writeTimerRegister(&system, index, 4, 0x91 | (gate << 1));
      for (std::uint64_t cycle = 1; cycle <= 33; ++cycle)
      {
        CAPTURE(cycle);
        system.clockMasterCycle();
        const bool blank = cycle % 17 >= 9;
        REQUIRE(system.iopTimers().gateLevels().horizontalBlank == blank);
        REQUIRE(system.iopTimers().gateLevels().verticalBlank == blank);
        if (cycle % 8 == 1)
        {
          REQUIRE(system.iopTimers().counter(index).count ==
            counts[gate][(cycle - 1) / 8]);
        }
        if (cycle == 17)
        {
          REQUIRE(system.iopInterruptController().status() == timerCauses[index]);
          REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
          REQUIRE(system.iopCore().exceptionProgramCounter() == 0xa0000008);
          REQUIRE(system.iopCore().generalRegister(2) == 0);
        }
      }
      REQUIRE(system.iopTimers().counter(index).oneShotConsumed);
      REQUIRE_FALSE(system.iopTimers().counter(index).gateWaiting);
      REQUIRE(system.iopInterruptController().status() == timerCauses[index]);
    }
  }
}

TEST_CASE("IOP system preserves all prescaler phases through MMIO before interrupt delivery",
  "[iop][system][timer][conformance]")
{
  const std::array<std::uint16_t, 4> divisors = {{1, 8, 16, 256}};
  for (std::size_t index : {std::size_t{2}, std::size_t{4}, std::size_t{5}})
  {
    const IOPWord selectors = index == 2 ? 2 : 4;
    for (IOPWord selector = 0; selector < selectors; ++selector)
    {
      CAPTURE(index);
      CAPTURE(selector);
      NekoSystem system;
      interruptProgram(&system);
      system.iopInterruptController().writeMask(timerCauses[index]);
      system.iopInterruptController().writeControl(1);
      system.runMasterCycles(9);
      const IOPWord mode = 0x10 | (selector << (index == 2 ? 9 : 13));
      const unsigned divisor = divisors[selector];
      writeTimerRegister(&system, index, 8, 1);
      writeTimerRegister(&system, index, 4, mode);
      system.runMasterCycles(8 * divisor - 1);
      REQUIRE(system.iopTimers().counter(index).count == 0);
      REQUIRE(system.iopTimers().counter(index).prescalerRemainder == divisor - 1);
      REQUIRE(system.iopInterruptController().status() == 0);
      writeTimerRegister(&system, index, 0, 7);
      writeTimerRegister(&system, index, 8, 8);
      const auto acknowledged = system.iopBus().read32(timerBases[index] + 4);
      REQUIRE(acknowledged.status == IOPBusStatus::Completed);
      REQUIRE(acknowledged.value == (mode | 0x400));
      REQUIRE(system.iopTimers().counter(index).prescalerRemainder == divisor - 1);
      system.clockMasterCycle();
      REQUIRE(system.iopTimers().counter(index).count == 8);
      REQUIRE(system.iopTimers().counter(index).prescalerRemainder == 0);
      REQUIRE(system.iopInterruptController().status() == timerCauses[index]);
      REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
      REQUIRE(system.iopCore().exceptionProgramCounter() ==
        0xa0000008 + 4 * (divisor - 1));
      REQUIRE(system.iopCore().retiredInstructions() == divisor + 1);
    }
  }
}

TEST_CASE("IOP system guest arms every timer through MMIO before next pulse interrupt admission",
  "[iop][system][timer][conformance]")
{
  for (std::size_t index = 0; index < 6; ++index)
  {
    CAPTURE(index);
    NekoSystem system;
    const std::array<IOPWord, 15> program = {{
      0x34010401, // ori r1, r0, IEc | IM2
      0x40816000, // mtc0 r1, Status
      0x3c03bf80, // lui r3, 0xbf80
      0x34630000 | (timerBases[index] & 0xffff),
      0x3c04bf80, // lui r4, 0xbf80
      0x34841070, // ori r4, r4, 0x1070
      0x34050001, // ori r5, r0, 1
      0xac850008, // sw r5, 8(r4): enable INTC
      0x3c060000 | (timerCauses[index] >> 16),
      0x34c60000 | (timerCauses[index] & 0xffff),
      0xac860004, // sw r6, 4(r4): unmask selected timer
      0xac650008, // sw r5, 8(r3): set target
      0x34070018, // ori r7, r0, target interrupt | reset on target
      0xac670004, // sw r7, 4(r3): reset and arm timer
      0x24420001  // addiu r2, r2, 1
    }};
    for (std::size_t word = 0; word < program.size(); ++word)
    {
      instruction(&system, static_cast<IOPAddress>(4 * word), program[word]);
    }
    system.iopCore().startExecution(0xa0000000);
    system.runMasterCycles(105);
    REQUIRE(system.iopCore().programCounter() == 0xa0000038);
    REQUIRE(system.iopCore().retiredInstructions() == 14);
    REQUIRE(system.iopTimers().counter(index).count == 0);
    REQUIRE(system.iopTimers().counter(index).target == 1);
    REQUIRE(system.iopTimers().counter(index).mode == 0x418);
    REQUIRE(system.iopInterruptController().mask() == timerCauses[index]);
    REQUIRE(system.iopInterruptController().status() == 0);
    system.runMasterCycles(7);
    REQUIRE(system.iopTimers().counter(index).count == 0);
    REQUIRE(system.iopInterruptController().status() == 0);
    system.clockMasterCycle();
    REQUIRE(system.iopTimers().counter(index).count == 0);
    REQUIRE(system.iopTimers().counter(index).mode == 0x818);
    REQUIRE(system.iopInterruptController().status() == timerCauses[index]);
    REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
    REQUIRE(system.iopCore().exceptionProgramCounter() == 0xa0000038);
    REQUIRE(system.iopCore().retiredInstructions() == 14);
    REQUIRE(system.iopCore().generalRegister(2) == 0);
  }
}

TEST_CASE("IOP system guest independently acknowledges timer modes and masked INTC causes",
  "[iop][system][timer][conformance]")
{
  NekoSystem system;
  prepareTimerService(&system);
  system.runMasterCycles(8);
  REQUIRE(system.iopInterruptController().status() == 0x10010);
  REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
  REQUIRE(system.iopCore().exceptionProgramCounter() == 0xa0000020);
  REQUIRE(system.iopTimers().counter(0).pulseReleasePending);
  REQUIRE(system.iopTimers().counter(0).oneShotConsumed);
  REQUIRE(system.iopTimers().counter(5).oneShotConsumed);
  system.runMasterCycles(8);
  REQUIRE(system.iopCore().delayedResultContinuation().source ==
    IOPDelayedResultSource::Load);
  REQUIRE(system.iopCore().generalRegister(8) == 0);
  REQUIRE(system.iopTimers().counter(0).mode == 0x410);
  REQUIRE_FALSE(system.iopTimers().counter(0).oneShotConsumed);
  REQUIRE_FALSE(system.iopTimers().counter(0).pulseReleasePending);
  REQUIRE(system.iopTimers().counter(5).mode == 0x890);
  REQUIRE(system.iopTimers().counter(5).oneShotConsumed);
  REQUIRE(system.iopInterruptController().status() == 0x10010);
  system.runMasterCycles(16);
  REQUIRE(system.iopCore().generalRegister(8) == 0xc10);
  REQUIRE(system.iopCore().generalRegister(9) == 0x10);
  REQUIRE(system.iopInterruptController().status() == 0x10010);
  system.runMasterCycles(16);
  REQUIRE(system.iopInterruptController().status() == 0x10000);
  REQUIRE(system.iopTimers().counter(5).mode == 0x890);
  system.runMasterCycles(32);
  REQUIRE(system.iopCore().programCounter() == 0xa0000020);
  REQUIRE(system.iopCore().status() ==
    (IOPCOP0Status::TLB_SHUTDOWN | 0x401));
  REQUIRE(system.iopInterruptController().mask() == 0x10);
  system.runMasterCycles(8);
  REQUIRE(system.iopInterruptController().mask() == 0x10000);
  REQUIRE(system.iopCore().programCounter() == 0xa0000024);
  system.runMasterCycles(8);
  REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
  REQUIRE(system.iopCore().exceptionProgramCounter() == 0xa0000024);
  REQUIRE(system.iopCore().generalRegister(2) == 0);
  system.runMasterCycles(72);
  REQUIRE(system.iopCore().programCounter() == 0xa0000024);
  REQUIRE(system.iopInterruptController().status() == 0);
  REQUIRE(system.iopTimers().counter(5).mode == 0x890);
  REQUIRE(system.iopTimers().counter(5).oneShotConsumed);
  system.runMasterCycles(8);
  REQUIRE(system.iopCore().generalRegister(2) == 1);
  REQUIRE(system.iopCore().elapsedCycles() == 30);
  REQUIRE(system.iopCore().retiredInstructions() == 28);
  system.runMasterCycles(32);
  REQUIRE(system.iopInterruptController().status() == 0);
  REQUIRE(system.iopCore().exception().kind == IOPException::None);
  REQUIRE(system.iopCore().generalRegister(2) == 1);
}

TEST_CASE("IOP system admits a halted CPU pending timer only after resume on a due pulse",
  "[iop][system][timer][conformance]")
{
  NekoSystem system;
  interruptProgram(&system);
  system.iopInterruptController().writeMask(0x40);
  system.iopInterruptController().writeControl(1);
  system.runMasterCycles(9);
  system.iopCore().haltExecution();
  writeTimerRegister(&system, 2, 8, 1);
  writeTimerRegister(&system, 2, 4, 0x210);
  system.runMasterCycles(64);
  REQUIRE(system.masterClockScheduler().currentCycle() == 73);
  REQUIRE(system.iopTimers().counter(2).count == 1);
  REQUIRE(system.iopTimers().counter(2).pulseReleasePending);
  REQUIRE(system.iopInterruptController().status() == 0x40);
  REQUIRE(system.iopCore().elapsedCycles() == 2);
  REQUIRE(system.iopCore().programCounter() == 0xa0000008);
  system.iopCore().resumeExecution();
  system.runMasterCycles(7);
  REQUIRE(system.iopCore().elapsedCycles() == 2);
  REQUIRE(system.iopTimers().counter(2).pulseReleasePending);
  system.clockMasterCycle();
  REQUIRE_FALSE(system.iopTimers().counter(2).pulseReleasePending);
  REQUIRE(system.iopInterruptController().status() == 0x40);
  REQUIRE(system.iopCore().elapsedCycles() == 3);
  REQUIRE(system.iopCore().retiredInstructions() == 2);
  REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
  REQUIRE(system.iopCore().exceptionProgramCounter() == 0xa0000008);
  REQUIRE(system.iopCore().generalRegister(2) == 0);
}

TEST_CASE("IOP system repeats and restores timer service at event load and branch boundaries",
  "[iop][system][timer][conformance]")
{
  NekoSystem reference;
  prepareTimerService(&reference);
  std::vector<std::uint64_t> hashes;
  for (unsigned cycle = 58; cycle <= 241; ++cycle)
  {
    reference.clockMasterCycle();
    hashes.push_back(reference.iopStateHash());
  }
  REQUIRE(reference.iopCore().generalRegister(2) == 1);
  REQUIRE(reference.iopInterruptController().status() == 0);
  const auto finalState = reference.saveState();
  for (unsigned checkpoint : {64u, 65u, 73u, 81u, 129u, 153u})
  {
    CAPTURE(checkpoint);
    NekoSystem repeated;
    prepareTimerService(&repeated);
    repeated.runMasterCycles(checkpoint - 57);
    REQUIRE(repeated.iopStateHash() == hashes[checkpoint - 58]);
    if (checkpoint == 64)
    {
      REQUIRE(repeated.iopTimers().counter(0).count == 0);
      REQUIRE(repeated.iopInterruptController().status() == 0);
    }
    else if (checkpoint == 65)
    {
      REQUIRE(repeated.iopTimers().counter(0).pulseReleasePending);
      REQUIRE(repeated.iopCore().exception().kind == IOPException::Interrupt);
    }
    else if (checkpoint == 73 || checkpoint == 81)
    {
      REQUIRE(repeated.iopCore().delayedResultContinuation().source ==
        IOPDelayedResultSource::Load);
      REQUIRE(repeated.iopCore().delayedResultContinuation().destination ==
        (checkpoint == 73 ? 8 : 9));
    }
    else if (checkpoint == 129)
    {
      REQUIRE(repeated.iopCore().branchContinuation().active);
    }
    else
    {
      REQUIRE(repeated.iopCore().exception().kind == IOPException::Interrupt);
      REQUIRE(repeated.iopCore().exceptionProgramCounter() == 0xa0000024);
    }
    const auto state = repeated.saveState();
    const bool stableSave = repeated.saveState() == state;
    REQUIRE(stableSave);
    NekoSystem restored;
    instruction(&restored, 0x100, 0xdeadbeef);
    restored.iopCore().startExecution(0xa0000100);
    restored.runMasterCycles(19);
    restored.loadState(state);
    REQUIRE(restored.iopStateHash() == repeated.iopStateHash());
    const bool identicalRestore = restored.saveState() == state;
    REQUIRE(identicalRestore);
    for (unsigned cycle = checkpoint + 1; cycle <= 241; ++cycle)
    {
      CAPTURE(cycle);
      repeated.clockMasterCycle();
      restored.clockMasterCycle();
      REQUIRE(repeated.iopStateHash() == hashes[cycle - 58]);
      REQUIRE(restored.iopStateHash() == hashes[cycle - 58]);
    }
    REQUIRE(restored.eeStateHash() == reference.eeStateHash());
    REQUIRE(repeated.eeStateHash() == reference.eeStateHash());
    const bool repeatedState = repeated.saveState() == finalState;
    const bool restoredState = restored.saveState() == finalState;
    REQUIRE(repeatedState);
    REQUIRE(restoredState);
  }
}

TEST_CASE("IOP system reset discards active timer interrupt and guest continuations",
  "[iop][system][timer][conformance]")
{
  NekoSystem fresh;
  const auto initialState = fresh.saveState();
  for (unsigned checkpoint : {65u, 73u, 129u})
  {
    CAPTURE(checkpoint);
    NekoSystem system;
    prepareTimerService(&system);
    system.runMasterCycles(checkpoint - 57);
    REQUIRE(system.iopInterruptController().status() != 0);
    REQUIRE(system.iopTimerTiming().pixelPhase() != 0);
    REQUIRE(system.iopTimers().counter(4).prescalerRemainder != 0);
    if (checkpoint == 65)
    {
      REQUIRE(system.iopTimers().counter(0).pulseReleasePending);
      REQUIRE(system.iopTimers().counter(1).gateWaiting);
      REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
    }
    else if (checkpoint == 73)
    {
      REQUIRE(system.iopCore().delayedResultContinuation().source ==
        IOPDelayedResultSource::Load);
    }
    else
    {
      REQUIRE(system.iopCore().branchContinuation().active);
    }
    system.reset();
    REQUIRE(system.iopStateHash() == fresh.iopStateHash());
    const bool pristine = system.saveState() == initialState;
    REQUIRE(pristine);
    system.reset();
    const bool idempotent = system.saveState() == initialState;
    REQUIRE(idempotent);
    prepareTimerService(&system);
    system.runMasterCycles(8);
    REQUIRE(system.iopInterruptController().status() == 0x10010);
    REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
    REQUIRE(system.iopCore().exceptionProgramCounter() == 0xa0000020);
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
