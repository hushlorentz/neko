#include <array>
#include <cstdint>
#include <stdexcept>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "gs.hpp"
#include "iop/iop_trace_test_utils.hpp"
#include "neko_system.hpp"

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
    std::uint16_t y,
    std::uint32_t z = 0)
  {
    return x | (static_cast<std::uint64_t>(y) << 16) |
      (static_cast<std::uint64_t>(z) << 32);
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

  std::uint64_t sourceAlphaTest(
    std::uint8_t method, std::uint8_t reference)
  {
    return UINT64_C(1) |
      (static_cast<std::uint64_t>(method) << 1) |
      (static_cast<std::uint64_t>(reference) << 4);
  }

  void configureAlphaContext(GS &gs, std::size_t context)
  {
    gs.writeRegister(context == 0 ? GSRegisterAddress::FRAME_1 :
      GSRegisterAddress::FRAME_2, frameValue());
    gs.writeRegister(context == 0 ? GSRegisterAddress::SCISSOR_1 :
      GSRegisterAddress::SCISSOR_2, scissorValue());
    gs.writeRegister(context == 0 ? GSRegisterAddress::ZBUF_1 :
      GSRegisterAddress::ZBUF_2, 2);
  }

  void drawAlphaPrimitive(
    GS &gs, GSPrimitiveType primitive, std::size_t context,
    std::uint8_t alpha)
  {
    gs.writeRegister(GSRegisterAddress::PRIM,
      static_cast<std::uint64_t>(primitive) | (context << 9));
    gs.writeRegister(GSRegisterAddress::RGBAQ, colorValue(1, 2, 3, alpha));
    gs.writeRegister(GSRegisterAddress::XYZ2,
      vertexValue(FIXED_POINT_ONE, FIXED_POINT_ONE, 20));
    if (primitive == GSPrimitiveType::Point)
    {
      return;
    }
    const bool triangle =
      primitive == GSPrimitiveType::Triangle ||
      primitive == GSPrimitiveType::TriangleStrip ||
      primitive == GSPrimitiveType::TriangleFan;
    gs.writeRegister(GSRegisterAddress::XYZ2,
      vertexValue((triangle ? 5 : 3) * FIXED_POINT_ONE,
        (triangle ? 1 : 3) * FIXED_POINT_ONE, 20));
    if (triangle)
    {
      gs.writeRegister(GSRegisterAddress::XYZ2,
        vertexValue(FIXED_POINT_ONE, 5 * FIXED_POINT_ONE, 20));
    }
  }

  void prepareAlphaSprite(NekoSystem &system)
  {
    auto &gs = system.gs();
    configureAlphaContext(gs, 0);
    gs.writeRegister(GSRegisterAddress::TEX0_1,
      (UINT64_C(1) << 14) | (UINT64_C(1) << 26) |
      (UINT64_C(1) << 34) | (UINT64_C(1) << 35));
    gs.writeRegister(GSRegisterAddress::RGBAQ, colorValue(128, 128, 128, 0));
    gs.writeRegister(GSRegisterAddress::ALPHA_1, 0x44);
    gs.writeDisplayPSMCT32(0, 1, 0, 0, UINT32_C(0x00808080));
    gs.writeDisplayPSMCT32(0, 1, 1, 0, UINT32_C(0x80808080));
    for (const std::uint16_t x : {1, 2})
    {
      gs.writePSMCT32(0, x, 1, UINT32_C(0x80000000));
      gs.writePSMZ32(0, x, 1, 10);
    }
    const auto write = [](std::uint8_t address, std::uint64_t value)
    {
      return GIFQuadword{{
        static_cast<std::uint32_t>(value),
        static_cast<std::uint32_t>(value >> 32), address, 0}};
    };
    const std::array<GIFQuadword, 8> packet = {{
      {{0, 0, 0, UINT32_C(0x50000007)}},
      {{6 | (1 << 15), 1 << 28, 0x0e, 0}},
      write(GSRegisterAddress::TEST_1, UINT64_C(0x3000d)),
      write(GSRegisterAddress::PRIM, UINT64_C(0x156)),
      write(GSRegisterAddress::UV, 0),
      write(GSRegisterAddress::XYZ2,
        vertexValue(FIXED_POINT_ONE, FIXED_POINT_ONE)),
      write(GSRegisterAddress::UV, 2 * FIXED_POINT_ONE),
      write(GSRegisterAddress::XYZ2,
        vertexValue(3 * FIXED_POINT_ONE, 2 * FIXED_POINT_ONE, 20))
    }};
    for (std::size_t index = 0; index < packet.size(); ++index)
    {
      const auto &quadword = packet[index];
      REQUIRE(system.eeMemorySystem().writeScratchpadDMA128(
        0x1000 + static_cast<std::uint32_t>(index * 16),
        {quadword[0] | (static_cast<std::uint64_t>(quadword[1]) << 32),
         quadword[2] | (static_cast<std::uint64_t>(quadword[3]) << 32)}) ==
        EEScratchpadAccessResult::Completed);
    }
    auto &bus = system.eeBus();
    bus.write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
    bus.write32(EEMemoryMap::D1_MADR, UINT32_C(0x80001000));
    bus.write32(EEMemoryMap::D1_QWC, static_cast<std::uint32_t>(packet.size()));
    bus.write32(EEMemoryMap::D1_CHCR,
      GIFDMACChannelControl::FROM_MEMORY | GIFDMACChannelControl::START);
  }
}

