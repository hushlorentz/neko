#include "elf_runner.hpp"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace
{
  const char *guestOutcomeName(EEGuestOutcome outcome)
  {
    switch (outcome)
    {
      case EEGuestOutcome::Completed:
        return "completed";
      case EEGuestOutcome::GuestReportedFailure:
        return "guest_failure";
      case EEGuestOutcome::CycleLimitReached:
        return "cycle_limit";
      case EEGuestOutcome::Exception:
        return "exception";
      case EEGuestOutcome::Stopped:
        return "stopped";
    }
    throw std::logic_error("Unknown EE guest outcome.");
  }

  std::vector<std::uint8_t> readELFFile(
    const std::string &path)
  {
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
      throw std::runtime_error(
        "Could not open ELF image at " + path + ".");
    }
    return std::vector<std::uint8_t>(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
  }
}

std::string neko_frontend::formatELFRun(
  const EEGuestExecutionResult &result)
{
  std::ostringstream output;
  output
    << "elf: outcome=" << guestOutcomeName(result.outcome)
    << " exit_code=" << result.exitCode
    << " master_cycles=" << result.execution.masterCycles
    << " ee_cycles=" << result.execution.eeCycles
    << " instructions=" << result.execution.instructions
    << " pc=0x" << std::hex << result.execution.programCounter;
  return output.str();
}

int neko_frontend::hostExitCodeFor(EEGuestOutcome outcome)
{
  return outcome == EEGuestOutcome::Completed
    ? EXIT_SUCCESS
    : EXIT_FAILURE;
}

neko_frontend::ELFRunReport neko_frontend::runELFFile(
  const std::string &path,
  std::uint64_t maxMasterCycles)
{
  NekoSystem system;
  ELFRunReport report;
  report.result =
    system.runELF(readELFFile(path), maxMasterCycles);
  report.diagnostic = formatELFRun(report.result);
  report.hostExitCode = hostExitCodeFor(report.result.outcome);
  return report;
}
