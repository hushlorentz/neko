#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "catch.hpp"
#include "iop/iop_trace_test_utils.hpp"
#include "startup_diagnostics.hpp"

namespace
{
  constexpr std::uint32_t ENTRY = 0xa0001000;
  constexpr std::uint32_t LOAD_VALUE = 0x81234567;
  constexpr std::uint64_t BUDGET = 65;

  enum class StoppedCore
  {
    EEHostHalt,
    IOPHostHalt,
    EEFailure,
    IOPFailure
  };

  std::vector<std::uint8_t> bootROM()
  {
    std::vector<std::uint8_t> image(BootROMImage::SIZE, 0);
    image[0] = 0x55;
    image[2] = 0x07;
    image[3] = 0x24; // addiu r7, r0, 0x55
    image.back() = 0xa5;
    return image;
  }

  std::string startupRecord(const StartupSummary &summary)
  {
    std::ostringstream output;
    writeStartupSummaryJsonLine(output, summary);
    return output.str();
  }

  void prepareStartupWork(
    NekoSystem &system, const std::vector<std::uint8_t> &image)
  {
    system.runDualCPUBIOS(image, 0);
    const std::uint32_t eeProgram[] = {
      0x0c000404, // jal 0x1010
      0x37e90000, // ori r9, r31, 0 (link dependency prevents pairing)
      0x3404dead, // skipped
      0,
      0x3c01b000, // lui r1, 0xb000
      0x3421f240, // ori r1, r1, 0xf240
      0x3c030004, // lui r3, 4
      0x34630100, // ori r3, r3, 0x100
      0xac230000  // sw r3, 0(r1): request IOP SBUS
    };
    const std::uint32_t iopProgram[] = {
      0x8c092000, // lw r9, 0x2000(r0)
      0x10000002, // beq r0, r0, 0x1010
      0x8c0a2000, // lw r10, 0x2000(r0) (delay slot)
      0x3404dead, // skipped
      0x3c01bf80, // lui r1, 0xbf80
      0x34211450, // ori r1, r1, 0x1450
      0x34030002, // ori r3, r0, 2
      0xac230000  // sw r3, 0(r1): request EE SBUS
    };
    for (std::uint32_t index = 0; index < 128; ++index)
    {
      system.eeBus().write32(0x1000 + 4 * index,
        index < 9 ? eeProgram[index] : 0x24420001);
      REQUIRE(system.iopBus().write32(0x1000 + 4 * index,
        index < 8 ? iopProgram[index] : 0x24420001).status ==
        IOPBusStatus::Completed);
    }
    REQUIRE(system.iopBus().write32(0x2000, LOAD_VALUE).status ==
      IOPBusStatus::Completed);

    REQUIRE(system.eeBus().writeData32(EEMemoryMap::SIF_BASE, 0x12345678));
    REQUIRE(system.iopBus().write32(IOPBus::SIF_BASE + 0x10, 0x87654321).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.sif().pushSIF0Prologue({{1, 2, 3, 4}}) ==
      SIFTransferOutcome::Completed);
    REQUIRE(system.sif().pushSIF0PayloadWord(5, SIFPayloadBoundary::Continuing) ==
      SIFTransferOutcome::Completed);
    REQUIRE(system.sif().pushSIF1Quadword({{6, 7, 8, 9}}) ==
      SIFTransferOutcome::Completed);
    system.interruptController().toggleMask(2);
    system.iopInterruptController().writeMask(0x12);
    system.iopInterruptController().writeControl(1);
    system.iopTimers().writeTarget(0, 2);
    system.iopTimers().writeMode(0,
      IOPTimerMode::INTERRUPT_ON_TARGET | IOPTimerMode::REPEAT_INTERRUPT |
        IOPTimerMode::RESET_ON_TARGET);

    for (std::uint32_t index = 0; index < 32; ++index)
    {
      REQUIRE(system.eeMemorySystem().writeScratchpadDMA128(
        16 * index, {0x12340000 + index, 0x56780000 + index}) ==
        EEScratchpadAccessResult::Completed);
    }
    system.fromScratchpadDMAC().writeMemoryAddress(0x4000);
    system.fromScratchpadDMAC().writeScratchpadAddress(0);
    system.fromScratchpadDMAC().writeQuadwordCount(32);
    system.fromScratchpadDMAC().writeChannelControl(DMACChannelControl::START);
    system.dmacController().writeStatus(DMACStatus::CHANNEL_8_MASK);
    system.gifDMAC().writeMemoryAddress(0x6000);
    system.gifDMAC().writeQuadwordCount(1);
    system.gifDMAC().writeChannelControl(DMACChannelControl::START);
    system.eeBus().write32(EEMemoryMap::GIF_MODE, GIFMode::M3R);
    system.dmacController().writeControl(DMACControl::DMA_ENABLE);
    system.eeCore().startExecution(ENTRY);
    system.iopCore().startExecution(ENTRY);
    system.startTrace();
  }

