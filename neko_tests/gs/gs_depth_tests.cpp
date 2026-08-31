#include <cstdint>

#include "catch.hpp"
#include "gs.hpp"

namespace
{
  constexpr std::uint16_t FIXED_POINT_ONE = 16;

  std::uint64_t frameValue()
  {
    return UINT64_C(1) | (UINT64_C(1) << 16);
  }

  std::uint64_t scissorValue()
  {
    return (UINT64_C(7) << 16) | (UINT64_C(7) << 48);
  }

  std::uint64_t colorValue(std::uint8_t red)
  {
    return red | UINT64_C(0xff000000);
  }

  std::uint64_t vertexValue(
    std::uint16_t x,
    std::uint16_t y,
    std::uint32_t z)
  {
    return
      x |
      (static_cast<std::uint64_t>(y) << 16) |
      (static_cast<std::uint64_t>(z) << 32);
  }

  std::uint64_t testValue(std::uint8_t method)
  {
    return
      (UINT64_C(1) << 16) |
      (static_cast<std::uint64_t>(method) << 17);
  }

  void configure(GS *gs, bool masked = false)
  {
    gs->writeRegister(GSRegisterAddress::FRAME_1, frameValue());
    gs->writeRegister(
      GSRegisterAddress::SCISSOR_1,
      scissorValue());
    gs->writeRegister(
      GSRegisterAddress::ZBUF_1,
      UINT64_C(2) |
      (static_cast<std::uint64_t>(masked) << 32));
  }

  void drawPoint(GS *gs, std::uint32_t z, std::uint8_t red)
  {
    gs->writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Point));
    gs->writeRegister(
      GSRegisterAddress::RGBAQ,
      colorValue(red));
    gs->writeRegister(
      GSRegisterAddress::XYZ2,
      vertexValue(FIXED_POINT_ONE, FIXED_POINT_ONE, z));
  }

  void submitTriangleVertex(
    GS *gs,
    std::uint16_t x,
    std::uint16_t y,
    std::uint32_t z)
  {
    gs->writeRegister(GSRegisterAddress::RGBAQ, colorValue(90));
    gs->writeRegister(
      GSRegisterAddress::XYZ2,
      vertexValue(x, y, z));
  }
}

TEST_CASE("GS PSMZ32 Storage Tests")
{
  GS gs;
  configure(&gs);

  REQUIRE(gs.context(0).depthBuffer.basePointer == 2);
  REQUIRE(
    gs.context(0).depthBuffer.pixelStorageMode ==
    GSPixelStorageMode::PSMZ32);
  REQUIRE_FALSE(gs.context(0).depthBuffer.drawingMasked);
  REQUIRE(gs.psmz32WordAddress(0, 0, 0) == 5632);

  gs.writePSMZ32(0, 7, 7, UINT32_C(0x12345678));
  REQUIRE(gs.readPSMZ32(0, 7, 7) == 0x12345678);

  gs.writeRegister(
    GSRegisterAddress::ZBUF_1,
    UINT64_C(1) << 24);
  REQUIRE_THROWS_WITH(
    gs.readPSMZ32(0, 0, 0),
    "GS depth buffer is not configured for PSMZ32.");
}

