#include <cstdint>
#include <cstring>

#include "catch.hpp"
#include "gs.hpp"

namespace
{
  constexpr std::uint16_t FIXED_POINT_ONE = 16;
  constexpr std::uint64_t TEXTURE_MAPPING = UINT64_C(1) << 4;
  constexpr std::uint64_t FIXED_UV = UINT64_C(1) << 8;

  std::uint32_t floatBits(float value)
  {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
  }

  std::uint64_t textureValue(
    std::uint8_t widthExponent,
    std::uint8_t heightExponent,
    bool rgba,
    std::uint8_t function)
  {
    return
      (UINT64_C(1) << 14) |
      (static_cast<std::uint64_t>(widthExponent) << 26) |
      (static_cast<std::uint64_t>(heightExponent) << 30) |
      (static_cast<std::uint64_t>(rgba) << 34) |
      (static_cast<std::uint64_t>(function) << 35);
  }

  std::uint64_t colorValue(
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha,
    float q = 1.0f)
  {
    return
      red |
      (static_cast<std::uint64_t>(green) << 8) |
      (static_cast<std::uint64_t>(blue) << 16) |
      (static_cast<std::uint64_t>(alpha) << 24) |
      (static_cast<std::uint64_t>(floatBits(q)) << 32);
  }

  std::uint64_t vertexValue(
    std::uint16_t x,
    std::uint16_t y)
  {
    return x | (static_cast<std::uint64_t>(y) << 16);
  }

  std::uint64_t uvValue(
    std::uint16_t u,
    std::uint16_t v)
  {
    return u | (static_cast<std::uint64_t>(v) << 16);
  }

  std::uint64_t stValue(float s, float t)
  {
    return
      floatBits(s) |
      (static_cast<std::uint64_t>(floatBits(t)) << 32);
  }

  void configureFrame(GS *gs)
  {
    gs->writeRegister(
      GSRegisterAddress::FRAME_1,
      UINT64_C(1) | (UINT64_C(1) << 16));
    gs->writeRegister(
      GSRegisterAddress::SCISSOR_1,
      (UINT64_C(31) << 16) |
      (UINT64_C(31) << 48));
  }

  void upload2x2(GS *gs)
  {
    gs->writeRegister(
      GSRegisterAddress::BITBLTBUF,
      UINT64_C(1) << 48);
    gs->writeRegister(
      GSRegisterAddress::TRXREG,
      UINT64_C(2) | (UINT64_C(2) << 32));
    gs->writeRegister(GSRegisterAddress::TRXDIR, 0);
    gs->writeRegister(
      GSRegisterAddress::HWREG,
      UINT64_C(0xff00ff00000000ff));
    gs->writeRegister(
      GSRegisterAddress::HWREG,
      UINT64_C(0xffffffffffff0000));
  }

  void submitFixedVertex(
    GS *gs,
    std::uint16_t x,
    std::uint16_t y,
    std::uint16_t u,
    std::uint16_t v,
    std::uint64_t color)
  {
    gs->writeRegister(GSRegisterAddress::UV, uvValue(u, v));
    gs->writeRegister(GSRegisterAddress::RGBAQ, color);
    gs->writeRegister(
      GSRegisterAddress::XYZ2,
      vertexValue(x, y));
  }

  void submitSTQVertex(
    GS *gs,
    std::uint16_t x,
    std::uint16_t y,
    float s,
    float t,
    float q,
    std::uint64_t color)
  {
    gs->writeRegister(GSRegisterAddress::ST, stValue(s, t));
    gs->writeRegister(
      GSRegisterAddress::RGBAQ,
      (color & UINT64_C(0xffffffff)) |
      (static_cast<std::uint64_t>(floatBits(q)) << 32));
    gs->writeRegister(
      GSRegisterAddress::XYZ2,
      vertexValue(x, y));
  }
}