TEST_CASE("GS source-alpha KEEP comparisons use unsigned eight-bit values",
          "[gs][source-alpha]")
{
  struct Sample
  {
    std::uint8_t alpha;
    std::uint8_t reference;
    std::uint8_t passingMethods;
  };
  const std::array<Sample, 9> samples = {{
    {0, 0, 0x3a}, {1, 0, 0xe2}, {127, 128, 0x8e},
    {128, 128, 0x3a}, {129, 128, 0xe2}, {254, 255, 0x8e},
    {255, 255, 0x3a}, {255, 0, 0xe2}, {0, 255, 0x8e}
  }};
  for (const std::size_t context : {0, 1})
  {
    GS gs;
    configureAlphaContext(gs, context);
    for (std::uint8_t method = 0; method < 8; ++method)
    for (const auto &sample : samples)
    {
      INFO("context " << context << " method " << static_cast<unsigned>(method)
        << " alpha " << static_cast<unsigned>(sample.alpha)
        << " reference " << static_cast<unsigned>(sample.reference));
      gs.writePSMCT32(context, 1, 1, UINT32_C(0x80102030));
      gs.writePSMZ32(context, 1, 1, 10);
      gs.writeRegister(context == 0 ? GSRegisterAddress::TEST_1 :
        GSRegisterAddress::TEST_2,
        sourceAlphaTest(method, sample.reference) | UINT64_C(0x30000));
      const auto before = gs.pixelWriteCount();
      drawAlphaPrimitive(gs, GSPrimitiveType::Point, context, sample.alpha);
      const bool passes = (sample.passingMethods & (1 << method)) != 0;
      REQUIRE(gs.readPSMCT32(context, 1, 1) == (passes
        ? static_cast<std::uint32_t>(colorValue(1, 2, 3, sample.alpha))
        : UINT32_C(0x80102030)));
      REQUIRE(gs.readPSMZ32(context, 1, 1) == (passes ? 20 : 10));
      REQUIRE(gs.pixelWriteCount() == before + (passes ? 1 : 0));
    }
  }
}

