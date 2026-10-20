#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "catch.hpp"
#include "startup_diagnostics.hpp"

namespace
{
  std::vector<std::uint8_t> startupROM(std::uint32_t first = 0x24420001)
  {
    std::vector<std::uint8_t> image(BootROMImage::SIZE, 0);
    for (std::size_t offset = 0; offset < 1024; offset += 4)
    {
      for (std::size_t byte = 0; byte < 4; ++byte)
      {
        image[offset + byte] =
          static_cast<std::uint8_t>(first >> (8 * byte));
      }
    }
    return image;
  }

  void requireSameState(const NekoSystem &left, const NekoSystem &right)
  {
    const bool equal = left.saveState() == right.saveState();
    REQUIRE(equal);
    REQUIRE(left.traceEnabled() == right.traceEnabled());
    REQUIRE(left.trace().size() == right.trace().size());
    REQUIRE(left.traceHash() == right.traceHash());
  }
}

TEST_CASE("Startup inspection preserves live state including side-effecting registers",
  "[startup-diagnostics]")
{
  NekoSystem system;
  system.runDualCPUBIOS(startupROM(), 3);
  system.startTrace();
  system.interruptController().setSource(EEInterruptSource::SBUS, true);
  system.interruptController().toggleMask(2);
  system.iopInterruptController().assertSource(IOPInterruptSource::SBUS);
  system.iopInterruptController().writeMask(2);
  system.iopInterruptController().writeControl(1);
  REQUIRE(system.sif().writeEE(SIFRegister::MSCOM, 0x1234).outcome ==
    SIFWriteOutcome::Completed);
  REQUIRE(system.sif().writeIOP(SIFRegister::SMCOM, 0x5678).outcome ==
    SIFWriteOutcome::Completed);
  system.sif().writeEE(SIFRegister::MSFLAG, 0x12);
  system.sif().writeIOP(SIFRegister::SMFLAG, 0x34);
  system.sif().writeEE(SIFRegister::CTRL, 0x100);
  system.sif().writeIOPDoorbell(0x80);
  REQUIRE(system.sif().pushSIF1Quadword({{1, 2, 3, 4}}) ==
    SIFTransferOutcome::Completed);
  REQUIRE(system.sif().pushSIF0Prologue({{5, 6, 7, 8}}) ==
    SIFTransferOutcome::Completed);
  REQUIRE(system.sif().pushSIF0PayloadWord(9, SIFPayloadBoundary::Continuing) ==
    SIFTransferOutcome::Completed);

  system.dmacController().signalChannelCompletion(DMACStatus::CHANNEL_8);
  system.dmacController().writeStatus(DMACStatus::CHANNEL_8_MASK);
  system.dmacController().writeInterleaveSize(0x00020003);
  system.fromScratchpadDMAC().writeMemoryAddress(0x4000);
  system.fromScratchpadDMAC().writeScratchpadAddress(0x20);
  system.fromScratchpadDMAC().writeQuadwordCount(4);
  system.fromScratchpadDMAC().writeChannelControl(DMACChannelControl::START);

  const auto before = system.saveState();
  const auto hash = system.traceHash();
  const auto traceSize = system.trace().size();
  const NekoSystem &readOnly = system;
  for (int repeat = 0; repeat < 2; ++repeat)
  {
    const auto summary = inspectStartup(readOnly);
    REQUIRE(summary.masterCycle == 3);
    REQUIRE(summary.ee.cycles == system.eeCore().elapsedCycles());
    REQUIRE(summary.ee.programCounter == system.eeCore().programCounter());
    REQUIRE(summary.ee.state == EEExecutionState::Running);
    REQUIRE(summary.iop.cycles == system.iopCore().elapsedCycles());
    REQUIRE(summary.iop.retiredInstructions == 1);
    REQUIRE(summary.iop.programCounter == system.iopCore().programCounter());
    REQUIRE(summary.iop.state == IOPExecutionState::Running);
    REQUIRE(summary.sif.mscom == 0x1234);
    REQUIRE(summary.sif.smcom == 0x5678);
    REQUIRE(summary.sif.msflag == 0x12);
    REQUIRE(summary.sif.smflag == 0x34);
    REQUIRE(summary.sif.control == system.sif().control());
    REQUIRE(summary.sif.iopDoorbell == 0x80);
    REQUIRE(summary.sif.sif0Words == 5);
    REQUIRE(summary.sif.sif1Words == 4);
    REQUIRE(summary.sif.freeWords == 23);
    REQUIRE(summary.sif.packingCursor == 1);
    REQUIRE(summary.eeInterrupts.status == 2);
    REQUIRE(summary.eeInterrupts.mask == 2);
    REQUIRE(summary.eeInterrupts.pending);
    REQUIRE(summary.iopInterrupts.status == 2);
    REQUIRE(summary.iopInterrupts.mask == 2);
    REQUIRE(summary.iopInterrupts.control == 1);
    REQUIRE(summary.iopInterrupts.pending);
    REQUIRE(summary.dmac.status ==
      (DMACStatus::CHANNEL_8 | DMACStatus::CHANNEL_8_MASK));
    REQUIRE(summary.dmac.interleaveSize == 0x00020003);
    REQUIRE(summary.dmac.interruptPending);
    REQUIRE(summary.dmac.channels.size() == 4);
    REQUIRE(summary.dmac.channels[0].channel == 1);
    REQUIRE(summary.dmac.channels[1].channel == 2);
    const auto &from = summary.dmac.channels[2];
    REQUIRE(from.channel == 8);
    REQUIRE(from.memoryAddress == 0x4000);
    REQUIRE(from.scratchpadAddress == 0x20);
    REQUIRE(from.quadwordCount == 4);
    REQUIRE(from.control == DMACChannelControl::START);
    REQUIRE_FALSE(from.clockEligible);
    REQUIRE_FALSE(from.lastTransferStalled);
    REQUIRE(summary.dmac.channels[3].channel == 9);
    std::ostringstream output;
    writeStartupSummaryJsonLine(output, summary);
    const bool unchanged = system.saveState() == before;
    REQUIRE(unchanged);
    REQUIRE(system.traceEnabled());
    REQUIRE(system.trace().size() == traceSize);
    REQUIRE(system.traceHash() == hash);
  }
}

