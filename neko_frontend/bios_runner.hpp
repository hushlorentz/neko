#ifndef BIOS_RUNNER_HPP
#define BIOS_RUNNER_HPP

#include <cstdint>
#include <string>

#include "neko_system.hpp"

namespace neko_frontend
{
  class BIOSSession
  {
    public:
      BIOSSession(
        const std::vector<std::uint8_t> &image,
        std::uint64_t maxMasterCycles);

      void advance(std::uint64_t maxSliceCycles);
      bool finished() const;
      std::uint64_t masterCycles() const;
      const DualCPUExecutionResult &execution() const;
      GSPresentation videoOutput() const;
      std::string diagnostic() const;

    private:
      NekoSystem system;
      const std::uint64_t cycleLimit;
      std::uint64_t elapsed = 0;
      DualCPUExecutionResult lastExecution;
  };

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
