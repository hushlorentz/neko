#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "gif_path3.hpp"
#include "gs.hpp"

namespace
{
  std::uint64_t transferBuffer(
    std::uint16_t destinationBasePointer,
    std::uint8_t destinationWidth,
    std::uint8_t destinationFormat)
  {
    return
      (static_cast<std::uint64_t>(
        destinationBasePointer) << 32) |
      (static_cast<std::uint64_t>(
        destinationWidth) << 48) |
      (static_cast<std::uint64_t>(
        destinationFormat) << 56);
  }

  std::uint64_t transferPosition(
    std::uint16_t destinationX,
    std::uint16_t destinationY)
  {
    return
      (static_cast<std::uint64_t>(destinationX) << 32) |
      (static_cast<std::uint64_t>(destinationY) << 48);
  }

  std::uint64_t sourceTransferBuffer(
    std::uint16_t sourceBasePointer,
    std::uint8_t sourceWidth,
    std::uint8_t sourceFormat)
  {
    return
      sourceBasePointer |
      (static_cast<std::uint64_t>(sourceWidth) << 16) |
      (static_cast<std::uint64_t>(sourceFormat) << 24);
  }

  std::uint64_t sourceTransferPosition(
    std::uint16_t sourceX,
    std::uint16_t sourceY)
  {
    return
      sourceX |
      (static_cast<std::uint64_t>(sourceY) << 16);
  }

  std::uint64_t transferRegion(
    std::uint16_t width,
    std::uint16_t height)
  {
    return
      width |
      (static_cast<std::uint64_t>(height) << 32);
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

TEST_CASE("GS PSMT4 uploads follow manual column word and nibble planes", "[gs4]")
{
  GS gs;
  gs.writeRegister(GSRegisterAddress::BITBLTBUF, transferBuffer(0, 2, 0x14));
  gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(32, 16));
  REQUIRE_NOTHROW(gs.writeRegister(GSRegisterAddress::TRXDIR, 0));
  for (unsigned pixel = 0; pixel < 512; pixel += 16)
  {
    std::uint64_t data = 0;
    for (unsigned nibble = 0; nibble < 16; ++nibble)
    {
      const auto index = pixel + nibble;
      data |= static_cast<std::uint64_t>(
        ((index % 32) + 3 * (index / 32) + 1) % 16) << (nibble * 4);
    }
    gs.writeRegister(GSRegisterAddress::HWREG, data);
  }
  constexpr unsigned words[4][8] = {
    {0, 1, 4, 5, 8, 9, 12, 13},
    {2, 3, 6, 7, 10, 11, 14, 15},
    {8, 9, 12, 13, 0, 1, 4, 5},
    {10, 11, 14, 15, 2, 3, 6, 7}
  };
  for (unsigned column = 0; column < 4; ++column)
  {
    std::uint32_t expected[16]{};
    for (unsigned y = 0; y < 4; ++y)
    {
      for (unsigned x = 0; x < 32; ++x)
      {
        const unsigned row = (y + ((column % 2) * 2)) % 4;
        const unsigned shift = (x / 8) * 8 + (y / 2) * 4;
        expected[words[row][x % 8]] |=
          ((x + 3 * (column * 4 + y) + 1) % 16) << shift;
      }
    }
    for (unsigned word = 0; word < 16; ++word)
    {
      CAPTURE(column);
      CAPTURE(word);
      REQUIRE(gs.localMemoryWord(column * 16 + word) == expected[word]);
    }
  }
  REQUIRE(gs.imageTransfer().transferredPixels == 512);
  REQUIRE_FALSE(gs.imageTransfer().active);
}

TEST_CASE("GS PSMT4 uploads preserve other nibbles and ignore the final tail", "[gs4]")
{
  GS gs;
  gs.writeDisplayPSMCT32(0, 1, 0, 0, 0xa55ac33c);
  gs.writeRegister(GSRegisterAddress::BITBLTBUF, transferBuffer(0, 2, 0x14));
  gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(8, 1));
  gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
  gs.writeRegister(GSRegisterAddress::HWREG, UINT64_C(0xfffffff087654321));
  REQUIRE(gs.localMemoryWord(0) == 0xa55ac331);
  REQUIRE(gs.localMemoryWord(1) == 2);
  REQUIRE(gs.localMemoryWord(4) == 3);
  REQUIRE(gs.localMemoryWord(13) == 8);
  REQUIRE(gs.localMemoryWord(16) == 0);
  REQUIRE(gs.imageTransfer().transferredPixels == 8);
  REQUIRE_FALSE(gs.imageTransfer().active);
  gs.writeRegister(GSRegisterAddress::HWREG, UINT64_MAX);
  REQUIRE(gs.localMemoryWord(0) == 0xa55ac331);

  gs.writeRegister(GSRegisterAddress::TRXPOS, transferPosition(8, 0));
  gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
  gs.writeRegister(GSRegisterAddress::HWREG, UINT64_C(0x111111119abcdef0));
  REQUIRE(gs.localMemoryWord(0) == 0xa55ac031);
  REQUIRE(gs.localMemoryWord(1) == 0x00000f02);
}