TEST_CASE("Startup DMA summary separates controller gating from last transfer stalls",
  "[startup-diagnostics]")
{
  NekoSystem system;
  SECTION("PATH3 backpressure")
  {
    REQUIRE(system.eeBus().writeQuadword(0x1000, {}));
    system.gifDMAC().writeMemoryAddress(0x1000);
    system.gifDMAC().writeQuadwordCount(1);
    system.gifDMAC().writeTagAddress(0x2000);
    system.gifDMAC().writeChannelControl(DMACChannelControl::START);
    system.eeBus().write32(EEMemoryMap::GIF_MODE, GIFMode::M3R);
    system.dmacController().writeControl(DMACControl::DMA_ENABLE);
    system.clockMasterCycle();
    const auto snapshot = inspectStartup(system);
    REQUIRE(snapshot.dmac.channels[1].lastTransferStalled);
    REQUIRE(snapshot.dmac.channels[1].clockEligible);
    REQUIRE(snapshot.dmac.channels[1].tagAddress == 0x2000);
    system.dmacController().writeControl(0);
    system.eeBus().write32(EEMemoryMap::GIF_MODE, 0);
    const auto gated = inspectStartup(system);
    REQUIRE_FALSE(gated.dmac.channels[1].clockEligible);
    REQUIRE(gated.dmac.channels[1].lastTransferStalled);
    system.dmacController().writeControl(DMACControl::DMA_ENABLE);
    system.clockMasterCycle();
    REQUIRE_FALSE(inspectStartup(system).dmac.channels[1].lastTransferStalled);
  }
  SECTION("VIF1 backpressure")
  {
    REQUIRE(system.eeBus().writeGuestData128(EEMemoryMap::VIF1_FIFO,
      {UINT64_C(0x0000000080000000), 0}) == EEDataWriteResult::Completed);
    system.eeBus().advanceGuestFIFOs();
    for (std::size_t index = 0; index < 15; ++index)
    {
      REQUIRE(system.eeBus().writeGuestData128(EEMemoryMap::VIF1_FIFO, {}) ==
        EEDataWriteResult::Completed);
    }
    REQUIRE(system.vif1().fifoQuadwordCount() == 16);
    REQUIRE(system.eeBus().writeQuadword(0x1000, {}));
    system.vif1DMAC().writeMemoryAddress(0x1000);
    system.vif1DMAC().writeQuadwordCount(1);
    system.vif1DMAC().writeChannelControl(
      DMACChannelControl::START | DMACChannelControl::FROM_MEMORY);
    system.dmacController().writeControl(DMACControl::DMA_ENABLE);
    system.clockMasterCycle();
    const auto snapshot = inspectStartup(system);
    REQUIRE(snapshot.dmac.channels[0].clockEligible);
    REQUIRE(snapshot.dmac.channels[0].lastTransferStalled);
    REQUIRE(snapshot.dmac.channels[0].quadwordCount == 1);
  }
  SECTION("Both scratchpad directions")
  {
    system.fromScratchpadDMAC().writeQuadwordCount(2);
    system.fromScratchpadDMAC().writeChannelControl(DMACChannelControl::START);
    system.toScratchpadDMAC().writeMemoryAddress(0x3000);
    system.toScratchpadDMAC().writeScratchpadAddress(0x40);
    system.toScratchpadDMAC().writeQuadwordCount(3);
    system.toScratchpadDMAC().writeChannelControl(DMACChannelControl::START);
    system.dmacController().writeControl(DMACControl::DMA_ENABLE);
    const auto snapshot = inspectStartup(system);
    REQUIRE(snapshot.dmac.channels[2].clockEligible);
    REQUIRE(snapshot.dmac.channels[3].clockEligible);
    REQUIRE(snapshot.dmac.channels[3].memoryAddress == 0x3000);
    REQUIRE(snapshot.dmac.channels[3].scratchpadAddress == 0x40);
    REQUIRE(snapshot.dmac.channels[3].quadwordCount == 3);
  }
}

