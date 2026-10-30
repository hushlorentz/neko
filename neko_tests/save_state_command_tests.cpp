#include "catch.hpp"
#include "save_state_commands.hpp"

#include "neko_system.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
  class TemporarySaveStateFile
  {
    public:
      explicit TemporarySaveStateFile(
        const std::vector<std::uint8_t> &bytes) :
        path("neko_save_state_command_test.state")
      {
        std::ofstream file(path, std::ios::binary);
        file.write(
          reinterpret_cast<const char *>(bytes.data()),
          static_cast<std::streamsize>(bytes.size()));
        if (!file)
        {
          throw std::runtime_error(
            "Could not create temporary save state.");
        }
      }

      ~TemporarySaveStateFile()
      {
        std::remove(path.c_str());
      }

      std::string path;
  };

  neko_cli::SaveStateFileLoader loaderFor(
    const std::vector<std::uint8_t> &left,
    const std::vector<std::uint8_t> &right = {})
  {
    return
      [=](const std::string &path)
      {
        if (path == "left.state")
        {
          return left;
        }
        if (path == "right.state" && !right.empty())
        {
          return right;
        }
        throw std::runtime_error("Cannot read save state: " + path);
      };
  }
}

TEST_CASE("Save-state inspect commands format observed fields")
{
  NekoSystem system;
  std::ostringstream output;
  std::ostringstream error;

  const int result = neko_cli::runSaveStateCommand(
    {
      "inspect",
      "left.state",
      "--bytes",
      "container.magic"
    },
    loaderFor(system.saveState()),
    output,
    error);

  REQUIRE(result == 0);
  REQUIRE(error.str().empty());
  REQUIRE(
    output.str().find(
      "container.magic bytes size=8 "
      "fnv1a64=0xad5d00c4b2c17fe4 "
      "data=4e454b4f53544154") !=
    std::string::npos);
  REQUIRE(
    output.str().find(
      "system.input.buttons u16 0x0000") !=
    std::string::npos);
}

TEST_CASE("Save-state commands read binary files")
{
  NekoSystem system;
  const TemporarySaveStateFile file(system.saveState());
  std::ostringstream output;
  std::ostringstream error;

  const int result = neko_cli::runSaveStateCommand(
    {"locate", file.path, "container.magic", "--bytes"},
    output,
    error);

  REQUIRE(result == 0);
  REQUIRE(error.str().empty());
  REQUIRE(
    output.str().find("data=4e454b4f53544154") !=
    std::string::npos);
}

TEST_CASE("Save-state IOP memory inspection distinguishes RAM from cached words",
  "[checkpoint-memory]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  REQUIRE(bus.write32(0x100, 0x24020001).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x104, 0x24030002).status == IOPBusStatus::Completed);
  auto &core = system.iopCore();
  core.startExecution(0x100);
  core.stepInstruction();
  REQUIRE(bus.write32(0x100, 0x24020009).status == IOPBusStatus::Completed);
  const auto state = system.saveState();
  std::ostringstream output;
  std::ostringstream error;
  const int result = neko_cli::runSaveStateCommand(
    {"iop-memory", "left.state", "0x100", "--words", "2"},
    loaderFor(state), output, error);
  INFO(error.str());
  REQUIRE(result == 0);
  REQUIRE(error.str().empty());
  REQUIRE(output.str().find(
    "0x00000100 ram=0x24020009 instruction={index=16 tag=0x00000000 "
    "valid=true match=true word=0x24020001}") != std::string::npos);
  REQUIRE(output.str().find(
    "data={index=64 tag=0x00000000 valid=false match=false word=0x00000000}") !=
    std::string::npos);
  REQUIRE(output.str().find("0x00000104 ram=0x24030002") != std::string::npos);
  REQUIRE(system.saveState() == state);

  std::ostringstream aliasOutput;
  std::ostringstream aliasError;
  REQUIRE(neko_cli::runSaveStateCommand(
    {"iop-memory", "left.state", "4352", "--words", "1"},
    loaderFor(state), aliasOutput, aliasError) == 0);
  REQUIRE(aliasError.str().empty());
  REQUIRE(aliasOutput.str().find(
    "instruction={index=16 tag=0x00000000 valid=true match=false "
    "word=0x24020001}") != std::string::npos);

  REQUIRE(bus.write32(0x1ffffc, 0x87654321).status == IOPBusStatus::Completed);
  std::ostringstream endOutput;
  std::ostringstream endError;
  REQUIRE(neko_cli::runSaveStateCommand(
    {"iop-memory", "left.state", "0x1ffffc", "--words", "1"},
    loaderFor(system.saveState()), endOutput, endError) == 0);
  REQUIRE(endError.str().empty());
  REQUIRE(endOutput.str().find("0x001ffffc ram=0x87654321") == 0);
}

