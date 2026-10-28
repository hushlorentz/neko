#include "startup_diagnostics.hpp"

#include <locale>
#include <ostream>
#include <sstream>
#include <stdexcept>

namespace
{
  bool failedMemoryAccess(const NekoTraceEvent &event) noexcept
  {
    if (event.type != NekoTraceEventType::MemoryAccess)
    {
      return false;
    }
    if (event.subsystem == NekoTraceSubsystem::IOP)
    {
      return (event.value3 & NekoIOPTraceMemory::OUTCOME_MASK) != 0;
    }
    if (event.subsystem != NekoTraceSubsystem::EE)
    {
      return false;
    }
    const auto translation = (event.value3 &
      NekoEETraceMemory::TRANSLATION_OUTCOME_MASK) >>
        NekoEETraceMemory::TRANSLATION_OUTCOME_SHIFT;
    const auto transfer = (event.value3 &
      NekoEETraceMemory::TRANSFER_OUTCOME_MASK) >>
        NekoEETraceMemory::TRANSFER_OUTCOME_SHIFT;
    return translation >= static_cast<unsigned>(
        NekoEETraceMemory::TranslationOutcome::AddressErrorLoadOrFetch) ||
      transfer == static_cast<unsigned>(
        NekoEETraceMemory::TransferOutcome::PhysicalBusError);
  }

  StartupInstructionContext faultInstruction(const NekoTraceEvent &event)
  {
    StartupInstructionContext context;
    context.available = true;
    context.programCounter = static_cast<std::uint32_t>(event.value0);
    if ((event.value3 & NekoEETraceFault::INSTRUCTION_AVAILABLE) != 0)
    {
      context.instructionAvailable = true;
      context.instruction = static_cast<std::uint32_t>(event.value1);
    }
    context.delaySlot = (event.value3 & NekoEETraceFault::DELAY_SLOT) != 0;
    context.delaySlotAvailable = true;
    if (context.delaySlot)
    {
      context.branchAddressAvailable = true;
      context.branchAddress = static_cast<std::uint32_t>(event.value2);
    }
    return context;
  }

  StartupInstructionContext issuedInstruction(const NekoTraceEvent &event)
  {
    StartupInstructionContext context;
    context.available = true;
    context.programCounter = static_cast<std::uint32_t>(event.value0);
    context.instruction = static_cast<std::uint32_t>(event.value1);
    context.instructionAvailable = true;
    context.delaySlotAvailable = true;
    context.delaySlot = (event.value3 & NekoIOPTraceInstruction::DELAY_SLOT) != 0;
    return context;
  }

  void associateIOPBranchAddress(
    StartupInstructionContext &context, const NekoTraceEvent &exception)
  {
    if (context.available && context.delaySlotAvailable && context.delaySlot)
    {
      context.branchAddressAvailable = true;
      context.branchAddress = static_cast<std::uint32_t>(exception.value1);
    }
  }

  void associateMemoryException(
    StartupFailure &failure, const std::vector<NekoTraceEvent> &trace)
  {
    const bool ee = failure.event.subsystem == NekoTraceSubsystem::EE;
    const auto kind = (failure.event.value3 >> 10) & 3;
    if (kind == 0)
    {
      StartupInstructionContext fetch;
      fetch.available = true;
      fetch.programCounter = static_cast<std::uint32_t>(failure.event.value0);
      failure.instruction = fetch;
    }
    for (std::size_t index = failure.eventIndex + 1; index < trace.size(); ++index)
    {
      const auto &event = trace[index];
      if (event.masterCycle != failure.event.masterCycle)
      {
        break;
      }
      if (event.subsystem != failure.event.subsystem)
      {
        continue;
      }
      if (event.type == NekoTraceEventType::MemoryAccess ||
          event.type == NekoTraceEventType::InstructionIssued ||
          event.type == NekoTraceEventType::InstructionRetired)
      {
        break;
      }
      if (ee && event.type == NekoTraceEventType::FaultContext)
      {
        if ((event.value3 & NekoEETraceFault::HOST_REJECTION) != 0 ||
            (kind == 0 && (event.value0 != failure.event.value0 ||
              (event.value3 & NekoEETraceFault::INSTRUCTION_AVAILABLE) != 0)))
        {
          break;
        }
        failure.instruction = faultInstruction(event);
      }
      if (event.type == NekoTraceEventType::ExceptionEntered)
      {
        if (ee && (!failure.instruction.available ||
            event.value1 != failure.event.value0))
        {
          break;
        }
        failure.exception = event;
        failure.exceptionAvailable = true;
        if (!ee && failure.instruction.available)
        {
          if (kind == 0)
          {
            failure.instruction.delaySlotAvailable = true;
            failure.instruction.delaySlot = (event.value3 & (UINT64_C(1) << 31)) != 0;
          }
          associateIOPBranchAddress(failure.instruction, event);
        }
        break;
      }
    }
  }

