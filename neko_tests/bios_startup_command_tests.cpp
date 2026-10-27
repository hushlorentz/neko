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

TEST_CASE("BIOS startup trace can begin at a completed absolute cycle without changing execution",
  "[bios-startup-command][checkpoint]")
{
  const auto image = discoveryROM();
  std::ostringstream output;
  std::ostringstream error;
  REQUIRE(neko_cli::runBIOSStartupCommand(
    {"authored.bin", "--cycles", "17", "--trace-events", "100",
      "--trace-from-cycle", "8", "--failure-context"},
    [&](const std::string &) { return image; }, output, error) == 0);
  REQUIRE(error.str().empty());
  NekoSystem expected;
  expected.runDualCPUBIOS(image, 8);
  expected.startTrace(100, std::make_shared<StartupFailureCapture>());
  const auto report = runDualCPUWithStartupSummary(expected, 9);
  std::ostringstream snapshot;
  writeStartupSummaryJsonLine(snapshot, report.startup);
  const auto state = snapshot.str();
  const auto first = state.find("\"snapshot\":");
  REQUIRE(output.str().find(state.substr(first, state.size() - first - 1)) !=
    std::string::npos);
  std::ostringstream trace;
  writeStartupTraceJsonLines(trace, expected);
  REQUIRE(output.str().find(trace.str()) != std::string::npos);
  REQUIRE(output.str().find("\"master_cycles\":17") != std::string::npos);
  REQUIRE(output.str().find("\"trace_start_cycle\":8") != std::string::npos);
}

TEST_CASE("BIOS startup checkpoints preserve canonical state and absolute continuation ceilings",
  "[bios-startup-command][checkpoint]")
{
  const auto image = discoveryROM();
  std::vector<std::uint8_t> checkpoint;
  const auto loader = [&](const std::string &) { return image; };
  std::ostringstream creation;
  std::ostringstream errors;
  REQUIRE(neko_cli::runBIOSStartupCommand(
    {"authored.bin", "--cycles", "8", "--save-checkpoint", "prefix.state"},
    loader, {}, [&](const std::string &path, const std::vector<std::uint8_t> &state)
    {
      REQUIRE(path == "prefix.state");
      checkpoint = state;
    }, creation, errors) == 0);
  NekoSystem expected;
  expected.runDualCPUBIOS(image, 8);
  REQUIRE(checkpoint == expected.saveState());

  for (const std::uint64_t ceiling : {8u, 17u})
  {
    CAPTURE(ceiling);
    std::vector<std::uint8_t> coldState;
    std::vector<std::uint8_t> resumedState;
    std::ostringstream cold;
    std::ostringstream resumed;
    const std::vector<std::string> arguments = {
      "authored.bin", "--cycles", std::to_string(ceiling),
      "--trace-events", "100", "--failure-context",
      "--trace-from-cycle", "8", "--save-checkpoint", "final.state"
    };
    REQUIRE(neko_cli::runBIOSStartupCommand(arguments, loader, {},
      [&](const std::string &, const std::vector<std::uint8_t> &state)
      { coldState = state; }, cold, errors) == 0);
    auto resumeArguments = arguments;
    resumeArguments.insert(resumeArguments.end(),
      {"--resume-checkpoint", "prefix.state"});
    REQUIRE(neko_cli::runBIOSStartupCommand(resumeArguments, loader,
      [&](const std::string &path)
      {
        REQUIRE(path == "prefix.state");
        return checkpoint;
      }, [&](const std::string &, const std::vector<std::uint8_t> &state)
      { resumedState = state; }, resumed, errors) == 0);
    REQUIRE(errors.str().empty());
    REQUIRE(coldState == resumedState);
    const auto suffix = cold.str().find("{\"type\":\"startup_trace_summary\"");
    const auto resumedSuffix =
      resumed.str().find("{\"type\":\"startup_trace_summary\"");
    REQUIRE(suffix != std::string::npos);
    REQUIRE(resumedSuffix != std::string::npos);
    REQUIRE(cold.str().substr(suffix) == resumed.str().substr(resumedSuffix));
    REQUIRE(resumed.str().find("\"start\":\"checkpoint\",\"start_master_cycle\":8") !=
      std::string::npos);
    REQUIRE(resumed.str().find("\"master_cycles\":" +
      std::to_string(ceiling - 8)) != std::string::npos);
  }
}

