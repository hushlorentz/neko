#include "startup_diagnostics.hpp"

#include <locale>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
  const char *const states[] = {"halted", "running"};
  const char *const eeStops[] = {
    "none", "host_halt", "fetch_exception", "reserved_instruction",
    "unsupported_instruction", "execution_exception", "undefined_operation"
  };
  const char *const iopStops[] = {
    "none", "host_halt", "undefined_operation", "execution_exception",
    "store_failure"
  };
  const char *const eeExceptions[] = {
    "none", "address_error_load_or_fetch", "instruction_bus_error",
    "arithmetic_overflow", "data_bus_error_load", "data_bus_error_store",
    "address_error_store", "interrupt", "reserved_instruction", "system_call",
    "breakpoint", "coprocessor_unusable", "tlb_refill_load_or_fetch",
    "tlb_invalid_load_or_fetch", "tlb_refill_store", "tlb_invalid_store",
    "tlb_modified"
  };
  const char *const iopExceptions[] = {
    "none", "interrupt", "address_error_load_or_fetch", "address_error_store",
    "instruction_bus_error", "data_bus_error", "system_call", "breakpoint",
    "reserved_instruction", "coprocessor_unusable", "arithmetic_overflow"
  };
  const char *const accessOutcomes[] = {
    "completed", "protection_failure", "misaligned", "read_only", "unmapped"
  };
  const char *const cacheRoutes[] = {"none", "cached", "uncached"};
  const char *const dmaOutcomes[] = {"accepted", "completed", "wait", "rejected"};
  const char *const dmaFailures[] = {"none", "unaligned_header", "destination_write"};
  const char *const busStatuses[] = {"completed", "misaligned", "read_only", "unmapped"};

  template<typename Enum, std::size_t N>
  const char *enumName(Enum value, const char *const (&names)[N])
  {
    const auto index = static_cast<std::size_t>(value);
    if (index >= N)
    {
      throw std::invalid_argument("Unknown startup diagnostic enum value.");
    }
    return names[index];
  }

  const char *jsonBoolean(bool value)
  {
    return value ? "true" : "false";
  }

  void writeStoreFailure(std::ostream &output, const IOPStoreFailure &failure)
  {
    switch (failure.width)
    {
      case IOPStoreWidth::None:
        output << "null";
        return;
      case IOPStoreWidth::Byte:
      case IOPStoreWidth::Halfword:
      case IOPStoreWidth::Word:
        break;
      default:
        throw std::invalid_argument("Unknown startup diagnostic store width.");
    }
    output
      << "{\"width\":" << static_cast<unsigned>(failure.width)
      << ",\"value\":" << failure.value
      << ",\"outcome\":\"" << enumName(failure.access.outcome, accessOutcomes)
      << "\",\"exception\":\"" << enumName(failure.access.exception, iopExceptions)
      << "\",\"virtual_address\":" << failure.access.virtualAddress
      << ",\"physical_address\":" << failure.access.physicalAddress
      << ",\"cache_route\":\"" << enumName(failure.access.cacheRoute, cacheRoutes)
      << "\",\"merge_byte_enable\":" << static_cast<unsigned>(failure.mergeByteEnable)
      << "}";
  }

  void writeSnapshot(std::ostream &output, const StartupSummary &summary)
  {
    const auto &ee = summary.ee;
    const auto &iop = summary.iop;
    const auto &sif = summary.sif;
    const auto &dma = summary.dmac;
    output
      << "{\"master_cycle\":" << summary.masterCycle
      << ",\"ee\":{\"cycles\":" << ee.cycles
      << ",\"pc\":" << ee.programCounter
      << ",\"state\":\"" << enumName(ee.state, states)
      << "\",\"stop_reason\":\"" << enumName(ee.stopReason, eeStops)
      << "\",\"rejected_instruction\":" << ee.rejectedInstruction
      << ",\"pending_exception\":\"" << enumName(ee.pendingException, eeExceptions)
      << "\",\"exception_address\":" << ee.exceptionAddress
      << ",\"status\":" << ee.status
      << ",\"cause\":" << ee.cause
      << ",\"epc\":" << ee.epc
      << ",\"bad_vaddr\":" << ee.badVirtualAddress << "}"
      << ",\"iop\":{\"cycles\":" << iop.cycles
      << ",\"retired_instructions\":" << iop.retiredInstructions
      << ",\"pc\":" << iop.programCounter
      << ",\"state\":\"" << enumName(iop.state, states)
      << "\",\"stop_reason\":\"" << enumName(iop.stopReason, iopStops)
      << "\",\"exception\":{\"kind\":\""
      << enumName(iop.exception.kind, iopExceptions)
      << "\",\"bad_vaddr\":" << iop.exception.badVirtualAddress
      << ",\"delay_slot\":" << jsonBoolean(iop.exception.inBranchDelaySlot)
      << ",\"coprocessor\":" << static_cast<unsigned>(iop.exception.coprocessor)
      << "},\"store_failure\":";
    writeStoreFailure(output, iop.storeFailure);
    output
      << ",\"status\":" << iop.status
      << ",\"cause\":" << iop.cause
      << ",\"epc\":" << iop.epc
      << ",\"bad_vaddr\":" << iop.badVirtualAddress << "}"
      << ",\"sif\":{\"mscom\":" << sif.mscom
      << ",\"smcom\":" << sif.smcom
      << ",\"msflag\":" << sif.msflag
      << ",\"smflag\":" << sif.smflag
      << ",\"control\":" << sif.control
      << ",\"iop_doorbell\":" << sif.iopDoorbell
      << ",\"sif0_words\":" << sif.sif0Words
      << ",\"sif1_words\":" << sif.sif1Words
      << ",\"free_words\":" << sif.freeWords
      << ",\"packing_cursor\":" << sif.packingCursor << "}"
      << ",\"dmac\":{\"control\":" << dma.control
      << ",\"status\":" << dma.status
      << ",\"interleave_size\":" << dma.interleaveSize
      << ",\"interrupt_pending\":" << jsonBoolean(dma.interruptPending)
      << ",\"channels\":[";
    for (std::size_t index = 0; index < dma.channels.size(); ++index)
    {
      if (index != 0)
      {
        output << ",";
      }
      const auto &channel = dma.channels[index];
      output
        << "{\"channel\":" << static_cast<unsigned>(channel.channel)
        << ",\"control\":" << channel.control
        << ",\"madr\":" << channel.memoryAddress
        << ",\"qwc\":" << channel.quadwordCount
        << ",\"tadr\":";
      if (channel.channel == 8)
      {
        output << "null";
      }
      else
      {
        output << channel.tagAddress;
      }
      output << ",\"sadr\":";
      if (channel.channel == 8 || channel.channel == 9)
      {
        output << channel.scratchpadAddress;
      }
      else
      {
        output << "null";
      }
      output
        << ",\"clock_eligible\":" << jsonBoolean(channel.clockEligible)
        << ",\"last_transfer_stalled\":"
        << jsonBoolean(channel.lastTransferStalled) << "}";
    }
    const auto &iopDMA = summary.iopDMA;
    const auto &beat = iopDMA.nextBeat;
    output
      << "]},\"iop_dma\":{\"priority\":[" << iopDMA.priority[0]
      << "," << iopDMA.priority[1]
      << "],\"interrupt_status\":[" << iopDMA.interruptStatus[0]
      << "," << iopDMA.interruptStatus[1]
      << "],\"interrupt_pending\":" << jsonBoolean(iopDMA.interruptPending)
      << ",\"sif1\":{\"channel\":10,\"control\":" << iopDMA.control
      << ",\"madr\":" << iopDMA.memoryAddress
      << ",\"bcr\":" << iopDMA.blockControl
      << ",\"remaining_words\":" << iopDMA.remainingWords
      << ",\"terminal\":" << jsonBoolean(iopDMA.terminal)
      << ",\"enabled\":" << jsonBoolean(iopDMA.enabled)
      << ",\"next_beat\":{\"outcome\":\"" << enumName(beat.outcome, dmaOutcomes)
      << "\",\"failure\":\"" << enumName(beat.failure, dmaFailures)
      << "\",\"address\":" << beat.address
      << ",\"value\":" << beat.value
      << ",\"bus_status\":\"" << enumName(beat.status, busStatuses)
      << "\"}}},\"ee_interrupts\":{\"status\":" << summary.eeInterrupts.status
      << ",\"mask\":" << summary.eeInterrupts.mask
      << ",\"pending\":" << jsonBoolean(summary.eeInterrupts.pending)
      << "},\"iop_interrupts\":{\"status\":" << summary.iopInterrupts.status
      << ",\"mask\":" << summary.iopInterrupts.mask
      << ",\"control\":" << summary.iopInterrupts.control
      << ",\"pending\":" << jsonBoolean(summary.iopInterrupts.pending) << "}}";
  }

  void emitJson(std::ostream &output, const std::ostringstream &json)
  {
    const std::string line = json.str();
    output.write(line.data(), static_cast<std::streamsize>(line.size()));
  }
}