TEST_CASE("GS source-alpha admission is shared and non-KEEP stays guarded",
          "[gs][source-alpha]")
{
  for (const auto primitive : {
         GSPrimitiveType::Point, GSPrimitiveType::Line,
         GSPrimitiveType::LineStrip, GSPrimitiveType::Sprite,
         GSPrimitiveType::Triangle, GSPrimitiveType::TriangleStrip,
         GSPrimitiveType::TriangleFan})
  for (const std::size_t context : {0, 1})
  for (const std::uint8_t fail : {0, 1, 2, 3})
  for (const std::uint8_t alpha : {0, 128})
  {
    INFO("primitive " << static_cast<unsigned>(primitive) << " context "
      << context << " AFAIL " << static_cast<unsigned>(fail)
      << " alpha " << static_cast<unsigned>(alpha));
    GS gs;
    configureAlphaContext(gs, context);
    gs.writePSMCT32(context, 1, 1, UINT32_C(0x80102030));
    gs.writeRegister(context == 0 ? GSRegisterAddress::TEST_1 :
      GSRegisterAddress::TEST_2,
      sourceAlphaTest(6, 0) | (static_cast<std::uint64_t>(fail) << 12));
    if (fail != 0)
    {
      REQUIRE_THROWS_AS(drawAlphaPrimitive(gs, primitive, context, alpha),
        std::runtime_error);
      REQUIRE(gs.readPSMCT32(context, 1, 1) == UINT32_C(0x80102030));
      REQUIRE(gs.pixelWriteCount() == 0);
    }
    else
    {
      drawAlphaPrimitive(gs, primitive, context, alpha);
      REQUIRE((gs.pixelWriteCount() != 0) == (alpha != 0));
      REQUIRE(gs.pointCount() + gs.lineCount() +
        gs.spriteCount() + gs.triangleCount() == 1);
      if (alpha == 0)
      {
        REQUIRE(gs.readPSMCT32(context, 1, 1) == UINT32_C(0x80102030));
      }
    }
  }
}

TEST_CASE("GS source-alpha KEEP retains unsupported drawing boundaries",
          "[gs][source-alpha]")
{
  GS gs;
  configureAlphaContext(gs, 0);
  gs.writeRegister(GSRegisterAddress::TEST_1, sourceAlphaTest(0, 0));
  gs.writeRegister(GSRegisterAddress::PRIM, 0);
  SECTION("Unsupported framebuffer format remains rejected")
  {
    gs.writeRegister(GSRegisterAddress::FRAME_1, frameValue() | (UINT64_C(1) << 24));
    REQUIRE_THROWS_WITH(drawPoint(&gs, 1, 2, 3, 0),
      "GS frame buffer is not configured for PSMCT32.");
  }
  SECTION("Unsupported depth format remains rejected")
  {
    gs.writeRegister(GSRegisterAddress::TEST_1,
      sourceAlphaTest(0, 0) | UINT64_C(0x30000));
    gs.writeRegister(GSRegisterAddress::ZBUF_1, UINT64_C(2) | (UINT64_C(2) << 24));
    REQUIRE_THROWS_WITH(drawPoint(&gs, 1, 2, 3, 0),
      "GS depth buffer uses an unsupported pixel storage mode.");
  }
  SECTION("Unsupported texture format is not skipped for NEVER")
  {
    gs.writeRegister(GSRegisterAddress::PRIM, UINT64_C(0x110));
    gs.writeRegister(GSRegisterAddress::TEX0_1,
      (UINT64_C(1) << 14) | (UINT64_C(0x14) << 20));
    REQUIRE_THROWS_WITH(drawPoint(&gs, 1, 2, 3, 0),
      "GS texture sampling requires PSMCT32 or PSMCT24.");
  }
  SECTION("Unsupported drawing attributes remain rejected")
  {
    gs.writeRegister(GSRegisterAddress::PRIM, UINT64_C(1) << 5);
    REQUIRE_THROWS_WITH(drawPoint(&gs, 1, 2, 3, 0),
      "GS point uses unsupported drawing attributes.");
  }
  REQUIRE(gs.pixelWriteCount() == 0);
}