  bool rejectedInstruction(const NekoTraceEvent &event) noexcept
  {
    if (event.subsystem == NekoTraceSubsystem::EE)
    {
      if (event.type == NekoTraceEventType::FaultContext)
      {
        return (event.value3 & NekoEETraceFault::HOST_REJECTION) != 0;
      }
      return event.type == NekoTraceEventType::ExceptionEntered &&
        (event.value0 == static_cast<unsigned>(EEException::ReservedInstruction) ||
         event.value0 == static_cast<unsigned>(EEException::CoprocessorUnusable));
    }
    if (event.subsystem == NekoTraceSubsystem::IOP)
    {
      return (event.type == NekoTraceEventType::StateChanged &&
          event.value2 == static_cast<unsigned>(NekoIOPTraceState::StopReason::UndefinedOperation)) ||
        (event.type == NekoTraceEventType::ExceptionEntered &&
          (event.value0 == 10 || event.value0 == 11));
    }
    return false;
  }

  bool rejectedDMA(const NekoTraceEvent &event) noexcept
  {
    return event.subsystem == NekoTraceSubsystem::SIF &&
      event.type == NekoTraceEventType::DMABeat &&
      ((event.value0 >> NekoSIFDMATrace::OUTCOME_SHIFT) & 255) ==
        static_cast<unsigned>(NekoSIFDMATrace::Outcome::Rejected);
  }

  template<std::size_t N>
  const char *name(std::uint64_t value, const char *const (&names)[N])
  {
    if (value >= N)
    {
      throw std::invalid_argument("Unknown startup trace metadata value.");
    }
    return names[value];
  }

  void optionalNumber(std::ostream &output, bool available, std::uint32_t value)
  {
    if (available)
    {
      output << value;
    }
    else
    {
      output << "null";
    }
  }

  void writeInstruction(
    std::ostream &output, const StartupInstructionContext &context)
  {
    if (!context.available)
    {
      output << "null";
      return;
    }
    output << "{\"pc\":" << context.programCounter << ",\"bits\":";
    optionalNumber(output, context.instructionAvailable, context.instruction);
    output << ",\"delay_slot\":";
    if (context.delaySlotAvailable)
    {
      output << (context.delaySlot ? "true" : "false");
    }
    else
    {
      output << "null";
    }
    output << ",\"branch_pc\":";
    optionalNumber(output, context.branchAddressAvailable, context.branchAddress);
    output << "}";
  }

