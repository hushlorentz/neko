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
      "GS host-to-local transfer requires PSMCT32.");
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

TEST_CASE("PATH3 IMAGE to GS Transfer Integration Tests")
{
  SECTION("PATH3 configures and uploads a PSMCT32 rectangle")
  {
    GS gs;
    GIFDecoder decoder;
    decoder.attachRegisterWriteHandler(&gs);
    GIFPath3Transfer path3(&decoder);
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
