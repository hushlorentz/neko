#include "bios_startup_command.hpp"
#include "catch.hpp"
#include "startup_diagnostics.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
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

  std::vector<std::uint8_t> rejectedDMAROM()
  {
    auto image = discoveryROM();
    const std::vector<std::uint32_t> program = {
      0x3c01bf80, 0x34211500, // IOP DMA register base
      0x3c020080, 0xac22002c, // unmapped SIF0 descriptor at 0x800000
      0x3c020100, 0x34420401, 0xac220028, // start channel 9
      0x34020800, 0xac220070 // enable channel 9 in DPCR2
    };
    for (std::size_t index = 0; index < program.size(); ++index)
    {
      for (std::size_t byte = 0; byte < 4; ++byte)
      {
        image[index * 4 + byte] =
          static_cast<std::uint8_t>(program[index] >> (8 * byte));
      }
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

TEST_CASE("BIOS startup command exports an exact bounded ordered trace prefix",
  "[bios-startup-command][bounded-trace]")
{
  const auto image = discoveryROM();
  for (const std::uint64_t budget : {0, 1, 9, 33})
  {
    CAPTURE(budget);
    NekoSystem reference;
    reference.runDualCPUBIOS(image, 0);
    reference.startTrace();
    reference.runDualCPU(budget);
    const std::uint64_t total = reference.trace().size();
    if (budget != 0)
    {
      REQUIRE(total > 1);
      REQUIRE(reference.trace().front().masterCycle == 1);
    }
    std::ostringstream plain;
    std::ostringstream error;
    const auto loader = [&](const std::string &) { return image; };
    const int plainStatus = neko_cli::runBIOSStartupCommand(
      {"authored.bin", "--cycles", std::to_string(budget)},
      loader, plain, error);
    REQUIRE(plainStatus == 0);
    REQUIRE(error.str().empty());

    for (const std::uint64_t limit :
         {UINT64_C(0), UINT64_C(1), total, total + 1,
          std::numeric_limits<std::uint64_t>::max()})
    {
      CAPTURE(limit);
      std::ostringstream output;
      REQUIRE(neko_cli::runBIOSStartupCommand(
        {"authored.bin", "--cycles", std::to_string(budget),
         "--trace-events", std::to_string(limit)},
        loader, output, error) == plainStatus);
      REQUIRE(error.str().empty());
      const auto captured = std::min(limit, total);
      std::ostringstream expected;
      expected << plain.str() <<
        "{\"type\":\"startup_trace_summary\",\"event_limit\":" << limit <<
        ",\"captured_events\":" << captured <<
        ",\"truncated\":" << (limit < total ? "true" : "false") << "}\n";
      for (std::size_t index = 0; index < captured; ++index)
      {
        const auto &event = reference.trace()[index];
        expected <<
          "{\"type\":\"trace_event\",\"master_cycle\":" << event.masterCycle <<
          ",\"subsystem\":" << static_cast<unsigned>(event.subsystem) <<
          ",\"event_type\":" << static_cast<unsigned>(event.type) <<
          ",\"values\":[" << event.value0 << ',' << event.value1 << ',' <<
          event.value2 << ',' << event.value3 << "]}\n";
      }
      expected << "{\"type\":\"startup_first_failure\",\"trace_truncated\":"
        << (limit < total ? "true" : "false")
        << ",\"execution_boundary\":\"complete_cycle\",\"failure\":null}\n";
      REQUIRE(output.str() == expected.str());
    }
  }
}

TEST_CASE("BIOS startup trace options are validated before loading",
  "[bios-startup-command][bounded-trace]")
{
  for (const std::vector<std::string> options : {
       std::vector<std::string>{"--trace-events"},
       {"--trace-events", "1", "extra"},
       {"--trace-events", "1", "--trace-events", "2"},
       {"--trace", "1"}, {"--trace-events", ""}, {"--trace-events", "-1"},
       {"--trace-events", "+1"}, {"--trace-events", " 1"},
       {"--trace-events", "1x"}, {"--trace-events", "18446744073709551616"}})
  {
    auto arguments = std::vector<std::string>{"authored.bin", "--cycles", "1"};
    arguments.insert(arguments.end(), options.begin(), options.end());
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
      output, error) == 2);
    REQUIRE_FALSE(loaded);
    REQUIRE(output.str().empty());
    REQUIRE_FALSE(error.str().empty());
  }
}

TEST_CASE("BIOS startup trace limits preserve host stops and guest exception handlers",
  "[bios-startup-command][bounded-trace]")
{
  for (const std::uint32_t instruction : {0x40003800u, 0x0000000cu})
  {
    CAPTURE(instruction);
    const auto image = discoveryROM(instruction);
    const auto loader = [&](const std::string &) { return image; };
    std::ostringstream plain;
    std::ostringstream error;
    const int plainStatus = neko_cli::runBIOSStartupCommand(
      {"authored.bin", "--cycles", "17"}, loader, plain, error);
    REQUIRE(plainStatus == (instruction == 0x40003800u ? 1 : 0));
    REQUIRE(error.str().empty());
    for (const std::string limit : {"0", "1", "10000"})
    {
      std::ostringstream output;
      REQUIRE(neko_cli::runBIOSStartupCommand(
        {"authored.bin", "--cycles", "17", "--trace-events", limit},
        loader, output, error) == plainStatus);
      REQUIRE(error.str().empty());
      REQUIRE(output.str().compare(0, plain.str().size(), plain.str()) == 0);
      REQUIRE(output.str().find("\"type\":\"startup_trace_summary\"") !=
        std::string::npos);
    }
  }
}