  void writeAccess(std::ostream &output, const NekoTraceEvent &event)
  {
    const auto metadata = event.value3;
    const bool ee = event.subsystem == NekoTraceSubsystem::EE;
    const bool physical = (metadata & (ee ?
      NekoEETraceMemory::PHYSICAL_ADDRESS_VALID :
      NekoIOPTraceMemory::PHYSICAL_ADDRESS_VALID)) != 0;
    const char *const kinds[] = {"instruction_fetch", "data_load", "data_store"};
    output << "{\"kind\":\"" << name((metadata >> 10) & 3, kinds)
      << "\",\"virtual_address\":" << event.value0 << ",\"physical_address\":";
    if (physical)
    {
      output << (metadata >> 32);
    }
    else
    {
      output << "null";
    }
    output << ",\"width\":" << (metadata & 255)
      << ",\"write\":" << ((metadata & (UINT64_C(1) << 8)) != 0 ? "true" : "false")
      << ",\"data\":[" << event.value1 << ',' << event.value2 << ']';
    if (ee)
    {
      const char *const translations[] = {
        "translated", "tlb_lookup", "address_error_load_or_fetch",
        "address_error_store", "tlb_refill_load_or_fetch", "tlb_refill_store",
        "tlb_invalid_load_or_fetch", "tlb_invalid_store", "tlb_modified",
        "unsupported_scratchpad_instruction", "unsupported_scratchpad_page_size",
        "unsupported_cache_attribute"};
      const char *const routes[] = {"uncached", "cached_noncoherent",
        "uncached_accelerated", "tlb_selected", "unsupported"};
      const char *const accesses[] = {
        "not_accessed", "bypassed", "hit", "miss", "refilled", "allocated"};
      const char *const transfers[] = {
        "not_attempted", "completed", "stalled", "physical_bus_error", "device_not_ready"};
      const char *const phases[] = {"none", "direct", "refill", "writeback"};
      output << ",\"translation\":\"" << name((metadata >> 12) & 15, translations)
        << "\",\"cache_route\":\"" << name((metadata >> 16) & 7, routes)
        << "\",\"cache_access\":\"" << name((metadata >> 19) & 7, accesses)
        << "\",\"transfer\":\"" << name((metadata >> 22) & 7, transfers)
        << "\",\"failure_phase\":\"" << name((metadata >> 30) & 3, phases)
        << "\",\"cache_attribute\":" << ((metadata >> 27) & 7)
        << ",\"scratchpad\":" << ((metadata & NekoEETraceMemory::SCRATCHPAD_ROUTE) != 0 ?
          "true" : "false");
    }
    else
    {
      const char *const outcomes[] = {
        "completed", "protection_failure", "misaligned", "read_only", "unmapped"};
      const char *const routes[] = {"none", "cached", "uncached"};
      output << ",\"outcome\":\"" << name((metadata >> 12) & 7, outcomes)
        << "\",\"cache_route\":\"" << name((metadata >> 15) & 3, routes)
        << "\",\"byte_enable\":" << ((metadata >> 18) & 15);
    }
    output << "}";
  }

  void writeDMA(std::ostream &output, const NekoTraceEvent &event)
  {
    const char *const endpoints[] = {"ee_sif1", "iop_sif1", "ee_sif0", "iop_sif0"};
    const char *const kinds[] = {
      "source_tag", "header", "payload", "completion", "destination_tag"};
    const char *const failures[] = {"none", "source_bus", "unaligned_header",
      "destination_write", "destination_bus", "descriptor_read", "source_read"};
    output << "{\"endpoint\":\"" << name(event.value0 & 255, endpoints)
      << "\",\"kind\":\"" << name((event.value0 >> 8) & 255, kinds)
      << "\",\"outcome\":\"rejected\",\"failure\":\""
      << name((event.value0 >> 24) & 255, failures)
      << "\",\"attempted_address\":" << event.value1 << "}";
  }
}

constexpr std::size_t StartupFailureCapture::CAPACITY;

void StartupFailureCapture::observe(const NekoTraceEvent &event) noexcept
{
  ++observed;
  if (triggered)
  {
    if (event.masterCycle != failureCycle)
    {
      return;
    }
    if (failureCycleEvents == CAPACITY - 1)
    {
      truncatedCycle = true;
      return;
    }
    ++failureCycleEvents;
  }
  else if (failedMemoryAccess(event) || rejectedInstruction(event) ||
      rejectedDMA(event))
  {
    triggered = true;
    failureCycle = event.masterCycle;
  }
  if (triggered && count == CAPACITY &&
      history[cursor].masterCycle == failureCycle)
  {
    truncatedCycle = true;
  }
  history[cursor] = event;
  cursor = (cursor + 1) % CAPACITY;
  if (count == CAPACITY)
  {
    ++firstIndex;
  }
  else
  {
    ++count;
  }
}

