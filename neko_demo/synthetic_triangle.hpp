#ifndef SYNTHETIC_TRIANGLE_HPP
#define SYNTHETIC_TRIANGLE_HPP

#include <array>
#include <cstdint>
#include <vector>

#include "gif.hpp"

namespace neko_demo
{
  constexpr std::uint16_t SYNTHETIC_FRAME_WIDTH = 8;
  constexpr std::uint16_t SYNTHETIC_FRAME_HEIGHT = 8;

  struct SyntheticTriangleResult
  {
    std::vector<std::uint8_t> rgbaPixels;
    std::array<GIFQuadword, 3> transformedVertices = {};
    std::uint64_t framebufferHash = 0;
    std::uint64_t transferredQuadwords = 0;
    std::uint64_t triangleCount = 0;
    std::uint64_t pixelWriteCount = 0;
    bool vpuCompleted = false;
    bool path1Completed = false;
  };

  SyntheticTriangleResult renderSyntheticTriangle();
}

#endif
