#ifndef ROTATION_VU1_HPP
#define ROTATION_VU1_HPP

#include <cstdint>
#include <vector>

namespace neko_demo
{
  constexpr std::uint16_t ROTATION_FRAME_WIDTH = 640;
  constexpr std::uint16_t ROTATION_FRAME_HEIGHT = 448;
  constexpr std::uint32_t ROTATION_PHASE_COUNT = 64;

  struct RotationVU1Result
  {
    std::vector<std::uint8_t> rgbaPixels;
    std::uint64_t framebufferHash = 0;
    std::uint64_t transferredQuadwords = 0;
    std::uint64_t triangleCount = 0;
    std::uint64_t pixelWriteCount = 0;
    std::uint32_t cycleCount = 0;
    bool vpuCompleted = false;
    bool path1Completed = false;
  };

  RotationVU1Result renderRotationVU1(
    const std::vector<std::uint8_t> &microprogram,
    std::uint32_t phase);
}

#endif