TEST_CASE("Save-state IOP memory inspection bounds input before loading",
  "[checkpoint-memory]")
{
  unsigned int loads = 0;
  const neko_cli::SaveStateFileLoader loader =
    [&](const std::string &) -> std::vector<std::uint8_t>
    {
      ++loads;
      throw std::runtime_error("unexpected load");
    };
  for (const auto &arguments : std::vector<std::vector<std::string>>{
      {"iop-memory", "left.state", "0x101", "--words", "1"},
      {"iop-memory", "left.state", "0x200000", "--words", "1"},
      {"iop-memory", "left.state", "0x1ffffc", "--words", "2"},
      {"iop-memory", "left.state", "0", "--words", "0"},
      {"iop-memory", "left.state", "0", "--words", "257"},
      {"iop-memory", "left.state", "-4", "--words", "1"},
      {"iop-memory", "left.state", "0x", "--words", "1"},
      {"iop-memory", "left.state", "4294967296", "--words", "1"},
      {"iop-memory", "left.state", "0", "--words", "18446744073709551616"},
      {"iop-memory", "left.state", "0", "--words", "1", "extra"}})
  {
    std::ostringstream output;
    std::ostringstream error;
    REQUIRE(neko_cli::runSaveStateCommand(arguments, loader, output, error) == 2);
    REQUIRE(output.str().empty());
    REQUIRE_FALSE(error.str().empty());
  }
  REQUIRE(loads == 0);

  std::ostringstream output;
  std::ostringstream error;
  REQUIRE(neko_cli::runSaveStateCommand(
    {"iop-memory", "left.state", "0", "--words", "1"},
    loaderFor({1, 2, 3}), output, error) == 2);
  REQUIRE(output.str().empty());
  REQUIRE_FALSE(error.str().empty());
}

TEST_CASE("Save-state EE memory inspection reports bounded backing words and provenance",
  "[checkpoint-memory][ee-memory]")
{
  NekoSystem system;
  REQUIRE(system.eeBus().writeData32(0x100, 0x24020081));
  REQUIRE(system.eeBus().writeData32(0x104, 0xa4620000));
  REQUIRE(system.eeBus().writeData32(0x1fffffc, 0x87654321));
  const auto state = system.saveState();
  const auto loader = loaderFor(state);
  std::ostringstream output;
  std::ostringstream error;
  REQUIRE(neko_cli::runSaveStateCommand(
    {"ee-memory", "left.state", "0x100", "--words", "2"},
    loader, output, error) == 0);
  REQUIRE(error.str().empty());
  REQUIRE(output.str().find(
    "EE physical RAM backing only; not an instruction-cache or fetch view.\n") == 0);
  REQUIRE(output.str().find("container.version u32 0x") !=
    std::string::npos);
  REQUIRE(output.str().find("container.checksum u64 0x") != std::string::npos);
  REQUIRE(output.str().find(
    "system.masterClock.masterCycle u64 0x0000000000000000 container=") !=
    std::string::npos);
  REQUIRE(output.str().find("system.eeBus.romPresent bool false container=") !=
    std::string::npos);
  REQUIRE(output.str().find(
    "system.eeBus.romFingerprint u64 0x0000000000000000 container=") !=
    std::string::npos);
  REQUIRE(output.str().find(
    "0x00000100 ram=0x24020081\n0x00000104 ram=0xa4620000\n") !=
    std::string::npos);
  REQUIRE(system.saveState() == state);

  std::ostringstream endOutput;
  std::ostringstream endError;
  REQUIRE(neko_cli::runSaveStateCommand(
    {"ee-memory", "left.state", "0x1fffffc", "--words", "1"},
    loader, endOutput, endError) == 0);
  REQUIRE(endError.str().empty());
  REQUIRE(endOutput.str().find("0x01fffffc ram=0x87654321\n") !=
    std::string::npos);

  std::ostringstream maximumOutput;
  std::ostringstream maximumError;
  REQUIRE(neko_cli::runSaveStateCommand(
    {"ee-memory", "left.state", "256", "--words", "256"},
    loader, maximumOutput, maximumError) == 0);
  REQUIRE(maximumError.str().empty());
  REQUIRE(maximumOutput.str().find("0x000004fc ram=0x00000000\n") !=
    std::string::npos);
  REQUIRE(maximumOutput.str().find("0x00000500 ram=") == std::string::npos);
}