TEST_CASE("BIOS startup command reports failures while exporting trace records",
  "[bios-startup-command][bounded-trace]")
{
  class FailingTraceBuffer : public std::stringbuf
  {
    std::streamsize xsputn(
      const char *data, std::streamsize size) override
    {
      if (std::string(data, static_cast<std::size_t>(size)).find(
            "\"type\":\"trace_event\"") != std::string::npos)
      {
        return 0;
      }
      return std::stringbuf::xsputn(data, size);
    }
  };
  FailingTraceBuffer buffer;
  std::ostream output(&buffer);
  std::ostringstream error;
  REQUIRE(neko_cli::runBIOSStartupCommand(
    {"authored.bin", "--cycles", "1", "--trace-events", "1"},
    [](const std::string &) { return discoveryROM(); },
    output, error) == 2);
  REQUIRE(buffer.str().find("\"type\":\"startup_trace_summary\"") !=
    std::string::npos);
  REQUIRE(buffer.str().find("\"type\":\"trace_event\"") == std::string::npos);
  REQUIRE(error.str().find("Cannot write BIOS startup report") !=
    std::string::npos);
}

TEST_CASE("BIOS startup command attributes captured faults without stopping handlers",
  "[bios-startup-command][startup-failure]")
{
  const auto image = discoveryROM(0x8c020001); // lw v0, 1(zero)
  std::ostringstream output;
  std::ostringstream error;
  REQUIRE(neko_cli::runBIOSStartupCommand(
    {"authored.bin", "--cycles", "17", "--trace-events", "10000"},
    [&](const std::string &) { return image; }, output, error) == 0);
  REQUIRE(error.str().empty());
  REQUIRE(output.str().find("\"type\":\"startup_first_failure\"") !=
    std::string::npos);
  REQUIRE(output.str().find("\"kind\":\"memory_access\",\"cpu\":\"ee\"") !=
    std::string::npos);
  REQUIRE(output.str().find("\"pc\":3217031168,\"bits\":2348941313") !=
    std::string::npos);
  REQUIRE(output.str().find("\"virtual_address\":1,\"physical_address\":null") !=
    std::string::npos);
}

TEST_CASE("BIOS startup command retains runtime error evidence and an incomplete boundary",
  "[bios-startup-command][startup-failure]")
{
  const auto image = rejectedDMAROM();
  for (const std::string limit : {"", "0", "1", "10000"})
  {
    CAPTURE(limit);
    const bool traced = !limit.empty();
    std::vector<std::string> arguments = {"authored.bin", "--cycles", "100"};
    if (traced)
    {
      arguments.insert(arguments.end(), {"--trace-events", limit});
    }
    std::ostringstream output;
    std::ostringstream error;
    REQUIRE(neko_cli::runBIOSStartupCommand(arguments,
      [&](const std::string &) { return image; }, output, error) == 2);
    REQUIRE(error.str().find("IOP SIF0 DMA rejected") != std::string::npos);
    REQUIRE(output.str().find("\"type\":\"bios_startup_input\"") !=
      std::string::npos);
    REQUIRE(output.str().find("\"type\":\"startup_runtime_error\"") !=
      std::string::npos);
    REQUIRE(output.str().find("\"execution_boundary\":\"incomplete_cycle\"") !=
      std::string::npos);
    REQUIRE(output.str().find("\"type\":\"startup_run_summary\"") ==
      std::string::npos);
    REQUIRE((output.str().find("\"type\":\"startup_trace_summary\"") !=
      std::string::npos) == traced);
  }
}

TEST_CASE("BIOS runtime errors remain visible when report output also fails",
  "[bios-startup-command][startup-failure]")
{
  const auto image = rejectedDMAROM();
  std::ostringstream output;
  output.setstate(std::ios::badbit);
  std::ostringstream error;
  REQUIRE(neko_cli::runBIOSStartupCommand(
    {"authored.bin", "--cycles", "100", "--trace-events", "10000"},
    [&](const std::string &) { return image; }, output, error) == 2);
  REQUIRE(error.str().find("IOP SIF0 DMA rejected") != std::string::npos);
  REQUIRE(error.str().find("Cannot write BIOS startup report") != std::string::npos);
}

TEST_CASE("BIOS first failure reports missing context rather than inferring it past the cap",
  "[bios-startup-command][startup-failure]")
{
  const auto image = discoveryROM(0x8c020001);
  for (const std::string limit : {"0", "1"})
  {
    std::ostringstream output;
    std::ostringstream error;
    REQUIRE(neko_cli::runBIOSStartupCommand(
      {"authored.bin", "--cycles", "17", "--trace-events", limit},
      [&](const std::string &) { return image; }, output, error) == 0);
    REQUIRE(error.str().empty());
    REQUIRE(output.str().find("\"type\":\"startup_first_failure\","
      "\"trace_truncated\":true,\"execution_boundary\":\"complete_cycle\","
      "\"failure\":null") != std::string::npos);
  }
}