TEST_CASE("GS Depth Comparison Tests")
{
  SECTION("NEVER rejects every fragment")
  {
    GS gs;
    configure(&gs);
    gs.writePSMCT32(0, 1, 1, UINT32_C(0xff000063));
    gs.writePSMZ32(0, 1, 1, 40);
    gs.writeRegister(GSRegisterAddress::TEST_1, testValue(0));
    drawPoint(&gs, 80, 10);

    REQUIRE(gs.readPSMCT32(0, 1, 1) == 0xff000063);
    REQUIRE(gs.readPSMZ32(0, 1, 1) == 40);
  }

  SECTION("ALWAYS writes color and depth")
  {
    GS gs;
    configure(&gs);
    gs.writeRegister(GSRegisterAddress::TEST_1, testValue(1));
    drawPoint(&gs, 40, 10);

    REQUIRE(gs.readPSMCT32(0, 1, 1) == 0xff00000a);
    REQUIRE(gs.readPSMZ32(0, 1, 1) == 40);
  }

  SECTION("GEQUAL accepts equal and closer fragments")
  {
    GS gs;
    configure(&gs);
    gs.writePSMZ32(0, 1, 1, 40);
    gs.writeRegister(GSRegisterAddress::TEST_1, testValue(2));
    drawPoint(&gs, 40, 10);
    drawPoint(&gs, 60, 20);

    REQUIRE(gs.readPSMCT32(0, 1, 1) == 0xff000014);
    REQUIRE(gs.readPSMZ32(0, 1, 1) == 60);
  }

  SECTION("GREATER rejects equal and farther fragments")
  {
    GS gs;
    configure(&gs);
    gs.writePSMCT32(0, 1, 1, UINT32_C(0xff000063));
    gs.writePSMZ32(0, 1, 1, 40);
    gs.writeRegister(GSRegisterAddress::TEST_1, testValue(3));
    drawPoint(&gs, 40, 10);
    drawPoint(&gs, 20, 20);

    REQUIRE(gs.readPSMCT32(0, 1, 1) == 0xff000063);
    REQUIRE(gs.readPSMZ32(0, 1, 1) == 40);
  }

  SECTION("ZMSK prevents depth writes after a passing test")
  {
    GS gs;
    configure(&gs, true);
    gs.writePSMZ32(0, 1, 1, 30);
    gs.writeRegister(GSRegisterAddress::TEST_1, testValue(1));
    drawPoint(&gs, 80, 10);

    REQUIRE(gs.readPSMCT32(0, 1, 1) == 0xff00000a);
    REQUIRE(gs.readPSMZ32(0, 1, 1) == 30);
  }
}

TEST_CASE("GS Triangle Depth Interpolation Tests")
{
  GS gs;
  configure(&gs);
  gs.writeRegister(GSRegisterAddress::TEST_1, testValue(1));
  gs.writeRegister(
    GSRegisterAddress::PRIM,
    static_cast<std::uint64_t>(GSPrimitiveType::Triangle));
  submitTriangleVertex(
    &gs, FIXED_POINT_ONE, FIXED_POINT_ONE, 0);
  submitTriangleVertex(
    &gs, 5 * FIXED_POINT_ONE, FIXED_POINT_ONE, 100);
  submitTriangleVertex(
    &gs, FIXED_POINT_ONE, 5 * FIXED_POINT_ONE, 0);

  REQUIRE(gs.readPSMZ32(0, 3, 1) == 50);
}

TEST_CASE("GS Line and Sprite Depth Tests")
{
  SECTION("Lines interpolate Z along the major axis")
  {
    GS gs;
    configure(&gs);
    gs.writeRegister(GSRegisterAddress::TEST_1, testValue(1));
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Line));
    gs.writeRegister(GSRegisterAddress::RGBAQ, colorValue(80));
    gs.writeRegister(
      GSRegisterAddress::XYZ2,
      vertexValue(
        FIXED_POINT_ONE,
        FIXED_POINT_ONE,
        10));
    gs.writeRegister(GSRegisterAddress::RGBAQ, colorValue(80));
    gs.writeRegister(
      GSRegisterAddress::XYZ2,
      vertexValue(
        5 * FIXED_POINT_ONE,
        FIXED_POINT_ONE,
        50));

    REQUIRE(gs.readPSMZ32(0, 3, 1) == 30);
  }

  SECTION("Sprites use the second vertex Z for every fragment")
  {
    GS gs;
    configure(&gs);
    gs.writeRegister(GSRegisterAddress::TEST_1, testValue(1));
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Sprite));
    gs.writeRegister(GSRegisterAddress::RGBAQ, colorValue(80));
    gs.writeRegister(
      GSRegisterAddress::XYZ2,
      vertexValue(
        FIXED_POINT_ONE,
        FIXED_POINT_ONE,
        20));
    gs.writeRegister(GSRegisterAddress::RGBAQ, colorValue(80));
    gs.writeRegister(
      GSRegisterAddress::XYZ2,
      vertexValue(
        3 * FIXED_POINT_ONE,
        3 * FIXED_POINT_ONE,
        70));

    REQUIRE(gs.readPSMZ32(0, 1, 1) == 70);
    REQUIRE(gs.readPSMZ32(0, 2, 2) == 70);
  }
}