TEST_CASE("GS Fixed UV Textured Sprite Tests")
{
  GS gs;
  configureFrame(&gs);
  upload2x2(&gs);
  gs.writeRegister(
    GSRegisterAddress::TEX0_1,
    textureValue(1, 1, true, 1));
  gs.writeRegister(
    GSRegisterAddress::PRIM,
    static_cast<std::uint64_t>(GSPrimitiveType::Sprite) |
    TEXTURE_MAPPING |
    FIXED_UV);

  submitFixedVertex(
    &gs,
    1 * FIXED_POINT_ONE,
    1 * FIXED_POINT_ONE,
    0,
    0,
    colorValue(0x80, 0x80, 0x80, 0x80));
  submitFixedVertex(
    &gs,
    5 * FIXED_POINT_ONE,
    5 * FIXED_POINT_ONE,
    2 * FIXED_POINT_ONE,
    2 * FIXED_POINT_ONE,
    colorValue(0x80, 0x80, 0x80, 0x80));

  REQUIRE(gs.spriteCount() == 1);
  REQUIRE(gs.pixelWriteCount() == 16);
  REQUIRE(gs.readPSMCT32(0, 1, 1) == 0x000000ff);
  REQUIRE(gs.readPSMCT32(0, 4, 1) == 0xff00ff00);
  REQUIRE(gs.readPSMCT32(0, 1, 4) == 0xffff0000);
  REQUIRE(gs.readPSMCT32(0, 4, 4) == 0xffffffff);
}

TEST_CASE("GS Fixed UV Textured Triangle Tests")
{
  GS gs;
  configureFrame(&gs);
  upload2x2(&gs);
  gs.writeRegister(
    GSRegisterAddress::TEX0_1,
    textureValue(1, 1, true, 1));
  gs.writeRegister(
    GSRegisterAddress::PRIM,
    static_cast<std::uint64_t>(GSPrimitiveType::Triangle) |
    TEXTURE_MAPPING |
    FIXED_UV);
  const std::uint64_t color =
    colorValue(0x80, 0x80, 0x80, 0x80);

  submitFixedVertex(
    &gs, 1 * FIXED_POINT_ONE, 1 * FIXED_POINT_ONE,
    0, 0, color);
  submitFixedVertex(
    &gs, 5 * FIXED_POINT_ONE, 1 * FIXED_POINT_ONE,
    2 * FIXED_POINT_ONE, 0, color);
  submitFixedVertex(
    &gs, 1 * FIXED_POINT_ONE, 5 * FIXED_POINT_ONE,
    0, 2 * FIXED_POINT_ONE, color);

  REQUIRE(gs.triangleCount() == 1);
  REQUIRE(gs.readPSMCT32(0, 1, 1) == 0x000000ff);
  REQUIRE(gs.readPSMCT32(0, 3, 1) == 0xff00ff00);
  REQUIRE(gs.readPSMCT32(0, 1, 3) == 0xffff0000);
}

TEST_CASE("GS Perspective STQ Textured Triangle Tests")
{
  GS gs;
  configureFrame(&gs);
  gs.writeRegister(
    GSRegisterAddress::BITBLTBUF,
    UINT64_C(1) << 48);
  gs.writeRegister(
    GSRegisterAddress::TRXREG,
    UINT64_C(4) | (UINT64_C(1) << 32));
  gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
  gs.writeRegister(
    GSRegisterAddress::HWREG,
    UINT64_C(0x2222222211111111));
  gs.writeRegister(
    GSRegisterAddress::HWREG,
    UINT64_C(0x4444444433333333));
  gs.writeRegister(
    GSRegisterAddress::TEX0_1,
    textureValue(2, 0, true, 1));
  gs.writeRegister(
    GSRegisterAddress::PRIM,
    static_cast<std::uint64_t>(GSPrimitiveType::Triangle) |
    TEXTURE_MAPPING);
  const std::uint64_t color =
    colorValue(0x80, 0x80, 0x80, 0x80);

  submitSTQVertex(
    &gs, 1 * FIXED_POINT_ONE, 1 * FIXED_POINT_ONE,
    0.0f, 0.0f, 1.0f, color);
  submitSTQVertex(
    &gs, 5 * FIXED_POINT_ONE, 1 * FIXED_POINT_ONE,
    0.5f, 0.0f, 0.5f, color);
  submitSTQVertex(
    &gs, 1 * FIXED_POINT_ONE, 5 * FIXED_POINT_ONE,
    0.0f, 0.0f, 1.0f, color);

  REQUIRE(gs.readPSMCT32(0, 3, 1) == 0x22222222);
}