std::vector<NekoTraceEvent> StartupFailureCapture::events() const
{
  std::vector<NekoTraceEvent> result;
  result.reserve(count);
  const auto first = (cursor + CAPACITY - count) % CAPACITY;
  for (std::size_t index = 0; index < count; ++index)
  {
    result.push_back(history[(first + index) % CAPACITY]);
  }
  return result;
}

std::uint64_t StartupFailureCapture::observedEvents() const noexcept
{
  return observed;
}

std::uint64_t StartupFailureCapture::firstEventIndex() const noexcept
{
  return firstIndex;
}

bool StartupFailureCapture::failureCycleTruncated() const noexcept
{
  return truncatedCycle;
}

void writeStartupFailureCaptureJsonLine(
  std::ostream &output, const StartupFailureCapture &capture)
{
  std::ostringstream json;
  json.imbue(std::locale::classic());
  json << "{\"type\":\"startup_failure_capture\",\"capacity\":"
    << StartupFailureCapture::CAPACITY
    << ",\"observed_events\":" << capture.observedEvents()
    << ",\"first_event_index\":" << capture.firstEventIndex()
    << ",\"failure_cycle_truncated\":"
    << (capture.failureCycleTruncated() ? "true" : "false") << ",\"events\":[";
  bool first = true;
  for (const auto &event : capture.events())
  {
    if (!first)
    {
      json << ',';
    }
    first = false;
    json << '{';
    writeStartupTraceEventFields(json, event);
    json << '}';
  }
  json << "]}\n";
  const auto line = json.str();
  output.write(line.data(), static_cast<std::streamsize>(line.size()));
}

StartupFailure firstStartupFailure(
  const std::vector<NekoTraceEvent> &trace)
{
  StartupInstructionContext iopInstruction;
  NekoTraceEvent eeContext;
  bool eeContextAvailable = false;
  std::uint64_t cycle = 0;
  for (std::size_t index = 0; index < trace.size(); ++index)
  {
    const auto &event = trace[index];
    if (event.masterCycle != cycle)
    {
      cycle = event.masterCycle;
      iopInstruction = {};
      eeContextAvailable = false;
    }
    if (event.subsystem == NekoTraceSubsystem::IOP)
    {
      if (event.type == NekoTraceEventType::InstructionIssued)
      {
        iopInstruction = issuedInstruction(event);
      }
      else if (event.type == NekoTraceEventType::InstructionRetired ||
          (event.type == NekoTraceEventType::MemoryAccess &&
           ((event.value3 >> 10) & 3) == 0))
      {
        iopInstruction = {};
      }
    }
    if (event.subsystem == NekoTraceSubsystem::EE)
    {
      if (event.type == NekoTraceEventType::FaultContext)
      {
        eeContext = event;
        eeContextAvailable = true;
      }
      else if (event.type != NekoTraceEventType::ExceptionEntered)
      {
        eeContextAvailable = false;
      }
    }
    StartupFailure failure;
    failure.found = true;
    failure.eventIndex = index;
    failure.event = event;
    if (failedMemoryAccess(event))
    {
      if (event.subsystem == NekoTraceSubsystem::IOP)
      {
        failure.instruction = iopInstruction;
      }
      associateMemoryException(failure, trace);
      return failure;
    }
    if (rejectedInstruction(event))
    {
      failure.kind = StartupFailureKind::InstructionRejection;
      if (event.subsystem == NekoTraceSubsystem::EE && eeContextAvailable)
      {
        failure.instruction = faultInstruction(eeContext);
      }
      else if (event.subsystem == NekoTraceSubsystem::IOP)
      {
        failure.instruction = iopInstruction;
      }
      if (event.type == NekoTraceEventType::ExceptionEntered)
      {
        failure.exception = event;
        failure.exceptionAvailable = true;
        if (event.subsystem == NekoTraceSubsystem::IOP)
        {
          associateIOPBranchAddress(failure.instruction, event);
        }
      }
      return failure;
    }
    if (rejectedDMA(event))
    {
      failure.kind = StartupFailureKind::DMARejection;
      return failure;
    }
  }
  return {};
}

