#include <cstdlib>
#include <string>

#include "catch.hpp"
#include "elf_runner.hpp"

TEST_CASE("ELF frontend reports deterministic execution details")
{
  EEGuestExecutionResult result;
  result.outcome = EEGuestOutcome::GuestReportedFailure;
  result.exitCode = 7;
  result.execution.masterCycles = 12;
  result.execution.eeCycles = 11;
  result.execution.instructions = 9;
  result.execution.programCounter = UINT32_C(0x80001024);

  REQUIRE(
    neko_frontend::formatELFRun(result) ==
    "elf: outcome=guest_failure exit_code=7 "
    "master_cycles=12 ee_cycles=11 instructions=9 "
    "pc=0x80001024");
}

TEST_CASE("ELF frontend maps only completion to host success")
{
  REQUIRE(
    neko_frontend::hostExitCodeFor(
      EEGuestOutcome::Completed) ==
    EXIT_SUCCESS);
  REQUIRE(
    neko_frontend::hostExitCodeFor(
      EEGuestOutcome::GuestReportedFailure) ==
    EXIT_FAILURE);
  REQUIRE(
    neko_frontend::hostExitCodeFor(
      EEGuestOutcome::CycleLimitReached) ==
    EXIT_FAILURE);
  REQUIRE(
    neko_frontend::hostExitCodeFor(
      EEGuestOutcome::Exception) ==
    EXIT_FAILURE);
  REQUIRE(
    neko_frontend::hostExitCodeFor(
      EEGuestOutcome::Stopped) ==
    EXIT_FAILURE);
}

TEST_CASE("ELF frontend reports file-open failures")
{
  REQUIRE_THROWS_WITH(
    neko_frontend::runELFFile(
      "missing-neko-test-guest.elf",
      1),
    "Could not open ELF image at missing-neko-test-guest.elf.");
}
