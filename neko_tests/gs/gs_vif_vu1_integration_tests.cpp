#include <array>
#include <cstddef>
#include <cstdint>

#include "catch.hpp"
#include "synthetic_triangle.hpp"

namespace
{
  constexpr std::uint32_t FIXED_POINT_ONE = 16;
  constexpr std::uint32_t FIXED_POINT_FOUR = 64;
  constexpr std::uint64_t PACKET_QUADWORD_COUNT = 11;
  constexpr std::uint64_t EXPECTED_PIXEL_WRITES = 6;
  constexpr std::uint64_t EXPECTED_FRAMEBUFFER_HASH =
    UINT64_C(0x108089dcd964d365);
  constexpr std::size_t RGBA_COMPONENT_COUNT = 4;
  constexpr std::uint8_t COLOR_RED = 0x10;
  constexpr std::uint8_t COLOR_GREEN = 0x20;
  constexpr std::uint8_t COLOR_BLUE = 0x40;
  constexpr std::uint8_t COLOR_ALPHA = 0x80;
}

TEST_CASE("VIF to VU1 to GS Graphics Integration Tests")
{
  const neko_demo::SyntheticTriangleResult result =
    neko_demo::renderSyntheticTriangle();

  REQUIRE(result.vpuCompleted);
  REQUIRE(result.path1Completed);
  REQUIRE(
    result.transferredQuadwords ==
    PACKET_QUADWORD_COUNT);
  REQUIRE(
    result.transformedVertices[0] ==
    GIFQuadword{{
      FIXED_POINT_ONE,
      FIXED_POINT_ONE,
      0,
      0
    }});
  REQUIRE(
    result.transformedVertices[1] ==
    GIFQuadword{{
      FIXED_POINT_FOUR,
      FIXED_POINT_ONE,
      0,
      0
    }});
  REQUIRE(
    result.transformedVertices[2] ==
    GIFQuadword{{
      FIXED_POINT_ONE,
      FIXED_POINT_FOUR,
      0,
      0
    }});
  REQUIRE(result.triangleCount == 1);
  REQUIRE(result.pixelWriteCount == EXPECTED_PIXEL_WRITES);
  REQUIRE(
    result.framebufferHash ==
    EXPECTED_FRAMEBUFFER_HASH);

  const std::size_t expectedByteCount =
    neko_demo::SYNTHETIC_FRAME_WIDTH *
    neko_demo::SYNTHETIC_FRAME_HEIGHT *
    RGBA_COMPONENT_COUNT;
  REQUIRE(result.rgbaPixels.size() == expectedByteCount);
  const std::size_t coveredPixel =
    (neko_demo::SYNTHETIC_FRAME_WIDTH + 1) *
    RGBA_COMPONENT_COUNT;
  REQUIRE(result.rgbaPixels[coveredPixel] == COLOR_RED);
  REQUIRE(result.rgbaPixels[coveredPixel + 1] == COLOR_GREEN);
  REQUIRE(result.rgbaPixels[coveredPixel + 2] == COLOR_BLUE);
  REQUIRE(result.rgbaPixels[coveredPixel + 3] == COLOR_ALPHA);
}