TEST_CASE("BIOS startup rejects incompatible checkpoints and invalid resume bounds",
  "[bios-startup-command][checkpoint]")
{
  auto image = discoveryROM();
  NekoSystem source;
  source.runDualCPUBIOS(image, 8);
  auto state = source.saveState();
  std::vector<std::string> arguments = {
    "authored.bin", "--cycles", "17", "--resume-checkpoint", "prefix.state"
  };
  SECTION("Ceiling precedes checkpoint") { arguments[2] = "7"; }
  SECTION("Trace start precedes checkpoint")
  {
    arguments.insert(arguments.end(),
      {"--trace-events", "100", "--trace-from-cycle", "7"});
  }
  SECTION("Corrupt state") { state[0] ^= 1; }
  SECTION("Different ROM") { image.back() ^= 1; }
  std::ostringstream output;
  std::ostringstream errors;
  REQUIRE(neko_cli::runBIOSStartupCommand(arguments,
    [&](const std::string &) { return image; },
    [&](const std::string &) { return state; }, {}, output, errors) == 2);
  REQUIRE(output.str().empty());
  REQUIRE_FALSE(errors.str().empty());
}

TEST_CASE("BIOS startup delayed tracing preserves early host stops and incomplete boundaries",
  "[bios-startup-command][checkpoint]")
{
  for (const bool runtimeFailure : {false, true})
  {
    CAPTURE(runtimeFailure);
    std::ostringstream output;
    std::ostringstream errors;
    bool saved = false;
    const auto image = runtimeFailure ? rejectedDMAROM() : discoveryROM(0x40003800);
    const int result = neko_cli::runBIOSStartupCommand(
      {"authored.bin", "--cycles", "200", "--trace-events", "100",
        "--trace-from-cycle", "100", "--failure-context",
        "--save-checkpoint", "final.state"},
      [&](const std::string &) { return image; }, {},
      [&](const std::string &, const std::vector<std::uint8_t> &) { saved = true; },
      output, errors);
    REQUIRE(result == (runtimeFailure ? 2 : 1));
    REQUIRE(saved == !runtimeFailure);
    REQUIRE(output.str().find("\"captured_events\":0,\"truncated\":false") !=
      std::string::npos);
    REQUIRE(output.str().find(runtimeFailure ?
      "\"execution_boundary\":\"incomplete_cycle\"" : "\"master_cycles\":1") !=
      std::string::npos);
  }
}

TEST_CASE("BIOS startup checkpoint output failures are explicit and existing files are preserved",
  "[bios-startup-command][checkpoint]")
{
  std::ostringstream output;
  std::ostringstream errors;
  SECTION("Existing destination")
  {
    REQUIRE(neko_cli::runBIOSStartupCommand(
      {"authored.bin", "--cycles", "0", "--save-checkpoint", __FILE__},
      [](const std::string &) { return discoveryROM(); }, output, errors) == 2);
    REQUIRE(errors.str().find("destination must not exist") != std::string::npos);
  }
  SECTION("Writer failure")
  {
    REQUIRE(neko_cli::runBIOSStartupCommand(
      {"authored.bin", "--cycles", "0", "--save-checkpoint", "prefix.state"},
      [](const std::string &) { return discoveryROM(); }, {},
      [](const std::string &, const std::vector<std::uint8_t> &)
      { throw std::runtime_error("Authored checkpoint write failure."); },
      output, errors) == 2);
    REQUIRE(errors.str().find("Authored checkpoint write failure") != std::string::npos);
  }
  REQUIRE_FALSE(output.str().empty());
}

TEST_CASE("BIOS startup failure context survives an exhausted prefix",
  "[bios-startup-command][failure-capture]")
{
  std::ostringstream output;
  std::ostringstream error;
  REQUIRE(neko_cli::runBIOSStartupCommand(
    {"authored.bin", "--cycles", "9", "--trace-events", "0", "--failure-context"},
    [](const std::string &) { return discoveryROM(0x40003800); },
    output, error) == 1);
  REQUIRE(error.str().empty());
  REQUIRE(output.str().find("\"captured_events\":0,\"truncated\":true") !=
    std::string::npos);
  REQUIRE(output.str().find("\"type\":\"startup_failure_capture\"") !=
    std::string::npos);
  REQUIRE(output.str().find("\"event_source\":\"failure_capture\"") !=
    std::string::npos);
  REQUIRE(output.str().find("\"kind\":\"instruction_rejection\",\"cpu\":\"ee\"") !=
    std::string::npos);
  REQUIRE(output.str().find("\"pc\":3217031168,\"bits\":1073756160") !=
    std::string::npos);
}