TEST_CASE("GS Texture Function Tests")
{
  const std::uint32_t expected[] = {
    0x20401010,
    0x40804020,
    0x80805050,
    0x40805050
  };
  for (std::uint8_t function = 0; function < 4; ++function)
  {
    GS gs;
    configureFrame(&gs);
    gs.writeRegister(
      GSRegisterAddress::BITBLTBUF,
      UINT64_C(1) << 48);
    gs.writeRegister(
      GSRegisterAddress::TRXREG,
      UINT64_C(1) | (UINT64_C(1) << 32));
    gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
    gs.writeRegister(
      GSRegisterAddress::HWREG,
      UINT64_C(0x40804020));
    gs.writeRegister(
      GSRegisterAddress::TEX0_1,
      textureValue(0, 0, true, function));
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Point) |
      TEXTURE_MAPPING |
      FIXED_UV);
    submitFixedVertex(
      &gs,
      FIXED_POINT_ONE,
      FIXED_POINT_ONE,
      0,
      0,
      colorValue(0x40, 0x20, 0x40, 0x40));

    REQUIRE(gs.readPSMCT32(0, 1, 1) == expected[function]);
  }

  SECTION("TCC RGB preserves fragment alpha during modulation")
  {
    GS gs;
    configureFrame(&gs);
    gs.writeRegister(
      GSRegisterAddress::BITBLTBUF,
      UINT64_C(1) << 48);
    gs.writeRegister(
      GSRegisterAddress::TRXREG,
      UINT64_C(1) | (UINT64_C(1) << 32));
    gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
    gs.writeRegister(
      GSRegisterAddress::HWREG,
      UINT64_C(0x40804020));
    gs.writeRegister(
      GSRegisterAddress::TEX0_1,
      textureValue(0, 0, false, 0));
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Point) |
      TEXTURE_MAPPING |
      FIXED_UV);
    submitFixedVertex(
      &gs,
      FIXED_POINT_ONE,
      FIXED_POINT_ONE,
      0,
      0,
      colorValue(0x40, 0x20, 0x40, 0x40));

    REQUIRE(gs.readPSMCT32(0, 1, 1) == 0x40401010);
  }
}

TEST_CASE("GS Invalid STQ Texture Coordinate Tests")
{
  GS gs;
  configureFrame(&gs);
  gs.writeRegister(
    GSRegisterAddress::BITBLTBUF,
    UINT64_C(1) << 48);
  gs.writeRegister(
    GSRegisterAddress::TRXREG,
    UINT64_C(1) | (UINT64_C(1) << 32));
  gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
  gs.writeRegister(GSRegisterAddress::HWREG, UINT64_C(0xffffffff));
  gs.writeRegister(
    GSRegisterAddress::TEX0_1,
    textureValue(0, 0, true, 1));
  gs.writeRegister(
    GSRegisterAddress::PRIM,
    static_cast<std::uint64_t>(GSPrimitiveType::Point) |
    TEXTURE_MAPPING);
  gs.writeRegister(GSRegisterAddress::ST, stValue(0.0f, 0.0f));
  gs.writeRegister(
    GSRegisterAddress::RGBAQ,
    colorValue(0x80, 0x80, 0x80, 0x80, 0.0f));

  REQUIRE_THROWS_WITH(
    gs.writeRegister(
      GSRegisterAddress::XYZ2,
      vertexValue(FIXED_POINT_ONE, FIXED_POINT_ONE)),
    "GS STQ texture coordinates must be finite with non-zero Q.");
}
