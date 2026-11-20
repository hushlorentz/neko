#include <array>
#include <cstdint>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "gs.hpp"
#include "iop/iop_trace_test_utils.hpp"
#include "neko_system.hpp"

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

  void prepareDepthSprite(NekoSystem &system, std::uint8_t textureFormat)
  {
    auto &gs = system.gs();
    configure(&gs);
    gs.writeRegister(GSRegisterAddress::RGBAQ, colorValue(10));
    gs.writeRegister(
      GSRegisterAddress::TEX0_1,
      UINT64_C(128) | (UINT64_C(1) << 14) |
      (static_cast<std::uint64_t>(textureFormat) << 20) |
      (UINT64_C(3) << 26) | (UINT64_C(3) << 30) | (UINT64_C(1) << 34));
    gs.writeRegister(GSRegisterAddress::TEXA, UINT64_C(0x000000810000807f));
    gs.writeRegister(
      GSRegisterAddress::TEX1_1,
      (UINT64_C(1) << 5) | (UINT64_C(1) << 6));
    for (std::uint16_t y = 0; y < 8; ++y)
    {
      for (std::uint16_t x = 0; x < 8; ++x)
      {
        gs.writeDisplayPSMCT32(128, 1, x, y, UINT32_C(0x80808080));
      }
    }
    for (std::uint16_t y = 1; y < 3; ++y)
    {
      for (std::uint16_t x = 1; x < 3; ++x)
      {
        gs.writePSMZ32(0, x, y, UINT32_C(0xb0000020));
      }
    }
    const auto registerWrite =
      [](std::uint8_t address, std::uint64_t value)
      {
        return GIFQuadword{{
          static_cast<std::uint32_t>(value),
          static_cast<std::uint32_t>(value >> 32),
          address, 0
        }};
      };
    const std::array<GIFQuadword, 7> packet = {{
      {{0, 0, 0, UINT32_C(0x50000006)}},
      {{5 | (1 << 15), 1 << 28, 0x0e, 0}},
      registerWrite(GSRegisterAddress::ZBUF_1,
        UINT64_C(2) | (UINT64_C(1) << 24) | (UINT64_C(1) << 32)),
      registerWrite(GSRegisterAddress::TEST_1, testValue(2)),
      registerWrite(GSRegisterAddress::PRIM,
        static_cast<std::uint64_t>(GSPrimitiveType::Sprite) |
        (UINT64_C(1) << 4) | (UINT64_C(1) << 8)),
      registerWrite(GSRegisterAddress::XYZ2,
        vertexValue(FIXED_POINT_ONE, FIXED_POINT_ONE, 0)),
      registerWrite(GSRegisterAddress::XYZ2,
        vertexValue(3 * FIXED_POINT_ONE, 3 * FIXED_POINT_ONE, 32))
    }};
    for (std::size_t index = 0; index < packet.size(); ++index)
    {
      const auto &quadword = packet[index];
      REQUIRE(
        system.eeMemorySystem().writeScratchpadDMA128(
          0x1000 + static_cast<std::uint32_t>(index * 16),
          {
            quadword[0] | (static_cast<std::uint64_t>(quadword[1]) << 32),
            quadword[2] | (static_cast<std::uint64_t>(quadword[3]) << 32)
          }) == EEScratchpadAccessResult::Completed);
    }
    auto &bus = system.eeBus();
    bus.write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
    bus.write32(EEMemoryMap::D1_MADR, UINT32_C(0x80001000));
    bus.write32(EEMemoryMap::D1_QWC,
      static_cast<std::uint32_t>(packet.size()));
    bus.write32(EEMemoryMap::D1_CHCR,
      GIFDMACChannelControl::FROM_MEMORY | GIFDMACChannelControl::START);
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

TEST_CASE("GS PSMZ24 masked fragments ignore unused depth bytes",
          "[gs][psmz24]")
{
  GS gs;
  configure(&gs);
  const auto address = gs.psmz32WordAddress(0, 1, 1);
  gs.writePSMZ32(0, 1, 1, UINT32_C(0xff000020));
  gs.writeRegister(
    GSRegisterAddress::ZBUF_1,
    UINT64_C(2) | (UINT64_C(1) << 24) | (UINT64_C(1) << 32));
  gs.writeRegister(GSRegisterAddress::TEST_1, testValue(2));
  drawPoint(&gs, 32, 10);

  REQUIRE(gs.readPSMCT32(0, 1, 1) == UINT32_C(0xff00000a));
  REQUIRE(gs.localMemoryWord(address) == UINT32_C(0xff000020));
  REQUIRE(gs.pixelWriteCount() == 1);
}

TEST_CASE("GS PSMZ24 storage shares Z32 layout and preserves unused bytes",
          "[gs][psmz24]")
{
  GS gs;
  configure(&gs);
  gs.writeRegister(GSRegisterAddress::FRAME_1,
    UINT64_C(1) | (UINT64_C(2) << 16));
  const std::array<std::array<std::uint16_t, 2>, 5> pixels = {{
    {{0, 0}}, {{7, 7}}, {{8, 8}}, {{63, 31}}, {{64, 32}}
  }};
  std::array<std::size_t, pixels.size()> addresses = {};
  for (std::size_t index = 0; index < pixels.size(); ++index)
  {
    addresses[index] = gs.psmz32WordAddress(
      0, pixels[index][0], pixels[index][1]);
    gs.writePSMZ32(0, pixels[index][0], pixels[index][1],
      UINT32_C(0xa5000020) + static_cast<std::uint32_t>(index));
  }
  gs.writeRegister(GSRegisterAddress::ZBUF_1,
    UINT64_C(2) | (UINT64_C(1) << 24));
  REQUIRE(gs.psmz24WordAddress(0, 0, 0) == 5632);
  for (std::size_t index = 0; index < pixels.size(); ++index)
  {
    REQUIRE(gs.psmz24WordAddress(
      0, pixels[index][0], pixels[index][1]) == addresses[index]);
    REQUIRE(gs.readPSMZ24(0, pixels[index][0], pixels[index][1]) ==
      32 + index);
    gs.writePSMZ24(0, pixels[index][0], pixels[index][1],
      UINT32_C(0x01020304));
    REQUIRE(gs.localMemoryWord(addresses[index]) == UINT32_C(0xa5020304));
    REQUIRE(gs.readPSMZ24(0, pixels[index][0], pixels[index][1]) ==
      UINT32_C(0x00020304));
  }
  gs.writeRegister(GSRegisterAddress::ZBUF_1,
    UINT64_C(511) | (UINT64_C(1) << 24));
  REQUIRE(gs.psmz24WordAddress(0, 64, 0) == 1536);
  gs.writeRegister(GSRegisterAddress::FRAME_2,
    UINT64_C(1) | (UINT64_C(1) << 16));
  gs.writeRegister(GSRegisterAddress::ZBUF_2,
    UINT64_C(3) | (UINT64_C(1) << 24));
  gs.writePSMZ24(1, 0, 0, 17);
  REQUIRE(gs.readPSMZ24(1, 0, 0) == 17);
  REQUIRE_THROWS_WITH(gs.readPSMZ32(0, 0, 0),
    "GS depth buffer is not configured for PSMZ32.");
}

TEST_CASE("GS PSMZ24 fragment comparisons and writes honor masks",
          "[gs][psmz24]")
{
  for (const auto masked : {false, true})
  {
    for (std::uint8_t method = 0; method < 4; ++method)
    {
      for (const std::uint32_t source : {31, 32, 33})
      {
        CAPTURE(masked);
        CAPTURE(method);
        CAPTURE(source);
        GS gs;
        configure(&gs);
        const auto address = gs.psmz32WordAddress(0, 1, 1);
        gs.writePSMZ32(0, 1, 1, UINT32_C(0x7f000020));
        gs.writeRegister(GSRegisterAddress::ZBUF_1,
          UINT64_C(2) | (UINT64_C(1) << 24) |
          (static_cast<std::uint64_t>(masked) << 32));
        gs.writeRegister(GSRegisterAddress::TEST_1, testValue(method));
        drawPoint(&gs, source, 10);
        const bool passes = method == 1 ||
          (method == 2 && source >= 32) || (method == 3 && source > 32);
        REQUIRE(gs.readPSMCT32(0, 1, 1) ==
          (passes ? UINT32_C(0xff00000a) : 0));
        REQUIRE(gs.localMemoryWord(address) ==
          (UINT32_C(0x7f000000) | (passes && !masked ? source : 32)));
      }
    }
  }
}

TEST_CASE("GS PSMZ24 source saturation is gated by depth writes",
          "[gs][psmz24]")
{
  for (const auto masked : {false, true})
  {
    GS gs;
    configure(&gs);
    const auto address = gs.psmz32WordAddress(0, 1, 1);
    gs.writePSMZ32(0, 1, 1, UINT32_C(0xa5ffffff));
    gs.writeRegister(GSRegisterAddress::ZBUF_1,
      UINT64_C(2) | (UINT64_C(1) << 24) |
      (static_cast<std::uint64_t>(masked) << 32));
    gs.writeRegister(GSRegisterAddress::TEST_1, testValue(3));
    drawPoint(&gs, UINT32_C(0x01000000), 10);
    REQUIRE(gs.pixelWriteCount() == (masked ? 1 : 0));
    REQUIRE(gs.localMemoryWord(address) == UINT32_C(0xa5ffffff));
    gs.writeRegister(GSRegisterAddress::TEST_1, testValue(1));
    drawPoint(&gs, UINT32_C(0x01000001), 20);
    REQUIRE(gs.localMemoryWord(address) == UINT32_C(0xa5ffffff));
  }
}

TEST_CASE("GS PSMZ24 support retains other depth format guards",
          "[gs][psmz24]")
{
  for (const std::uint8_t format : {2, 10, 15})
  {
    GS gs;
    configure(&gs);
    gs.writeRegister(GSRegisterAddress::ZBUF_1,
      UINT64_C(2) | (static_cast<std::uint64_t>(format) << 24));
    gs.writeRegister(GSRegisterAddress::TEST_1, testValue(2));
    REQUIRE_THROWS_WITH(drawPoint(&gs, 32, 10),
      "GS depth buffer uses an unsupported pixel storage mode.");
    REQUIRE(gs.pixelWriteCount() == 0);
  }
}

TEST_CASE("GS PSMZ24 interpolated depth saturates after rasterization",
          "[gs][psmz24]")
{
  for (const auto primitive : {GSPrimitiveType::Line, GSPrimitiveType::Triangle})
  {
    GS gs;
    configure(&gs);
    const auto address = gs.psmz32WordAddress(0, 3, 1);
    gs.writePSMZ32(0, 3, 1, UINT32_C(0xa5000000));
    gs.writeRegister(GSRegisterAddress::ZBUF_1,
      UINT64_C(2) | (UINT64_C(1) << 24));
    gs.writeRegister(GSRegisterAddress::TEST_1, testValue(1));
    gs.writeRegister(GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(primitive));
    submitTriangleVertex(&gs, FIXED_POINT_ONE, FIXED_POINT_ONE, 0);
    submitTriangleVertex(&gs, 5 * FIXED_POINT_ONE, FIXED_POINT_ONE,
      UINT32_C(0x02000000));
    if (primitive == GSPrimitiveType::Triangle)
    {
      submitTriangleVertex(&gs, FIXED_POINT_ONE, 5 * FIXED_POINT_ONE, 0);
    }
    REQUIRE(gs.readPSMZ24(0, 3, 1) == UINT32_C(0x00ffffff));
    REQUIRE(gs.localMemoryWord(address) == UINT32_C(0xa5ffffff));
  }
}

TEST_CASE("GS PSMZ24 scratchpad DIRECT sprites agree cold restored and traced",
          "[gs][psmz24][psmct24][conformance]")
{
  for (const auto textureFormat : {
         GSPixelStorageMode::PSMCT32, GSPixelStorageMode::PSMCT24})
  {
    INFO("texture format " << static_cast<unsigned>(textureFormat));
    const auto expectedPixel = textureFormat == GSPixelStorageMode::PSMCT24
      ? UINT32_C(0xfd00000a) : UINT32_C(0xff00000a);
    NekoSystem origin;
    prepareDepthSprite(origin, textureFormat);
    const auto staged = origin.saveState();
    std::uint64_t checkpointCycle = 0;
    while (checkpointCycle < 32 && origin.gs().queuedVertexCount() != 1)
    {
      origin.clockMasterCycle();
      ++checkpointCycle;
    }
    REQUIRE(checkpointCycle < 32);
    REQUIRE(origin.gs().context(0).depthBuffer.pixelStorageMode ==
      GSPixelStorageMode::PSMZ24);
    const auto active = origin.saveState();
    origin.runMasterCycles(64 - checkpointCycle);
    REQUIRE(origin.gs().spriteCount() == 1);
    REQUIRE(origin.gs().pixelWriteCount() == 4);
    REQUIRE(origin.gs().queuedVertexCount() == 0);
    REQUIRE(origin.vif1DMAC().quadwordCount() == 0);
    for (std::uint16_t y = 1; y < 3; ++y)
    {
      for (std::uint16_t x = 1; x < 3; ++x)
      {
        REQUIRE(origin.gs().readPSMCT32(0, x, y) == expectedPixel);
        REQUIRE(origin.gs().localMemoryWord(
          origin.gs().psmz24WordAddress(0, x, y)) == UINT32_C(0xb0000020));
      }
    }
    const auto expected = origin.saveState();
    for (const auto restoredAt : {UINT64_C(0), checkpointCycle})
    {
      NekoSystem coldTraced;
      NekoSystem coldPlain;
      NekoSystem restoredTraced;
      NekoSystem restoredPlain;
      prepareDepthSprite(coldTraced, textureFormat);
      prepareDepthSprite(coldPlain, textureFormat);
      const auto &checkpoint = restoredAt == 0 ? staged : active;
      restoredTraced.loadState(checkpoint);
      restoredPlain.loadState(checkpoint);
      REQUIRE(restoredTraced.saveState() == checkpoint);
      coldTraced.startTrace();
      restoredTraced.startTrace();
      coldTraced.runMasterCycles(64);
      coldPlain.runMasterCycles(64);
      restoredTraced.runMasterCycles(64 - restoredAt);
      restoredPlain.runMasterCycles(64 - restoredAt);
      REQUIRE(coldTraced.saveState() == expected);
      REQUIRE(coldPlain.saveState() == expected);
      REQUIRE(restoredTraced.saveState() == expected);
      REQUIRE(restoredPlain.saveState() == expected);
      IOPTraceTest::Events suffix;
      for (const auto &event : coldTraced.trace())
      {
        if (event.masterCycle > restoredAt)
        {
          suffix.push_back(event);
        }
      }
      IOPTraceTest::requireEvents(restoredTraced.trace(), suffix);
    }
  }
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
