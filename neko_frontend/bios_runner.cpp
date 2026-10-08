#include "bios_runner.hpp"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace
{
  const char *executionStateName(EEExecutionState state)
  {
    switch (state)
    {
      case EEExecutionState::Halted:
        return "halted";
      case EEExecutionState::Running:
        return "running";
    }
    throw std::logic_error("Unknown EE execution state.");
  }

  const char *stopReasonName(EEStopReason reason)
  {
    switch (reason)
    {
      case EEStopReason::None:
        return "none";
      case EEStopReason::HostHalt:
        return "host_halt";
      case EEStopReason::FetchException:
        return "fetch_exception";
      case EEStopReason::ReservedInstruction:
        return "reserved_instruction";
      case EEStopReason::UnsupportedInstruction:
        return "unsupported_instruction";
      case EEStopReason::ExecutionException:
        return "execution_exception";
      case EEStopReason::UndefinedOperation:
        return "undefined_operation";
    }
    throw std::logic_error("Unknown EE stop reason.");
  }

  const char *exceptionName(EEException exception)
  {
    switch (exception)
    {
      case EEException::None:
        return "none";
      case EEException::Interrupt:
        return "interrupt";
      case EEException::TLBModified:
        return "tlb_modified";
      case EEException::TLBRefillLoadOrFetch:
        return "tlb_refill_load_or_fetch";
      case EEException::TLBInvalidLoadOrFetch:
        return "tlb_invalid_load_or_fetch";
      case EEException::TLBRefillStore:
        return "tlb_refill_store";
      case EEException::TLBInvalidStore:
        return "tlb_invalid_store";
      case EEException::AddressErrorLoadOrFetch:
        return "address_error_load_or_fetch";
      case EEException::AddressErrorStore:
        return "address_error_store";
      case EEException::InstructionBusError:
        return "instruction_bus_error";
      case EEException::DataBusErrorLoad:
        return "data_bus_error_load";
      case EEException::DataBusErrorStore:
        return "data_bus_error_store";
      case EEException::SystemCall:
        return "system_call";
      case EEException::Breakpoint:
        return "breakpoint";
      case EEException::ReservedInstruction:
        return "reserved_instruction";
      case EEException::CoprocessorUnusable:
        return "coprocessor_unusable";
      case EEException::ArithmeticOverflow:
        return "arithmetic_overflow";
    }
    throw std::logic_error("Unknown EE exception.");
  }

  std::vector<std::uint8_t> readBIOSFile(
    const std::string &path)
  {
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
      throw std::runtime_error(
        "Could not open BIOS image at " + path + ".");
    }
    return std::vector<std::uint8_t>(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
  }
}

std::string neko_frontend::formatBIOSRun(
  const BIOSRunReport &report)
{
  std::ostringstream output;
  output
    << "bios: master_cycles=" << report.result.masterCycles
    << " ee_cycles=" << report.result.eeCycles
    << " instructions=" << report.result.instructions
    << " state=" << executionStateName(report.result.state)
    << " stop=" << stopReasonName(report.result.stopReason)
    << " pc=0x" << std::hex << report.result.programCounter
    << " rejected=0x" << report.rejectedInstruction
    << " exception=" << exceptionName(
      report.result.pendingException)
    << " exception_address=0x"
    << report.result.exceptionAddress;
  return output.str();
}

neko_frontend::BIOSRunReport neko_frontend::runBIOSFile(
  const std::string &path,
  std::uint64_t maxMasterCycles)
{
  NekoSystem system;
  BIOSRunReport report;
  report.result =
    system.runBIOS(readBIOSFile(path), maxMasterCycles);
  report.rejectedInstruction =
    system.eeCore().rejectedInstruction();
  report.diagnostic = formatBIOSRun(report);
  report.hostExitCode = EXIT_SUCCESS;
  return report;
}
