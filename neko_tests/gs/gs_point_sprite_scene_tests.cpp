#include "catch.hpp"
#include "point_sprite_scene.hpp"

TEST_CASE("Point and Sprite Desktop Scene Tests")
{
  const neko_demo::PointSpriteSceneResult first =
    neko_demo::renderPointSpriteScene(0);
  const neko_demo::PointSpriteSceneResult second =
    neko_demo::renderPointSpriteScene(32);

  REQUIRE(first.pointCount == 96);
  REQUIRE(first.spriteCount == 7);
  REQUIRE(first.pixelWriteCount > 10000);
  REQUIRE(first.transferredQuadwords == 226);
  REQUIRE(
    first.rgbaPixels.size() ==
    neko_demo::POINT_SPRITE_FRAME_WIDTH *
    neko_demo::POINT_SPRITE_FRAME_HEIGHT * 4);
  REQUIRE(first.framebufferHash != second.framebufferHash);
}