  void stopCore(NekoSystem &system, StoppedCore stopped)
  {
    switch (stopped)
    {
      case StoppedCore::EEHostHalt:
        REQUIRE(system.runDualCPU(1).cycleLimitReached);
        system.eeCore().haltExecution();
        break;
      case StoppedCore::IOPHostHalt:
        REQUIRE(system.runDualCPU(1).cycleLimitReached);
        system.iopCore().haltExecution();
        break;
      case StoppedCore::EEFailure:
        system.eeBus().write32(0x1000, 0x40003800);
        REQUIRE_FALSE(system.runDualCPU(1).cycleLimitReached);
        REQUIRE(system.eeCore().stopReason() == EEStopReason::UnsupportedInstruction);
        break;
      case StoppedCore::IOPFailure:
        REQUIRE(system.iopBus().write32(0x1000, 0x3c011000).status ==
          IOPBusStatus::Completed);
        REQUIRE(system.iopBus().write32(0x1004, 0xac200000).status ==
          IOPBusStatus::Completed);
        REQUIRE_FALSE(system.runDualCPU(9).cycleLimitReached);
        REQUIRE(system.iopCore().stopReason() == IOPStopReason::StoreFailure);
        break;
    }
    REQUIRE(system.masterClockScheduler().currentCycle() ==
      (stopped == StoppedCore::IOPFailure ? 9 : 1));
  }

  void requireCompletedDevices(const NekoSystem &system)
  {
    REQUIRE(system.masterClockScheduler().currentCycle() == BUDGET);
    REQUIRE(system.iopTimers().counter(2).count == 9);
    REQUIRE(system.iopTimers().counter(0).count == 1);
    REQUIRE((system.iopInterruptController().status() & 0x10) != 0);
    const auto summary = inspectStartup(system);
    REQUIRE(summary.iopInterrupts.control == 1);
    REQUIRE(summary.sif.mscom == 0x12345678);
    REQUIRE(summary.sif.smcom == 0x87654321);
    REQUIRE(summary.sif.sif0Words == 5);
    REQUIRE(summary.sif.sif1Words == 4);
    REQUIRE(summary.sif.freeWords == 23);
    REQUIRE(summary.sif.packingCursor == 1);
    REQUIRE(summary.dmac.interruptPending);
    REQUIRE((summary.dmac.status & DMACStatus::CHANNEL_8) != 0);
    REQUIRE(summary.dmac.channels[2].quadwordCount == 0);
    REQUIRE_FALSE(summary.dmac.channels[2].clockEligible);
    REQUIRE(summary.dmac.channels[1].quadwordCount == 1);
    REQUIRE(summary.dmac.channels[1].clockEligible);
    REQUIRE(summary.dmac.channels[1].lastTransferStalled);
    for (std::uint32_t index = 0; index < 32; ++index)
    {
      const auto value = system.eeBus().readQuadword(0x4000 + 16 * index);
      REQUIRE(value[0] == 0x12340000 + index);
      REQUIRE(value[1] == 0);
      REQUIRE(value[2] == 0x56780000 + index);
      REQUIRE(value[3] == 0);
    }
  }

  void requireMatchingStartup(const NekoSystem &actual, const NekoSystem &expected)
  {
    INFO("Comparing startup at master cycle " <<
      actual.masterClockScheduler().currentCycle());
    IOPTraceTest::requireMachine(actual, expected);
    IOPTraceTest::requireEvents(actual.trace(), expected.trace());
    REQUIRE(startupRecord(inspectStartup(actual)) ==
      startupRecord(inspectStartup(expected)));
  }
}