TEST_CASE("Startup run wrappers preserve exact budgets state and tracing",
  "[startup-diagnostics]")
{
  const auto image = startupROM();
  for (const std::uint64_t budget : {0, 1, 7, 8, 9, 17})
  {
    CAPTURE(budget);
    NekoSystem inspected;
    NekoSystem reference;
    const auto reported = runDualCPUBIOSWithStartupSummary(inspected, image, budget);
    const auto expected = reference.runDualCPUBIOS(image, budget);
    REQUIRE(reported.execution.masterCycles == expected.masterCycles);
    REQUIRE(reported.execution.cycleLimitReached == expected.cycleLimitReached);
    REQUIRE(reported.execution.ee.instructions == expected.ee.instructions);
    REQUIRE(reported.execution.iop.instructions == expected.iop.instructions);
    REQUIRE(reported.startup.masterCycle == budget);
    REQUIRE(reported.startup.ee.programCounter == expected.ee.programCounter);
    REQUIRE(reported.startup.iop.programCounter == expected.iop.programCounter);
    requireSameState(inspected, reference);
    inspected.startTrace();
    reference.startTrace();
    const auto continued = runDualCPUWithStartupSummary(inspected, 9);
    const auto continuedReference = reference.runDualCPU(9);
    REQUIRE(continued.startup.masterCycle == budget + 9);
    REQUIRE(continued.execution.masterCycles == 9);
    REQUIRE(continued.execution.iop.instructions ==
      continuedReference.iop.instructions);
    REQUIRE(continued.startup.iop.cycles == reference.iopCore().elapsedCycles());
    requireSameState(inspected, reference);
  }
}

