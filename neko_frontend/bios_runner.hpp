#ifndef BIOS_RUNNER_HPP
#define BIOS_RUNNER_HPP

#include <cstdint>
#include <string>

#include "neko_system.hpp"

namespace neko_frontend
{
  struct BIOSRunReport
  {
    EEExecutionResult result;
    std::uint32_t rejectedInstruction = 0;
    std::string diagnostic;
    int hostExitCode = 0;
  };

  std::string formatBIOSRun(const BIOSRunReport &report);
  BIOSRunReport runBIOSFile(
    const std::string &path,
    std::uint64_t maxMasterCycles);
}

#endif