void writeStartupFirstFailureJsonLine(
  std::ostream &output, const NekoSystem &system, StartupExecutionBoundary boundary,
  const StartupFailureCapture *capture)
{
  std::ostringstream json;
  json.imbue(std::locale::classic());
  json << "{\"type\":\"startup_first_failure\",\"trace_truncated\":"
    << (system.traceTruncated() ? "true" : "false")
    << ",\"execution_boundary\":\"";
  const char *const boundaries[] = {"complete_cycle", "incomplete_cycle"};
  json << name(static_cast<unsigned>(boundary), boundaries) << "\"";
  if (capture != nullptr)
  {
    json << ",\"event_source\":\"failure_capture\"";
  }
  json << ",\"failure\":";
  const auto failure = capture != nullptr ?
    firstStartupFailure(capture->events()) : firstStartupFailure(system.trace());
  if (!failure.found)
  {
    json << "null";
  }
  else
  {
    const auto &event = failure.event;
    const char *const kinds[] = {"memory_access", "instruction_rejection", "dma_rejection"};
    json << "{\"kind\":\"" << name(static_cast<unsigned>(failure.kind), kinds)
      << "\",\"cpu\":";
    if (event.subsystem == NekoTraceSubsystem::SIF)
    {
      json << "null";
    }
    else
    {
      json << (event.subsystem == NekoTraceSubsystem::EE ? "\"ee\"" : "\"iop\"");
    }
    json << ",\"event_index\":" << failure.eventIndex
      << ",\"master_cycle\":" << event.masterCycle << ",\"instruction\":";
    writeInstruction(json, failure.instruction);
    json << ",\"access\":";
    if (failure.kind == StartupFailureKind::MemoryAccess)
    {
      writeAccess(json, event);
    }
    else
    {
      json << "null";
    }
    json << ",\"dma\":";
    if (failure.kind == StartupFailureKind::DMARejection)
    {
      writeDMA(json, event);
    }
    else
    {
      json << "null";
    }
    json << ",\"exception\":";
    if (failure.exceptionAvailable)
    {
      const auto &exception = failure.exception;
      json << "{\"code\":" << ((exception.value3 >> 2) & 31)
        << (exception.subsystem == NekoTraceSubsystem::EE ?
          ",\"fault_address\":" : ",\"epc\":") << exception.value1
        << ",\"vector\":" << exception.value2 << ",\"cause\":" << exception.value3 << "}";
    }
    else
    {
      json << "null";
    }
    json << ",\"rejection_reason\":";
    if (event.type == NekoTraceEventType::FaultContext)
    {
      const auto reason = static_cast<EEStopReason>(
        (event.value3 >> NekoEETraceFault::STOP_REASON_SHIFT) & 255);
      if (reason == EEStopReason::UnsupportedInstruction)
      {
        json << "\"unsupported_instruction\"";
      }
      else if (reason == EEStopReason::UndefinedOperation)
      {
        json << "\"undefined_operation\"";
      }
      else
      {
        throw std::invalid_argument("Unknown startup instruction rejection.");
      }
    }
    else if (failure.kind == StartupFailureKind::InstructionRejection)
    {
      json << (event.type == NekoTraceEventType::ExceptionEntered ?
        "\"guest_exception\"" : "\"undefined_operation\"");
    }
    else
    {
      json << "null";
    }
    json << "}";
  }
  json << "}\n";
  const auto line = json.str();
  output.write(line.data(), static_cast<std::streamsize>(line.size()));
}