TEST_CASE("Startup run summaries distinguish host failure from budget exhaustion",
  "[startup-diagnostics]")
{
  NekoSystem system;
  system.runDualCPUBIOS(startupROM(), 0);
  SECTION("Unsupported EE instruction")
  {
    system.eeBus().write32(0, 0x40003800);
    system.eeCore().startExecution(0xa0000000);
    const auto result = runDualCPUWithStartupSummary(system, 1);
    REQUIRE_FALSE(result.execution.cycleLimitReached);
    REQUIRE(result.startup.masterCycle == 1);
    REQUIRE(result.startup.ee.state == EEExecutionState::Halted);
    REQUIRE(result.startup.ee.stopReason == EEStopReason::UnsupportedInstruction);
    REQUIRE(result.startup.ee.rejectedInstruction == 0x40003800);
    REQUIRE(result.startup.ee.pendingException == EEException::None);
    REQUIRE(result.startup.iop.state == IOPExecutionState::Running);
    REQUIRE(result.startup.iop.retiredInstructions == 1);
  }
  SECTION("Rejected IOP physical store")
  {
    REQUIRE(system.iopBus().write32(0, 0x3c011000).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.iopBus().write32(4, 0xac200000).status ==
      IOPBusStatus::Completed);
    system.iopCore().startExecution(0xa0000000);
    const auto result = runDualCPUWithStartupSummary(system, 9);
    REQUIRE_FALSE(result.execution.cycleLimitReached);
    REQUIRE(result.startup.masterCycle == 9);
    REQUIRE(result.startup.ee.state == EEExecutionState::Running);
    REQUIRE(result.startup.iop.state == IOPExecutionState::Halted);
    REQUIRE(result.startup.iop.stopReason == IOPStopReason::StoreFailure);
    REQUIRE(result.startup.iop.exception.kind == IOPException::None);
    const auto &failure = result.startup.iop.storeFailure;
    REQUIRE(failure.width == IOPStoreWidth::Word);
    REQUIRE(failure.value == 0);
    REQUIRE(failure.access.outcome == IOPAccessOutcome::Unmapped);
    REQUIRE(failure.access.virtualAddress == 0x10000000);
    REQUIRE(failure.access.physicalAddress == 0x10000000);
    REQUIRE(failure.access.cacheRoute == IOPCacheRoute::Cached);
    std::ostringstream output;
    writeStartupRunSummaryJsonLine(output, result);
    REQUIRE(output.str().find(
      "\"store_failure\":{\"width\":4,\"value\":0,\"outcome\":\"unmapped\","
      "\"exception\":\"none\",\"virtual_address\":268435456,"
      "\"physical_address\":268435456,\"cache_route\":\"cached\","
      "\"merge_byte_enable\":0}") != std::string::npos);
  }
  SECTION("Already halted")
  {
    system.iopCore().haltExecution();
    const auto before = system.saveState();
    const auto result = runDualCPUWithStartupSummary(system, 0);
    REQUIRE_FALSE(result.execution.cycleLimitReached);
    REQUIRE(result.execution.masterCycles == 0);
    REQUIRE(result.startup.iop.stopReason == IOPStopReason::HostHalt);
    const bool unchanged = system.saveState() == before;
    REQUIRE(unchanged);
  }
}

TEST_CASE("Startup summaries retain guest exception context without calling it a host stop",
  "[startup-diagnostics]")
{
  NekoSystem system;
  system.runDualCPUBIOS(startupROM(), 0);
  system.eeBus().write32(0, 0xac220000); // sw r2, 0(r1)
  system.eeCore().setGeneralRegister(1, {0xb000f240, 0});
  system.eeCore().setGeneralRegister(2, {0xc0100, 0});
  system.eeCore().startExecution(0xa0000000);
  REQUIRE(system.iopBus().write32(0, 0x0000000c).status ==
    IOPBusStatus::Completed);
  system.iopCore().startExecution(0xa0000000);
  const auto entry = runDualCPUWithStartupSummary(system, 1);
  REQUIRE(entry.execution.cycleLimitReached);
  REQUIRE(entry.startup.ee.state == EEExecutionState::Running);
  REQUIRE(entry.startup.ee.stopReason == EEStopReason::None);
  REQUIRE(entry.startup.ee.pendingException == EEException::DataBusErrorStore);
  REQUIRE(entry.startup.ee.exceptionAddress == 0xb000f240);
  REQUIRE(entry.startup.ee.epc == 0xa0000000);
  REQUIRE(entry.startup.ee.cause == system.eeCore().cop0Register(EECOP0Register::Cause));
  REQUIRE(entry.startup.ee.status == system.eeCore().cop0Register(EECOP0Register::Status));
  REQUIRE(entry.startup.iop.exception.kind == IOPException::SystemCall);
  REQUIRE(entry.startup.iop.epc == 0xa0000000);
  REQUIRE(entry.startup.iop.cause == system.iopCore().cause());
  REQUIRE(entry.startup.iop.status == system.iopCore().status());
  REQUIRE(entry.startup.sif.control == 0);
  REQUIRE(entry.startup.iopInterrupts.status == 0);
  const auto handled = runDualCPUWithStartupSummary(system, 8);
  REQUIRE(handled.execution.cycleLimitReached);
  REQUIRE(handled.startup.ee.pendingException == EEException::DataBusErrorStore);
  REQUIRE(handled.startup.iop.exception.kind == IOPException::None);
  REQUIRE(handled.startup.iop.epc == 0xa0000000);
  REQUIRE(handled.startup.iop.cause == entry.startup.iop.cause);
}

