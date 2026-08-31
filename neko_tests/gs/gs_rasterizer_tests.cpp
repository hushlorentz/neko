#include <cstdint>

#include "catch.hpp"
#include "gif.hpp"
#include "gs.hpp"
#include "vif.hpp"

namespace
{
  constexpr std::uint32_t WORDS_PER_QUADWORD = 4;
  constexpr std::uint32_t FIXED_POINT_ONE = 16;
  constexpr std::uint32_t TRIANGLE_PRIMITIVE = 3;
  constexpr std::uint64_t GOURAUD_SHADING = UINT64_C(1) << 3;

  std::uint64_t frameValue(
    std::uint16_t basePointer,
    std::uint8_t width)
  {
    return
      basePointer |
      (static_cast<std::uint64_t>(width) << 16) |
      (static_cast<std::uint64_t>(
        GSPixelStorageMode::PSMCT32) << 24);
  }

  std::uint64_t scissorValue(
    std::uint16_t x0,
    std::uint16_t x1,
    std::uint16_t y0,
    std::uint16_t y1)
  {
    return
      x0 |
      (static_cast<std::uint64_t>(x1) << 16) |
      (static_cast<std::uint64_t>(y0) << 32) |
      (static_cast<std::uint64_t>(y1) << 48);
  }

  std::uint64_t offsetValue(
    std::uint16_t x,
    std::uint16_t y)
  {
    return
      x |
      (static_cast<std::uint64_t>(y) << 32);
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

  std::uint32_t packedColor(
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha)
  {
    return static_cast<std::uint32_t>(
      colorValue(red, green, blue, alpha));
  }

  std::uint64_t vertexValue(
    std::uint16_t x,
    std::uint16_t y,
    std::uint32_t z = 0)
  {
    return
      x |
      (static_cast<std::uint64_t>(y) << 16) |
      (static_cast<std::uint64_t>(z) << 32);
  }

  void configureContext(
    GS *gs,
    std::size_t context,
    std::uint16_t x0 = 0,
    std::uint16_t x1 = 63,
    std::uint16_t y0 = 0,
    std::uint16_t y1 = 31,
    std::uint16_t offsetX = 0,
    std::uint16_t offsetY = 0)
  {
    const std::uint8_t frameAddress =
      context == 0
        ? GSRegisterAddress::FRAME_1
        : GSRegisterAddress::FRAME_2;
    const std::uint8_t scissorAddress =
      context == 0
        ? GSRegisterAddress::SCISSOR_1
        : GSRegisterAddress::SCISSOR_2;
    const std::uint8_t offsetAddress =
      context == 0
        ? GSRegisterAddress::XYOFFSET_1
        : GSRegisterAddress::XYOFFSET_2;
    gs->writeRegister(frameAddress, frameValue(context, 1));
    gs->writeRegister(
      scissorAddress,
      scissorValue(x0, x1, y0, y1));
    gs->writeRegister(
      offsetAddress,
      offsetValue(offsetX, offsetY));
  }

  void submitVertex(
    GS *gs,
    std::uint16_t x,
    std::uint16_t y,
    std::uint64_t color,
    bool drawingKick = true)
  {
    gs->writeRegister(GSRegisterAddress::RGBAQ, color);
    gs->writeRegister(
      drawingKick
        ? GSRegisterAddress::XYZ2
        : GSRegisterAddress::XYZ3,
      vertexValue(x, y));
  }

  GIFQuadword gifTag(
    std::uint16_t loopCount,
    bool endOfPacket,
    std::uint8_t registerCount,
    std::uint64_t registers,
    bool primitiveEnabled = false,
    std::uint16_t primitive = 0)
  {
    const std::uint64_t low =
      loopCount |
      (static_cast<std::uint64_t>(endOfPacket) << 15) |
      (static_cast<std::uint64_t>(primitiveEnabled) << 46) |
      (static_cast<std::uint64_t>(primitive) << 47) |
      (static_cast<std::uint64_t>(GIFDataFormat::Packed) << 58) |
      (static_cast<std::uint64_t>(registerCount) << 60);
    return GIFQuadword{{
      static_cast<std::uint32_t>(low),
      static_cast<std::uint32_t>(low >> 32),
      static_cast<std::uint32_t>(registers),
      static_cast<std::uint32_t>(registers >> 32)
    }};
  }

  GIFQuadword adWrite(
    std::uint8_t address,
    std::uint64_t data)
  {
    return GIFQuadword{{
      static_cast<std::uint32_t>(data),
      static_cast<std::uint32_t>(data >> 32),
      address,
      0
    }};
  }

  GIFQuadword packedRGBA(
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha)
  {
    return GIFQuadword{{red, green, blue, alpha}};
  }

  GIFQuadword packedXYZ(
    std::uint16_t x,
    std::uint16_t y)
  {
    return GIFQuadword{{x, y, 0, 0}};
  }

  std::uint32_t vifCode(
    std::uint8_t command,
    std::uint16_t immediate = 0)
  {
    return
      (static_cast<std::uint32_t>(command) << 24) |
      immediate;
  }

  void ingestQuadword(
    VIF *vif,
    const GIFQuadword &quadword)
  {
    for (const std::uint32_t word : quadword)
    {
      vif->ingestWord(word);
    }
  }
}

TEST_CASE("GS Flat Triangle Rasterizer Tests")
{
  SECTION("The third XYZ2 kick draws a flat triangle")
  {
    GS gs;
    configureContext(&gs, 0);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      TRIANGLE_PRIMITIVE);
    submitVertex(
      &gs,
      1 * FIXED_POINT_ONE,
      1 * FIXED_POINT_ONE,
      colorValue(1, 2, 3, 4));
    submitVertex(
      &gs,
      4 * FIXED_POINT_ONE,
      1 * FIXED_POINT_ONE,
      colorValue(5, 6, 7, 8));

    REQUIRE(gs.queuedVertexCount() == 2);
    REQUIRE(gs.triangleCount() == 0);
    REQUIRE(gs.pixelWriteCount() == 0);

    const std::uint64_t finalColor =
      colorValue(0x10, 0x20, 0x40, 0x80);
    submitVertex(
      &gs,
      1 * FIXED_POINT_ONE,
      4 * FIXED_POINT_ONE,
      finalColor);

    const std::uint32_t expected =
      packedColor(0x10, 0x20, 0x40, 0x80);
    REQUIRE(gs.queuedVertexCount() == 0);
    REQUIRE(gs.triangleCount() == 1);
    REQUIRE(gs.pixelWriteCount() == 6);
    REQUIRE(gs.readPSMCT32(0, 1, 1) == expected);
    REQUIRE(gs.readPSMCT32(0, 2, 1) == expected);
    REQUIRE(gs.readPSMCT32(0, 3, 1) == expected);
    REQUIRE(gs.readPSMCT32(0, 1, 2) == expected);
    REQUIRE(gs.readPSMCT32(0, 2, 2) == expected);
    REQUIRE(gs.readPSMCT32(0, 1, 3) == expected);
    REQUIRE(gs.readPSMCT32(0, 3, 2) == 0);
    REQUIRE(gs.readPSMCT32(0, 4, 1) == 0);
    REQUIRE(gs.readPSMCT32(0, 1, 4) == 0);
  }

