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
  REQUIRE(first.spriteCount == 14);
  REQUIRE(first.triangleCount == 22);
  REQUIRE(first.pixelWriteCount > 10000);
  REQUIRE(first.transferredQuadwords == 1395);
  REQUIRE(first.framebufferHash == UINT64_C(0xf80088d2be321719));
  REQUIRE(
    first.rgbaPixels.size() ==
    neko_demo::PRIMITIVE_FRAME_WIDTH *
    neko_demo::PRIMITIVE_FRAME_HEIGHT * 4);
  REQUIRE(first.framebufferHash != second.framebufferHash);
}

TEST_CASE("Alpha Primitive Desktop Scene Compatibility Tests")
{
  const neko_demo::PrimitiveSceneResult scene =
    neko_demo::renderAlphaPrimitiveScene(0);

  REQUIRE(scene.pointCount == 96);
  REQUIRE(scene.lineCount == 12);
  REQUIRE(scene.spriteCount == 14);
  REQUIRE(scene.triangleCount == 18);
  REQUIRE(scene.transferredQuadwords == 1372);
  REQUIRE(scene.framebufferHash == UINT64_C(0xe997218cd72c6cde));
}

TEST_CASE("Textured Primitive Desktop Scene Compatibility Tests")
{
  const neko_demo::PrimitiveSceneResult scene =
    neko_demo::renderTexturedPrimitiveScene(0);

  REQUIRE(scene.pointCount == 96);
  REQUIRE(scene.lineCount == 12);
  REQUIRE(scene.spriteCount == 8);
  REQUIRE(scene.triangleCount == 18);
  REQUIRE(scene.transferredQuadwords == 1345);
  REQUIRE(scene.framebufferHash == UINT64_C(0xb3cd754e485c5305));
}

TEST_CASE("Untextured Primitive Desktop Scene Compatibility Tests")
{
  const neko_demo::PrimitiveSceneResult scene =
    neko_demo::renderUntexturedPrimitiveScene(0);

  REQUIRE(scene.pointCount == 96);
  REQUIRE(scene.lineCount == 12);
  REQUIRE(scene.spriteCount == 7);
  REQUIRE(scene.triangleCount == 18);
  REQUIRE(scene.transferredQuadwords == 308);
  REQUIRE(scene.framebufferHash == UINT64_C(0xee767434a886f3f3));
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

TEST_CASE("Point Line and Sprite Desktop Scene Compatibility Tests")
{
  const neko_demo::PrimitiveSceneResult scene =
    neko_demo::renderPointLineSpriteScene(0);

  REQUIRE(scene.pointCount == 96);
  REQUIRE(scene.lineCount == 12);
  REQUIRE(scene.spriteCount == 7);
  REQUIRE(scene.triangleCount == 0);
  REQUIRE(scene.transferredQuadwords == 262);
  REQUIRE(scene.framebufferHash == UINT64_C(0x4ab06c434c83a3f9));
}
