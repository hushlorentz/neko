#ifndef ELF_RUNNER_HPP
#define ELF_RUNNER_HPP

#include <cstdint>
#include <string>

#include "neko_system.hpp"

namespace neko_frontend
{
  struct ELFRunReport
  {
    EEGuestExecutionResult result;
    std::string diagnostic;
    int hostExitCode = 0;
  };

  std::string formatELFRun(
    const EEGuestExecutionResult &result);
  int hostExitCodeFor(EEGuestOutcome outcome);
  ELFRunReport runELFFile(
    const std::string &path,
    std::uint64_t maxMasterCycles);
}

#endif