  SECTION("Clockwise and counterclockwise vertices cover the same pixels")
  {
    GS clockwise;
    GS counterclockwise;
    configureContext(&clockwise, 0);
    configureContext(&counterclockwise, 0);
    clockwise.writeRegister(
      GSRegisterAddress::PRIM,
      TRIANGLE_PRIMITIVE);
    counterclockwise.writeRegister(
      GSRegisterAddress::PRIM,
      TRIANGLE_PRIMITIVE);
    const std::uint64_t color = colorValue(1, 2, 3, 4);

    submitVertex(
      &clockwise,
      1 * FIXED_POINT_ONE,
      1 * FIXED_POINT_ONE,
      color);
    submitVertex(
      &clockwise,
      4 * FIXED_POINT_ONE,
      1 * FIXED_POINT_ONE,
      color);
    submitVertex(
      &clockwise,
      1 * FIXED_POINT_ONE,
      4 * FIXED_POINT_ONE,
      color);

    submitVertex(
      &counterclockwise,
      1 * FIXED_POINT_ONE,
      1 * FIXED_POINT_ONE,
      color);
    submitVertex(
      &counterclockwise,
      1 * FIXED_POINT_ONE,
      4 * FIXED_POINT_ONE,
      color);
    submitVertex(
      &counterclockwise,
      4 * FIXED_POINT_ONE,
      1 * FIXED_POINT_ONE,
      color);

    REQUIRE(
      clockwise.framebufferHash(0, 8, 8) ==
      counterclockwise.framebufferHash(0, 8, 8));
    REQUIRE(clockwise.pixelWriteCount() == 6);
    REQUIRE(counterclockwise.pixelWriteCount() == 6);
  }

  SECTION("Gouraud shading interpolates each vertex color")
  {
    GS gs;
    configureContext(&gs, 0);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      TRIANGLE_PRIMITIVE | GOURAUD_SHADING);
    submitVertex(
      &gs,
      1 * FIXED_POINT_ONE,
      1 * FIXED_POINT_ONE,
      colorValue(255, 0, 0, 128));
    submitVertex(
      &gs,
      4 * FIXED_POINT_ONE,
      1 * FIXED_POINT_ONE,
      colorValue(0, 255, 0, 128));
    submitVertex(
      &gs,
      1 * FIXED_POINT_ONE,
      4 * FIXED_POINT_ONE,
      colorValue(0, 0, 255, 128));

