#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "catch.hpp"
#include "neko_system.hpp"
#include "save_state_diagnostics.hpp"

namespace
{
  constexpr std::uint32_t ENTRY = 0xa0001000;
  constexpr std::uint32_t LOAD_VALUE = 0x81234567;
  constexpr std::uint32_t SBUS = 2;

  std::vector<std::uint8_t> bootROM()
  {
    std::vector<std::uint8_t> image(BootROMImage::SIZE, 0);
    image[0] = 0x77;
    image[2] = 0x07;
    image[3] = 0x24; // addiu r7, r0, 0x77
    image.back() = 0xab;
    return image;
  }

  void prepareLiveMachine(NekoSystem &system)
  {
    system.installBootROM(bootROM());
    const std::uint32_t eeProgram[] = {
      0x0c000404, // jal 0x1010
      0x37e20000, // ori r2, r31, 0 (link dependency prevents pairing)
      0x3404dead, // skipped
      0,
      0x34030033
    };
    const std::uint32_t iopProgram[] = {
      0x8c022000, // lw r2, 0x2000(r0)
      0x10000002, // beq r0, r0, 0x1010
      0x8c032000, // lw r3, 0x2000(r0) (delay slot)
      0x3404dead, // skipped
      0x00602821, // addu r5, r3, r0 (older r3)
      0x00603021  // addu r6, r3, r0 (loaded r3)
    };
    for (std::size_t index = 0; index < 5; ++index)
    {
      system.eeBus().write32(
        0x1000 + static_cast<std::uint32_t>(4 * index), eeProgram[index]);
    }
    for (std::size_t index = 0; index < 6; ++index)
    {
      REQUIRE(system.iopBus().write32(
        0x1000 + static_cast<std::uint32_t>(4 * index), iopProgram[index]).status ==
        IOPBusStatus::Completed);
    }
    REQUIRE(system.iopBus().write32(0x2000, LOAD_VALUE).status ==
      IOPBusStatus::Completed);
    NekoInputState input;
    input.buttons = NekoButton::START;
    input.leftStickX = 0x12;
    system.setInput(input);

    REQUIRE(system.eeBus().writeData32(EEMemoryMap::SIF_BASE, 0x12345678));
    REQUIRE(system.iopBus().write32(IOPBus::SIF_BASE + 0x10, 0x87654321).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.eeBus().writeData32(EEMemoryMap::SIF_BASE + 0x20, 0x80000001));
    REQUIRE(system.iopBus().write32(IOPBus::SIF_BASE + 0x30, 0x40000001).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.eeBus().writeData32(EEMemoryMap::SIF_BASE + 0x40, 0x40100));
    REQUIRE(system.iopBus().write32(IOPBus::SIF_DOORBELL, SBUS).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.sif().pushSIF0Prologue({{1, 2, 3, 4}}) ==
      SIFTransferOutcome::Completed);
    REQUIRE(system.sif().pushSIF0PayloadWord(5, SIFPayloadBoundary::Continuing) ==
      SIFTransferOutcome::Completed);
    REQUIRE(system.sif().pushSIF1Quadword({{6, 7, 8, 9}}) ==
      SIFTransferOutcome::Completed);

    system.interruptController().setSource(EEInterruptSource::VIF0, true);
    system.interruptController().toggleMask(
      SBUS | EEInterruptSource::mask(EEInterruptSource::VIF0));
    system.iopInterruptController().assertSource(IOPInterruptSource::DMAC);
    system.iopInterruptController().writeMask(SBUS | 8 | 0x10);
    system.iopInterruptController().writeControl(1);
    system.iopTimers().writeTarget(0, 1);
    system.iopTimers().writeMode(0,
      IOPTimerMode::INTERRUPT_ON_TARGET | IOPTimerMode::REPEAT_INTERRUPT |
        IOPTimerMode::RESET_ON_TARGET);

    for (std::uint32_t index = 0; index < 4; ++index)
    {
      REQUIRE(system.eeMemorySystem().writeScratchpadDMA128(
        16 * index, {0x12340000 + index, 0x56780000 + index}) ==
        EEScratchpadAccessResult::Completed);
    }
    system.dmacController().signalChannelCompletion(2);
    system.dmacController().writeStatus(DMACStatus::CHANNEL_2_MASK);
    system.eeBus().write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
    system.eeBus().write32(EEMemoryMap::D8_MADR, 0x4000);
    system.eeBus().write32(EEMemoryMap::D8_QWC, 4);
    system.eeBus().write32(EEMemoryMap::D8_SADR, 0);
    system.eeBus().write32(EEMemoryMap::D8_CHCR, DMACChannelControl::START);
    system.eeCore().startExecution(ENTRY);
    system.iopCore().startExecution(ENTRY);
    system.startTrace();
  }