TEST_CASE("GS source-alpha disabled bypasses comparison and AFAIL",
          "[gs][source-alpha]")
{
  for (const std::uint8_t fail : {0, 1, 2, 3})
  {
    GS gs;
    configureAlphaContext(gs, 0);
    gs.writeRegister(GSRegisterAddress::TEST_1,
      UINT64_C(0x30000) | (static_cast<std::uint64_t>(fail) << 12));
    drawAlphaPrimitive(gs, GSPrimitiveType::Point, 0, 0);
    REQUIRE(gs.readPSMCT32(0, 1, 1) == UINT32_C(0x00030201));
    REQUIRE(gs.readPSMZ32(0, 1, 1) == 20);
    REQUIRE(gs.pixelWriteCount() == 1);
  }
}

TEST_CASE("GS source-alpha KEEP composes with later tests blending and masks",
          "[gs][source-alpha]")
{
  for (const std::uint8_t alpha : {0, 128})
  for (const bool destinationPasses : {false, true})
  for (const bool depthPasses : {false, true})
  for (const bool depthMasked : {false, true})
  for (const std::uint32_t frameMask : {UINT32_C(0), UINT32_C(0xff0000ff)})
  {
    GS gs;
    configureAlphaContext(gs, 0);
    gs.writePSMCT32(0, 1, 1, UINT32_C(0x80102030));
    gs.writePSMZ32(0, 1, 1, 10);
    gs.writeRegister(GSRegisterAddress::FRAME_1,
      frameValue() | (static_cast<std::uint64_t>(frameMask) << 32));
    gs.writeRegister(GSRegisterAddress::ZBUF_1,
      UINT64_C(2) | (static_cast<std::uint64_t>(depthMasked) << 32));
    gs.writeRegister(GSRegisterAddress::TEST_1,
      sourceAlphaTest(6, 127) | (UINT64_C(1) << 14) |
      (static_cast<std::uint64_t>(destinationPasses) << 15) |
      (UINT64_C(1) << 16) | (static_cast<std::uint64_t>(depthPasses) << 17));
    gs.writeRegister(GSRegisterAddress::ALPHA_1, 0x44);
    gs.writeRegister(GSRegisterAddress::FBA_1, 1);
    gs.writeRegister(GSRegisterAddress::PABE, 1);
    gs.writeRegister(GSRegisterAddress::PRIM, ALPHA_BLEND);
    gs.writeRegister(GSRegisterAddress::RGBAQ, colorValue(128, 128, 128, alpha));
    gs.writeRegister(GSRegisterAddress::XYZ2,
      vertexValue(FIXED_POINT_ONE, FIXED_POINT_ONE, 20));
    const bool passes = alpha == 128 && destinationPasses && depthPasses;
    const std::uint32_t output = frameMask == 0
      ? UINT32_C(0x80808080) : UINT32_C(0x80808030);
    REQUIRE(gs.readPSMCT32(0, 1, 1) ==
      (passes ? output : UINT32_C(0x80102030)));
    REQUIRE(gs.readPSMZ32(0, 1, 1) ==
      (passes && !depthMasked ? 20 : 10));
    REQUIRE(gs.pixelWriteCount() == (passes ? 1 : 0));
  }
}