TEST_CASE("GS PSMT4 page block base and coordinate wrapping follow manual anchors", "[gs4]")
{
  struct Anchor
  {
    std::uint16_t x;
    std::uint16_t y;
    std::uint16_t base;
    std::uint8_t stride;
    std::size_t word;
    unsigned shift;
  };
  for (const auto anchor : {
         Anchor{0, 0, 0, 2, 0, 0},
         Anchor{4, 0, 0, 2, 8, 0},
         Anchor{8, 0, 0, 2, 0, 8},
         Anchor{0, 2, 0, 2, 8, 4},
         Anchor{0, 4, 0, 2, 24, 0},
         Anchor{0, 8, 0, 2, 32, 0},
         Anchor{32, 0, 0, 2, 128, 0},
         Anchor{0, 16, 0, 2, 64, 0},
         Anchor{64, 0, 0, 2, 512, 0},
         Anchor{0, 32, 0, 2, 256, 0},
         Anchor{0, 64, 0, 2, 1024, 0},
         Anchor{128, 0, 0, 4, 2048, 0},
         Anchor{0, 128, 0, 4, 4096, 0},
         Anchor{0, 0, 1, 2, 64, 0},
         Anchor{32, 0, 0x3fff, 2, 64, 0},
         Anchor{2044, 2047, 0, 32, 0x7fffa, 28}})
  {
    CAPTURE(anchor.x);
    CAPTURE(anchor.y);
    CAPTURE(anchor.base);
    CAPTURE(anchor.stride);
    GS gs;
    gs.writeRegister(GSRegisterAddress::BITBLTBUF,
      transferBuffer(anchor.base, anchor.stride, 0x14));
    gs.writeRegister(GSRegisterAddress::TRXPOS, transferPosition(anchor.x, anchor.y));
    gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(8, 1));
    gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
    gs.writeRegister(GSRegisterAddress::HWREG, 1);
    REQUIRE(gs.localMemoryWord(anchor.word) == (UINT32_C(1) << anchor.shift));
  }
  GS gs;
  gs.writeRegister(GSRegisterAddress::BITBLTBUF, transferBuffer(0, 32, 0x14));
  gs.writeRegister(GSRegisterAddress::TRXPOS, transferPosition(2044, 2047));
  gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(8, 2));
  gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
  gs.writeRegister(GSRegisterAddress::HWREG, UINT64_C(0x0005000000050000));
  REQUIRE(gs.localMemoryWord(0) == 5);
  REQUIRE_FALSE(gs.imageTransfer().active);
}

TEST_CASE("GS PSMT4 admission restart and pending registers remain bounded", "[gs4]")
{
  GS gs;
  gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(8, 1));
  for (const std::uint8_t stride : {0, 1, 3, 33, 63})
  {
    gs.writeRegister(GSRegisterAddress::BITBLTBUF, transferBuffer(0, stride, 0x14));
    REQUIRE_THROWS_WITH(gs.writeRegister(GSRegisterAddress::TRXDIR, 0),
      "GS PSMT4 transfer requires an even destination width from 2 to 32.");
    REQUIRE_FALSE(gs.imageTransfer().active);
  }
  gs.writeRegister(GSRegisterAddress::BITBLTBUF, transferBuffer(0, 2, 0x14));
  gs.writeRegister(GSRegisterAddress::TRXPOS, transferPosition(2, 0));
  REQUIRE_THROWS_WITH(gs.writeRegister(GSRegisterAddress::TRXDIR, 0),
    "GS PSMT4 transfer requires aligned destination X and a width multiple of eight.");
  gs.writeRegister(GSRegisterAddress::TRXPOS, 0);
  gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(7, 1));
  REQUIRE_THROWS_WITH(gs.writeRegister(GSRegisterAddress::TRXDIR, 0),
    "GS PSMT4 transfer requires aligned destination X and a width multiple of eight.");
  gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(8, 0));
  gs.writeRegister(GSRegisterAddress::BITBLTBUF, transferBuffer(0, 63, 0x14));
  REQUIRE_NOTHROW(gs.writeRegister(GSRegisterAddress::TRXDIR, 0));
  REQUIRE_FALSE(gs.imageTransfer().active);
  gs.writeRegister(GSRegisterAddress::BITBLTBUF, transferBuffer(0, 2, 0x14));
  gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(32, 1));
  gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
  gs.writeRegister(GSRegisterAddress::HWREG, UINT64_MAX);
  gs.writeRegister(GSRegisterAddress::TRXPOS, transferPosition(8, 0));
  gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(8, 1));
  REQUIRE(gs.imageTransfer().width == 32);
  REQUIRE(gs.imageTransfer().destinationX == 0);
  REQUIRE(gs.imageTransfer().transferredPixels == 16);
  gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
  gs.writeRegister(GSRegisterAddress::HWREG, 0);
  REQUIRE(gs.localMemoryWord(0) == 15);
  REQUIRE(gs.imageTransfer().transferredPixels == 8);
  gs.writeRegister(GSRegisterAddress::BITBLTBUF, sourceTransferBuffer(0, 2, 0x14));
  REQUIRE_THROWS_WITH(gs.writeRegister(GSRegisterAddress::TRXDIR, 1),
    "GS local-to-host transfer requires PSMCT32.");
}