    REQUIRE(gs.pixelWriteCount() == 6);
    REQUIRE(
      gs.readPSMCT32(0, 1, 1) ==
      packedColor(255, 0, 0, 128));
    REQUIRE(
      gs.readPSMCT32(0, 2, 1) ==
      packedColor(170, 85, 0, 128));
    REQUIRE(
      gs.readPSMCT32(0, 1, 2) ==
      packedColor(170, 0, 85, 128));
    REQUIRE(
      gs.readPSMCT32(0, 2, 2) ==
      packedColor(85, 85, 85, 128));
  }

  SECTION("Gouraud colors remain attached to clockwise vertices")
  {
    GS clockwise;
    GS counterclockwise;
    configureContext(&clockwise, 0);
    configureContext(&counterclockwise, 0);
    const std::uint64_t gouraudTriangle =
      TRIANGLE_PRIMITIVE | GOURAUD_SHADING;
    clockwise.writeRegister(
      GSRegisterAddress::PRIM,
      gouraudTriangle);
    counterclockwise.writeRegister(
      GSRegisterAddress::PRIM,
      gouraudTriangle);
    const std::uint64_t red = colorValue(255, 0, 0, 128);
    const std::uint64_t green = colorValue(0, 255, 0, 128);
    const std::uint64_t blue = colorValue(0, 0, 255, 128);

    submitVertex(
      &clockwise,
      1 * FIXED_POINT_ONE,
      1 * FIXED_POINT_ONE,
      red);
    submitVertex(
      &clockwise,
      4 * FIXED_POINT_ONE,
      1 * FIXED_POINT_ONE,
      green);
    submitVertex(
      &clockwise,
      1 * FIXED_POINT_ONE,
      4 * FIXED_POINT_ONE,
      blue);

    submitVertex(
      &counterclockwise,
      1 * FIXED_POINT_ONE,
      1 * FIXED_POINT_ONE,
      red);
    submitVertex(
      &counterclockwise,
      1 * FIXED_POINT_ONE,
      4 * FIXED_POINT_ONE,
      blue);
    submitVertex(
      &counterclockwise,
      4 * FIXED_POINT_ONE,
      1 * FIXED_POINT_ONE,
      green);

    REQUIRE(
      clockwise.framebufferHash(0, 8, 8) ==
      counterclockwise.framebufferHash(0, 8, 8));
  }

  SECTION("Pixel centers use integer window coordinates")
  {
    GS gs;
    configureContext(&gs, 0);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      TRIANGLE_PRIMITIVE);
    const std::uint64_t color = colorValue(1, 2, 3, 4);
    constexpr std::uint16_t HALF_PIXEL = FIXED_POINT_ONE / 2;
    submitVertex(
      &gs,
      1 * FIXED_POINT_ONE + HALF_PIXEL,
      1 * FIXED_POINT_ONE + HALF_PIXEL,
      color);
    submitVertex(
      &gs,
      4 * FIXED_POINT_ONE + HALF_PIXEL,
      1 * FIXED_POINT_ONE + HALF_PIXEL,
      color);
    submitVertex(
      &gs,
      1 * FIXED_POINT_ONE + HALF_PIXEL,
      4 * FIXED_POINT_ONE + HALF_PIXEL,
      color);

    const std::uint32_t expected = packedColor(1, 2, 3, 4);
    REQUIRE(gs.pixelWriteCount() == 3);
    REQUIRE(gs.readPSMCT32(0, 2, 2) == expected);
    REQUIRE(gs.readPSMCT32(0, 3, 2) == expected);
    REQUIRE(gs.readPSMCT32(0, 2, 3) == expected);
    REQUIRE(gs.readPSMCT32(0, 1, 1) == 0);
    REQUIRE(gs.readPSMCT32(0, 4, 2) == 0);
  }

  SECTION("XYOFFSET conversion precedes inclusive scissoring")
  {
    GS gs;
    configureContext(
      &gs,
      0,
      2,
      3,
      1,
      2,
      8 * FIXED_POINT_ONE,
      9 * FIXED_POINT_ONE);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      TRIANGLE_PRIMITIVE);
    const std::uint64_t color = colorValue(1, 2, 3, 4);
    submitVertex(
      &gs,
      9 * FIXED_POINT_ONE,
      10 * FIXED_POINT_ONE,
      color);
    submitVertex(
      &gs,
      12 * FIXED_POINT_ONE,
      10 * FIXED_POINT_ONE,
      color);
    submitVertex(
      &gs,
      9 * FIXED_POINT_ONE,
      13 * FIXED_POINT_ONE,
      color);

    const std::uint32_t expected = packedColor(1, 2, 3, 4);
    REQUIRE(gs.pixelWriteCount() == 3);
    REQUIRE(gs.readPSMCT32(0, 2, 1) == expected);
    REQUIRE(gs.readPSMCT32(0, 3, 1) == expected);
    REQUIRE(gs.readPSMCT32(0, 2, 2) == expected);
    REQUIRE(gs.readPSMCT32(0, 1, 1) == 0);
    REQUIRE(gs.readPSMCT32(0, 1, 2) == 0);
  }

  SECTION("Scissoring safely clips negative window coordinates")
  {
    GS gs;
    configureContext(
      &gs,
      0,
      0,
      7,
      0,
      7,
      2 * FIXED_POINT_ONE,
      2 * FIXED_POINT_ONE);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      TRIANGLE_PRIMITIVE);
    const std::uint64_t color = colorValue(1, 2, 3, 4);
    submitVertex(&gs, 1 * FIXED_POINT_ONE, 1 * FIXED_POINT_ONE, color);
    submitVertex(&gs, 4 * FIXED_POINT_ONE, 1 * FIXED_POINT_ONE, color);
    submitVertex(&gs, 1 * FIXED_POINT_ONE, 4 * FIXED_POINT_ONE, color);

    REQUIRE(gs.pixelWriteCount() == 1);
    REQUIRE(
      gs.readPSMCT32(0, 0, 0) ==
      packedColor(1, 2, 3, 4));
    REQUIRE(gs.readPSMCT32(0, 1, 0) == 0);
    REQUIRE(gs.readPSMCT32(0, 0, 1) == 0);
  }

  SECTION("Shared top-left edges have neither gaps nor double writes")
  {
    GS gs;
    configureContext(&gs, 0);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      TRIANGLE_PRIMITIVE);
    const std::uint64_t firstColor =
      colorValue(0x10, 0, 0, 0xff);
    const std::uint64_t secondColor =
      colorValue(0x20, 0, 0, 0xff);

    submitVertex(
      &gs,
      1 * FIXED_POINT_ONE,
      1 * FIXED_POINT_ONE,
      firstColor);
    submitVertex(
      &gs,
      4 * FIXED_POINT_ONE,
      1 * FIXED_POINT_ONE,
      firstColor);
    submitVertex(
      &gs,
      1 * FIXED_POINT_ONE,
      4 * FIXED_POINT_ONE,
      firstColor);
    submitVertex(
      &gs,
      4 * FIXED_POINT_ONE,
      1 * FIXED_POINT_ONE,
      secondColor);
    submitVertex(
      &gs,
      4 * FIXED_POINT_ONE,
      4 * FIXED_POINT_ONE,
      secondColor);
    submitVertex(
      &gs,
      1 * FIXED_POINT_ONE,
      4 * FIXED_POINT_ONE,
      secondColor);

    REQUIRE(gs.triangleCount() == 2);
    REQUIRE(gs.pixelWriteCount() == 9);
    REQUIRE(
      gs.readPSMCT32(0, 2, 2) ==
      packedColor(0x10, 0, 0, 0xff));
    REQUIRE(
      gs.readPSMCT32(0, 3, 2) ==
      packedColor(0x20, 0, 0, 0xff));
    for (std::uint16_t y = 1; y < 4; ++y)
    {
      for (std::uint16_t x = 1; x < 4; ++x)
      {
        REQUIRE(gs.readPSMCT32(0, x, y) != 0);
      }
    }
  }

  SECTION("XYZ3 advances the queue without drawing")
  {
    GS gs;
    configureContext(&gs, 0);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      TRIANGLE_PRIMITIVE);
    const std::uint64_t color = colorValue(1, 2, 3, 4);
    submitVertex(&gs, 16, 16, color);
    submitVertex(&gs, 64, 16, color);
    submitVertex(&gs, 16, 64, color, false);

    REQUIRE(gs.queuedVertexCount() == 0);
    REQUIRE(gs.triangleCount() == 0);
    REQUIRE(gs.pixelWriteCount() == 0);
  }

  SECTION("Degenerate triangles produce no pixels")
  {
    GS gs;
    configureContext(&gs, 0);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      TRIANGLE_PRIMITIVE);
    const std::uint64_t color = colorValue(1, 2, 3, 4);
    submitVertex(&gs, 16, 16, color);
    submitVertex(&gs, 32, 32, color);
    submitVertex(&gs, 48, 48, color);

    REQUIRE(gs.triangleCount() == 0);
    REQUIRE(gs.pixelWriteCount() == 0);
  }

  SECTION("The PRIM context selects drawing environment and framebuffer")
  {
    GS gs;
    configureContext(&gs, 0);
    configureContext(&gs, 1);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      TRIANGLE_PRIMITIVE | (UINT64_C(1) << 9));
    const std::uint64_t color = colorValue(1, 2, 3, 4);
    submitVertex(&gs, 16, 16, color);
    submitVertex(&gs, 64, 16, color);
    submitVertex(&gs, 16, 64, color);

    REQUIRE(gs.readPSMCT32(0, 1, 1) == 0);
    REQUIRE(
      gs.readPSMCT32(1, 1, 1) ==
      packedColor(1, 2, 3, 4));
  }

  SECTION("Unsupported triangle features fail explicitly")
  {
    GS gs;
    configureContext(&gs, 0);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      TRIANGLE_PRIMITIVE | (UINT64_C(1) << 4));
    const std::uint64_t color = colorValue(1, 2, 3, 4);
    submitVertex(&gs, 16, 16, color);
    submitVertex(&gs, 64, 16, color);
    REQUIRE_THROWS_WITH(
      submitVertex(&gs, 16, 64, color),
      "GS triangle uses unsupported drawing attributes.");
    REQUIRE(gs.queuedVertexCount() == 0);
  }
}