StartupSummary inspectStartup(const NekoSystem &system)
{
  StartupSummary summary;
  summary.masterCycle = system.masterClockScheduler().currentCycle();
  const auto &ee = system.eeCore();
  summary.ee = {
    ee.elapsedCycles(), ee.programCounter(), ee.executionState(), ee.stopReason(),
    ee.rejectedInstruction(), ee.pendingException(), ee.exceptionAddress(),
    ee.cop0Register(EECOP0Register::Status), ee.cop0Register(EECOP0Register::Cause),
    ee.cop0Register(EECOP0Register::EPC), ee.cop0Register(EECOP0Register::BadVAddr)
  };
  const auto &iop = system.iopCore();
  summary.iop = {
    iop.elapsedCycles(), iop.retiredInstructions(), iop.programCounter(),
    iop.executionState(), iop.stopReason(), iop.exception(), iop.storeFailure(),
    iop.status(), iop.cause(), iop.exceptionProgramCounter(), iop.badVirtualAddress()
  };
  const auto &sif = system.sif();
  summary.sif = {
    sif.readEE(SIFRegister::MSCOM), sif.readEE(SIFRegister::SMCOM),
    sif.readEE(SIFRegister::MSFLAG), sif.readEE(SIFRegister::SMFLAG), sif.control(),
    sif.iopDoorbell(), sif.wordCount(SIFDirection::SIF0),
    sif.wordCount(SIFDirection::SIF1), sif.freeWordCount(), sif.sif0PackingCursor()
  };
  const auto &dma = system.dmacController();
  summary.dmac.control = dma.control();
  summary.dmac.status = dma.status();
  summary.dmac.interleaveSize = dma.interleaveSize();
  summary.dmac.interruptPending = dma.interruptPending();
  const auto &vif = system.vif1DMAC();
  summary.dmac.channels[0] = {
    1, vif.channelControl(), vif.memoryAddress(), vif.quadwordCount(),
    vif.tagAddress(), 0, vif.clockActive(), vif.stalledByVIF1()
  };
  const auto &gif = system.gifDMAC();
  summary.dmac.channels[1] = {
    2, gif.channelControl(), gif.memoryAddress(), gif.quadwordCount(),
    gif.tagAddress(), 0, gif.clockActive(), gif.stalledByPATH3()
  };
  const auto &from = system.fromScratchpadDMAC();
  summary.dmac.channels[2] = {
    8, from.channelControl(), from.memoryAddress(), from.quadwordCount(),
    0, from.scratchpadAddress(), from.clockActive(), false
  };
  const auto &to = system.toScratchpadDMAC();
  summary.dmac.channels[3] = {
    9, to.channelControl(), to.memoryAddress(), to.quadwordCount(),
    to.tagAddress(), to.scratchpadAddress(), to.clockActive(), false
  };
  const auto &iopDMA = system.iopDMAController();
  const auto &receiver = system.iopSIF1DMAC();
  summary.iopDMA = {
    {{iopDMA.priority(IOPDMABank::Primary), iopDMA.priority(IOPDMABank::Secondary)}},
    {{iopDMA.interruptStatus(IOPDMABank::Primary),
      iopDMA.interruptStatus(IOPDMABank::Secondary)}},
    iopDMA.interruptPending(), receiver.channelControl(), receiver.memoryAddress(),
    receiver.blockControl(), receiver.remainingWords(), receiver.terminalPacket(),
    iopDMA.channelEnabled(IOPDMAChannel::SIF1),
    iopDMA.inspectSIF(receiver, system.iopBus(), sif)
  };
  const auto &eeInterrupts = system.interruptController();
  summary.eeInterrupts = {
    eeInterrupts.status(), eeInterrupts.mask(), eeInterrupts.interruptPending()
  };
  const auto &iopInterrupts = system.iopInterruptController();
  summary.iopInterrupts = {
    iopInterrupts.status(), iopInterrupts.mask(), iopInterrupts.control(),
    iopInterrupts.interruptPending()
  };
  return summary;
}

