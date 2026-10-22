#include "bios_startup_command.hpp"
#include "catch.hpp"
#include "startup_diagnostics.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace
{
  std::vector<std::uint8_t> discoveryROM(
    std::uint32_t first = 0x24420001)
  {
    std::vector<std::uint8_t> image(BootROMImage::SIZE, 0);
    for (std::size_t byte = 0; byte < 4; ++byte)
    {
      image[byte] = static_cast<std::uint8_t>(first >> (8 * byte));
    }
    return image;
  }
}

TEST_CASE("BIOS startup command reproduces the dual runner and identifies its input",
  "[bios-startup-command]")
{
  const auto image = discoveryROM();
  for (const std::uint64_t budget : {0, 1, 8, 9, 17})
  {
    CAPTURE(budget);
    std::ostringstream output;
    std::ostringstream error;
    int loads = 0;
    const int result = neko_cli::runBIOSStartupCommand(
      {"authored.bin", "--cycles", std::to_string(budget)},
      [&](const std::string &path)
      {
        REQUIRE(path == "authored.bin");
        ++loads;
        return image;
      },
      output,
      error);
    REQUIRE(result == 0);
    REQUIRE(loads == 1);
    REQUIRE(error.str().empty());

    NekoSystem expected;
    const auto report =
      runDualCPUBIOSWithStartupSummary(expected, image, budget);
    std::ostringstream expectedOutput;
    expectedOutput <<
      "{\"type\":\"bios_startup_input\",\"rom_bytes\":4194304,"
      "\"rom_fnv1a64\":\"" <<
      std::hex << std::setfill('0') << std::setw(16) <<
      expected.eeBus().bootROMIdentity()->fingerprint() <<
      std::dec << "\",\"max_master_cycles\":" << budget <<
      ",\"start\":\"cold_dual_reset_vectors\"}\n";
    writeStartupRunSummaryJsonLine(expectedOutput, report);
    REQUIRE(output.str() == expectedOutput.str());
  }
}

TEST_CASE("BIOS startup command reports host stops without claiming BIOS success",
  "[bios-startup-command]")
{
  std::ostringstream output;
  std::ostringstream error;
  const auto image = discoveryROM(0x40003800); // unsupported EE COP0 register
  const int result = neko_cli::runBIOSStartupCommand(
    {"authored.bin", "--cycles", "18446744073709551615"},
    [&](const std::string &) { return image; },
    output,
    error);
  REQUIRE(result == 1);
  REQUIRE(error.str().empty());
  REQUIRE(output.str().find("\"master_cycles\":1") != std::string::npos);
  REQUIRE(output.str().find("\"unsupported_instruction\"") !=
    std::string::npos);
  REQUIRE(output.str().find("\"max_master_cycles\":18446744073709551615") !=
    std::string::npos);
}

TEST_CASE("BIOS startup command validates arguments before file loading",
  "[bios-startup-command]")
{
  const std::vector<std::vector<std::string>> invalid = {
    {}, {"authored.bin"}, {"authored.bin", "--cycles"},
    {"", "--cycles", "1"}, {"authored.bin", "--unknown", "1"},
    {"authored.bin", "--cycles", "1", "extra"},
    {"authored.bin", "--cycles", "1", "--cycles", "2"}
  };
  for (const auto &arguments : invalid)
  {
    std::ostringstream output;
    std::ostringstream error;
    bool loaded = false;
    REQUIRE(neko_cli::runBIOSStartupCommand(
      arguments,
      [&](const std::string &)
      {
        loaded = true;
        return discoveryROM();
      },
      output,
      error) == 2);
    REQUIRE_FALSE(loaded);
    REQUIRE(output.str().empty());
    REQUIRE(error.str().find("Usage: neko bios-startup") != std::string::npos);
  }
}

TEST_CASE("BIOS startup command rejects malformed and overflowing cycle ceilings",
  "[bios-startup-command]")
{
  for (const std::string budget :
       {"", "-1", "+1", " 1", "1 ", "1x", "0x10",
        "18446744073709551616", "999999999999999999999999"})
  {
    CAPTURE(budget);
    std::ostringstream output;
    std::ostringstream error;
    bool loaded = false;
    REQUIRE(neko_cli::runBIOSStartupCommand(
      {"authored.bin", "--cycles", budget},
      [&](const std::string &)
      {
        loaded = true;
        return discoveryROM();
      },
      output,
      error) == 2);
    REQUIRE_FALSE(loaded);
    REQUIRE(output.str().empty());
    REQUIRE(error.str().find("unsigned 64-bit decimal") != std::string::npos);
  }
}

TEST_CASE("BIOS startup command surfaces file and image failures",
  "[bios-startup-command]")
{
  std::ostringstream output;
  std::ostringstream error;
  SECTION("Loader failure")
  {
    REQUIRE(neko_cli::runBIOSStartupCommand(
      {"missing.bin", "--cycles", "1"},
      [](const std::string &) -> std::vector<std::uint8_t>
      {
        throw std::runtime_error("Authored file failure.");
      },
      output,
      error) == 2);
    REQUIRE(error.str() == "Authored file failure.\n");
  }
  SECTION("Short image")
  {
    REQUIRE(neko_cli::runBIOSStartupCommand(
      {"short.bin", "--cycles", "1"},
      [](const std::string &) { return std::vector<std::uint8_t>(4); },
      output,
      error) == 2);
    REQUIRE_FALSE(error.str().empty());
  }
  SECTION("Oversized image")
  {
    REQUIRE(neko_cli::runBIOSStartupCommand(
      {"large.bin", "--cycles", "1"},
      [](const std::string &)
      {
        return std::vector<std::uint8_t>(BootROMImage::SIZE + 1);
      },
      output,
      error) == 2);
    REQUIRE_FALSE(error.str().empty());
  }
  SECTION("Real file loader rejects a short non-ROM file")
  {
    REQUIRE(neko_cli::runBIOSStartupCommand(
      {__FILE__, "--cycles", "1"}, output, error) == 2);
    REQUIRE(error.str().find("exactly 4194304 bytes") != std::string::npos);
  }
  REQUIRE(output.str().empty());
}

TEST_CASE("BIOS startup command surfaces report output failures",
  "[bios-startup-command]")
{
  std::ostringstream output;
  std::ostringstream error;
  output.setstate(std::ios::badbit);
  REQUIRE(neko_cli::runBIOSStartupCommand(
    {"authored.bin", "--cycles", "0"},
    [](const std::string &) { return discoveryROM(); },
    output,
    error) == 2);
  REQUIRE(error.str().find("Cannot write BIOS startup report") !=
    std::string::npos);
}

TEST_CASE("BIOS startup command flushes buffered reports before selecting status",
  "[bios-startup-command]")
{
  class FailingSyncBuffer : public std::stringbuf
  {
    int sync() override
    {
      return -1;
    }
  };
  FailingSyncBuffer buffer;
  std::ostream output(&buffer);
  std::ostringstream error;
  REQUIRE(neko_cli::runBIOSStartupCommand(
    {"authored.bin", "--cycles", "0"},
    [](const std::string &) { return discoveryROM(); },
    output,
    error) == 2);
  REQUIRE_FALSE(buffer.str().empty());
  REQUIRE(error.str().find("Cannot write BIOS startup report") !=
    std::string::npos);
}