TEST_CASE("GS Point Rasterizer Tests")
{
  SECTION("Each drawing kick selects the closest pixel")
  {
    GS gs;
    configureContext(&gs, 0);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Point));

    submitVertex(
      &gs,
      2 * FIXED_POINT_ONE + 7,
      3 * FIXED_POINT_ONE + 7,
      colorValue(1, 2, 3, 4));
    submitVertex(
      &gs,
      4 * FIXED_POINT_ONE + 8,
      5 * FIXED_POINT_ONE + 8,
      colorValue(5, 6, 7, 8));

    REQUIRE(gs.pointCount() == 2);
    REQUIRE(gs.pixelWriteCount() == 2);
    REQUIRE(
      gs.readPSMCT32(0, 2, 3) ==
      packedColor(1, 2, 3, 4));
    REQUIRE(
      gs.readPSMCT32(0, 5, 6) ==
      packedColor(5, 6, 7, 8));
    REQUIRE(gs.readPSMCT32(0, 4, 5) == 0);
  }

  SECTION("Offset and scissor are applied before point coverage")
  {
    GS gs;
    configureContext(
      &gs,
      0,
      1,
      2,
      1,
      2,
      4 * FIXED_POINT_ONE,
      5 * FIXED_POINT_ONE);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Point));

    submitVertex(
      &gs,
      5 * FIXED_POINT_ONE,
      6 * FIXED_POINT_ONE,
      colorValue(1, 2, 3, 4));
    submitVertex(
      &gs,
      8 * FIXED_POINT_ONE,
      9 * FIXED_POINT_ONE,
      colorValue(5, 6, 7, 8));

    REQUIRE(gs.pointCount() == 2);
    REQUIRE(gs.pixelWriteCount() == 1);
    REQUIRE(
      gs.readPSMCT32(0, 1, 1) ==
      packedColor(1, 2, 3, 4));
  }

  SECTION("Point shading and antialias attributes are fixed")
  {
    GS gs;
    configureContext(&gs, 0);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Point) |
      GOURAUD_SHADING |
      (UINT64_C(1) << 7));

    submitVertex(
      &gs,
      FIXED_POINT_ONE,
      FIXED_POINT_ONE,
      colorValue(9, 8, 7, 6));

    REQUIRE(gs.pointCount() == 1);
    REQUIRE(
      gs.readPSMCT32(0, 1, 1) ==
      packedColor(9, 8, 7, 6));
  }

  SECTION("XYZ3 advances a point without drawing")
  {
    GS gs;
    configureContext(&gs, 0);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Point));
    submitVertex(
      &gs,
      FIXED_POINT_ONE,
      FIXED_POINT_ONE,
      colorValue(1, 2, 3, 4),
      false);

    REQUIRE(gs.pointCount() == 0);
    REQUIRE(gs.pixelWriteCount() == 0);
    REQUIRE(gs.queuedVertexCount() == 0);
  }
}

