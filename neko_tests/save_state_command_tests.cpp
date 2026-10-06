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
