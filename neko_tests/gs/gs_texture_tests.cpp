#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "catch.hpp"
#include "gif_path3.hpp"
#include "gs.hpp"
#include "ee_bus.hpp"
#include "iop/iop_trace_test_utils.hpp"
#include "neko_system.hpp"
#include "save_state_mutation.hpp"

namespace
{
  std::uint64_t textureValue(
    std::uint16_t basePointer,
    std::uint8_t bufferWidth,
    std::uint8_t pixelStorageMode,
    std::uint8_t widthExponent,
    std::uint8_t heightExponent,
    bool rgba = true,
    std::uint8_t function = 0)
  {
    return
      basePointer |
      (static_cast<std::uint64_t>(bufferWidth) << 14) |
      (static_cast<std::uint64_t>(pixelStorageMode) << 20) |
      (static_cast<std::uint64_t>(widthExponent) << 26) |
      (static_cast<std::uint64_t>(heightExponent) << 30) |
      (static_cast<std::uint64_t>(rgba) << 34) |
      (static_cast<std::uint64_t>(function) << 35);
  }

  std::uint64_t transferBuffer(
    std::uint16_t destinationBasePointer,
    std::uint8_t destinationWidth)
  {
    return
      (static_cast<std::uint64_t>(
        destinationBasePointer) << 32) |
      (static_cast<std::uint64_t>(
        destinationWidth) << 48);
  }

  std::uint64_t transferRegion(
    std::uint16_t width,
    std::uint16_t height)
  {
    return
      width |
      (static_cast<std::uint64_t>(height) << 32);
  }

  void upload2x2(GS *gs, std::uint16_t basePointer)
  {
    gs->writeRegister(
      GSRegisterAddress::BITBLTBUF,
      transferBuffer(basePointer, 1));
    gs->writeRegister(
      GSRegisterAddress::TRXREG,
      transferRegion(2, 2));
    gs->writeRegister(GSRegisterAddress::TRXDIR, 0);
    gs->writeRegister(
      GSRegisterAddress::HWREG,
      UINT64_C(0x2222222211111111));
    gs->writeRegister(
      GSRegisterAddress::HWREG,
      UINT64_C(0x4444444433333333));
  }