TEST_CASE("GS Sprite Rasterizer Tests")
{
  SECTION("Two diagonal vertices draw a top-left rectangle")
  {
    GS gs;
    configureContext(&gs, 0);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Sprite));
    submitVertex(
      &gs,
      1 * FIXED_POINT_ONE,
      2 * FIXED_POINT_ONE,
      colorValue(1, 2, 3, 4));

    REQUIRE(gs.queuedVertexCount() == 1);
    REQUIRE(gs.spriteCount() == 0);

    submitVertex(
      &gs,
      4 * FIXED_POINT_ONE,
      5 * FIXED_POINT_ONE,
      colorValue(0x10, 0x20, 0x40, 0x80));

    const std::uint32_t expected =
      packedColor(0x10, 0x20, 0x40, 0x80);
    REQUIRE(gs.queuedVertexCount() == 0);
    REQUIRE(gs.spriteCount() == 1);
    REQUIRE(gs.pixelWriteCount() == 9);
    for (std::uint16_t y = 2; y < 5; ++y)
    {
      for (std::uint16_t x = 1; x < 4; ++x)
      {
        REQUIRE(gs.readPSMCT32(0, x, y) == expected);
      }
    }
    REQUIRE(gs.readPSMCT32(0, 4, 4) == 0);
    REQUIRE(gs.readPSMCT32(0, 3, 5) == 0);
  }

  SECTION("Reversed diagonal vertices cover the same rectangle")
  {
    GS forward;
    GS reverse;
    configureContext(&forward, 0);
    configureContext(&reverse, 0);
    forward.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Sprite));
    reverse.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Sprite));
    const std::uint64_t color = colorValue(1, 2, 3, 4);

    submitVertex(&forward, 16, 32, color);
    submitVertex(&forward, 64, 80, color);
    submitVertex(&reverse, 64, 80, color);
    submitVertex(&reverse, 16, 32, color);

    REQUIRE(
      forward.framebufferHash(0, 8, 8) ==
      reverse.framebufferHash(0, 8, 8));
    REQUIRE(forward.pixelWriteCount() == 9);
    REQUIRE(reverse.pixelWriteCount() == 9);
  }

  SECTION("Sprite coverage applies offset, scissor, and second-vertex color")
  {
    GS gs;
    configureContext(
      &gs,
      0,
      2,
      3,
      2,
      3,
      4 * FIXED_POINT_ONE,
      5 * FIXED_POINT_ONE);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Sprite) |
      GOURAUD_SHADING |
      (UINT64_C(1) << 7));
    submitVertex(
      &gs,
      5 * FIXED_POINT_ONE,
      6 * FIXED_POINT_ONE,
      colorValue(1, 2, 3, 4));
    submitVertex(
      &gs,
      8 * FIXED_POINT_ONE,
      9 * FIXED_POINT_ONE,
      colorValue(5, 6, 7, 8));

    REQUIRE(gs.spriteCount() == 1);
    REQUIRE(gs.pixelWriteCount() == 4);
    for (std::uint16_t y = 2; y <= 3; ++y)
    {
      for (std::uint16_t x = 2; x <= 3; ++x)
      {
        REQUIRE(
          gs.readPSMCT32(0, x, y) ==
          packedColor(5, 6, 7, 8));
      }
    }
  }

  SECTION("XYZ3 completes the pair without drawing")
  {
    GS gs;
    configureContext(&gs, 0);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Sprite));
    const std::uint64_t color = colorValue(1, 2, 3, 4);
    submitVertex(&gs, 16, 16, color);
    submitVertex(&gs, 64, 64, color, false);

    REQUIRE(gs.queuedVertexCount() == 0);
    REQUIRE(gs.spriteCount() == 0);
    REQUIRE(gs.pixelWriteCount() == 0);
  }
}

