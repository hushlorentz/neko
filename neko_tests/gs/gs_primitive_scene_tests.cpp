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

TEST_CASE("Point and Sprite Desktop Scene Compatibility Tests")
{
  const neko_demo::PrimitiveSceneResult scene =
    neko_demo::renderPointSpriteScene(0);

  REQUIRE(scene.pointCount == 96);
  REQUIRE(scene.lineCount == 0);
  REQUIRE(scene.spriteCount == 7);
  REQUIRE(scene.transferredQuadwords == 226);
  REQUIRE(scene.framebufferHash == UINT64_C(0xc1adcf6554c82b99));
}
