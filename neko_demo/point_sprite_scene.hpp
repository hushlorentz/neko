#ifndef POINT_SPRITE_SCENE_HPP
#define POINT_SPRITE_SCENE_HPP

#include <cstdint>
#include <vector>

namespace neko_demo
{
  constexpr std::uint16_t POINT_SPRITE_FRAME_WIDTH = 640;
  constexpr std::uint16_t POINT_SPRITE_FRAME_HEIGHT = 448;
  constexpr std::uint32_t POINT_SPRITE_PHASE_COUNT = 128;

  struct PointSpriteSceneResult
  {
    std::vector<std::uint8_t> rgbaPixels;
    std::uint64_t framebufferHash = 0;
    std::uint64_t pointCount = 0;
    std::uint64_t spriteCount = 0;
    std::uint64_t pixelWriteCount = 0;
    std::uint64_t transferredQuadwords = 0;
  };

  PointSpriteSceneResult renderPointSpriteScene(
    std::uint32_t phase);
}

#endif