TEST_CASE("GS Line Rasterizer Tests")
{
  SECTION("Horizontal lines include the start and exclude the endpoint")
  {
    GS forward;
    GS reverse;
    configureContext(&forward, 0);
    configureContext(&reverse, 0);
    forward.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Line));
    reverse.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Line));
    const std::uint64_t color = colorValue(1, 2, 3, 4);

    submitVertex(&forward, 1 * FIXED_POINT_ONE, 2 * FIXED_POINT_ONE, color);
    submitVertex(&forward, 5 * FIXED_POINT_ONE, 2 * FIXED_POINT_ONE, color);
    submitVertex(&reverse, 5 * FIXED_POINT_ONE, 3 * FIXED_POINT_ONE, color);
    submitVertex(&reverse, 1 * FIXED_POINT_ONE, 3 * FIXED_POINT_ONE, color);

    REQUIRE(forward.lineCount() == 1);
    REQUIRE(reverse.lineCount() == 1);
    REQUIRE(forward.pixelWriteCount() == 4);
    REQUIRE(reverse.pixelWriteCount() == 4);
    for (std::uint16_t x = 1; x < 5; ++x)
    {
      REQUIRE(forward.readPSMCT32(0, x, 2) != 0);
    }
    REQUIRE(forward.readPSMCT32(0, 5, 2) == 0);
    REQUIRE(reverse.readPSMCT32(0, 5, 3) != 0);
    REQUIRE(reverse.readPSMCT32(0, 1, 3) == 0);
  }

  SECTION("Lines follow the closest diamond across the driving axis")
  {
    GS gs;
    configureContext(&gs, 0);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Line));
    submitVertex(
      &gs,
      1 * FIXED_POINT_ONE,
      1 * FIXED_POINT_ONE,
      colorValue(9, 8, 7, 6));
    submitVertex(
      &gs,
      5 * FIXED_POINT_ONE,
      3 * FIXED_POINT_ONE,
      colorValue(9, 8, 7, 6));

    REQUIRE(gs.pixelWriteCount() == 4);
    REQUIRE(gs.readPSMCT32(0, 1, 1) != 0);
    REQUIRE(gs.readPSMCT32(0, 2, 2) != 0);
    REQUIRE(gs.readPSMCT32(0, 3, 2) != 0);
    REQUIRE(gs.readPSMCT32(0, 4, 3) != 0);
    REQUIRE(gs.readPSMCT32(0, 5, 3) == 0);
  }

  SECTION("Diamond exit ties select the documented starting pixel")
  {
    GS exiting;
    GS entering;
    configureContext(&exiting, 0);
    configureContext(&entering, 0);
    exiting.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Line));
    entering.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Line));
    const std::uint64_t color = colorValue(1, 2, 3, 4);

    submitVertex(&exiting, 1 * FIXED_POINT_ONE + 4,
                 1 * FIXED_POINT_ONE + 4, color);
    submitVertex(&exiting, 4 * FIXED_POINT_ONE,
                 1 * FIXED_POINT_ONE + 4, color);
    submitVertex(&entering, 1 * FIXED_POINT_ONE + 4,
                 2 * FIXED_POINT_ONE - 4, color);
    submitVertex(&entering, 4 * FIXED_POINT_ONE,
                 2 * FIXED_POINT_ONE - 4, color);

    REQUIRE(exiting.pixelWriteCount() == 2);
    REQUIRE(exiting.readPSMCT32(0, 1, 1) == 0);
    REQUIRE(exiting.readPSMCT32(0, 2, 1) != 0);
    REQUIRE(entering.pixelWriteCount() == 3);
    REQUIRE(entering.readPSMCT32(0, 1, 2) != 0);
  }

  SECTION("Flat and Gouraud colors use drawing-kick ordering")
  {
    GS flat;
    GS gouraud;
    configureContext(&flat, 0);
    configureContext(&gouraud, 0);
    flat.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Line));
    gouraud.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Line) |
      GOURAUD_SHADING);

    submitVertex(
      &flat, 1 * FIXED_POINT_ONE, 1 * FIXED_POINT_ONE,
      colorValue(10, 20, 30, 40));
    submitVertex(
      &flat, 5 * FIXED_POINT_ONE, 1 * FIXED_POINT_ONE,
      colorValue(110, 120, 130, 140));
    submitVertex(
      &gouraud, 1 * FIXED_POINT_ONE, 2 * FIXED_POINT_ONE,
      colorValue(0, 20, 40, 60));
    submitVertex(
      &gouraud, 5 * FIXED_POINT_ONE, 2 * FIXED_POINT_ONE,
      colorValue(100, 120, 140, 160));

    REQUIRE(
      flat.readPSMCT32(0, 1, 1) ==
      packedColor(110, 120, 130, 140));
    REQUIRE(
      gouraud.readPSMCT32(0, 1, 2) ==
      packedColor(0, 20, 40, 60));
    REQUIRE(
      gouraud.readPSMCT32(0, 3, 2) ==
      packedColor(50, 70, 90, 110));
  }

  SECTION("Gouraud values are prestepped to the first covered pixel")
  {
    GS gs;
    configureContext(&gs, 0);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Line) |
      GOURAUD_SHADING);
    submitVertex(
      &gs,
      1 * FIXED_POINT_ONE + 4,
      1 * FIXED_POINT_ONE,
      colorValue(20, 40, 60, 80));
    submitVertex(
      &gs,
      5 * FIXED_POINT_ONE + 4,
      1 * FIXED_POINT_ONE,
      colorValue(100, 120, 140, 160));

    REQUIRE(
      gs.readPSMCT32(0, 1, 1) ==
      packedColor(15, 35, 55, 75));
  }

  SECTION("Offset and scissor clip generated line pixels")
  {
    GS gs;
    configureContext(
      &gs,
      0,
      2,
      3,
      1,
      1,
      4 * FIXED_POINT_ONE,
      5 * FIXED_POINT_ONE);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Line));
    submitVertex(
      &gs, 4 * FIXED_POINT_ONE, 6 * FIXED_POINT_ONE,
      colorValue(1, 2, 3, 4));
    submitVertex(
      &gs, 9 * FIXED_POINT_ONE, 6 * FIXED_POINT_ONE,
      colorValue(5, 6, 7, 8));

    REQUIRE(gs.lineCount() == 1);
    REQUIRE(gs.pixelWriteCount() == 2);
    REQUIRE(gs.readPSMCT32(0, 2, 1) != 0);
    REQUIRE(gs.readPSMCT32(0, 3, 1) != 0);
  }

  SECTION("XYZ3 completes an independent line without drawing")
  {
    GS gs;
    configureContext(&gs, 0);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Line));
    const std::uint64_t color = colorValue(1, 2, 3, 4);
    submitVertex(&gs, 16, 16, color);
    submitVertex(&gs, 64, 64, color, false);

    REQUIRE(gs.queuedVertexCount() == 0);
    REQUIRE(gs.lineCount() == 0);
    REQUIRE(gs.pixelWriteCount() == 0);
  }

  SECTION("Unsupported line antialiasing is rejected explicitly")
  {
    GS gs;
    configureContext(&gs, 0);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::Line) |
      (UINT64_C(1) << 7));
    submitVertex(
      &gs, 16, 16, colorValue(1, 2, 3, 4));

    REQUIRE_THROWS(
      submitVertex(
        &gs, 64, 64, colorValue(5, 6, 7, 8)));
    REQUIRE(gs.queuedVertexCount() == 0);
  }
}

