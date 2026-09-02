#include "desktop_options.hpp"

#include <stdexcept>

namespace
{
  const char *USAGE =
    "Usage: neko_desktop [--scene <name>] [--frames <count>] "
    "| --elf <path> [--cycles <count>]";

  std::uint64_t parsePositiveInteger(
    const std::string &text,
    const char *error)
  {
    if (text.empty())
    {
      throw std::invalid_argument(error);
    }
    for (char character : text)
    {
      if (character < '0' || character > '9')
      {
        throw std::invalid_argument(error);
      }
    }
    std::size_t consumed = 0;
    std::uint64_t value = 0;
    try
    {
      value = std::stoull(text, &consumed);
    }
    catch (const std::exception &)
    {
      throw std::invalid_argument(error);
    }
    if (consumed != text.size() || value == 0)
    {
      throw std::invalid_argument(error);
    }
    return value;
  }

  neko_desktop::DesktopScene parseScene(
    const std::string &scene)
  {
    using neko_desktop::DesktopScene;
    if (scene == "rotation")
    {
      return DesktopScene::Rotation;
    }
    if (scene == "points-sprites")
    {
      return DesktopScene::PointsAndSprites;
    }
    if (scene == "points-lines-sprites")
    {
      return DesktopScene::PointsLinesAndSprites;
    }
    if (scene == "points-lines-sprites-strips-fans")
    {
      return DesktopScene::UntexturedPrimitives;
    }
    if (scene == "points-lines-sprites-strips-fans-textures")
    {
      return DesktopScene::TexturedPrimitives;
    }
    if (scene ==
        "points-lines-sprites-strips-fans-textures-alpha")
    {
      return DesktopScene::AlphaPrimitives;
    }
    if (scene ==
          "points-lines-sprites-strips-fans-textures-alpha-depth" ||
        scene == "primitives")
    {
      return DesktopScene::Primitives;
    }
    throw std::invalid_argument("Desktop scene is invalid.");
  }
}

neko_desktop::DesktopOptions
neko_desktop::parseDesktopOptions(
  const std::vector<std::string> &arguments)
{
  DesktopOptions options;
  bool sceneSpecified = false;
  bool cyclesSpecified = false;
  for (std::size_t index = 0;
       index < arguments.size();
       ++index)
  {
    const std::string &argument = arguments[index];
    if (argument == "--frames" && index + 1 < arguments.size())
    {
      const std::uint64_t frames = parsePositiveInteger(
        arguments[++index],
        "Desktop frame count must be positive.");
      if (frames > static_cast<std::uint64_t>(INT32_MAX))
      {
        throw std::invalid_argument(
          "Desktop frame count is too large.");
      }
      options.frameLimit = static_cast<int>(frames);
    }
    else if (argument == "--scene" &&
             index + 1 < arguments.size())
    {
      options.scene = parseScene(arguments[++index]);
      sceneSpecified = true;
    }
    else if (argument == "--elf" &&
             index + 1 < arguments.size())
    {
      if (!options.elfPath.empty())
      {
        throw std::invalid_argument(
          "Only one ELF path may be specified.");
      }
      options.elfPath = arguments[++index];
      if (options.elfPath.empty())
      {
        throw std::invalid_argument(
          "ELF path must not be empty.");
      }
    }
    else if (argument == "--cycles" &&
             index + 1 < arguments.size())
    {
      options.elfCycleLimit = parsePositiveInteger(
        arguments[++index],
        "ELF cycle limit must be positive.");
      cyclesSpecified = true;
    }
    else
    {
      throw std::invalid_argument(USAGE);
    }
  }

  if (!options.elfPath.empty() &&
      (sceneSpecified || options.frameLimit != 0))
  {
    throw std::invalid_argument(
      "ELF execution cannot be combined with a scene or frame limit.");
  }
  if (options.elfPath.empty() && cyclesSpecified)
  {
    throw std::invalid_argument(
      "--cycles requires --elf.");
  }
  return options;
}