  void requireState(
    const NekoSystem &system, const std::vector<std::uint8_t> &expected)
  {
    const bool equal = system.saveState() == expected;
    REQUIRE(equal);
  }

  void requireOnlyHaltFieldsChanged(
    const std::vector<std::uint8_t> &before,
    const NekoSystem &system,
    const std::string &statePath,
    const std::string &reasonPath)
  {
    const auto differences = diffSaveStates(
      inspectSaveState(before), inspectSaveState(system.saveState()));
    REQUIRE(differences.size() == 2);
    for (const auto &difference : differences)
    {
      INFO(formatSaveStateDifference(difference));
      REQUIRE((difference.path == statePath || difference.path == reasonPath));
    }
  }

  void requireOnlyCoreFieldsChanged(
    const std::vector<std::uint8_t> &before,
    const NekoSystem &system,
    const std::string &corePath)
  {
    const auto differences = diffSaveStates(
      inspectSaveState(before), inspectSaveState(system.saveState()));
    REQUIRE_FALSE(differences.empty());
    for (const auto &difference : differences)
    {
      INFO(formatSaveStateDifference(difference));
      REQUIRE(difference.path.compare(0, corePath.size(), corePath) == 0);
    }
  }
}

TEST_CASE("Boot lifecycle whole reset clears live work but preserves shared ROM",
  "[system][bios][boot-lifecycle][reset]")
{
  NekoSystem system;
  prepareLiveMachine(system);
  system.clockMasterCycle();
  REQUIRE(system.eeCore().programCounter() == ENTRY + 4);
  REQUIRE(system.iopCore().delayedResultContinuation().source ==
    IOPDelayedResultSource::Load);
  REQUIRE(system.fromScratchpadDMAC().active());
  REQUIRE(system.fromScratchpadDMAC().quadwordCount() == 3);
  REQUIRE(system.iopTimers().counter(0).pulseReleasePending);
  REQUIRE(system.iopTimerTiming().pixelPhase() != 0);
  REQUIRE(system.interruptController().status() != 0);
  REQUIRE(system.iopInterruptController().status() != 0);
  REQUIRE(system.sif().freeWordCount() < SIF::WORD_CAPACITY);
  REQUIRE(system.sif().sif0PackingCursor() == 1);
  REQUIRE_FALSE(system.trace().empty());
  SECTION("both CPUs running") {}
  SECTION("EE host halted") { system.eeCore().haltExecution(); }
  SECTION("IOP host halted") { system.iopCore().haltExecution(); }
  SECTION("both CPUs host halted")
  {
    system.eeCore().haltExecution();
    system.iopCore().haltExecution();
  }

  const auto identity = system.eeBus().bootROMIdentity();
  NekoSystem fresh;
  fresh.installBootROM(bootROM());
  fresh.reset();
  const auto coldState = fresh.saveState();
  system.reset();
  requireState(system, coldState);
  REQUIRE(system.eeBus().bootROMIdentity() == identity);
  REQUIRE(system.iopBus().romIdentity() == identity);
  REQUIRE(system.eeCore().executionState() == EEExecutionState::Halted);
  REQUIRE(system.eeCore().programCounter() == EEReset::VECTOR);
  REQUIRE(system.iopCore().executionState() == IOPExecutionState::Halted);
  REQUIRE(system.iopCore().programCounter() == IOPReset::VECTOR);
  REQUIRE(system.masterClockScheduler().currentCycle() == 0);
  REQUIRE(system.trace().empty());
  REQUIRE_FALSE(system.traceEnabled());
  system.reset();
  requireState(system, coldState);
  REQUIRE(system.eeBus().bootROMIdentity() == identity);
  REQUIRE(system.iopBus().romIdentity() == identity);

  system.runMasterCycles(8);
  fresh.runMasterCycles(8);
  requireState(system, fresh.saveState());
  REQUIRE(system.dmacController().status() == 0);
  REQUIRE(system.interruptController().status() == 0);
  REQUIRE(system.iopInterruptController().status() == 0);
  REQUIRE(system.eeBus().read32(0x4000) == 0);
  REQUIRE(system.eeBus().writeData32(EEMemoryMap::SIF_BASE + 0x40, 0x40000));
  REQUIRE(system.iopBus().write32(IOPBus::SIF_DOORBELL, SBUS).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.interruptController().status() == SBUS);
  REQUIRE(system.iopInterruptController().status() == SBUS);
  system.eeCore().startExecution(EEReset::VECTOR);
  system.iopCore().startExecution(IOPReset::VECTOR);
  system.clockMasterCycle();
  REQUIRE(system.eeCore().generalRegister(7).low == 0x77);
  REQUIRE(system.iopCore().generalRegister(7) == 0x77);
}

