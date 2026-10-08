#include <cstdint>
#include <string>
#include <vector>

#include "catch.hpp"
#include "desktop_options.hpp"

using neko_desktop::DesktopOptions;
using neko_desktop::DesktopScene;

TEST_CASE("Desktop options preserve the default rotation scene")
{
  const DesktopOptions options =
    neko_desktop::parseDesktopOptions({});

  REQUIRE(options.scene == DesktopScene::Rotation);
  REQUIRE(options.frameLimit == 0);
  REQUIRE(options.elfPath.empty());
  REQUIRE(
    options.elfCycleLimit ==
    neko_desktop::DEFAULT_ELF_CYCLE_LIMIT);
}

TEST_CASE("Desktop options parse headless ELF execution")
{
  SECTION("Default cycle budget")
  {
    const DesktopOptions options =
      neko_desktop::parseDesktopOptions(
        {"--elf", "guest.elf"});

    REQUIRE(options.elfPath == "guest.elf");
    REQUIRE(
      options.elfCycleLimit ==
      neko_desktop::DEFAULT_ELF_CYCLE_LIMIT);
  }

  SECTION("Explicit cycle budget")
  {
    const DesktopOptions options =
      neko_desktop::parseDesktopOptions(
        {"--elf", "guest.elf", "--cycles", "12345"});

    REQUIRE(options.elfPath == "guest.elf");
    REQUIRE(options.elfCycleLimit == 12345);
  }
}

TEST_CASE("Desktop options parse bounded BIOS execution")
{
  SECTION("Default cycle budget")
  {
    const DesktopOptions options =
      neko_desktop::parseDesktopOptions(
        {"--bios", "SCPH-70012.bin"});

    REQUIRE(options.biosPath == "SCPH-70012.bin");
    REQUIRE(
      options.biosCycleLimit ==
      neko_desktop::DEFAULT_BIOS_CYCLE_LIMIT);
  }

  SECTION("Explicit cycle budget")
  {
    const DesktopOptions options =
      neko_desktop::parseDesktopOptions(
        {"--bios", "SCPH-70012.bin", "--cycles", "12345"});

    REQUIRE(options.biosPath == "SCPH-70012.bin");
    REQUIRE(options.biosCycleLimit == 12345);
  }
}

TEST_CASE("Desktop ELF options reject ambiguous execution modes")
{
  REQUIRE_THROWS_WITH(
    neko_desktop::parseDesktopOptions(
      {"--elf", "guest.elf", "--scene", "primitives"}),
    "ELF execution cannot be combined with a scene or frame limit.");
  REQUIRE_THROWS_WITH(
    neko_desktop::parseDesktopOptions(
      {"--frames", "1", "--elf", "guest.elf"}),
    "ELF execution cannot be combined with a scene or frame limit.");
  REQUIRE_THROWS_WITH(
    neko_desktop::parseDesktopOptions({"--cycles", "10"}),
    "--cycles requires --elf or --bios.");
  REQUIRE_THROWS_WITH(
    neko_desktop::parseDesktopOptions(
      {"--elf", "first.elf", "--elf", "second.elf"}),
    "Only one ELF path may be specified.");
  REQUIRE_THROWS_WITH(
    neko_desktop::parseDesktopOptions(
      {"--elf", "guest.elf", "--bios", "bios.bin"}),
    "ELF and BIOS execution modes are mutually exclusive.");
  REQUIRE_THROWS_WITH(
    neko_desktop::parseDesktopOptions(
      {"--bios", "bios.bin", "--scene", "primitives"}),
    "BIOS execution cannot be combined with a scene or frame limit.");
}

TEST_CASE("Desktop ELF cycle budgets must be positive integers")
{
  REQUIRE_THROWS_WITH(
    neko_desktop::parseDesktopOptions(
      {"--elf", "guest.elf", "--cycles", "0"}),
    "Execution cycle limit must be positive.");
  REQUIRE_THROWS_WITH(
    neko_desktop::parseDesktopOptions(
      {"--elf", "guest.elf", "--cycles", "-1"}),
    "Execution cycle limit must be positive.");
  REQUIRE_THROWS_WITH(
    neko_desktop::parseDesktopOptions(
      {"--elf", "guest.elf", "--cycles", " -1"}),
    "Execution cycle limit must be positive.");
  REQUIRE_THROWS_WITH(
    neko_desktop::parseDesktopOptions(
      {"--elf", "guest.elf", "--cycles", "+1"}),
    "Execution cycle limit must be positive.");
  REQUIRE_THROWS_WITH(
    neko_desktop::parseDesktopOptions(
      {"--elf", "guest.elf", "--cycles", "12x"}),
    "Execution cycle limit must be positive.");
}
