#ifndef PRIMITIVE_SCENE_HPP
#define PRIMITIVE_SCENE_HPP

#include <cstdint>
#include <vector>

namespace neko_demo
{
  constexpr std::uint16_t PRIMITIVE_FRAME_WIDTH = 640;
  constexpr std::uint16_t PRIMITIVE_FRAME_HEIGHT = 448;
  constexpr std::uint32_t PRIMITIVE_PHASE_COUNT = 128;

  struct PrimitiveSceneResult
  {
    std::vector<std::uint8_t> rgbaPixels;
    std::uint64_t framebufferHash = 0;
    std::uint64_t pointCount = 0;
    std::uint64_t lineCount = 0;
    std::uint64_t spriteCount = 0;
    std::uint64_t triangleCount = 0;
    std::uint64_t pixelWriteCount = 0;
    std::uint64_t transferredQuadwords = 0;
  };

  PrimitiveSceneResult renderPrimitiveScene(
    std::uint32_t phase);
  PrimitiveSceneResult renderPointSpriteScene(
    std::uint32_t phase);
  PrimitiveSceneResult renderPointLineSpriteScene(
    std::uint32_t phase);
}

#endif