TEST_CASE("Boot conformance stopped cores retain work while peer and devices advance",
  "[system][bios][boot-conformance]")
{
  const auto image = bootROM();
  for (const auto stopped : {
    StoppedCore::EEHostHalt, StoppedCore::IOPHostHalt,
    StoppedCore::EEFailure, StoppedCore::IOPFailure})
  {
    CAPTURE(static_cast<int>(stopped));
    NekoSystem original;
    prepareStartupWork(original, image);
    stopCore(original, stopped);
    const auto boundary = inspectStartup(original);
    const auto state = original.saveState();
    NekoSystem restored;
    restored.installBootROM(image);
    restored.startTrace();
    restored.loadState(state);
    // Load preserves destination observers; align both suffix baselines.
    restored.clearTrace();
    original.clearTrace();
    requireMatchingStartup(restored, original);
    for (const std::uint64_t budget : {0, 1, 64})
    {
      const auto stoppedRun = runDualCPUWithStartupSummary(original, budget);
      REQUIRE(stoppedRun.execution.masterCycles == 0);
      REQUIRE_FALSE(stoppedRun.execution.cycleLimitReached);
      REQUIRE(stoppedRun.execution.ee.eeCycles == 0);
      REQUIRE(stoppedRun.execution.iop.iopCycles == 0);
      REQUIRE(startupRecord(stoppedRun.startup) == startupRecord(boundary));
      const bool unchanged = original.saveState() == state;
      REQUIRE(unchanged);
      REQUIRE(original.trace().empty());
    }

    original.runMasterCycles(BUDGET - boundary.masterCycle);
    restored.runMasterCycles(BUDGET - boundary.masterCycle);
    requireMatchingStartup(restored, original);
    requireCompletedDevices(original);
    const auto progressed = inspectStartup(original);
    if (stopped == StoppedCore::EEHostHalt || stopped == StoppedCore::EEFailure)
    {
      REQUIRE(progressed.ee.cycles == boundary.ee.cycles);
      REQUIRE(progressed.ee.programCounter == boundary.ee.programCounter);
      REQUIRE(progressed.ee.state == EEExecutionState::Halted);
      REQUIRE(progressed.ee.stopReason == boundary.ee.stopReason);
      REQUIRE(progressed.ee.rejectedInstruction == boundary.ee.rejectedInstruction);
      REQUIRE(progressed.iop.cycles == 9);
      REQUIRE(progressed.iop.retiredInstructions == 9);
      REQUIRE(original.iopCore().generalRegister(2) == 2);
      REQUIRE(progressed.eeInterrupts.pending);
    }
    else
    {
      REQUIRE(progressed.iop.cycles == boundary.iop.cycles);
      REQUIRE(progressed.iop.retiredInstructions == boundary.iop.retiredInstructions);
      REQUIRE(progressed.iop.programCounter == boundary.iop.programCounter);
      REQUIRE(progressed.iop.state == IOPExecutionState::Halted);
      REQUIRE(progressed.iop.stopReason == boundary.iop.stopReason);
      REQUIRE(progressed.iop.storeFailure.width == boundary.iop.storeFailure.width);
      REQUIRE(progressed.iop.storeFailure.access.virtualAddress ==
        boundary.iop.storeFailure.access.virtualAddress);
      REQUIRE(progressed.ee.cycles == BUDGET);
      REQUIRE(original.eeCore().generalRegister(2).low > 0);
      REQUIRE((progressed.iopInterrupts.status & 2) != 0);
    }

    original.dmacController().writeStatus(DMACStatus::CHANNEL_8);
    restored.dmacController().writeStatus(DMACStatus::CHANNEL_8);
    original.runMasterCycles(8);
    restored.runMasterCycles(8);
    REQUIRE((original.dmacController().status() & DMACStatus::CHANNEL_8) == 0);
    if (stopped == StoppedCore::EEHostHalt)
    {
      REQUIRE(original.eeCore().generalRegister(9).low == 0);
      original.eeCore().startExecution(boundary.ee.programCounter);
      restored.eeCore().startExecution(boundary.ee.programCounter);
      REQUIRE(original.runDualCPU(16).cycleLimitReached);
      REQUIRE(restored.runDualCPU(16).cycleLimitReached);
      REQUIRE(original.eeCore().generalRegister(9).low == ENTRY + 8);
      REQUIRE(original.eeCore().generalRegister(4).low == 0);
    }
    else if (stopped == StoppedCore::IOPHostHalt)
    {
      REQUIRE(original.iopCore().generalRegister(9) == 0);
      original.iopCore().resumeExecution();
      restored.iopCore().resumeExecution();
      REQUIRE(original.runDualCPU(16).cycleLimitReached);
      REQUIRE(restored.runDualCPU(16).cycleLimitReached);
      REQUIRE(original.iopCore().generalRegister(9) == LOAD_VALUE);
      REQUIRE(original.iopCore().generalRegister(10) == 0);
      REQUIRE(original.iopCore().delayedResultContinuation().source ==
        IOPDelayedResultSource::Load);
      REQUIRE(original.runDualCPU(8).cycleLimitReached);
      REQUIRE(restored.runDualCPU(8).cycleLimitReached);
      REQUIRE(original.iopCore().generalRegister(10) == LOAD_VALUE);
      REQUIRE(original.iopCore().generalRegister(4) == 0);
    }
    requireMatchingStartup(restored, original);
  }
}

