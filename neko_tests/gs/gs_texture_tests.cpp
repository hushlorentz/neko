#include <cstdint>
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
      textureValue(0, 1, 1, 1, 1));
    REQUIRE_THROWS_WITH(
      gs.sampleTextureNearest(0, 0, 0),
      "GS texture sampling requires PSMCT32.");

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
