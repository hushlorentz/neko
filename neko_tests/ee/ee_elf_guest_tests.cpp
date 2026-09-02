#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "catch.hpp"
#include "elf_runner.hpp"
#include "neko_system.hpp"

namespace
{
  std::string guestPath(const std::string &fileName)
  {
    return
      std::string(NEKO_EE_ELF_GUEST_DIR) + "/" + fileName;
  }

  std::vector<std::uint8_t> readGuest(
    const std::string &fileName)
  {
    const std::string path = guestPath(fileName);
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
      throw std::runtime_error(
        "Could not open EE ELF guest: " + path);
    }
    return std::vector<std::uint8_t>(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
  }
}

TEST_CASE("PS2DEV scalar EE ELF guests complete successfully")
{
  struct GuestExpectation
  {
    const char *fileName;
    std::uint64_t instructions;
  };
  const GuestExpectation guests[] = {
    {"arithmetic.elf", 16},
    {"branches.elf", 12},
    {"memory.elf", 23}
  };

  for (const GuestExpectation &guest : guests)
  {
    CAPTURE(guest.fileName);
    NekoSystem system;
    const EEGuestExecutionResult result =
      system.runELF(readGuest(guest.fileName), 128);

    REQUIRE(result.outcome == EEGuestOutcome::Completed);
    REQUIRE(result.exitCode == 0);
    REQUIRE(
      result.execution.instructions ==
      guest.instructions);
    REQUIRE_FALSE(result.execution.cycleLimitReached);
    REQUIRE(
      result.execution.programCounter ==
      EEGuestRuntime::RETURN_ADDRESS);
  }
}

TEST_CASE("PS2DEV EE ELF guest runs through frontend support")
{
  const neko_frontend::ELFRunReport report =
    neko_frontend::runELFFile(
      guestPath("arithmetic.elf"),
      128);

  REQUIRE(report.result.outcome == EEGuestOutcome::Completed);
  REQUIRE(report.result.exitCode == 0);
  REQUIRE(report.hostExitCode == 0);
  REQUIRE(
    report.diagnostic.find("outcome=completed") !=
    std::string::npos);
}
