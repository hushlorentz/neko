#ifndef DESKTOP_OPTIONS_HPP
#define DESKTOP_OPTIONS_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace neko_desktop
{
  constexpr std::uint64_t DEFAULT_ELF_CYCLE_LIMIT = 1000000;

  enum class DesktopScene
  {
    Rotation,
    PointsAndSprites,
    PointsLinesAndSprites,
    UntexturedPrimitives,
    TexturedPrimitives,
    AlphaPrimitives,
    Primitives
  };

  struct DesktopOptions
  {
    DesktopScene scene = DesktopScene::Rotation;
    int frameLimit = 0;
    std::string elfPath;
    std::uint64_t elfCycleLimit = DEFAULT_ELF_CYCLE_LIMIT;
  };

  DesktopOptions parseDesktopOptions(
    const std::vector<std::string> &arguments);
}

#endif
