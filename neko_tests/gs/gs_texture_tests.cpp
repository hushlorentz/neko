#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "catch.hpp"
#include "gif_path3.hpp"
#include "gs.hpp"

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
      "GS texture sampling requires PSMCT32 or PSMCT24.");

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
    "GS texture sampling requires PSMCT32 or PSMCT24.");
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