TEST_CASE("GS transfer parameters latch only on TRXDIR", "[gs16]")
{
  GS gs;
  gs.writeRegister(
    GSRegisterAddress::BITBLTBUF,
    transferBuffer(0, 1, 0) | sourceTransferBuffer(0, 1, 0));
  gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(4, 1));
  gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
  gs.writeRegister(GSRegisterAddress::HWREG, UINT64_C(0x2222222211111111));

  SECTION("Pending destination state does not redirect an active upload")
  {
    gs.writeRegister(GSRegisterAddress::BITBLTBUF, transferBuffer(32, 2, 0));
    gs.writeRegister(GSRegisterAddress::TRXPOS, transferPosition(8, 2));
    gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(2, 1));
    REQUIRE(gs.imageTransfer().destinationBasePointer == 0);
    REQUIRE(gs.imageTransfer().width == 4);
    REQUIRE(gs.imageTransfer().transferredPixels == 2);
    gs.writeRegister(GSRegisterAddress::HWREG, UINT64_C(0x4444444433333333));
    REQUIRE(gs.localMemoryWord(4) == 0x33333333);
    REQUIRE(gs.localMemoryWord(5) == 0x44444444);
    REQUIRE_FALSE(gs.imageTransfer().active);
    REQUIRE(gs.imageTransfer().transferredPixels == 4);
    gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
    REQUIRE(gs.imageTransfer().destinationBasePointer == 32);
    REQUIRE(gs.imageTransfer().destinationX == 8);
    REQUIRE(gs.imageTransfer().width == 2);
    REQUIRE(gs.imageTransfer().transferredPixels == 0);
  }

  SECTION("Pending source state does not redirect active readback")
  {
    gs.writeRegister(GSRegisterAddress::HWREG, UINT64_C(0x4444444433333333));
    gs.writeRegister(GSRegisterAddress::TRXDIR, 1);
    gs.writePrivilegedRegister(GSPrivilegedRegisterAddress::BUSDIR, 1);
    REQUIRE(gs.readHostInterface() == UINT64_C(0x2222222211111111));
    gs.writePrivilegedRegister(GSPrivilegedRegisterAddress::BUSDIR, 0);
    gs.writeRegister(GSRegisterAddress::BITBLTBUF, sourceTransferBuffer(32, 2, 0));
    gs.writeRegister(GSRegisterAddress::TRXPOS, sourceTransferPosition(8, 2));
    gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(2, 1));
    REQUIRE(gs.imageTransfer().sourceBasePointer == 0);
    REQUIRE(gs.imageTransfer().width == 4);
    gs.writePrivilegedRegister(GSPrivilegedRegisterAddress::BUSDIR, 1);
    REQUIRE(gs.readHostInterface() == UINT64_C(0x4444444433333333));
    REQUIRE_FALSE(gs.imageTransfer().active);
  }
}

TEST_CASE("GS PSMCT16 uploads preserve raw neighboring halves", "[gs16]")
{
  GS gs;
  gs.writeDisplayPSMCT32(0, 1, 0, 0, 0xa55ac33c);
  gs.writeRegister(GSRegisterAddress::BITBLTBUF, transferBuffer(0, 1, 2));
  gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(4, 1));
  gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
  gs.writeRegister(GSRegisterAddress::HWREG, UINT64_C(0xffff800000010002));
  REQUIRE(gs.localMemoryWord(0) == 0xa55a0002);
  REQUIRE(gs.localMemoryWord(1) == 0x00000001);
  REQUIRE(gs.localMemoryWord(4) == 0x00008000);
  REQUIRE(gs.localMemoryWord(5) == 0x0000ffff);
  REQUIRE(gs.imageTransfer().transferredPixels == 4);
  REQUIRE_FALSE(gs.imageTransfer().active);

  gs.writeRegister(GSRegisterAddress::TRXPOS, transferPosition(8, 0));
  gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
  gs.writeRegister(GSRegisterAddress::HWREG, UINT64_C(0x00018000ffff9234));
  REQUIRE(gs.localMemoryWord(0) == 0x92340002);
  REQUIRE(gs.localMemoryWord(1) == 0xffff0001);
  REQUIRE(gs.localMemoryWord(4) == 0x80008000);
  REQUIRE(gs.localMemoryWord(5) == 0x0001ffff);
  gs.writeRegister(GSRegisterAddress::HWREG, UINT64_C(0xdeadbeefdeadbeef));
  REQUIRE(gs.localMemoryWord(0) == 0x92340002);
  REQUIRE(gs.imageTransfer().transferredPixels == 4);
}

TEST_CASE("GS PSMCT16 mapping follows manual word and halfword anchors", "[gs16]")
{
  struct Anchor
  {
    std::uint16_t x;
    std::uint16_t y;
    std::uint16_t base;
    std::uint8_t stride;
    std::size_t word;
    unsigned shift;
  };
  for (const auto anchor : {
         Anchor{0, 0, 0, 1, 0, 0},
         Anchor{1, 0, 0, 1, 1, 0},
         Anchor{2, 0, 0, 1, 4, 0},
         Anchor{7, 0, 0, 1, 13, 0},
         Anchor{8, 0, 0, 1, 0, 16},
         Anchor{0, 1, 0, 1, 2, 0},
         Anchor{0, 2, 0, 1, 16, 0},
         Anchor{0, 8, 0, 1, 64, 0},
         Anchor{16, 0, 0, 1, 128, 0},
         Anchor{0, 16, 0, 1, 256, 0},
         Anchor{32, 0, 0, 1, 512, 0},
         Anchor{0, 32, 0, 1, 1024, 0},
         Anchor{64, 0, 0, 2, 2048, 0},
         Anchor{0, 64, 0, 2, 4096, 0},
         Anchor{0, 0, 1, 1, 64, 0},
         Anchor{16, 0, 0x3fff, 1, 64, 0}})
  {
    CAPTURE(anchor.x);
    CAPTURE(anchor.y);
    CAPTURE(anchor.base);
    GS gs;
    gs.writeRegister(
      GSRegisterAddress::BITBLTBUF, transferBuffer(anchor.base, anchor.stride, 2));
    gs.writeRegister(GSRegisterAddress::TRXPOS, transferPosition(anchor.x, anchor.y));
    gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(4, 1));
    gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
    gs.writeRegister(GSRegisterAddress::HWREG, UINT64_C(0xddddccccbbbbaaaa));
    REQUIRE(gs.localMemoryWord(anchor.word) == (UINT32_C(0xaaaa) << anchor.shift));
  }
}