TEST_CASE("Boot conformance split budgets preserve composed state and event order",
  "[system][bios][boot-conformance]")
{
  const auto image = bootROM();
  NekoSystem whole;
  NekoSystem split;
  prepareStartupWork(whole, image);
  prepareStartupWork(split, image);
  const auto uninterrupted = runDualCPUWithStartupSummary(whole, BUDGET);
  std::uint64_t masterCycles = 0;
  std::uint64_t eeInstructions = 0;
  std::uint64_t iopInstructions = 0;
  std::uint64_t iopCycles = 0;
  for (const std::uint64_t budget : {0, 1, 6, 1, 1, 7, 1, 14, 1, 1, 31, 1})
  {
    CAPTURE(budget);
    const auto part = runDualCPUWithStartupSummary(split, budget);
    masterCycles += budget;
    eeInstructions += part.execution.ee.instructions;
    iopInstructions += part.execution.iop.instructions;
    iopCycles += part.execution.iop.iopCycles;
    REQUIRE(part.execution.masterCycles == budget);
    REQUIRE(part.execution.ee.eeCycles == budget);
    REQUIRE(part.execution.cycleLimitReached);
    REQUIRE(part.startup.masterCycle == masterCycles);
    REQUIRE(part.startup.ee.state == EEExecutionState::Running);
    REQUIRE(part.startup.iop.state == IOPExecutionState::Running);
    REQUIRE(part.startup.iop.cycles == (masterCycles + 7) / 8);
  }
  REQUIRE(masterCycles == BUDGET);
  REQUIRE(eeInstructions == uninterrupted.execution.ee.instructions);
  REQUIRE(iopInstructions == uninterrupted.execution.iop.instructions);
  REQUIRE(iopCycles == 9);
  requireMatchingStartup(split, whole);
  requireCompletedDevices(split);
  REQUIRE(split.eeCore().generalRegister(9).low == ENTRY + 8);
  REQUIRE(split.iopCore().generalRegister(9) == LOAD_VALUE);
  REQUIRE(split.iopCore().generalRegister(10) == LOAD_VALUE);
  REQUIRE(split.eeCore().generalRegister(4).low == 0);
  REQUIRE(split.iopCore().generalRegister(4) == 0);
  REQUIRE((split.interruptController().status() & 2) != 0);
  REQUIRE((split.iopInterruptController().status() & 2) != 0);
}

