#include "catch.hpp"
#include "primitive_scene.hpp"

TEST_CASE("Primitive Desktop Scene Tests")
{
  const neko_demo::PrimitiveSceneResult first =
    neko_demo::renderPrimitiveScene(0);
  const neko_demo::PrimitiveSceneResult second =
    neko_demo::renderPrimitiveScene(32);

  REQUIRE(first.pointCount == 96);
  REQUIRE(first.lineCount == 12);
  REQUIRE(first.spriteCount == 7);
  REQUIRE(first.pixelWriteCount > 10000);
  REQUIRE(first.transferredQuadwords == 262);
  REQUIRE(
    first.rgbaPixels.size() ==
    neko_demo::PRIMITIVE_FRAME_WIDTH *
    neko_demo::PRIMITIVE_FRAME_HEIGHT * 4);
  REQUIRE(first.framebufferHash != second.framebufferHash);
}