TEST_CASE("GS PSMCT16 coordinates and VRAM addresses wrap independently", "[gs16]")
{
  GS gs;
  gs.writeRegister(GSRegisterAddress::BITBLTBUF, transferBuffer(0, 32, 2));
  gs.writeRegister(GSRegisterAddress::TRXPOS, transferPosition(2046, 2047));
  gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(4, 2));
  gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
  gs.writeRegister(GSRegisterAddress::HWREG, UINT64_C(0x0004000300020001));
  gs.writeRegister(GSRegisterAddress::HWREG, UINT64_C(0x0008000700060005));
  REQUIRE(gs.localMemoryWord(0xffffe) == 0x00010000);
  REQUIRE(gs.localMemoryWord(0xfffff) == 0x00020000);
  REQUIRE(gs.localMemoryWord(0xf0572) == 3);
  REQUIRE(gs.localMemoryWord(0xf0573) == 4);
  REQUIRE(gs.localMemoryWord(0xfa8c) == 0x00050000);
  REQUIRE(gs.localMemoryWord(0xfa8d) == 0x00060000);
  REQUIRE(gs.localMemoryWord(0) == 7);
  REQUIRE(gs.localMemoryWord(1) == 8);
  REQUIRE_FALSE(gs.imageTransfer().active);
}

TEST_CASE("GS PSMCT16 admission preserves explicit bounds and empty precedence", "[gs16]")
{
  GS gs;
  gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(4, 1));
  for (const std::uint8_t stride : {0, 33, 63})
  {
    gs.writeRegister(GSRegisterAddress::BITBLTBUF, transferBuffer(0, stride, 2));
    REQUIRE_THROWS_WITH(gs.writeRegister(GSRegisterAddress::TRXDIR, 0),
      "GS PSMCT16 transfer requires a destination width from 1 to 32.");
    REQUIRE_FALSE(gs.imageTransfer().active);
    REQUIRE(gs.imageTransfer().transferredPixels == 0);
  }
  gs.writeRegister(GSRegisterAddress::BITBLTBUF, transferBuffer(0, 1, 2));
  for (const std::uint16_t width : {1, 2, 3, 5, 4095})
  {
    gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(width, 1));
    REQUIRE_THROWS_WITH(gs.writeRegister(GSRegisterAddress::TRXDIR, 0),
      "GS PSMCT16 transfer width must be a multiple of four.");
  }
  gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(4092, 4095));
  REQUIRE_NOTHROW(gs.writeRegister(GSRegisterAddress::TRXDIR, 0));
  REQUIRE(gs.imageTransfer().active);
  for (const auto empty : {transferRegion(0, 3), transferRegion(3, 0), transferRegion(0, 0)})
  {
    gs.writeRegister(GSRegisterAddress::BITBLTBUF, transferBuffer(0, 63, 2));
    gs.writeRegister(GSRegisterAddress::TRXREG, empty);
    REQUIRE_NOTHROW(gs.writeRegister(GSRegisterAddress::TRXDIR, 0));
    REQUIRE_FALSE(gs.imageTransfer().active);
    REQUIRE(gs.imageTransfer().transferredPixels == 0);
    gs.writeRegister(GSRegisterAddress::HWREG, UINT64_MAX);
    REQUIRE(gs.localMemoryWord(0) == 0);
    gs.writeRegister(GSRegisterAddress::BITBLTBUF, sourceTransferBuffer(0, 1, 2));
    REQUIRE_THROWS_WITH(gs.writeRegister(GSRegisterAddress::TRXDIR, 1),
      "GS local-to-host transfer requires PSMCT32.");
  }
  for (const std::uint8_t format : {1, 0x0a, 0x13, 0x30, 0x32, 0x3f})
  {
    gs.writeRegister(GSRegisterAddress::BITBLTBUF, transferBuffer(0, 1, format));
    REQUIRE_THROWS_WITH(gs.writeRegister(GSRegisterAddress::TRXDIR, 0),
      "GS host-to-local transfer requires PSMCT32, PSMCT16 or PSMT4.");
  }
  gs.writeRegister(GSRegisterAddress::BITBLTBUF, UINT64_MAX);
  gs.writeRegister(GSRegisterAddress::TRXPOS, UINT64_MAX);
  gs.writeRegister(GSRegisterAddress::TRXREG, UINT64_MAX);
  REQUIRE_NOTHROW(gs.writeRegister(GSRegisterAddress::TRXDIR, UINT64_MAX));
  REQUIRE(gs.imageTransfer().destinationBasePointer == 0x3fff);
  REQUIRE(gs.imageTransfer().destinationX == 0x7ff);
  REQUIRE(gs.imageTransfer().width == 0xfff);
  REQUIRE(gs.imageTransfer().destinationPixelStorageMode == 0x3f);
  REQUIRE_FALSE(gs.imageTransfer().active);
}

TEST_CASE("GS PSMCT16 restart and rejection retain only committed pixels", "[gs16]")
{
  GS gs;
  gs.writeRegister(GSRegisterAddress::BITBLTBUF, transferBuffer(0, 1, 2));
  gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(8, 1));
  gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
  gs.writeRegister(GSRegisterAddress::HWREG, UINT64_C(0x0004000300020001));
  gs.writeRegister(GSRegisterAddress::TRXPOS, transferPosition(8, 0));
  gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(4, 1));
  REQUIRE(gs.imageTransfer().transferredPixels == 4);
  REQUIRE(gs.imageTransfer().width == 8);
  gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
  REQUIRE(gs.imageTransfer().transferredPixels == 0);
  gs.writeRegister(GSRegisterAddress::HWREG, UINT64_C(0x0008000700060005));
  REQUIRE(gs.localMemoryWord(0) == 0x00050001);
  REQUIRE(gs.localMemoryWord(5) == 0x00080004);

  gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
  gs.writeRegister(GSRegisterAddress::BITBLTBUF, transferBuffer(0, 33, 2));
  REQUIRE_THROWS_WITH(gs.writeRegister(GSRegisterAddress::TRXDIR, 0),
    "GS PSMCT16 transfer requires a destination width from 1 to 32.");
  REQUIRE_FALSE(gs.imageTransfer().active);
  REQUIRE(gs.imageTransfer().transferredPixels == 0);
  REQUIRE(gs.imageTransfer().destinationBufferWidth == 33);
  gs.writeRegister(GSRegisterAddress::HWREG, UINT64_MAX);
  REQUIRE(gs.localMemoryWord(0) == 0x00050001);
}

