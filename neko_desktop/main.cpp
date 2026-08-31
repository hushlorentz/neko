#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <SDL3/SDL.h>

#include "synthetic_triangle.hpp"

namespace
{
  constexpr int WINDOW_WIDTH = 512;
  constexpr int WINDOW_HEIGHT = 512;
  constexpr int RGBA_COMPONENT_COUNT = 4;
  constexpr std::uint32_t FRAME_DELAY_MILLISECONDS = 16;

  int requestedFrameCount(int argc, char **argv)
  {
    if (argc == 1)
    {
      return 0;
    }
    if (argc != 3 ||
        std::string(argv[1]) != "--frames")
    {
      throw std::invalid_argument(
        "Usage: neko_desktop [--frames count]");
    }

    const int count = std::stoi(argv[2]);
    if (count <= 0)
    {
      throw std::invalid_argument(
        "Desktop frame count must be positive.");
    }
    return count;
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
    const int frameLimit = requestedFrameCount(argc, argv);
    const neko_demo::SyntheticTriangleResult triangle =
      neko_demo::renderSyntheticTriangle();
    if (!triangle.vpuCompleted ||
        !triangle.path1Completed)
    {
      throw std::runtime_error(
        "The synthetic graphics workload did not complete.");
    }

    SDLVideoSession video;

    SDL_Window *rawWindow = nullptr;
    SDL_Renderer *rawRenderer = nullptr;
    const bool windowCreated =
      SDL_CreateWindowAndRenderer(
        "Neko - VIF/VU1/GIF/GS Triangle",
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
          SDL_TEXTUREACCESS_STATIC,
          neko_demo::SYNTHETIC_FRAME_WIDTH,
          neko_demo::SYNTHETIC_FRAME_HEIGHT),
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
    requireSDL(
      SDL_UpdateTexture(
        texture.get(),
        nullptr,
        triangle.rgbaPixels.data(),
        neko_demo::SYNTHETIC_FRAME_WIDTH *
          RGBA_COMPONENT_COUNT),
      "SDL texture upload failed");

    bool running = true;
    int renderedFrames = 0;
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
      if (frameLimit != 0 &&
          renderedFrames >= frameLimit)
      {
        running = false;
      }
      SDL_Delay(FRAME_DELAY_MILLISECONDS);
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