TEST_CASE("Boot lifecycle reset preserves independent external ROM configurations",
  "[system][bios][boot-lifecycle][reset]")
{
  NekoSystem system;
  SECTION("neither ROM") {}
  SECTION("EE ROM only") { system.eeBus().installBootROM(bootROM()); }
  SECTION("IOP ROM only")
  {
    REQUIRE(system.iopBus().installRom(BootROMImage::create(bootROM())));
  }
  SECTION("one shared ROM") { system.installBootROM(bootROM()); }
  SECTION("distinct ROMs")
  {
    system.eeBus().installBootROM(bootROM());
    auto other = bootROM();
    other.back() = 0xcd;
    REQUIRE(system.iopBus().installRom(BootROMImage::create(other)));
  }
  const auto eeROM = system.eeBus().bootROMIdentity();
  const auto iopROM = system.iopBus().romIdentity();
  system.runMasterCycles(3);
  system.reset();
  REQUIRE(system.eeBus().bootROMIdentity() == eeROM);
  REQUIRE(system.iopBus().romIdentity() == iopROM);
  REQUIRE(system.masterClockScheduler().currentCycle() == 0);
  REQUIRE(system.eeCore().executionState() == EEExecutionState::Halted);
  REQUIRE(system.iopCore().executionState() == IOPExecutionState::Halted);
  std::uint8_t eeByte = 0x55;
  REQUIRE(system.eeBus().readData8(EEMemoryMap::BOOT_ROM_END - 1, &eeByte) ==
    (eeROM != nullptr));
  REQUIRE(eeByte == (eeROM != nullptr ? 0xab : 0x55));
  const auto iopByte = system.iopBus().read8(
    IOPBus::ROM_BASE + static_cast<IOPAddress>(IOPBus::ROM_SIZE) - 1);
  if (iopROM != nullptr)
  {
    REQUIRE(iopByte.status == IOPBusStatus::Completed);
    REQUIRE(iopByte.value == (*iopROM)[BootROMImage::SIZE - 1]);
  }
  else
  {
    REQUIRE(iopByte.status == IOPBusStatus::Unmapped);
  }
}

TEST_CASE("Boot lifecycle EE halt and exact PC resume affect only host execution state",
  "[system][bios][boot-lifecycle][halt]")
{
  NekoSystem system;
  prepareLiveMachine(system);
  system.clockMasterCycle();
  const auto before = system.saveState();
  const auto snapshot = inspectSaveState(before);
  const auto *branch = snapshot.find("system.eeCore.branchDelayPending");
  REQUIRE(branch != nullptr);
  REQUIRE(branch->scalarValue == 1);
  const auto identity = system.eeBus().bootROMIdentity();
  const auto traceHash = system.traceHash();
  const auto traceSize = system.trace().size();
  const auto pc = system.eeCore().programCounter();
  system.eeCore().haltExecution();
  REQUIRE(system.eeCore().executionState() == EEExecutionState::Halted);
  REQUIRE(system.eeCore().stopReason() == EEStopReason::HostHalt);
  requireOnlyHaltFieldsChanged(before, system,
    "system.eeCore.state", "system.eeCore.haltReason");
  system.eeCore().startExecution(pc);
  requireState(system, before);
  REQUIRE(system.eeBus().bootROMIdentity() == identity);
  REQUIRE(system.iopBus().romIdentity() == identity);
  REQUIRE(system.traceHash() == traceHash);
  REQUIRE(system.trace().size() == traceSize);
  REQUIRE(system.traceEnabled());
  system.clockMasterCycle();
  REQUIRE(system.eeCore().generalRegister(2).low ==
    system.eeCore().generalRegister(31).low);
  REQUIRE(static_cast<std::uint32_t>(system.eeCore().generalRegister(2).low) ==
    ENTRY + 8);
  REQUIRE(system.eeCore().generalRegister(4).low == 0);
  REQUIRE(system.eeCore().programCounter() == ENTRY + 0x10);
}