TEST_CASE("GS PSMCT32 admission retains wide strides and odd rectangles", "[gs16]")
{
  GS gs;
  gs.writeRegister(GSRegisterAddress::BITBLTBUF, transferBuffer(0, 63, 0));
  gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(3, 1));
  gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
  gs.writeRegister(GSRegisterAddress::HWREG, UINT64_C(0x0000000200000001));
  gs.writeRegister(GSRegisterAddress::HWREG, UINT64_C(0xdeadbeef00000003));
  REQUIRE(gs.localMemoryWord(4) == 3);
  REQUIRE_FALSE(gs.imageTransfer().active);
  gs.writeRegister(GSRegisterAddress::BITBLTBUF, sourceTransferBuffer(0, 63, 0));
  gs.writeRegister(GSRegisterAddress::TRXDIR, 1);
  gs.writePrivilegedRegister(GSPrivilegedRegisterAddress::BUSDIR, 1);
  REQUIRE(gs.readHostInterface() == UINT64_C(0x0000000200000001));
  REQUIRE(gs.readHostInterface() == 3);
  REQUIRE_FALSE(gs.imageTransfer().active);
}

TEST_CASE("GS Host-to-Local Image Transfer Tests")
{
  SECTION("Transfer registers decode destination state")
  {
    GS gs;
    gs.writeRegister(
      GSRegisterAddress::BITBLTBUF,
      transferBuffer(0x1234, 17, GSPixelStorageMode::PSMCT32));
    gs.writeRegister(
      GSRegisterAddress::TRXPOS,
      transferPosition(0x345, 0x678));
    gs.writeRegister(
      GSRegisterAddress::TRXREG,
      transferRegion(13, 29));
    gs.writeRegister(GSRegisterAddress::TRXDIR, 0);

    const GSImageTransfer &transfer = gs.imageTransfer();
    REQUIRE(transfer.destinationBasePointer == 0x1234);
    REQUIRE(transfer.destinationBufferWidth == 17);
    REQUIRE(
      transfer.destinationPixelStorageMode ==
      GSPixelStorageMode::PSMCT32);
    REQUIRE(transfer.destinationX == 0x345);
    REQUIRE(transfer.destinationY == 0x678);
    REQUIRE(transfer.width == 13);
    REQUIRE(transfer.height == 29);
    REQUIRE(transfer.transferredPixels == 0);
    REQUIRE(transfer.active);
  }

  SECTION("HWREG writes PSMCT32 pixels left-to-right and top-to-bottom")
  {
    GS gs;
    gs.writeRegister(
      GSRegisterAddress::BITBLTBUF,
      transferBuffer(32, 2, GSPixelStorageMode::PSMCT32));
    gs.writeRegister(
      GSRegisterAddress::TRXPOS,
      transferPosition(3, 4));
    gs.writeRegister(
      GSRegisterAddress::TRXREG,
      transferRegion(3, 2));
    gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
    gs.writeRegister(
      GSRegisterAddress::HWREG,
      UINT64_C(0x2222222211111111));
    gs.writeRegister(
      GSRegisterAddress::HWREG,
      UINT64_C(0x4444444433333333));
    gs.writeRegister(
      GSRegisterAddress::HWREG,
      UINT64_C(0x6666666655555555));

    GS frameView;
    frameView.writeRegister(
      GSRegisterAddress::FRAME_1,
      1 | (UINT64_C(2) << 16));
    for (std::uint16_t index = 0; index < 6; ++index)
    {
      const std::uint16_t x = 3 + (index % 3);
      const std::uint16_t y = 4 + (index / 3);
      const std::size_t address =
        frameView.psmct32WordAddress(0, x, y);
      REQUIRE(
        gs.localMemoryWord(address) ==
        static_cast<std::uint32_t>(
          UINT32_C(0x11111111) * (index + 1)));
    }
    REQUIRE(!gs.imageTransfer().active);
    REQUIRE(gs.imageTransfer().transferredPixels == 6);
  }

  SECTION("Odd pixel counts ignore the unused upper HWREG word")
  {
    GS gs;
    gs.writeRegister(
      GSRegisterAddress::BITBLTBUF,
      transferBuffer(0, 1, GSPixelStorageMode::PSMCT32));
    gs.writeRegister(
      GSRegisterAddress::TRXREG,
      transferRegion(3, 1));
    gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
    gs.writeRegister(
      GSRegisterAddress::HWREG,
      UINT64_C(0x2222222211111111));
    gs.writeRegister(
      GSRegisterAddress::HWREG,
      UINT64_C(0xdeadbeef33333333));

    REQUIRE(gs.localMemoryWord(0) == 0x11111111);
    REQUIRE(gs.localMemoryWord(1) == 0x22222222);
    REQUIRE(gs.localMemoryWord(4) == 0x33333333);
    REQUIRE(gs.imageTransfer().transferredPixels == 3);
    REQUIRE(!gs.imageTransfer().active);
  }

  SECTION("TRXDIR restarts an incomplete transfer")
  {
    GS gs;
    gs.writeRegister(
      GSRegisterAddress::BITBLTBUF,
      transferBuffer(0, 1, GSPixelStorageMode::PSMCT32));
    gs.writeRegister(
      GSRegisterAddress::TRXREG,
      transferRegion(4, 1));
    gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
    gs.writeRegister(
      GSRegisterAddress::HWREG,
      UINT64_C(0x2222222211111111));
    gs.writeRegister(
      GSRegisterAddress::TRXPOS,
      transferPosition(8, 0));
    gs.writeRegister(
      GSRegisterAddress::TRXREG,
      transferRegion(2, 1));
    gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
    gs.writeRegister(
      GSRegisterAddress::HWREG,
      UINT64_C(0x4444444433333333));

    REQUIRE(gs.localMemoryWord(0) == 0x11111111);
    REQUIRE(gs.localMemoryWord(1) == 0x22222222);
    REQUIRE(gs.localMemoryWord(64) == 0x33333333);
    REQUIRE(gs.localMemoryWord(65) == 0x44444444);
    REQUIRE(gs.imageTransfer().transferredPixels == 2);
    REQUIRE(!gs.imageTransfer().active);
  }

  SECTION("Destination coordinates wrap at 2048")
  {
    GS gs;
    gs.writeRegister(
      GSRegisterAddress::BITBLTBUF,
      transferBuffer(0, 1, GSPixelStorageMode::PSMCT32));
    gs.writeRegister(
      GSRegisterAddress::TRXPOS,
      transferPosition(2047, 2047));
    gs.writeRegister(
      GSRegisterAddress::TRXREG,
      transferRegion(2, 2));
    gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
    gs.writeRegister(
      GSRegisterAddress::HWREG,
      UINT64_C(0x2222222211111111));
    gs.writeRegister(
      GSRegisterAddress::HWREG,
      UINT64_C(0x4444444433333333));

    GS addressView;
    addressView.writeRegister(
      GSRegisterAddress::FRAME_1,
      UINT64_C(1) << 16);
    REQUIRE(
      gs.localMemoryWord(
        addressView.psmct32WordAddress(0, 2047, 2047)) ==
      0x11111111);
    REQUIRE(
      gs.localMemoryWord(
        addressView.psmct32WordAddress(0, 0, 2047)) ==
      0x22222222);
    REQUIRE(
      gs.localMemoryWord(
        addressView.psmct32WordAddress(0, 2047, 0)) ==
      0x33333333);
    REQUIRE(gs.localMemoryWord(0) == 0x44444444);
  }

  SECTION("Invalid or unsupported transfers fail explicitly")
  {
    GS gs;
    gs.writeRegister(
      GSRegisterAddress::TRXREG,
      transferRegion(1, 1));
    REQUIRE_THROWS_WITH(
      gs.writeRegister(GSRegisterAddress::TRXDIR, 0),
      "GS image transfer requires a valid destination width.");

    gs.writeRegister(
      GSRegisterAddress::BITBLTBUF,
      transferBuffer(0, 1, 1));
    REQUIRE_THROWS_WITH(
      gs.writeRegister(GSRegisterAddress::TRXDIR, 0),
      "GS host-to-local transfer requires PSMCT32, PSMCT16 or PSMT4.");
    REQUIRE_THROWS_WITH(
      gs.writeRegister(GSRegisterAddress::TRXDIR, 2),
      "GS image transfer direction is not implemented.");

    gs.writeRegister(GSRegisterAddress::TRXDIR, 3);
    REQUIRE(!gs.imageTransfer().active);
  }
}

