#include <cstdint>

#include "catch.hpp"
#include "gs.hpp"

namespace
{
  constexpr std::uint16_t FIXED_POINT_ONE = 16;
  constexpr std::uint64_t ALPHA_BLEND = UINT64_C(1) << 6;

  std::uint64_t frameValue()
  {
    return UINT64_C(1) | (UINT64_C(1) << 16);
  }

  std::uint64_t scissorValue()
  {
    return (UINT64_C(7) << 16) | (UINT64_C(7) << 48);
  }

  std::uint64_t colorValue(
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha)
  {
    return
      red |
      (static_cast<std::uint64_t>(green) << 8) |
      (static_cast<std::uint64_t>(blue) << 16) |
      (static_cast<std::uint64_t>(alpha) << 24);
  }

  std::uint64_t vertexValue(
    std::uint16_t x,
    std::uint16_t y)
  {
    return x | (static_cast<std::uint64_t>(y) << 16);
  }

  void configure(GS *gs)
  {
    gs->writeRegister(GSRegisterAddress::FRAME_1, frameValue());
    gs->writeRegister(
      GSRegisterAddress::SCISSOR_1,
      scissorValue());
  }

  void drawPoint(
    GS *gs,
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha)
  {
    gs->writeRegister(
      GSRegisterAddress::RGBAQ,
      colorValue(red, green, blue, alpha));
    gs->writeRegister(
      GSRegisterAddress::XYZ2,
      vertexValue(FIXED_POINT_ONE, FIXED_POINT_ONE));
  }
}

TEST_CASE("GS Destination Alpha Tests")
{
  GS gs;
  configure(&gs);
  gs.writePSMCT32(0, 1, 1, UINT32_C(0x80102030));
  gs.writeRegister(
    GSRegisterAddress::PRIM,
    static_cast<std::uint64_t>(GSPrimitiveType::Point));

  SECTION("DATM one accepts destination alpha with its MSB set")
  {
    gs.writeRegister(
      GSRegisterAddress::TEST_1,
      (UINT64_C(1) << 14) | (UINT64_C(1) << 15));
    drawPoint(&gs, 1, 2, 3, 4);

    REQUIRE(gs.readPSMCT32(0, 1, 1) == 0x04030201);
    REQUIRE(gs.pixelWriteCount() == 1);
  }

  SECTION("DATM zero rejects destination alpha with its MSB set")
  {
    gs.writeRegister(
      GSRegisterAddress::TEST_1,
      UINT64_C(1) << 14);
    drawPoint(&gs, 1, 2, 3, 4);

    REQUIRE(gs.readPSMCT32(0, 1, 1) == 0x80102030);
    REQUIRE(gs.pixelWriteCount() == 0);
  }
}

TEST_CASE("GS Configurable Alpha Blending Tests")
{
  SECTION("Source alpha can blend source and destination colors")
  {
    GS gs;
    configure(&gs);
    gs.writePSMCT32(0, 1, 1, UINT32_C(0x503c2814));
    gs.writeRegister(
      GSRegisterAddress::ALPHA_1,
      UINT64_C(0x44));
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Point) |
      ALPHA_BLEND);

    drawPoint(&gs, 200, 100, 50, 64);

    REQUIRE(gs.readPSMCT32(0, 1, 1) == 0x4037466e);
  }

  SECTION("The FIX field can provide the blend alpha")
  {
    GS gs;
    configure(&gs);
    gs.writeRegister(
      GSRegisterAddress::ALPHA_1,
      (UINT64_C(64) << 32) | UINT64_C(0xa8));
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Point) |
      ALPHA_BLEND);

    drawPoint(&gs, 200, 100, 50, 90);

    REQUIRE(gs.readPSMCT32(0, 1, 1) == 0x5a193264);
  }
}

TEST_CASE("GS Per-Pixel Alpha Blending and Alpha Correction Tests")
{
  GS gs;
  configure(&gs);
  gs.writePSMCT32(0, 1, 1, UINT32_C(0xff010203));
  gs.writeRegister(
    GSRegisterAddress::ALPHA_1,
    UINT64_C(0x44));
  gs.writeRegister(GSRegisterAddress::PABE, 1);
  gs.writeRegister(GSRegisterAddress::FBA_1, 1);
  gs.writeRegister(
    GSRegisterAddress::PRIM,
    static_cast<std::uint64_t>(GSPrimitiveType::Point) |
    ALPHA_BLEND);

  drawPoint(&gs, 20, 30, 40, 32);

  REQUIRE(gs.readPSMCT32(0, 1, 1) == 0xa0281e14);
}