TEST_CASE("Save-state EE memory inspection rejects invalid ranges before loading",
  "[checkpoint-memory][ee-memory]")
{
  unsigned int loads = 0;
  const neko_cli::SaveStateFileLoader loader =
    [&](const std::string &) -> std::vector<std::uint8_t>
    {
      ++loads;
      throw std::runtime_error("unexpected load");
    };
  for (const auto &arguments : std::vector<std::vector<std::string>>{
      {"ee-memory", "left.state", "0x101", "--words", "1"},
      {"ee-memory", "left.state", "0x2000000", "--words", "1"},
      {"ee-memory", "left.state", "0x1fffffc", "--words", "2"},
      {"ee-memory", "left.state", "0x80000100", "--words", "1"},
      {"ee-memory", "left.state", "0xa0000100", "--words", "1"},
      {"ee-memory", "left.state", "0x20000100", "--words", "1"},
      {"ee-memory", "left.state", "0x1a000000", "--words", "1"},
      {"ee-memory", "left.state", "0", "--words", "0"},
      {"ee-memory", "left.state", "0", "--words", "257"},
      {"ee-memory", "left.state", "-4", "--words", "1"},
      {"ee-memory", "left.state", "0x", "--words", "1"},
      {"ee-memory", "left.state", "4294967296", "--words", "1"},
      {"ee-memory", "left.state", "0", "--words", "18446744073709551616"},
      {"ee-memory", "left.state", "0", "--words", "1", "extra"},
      {"ee-memory", "", "0", "--words", "1"},
      {"ee-memory", "left.state", "0", "--bytes", "1"},
      {"ee-memory", "left.state", "0"}})
  {
    std::ostringstream output;
    std::ostringstream error;
    REQUIRE(neko_cli::runSaveStateCommand(arguments, loader, output, error) == 2);
    REQUIRE(output.str().empty());
    REQUIRE_FALSE(error.str().empty());
  }
  REQUIRE(loads == 0);
}

TEST_CASE("Save-state EE memory inspection emits no evidence for invalid files",
  "[checkpoint-memory][ee-memory]")
{
  NekoSystem system;
  auto corruptState = system.saveState();
  corruptState.back() ^= 0x80;
  for (const auto &loader : {
      loaderFor({1, 2, 3}), loaderFor(corruptState), loaderFor({})})
  {
    std::ostringstream output;
    std::ostringstream error;
    REQUIRE(neko_cli::runSaveStateCommand(
      {"ee-memory", "left.state", "0", "--words", "1"},
      loader, output, error) == 2);
    REQUIRE(output.str().empty());
    REQUIRE_FALSE(error.str().empty());
    REQUIRE(error.str().find("Usage:") == std::string::npos);
  }
}