TEST_CASE("GS PSMCT32 Local-to-Host Transfer Tests")
{
  SECTION("BUSDIR gates packed local-memory readback")
  {
    GS gs;
    gs.writeRegister(
      GSRegisterAddress::FRAME_1,
      1 | (UINT64_C(2) << 16));
    const std::uint32_t pixels[] = {
      0x11111111,
      0x22222222,
      0x33333333,
      0x44444444,
      0x55555555,
      0x66666666
    };
    for (std::uint16_t index = 0; index < 6; ++index)
    {
      gs.writePSMCT32(
        0,
        3 + (index % 3),
        4 + (index / 3),
        pixels[index]);
    }

    gs.writeRegister(
      GSRegisterAddress::BITBLTBUF,
      sourceTransferBuffer(
        32,
        2,
        GSPixelStorageMode::PSMCT32));
    gs.writeRegister(
      GSRegisterAddress::TRXPOS,
      sourceTransferPosition(3, 4));
    gs.writeRegister(
      GSRegisterAddress::TRXREG,
      transferRegion(3, 2));
    gs.writeRegister(GSRegisterAddress::TRXDIR, 1);

    REQUIRE(
      gs.imageTransfer().direction ==
      GSImageTransferDirection::LocalToHost);
    REQUIRE(gs.imageTransfer().active);
    REQUIRE_THROWS_WITH(
      gs.readHostInterface(),
      "GS host-interface reads require BUSDIR local-to-host.");

    gs.writePrivilegedRegister(
      GSPrivilegedRegisterAddress::BUSDIR,
      1);
    REQUIRE(gs.hostInterfaceReversed());
    REQUIRE_THROWS_WITH(
      gs.writeRegister(GSRegisterAddress::PRIM, 0),
      "GS general registers are unavailable while BUSDIR is reversed.");
    REQUIRE(
      gs.readHostInterface() ==
      UINT64_C(0x2222222211111111));
    REQUIRE(
      gs.readHostInterface() ==
      UINT64_C(0x4444444433333333));
    REQUIRE(
      gs.readHostInterface() ==
      UINT64_C(0x6666666655555555));
    REQUIRE(!gs.imageTransfer().active);
    REQUIRE(gs.imageTransfer().transferredPixels == 6);
    REQUIRE_THROWS_WITH(
      gs.readHostInterface(),
      "GS local-to-host transfer is not active.");

    gs.writePrivilegedRegister(
      GSPrivilegedRegisterAddress::BUSDIR,
      0);
    REQUIRE(!gs.hostInterfaceReversed());
    gs.writeRegister(GSRegisterAddress::PRIM, 3);
    REQUIRE(gs.primitive().type == GSPrimitiveType::Triangle);
  }

  SECTION("Odd readback pads the unused upper word with zero")
  {
    GS gs;
    gs.writeRegister(
      GSRegisterAddress::FRAME_1,
      UINT64_C(1) << 16);
    gs.writePSMCT32(0, 0, 0, 0x11111111);
    gs.writePSMCT32(0, 1, 0, 0x22222222);
    gs.writePSMCT32(0, 2, 0, 0x33333333);
    gs.writeRegister(
      GSRegisterAddress::BITBLTBUF,
      sourceTransferBuffer(
        0,
        1,
        GSPixelStorageMode::PSMCT32));
    gs.writeRegister(
      GSRegisterAddress::TRXREG,
      transferRegion(3, 1));
    gs.writeRegister(GSRegisterAddress::TRXDIR, 1);
    gs.writePrivilegedRegister(
      GSPrivilegedRegisterAddress::BUSDIR,
      1);

    REQUIRE(
      gs.readHostInterface() ==
      UINT64_C(0x2222222211111111));
    REQUIRE(gs.readHostInterface() == UINT64_C(0x33333333));
    REQUIRE(!gs.imageTransfer().active);
  }

  SECTION("A PSMCT32 upload can be read back exactly")
  {
    GS gs;
    gs.writeRegister(
      GSRegisterAddress::BITBLTBUF,
      transferBuffer(0, 1, GSPixelStorageMode::PSMCT32));
    gs.writeRegister(
      GSRegisterAddress::TRXPOS,
      transferPosition(5, 6));
    gs.writeRegister(
      GSRegisterAddress::TRXREG,
      transferRegion(2, 2));
    gs.writeRegister(GSRegisterAddress::TRXDIR, 0);
    gs.writeRegister(
      GSRegisterAddress::HWREG,
      UINT64_C(0x0123456789abcdef));
    gs.writeRegister(
      GSRegisterAddress::HWREG,
      UINT64_C(0xfedcba9876543210));

    gs.writeRegister(
      GSRegisterAddress::BITBLTBUF,
      sourceTransferBuffer(
        0,
        1,
        GSPixelStorageMode::PSMCT32));
    gs.writeRegister(
      GSRegisterAddress::TRXPOS,
      sourceTransferPosition(5, 6));
    gs.writeRegister(GSRegisterAddress::TRXDIR, 1);
    gs.writePrivilegedRegister(
      GSPrivilegedRegisterAddress::BUSDIR,
      1);

    REQUIRE(
      gs.readHostInterface() ==
      UINT64_C(0x0123456789abcdef));
    REQUIRE(
      gs.readHostInterface() ==
      UINT64_C(0xfedcba9876543210));
  }

  SECTION("Readback validates source configuration and privileged ports")
  {
    GS gs;
    gs.writeRegister(
      GSRegisterAddress::TRXREG,
      transferRegion(1, 1));
    REQUIRE_THROWS_WITH(
      gs.writeRegister(GSRegisterAddress::TRXDIR, 1),
      "GS image transfer requires a valid source width.");

    gs.writeRegister(
      GSRegisterAddress::BITBLTBUF,
      sourceTransferBuffer(0, 1, 1));
    REQUIRE_THROWS_WITH(
      gs.writeRegister(GSRegisterAddress::TRXDIR, 1),
      "GS local-to-host transfer requires PSMCT32.");
    REQUIRE_THROWS_WITH(
      gs.writePrivilegedRegister(0, 0),
      "GS privileged register is not implemented.");
  }
}