TEST_CASE("Startup wrapper preserves BIOS admission failure atomicity",
  "[startup-diagnostics]")
{
  NekoSystem system;
  system.installBootROM(startupROM());
  SECTION("Invalid image")
  {
    const auto before = system.saveState();
    REQUIRE_THROWS_AS(runDualCPUBIOSWithStartupSummary(system, {}, 1),
      std::invalid_argument);
    const bool unchanged = system.saveState() == before;
    REQUIRE(unchanged);
  }
  SECTION("Running CPU")
  {
    system.eeCore().startExecution(EEReset::VECTOR);
    const auto before = system.saveState();
    REQUIRE_THROWS_AS(runDualCPUBIOSWithStartupSummary(system, startupROM(), 1),
      std::logic_error);
    const bool unchanged = system.saveState() == before;
    REQUIRE(unchanged);
  }
}

TEST_CASE("Startup summary JSON has a stable shape independent of caller formatting",
  "[startup-diagnostics]")
{
  StartupSummary summary;
  summary.masterCycle = 17;
  summary.ee.programCounter = 32;
  summary.dmac.channels[0].channel = 1;
  summary.dmac.channels[1].channel = 2;
  summary.dmac.channels[2].channel = 8;
  summary.dmac.channels[3].channel = 9;
  std::ostringstream output;
  output << std::hex << std::showbase << std::boolalpha << std::setfill('!');
  output.width(7);
  const auto flags = output.flags();
  writeStartupSummaryJsonLine(output, summary);
  const std::string snapshot =
    R"({"master_cycle":17,"ee":{"cycles":0,"pc":32,"state":"halted",)"
    R"("stop_reason":"none","rejected_instruction":0,"pending_exception":"none",)"
    R"("exception_address":0,"status":0,"cause":0,"epc":0,"bad_vaddr":0},)"
    R"("iop":{"cycles":0,"retired_instructions":0,"pc":0,"state":"halted",)"
    R"("stop_reason":"none","exception":{"kind":"none","bad_vaddr":0,)"
    R"("delay_slot":false,"coprocessor":0},"store_failure":null,)"
    R"("status":0,"cause":0,"epc":0,"bad_vaddr":0},"sif":{"mscom":0,"smcom":0,)"
    R"("msflag":0,"smflag":0,"control":0,"iop_doorbell":0,"sif0_words":0,)"
    R"("sif1_words":0,"free_words":0,"packing_cursor":0},"dmac":{"control":0,)"
    R"("status":0,"interleave_size":0,"interrupt_pending":false,"channels":[)"
    R"({"channel":1,"control":0,"madr":0,"qwc":0,"tadr":0,"sadr":null,)"
    R"("clock_eligible":false,"last_transfer_stalled":false},)"
    R"({"channel":2,"control":0,"madr":0,"qwc":0,"tadr":0,"sadr":null,)"
    R"("clock_eligible":false,"last_transfer_stalled":false},)"
    R"({"channel":8,"control":0,"madr":0,"qwc":0,"tadr":null,"sadr":0,)"
    R"("clock_eligible":false,"last_transfer_stalled":false},)"
    R"({"channel":9,"control":0,"madr":0,"qwc":0,"tadr":0,"sadr":0,)"
    R"("clock_eligible":false,"last_transfer_stalled":false}]},)"
    R"("iop_dma":{"priority":[0,0],"interrupt_status":[0,0],)"
    R"("interrupt_pending":false,"sif1":{"channel":10,"control":0,"madr":0,)"
    R"("bcr":0,"remaining_words":0,"terminal":false,"enabled":false,)"
    R"("next_beat":{"outcome":"wait","failure":"none","address":0,)"
    R"("value":0,"bus_status":"completed"}}},)"
    R"("ee_interrupts":{"status":0,"mask":0,"pending":false},)"
    R"("iop_interrupts":{"status":0,"mask":0,"control":0,"pending":false}})";
  REQUIRE(output.str() ==
    "{\"type\":\"startup_summary\",\"snapshot\":" + snapshot + "}\n");
  REQUIRE(output.flags() == flags);
  REQUIRE(output.fill() == '!');
  REQUIRE(output.width() == 7);
  StartupRunSummary run;
  run.startup = summary;
  run.execution.masterCycles = 9;
  run.execution.cycleLimitReached = true;
  run.execution.ee.eeCycles = 9;
  run.execution.ee.instructions = 8;
  run.execution.iop.iopCycles = 2;
  run.execution.iop.instructions = 1;
  std::ostringstream runOutput;
  writeStartupRunSummaryJsonLine(runOutput, run);
  REQUIRE(runOutput.str() ==
    "{\"type\":\"startup_run_summary\",\"run\":{\"master_cycles\":9,"
    "\"cycle_limit_reached\":true,\"ee_cycles\":9,\"ee_accepted_instructions\":8,"
    "\"iop_cycles\":2,\"iop_retired_instructions\":1},\"snapshot\":" + snapshot + "}\n");
  summary.ee.stopReason = static_cast<EEStopReason>(255);
  std::ostringstream invalid;
  REQUIRE_THROWS_AS(writeStartupSummaryJsonLine(invalid, summary),
    std::invalid_argument);
  REQUIRE(invalid.str().empty());
}

