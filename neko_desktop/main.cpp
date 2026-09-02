#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "desktop_options.hpp"
#include "elf_runner.hpp"
#include "primitive_scene.hpp"
#include "rotation_vu1.hpp"

namespace
{
  constexpr int WINDOW_WIDTH = neko_demo::ROTATION_FRAME_WIDTH;
  constexpr int WINDOW_HEIGHT = neko_demo::ROTATION_FRAME_HEIGHT;
  static_assert(
    neko_demo::PRIMITIVE_FRAME_WIDTH ==
      neko_demo::ROTATION_FRAME_WIDTH &&
    neko_demo::PRIMITIVE_FRAME_HEIGHT ==
      neko_demo::ROTATION_FRAME_HEIGHT,
    "Desktop scenes must use the configured presentation size.");
  constexpr int RGBA_COMPONENT_COUNT = 4;
  constexpr std::uint32_t FRAME_DELAY_MILLISECONDS = 16;
  constexpr std::uint32_t FRAMES_PER_ROTATION_PHASE = 2;

  std::vector<std::string> commandLineArguments(
    int argc,
    char **argv)
  {
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc - 1));
    for (int index = 1; index < argc; ++index)
    {
      arguments.emplace_back(argv[index]);
    }
    return arguments;
  }

  std::vector<std::uint8_t> readBinaryFile(
    const std::string &path,
    const std::string &description)
  {
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
      throw std::runtime_error(
        "Could not open " + description + " at " + path + ".");
    }
    return std::vector<std::uint8_t>(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
  }

  void requireSDL(bool succeeded, const char *operation)
  {
    if (!succeeded)
    {
      throw std::runtime_error(
        std::string(operation) + ": " + SDL_GetError());
    }
  }

  class SDLVideoSession
  {
    public:
      SDLVideoSession()
      {
        requireSDL(
          SDL_Init(SDL_INIT_VIDEO),
          "SDL video initialization failed");
      }

      ~SDLVideoSession()
      {
        SDL_Quit();
      }
  };

  int runDesktop(int argc, char **argv)
  {
    using neko_desktop::DesktopOptions;
    using neko_desktop::DesktopScene;
    const DesktopOptions options =
      neko_desktop::parseDesktopOptions(
        commandLineArguments(argc, argv));
    if (!options.elfPath.empty())
    {
      const neko_frontend::ELFRunReport report =
        neko_frontend::runELFFile(
          options.elfPath,
          options.elfCycleLimit);
      std::cout << report.diagnostic << '\n';
      return report.hostExitCode;
    }
    const std::vector<std::uint8_t> microprogram =
      options.scene == DesktopScene::Rotation
        ? readBinaryFile(
            NEKO_ROTATION_VU1_BINARY,
            "assembled rotation VU1 program")
        : std::vector<std::uint8_t>();

    SDLVideoSession video;

    SDL_Window *rawWindow = nullptr;
    SDL_Renderer *rawRenderer = nullptr;
    const bool windowCreated =
      SDL_CreateWindowAndRenderer(
        options.scene == DesktopScene::Rotation
          ? "Neko - rotation_vu1.asm"
          : options.scene == DesktopScene::PointsAndSprites
            ? "Neko - POINT and SPRITE"
            : options.scene == DesktopScene::PointsLinesAndSprites
              ? "Neko - POINT, LINE, and SPRITE"
              : "Neko - GS Primitive Showcase",
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        0,
        &rawWindow,
        &rawRenderer);
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>
      window(rawWindow, SDL_DestroyWindow);
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>
      renderer(rawRenderer, SDL_DestroyRenderer);
    if (!windowCreated)
    {
      throw std::runtime_error(
        std::string("SDL window creation failed: ") +
        SDL_GetError());
    }

    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>
      texture(
        SDL_CreateTexture(
          renderer.get(),
          SDL_PIXELFORMAT_RGBA32,
          SDL_TEXTUREACCESS_STREAMING,
          WINDOW_WIDTH,
          WINDOW_HEIGHT),
        SDL_DestroyTexture);
    if (texture == nullptr)
    {
      throw std::runtime_error(
        std::string("SDL texture creation failed: ") +
        SDL_GetError());
    }

    requireSDL(
      SDL_SetTextureScaleMode(
        texture.get(),
        SDL_SCALEMODE_NEAREST),
      "SDL texture scale-mode selection failed");
    requireSDL(
      SDL_SetTextureBlendMode(
        texture.get(),
        SDL_BLENDMODE_NONE),
      "SDL texture blend-mode selection failed");
    bool running = true;
    int renderedFrames = 0;
    std::uint64_t firstFramebufferHash = 0;
    std::uint64_t lastFramebufferHash = 0;
    while (running)
    {
      SDL_Event event;
      while (SDL_PollEvent(&event))
      {
        if (event.type == SDL_EVENT_QUIT)
        {
          running = false;
        }
      }

      std::vector<std::uint8_t> rgbaPixels;
      std::uint64_t framebufferHash = 0;
      if (options.scene == DesktopScene::Rotation)
      {
        const std::uint32_t rotationPhase =
          (static_cast<std::uint32_t>(renderedFrames) /
           FRAMES_PER_ROTATION_PHASE) %
          neko_demo::ROTATION_PHASE_COUNT;
        neko_demo::RotationVU1Result rotation =
          neko_demo::renderRotationVU1(
            microprogram,
            rotationPhase);
        if (!rotation.vpuCompleted ||
            !rotation.path1Completed)
        {
          throw std::runtime_error(
            "The rotation graphics workload did not complete.");
        }
        rgbaPixels = std::move(rotation.rgbaPixels);
        framebufferHash = rotation.framebufferHash;
      }
      else
      {
        neko_demo::PrimitiveSceneResult scene;
        if (options.scene == DesktopScene::PointsAndSprites)
        {
          scene = neko_demo::renderPointSpriteScene(
            static_cast<std::uint32_t>(renderedFrames));
        }
        else if (
          options.scene == DesktopScene::PointsLinesAndSprites)
        {
          scene = neko_demo::renderPointLineSpriteScene(
            static_cast<std::uint32_t>(renderedFrames));
        }
        else
        {
          scene =
            options.scene == DesktopScene::UntexturedPrimitives
              ? neko_demo::renderUntexturedPrimitiveScene(
                  static_cast<std::uint32_t>(renderedFrames))
              : options.scene == DesktopScene::TexturedPrimitives
                ? neko_demo::renderTexturedPrimitiveScene(
                    static_cast<std::uint32_t>(renderedFrames))
              : options.scene == DesktopScene::AlphaPrimitives
                ? neko_demo::renderAlphaPrimitiveScene(
                    static_cast<std::uint32_t>(renderedFrames))
              : neko_demo::renderPrimitiveScene(
                  static_cast<std::uint32_t>(renderedFrames));
        }
        rgbaPixels = std::move(scene.rgbaPixels);
        framebufferHash = scene.framebufferHash;
      }
      if (renderedFrames == 0)
      {
        firstFramebufferHash = framebufferHash;
      }
      lastFramebufferHash = framebufferHash;
      requireSDL(
        SDL_UpdateTexture(
          texture.get(),
          nullptr,
          rgbaPixels.data(),
          WINDOW_WIDTH * RGBA_COMPONENT_COUNT),
        "SDL texture upload failed");

      requireSDL(
        SDL_SetRenderDrawColor(
          renderer.get(),
          0,
          0,
          0,
          255),
        "SDL clear-color selection failed");
      requireSDL(
        SDL_RenderClear(renderer.get()),
        "SDL renderer clear failed");
      requireSDL(
        SDL_RenderTexture(
          renderer.get(),
          texture.get(),
          nullptr,
          nullptr),
        "SDL texture rendering failed");
      requireSDL(
        SDL_RenderPresent(renderer.get()),
        "SDL presentation failed");

      ++renderedFrames;
      if (options.frameLimit != 0 &&
          renderedFrames >= options.frameLimit)
      {
        running = false;
      }
      SDL_Delay(FRAME_DELAY_MILLISECONDS);
    }
    if (options.frameLimit != 0)
    {
      std::cout
        << (options.scene == DesktopScene::Rotation
              ? "rotation_vu1"
              : options.scene == DesktopScene::PointsAndSprites
                ? "points_sprites"
                : options.scene ==
                    DesktopScene::PointsLinesAndSprites
                  ? "points_lines_sprites"
                  : options.scene ==
                      DesktopScene::UntexturedPrimitives
                    ? "points_lines_sprites_strips_fans"
                    : options.scene ==
                        DesktopScene::TexturedPrimitives
                      ? "points_lines_sprites_strips_fans_textures"
                      : options.scene ==
                          DesktopScene::AlphaPrimitives
                        ? "points_lines_sprites_strips_fans_textures_alpha"
                        : "points_lines_sprites_strips_fans_textures_alpha_depth")
        << ": frames=" << renderedFrames
        << " first_hash=0x" << std::hex
        << firstFramebufferHash
        << " last_hash=0x" << lastFramebufferHash
        << std::dec << '\n';
    }
    return EXIT_SUCCESS;
  }
}

int main(int argc, char **argv)
{
  try
  {
    return runDesktop(argc, argv);
  }
  catch (const std::exception &error)
  {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