TEST_CASE("GS empty PSMCT32 transfers remain inactive")
{
  for (const std::uint64_t direction : {0, 1})
  {
    for (const std::uint8_t bufferWidth : {0, 1})
    {
      for (const auto dimensions : {
             transferRegion(0, 3),
             transferRegion(3, 0),
             transferRegion(0, 0)})
      {
        CAPTURE(direction);
        CAPTURE(bufferWidth);
        CAPTURE(dimensions);
        GS gs;
        gs.writeRegister(
          GSRegisterAddress::BITBLTBUF,
          transferBuffer(0, 1, GSPixelStorageMode::PSMCT32) |
          sourceTransferBuffer(0, 1, GSPixelStorageMode::PSMCT32));
        gs.writeRegister(GSRegisterAddress::TRXREG, transferRegion(4, 1));
        gs.writeRegister(GSRegisterAddress::TRXDIR, direction);
        if (direction == 0)
        {
          gs.writeRegister(
            GSRegisterAddress::HWREG,
            UINT64_C(0x2222222211111111));
        }
        else
        {
          gs.writePrivilegedRegister(GSPrivilegedRegisterAddress::BUSDIR, 1);
          gs.readHostInterface();
          gs.writePrivilegedRegister(GSPrivilegedRegisterAddress::BUSDIR, 0);
        }
        REQUIRE(gs.imageTransfer().active);
        REQUIRE(gs.imageTransfer().transferredPixels == 2);

        const auto firstPixel = gs.localMemoryWord(0);
        const auto secondPixel = gs.localMemoryWord(1);
        const auto buffer =
          transferBuffer(0, bufferWidth, GSPixelStorageMode::PSMCT32) |
          sourceTransferBuffer(0, bufferWidth, GSPixelStorageMode::PSMCT32);
        gs.writeRegister(GSRegisterAddress::BITBLTBUF, buffer);
        gs.writeRegister(GSRegisterAddress::TRXREG, dimensions);
        REQUIRE_NOTHROW(gs.writeRegister(GSRegisterAddress::TRXDIR, direction));
        REQUIRE(!gs.imageTransfer().active);
        REQUIRE(gs.imageTransfer().transferredPixels == 0);
        REQUIRE(
          gs.imageTransfer().direction ==
          static_cast<GSImageTransferDirection>(direction));
        REQUIRE(gs.registerValue(GSRegisterAddress::BITBLTBUF) == buffer);
        REQUIRE(gs.registerValue(GSRegisterAddress::TRXREG) == dimensions);
        REQUIRE(gs.registerValue(GSRegisterAddress::TRXDIR) == direction);

        gs.writeRegister(GSRegisterAddress::HWREG, UINT64_C(0xdeadbeefdeadbeef));
        REQUIRE(gs.localMemoryWord(0) == firstPixel);
        REQUIRE(gs.localMemoryWord(1) == secondPixel);
        REQUIRE(gs.localMemoryWord(4) == 0);
        REQUIRE_THROWS_WITH(
          gs.readHostInterface(),
          "GS host-interface reads require BUSDIR local-to-host.");
        gs.writePrivilegedRegister(GSPrivilegedRegisterAddress::BUSDIR, 1);
        REQUIRE_THROWS_WITH(
          gs.readHostInterface(),
          "GS local-to-host transfer is not active.");
        REQUIRE(gs.imageTransfer().transferredPixels == 0);
      }
    }
  }
}