TEST_CASE("Boot conformance live restore preserves continuation and pending device work",
  "[system][bios][boot-conformance][save-state]")
{
  const auto image = bootROM();
  for (const std::uint64_t checkpoint : {1, 7, 8, 9, 16, 17, 31, 32, 33})
  {
    CAPTURE(checkpoint);
    NekoSystem original;
    prepareStartupWork(original, image);
    REQUIRE(original.runDualCPU(checkpoint).cycleLimitReached);
    const auto boundary = inspectStartup(original);
    REQUIRE(boundary.sif.packingCursor == 1);
    REQUIRE(boundary.dmac.channels[1].lastTransferStalled);
    REQUIRE(boundary.dmac.channels[2].quadwordCount ==
      (checkpoint < 32 ? 32 - checkpoint : 0));
    if (checkpoint == 1)
    {
      REQUIRE(original.eeCore().programCounter() == ENTRY + 4);
      REQUIRE(original.eeCore().generalRegister(9).low == 0);
      REQUIRE(original.iopCore().delayedResultContinuation().source ==
        IOPDelayedResultSource::Load);
    }
    if (checkpoint == 9 || checkpoint == 16)
    {
      REQUIRE(original.iopCore().branchContinuation().active);
    }
    if (checkpoint == 17)
    {
      REQUIRE(original.iopCore().delayedResultContinuation().source ==
        IOPDelayedResultSource::Load);
      REQUIRE(original.iopCore().generalRegister(10) == 0);
    }
    NekoSystem restored;
    restored.installBootROM(image);
    restored.startTrace();
    restored.loadState(original.saveState());
    restored.clearTrace();
    original.clearTrace();
    requireMatchingStartup(restored, original);
    REQUIRE(original.runDualCPU(BUDGET - checkpoint).cycleLimitReached);
    const auto continued = runDualCPUWithStartupSummary(restored, BUDGET - checkpoint);
    REQUIRE(continued.execution.masterCycles == BUDGET - checkpoint);
    REQUIRE(continued.execution.cycleLimitReached);
    REQUIRE(startupRecord(continued.startup) ==
      startupRecord(inspectStartup(original)));
    requireMatchingStartup(restored, original);
    requireCompletedDevices(restored);
    REQUIRE(restored.eeCore().generalRegister(9).low == ENTRY + 8);
    REQUIRE(restored.iopCore().generalRegister(9) == LOAD_VALUE);
    REQUIRE(restored.iopCore().generalRegister(10) == LOAD_VALUE);
  }
}

TEST_CASE("Boot conformance repeated cold startup discards stopped live machine state",
  "[system][bios][boot-conformance][reset]")
{
  const auto image = bootROM();
  NekoSystem reused;
  struct ColdStartCase
  {
    StoppedCore stopped;
    std::uint64_t budget;
  };
  for (const auto scenario : {
    ColdStartCase{StoppedCore::EEHostHalt, 0},
    ColdStartCase{StoppedCore::IOPHostHalt, 1},
    ColdStartCase{StoppedCore::EEFailure, 17},
    ColdStartCase{StoppedCore::IOPFailure, BUDGET}})
  {
    CAPTURE(static_cast<int>(scenario.stopped));
    CAPTURE(scenario.budget);
    prepareStartupWork(reused, image);
    stopCore(reused, scenario.stopped);
    if (reused.eeCore().executionState() == EEExecutionState::Running)
    {
      reused.eeCore().haltExecution();
    }
    if (reused.iopCore().executionState() == IOPExecutionState::Running)
    {
      reused.iopCore().haltExecution();
    }
    NekoInputState input;
    input.buttons = NekoButton::START;
    reused.setInput(input);
    REQUIRE_FALSE(reused.trace().empty());
    REQUIRE(inspectStartup(reused).dmac.channels[2].quadwordCount != 0);
    const auto restarted = runDualCPUBIOSWithStartupSummary(
      reused, image, scenario.budget);
    NekoSystem fresh;
    fresh.runDualCPUBIOS(image, scenario.budget);
    requireMatchingStartup(reused, fresh);
    REQUIRE(restarted.execution.masterCycles == scenario.budget);
    REQUIRE(restarted.execution.cycleLimitReached);
    REQUIRE(restarted.startup.masterCycle == scenario.budget);
    REQUIRE(restarted.startup.sif.freeWords == 32);
    REQUIRE(restarted.startup.sif.packingCursor == 0);
    REQUIRE_FALSE(restarted.startup.dmac.channels[1].clockEligible);
    REQUIRE_FALSE(restarted.startup.dmac.channels[2].clockEligible);
    REQUIRE_FALSE(restarted.startup.dmac.interruptPending);
    REQUIRE_FALSE(restarted.startup.eeInterrupts.pending);
    REQUIRE_FALSE(restarted.startup.iopInterrupts.pending);
    REQUIRE(restarted.startup.iop.storeFailure.width == IOPStoreWidth::None);
    REQUIRE(reused.eeBus().bootROMIdentity() == reused.iopBus().romIdentity());
    REQUIRE(reused.eeCore().generalRegister(7).low ==
      (scenario.budget == 0 ? 0 : 0x55));
    REQUIRE(reused.iopCore().generalRegister(7) ==
      (scenario.budget == 0 ? 0 : 0x55));
    REQUIRE_FALSE(reused.traceEnabled());
    REQUIRE(reused.trace().empty());
    reused.reset();
    fresh.reset();
    requireMatchingStartup(reused, fresh);
  }
}
