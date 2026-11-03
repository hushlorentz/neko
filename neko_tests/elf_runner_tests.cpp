#include <cstdlib>
#include <string>
#include <vector>

#include "catch.hpp"
#include "bios_runner.hpp"
#include "elf_runner.hpp"

namespace
{
  std::vector<std::uint8_t> frontendBIOS(
    const std::vector<std::uint32_t> &instructions)
  {
    std::vector<std::uint8_t> image(BootROMImage::SIZE, 0);
    for (std::size_t index = 0; index < instructions.size(); ++index)
    {
      for (std::size_t byte = 0; byte < 4; ++byte)
      {
        image[index * 4 + byte] = static_cast<std::uint8_t>(
          instructions[index] >> (byte * 8));
      }
    }
    return image;
  }
}

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

TEST_CASE("BIOS frontend reports deterministic stop details")
{
  neko_frontend::BIOSRunReport report;
  report.result.masterCycles = 12;
  report.result.eeCycles = 11;
  report.result.instructions = 9;
  report.result.state = EEExecutionState::Halted;
  report.result.stopReason = EEStopReason::UnsupportedInstruction;
  report.result.programCounter = UINT32_C(0xbfc00020);
  report.rejectedInstruction = UINT32_C(0x01234567);

  REQUIRE(
    neko_frontend::formatBIOSRun(report) ==
    "bios: master_cycles=12 ee_cycles=11 instructions=9 "
    "state=halted stop=unsupported_instruction "
    "pc=0xbfc00020 rejected=0x1234567 "
    "exception=none exception_address=0x0");
}

TEST_CASE("BIOS frontend reports file-open failures")
{
  REQUIRE_THROWS_WITH(
    neko_frontend::runBIOSFile(
      "missing-neko-test-bios.bin",
      1),
    "Could not open BIOS image at missing-neko-test-bios.bin.");
}

TEST_CASE("BIOS window session shares one exact dual CPU budget",
  "[frontend][bios][desktop]")
{
  neko_frontend::BIOSSession session(frontendBIOS({}), 17);
  REQUIRE_FALSE(session.finished());
  for (const std::uint64_t elapsed : {7, 14, 17})
  {
    session.advance(7);
    REQUIRE(session.masterCycles() == elapsed);
    REQUIRE(session.execution().ee.programCounter ==
      EEReset::VECTOR + elapsed * 4);
    REQUIRE(session.execution().iop.programCounter ==
      IOPReset::VECTOR + ((elapsed + 7) / 8) * 4);
  }
  REQUIRE(session.finished());
  REQUIRE(session.diagnostic().find("outcome=cycle_limit") !=
    std::string::npos);
  session.advance(UINT64_MAX);
  REQUIRE(session.masterCycles() == 17);
  REQUIRE(session.execution().iop.programCounter == IOPReset::VECTOR + 12);
  REQUIRE_THROWS_WITH(session.advance(0),
    "BIOS slice budget must be positive.");
  REQUIRE_THROWS_WITH(
    neko_frontend::BIOSSession(frontendBIOS({}), 0),
    "BIOS cycle limit must be positive.");
}

TEST_CASE("BIOS window session retains precise CPU stops",
  "[frontend][bios][desktop]")
{
  SECTION("EE stop freezes the session before its budget")
  {
    neko_frontend::BIOSSession session(
      frontendBIOS({0x40003800}), 100);
    session.advance(100);
    REQUIRE(session.finished());
    REQUIRE(session.masterCycles() == 1);
    REQUIRE(session.execution().ee.stopReason ==
      EEStopReason::UnsupportedInstruction);
    REQUIRE(session.execution().iop.state == IOPExecutionState::Running);
    REQUIRE(session.diagnostic().find("ee_stop=unsupported_instruction") !=
      std::string::npos);
    session.advance(100);
    REQUIRE(session.masterCycles() == 1);
  }
  SECTION("IOP stop freezes the session even at the budget boundary")
  {
    neko_frontend::BIOSSession session(
      frontendBIOS({0x3c011000, 0xac200000}), 9);
    session.advance(8);
    REQUIRE_FALSE(session.finished());
    session.advance(8);
    REQUIRE(session.finished());
    REQUIRE(session.masterCycles() == 9);
    REQUIRE(session.execution().ee.state == EEExecutionState::Running);
    REQUIRE(session.execution().iop.stopReason == IOPStopReason::StoreFailure);
    REQUIRE(session.execution().iop.storeFailure.access.physicalAddress ==
      0x10000000);
    REQUIRE(session.diagnostic().find("outcome=cpu_stopped") !=
      std::string::npos);
    REQUIRE(session.diagnostic().find("iop_store_pa=0x10000000") !=
      std::string::npos);
    session.advance(100);
    REQUIRE(session.masterCycles() == 9);
  }
}

TEST_CASE("BIOS window session exposes guest configured GS output",
  "[frontend][bios][desktop]")
{
  neko_frontend::BIOSSession session(
    frontendBIOS({0x3c011200, 0x24020001, 0xfc220000}), 17);
  REQUIRE(session.videoOutput().rgba.empty());
  session.advance(17);
  const auto video = session.videoOutput();
  REQUIRE(video.width == 1);
  REQUIRE(video.height == 1);
  REQUIRE(video.rgba.size() == 4);
}