TEST_CASE("BIOS startup failure capture keeps empty and incomplete outcomes explicit",
  "[bios-startup-command][failure-capture]")
{
  SECTION("Empty capture at zero budget")
  {
    std::ostringstream output;
    std::ostringstream error;
    REQUIRE(neko_cli::runBIOSStartupCommand(
      {"authored.bin", "--cycles", "0", "--trace-events", "0", "--failure-context"},
      [](const std::string &) { return discoveryROM(); }, output, error) == 0);
    REQUIRE(error.str().empty());
    REQUIRE(output.str().find("\"observed_events\":0,\"first_event_index\":0,"
      "\"failure_cycle_truncated\":false,\"events\":[]") != std::string::npos);
    REQUIRE(output.str().find("\"failure\":null") != std::string::npos);
  }
  SECTION("Runtime error after an earlier first failure")
  {
    std::ostringstream output;
    std::ostringstream error;
    REQUIRE(neko_cli::runBIOSStartupCommand(
      {"authored.bin", "--cycles", "100", "--trace-events", "0", "--failure-context"},
      [](const std::string &) { return rejectedDMAROM(); }, output, error) == 2);
    REQUIRE(error.str().find("IOP SIF0 DMA rejected") != std::string::npos);
    REQUIRE(output.str().find("\"type\":\"startup_runtime_error\"") != std::string::npos);
    REQUIRE(output.str().find("\"execution_boundary\":\"incomplete_cycle\","
      "\"event_source\":\"failure_capture\",\"failure\":{\"kind\":\"memory_access\"") !=
      std::string::npos);
  }
}

TEST_CASE("BIOS startup command validates arguments before file loading",
  "[bios-startup-command]")
{
  const std::vector<std::vector<std::string>> invalid = {
    {}, {"authored.bin"}, {"authored.bin", "--cycles"},
    {"", "--cycles", "1"}, {"authored.bin", "--unknown", "1"},
    {"authored.bin", "--cycles", "1", "extra"},
    {"authored.bin", "--cycles", "1", "--cycles", "2"},
    {"authored.bin", "--cycles", "1", "--failure-context"},
    {"authored.bin", "--cycles", "1", "--trace-events", "0", "--unknown"},
    {"authored.bin", "--cycles", "1", "--trace-events", "0",
      "--failure-context", "--failure-context"},
    {"authored.bin", "--cycles", "1", "--resume-checkpoint", ""},
    {"authored.bin", "--cycles", "1", "--trace-from-cycle", "0"},
    {"authored.bin", "--cycles", "1", "--trace-events", "0",
      "--trace-from-cycle", "2"},
    {"authored.bin", "--cycles", "1", "--save-checkpoint", "a",
      "--save-checkpoint", "b"}
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

TEST_CASE("BIOS runtime report failures preserve execution error precedence at every record stage",
  "[bios-startup-command][startup-failure][diagnostic-conformance]")
{
  class FailingRecordBuffer : public std::stringbuf
  {
    public:
      explicit FailingRecordBuffer(const std::string &type)
        : marker("\"type\":\"" + type + "\"")
      {
      }

      bool rejected = false;

    private:
      std::streamsize xsputn(const char *data, std::streamsize size) override
      {
        if (std::string(data, static_cast<std::size_t>(size)).find(marker) !=
            std::string::npos)
        {
          rejected = true;
          return 0;
        }
        return std::stringbuf::xsputn(data, size);
      }

      std::string marker;
  };
  const auto image = rejectedDMAROM();
  for (const std::string type : {"startup_runtime_error", "startup_trace_summary",
      "trace_event", "startup_first_failure"})
  {
    CAPTURE(type);
    FailingRecordBuffer buffer(type);
    std::ostream output(&buffer);
    std::ostringstream error;
    REQUIRE(neko_cli::runBIOSStartupCommand(
      {"authored.bin", "--cycles", "100", "--trace-events", "10000"},
      [&](const std::string &) { return image; }, output, error) == 2);
    REQUIRE(buffer.rejected);
    REQUIRE(buffer.str().find("\"type\":\"" + type + "\"") == std::string::npos);
    const auto executionError = error.str().find("IOP SIF0 DMA rejected");
    const auto outputError = error.str().find("Cannot write BIOS startup report");
    REQUIRE(executionError != std::string::npos);
    REQUIRE(outputError != std::string::npos);
    REQUIRE(executionError < outputError);
    REQUIRE(buffer.str().find("\"type\":\"startup_run_summary\"") ==
      std::string::npos);
    if (type == "startup_first_failure")
    {
      REQUIRE(buffer.str().find("\"type\":\"startup_runtime_error\"") !=
        std::string::npos);
      REQUIRE(buffer.str().find("\"type\":\"trace_event\"") !=
        std::string::npos);
    }
  }
}