TEST_CASE("Save-state locate commands use exact schema paths")
{
  NekoSystem system;

  SECTION("Located fields")
  {
    std::ostringstream output;
    std::ostringstream error;
    const int result = neko_cli::runSaveStateCommand(
      {"locate", "left.state", "system.vu0.mode"},
      loaderFor(system.saveState()),
      output,
      error);

    REQUIRE(result == 0);
    REQUIRE(error.str().empty());
    REQUIRE(
      output.str().find("system.vu0.mode u8") == 0);
  }

  SECTION("Missing fields")
  {
    std::ostringstream output;
    std::ostringstream error;
    const int result = neko_cli::runSaveStateCommand(
      {"locate", "left.state", "system.vu0.missing"},
      loaderFor(system.saveState()),
      output,
      error);

    REQUIRE(result == 1);
    REQUIRE(output.str().empty());
    REQUIRE(
      error.str() ==
      "Save-state field not found: system.vu0.missing\n");
  }

  SECTION("Expanded byte ranges")
  {
    std::ostringstream output;
    std::ostringstream error;
    const int result = neko_cli::runSaveStateCommand(
      {"locate", "left.state", "container.magic", "--bytes"},
      loaderFor(system.saveState()),
      output,
      error);

    REQUIRE(result == 0);
    REQUIRE(error.str().empty());
    REQUIRE(
      output.str().find("data=4e454b4f53544154") !=
      std::string::npos);
  }
}

TEST_CASE("Save-state diff commands report semantic changes")
{
  NekoSystem leftSystem;
  NekoSystem rightSystem;
  NekoInputState input;
  input.buttons = 0x1234;
  rightSystem.setInput(input);

  SECTION("Different states")
  {
    std::ostringstream output;
    std::ostringstream error;
    const int result = neko_cli::runSaveStateCommand(
      {"diff", "left.state", "right.state"},
      loaderFor(
        leftSystem.saveState(),
        rightSystem.saveState()),
      output,
      error);

    REQUIRE(result == 1);
    REQUIRE(error.str().empty());
    REQUIRE(
      output.str() ==
      "changed system.input.buttons: 0x0000 -> 0x1234\n");
  }

  SECTION("Equal states")
  {
    const std::vector<std::uint8_t> state =
      leftSystem.saveState();
    std::ostringstream output;
    std::ostringstream error;
    const int result = neko_cli::runSaveStateCommand(
      {"diff", "left.state", "right.state"},
      loaderFor(state, state),
      output,
      error);

    REQUIRE(result == 0);
    REQUIRE(error.str().empty());
    REQUIRE(output.str() == "No semantic differences.\n");
  }
}

TEST_CASE("Save-state commands reject invalid inputs")
{
  NekoSystem system;

  SECTION("Usage")
  {
    std::ostringstream output;
    std::ostringstream error;
    const int result = neko_cli::runSaveStateCommand(
      {"inspect"},
      loaderFor(system.saveState()),
      output,
      error);

    REQUIRE(result == 2);
    REQUIRE(output.str().empty());
    REQUIRE(
      error.str().find(
        "Usage: neko save-state inspect") !=
      std::string::npos);
  }

  SECTION("File errors")
  {
    std::ostringstream output;
    std::ostringstream error;
    const int result = neko_cli::runSaveStateCommand(
      {"inspect", "missing.state"},
      loaderFor(system.saveState()),
      output,
      error);

    REQUIRE(result == 2);
    REQUIRE(output.str().empty());
    REQUIRE(
      error.str().find(
        "Cannot read save state: missing.state") !=
      std::string::npos);
  }

  SECTION("Malformed states")
  {
    std::ostringstream output;
    std::ostringstream error;
    const int result = neko_cli::runSaveStateCommand(
      {"inspect", "left.state"},
      loaderFor({0}),
      output,
      error);

    REQUIRE(result == 2);
    REQUIRE(output.str().empty());
    REQUIRE(
      error.str().find("data is truncated") !=
      std::string::npos);
  }

  SECTION("Unknown byte paths")
  {
    std::ostringstream output;
    std::ostringstream error;
    const int result = neko_cli::runSaveStateCommand(
      {
        "inspect",
        "left.state",
        "--bytes",
        "system.missing"
      },
      loaderFor(system.saveState()),
      output,
      error);

    REQUIRE(result == 2);
    REQUIRE(output.str().empty());
    REQUIRE(
      error.str() ==
      "Save-state byte field not found: system.missing\n");
  }
}