TEST_CASE("GS Line Strip Rasterizer Tests")
{
  SECTION("Each new vertex reuses the previous endpoint")
  {
    GS gs;
    configureContext(&gs, 0);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::LineStrip));
    const std::uint64_t color = colorValue(1, 2, 3, 4);

    submitVertex(&gs, 1 * FIXED_POINT_ONE, 1 * FIXED_POINT_ONE, color);
    submitVertex(&gs, 5 * FIXED_POINT_ONE, 1 * FIXED_POINT_ONE, color);
    submitVertex(&gs, 5 * FIXED_POINT_ONE, 4 * FIXED_POINT_ONE, color);

    REQUIRE(gs.queuedVertexCount() == 1);
    REQUIRE(gs.lineCount() == 2);
    REQUIRE(gs.pixelWriteCount() == 7);
    for (std::uint16_t x = 1; x < 5; ++x)
    {
      REQUIRE(gs.readPSMCT32(0, x, 1) != 0);
    }
    for (std::uint16_t y = 1; y < 4; ++y)
    {
      REQUIRE(gs.readPSMCT32(0, 5, y) != 0);
    }
  }

  SECTION("XYZ3 advances the strip and its endpoint is reused")
  {
    GS gs;
    configureContext(&gs, 0);
    gs.writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(GSPrimitiveType::LineStrip));
    const std::uint64_t color = colorValue(1, 2, 3, 4);

    submitVertex(&gs, 1 * FIXED_POINT_ONE, 1 * FIXED_POINT_ONE, color);
    submitVertex(
      &gs, 4 * FIXED_POINT_ONE, 1 * FIXED_POINT_ONE, color, false);
    submitVertex(&gs, 4 * FIXED_POINT_ONE, 4 * FIXED_POINT_ONE, color);

    REQUIRE(gs.queuedVertexCount() == 1);
    REQUIRE(gs.lineCount() == 1);
    REQUIRE(gs.pixelWriteCount() == 3);
    REQUIRE(gs.readPSMCT32(0, 4, 1) != 0);
    REQUIRE(gs.readPSMCT32(0, 4, 3) != 0);
    REQUIRE(gs.readPSMCT32(0, 1, 1) == 0);
  }
}

TEST_CASE("GIF Point and Sprite Integration Tests")
{
  SECTION("PACKED drawing kicks render both primitive types")
  {
    GS gs;
    GIFDecoder decoder;
    decoder.attachRegisterWriteHandler(&gs);
    configureContext(&gs, 0);
    const std::uint64_t descriptors =
      GIFRegisterDescriptor::RGBAQ |
      (static_cast<std::uint64_t>(
        GIFRegisterDescriptor::XYZ2) << 4);

    decoder.ingestQuadword(gifTag(
      1,
      true,
      2,
      descriptors,
      true,
      static_cast<std::uint16_t>(GSPrimitiveType::Point)));
    decoder.ingestQuadword(packedRGBA(1, 2, 3, 4));
    decoder.ingestQuadword(
      packedXYZ(2 * FIXED_POINT_ONE, 3 * FIXED_POINT_ONE));

    decoder.ingestQuadword(gifTag(
      2,
      true,
      2,
      descriptors,
      true,
      static_cast<std::uint16_t>(GSPrimitiveType::Sprite)));
    decoder.ingestQuadword(packedRGBA(5, 6, 7, 8));
    decoder.ingestQuadword(
      packedXYZ(4 * FIXED_POINT_ONE, 1 * FIXED_POINT_ONE));
    decoder.ingestQuadword(packedRGBA(9, 10, 11, 12));
    decoder.ingestQuadword(
      packedXYZ(6 * FIXED_POINT_ONE, 3 * FIXED_POINT_ONE));

    REQUIRE(gs.pointCount() == 1);
    REQUIRE(gs.spriteCount() == 1);
    REQUIRE(gs.pixelWriteCount() == 5);
    REQUIRE(
      gs.readPSMCT32(0, 2, 3) ==
      packedColor(1, 2, 3, 4));
    REQUIRE(
      gs.readPSMCT32(0, 4, 1) ==
      packedColor(9, 10, 11, 12));
    REQUIRE(
      gs.readPSMCT32(0, 5, 2) ==
      packedColor(9, 10, 11, 12));
  }
}