TEST_CASE("Boot lifecycle IOP halt and resume preserve load and branch continuations",
  "[system][bios][boot-lifecycle][halt]")
{
  for (const unsigned checkpoint : {1u, 9u, 17u})
  {
    CAPTURE(checkpoint);
    NekoSystem system;
    prepareLiveMachine(system);
    system.runMasterCycles(checkpoint);
    if (checkpoint == 9)
    {
      REQUIRE(system.iopCore().branchContinuation().active);
      REQUIRE(system.iopCore().branchContinuation().targetAddress == ENTRY + 0x10);
    }
    else
    {
      REQUIRE(system.iopCore().delayedResultContinuation().source ==
        IOPDelayedResultSource::Load);
      REQUIRE(system.iopCore().delayedResultContinuation().value == LOAD_VALUE);
    }
    const auto before = system.saveState();
    const auto identity = system.iopBus().romIdentity();
    const auto traceHash = system.traceHash();
    const auto traceSize = system.trace().size();
    system.iopCore().haltExecution();
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Halted);
    REQUIRE(system.iopCore().stopReason() == IOPStopReason::HostHalt);
    requireOnlyHaltFieldsChanged(before, system,
      "system.iop.core.state", "system.iop.core.stopReason");
    system.iopCore().resumeExecution();
    requireState(system, before);
    REQUIRE(system.eeBus().bootROMIdentity() == identity);
    REQUIRE(system.iopBus().romIdentity() == identity);
    REQUIRE(system.traceHash() == traceHash);
    REQUIRE(system.trace().size() == traceSize);
    REQUIRE(system.traceEnabled());
    system.runMasterCycles(41 - checkpoint);
    REQUIRE(system.iopCore().generalRegister(2) == LOAD_VALUE);
    REQUIRE(system.iopCore().generalRegister(3) == LOAD_VALUE);
    REQUIRE(system.iopCore().generalRegister(4) == 0);
    REQUIRE(system.iopCore().generalRegister(5) == 0);
    REQUIRE(system.iopCore().generalRegister(6) == LOAD_VALUE);
    REQUIRE(system.iopCore().retiredInstructions() == 6);
  }
}

TEST_CASE("Boot lifecycle EE different PC restart is not a guest warm reset",
  "[system][bios][boot-lifecycle][restart]")
{
  NekoSystem system;
  prepareLiveMachine(system);
  system.clockMasterCycle();
  const auto before = system.saveState();
  system.eeCore().haltExecution();
  system.eeCore().startExecution(ENTRY + 0x10);
  requireOnlyCoreFieldsChanged(before, system, "system.eeCore.");
  REQUIRE(system.eeCore().elapsedCycles() == 1);
  system.clockMasterCycle();
  REQUIRE(system.eeCore().generalRegister(2).low == 0);
  REQUIRE(system.eeCore().generalRegister(3).low == 0x33);
}

TEST_CASE("Boot lifecycle IOP start is a core local restart even at the same PC",
  "[system][bios][boot-lifecycle][restart]")
{
  NekoSystem system;
  prepareLiveMachine(system);
  system.clockMasterCycle();
  const auto before = system.saveState();
  system.iopCore().haltExecution();
  system.iopCore().startExecution(system.iopCore().programCounter());
  requireOnlyCoreFieldsChanged(before, system, "system.iop.core.");
  REQUIRE(system.iopCore().elapsedCycles() == 1);
  REQUIRE(system.iopCore().retiredInstructions() == 1);
  REQUIRE(system.iopCore().generalRegister(2) == 0);
  REQUIRE(system.iopCore().delayedResultContinuation().source ==
    IOPDelayedResultSource::None);
  system.runMasterCycles(8);
  REQUIRE(system.iopCore().generalRegister(2) == 0);
}

TEST_CASE("Boot lifecycle unsupported guest mode commands cannot reset or signal a peer",
  "[system][bios][boot-lifecycle][sif]")
{
  NekoSystem system;
  prepareLiveMachine(system);
  system.clockMasterCycle();
  system.iopInterruptController().writeStatus(~SBUS);
  REQUIRE((system.iopInterruptController().status() & SBUS) == 0);
  const auto before = system.saveState();
  const auto traceHash = system.traceHash();
  const auto traceSize = system.trace().size();
  for (const std::uint32_t command : {0x80000u, 0xc0000u, 0xc0100u, 0xffffffffu})
  {
    CAPTURE(command);
    REQUIRE_FALSE(system.eeBus().writeData32(EEMemoryMap::SIF_BASE + 0x40, command));
    requireState(system, before);
    REQUIRE_THROWS_WITH(
      system.eeBus().write32(EEMemoryMap::SIF_BASE + 0x40, command),
      "Unsupported SIF CTRL PS1-mode operation.");
    requireState(system, before);
    REQUIRE(system.traceHash() == traceHash);
    REQUIRE(system.trace().size() == traceSize);
    REQUIRE(system.traceEnabled());
  }
}
