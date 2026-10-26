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
  const char *const dmaFailures[] = {
    "none", "unaligned_header", "destination_write", "descriptor_read", "source_read"
  };
  const char *const busStatuses[] = {"completed", "misaligned", "read_only", "unmapped"};
  const char *const sifDirections[] = {"sif0", "sif1"};
  const char *const eeBeatKinds[] = {
    "source_tag", "payload", "completion", "destination_tag"
  };

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

  unsigned iopChannelNumber(IOPDMAChannel channel)
  {
    switch (channel)
    {
      case IOPDMAChannel::SIF0: return 9;
      case IOPDMAChannel::SIF1: return 10;
    }
    throw std::invalid_argument("Unknown startup diagnostic IOP DMA channel.");
  }

  void writeEESIF(std::ostream &output, const StartupDMACChannelSummary &channel)
  {
    const auto &sif = channel.sif;
    output << "{\"active\":" << jsonBoolean(sif.active)
      << ",\"enabled\":" << jsonBoolean(sif.enabled)
      << ",\"next_beat_kind\":\"" << enumName(sif.nextBeatKind, eeBeatKinds)
      << "\",\"continuation\":{\"terminal\":"
      << jsonBoolean(sif.continuation.terminateAfterPacket)
      << ",\"destination_packet_accepted\":";
    if (channel.channel == 5)
    {
      output << jsonBoolean(sif.continuation.destinationPacketAccepted);
    }
    else
    {
      output << "null";
    }
    output << ",\"slice_qwc_remaining\":";
    if (channel.channel == 6)
    {
      output << static_cast<unsigned>(sif.continuation.sliceQuadwordsRemaining);
    }
    else
    {
      output << "null";
    }
    output << "}}";
  }

  void writeIOPSIF(std::ostream &output,
    const StartupIOPDMAChannelSummary &channel, IOPDMAChannel id)
  {
    const auto &beat = channel.nextBeat;
    output << "{\"channel\":" << iopChannelNumber(id)
      << ",\"control\":" << channel.control
      << ",\"madr\":" << channel.memoryAddress
      << ",\"bcr\":" << channel.blockControl
      << ",\"tadr\":";
    if (id == IOPDMAChannel::SIF0)
    {
      output << channel.tagAddress;
    }
    else
    {
      output << "null";
    }
    output << ",\"remaining_words\":" << channel.remainingWords
      << ",\"terminal\":" << jsonBoolean(channel.terminal)
      << ",\"active\":" << jsonBoolean(channel.active)
      << ",\"enabled\":" << jsonBoolean(channel.enabled)
      << ",\"priority\":" << static_cast<unsigned>(channel.priority)
      << ",\"next_beat\":{\"outcome\":\"" << enumName(beat.outcome, dmaOutcomes)
      << "\",\"failure\":\"" << enumName(beat.failure, dmaFailures)
      << "\",\"address\":" << beat.address
      << ",\"value\":" << beat.value
      << ",\"bus_status\":\"" << enumName(beat.status, busStatuses) << "\"}}";
  }

  template<typename Channel>
  StartupIOPDMAChannelSummary inspectIOPSIF(
    const NekoSystem &system, const Channel &channel, IOPDMAChannel id)
  {
    const auto &controller = system.iopDMAController();
    StartupIOPDMAChannelSummary summary(id);
    summary.control = channel.channelControl();
    summary.memoryAddress = channel.memoryAddress();
    summary.blockControl = channel.blockControl();
    summary.remainingWords = channel.remainingWords();
    summary.terminal = channel.terminalPacket();
    summary.active = channel.active();
    summary.enabled = controller.channelEnabled(id);
    summary.priority = controller.channelPriority(id);
    summary.nextBeat = controller.inspectSIF(channel, system.iopBus(), system.sif());
    return summary;
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
      << ",\"packing_cursor\":" << sif.packingCursor
      << ",\"packing_latch\":[" << sif.packingLatch[0]
      << "," << sif.packingLatch[1] << "," << sif.packingLatch[2]
      << "," << sif.packingLatch[3] << "]}"
      << ",\"dmac\":{\"control\":" << dma.control
      << ",\"hold_control\":" << dma.holdControl
      << ",\"status\":" << dma.status
      << ",\"interleave_size\":" << dma.interleaveSize
      << ",\"interrupt_pending\":" << jsonBoolean(dma.interruptPending)
      << ",\"priority_control\":" << dma.priorityControl
      << ",\"stall_address\":" << dma.stallAddress
      << ",\"sif_grant_order\":[\"" << enumName(dma.sifGrantOrder[0], sifDirections)
      << "\",\"" << enumName(dma.sifGrantOrder[1], sifDirections)
      << "\"],\"channels\":[";
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
      if (channel.channel == 5 || channel.channel == 8)
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
      output << ",\"clock_eligible\":" << jsonBoolean(channel.clockEligible)
        << ",\"last_transfer_stalled\":";
      if (channel.channel == 5 || channel.channel == 6)
      {
        output << "null,\"sif\":";
        writeEESIF(output, channel);
      }
      else
      {
        output << jsonBoolean(channel.lastTransferStalled);
      }
      output << "}";
    }
    const auto &iopDMA = summary.iopDMA;
    output
      << "]},\"iop_dma\":{\"priority\":[" << iopDMA.priority[0]
      << "," << iopDMA.priority[1]
      << "],\"interrupt_status\":[" << iopDMA.interruptStatus[0]
      << "," << iopDMA.interruptStatus[1]
      << "],\"interrupt_pending\":" << jsonBoolean(iopDMA.interruptPending)
      << ",\"sif_grant_order\":[" << iopChannelNumber(iopDMA.sifGrantOrder[0])
      << "," << iopChannelNumber(iopDMA.sifGrantOrder[1])
      << "],\"sif_tie_break_channel\":" << iopChannelNumber(iopDMA.sifTieBreakChannel)
      << ",\"sif0\":";
    writeIOPSIF(output, iopDMA.sif0, IOPDMAChannel::SIF0);
    output << ",\"sif1\":";
    writeIOPSIF(output, iopDMA.sif1, IOPDMAChannel::SIF1);
    output << "},\"ee_interrupts\":{\"status\":" << summary.eeInterrupts.status
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
    sif.wordCount(SIFDirection::SIF1), sif.freeWordCount(), sif.sif0PackingCursor(),
    sif.sif0PackingLatch()
  };
  const auto &dma = system.dmacController();
  summary.dmac.control = dma.control();
  summary.dmac.holdControl = dma.holdControl();
  summary.dmac.status = dma.status();
  summary.dmac.interleaveSize = dma.interleaveSize();
  summary.dmac.interruptPending = dma.interruptPending();
  summary.dmac.priorityControl = dma.priorityControl();
  summary.dmac.stallAddress = dma.stallAddress();
  summary.dmac.sifGrantOrder = dma.sifGrantOrder();
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
  const auto &eeReceiver = system.sif0DMAC();
  summary.dmac.channels[4] = {
    5, eeReceiver.channelControl(), eeReceiver.memoryAddress(), eeReceiver.quadwordCount(),
    0, 0, eeReceiver.transferEnabled(), false,
    {eeReceiver.active(), dma.channelEnabled(DMACStatus::CHANNEL_5),
      eeReceiver.nextBeatKind(), eeReceiver.continuation()}
  };
  const auto &eeSource = system.sif1DMAC();
  summary.dmac.channels[5] = {
    6, eeSource.channelControl(), eeSource.memoryAddress(), eeSource.quadwordCount(),
    eeSource.tagAddress(), 0, eeSource.transferEnabled(), false,
    {eeSource.active(), dma.channelEnabled(DMACStatus::CHANNEL_6),
      eeSource.nextBeatKind(), eeSource.continuation()}
  };
  const auto &iopDMA = system.iopDMAController();
  summary.iopDMA.priority =
    {{iopDMA.priority(IOPDMABank::Primary), iopDMA.priority(IOPDMABank::Secondary)}};
  summary.iopDMA.interruptStatus = {{
    iopDMA.interruptStatus(IOPDMABank::Primary),
    iopDMA.interruptStatus(IOPDMABank::Secondary)}};
  summary.iopDMA.interruptPending = iopDMA.interruptPending();
  summary.iopDMA.sifGrantOrder = iopDMA.sifGrantOrder();
  summary.iopDMA.sifTieBreakChannel = iopDMA.sifTieBreakChannel();
  summary.iopDMA.sif0 = inspectIOPSIF(system, system.iopSIF0DMAC(), IOPDMAChannel::SIF0);
  summary.iopDMA.sif0.tagAddress = system.iopSIF0DMAC().tagAddress();
  summary.iopDMA.sif1 = inspectIOPSIF(system, system.iopSIF1DMAC(), IOPDMAChannel::SIF1);
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

