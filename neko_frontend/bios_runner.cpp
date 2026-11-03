#include "bios_runner.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace
{
  const char *stopReasonName(IOPStopReason reason)
  {
    switch (reason)
    {
      case IOPStopReason::None:
        return "none";
      case IOPStopReason::HostHalt:
        return "host_halt";
      case IOPStopReason::UndefinedOperation:
        return "undefined_operation";
      case IOPStopReason::ExecutionException:
        return "execution_exception";
      case IOPStopReason::StoreFailure:
        return "store_failure";
    }
    throw std::logic_error("Unknown IOP stop reason.");
  }

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

neko_frontend::BIOSSession::BIOSSession(
  const std::vector<std::uint8_t> &image,
  std::uint64_t maxMasterCycles) :
  cycleLimit(maxMasterCycles)
{
  if (cycleLimit == 0)
  {
    throw std::invalid_argument("BIOS cycle limit must be positive.");
  }
  lastExecution = system.runDualCPUBIOS(image, 0);
}

void neko_frontend::BIOSSession::advance(
  std::uint64_t maxSliceCycles)
{
  if (maxSliceCycles == 0)
  {
    throw std::invalid_argument("BIOS slice budget must be positive.");
  }
  if (finished())
  {
    return;
  }
  lastExecution = system.runDualCPU(
    std::min(maxSliceCycles, cycleLimit - elapsed));
  elapsed += lastExecution.masterCycles;
}

bool neko_frontend::BIOSSession::finished() const
{
  return elapsed == cycleLimit ||
    lastExecution.ee.state == EEExecutionState::Halted ||
    lastExecution.iop.state == IOPExecutionState::Halted;
}

std::uint64_t neko_frontend::BIOSSession::masterCycles() const
{
  return elapsed;
}

const DualCPUExecutionResult &
neko_frontend::BIOSSession::execution() const
{
  return lastExecution;
}

GSPresentation neko_frontend::BIOSSession::videoOutput() const
{
  return system.videoOutput();
}

std::string neko_frontend::BIOSSession::diagnostic() const
{
  std::ostringstream output;
  output
    << "bios: master_cycles=" << elapsed
    << " outcome="
    << (lastExecution.ee.state == EEExecutionState::Halted ||
        lastExecution.iop.state == IOPExecutionState::Halted
          ? "cpu_stopped"
          : elapsed == cycleLimit ? "cycle_limit" : "running")
    << " ee_stop=" << stopReasonName(lastExecution.ee.stopReason)
    << " ee_pc=0x" << std::hex
    << lastExecution.ee.programCounter
    << " iop_stop=" << stopReasonName(lastExecution.iop.stopReason)
    << " iop_pc=0x" << lastExecution.iop.programCounter;
  if (lastExecution.iop.stopReason == IOPStopReason::StoreFailure)
  {
    output << " iop_store_pa=0x"
      << lastExecution.iop.storeFailure.access.physicalAddress
      << " iop_store_value=0x"
      << lastExecution.iop.storeFailure.value;
  }
  return output.str();
}