  GIFQuadword gifTag(
    std::uint16_t loopCount,
    GIFDataFormat format,
    std::uint64_t descriptors = 0)
  {
    const std::uint64_t low =
      loopCount |
      (UINT64_C(1) << 15) |
      (static_cast<std::uint64_t>(format) << 58) |
      (UINT64_C(1) << 60);
    return GIFQuadword{{
      static_cast<std::uint32_t>(low),
      static_cast<std::uint32_t>(low >> 32),
      static_cast<std::uint32_t>(descriptors),
      static_cast<std::uint32_t>(descriptors >> 32)
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

  std::uint64_t clutValue(
    std::uint16_t basePointer,
    std::uint8_t offset,
    std::uint8_t load)
  {
    return (static_cast<std::uint64_t>(basePointer) << 37) |
      (static_cast<std::uint64_t>(offset) << 56) |
      (static_cast<std::uint64_t>(load) << 61);
  }

  void uploadPalette(GS *gs, std::uint16_t base, std::uint32_t bias)
  {
    for (std::uint16_t y = 0; y < 2; ++y)
    {
      for (std::uint16_t x = 0; x < 8; ++x)
      {
        gs->writeDisplayPSMCT32(base, 1, x, y, bias + y * 8 + x);
      }
    }
  }

  void uploadIndices(GS *gs, std::uint16_t base)
  {
    gs->writeRegister(GSRegisterAddress::BITBLTBUF,
      transferBuffer(base, 4) | (UINT64_C(0x14) << 56));
    gs->writeRegister(GSRegisterAddress::TRXREG, transferRegion(256, 256));
    gs->writeRegister(GSRegisterAddress::TRXDIR, 0);
    for (std::uint16_t y = 0; y < 256; ++y)
    {
      std::uint64_t indices = 0;
      for (std::uint16_t x = 0; x < 16; ++x)
      {
        indices |= static_cast<std::uint64_t>((x + y) & 15) << (x * 4);
      }
      for (std::uint16_t group = 0; group < 16; ++group)
      {
        gs->writeRegister(GSRegisterAddress::HWREG, indices);
      }
    }

  }

  void requireStateEqual(
    const std::vector<std::uint8_t> &actual,
    const std::vector<std::uint8_t> &expected)
  {
    REQUIRE(actual.size() == expected.size());
    REQUIRE(std::equal(actual.begin(), actual.end(), expected.begin()));
  }
}

TEST_CASE("GS PSMT4 sampling uses retained CSM1 palette banks", "[gs-clut]")
{
  GS gs;
  uploadIndices(&gs, 64);
  uploadPalette(&gs, 0, 0x80402000);
  const auto texture = textureValue(64, 4, GSPixelStorageMode::PSMT4, 8, 8);
  gs.writeRegister(GSRegisterAddress::TEX0_1, texture | clutValue(0, 0, 1));
  for (const auto x : {0, 1, 7, 8, 15, 16, 31, 32, 127, 128, 255})
  {
    for (const auto y : {0, 1, 3, 4, 15, 16, 127, 128, 255})
    {
      REQUIRE(gs.sampleTextureNearest(0, x * 16, y * 16) ==
        0x80402000u + ((x + y) & 15));
    }
  }
  uploadPalette(&gs, 0, 0x40201000);
  gs.writeRegister(GSRegisterAddress::TEX0_2, texture | clutValue(0, 15, 1));
  gs.writeRegister(GSRegisterAddress::TEX0_1, texture | clutValue(0, 0, 0));
  REQUIRE(gs.sampleTextureNearest(0, 15 * 16, 0) == 0x8040200f);
  REQUIRE(gs.sampleTextureNearest(1, 15 * 16, 0) == 0x4020100f);
  gs.writeRegister(GSRegisterAddress::TEX0_2, texture | clutValue(0, 0, 0));
  REQUIRE(gs.sampleTextureNearest(1, 15 * 16, 0) == 0x8040200f);
  gs.writeRegister(GSRegisterAddress::TEX0_2, texture | clutValue(0, 0, 1));
  REQUIRE(gs.sampleTextureNearest(0, 15 * 16, 0) == 0x4020100f);
  for (std::uint8_t bank = 0; bank < 16; ++bank)
  {
    uploadPalette(&gs, 0, 0x80000000u + bank * 256);
    gs.writeRegister(GSRegisterAddress::TEX0_1, texture | clutValue(0, bank, 1));
  }
  for (std::uint8_t bank = 0; bank < 16; ++bank)
  {
    gs.writeRegister(GSRegisterAddress::TEX2_2,
      (UINT64_C(0x14) << 20) | clutValue(0, bank, 0));
    REQUIRE(gs.sampleTextureNearest(1, -16, 0) == 0x8000000fu + bank * 256);
  }
}

TEST_CASE("GS conditional CLUT loads remember each shared CBP", "[gs-clut]")
{
  for (const std::uint8_t slot : {0, 1})
  {
    GS gs;
    uploadPalette(&gs, 0, 0x11111110);
    uploadPalette(&gs, 4, 0x22222220);
    const auto texture = textureValue(64, 2, GSPixelStorageMode::PSMT4, 3, 3);
    const auto conditional = static_cast<std::uint8_t>(4 + slot);
    gs.writeRegister(GSRegisterAddress::TEX0_1,
      texture | clutValue(0, 0, conditional));
    REQUIRE(gs.sampleTextureNearest(0, 0, 0) == 0);
    gs.writeRegister(GSRegisterAddress::TEX0_1,
      texture | clutValue(0, 0, static_cast<std::uint8_t>(2 + slot)));
    REQUIRE(gs.sampleTextureNearest(0, 0, 0) == 0x11111110);
    gs.writeRegister(GSRegisterAddress::TEX0_2,
      texture | clutValue(4, 0, conditional));
    REQUIRE(gs.sampleTextureNearest(0, 0, 0) == 0x22222220);
    uploadPalette(&gs, 4, 0x33333330);
    gs.writeRegister(GSRegisterAddress::TEX0_1,
      texture | clutValue(4, 0, conditional));
    REQUIRE(gs.sampleTextureNearest(0, 0, 0) == 0x22222220);
    gs.writeRegister(GSRegisterAddress::TEX0_1, texture | clutValue(0, 0, 1));
    gs.writeRegister(GSRegisterAddress::TEX0_1,
      texture | clutValue(4, 0, conditional));
    REQUIRE(gs.sampleTextureNearest(0, 0, 0) == 0x11111110);
    gs.writeRegister(GSRegisterAddress::TEX0_1,
      texture | clutValue(4, 0, static_cast<std::uint8_t>(5 - slot)));
    REQUIRE(gs.sampleTextureNearest(0, 0, 0) == 0x33333330);
  }
}

TEST_CASE("GS TEX2 updates texture selection without changing geometry or filters",
  "[gs-clut]")
{
  for (const auto context : {0, 1})
  {
    GS gs;
    uploadPalette(&gs, 4, 0x80808080);
    const auto tex0Address = context == 0 ? GSRegisterAddress::TEX0_1 :
      GSRegisterAddress::TEX0_2;
    const auto tex2Address = context == 0 ? GSRegisterAddress::TEX2_1 :
      GSRegisterAddress::TEX2_2;
    const auto texture = textureValue(64, 2, GSPixelStorageMode::PSMCT32,
      3, 3, false, 2);
    gs.writeRegister(tex0Address, texture);
    const auto subset = (UINT64_C(0x14) << 20) | clutValue(4, 3, 1);
    gs.writeRegister(tex2Address, subset | UINT64_C(0xfffff));
    REQUIRE(gs.texture(context).basePointer == 64);
    REQUIRE(gs.texture(context).bufferWidth == 2);
    REQUIRE(gs.texture(context).widthExponent == 3);
    REQUIRE(gs.texture(context).heightExponent == 3);
    REQUIRE_FALSE(gs.texture(context).rgba);
    REQUIRE(gs.texture(context).function == 2);
    REQUIRE(gs.texture(context).pixelStorageMode == GSPixelStorageMode::PSMT4);
    REQUIRE(gs.registerValue(tex0Address) ==
      ((texture & ~(UINT64_C(0x3f) << 20)) | subset));
    REQUIRE(gs.registerValue(tex2Address) == (subset | UINT64_C(0xfffff)));
    REQUIRE(gs.sampleTextureNearest(context, 0, 0) == 0x80808080);
  }
}

TEST_CASE("GS indexed bilinear filtering interpolates palette colors not indices",
  "[gs-clut]")
{
  GS gs;
  uploadIndices(&gs, 64);
  uploadPalette(&gs, 0, 0);
  gs.writeDisplayPSMCT32(0, 1, 0, 0, 0x00000000);
  gs.writeDisplayPSMCT32(0, 1, 1, 0, 0x80402000);
  gs.writeDisplayPSMCT32(0, 1, 2, 0, 0x00804020);
  gs.writeRegister(GSRegisterAddress::TEX0_1,
    textureValue(64, 4, GSPixelStorageMode::PSMT4, 8, 8) | clutValue(0, 0, 1));
  gs.writeRegister(GSRegisterAddress::TEX1_1, 0x60);
  gs.writeRegister(GSRegisterAddress::TEXA, UINT64_C(0xff000080ff));
  REQUIRE(gs.sampleTextureBilinear(0, 16, 16) == 0x40402008);
  REQUIRE(gs.sampleTextureBilinear(0, 24, 8) == 0x80402000);
}

TEST_CASE("GS CLUT boundaries reject unsupported loads without changing retained colors",
  "[gs-clut]")
{
  GS gs;
  uploadPalette(&gs, 4, 0x80808080);
  const auto texture = textureValue(64, 2, GSPixelStorageMode::PSMT4, 3, 3);
  gs.writeRegister(GSRegisterAddress::TEX0_1, texture | clutValue(4, 0, 2));
  for (const auto invalid : {
      clutValue(8, 0, 2) | (UINT64_C(2) << 51),
      clutValue(8, 0, 2) | (UINT64_C(1) << 55),
      clutValue(8, 16, 2), clutValue(8, 0, 6), clutValue(8, 0, 7)})
  {
    REQUIRE_THROWS(gs.writeRegister(GSRegisterAddress::TEX0_2, texture | invalid));
    REQUIRE(gs.sampleTextureNearest(0, 0, 0) == 0x80808080);
  }
  uploadPalette(&gs, 4, 0x40404040);
  gs.writeRegister(GSRegisterAddress::TEX0_1, texture | clutValue(4, 0, 4));
  REQUIRE(gs.sampleTextureNearest(0, 0, 0) == 0x80808080);
  gs.writeRegister(GSRegisterAddress::TEX0_2,
    textureValue(0, 1, GSPixelStorageMode::PSMCT32, 3, 3) | clutValue(8, 0, 2));
  gs.writeRegister(GSRegisterAddress::TEX0_1, texture | clutValue(4, 0, 4));
  REQUIRE(gs.sampleTextureNearest(0, 0, 0) == 0x80808080);
  REQUIRE_THROWS_WITH(gs.writeRegister(GSRegisterAddress::TEX0_2,
    textureValue(64, 2, 0x13, 3, 3) | clutValue(8, 0, 1)),
    "GS CLUT loading requires PSMT4.");
  for (const auto width : {1, 3, 33})
  {
    gs.writeRegister(GSRegisterAddress::TEX0_1,
      textureValue(64, width, GSPixelStorageMode::PSMT4, 3, 3));
    REQUIRE_THROWS_WITH(gs.sampleTextureNearest(0, 0, 0),
      "GS PSMT4 sampling requires an even buffer width from 2 to 32.");
  }
}

TEST_CASE("GS retained palettes restore independently of overwritten VRAM and latest CBP",
  "[gs-clut][save-state]")
{
  NekoSystem original;
  auto &gs = original.gs();
  const auto texture = textureValue(64, 2, GSPixelStorageMode::PSMT4, 3, 3);
  uploadPalette(&gs, 4, 0x80808080);
  gs.writeRegister(GSRegisterAddress::TEX0_1, texture | clutValue(4, 15, 2));
  uploadPalette(&gs, 8, 0x40404040);
  gs.writeRegister(GSRegisterAddress::TEX0_2, texture | clutValue(8, 3, 3));
  uploadPalette(&gs, 4, 0x20202020);
  uploadPalette(&gs, 8, 0x10101010);
  const auto saved = original.saveState();
  NekoSystem restored;
  restored.loadState(saved);
  requireStateEqual(restored.saveState(), saved);
  for (auto *system : {&original, &restored})
  {
    REQUIRE(system->gs().sampleTextureNearest(0, 0, 0) == 0x80808080);
    REQUIRE(system->gs().sampleTextureNearest(1, 0, 0) == 0x40404040);
    system->gs().writeRegister(GSRegisterAddress::TEX2_1,
      (UINT64_C(0x14) << 20) | clutValue(4, 15, 4));
    system->gs().writeRegister(GSRegisterAddress::TEX2_2,
      (UINT64_C(0x14) << 20) | clutValue(8, 3, 5));
    REQUIRE(system->gs().sampleTextureNearest(0, 0, 0) == 0x80808080);
    REQUIRE(system->gs().sampleTextureNearest(1, 0, 0) == 0x40404040);
  }
  requireStateEqual(original.saveState(), restored.saveState());
  auto changed = SaveStateMutation(saved);
  changed.writeScalar("system.gs.clutEntries[240].value", 0xaabbccdd);
  changed.refreshChecksum();
  restored.loadState(changed.bytes());
  REQUIRE(restored.gs().sampleTextureNearest(0, 0, 0) == 0xaabbccdd);
  REQUIRE(restored.saveState() != saved);

  for (const auto &invalid : std::vector<std::pair<const char *, std::uint64_t>>{
      {"system.gs.clutBasePointers[0].value", 0x4000},
      {"system.gs.clutBasePointers[1].value", 0x4000},
      {"system.gs.contexts[0].clut.basePointer", 0x4000},
      {"system.gs.contexts[1].clut.pixelStorageMode", 16},
      {"system.gs.contexts[0].clut.offset", 32},
      {"system.gs.contexts[1].clut.loadControl", 8}})
  {
    auto malformed = SaveStateMutation(saved);
    malformed.writeScalar(invalid.first, invalid.second);
    malformed.refreshChecksum();
    REQUIRE_THROWS_AS(restored.loadState(malformed.bytes()), std::invalid_argument);
    requireStateEqual(restored.saveState(), changed.bytes());
  }
}

TEST_CASE("GS indexed scratchpad DIRECT preserves palette TFX alpha and four-mode state",
  "[gs-clut][conformance]")
{
  const auto prepare = [](NekoSystem &system)
  {
    auto &gs = system.gs();
    gs.writeRegister(GSRegisterAddress::FRAME_1, UINT64_C(1) | (UINT64_C(1) << 16));
    gs.writeRegister(GSRegisterAddress::SCISSOR_1,
      (UINT64_C(7) << 16) | (UINT64_C(7) << 48));
    gs.writeRegister(GSRegisterAddress::ZBUF_1, 2);
    gs.writeRegister(GSRegisterAddress::ALPHA_1, 0x44);
    gs.writeRegister(GSRegisterAddress::RGBAQ, UINT64_C(0x3f80000080808080));
    gs.writeRegister(GSRegisterAddress::TEX1_1, 0x61);
    gs.writeRegister(GSRegisterAddress::CLAMP_1, 5);
    gs.writeDisplayPSMCT32(4, 1, 0, 0, 0x00808080);
    gs.writeDisplayPSMCT32(4, 1, 1, 0, 0x80808080);
    gs.writeRegister(GSRegisterAddress::BITBLTBUF,
      transferBuffer(64, 2) | (UINT64_C(0x14) << 56));
    gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(8, 8));
    gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
    for (unsigned group = 0; group < 4; ++group)
    {
      gs.writeRegister(GSRegisterAddress::HWREG, UINT64_C(0x0000001000000010));
    }
    for (const std::uint16_t x : {1, 2})
    {
      gs.writePSMCT32(0, x, 1, 0x80000000);
      gs.writePSMZ32(0, x, 1, 10);
    }
    const auto texture = textureValue(64, 2, GSPixelStorageMode::PSMT4, 3, 3);
    const std::vector<GIFQuadword> packet = {
      {{0, 0, 0, UINT32_C(0x50000009)}},
      gifTag(8, GIFDataFormat::Packed, 0x0e),
      adWrite(GSRegisterAddress::TEX0_1, texture | clutValue(4, 3, 2)),
      adWrite(GSRegisterAddress::TEX2_1,
        (UINT64_C(0x14) << 20) | clutValue(4, 3, 4)),
      adWrite(GSRegisterAddress::TEST_1, 0x3000d),
      adWrite(GSRegisterAddress::PRIM, 0x156),
      adWrite(GSRegisterAddress::UV, 0),
      adWrite(GSRegisterAddress::XYZ2, UINT64_C(0x00100010)),
      adWrite(GSRegisterAddress::UV, 32),
      adWrite(GSRegisterAddress::XYZ2, UINT64_C(0x1400200030))
    };
    for (std::size_t index = 0; index < packet.size(); ++index)
    {
      const auto &q = packet[index];
      REQUIRE(system.eeMemorySystem().writeScratchpadDMA128(
        0x1000 + static_cast<std::uint32_t>(index * 16),
        {q[0] | (static_cast<std::uint64_t>(q[1]) << 32),
         q[2] | (static_cast<std::uint64_t>(q[3]) << 32)}) ==
        EEScratchpadAccessResult::Completed);
    }
    auto &bus = system.eeBus();
    bus.write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
    bus.write32(EEMemoryMap::D1_MADR, 0x80001000);
    bus.write32(EEMemoryMap::D1_QWC, static_cast<std::uint32_t>(packet.size()));
    bus.write32(EEMemoryMap::D1_CHCR,
      GIFDMACChannelControl::FROM_MEMORY | GIFDMACChannelControl::START);
    bus.write32(0x10000, 0x1000ffff);
    bus.write32(0x10004, 0);
    system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
    system.eeCore().startExecution(0xa0010000);
  };
  NekoSystem origin;
  prepare(origin);
  bool paletteLoaded = true;
  SECTION("retained palette and queued first vertex") {}
  SECTION("before the first palette load") { paletteLoaded = false; }
  std::uint64_t checkpointCycle = 0;
  while (paletteLoaded && checkpointCycle < 32 &&
         origin.gs().queuedVertexCount() != 1)
  {
    origin.clockMasterCycle();
    ++checkpointCycle;
  }
  REQUIRE(checkpointCycle < 32);
  if (paletteLoaded)
  {
    REQUIRE(origin.gs().sampleTextureBilinear(0, 24, 8) == 0x80808080);
  }
  const auto checkpoint = origin.saveState();
  origin.runMasterCycles(64 - checkpointCycle);
  REQUIRE(origin.gs().spriteCount() == 1);
  REQUIRE(origin.gs().pixelWriteCount() == 1);
  REQUIRE(origin.gs().readPSMCT32(0, 1, 1) == 0x80000000);
  // U=1 blends palette alpha 0/128 to 64, then source-alpha blending halves RGB.
  REQUIRE(origin.gs().readPSMCT32(0, 2, 1) == 0x40404040);
  REQUIRE(origin.gs().readPSMZ32(0, 1, 1) == 10);
  REQUIRE(origin.gs().readPSMZ32(0, 2, 1) == 20);
  REQUIRE(origin.gs().queuedVertexCount() == 0);
  REQUIRE(origin.vif1DMAC().quadwordCount() == 0);
  const auto expected = origin.saveState();
  NekoSystem coldTraced, coldPlain, restoredTraced, restoredPlain;
  prepare(coldTraced);
  prepare(coldPlain);
  restoredTraced.loadState(checkpoint);
  restoredPlain.loadState(checkpoint);
  requireStateEqual(restoredTraced.saveState(), checkpoint);
  coldTraced.startTrace();
  restoredTraced.startTrace();
  coldTraced.runMasterCycles(64);
  coldPlain.runMasterCycles(64);
  restoredTraced.runMasterCycles(64 - checkpointCycle);
  restoredPlain.runMasterCycles(64 - checkpointCycle);
  for (auto *system : {&coldTraced, &coldPlain, &restoredTraced, &restoredPlain})
  {
    requireStateEqual(system->saveState(), expected);
  }
  IOPTraceTest::Events suffix;
  for (const auto &event : coldTraced.trace())
  {
    if (event.masterCycle > checkpointCycle)
    {
      suffix.push_back(event);
    }
  }
  REQUIRE_FALSE(suffix.empty());
  IOPTraceTest::requireEvents(restoredTraced.trace(), suffix);
}

TEST_CASE("GS PSMCT32 Texture State Tests")
{
  SECTION("TEX0 and TEX1 decode independently per context")
  {
    GS gs;
    gs.writeRegister(
      GSRegisterAddress::TEX0_1,
      textureValue(0x1234, 17, 0, 6, 5, true, 2));
    gs.writeRegister(
      GSRegisterAddress::TEX0_2,
      textureValue(0x42, 3, 0, 2, 1, false, 1));
    gs.writeRegister(
      GSRegisterAddress::TEX1_1,
      (UINT64_C(5) << 2) |
      (UINT64_C(1) << 5) |
      (UINT64_C(3) << 6));

    const GSTexture &first = gs.texture(0);
    REQUIRE(first.basePointer == 0x1234);
    REQUIRE(first.bufferWidth == 17);
    REQUIRE(first.pixelStorageMode == GSPixelStorageMode::PSMCT32);
    REQUIRE(first.widthExponent == 6);
    REQUIRE(first.heightExponent == 5);
    REQUIRE(first.rgba);
    REQUIRE(first.function == 2);
    REQUIRE(first.maximumMipLevel == 5);
    REQUIRE(first.magnificationLinear);
    REQUIRE(first.minificationFilter == 3);

    const GSTexture &second = gs.texture(1);
    REQUIRE(second.basePointer == 0x42);
    REQUIRE(second.bufferWidth == 3);
    REQUIRE(second.widthExponent == 2);
    REQUIRE(second.heightExponent == 1);
    REQUIRE(!second.rgba);
    REQUIRE(second.function == 1);
    REQUIRE(!second.magnificationLinear);
    REQUIRE(second.minificationFilter == 0);
  }
}

TEST_CASE("GS PSMCT32 Nearest Texture Sampling Tests")
{
  SECTION("Uploaded texels are selected by the integer part of 16.4 coordinates")
  {
    GS gs;
    upload2x2(&gs, 32);
    gs.writeRegister(
      GSRegisterAddress::TEX0_1,
      textureValue(32, 1, 0, 1, 1));

    REQUIRE(gs.sampleTextureNearest(0, 0, 0) == 0x11111111);
    REQUIRE(gs.sampleTextureNearest(0, 15, 15) == 0x11111111);
    REQUIRE(gs.sampleTextureNearest(0, 16, 0) == 0x22222222);
    REQUIRE(gs.sampleTextureNearest(0, 0, 16) == 0x33333333);
    REQUIRE(gs.sampleTextureNearest(0, 16, 16) == 0x44444444);
  }

  SECTION("Repeat and clamp modes transform out-of-range coordinates")
  {
    GS gs;
    upload2x2(&gs, 0);
    gs.writeRegister(
      GSRegisterAddress::TEX0_1,
      textureValue(0, 1, 0, 1, 1));

    REQUIRE(gs.sampleTextureNearest(0, -16, 0) == 0x22222222);
    REQUIRE(gs.sampleTextureNearest(0, 32, 32) == 0x11111111);

    gs.writeRegister(
      GSRegisterAddress::CLAMP_1,
      UINT64_C(1) | (UINT64_C(1) << 2));
    REQUIRE(gs.sampleTextureNearest(0, -16, -16) == 0x11111111);
    REQUIRE(gs.sampleTextureNearest(0, 64, 64) == 0x44444444);
  }

  SECTION("Region clamp and region repeat use their configured fields")
  {
    GS gs;
    gs.writeRegister(
      GSRegisterAddress::BITBLTBUF,
      transferBuffer(0, 1));
    gs.writeRegister(
      GSRegisterAddress::TRXREG,
      transferRegion(4, 1));
    gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
    gs.writeRegister(
      GSRegisterAddress::HWREG,
      UINT64_C(0x2222222211111111));
    gs.writeRegister(
      GSRegisterAddress::HWREG,
      UINT64_C(0x4444444433333333));
    gs.writeRegister(
      GSRegisterAddress::TEX0_1,
      textureValue(0, 1, 0, 2, 0));

    gs.writeRegister(
      GSRegisterAddress::CLAMP_1,
      UINT64_C(2) |
      (UINT64_C(1) << 4) |
      (UINT64_C(2) << 14));
    REQUIRE(gs.sampleTextureNearest(0, 0, 0) == 0x22222222);
    REQUIRE(gs.sampleTextureNearest(0, 48, 0) == 0x33333333);

    gs.writeRegister(
      GSRegisterAddress::CLAMP_1,
      UINT64_C(3) |
      (UINT64_C(1) << 4) |
      (UINT64_C(2) << 14));
    REQUIRE(gs.sampleTextureNearest(0, 0, 0) == 0x33333333);
    REQUIRE(gs.sampleTextureNearest(0, 16, 0) == 0x44444444);
    REQUIRE(gs.sampleTextureNearest(0, 32, 0) == 0x33333333);
    REQUIRE(gs.sampleTextureNearest(0, 48, 0) == 0x44444444);
  }

  SECTION("Unsupported texture configurations fail explicitly")
  {
    GS gs;
    gs.writeRegister(
      GSRegisterAddress::TEX0_1,
      textureValue(0, 0, 0, 1, 1));
    REQUIRE_THROWS_WITH(
      gs.sampleTextureNearest(0, 0, 0),
      "GS texture sampling requires a valid buffer width.");

    gs.writeRegister(
      GSRegisterAddress::TEX0_1,
      textureValue(0, 1, GSPixelStorageMode::PSMCT16, 1, 1));
    REQUIRE_THROWS_WITH(
      gs.sampleTextureNearest(0, 0, 0),
      "GS texture sampling requires PSMCT32, PSMCT24 or PSMT4.");

    gs.writeRegister(
      GSRegisterAddress::TEX0_1,
      textureValue(0, 1, 0, 11, 1));
    REQUIRE_THROWS_WITH(
      gs.sampleTextureNearest(0, 0, 0),
      "GS texture dimensions exceed the supported range.");

    gs.writeRegister(
      GSRegisterAddress::TEX0_1,
      textureValue(0, 1, 0, 1, 1));
    gs.writeRegister(
      GSRegisterAddress::TEX1_1,
      UINT64_C(1) << 5);
    REQUIRE_THROWS_WITH(
      gs.sampleTextureNearest(0, 0, 0),
      "GS texture sampling requires nearest filtering.");

    gs.writeRegister(
      GSRegisterAddress::TEX1_1,
      UINT64_C(1) << 2);
    REQUIRE_THROWS_WITH(
      gs.sampleTextureNearest(0, 0, 0),
      "GS texture mipmapping is not implemented.");
  }
}

TEST_CASE("GS PSMCT32 bilinear sampling uses half-texel four-channel weights",
          "[gs][bilinear]")
{
  GS gs;
  gs.writeRegister(GSRegisterAddress::TEX0_1,
    textureValue(32, 1, 0, 3, 3));
  gs.writeRegister(GSRegisterAddress::TEX1_1,
    (UINT64_C(1) << 5) | (UINT64_C(1) << 6));
  gs.writeDisplayPSMCT32(32, 1, 0, 0, UINT32_C(0x10203040));
  gs.writeDisplayPSMCT32(32, 1, 1, 0, UINT32_C(0x50607080));
  gs.writeDisplayPSMCT32(32, 1, 0, 1, UINT32_C(0x90a0b0c0));
  gs.writeDisplayPSMCT32(32, 1, 1, 1, UINT32_C(0xd0e0f000));
  REQUIRE(gs.sampleTextureBilinear(0, 8, 8) == UINT32_C(0x10203040));
  REQUIRE(gs.sampleTextureBilinear(0, 24, 8) == UINT32_C(0x50607080));
  REQUIRE(gs.sampleTextureBilinear(0, 24, 24) == UINT32_C(0xd0e0f000));
  REQUIRE(gs.sampleTextureBilinear(0, 16, 16) == UINT32_C(0x70809060));
  REQUIRE(gs.sampleTextureBilinear(0, 12, 12) == UINT32_C(0x40506060));

  gs.writeDisplayPSMCT32(32, 1, 0, 0, 0);
  gs.writeDisplayPSMCT32(32, 1, 1, 0, UINT32_C(0x01010101));
  gs.writeDisplayPSMCT32(32, 1, 0, 1, UINT32_C(0x01010101));
  gs.writeDisplayPSMCT32(32, 1, 1, 1, UINT32_C(0x02020202));
  REQUIRE(gs.sampleTextureBilinear(0, 16, 16) == UINT32_C(0x01010101));
  REQUIRE_THROWS_WITH(gs.sampleTextureNearest(0, 16, 16),
    "GS texture sampling requires nearest filtering.");
}

TEST_CASE("GS PSMCT32 bilinear sampling wraps each neighbor",
          "[gs][bilinear]")
{
  GS gs;
  gs.writeRegister(GSRegisterAddress::TEX0_1,
    textureValue(32, 1, 0, 3, 3));
  gs.writeRegister(GSRegisterAddress::TEX1_1,
    (UINT64_C(1) << 5) | (UINT64_C(1) << 6));
  gs.writeDisplayPSMCT32(32, 1, 0, 0, UINT32_C(0x20202020));
  gs.writeDisplayPSMCT32(32, 1, 7, 0, UINT32_C(0x60606060));
  gs.writeDisplayPSMCT32(32, 1, 0, 7, UINT32_C(0xa0a0a0a0));
  gs.writeDisplayPSMCT32(32, 1, 7, 7, UINT32_C(0xe0e0e0e0));
  REQUIRE(gs.sampleTextureBilinear(0, 0, 0) == UINT32_C(0x80808080));
  gs.writeRegister(GSRegisterAddress::CLAMP_1,
    UINT64_C(1) | (UINT64_C(1) << 2));
  REQUIRE(gs.sampleTextureBilinear(0, 0, 0) == UINT32_C(0x20202020));
  REQUIRE(gs.sampleTextureBilinear(0,
    std::numeric_limits<std::int32_t>::min(),
    std::numeric_limits<std::int32_t>::max()) == UINT32_C(0xa0a0a0a0));

  gs.writeDisplayPSMCT32(32, 1, 1, 1, UINT32_C(0x11111111));
  gs.writeRegister(GSRegisterAddress::CLAMP_1,
    UINT64_C(2) | (UINT64_C(2) << 2) |
    (UINT64_C(1) << 4) | (UINT64_C(2) << 14) |
    (UINT64_C(1) << 24) | (UINT64_C(2) << 34));
  REQUIRE(gs.sampleTextureBilinear(0, 0, 0) == UINT32_C(0x11111111));
  gs.writeDisplayPSMCT32(32, 1, 4, 4, UINT32_C(0x10101010));
  gs.writeDisplayPSMCT32(32, 1, 5, 4, UINT32_C(0x30303030));
  gs.writeDisplayPSMCT32(32, 1, 4, 5, UINT32_C(0x50505050));
  gs.writeDisplayPSMCT32(32, 1, 5, 5, UINT32_C(0x70707070));
  gs.writeRegister(GSRegisterAddress::CLAMP_1,
    UINT64_C(3) | (UINT64_C(3) << 2) |
    (UINT64_C(1) << 4) | (UINT64_C(4) << 14) |
    (UINT64_C(1) << 24) | (UINT64_C(4) << 34));
  REQUIRE(gs.sampleTextureBilinear(0, 16, 16) == UINT32_C(0x40404040));
}

TEST_CASE("GS bilinear sampling retains configuration guards",
          "[gs][bilinear]")
{
  GS gs;
  gs.writeRegister(GSRegisterAddress::TEX1_1,
    (UINT64_C(1) << 5) | (UINT64_C(1) << 6));
  gs.writeRegister(GSRegisterAddress::TEX0_1,
    textureValue(32, 0, 0, 3, 3));
  REQUIRE_THROWS_WITH(gs.sampleTextureBilinear(0, 8, 8),
    "GS texture sampling requires a valid buffer width.");
  gs.writeRegister(GSRegisterAddress::TEX0_1,
    textureValue(32, 1, 2, 3, 3));
  REQUIRE_THROWS_WITH(gs.sampleTextureBilinear(0, 8, 8),
    "GS texture sampling requires PSMCT32, PSMCT24 or PSMT4.");
  gs.writeRegister(GSRegisterAddress::TEX0_1,
    textureValue(32, 1, 0, 11, 3));
  REQUIRE_THROWS_WITH(gs.sampleTextureBilinear(0, 8, 8),
    "GS texture dimensions exceed the supported range.");
  gs.writeRegister(GSRegisterAddress::TEX0_1,
    textureValue(32, 1, 0, 3, 2));
  REQUIRE_THROWS_WITH(gs.sampleTextureBilinear(0, 8, 8),
    "GS bilinear sampling requires textures of at least 8x8 texels.");
  gs.writeRegister(GSRegisterAddress::TEX0_1,
    textureValue(32, 1, 0, 3, 3));
  gs.writeRegister(GSRegisterAddress::TEX1_1,
    (UINT64_C(1) << 2) | (UINT64_C(1) << 5) | (UINT64_C(1) << 6));
  REQUIRE_THROWS_WITH(gs.sampleTextureBilinear(0, 8, 8),
    "GS texture mipmapping is not implemented.");
  for (const std::uint64_t filter : {
         UINT64_C(0), UINT64_C(1) << 5, UINT64_C(1) << 6})
  {
    gs.writeRegister(GSRegisterAddress::TEX1_1, filter);
    REQUIRE_THROWS_WITH(gs.sampleTextureBilinear(0, 8, 8),
      "GS texture sampling requires matching base-level linear filters.");
  }
}

TEST_CASE("GS PSMCT24 uses shared TEXA alpha without changing stored words",
          "[gs][psmct24]")
{
  GS gs;
  for (const auto address : {GSRegisterAddress::TEX0_1, GSRegisterAddress::TEX0_2})
  {
    gs.writeRegister(address, textureValue(32, 1, 1, 3, 3));
  }
  gs.writeDisplayPSMCT32(32, 1, 0, 0, UINT32_C(0xcc000000));
  gs.writeDisplayPSMCT32(32, 1, 1, 0, UINT32_C(0xdd112233));
  gs.writeDisplayPSMCT32(32, 1, 2, 0, UINT32_C(0xee000001));
  for (const std::uint8_t alpha : {0, 1, 0x7f, 0x80, 0xff})
  {
    const auto expanded = static_cast<std::uint32_t>(alpha) << 24;
    gs.writeRegister(0x3b, (UINT64_C(0xa5) << 32) | alpha);
    for (const auto context : {0, 1})
    {
      REQUIRE(gs.sampleTextureNearest(context, 0, 0) == expanded);
      REQUIRE(gs.sampleTextureNearest(context, 16, 0) == (expanded | 0x112233));
      gs.writeRegister(0x3b, (UINT64_C(0x5a) << 32) | (UINT64_C(1) << 15) | alpha);
      REQUIRE(gs.sampleTextureNearest(context, 0, 0) == 0);
      REQUIRE(gs.sampleTextureNearest(context, 32, 0) == (expanded | 1));
      gs.writeRegister(0x3b, (UINT64_C(0xa5) << 32) | alpha);
    }
  }
  REQUIRE(gs.readDisplayPSMCT32(32, 1, 0, 0) == UINT32_C(0xcc000000));
  REQUIRE(gs.readDisplayPSMCT32(32, 1, 1, 0) == UINT32_C(0xdd112233));
  gs.writeRegister(GSRegisterAddress::TEX0_2, textureValue(32, 1, 0, 3, 3));
  REQUIRE(gs.sampleTextureNearest(1, 0, 0) == UINT32_C(0xcc000000));
  REQUIRE(gs.sampleTextureNearest(1, 16, 0) == UINT32_C(0xdd112233));
}

TEST_CASE("GS PSMCT24 shares word layout across pages and local memory wrap",
          "[gs][psmct24]")
{
  GS gs;
  for (const std::uint16_t base : {32, 0x3fff})
  {
    gs.writeRegister(GSRegisterAddress::TEX0_1, textureValue(base, 2, 1, 7, 6));
    gs.writeRegister(0x3b, 0x7f);
    for (const auto coordinate : {
           std::pair<std::uint16_t, std::uint16_t>{7, 1}, {63, 31}, {64, 32}})
    {
      const auto x = coordinate.first;
      const auto y = coordinate.second;
      gs.writeDisplayPSMCT32(base, 2, x, y, UINT32_C(0xee123456));
      REQUIRE(gs.sampleTextureNearest(0, x * 16, y * 16) == UINT32_C(0x7f123456));
      REQUIRE(gs.readDisplayPSMCT32(base, 2, x, y) == UINT32_C(0xee123456));
    }
  }
}

TEST_CASE("GS PSMCT24 expands each alpha before bilinear filtering",
          "[gs][psmct24][bilinear]")
{
  GS gs;
  gs.writeRegister(GSRegisterAddress::TEX0_1, textureValue(32, 1, 1, 3, 3));
  gs.writeRegister(GSRegisterAddress::TEX1_1,
    (UINT64_C(1) << 5) | (UINT64_C(1) << 6));
  gs.writeDisplayPSMCT32(32, 1, 0, 0, UINT32_C(0xaa000000));
  gs.writeDisplayPSMCT32(32, 1, 1, 0, UINT32_C(0xbb203040));
  gs.writeDisplayPSMCT32(32, 1, 0, 1, UINT32_C(0xcc405060));
  gs.writeDisplayPSMCT32(32, 1, 1, 1, UINT32_C(0xdd607080));
  gs.writeRegister(0x3b, UINT64_C(0x80) | (UINT64_C(1) << 15));
  REQUIRE(gs.sampleTextureBilinear(0, 8, 8) == 0);
  REQUIRE(gs.sampleTextureBilinear(0, 16, 16) == UINT32_C(0x60303c48));
  REQUIRE(gs.sampleTextureBilinear(0, 12, 12) == UINT32_C(0x38181f26));
  gs.writeRegister(0x3b, 0x80);
  REQUIRE(gs.sampleTextureBilinear(0, 8, 8) == UINT32_C(0x80000000));
  REQUIRE(gs.sampleTextureBilinear(0, 16, 16) == UINT32_C(0x80303c48));
}

TEST_CASE("PATH3 PSMCT32 uploads can be sampled as PSMCT24",
          "[gs][psmct24]")
{
  GS gs;
  GIFDecoder decoder;
  decoder.attachRegisterWriteHandler(&gs);
  GIFPath3Transfer path3(&decoder);
  const std::vector<GIFQuadword> packet = {
    gifTag(6, GIFDataFormat::Packed, GIFRegisterDescriptor::AD),
    adWrite(GSRegisterAddress::BITBLTBUF, transferBuffer(16, 1)),
    adWrite(GSRegisterAddress::TRXPOS, 0),
    adWrite(GSRegisterAddress::TRXREG, transferRegion(2, 2)),
    adWrite(GSRegisterAddress::TRXDIR, 0),
    adWrite(GSRegisterAddress::TEXA, UINT64_C(0x000000810000807f)),
    adWrite(GSRegisterAddress::TEX0_1, textureValue(16, 1, 1, 1, 1)),
    gifTag(1, GIFDataFormat::Image),
    GIFQuadword{{0xaa000000, 0xbb112233, 0xcc000001, 0xddccbbaa}}
  };
  const auto result = path3.submitQuadwords(packet.data(), packet.size());
  REQUIRE(result.transferredQuadwords == packet.size());
  REQUIRE(result.packetComplete);
  REQUIRE(gs.sampleTextureNearest(0, 0, 0) == 0);
  REQUIRE(gs.sampleTextureNearest(0, 16, 0) == UINT32_C(0x7f112233));
  REQUIRE(gs.sampleTextureNearest(0, 0, 16) == UINT32_C(0x7f000001));
  REQUIRE(gs.sampleTextureNearest(0, 16, 16) == UINT32_C(0x7fccbbaa));
}

TEST_CASE("PATH3 PSMCT32 Texture Upload and Sampling Tests")
{
  SECTION("A PATH3 IMAGE upload is sampled through TEX0 state")
  {
    GS gs;
    GIFDecoder decoder;
    decoder.attachRegisterWriteHandler(&gs);
    GIFPath3Transfer path3(&decoder);
    const std::vector<GIFQuadword> packet = {
      gifTag(5, GIFDataFormat::Packed, GIFRegisterDescriptor::AD),
      adWrite(
        GSRegisterAddress::BITBLTBUF,
        transferBuffer(16, 1)),
      adWrite(
        GSRegisterAddress::TRXPOS,
        0),
      adWrite(
        GSRegisterAddress::TRXREG,
        transferRegion(2, 2)),
      adWrite(GSRegisterAddress::TRXDIR, 0),
      adWrite(
        GSRegisterAddress::TEX0_1,
        textureValue(16, 1, 0, 1, 1)),
      gifTag(1, GIFDataFormat::Image),
      GIFQuadword{{
        0x01020304,
        0x11121314,
        0x21222324,
        0x31323334
      }}
    };

    const GIFPath3SubmissionResult result =
      path3.submitQuadwords(packet.data(), packet.size());
    REQUIRE(result.transferredQuadwords == packet.size());
    REQUIRE(result.packetComplete);
    REQUIRE(gs.sampleTextureNearest(0, 0, 0) == 0x01020304);
    REQUIRE(gs.sampleTextureNearest(0, 16, 0) == 0x11121314);
    REQUIRE(gs.sampleTextureNearest(0, 0, 16) == 0x21222324);
    REQUIRE(gs.sampleTextureNearest(0, 16, 16) == 0x31323334);
  }
}