TEST_CASE("GS empty transfers preserve format and direction rejection")
{
  for (const auto dimensions : {
         transferRegion(0, 3),
         transferRegion(3, 0),
         transferRegion(0, 0)})
  {
    CAPTURE(dimensions);
    GS gs;
    gs.writeRegister(GSRegisterAddress::TRXREG, dimensions);
    gs.writeRegister(
      GSRegisterAddress::BITBLTBUF,
      transferBuffer(0, 0, 1) | sourceTransferBuffer(0, 0, 1));
    REQUIRE_THROWS_WITH(
      gs.writeRegister(GSRegisterAddress::TRXDIR, 0),
      "GS host-to-local transfer requires PSMCT32, PSMCT16 or PSMT4.");
    REQUIRE_THROWS_WITH(
      gs.writeRegister(GSRegisterAddress::TRXDIR, 1),
      "GS local-to-host transfer requires PSMCT32.");
    gs.writeRegister(GSRegisterAddress::BITBLTBUF, 0);
    REQUIRE_THROWS_WITH(
      gs.writeRegister(GSRegisterAddress::TRXDIR, 2),
      "GS image transfer direction is not implemented.");
    REQUIRE_NOTHROW(gs.writeRegister(GSRegisterAddress::TRXDIR, 3));
    REQUIRE(!gs.imageTransfer().active);
    REQUIRE(gs.imageTransfer().transferredPixels == 0);
  }
}

TEST_CASE("PATH3 IMAGE to GS Transfer Integration Tests")
{
  SECTION("PSMCT16 final IMAGE padding does not write past the rectangle")
  {
    GS gs;
    GIFDecoder decoder;
    decoder.attachRegisterWriteHandler(&gs);
    GIFPath3Transfer path3(&decoder);
    const std::vector<GIFQuadword> packet = {
      gifTag(4, GIFDataFormat::Packed, GIFRegisterDescriptor::AD),
      adWrite(GSRegisterAddress::BITBLTBUF, transferBuffer(0, 1, 2)),
      adWrite(GSRegisterAddress::TRXPOS, 0),
      adWrite(GSRegisterAddress::TRXREG, transferRegion(4, 1)),
      adWrite(GSRegisterAddress::TRXDIR, 0),
      gifTag(1, GIFDataFormat::Image),
      GIFQuadword{{0x00020001, 0x00040003, 0xdeadbeef, 0xdeadbeef}}
    };
    REQUIRE(path3.submitQuadwords(packet.data(), packet.size()).packetComplete);
    REQUIRE(gs.localMemoryWord(0) == 1);
    REQUIRE(gs.localMemoryWord(5) == 4);
    REQUIRE(gs.localMemoryWord(8) == 0);
    REQUIRE(gs.imageTransfer().transferredPixels == 4);
    REQUIRE_FALSE(gs.imageTransfer().active);
    REQUIRE(gs.registerValue(GSRegisterAddress::HWREG) == UINT64_C(0xdeadbeefdeadbeef));
  }

  SECTION("PATH3 configures and uploads a PSMCT32 rectangle")
  {
    GS gs;
    GIFDecoder decoder;
    decoder.attachRegisterWriteHandler(&gs);
    GIFPath3Transfer path3(&decoder);
    const std::vector<GIFQuadword> emptyPacket = {
      gifTag(4, GIFDataFormat::Packed, GIFRegisterDescriptor::AD),
      adWrite(GSRegisterAddress::BITBLTBUF, 0),
      adWrite(GSRegisterAddress::TRXPOS, 0),
      adWrite(GSRegisterAddress::TRXREG, 0),
      adWrite(GSRegisterAddress::TRXDIR, 0)
    };
    const auto emptyResult =
      path3.submitQuadwords(emptyPacket.data(), emptyPacket.size());
    REQUIRE(emptyResult.transferredQuadwords == emptyPacket.size());
    REQUIRE(emptyResult.packetComplete);
    REQUIRE(path3.completedPacketCount() == 1);
    REQUIRE(!decoder.packetInProgress());
    REQUIRE(!gs.imageTransfer().active);
    REQUIRE(gs.imageTransfer().transferredPixels == 0);
    const std::vector<GIFQuadword> packet = {
      gifTag(4, GIFDataFormat::Packed, GIFRegisterDescriptor::AD),
      adWrite(
        GSRegisterAddress::BITBLTBUF,
        transferBuffer(0, 1, GSPixelStorageMode::PSMCT32)),
      adWrite(
        GSRegisterAddress::TRXPOS,
        transferPosition(1, 2)),
      adWrite(
        GSRegisterAddress::TRXREG,
        transferRegion(2, 2)),
      adWrite(GSRegisterAddress::TRXDIR, 0),
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
    REQUIRE(gs.localMemoryWord(17) == 0x01020304);
    REQUIRE(gs.localMemoryWord(20) == 0x11121314);
    REQUIRE(gs.localMemoryWord(19) == 0x21222324);
    REQUIRE(gs.localMemoryWord(22) == 0x31323334);
    REQUIRE(!gs.imageTransfer().active);
  }
}