TEST_CASE("GS source-alpha tests texture-function output rather than vertex alpha",
          "[gs][source-alpha]")
{
  struct Sample
  {
    std::uint8_t function;
    bool rgba;
    std::uint8_t textureAlpha;
    std::uint8_t vertexAlpha;
    std::uint8_t reference;
    std::uint32_t expected;
  };
  const std::array<Sample, 4> samples = {{
    {1, true, 128, 0, 127, 0x80808080},
    {1, true, 0, 255, 0, 0x80102030},
    {0, true, 255, 64, 96, 0x7f808080},
    {0, false, 0, 255, 128, 0xff808080}
  }};
  for (const auto &sample : samples)
  {
    GS gs;
    configureAlphaContext(gs, 0);
    gs.writePSMCT32(0, 1, 1, UINT32_C(0x80102030));
    gs.writeDisplayPSMCT32(0, 1, 0, 0,
      static_cast<std::uint32_t>(colorValue(128, 128, 128, sample.textureAlpha)));
    gs.writeRegister(GSRegisterAddress::TEX0_1,
      (UINT64_C(1) << 14) | (static_cast<std::uint64_t>(sample.rgba) << 34) |
      (static_cast<std::uint64_t>(sample.function) << 35));
    gs.writeRegister(GSRegisterAddress::TEST_1,
      sourceAlphaTest(6, sample.reference));
    gs.writeRegister(GSRegisterAddress::PRIM, UINT64_C(0x116));
    gs.writeRegister(GSRegisterAddress::RGBAQ,
      colorValue(128, 128, 128, sample.vertexAlpha));
    gs.writeRegister(GSRegisterAddress::XYZ2,
      vertexValue(FIXED_POINT_ONE, FIXED_POINT_ONE));
    gs.writeRegister(GSRegisterAddress::XYZ2,
      vertexValue(2 * FIXED_POINT_ONE, 2 * FIXED_POINT_ONE));
    REQUIRE(gs.readPSMCT32(0, 1, 1) == sample.expected);
    REQUIRE(gs.pixelWriteCount() == (sample.textureAlpha == 0 && sample.rgba ? 0 : 1));
  }
}

TEST_CASE("GS source-alpha scratchpad DIRECT agrees cold restored and traced",
          "[gs][source-alpha][conformance]")
{
  NekoSystem origin;
  prepareAlphaSprite(origin);
  std::uint64_t checkpointCycle = 0;
  while (checkpointCycle < 32 && origin.gs().queuedVertexCount() != 1)
  {
    origin.clockMasterCycle();
    ++checkpointCycle;
  }
  REQUIRE(checkpointCycle < 32);
  REQUIRE(origin.gs().context(0).test.alphaTestEnabled);
  REQUIRE(origin.gs().context(0).test.alphaTest == 6);
  const auto checkpoint = origin.saveState();
  origin.runMasterCycles(64 - checkpointCycle);
  REQUIRE(origin.gs().spriteCount() == 1);
  REQUIRE(origin.gs().pixelWriteCount() == 1);
  REQUIRE(origin.gs().queuedVertexCount() == 0);
  REQUIRE(origin.vif1DMAC().quadwordCount() == 0);
  REQUIRE(origin.gs().readPSMCT32(0, 1, 1) == UINT32_C(0x80000000));
  REQUIRE(origin.gs().readPSMCT32(0, 2, 1) == UINT32_C(0x80808080));
  REQUIRE(origin.gs().readPSMZ32(0, 1, 1) == 10);
  REQUIRE(origin.gs().readPSMZ32(0, 2, 1) == 20);
  const auto expected = origin.saveState();
  NekoSystem coldTraced;
  NekoSystem coldPlain;
  NekoSystem restoredTraced;
  NekoSystem restoredPlain;
  prepareAlphaSprite(coldTraced);
  prepareAlphaSprite(coldPlain);
  restoredTraced.loadState(checkpoint);
  restoredPlain.loadState(checkpoint);
  REQUIRE(restoredTraced.saveState() == checkpoint);
  coldTraced.startTrace();
  restoredTraced.startTrace();
  coldTraced.runMasterCycles(64);
  coldPlain.runMasterCycles(64);
  restoredTraced.runMasterCycles(64 - checkpointCycle);
  restoredPlain.runMasterCycles(64 - checkpointCycle);
  REQUIRE(coldTraced.saveState() == expected);
  REQUIRE(coldPlain.saveState() == expected);
  REQUIRE(restoredTraced.saveState() == expected);
  REQUIRE(restoredPlain.saveState() == expected);
  IOPTraceTest::Events suffix;
  for (const auto &event : coldTraced.trace())
  {
    if (event.masterCycle > checkpointCycle)
    {
      suffix.push_back(event);
    }
  }
  IOPTraceTest::requireEvents(restoredTraced.trace(), suffix);
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