TEST_CASE("GIF Line Integration Tests")
{
  SECTION("PACKED drawing kicks render lines and line strips")
  {
    GS gs;
    GIFDecoder decoder;
    decoder.attachRegisterWriteHandler(&gs);
    configureContext(&gs, 0);
    const std::uint64_t descriptors =
      GIFRegisterDescriptor::RGBAQ |
      (static_cast<std::uint64_t>(
        GIFRegisterDescriptor::XYZ2) << 4);

    decoder.ingestQuadword(gifTag(
      2,
      true,
      2,
      descriptors,
      true,
      static_cast<std::uint16_t>(GSPrimitiveType::Line)));
    decoder.ingestQuadword(packedRGBA(1, 2, 3, 4));
    decoder.ingestQuadword(
      packedXYZ(1 * FIXED_POINT_ONE, 1 * FIXED_POINT_ONE));
    decoder.ingestQuadword(packedRGBA(5, 6, 7, 8));
    decoder.ingestQuadword(
      packedXYZ(4 * FIXED_POINT_ONE, 1 * FIXED_POINT_ONE));

    decoder.ingestQuadword(gifTag(
      3,
      true,
      2,
      descriptors,
      true,
      static_cast<std::uint16_t>(GSPrimitiveType::LineStrip)));
    decoder.ingestQuadword(packedRGBA(9, 10, 11, 12));
    decoder.ingestQuadword(
      packedXYZ(5 * FIXED_POINT_ONE, 1 * FIXED_POINT_ONE));
    decoder.ingestQuadword(packedRGBA(13, 14, 15, 16));
    decoder.ingestQuadword(
      packedXYZ(5 * FIXED_POINT_ONE, 4 * FIXED_POINT_ONE));
    decoder.ingestQuadword(packedRGBA(17, 18, 19, 20));
    decoder.ingestQuadword(
      packedXYZ(7 * FIXED_POINT_ONE, 4 * FIXED_POINT_ONE));

    REQUIRE(gs.lineCount() == 3);
    REQUIRE(gs.pixelWriteCount() == 8);
    REQUIRE(
      gs.readPSMCT32(0, 1, 1) ==
      packedColor(5, 6, 7, 8));
    REQUIRE(
      gs.readPSMCT32(0, 5, 1) ==
      packedColor(13, 14, 15, 16));
    REQUIRE(
      gs.readPSMCT32(0, 6, 4) ==
      packedColor(17, 18, 19, 20));
  }
}

TEST_CASE("PATH2 Flat Triangle Integration Tests")
{
  SECTION("A VIF DIRECT packet produces a deterministic framebuffer")
  {
    GS gs;
    GIFDecoder decoder;
    decoder.attachRegisterWriteHandler(&gs);
    VIF vif(VIFType::VIF1);
    vif.attachGIFDecoder(&decoder);
    for (std::uint32_t index = 0;
         index < WORDS_PER_QUADWORD - 1;
         ++index)
    {
      vif.ingestWord(vifCode(VIFCommandEncoding::NOP));
    }

    constexpr std::uint16_t DIRECT_QUADWORDS = 11;
    vif.ingestWord(vifCode(
      VIFCommandEncoding::DIRECT,
      DIRECT_QUADWORDS));
    ingestQuadword(
      &vif,
      gifTag(
        3,
        false,
        1,
        GIFRegisterDescriptor::AD));
    ingestQuadword(
      &vif,
      adWrite(
        GSRegisterAddress::FRAME_1,
        frameValue(0, 1)));
    ingestQuadword(
      &vif,
      adWrite(
        GSRegisterAddress::SCISSOR_1,
        scissorValue(0, 7, 0, 7)));
    ingestQuadword(
      &vif,
      adWrite(
        GSRegisterAddress::XYOFFSET_1,
        offsetValue(0, 0)));

    const std::uint64_t vertexDescriptors =
      GIFRegisterDescriptor::RGBAQ |
      (static_cast<std::uint64_t>(
        GIFRegisterDescriptor::XYZ2) << 4);
    ingestQuadword(
      &vif,
      gifTag(
        3,
        true,
        2,
        vertexDescriptors,
        true,
        TRIANGLE_PRIMITIVE));
    ingestQuadword(
      &vif,
      packedRGBA(0x10, 0x20, 0x40, 0x80));
    ingestQuadword(
      &vif,
      packedXYZ(1 * FIXED_POINT_ONE, 1 * FIXED_POINT_ONE));
    ingestQuadword(
      &vif,
      packedRGBA(0x10, 0x20, 0x40, 0x80));
    ingestQuadword(
      &vif,
      packedXYZ(4 * FIXED_POINT_ONE, 1 * FIXED_POINT_ONE));
    ingestQuadword(
      &vif,
      packedRGBA(0x10, 0x20, 0x40, 0x80));
    ingestQuadword(
      &vif,
      packedXYZ(1 * FIXED_POINT_ONE, 4 * FIXED_POINT_ONE));

    REQUIRE(!vif.awaitingPayload());
    REQUIRE(!decoder.packetInProgress());
    REQUIRE(gs.triangleCount() == 1);
    REQUIRE(gs.pixelWriteCount() == 6);
    REQUIRE(
      gs.framebufferHash(0, 8, 8) ==
      UINT64_C(0x108089dcd964d365));
  }
}