TEST_CASE("Startup IOP DMA inspection reports current packet waits and rejection without mutation",
  "[startup-diagnostics][receiver]")
{
  NekoSystem system;
  REQUIRE(system.iopSIF1DMAC().writeChannelControl(0x01000400) ==
    IOPDMAWriteOutcome::Completed);
  auto snapshot = inspectStartup(system);
  REQUIRE(snapshot.iopDMA.control == 0x01000400);
  REQUIRE_FALSE(snapshot.iopDMA.enabled);
  REQUIRE(snapshot.iopDMA.nextBeat.outcome == IOPDMABeatOutcome::Wait);
  system.iopDMAController().writePriority(IOPDMABank::Secondary, 0x8000);
  snapshot = inspectStartup(system);
  REQUIRE(snapshot.iopDMA.enabled);
  REQUIRE(snapshot.iopDMA.remainingWords == 0);
  REQUIRE(snapshot.iopDMA.nextBeat.outcome == IOPDMABeatOutcome::Wait);
  REQUIRE(system.sif().pushSIF1Quadword({{0x80800000, 4, 0, 0}}) ==
    SIFTransferOutcome::Completed);
  snapshot = inspectStartup(system);
  REQUIRE(snapshot.iopDMA.nextBeat.outcome == IOPDMABeatOutcome::Accepted);
  system.clockMasterCycle();
  snapshot = inspectStartup(system);
  REQUIRE(snapshot.iopDMA.remainingWords == 4);
  REQUIRE(snapshot.iopDMA.terminal);
  REQUIRE(snapshot.iopDMA.nextBeat.outcome == IOPDMABeatOutcome::Wait);
  REQUIRE(system.sif().pushSIF1Quadword({{0x11223344, 2, 3, 4}}) ==
    SIFTransferOutcome::Completed);
  system.startTrace();
  const auto before = system.saveState();
  const auto hash = system.traceHash();
  for (unsigned repeat = 0; repeat < 2; ++repeat)
  {
    snapshot = inspectStartup(system);
    REQUIRE(snapshot.iopDMA.memoryAddress == 0x800000);
    REQUIRE(snapshot.iopDMA.nextBeat.outcome == IOPDMABeatOutcome::Rejected);
    REQUIRE(snapshot.iopDMA.nextBeat.failure == IOPDMAFailure::DestinationWrite);
    REQUIRE(snapshot.iopDMA.nextBeat.address == 0x800000);
    REQUIRE(snapshot.iopDMA.nextBeat.value == 0x11223344);
    REQUIRE(snapshot.iopDMA.nextBeat.status == IOPBusStatus::Unmapped);
    std::ostringstream output;
    writeStartupSummaryJsonLine(output, snapshot);
    REQUIRE(output.str().find(
      "\"next_beat\":{\"outcome\":\"rejected\",\"failure\":\"destination_write\","
      "\"address\":8388608,\"value\":287454020,\"bus_status\":\"unmapped\"}") !=
      std::string::npos);
    const bool unchanged = system.saveState() == before;
    REQUIRE(unchanged);
    REQUIRE(system.traceEnabled());
    REQUIRE(system.traceHash() == hash);
  }
  snapshot.iopDMA.nextBeat.failure = static_cast<IOPDMAFailure>(255);
  std::ostringstream invalid;
  REQUIRE_THROWS_AS(writeStartupSummaryJsonLine(invalid, snapshot),
    std::invalid_argument);
  REQUIRE(invalid.str().empty());
}