StartupRunEvidence runDualCPUWithStartupEvidence(
  NekoSystem &system, std::uint64_t maxMasterCycles)
{
  DualCPUExecutionResult execution;
  try
  {
    execution = system.runDualCPU(maxMasterCycles);
  }
  catch (const std::exception &error)
  {
    return {StartupRunOutcome::RuntimeError, {}, {error.what(), inspectStartup(system)}};
  }
  return {StartupRunOutcome::Completed, {execution, inspectStartup(system)}, {}};
}

void writeStartupRuntimeErrorJsonLine(
  std::ostream &output, const StartupRuntimeError &error)
{
  std::ostringstream json;
  json.imbue(std::locale::classic());
  json << "{\"type\":\"startup_runtime_error\","
    "\"execution_boundary\":\"incomplete_cycle\",\"run\":null,\"message\":\"";
  constexpr char hex[] = "0123456789abcdef";
  for (const unsigned char character : error.message)
  {
    if (character == '"' || character == '\\')
    {
      json << '\\' << static_cast<char>(character);
    }
    else if (character < 0x20)
    {
      json << "\\u00" << hex[character >> 4] << hex[character & 15];
    }
    else
    {
      json << static_cast<char>(character);
    }
  }
  json << "\",\"snapshot\":";
  writeSnapshot(json, error.startup);
  json << "}\n";
  emitJson(output, json);
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