StartupRunSummary runDualCPUWithStartupSummary(
  NekoSystem &system, std::uint64_t maxMasterCycles)
{
  const auto execution = system.runDualCPU(maxMasterCycles);
  return {execution, inspectStartup(system)};
}

StartupRunSummary runDualCPUBIOSWithStartupSummary(
  NekoSystem &system, const std::vector<std::uint8_t> &image,
  std::uint64_t maxMasterCycles)
{
  const auto execution = system.runDualCPUBIOS(image, maxMasterCycles);
  return {execution, inspectStartup(system)};
}

void writeStartupSummaryJsonLine(
  std::ostream &output, const StartupSummary &summary)
{
  std::ostringstream json;
  json.imbue(std::locale::classic());
  json << "{\"type\":\"startup_summary\",\"snapshot\":";
  writeSnapshot(json, summary);
  json << "}\n";
  emitJson(output, json);
}

void writeStartupRunSummaryJsonLine(
  std::ostream &output, const StartupRunSummary &summary)
{
  std::ostringstream json;
  json.imbue(std::locale::classic());
  const auto &run = summary.execution;
  json
    << "{\"type\":\"startup_run_summary\",\"run\":{\"master_cycles\":"
    << run.masterCycles
    << ",\"cycle_limit_reached\":" << jsonBoolean(run.cycleLimitReached)
    << ",\"ee_cycles\":" << run.ee.eeCycles
    << ",\"ee_accepted_instructions\":" << run.ee.instructions
    << ",\"iop_cycles\":" << run.iop.iopCycles
    << ",\"iop_retired_instructions\":" << run.iop.instructions
    << "},\"snapshot\":";
  writeSnapshot(json, summary.startup);
  json << "}\n";
  emitJson(output, json);
}

void writeStartupTraceJsonLines(
  std::ostream &output, const NekoSystem &system)
{
  std::ostringstream json;
  json.imbue(std::locale::classic());
  json <<
    "{\"type\":\"startup_trace_summary\",\"event_limit\":" <<
    system.traceEventLimit() <<
    ",\"captured_events\":" << system.trace().size() <<
    ",\"truncated\":" << jsonBoolean(system.traceTruncated()) << "}\n";
  emitJson(output, json);
  for (const auto &event : system.trace())
  {
    json.str("");
    json.clear();
    json <<
      "{\"type\":\"trace_event\",\"master_cycle\":" << event.masterCycle <<
      ",\"subsystem\":" << static_cast<unsigned>(event.subsystem) <<
      ",\"event_type\":" << static_cast<unsigned>(event.type) <<
      ",\"values\":[" << event.value0 << ',' << event.value1 << ',' <<
      event.value2 << ',' << event.value3 << "]}\n";
    emitJson(output, json);
  }
}
