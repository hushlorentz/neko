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
    "--cycles requires --elf.");
  REQUIRE_THROWS_WITH(
    neko_desktop::parseDesktopOptions(
      {"--elf", "first.elf", "--elf", "second.elf"}),
    "Only one ELF path may be specified.");
}

TEST_CASE("Desktop ELF cycle budgets must be positive integers")
{
  REQUIRE_THROWS_WITH(
    neko_desktop::parseDesktopOptions(
      {"--elf", "guest.elf", "--cycles", "0"}),
    "ELF cycle limit must be positive.");
  REQUIRE_THROWS_WITH(
    neko_desktop::parseDesktopOptions(
      {"--elf", "guest.elf", "--cycles", "-1"}),
    "ELF cycle limit must be positive.");
  REQUIRE_THROWS_WITH(
    neko_desktop::parseDesktopOptions(
      {"--elf", "guest.elf", "--cycles", " -1"}),
    "ELF cycle limit must be positive.");
  REQUIRE_THROWS_WITH(
    neko_desktop::parseDesktopOptions(
      {"--elf", "guest.elf", "--cycles", "+1"}),
    "ELF cycle limit must be positive.");
  REQUIRE_THROWS_WITH(
    neko_desktop::parseDesktopOptions(
      {"--elf", "guest.elf", "--cycles", "12x"}),
    "ELF cycle limit must be positive.");
}
