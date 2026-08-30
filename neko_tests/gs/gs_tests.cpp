#include <array>
#include <cstdint>

#include "catch.hpp"
#include "gif.hpp"
#include "gs.hpp"
#include "vif.hpp"

namespace
{
  constexpr std::uint32_t WORDS_PER_QUADWORD = 4;

  std::uint64_t frameValue(
    std::uint16_t basePointer,
    std::uint8_t width,
    std::uint8_t pixelStorageMode,
    std::uint32_t drawingMask = 0)
  {
    return
      basePointer |
      (static_cast<std::uint64_t>(width) << 16) |
      (static_cast<std::uint64_t>(pixelStorageMode) << 24) |
      (static_cast<std::uint64_t>(drawingMask) << 32);
  }

  GIFQuadword gifTag(
    std::uint16_t loopCount,
    bool endOfPacket,
    std::uint8_t registerCount,
    std::uint64_t registers)
  {
    const std::uint64_t low =
      loopCount |
      (static_cast<std::uint64_t>(endOfPacket) << 15) |
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

TEST_CASE("GS Drawing Register Tests")
{
  SECTION("Primitive, color, and vertex registers decode their fields")
  {
    GS gs;
    const std::uint64_t primitive =
      3 |
      (UINT64_C(1) << 3) |
      (UINT64_C(1) << 4) |
      (UINT64_C(1) << 5) |
      (UINT64_C(1) << 6) |
      (UINT64_C(1) << 7) |
      (UINT64_C(1) << 8) |
      (UINT64_C(1) << 9) |
      (UINT64_C(1) << 10);
    gs.writeRegister(GSRegisterAddress::PRIM, primitive);
    gs.writeRegister(
      GSRegisterAddress::RGBAQ,
      UINT64_C(0x1234567887654321));
    gs.writeRegister(
      GSRegisterAddress::XYZ2,
      UINT64_C(0x89abcdef56781234));

    REQUIRE(gs.primitive().type == GSPrimitiveType::Triangle);
    REQUIRE(gs.primitive().gouraudShading);
    REQUIRE(gs.primitive().textureMapping);
    REQUIRE(gs.primitive().fogging);
    REQUIRE(gs.primitive().alphaBlending);
    REQUIRE(gs.primitive().antialiasing);
    REQUIRE(gs.primitive().fixedTextureCoordinates);
    REQUIRE(gs.primitive().context == 1);
    REQUIRE(gs.primitive().fixedFragmentValue);
    REQUIRE(gs.color().red == 0x21);
    REQUIRE(gs.color().green == 0x43);
    REQUIRE(gs.color().blue == 0x65);
    REQUIRE(gs.color().alpha == 0x87);
    REQUIRE(gs.color().q == 0x12345678);
    REQUIRE(gs.vertex().x == 0x1234);
    REQUIRE(gs.vertex().y == 0x5678);
    REQUIRE(gs.vertex().z == 0x89abcdef);
  }

  SECTION("Both drawing contexts decode independently")
  {
    GS gs;
    gs.writeRegister(
      GSRegisterAddress::FRAME_1,
      frameValue(0x101, 10, GSPixelStorageMode::PSMCT32, 0x11223344));
    gs.writeRegister(
      GSRegisterAddress::FRAME_2,
      frameValue(0x055, 20, 0x12, 0xaabbccdd));
    gs.writeRegister(
      GSRegisterAddress::SCISSOR_1,
      UINT64_C(1) |
      (UINT64_C(2) << 16) |
      (UINT64_C(3) << 32) |
      (UINT64_C(4) << 48));
    gs.writeRegister(
      GSRegisterAddress::XYOFFSET_2,
      UINT64_C(0x1234) | (UINT64_C(0x5678) << 32));
    gs.writeRegister(
      GSRegisterAddress::TEST_1,
      UINT64_C(1) |
      (UINT64_C(5) << 1) |
      (UINT64_C(0x7f) << 4) |
      (UINT64_C(2) << 12) |
      (UINT64_C(1) << 14) |
      (UINT64_C(1) << 15) |
      (UINT64_C(1) << 16) |
      (UINT64_C(3) << 17));

    REQUIRE(gs.context(0).frame.basePointer == 0x101);
    REQUIRE(gs.context(0).frame.width == 10);
    REQUIRE(
      gs.context(0).frame.pixelStorageMode ==
      GSPixelStorageMode::PSMCT32);
    REQUIRE(gs.context(0).frame.drawingMask == 0x11223344);
    REQUIRE(gs.context(1).frame.basePointer == 0x055);
    REQUIRE(gs.context(1).frame.width == 20);
    REQUIRE(gs.context(1).frame.pixelStorageMode == 0x12);
    REQUIRE(gs.context(1).frame.drawingMask == 0xaabbccdd);
    REQUIRE(gs.context(0).scissor.x0 == 1);
    REQUIRE(gs.context(0).scissor.x1 == 2);
    REQUIRE(gs.context(0).scissor.y0 == 3);
    REQUIRE(gs.context(0).scissor.y1 == 4);
    REQUIRE(gs.context(1).offset.x == 0x1234);
    REQUIRE(gs.context(1).offset.y == 0x5678);
    REQUIRE(gs.context(0).test.alphaTestEnabled);
    REQUIRE(gs.context(0).test.alphaTest == 5);
    REQUIRE(gs.context(0).test.alphaReference == 0x7f);
    REQUIRE(gs.context(0).test.alphaFail == 2);
    REQUIRE(gs.context(0).test.destinationAlphaTestEnabled);
    REQUIRE(gs.context(0).test.destinationAlphaMode);
    REQUIRE(gs.context(0).test.depthTestEnabled);
    REQUIRE(gs.context(0).test.depthTest == 3);
  }

  SECTION("Raw values remain available for unsupported registers")
  {
    GS gs;
    gs.writeRegister(0x42, UINT64_C(0x0123456789abcdef));

    REQUIRE(
      gs.registerValue(0x42) ==
      UINT64_C(0x0123456789abcdef));
  }

  SECTION("Context indices are checked")
  {
    GS gs;
    REQUIRE_THROWS_WITH(
      gs.context(2),
      "GS context index is outside range.");
  }
}

TEST_CASE("GS PSMCT32 Local Memory Tests")
{
  SECTION("Page, block, column, and word swizzles match the manual")
  {
    GS gs;
    gs.writeRegister(
      GSRegisterAddress::FRAME_1,
      frameValue(2, 2, GSPixelStorageMode::PSMCT32));

    struct AddressContract
    {
      std::uint16_t x;
      std::uint16_t y;
      std::size_t address;
    };
    const std::array<AddressContract, 11> contracts = {{
      {0, 0, 4096},
      {1, 0, 4097},
      {0, 1, 4098},
      {2, 0, 4100},
      {0, 2, 4112},
      {8, 0, 4160},
      {0, 8, 4224},
      {16, 0, 4352},
      {63, 31, 6143},
      {64, 0, 6144},
      {0, 32, 8192}
    }};

    for (const AddressContract &contract : contracts)
    {
      REQUIRE(
        gs.psmct32WordAddress(0, contract.x, contract.y) ==
        contract.address);
    }
  }

  SECTION("Addresses wrap within four MiB of local memory")
  {
    GS gs;
    gs.writeRegister(
      GSRegisterAddress::FRAME_1,
      frameValue(511, 1, GSPixelStorageMode::PSMCT32));

    REQUIRE(gs.localMemoryWordCount() == 1024 * 1024);
    REQUIRE(gs.psmct32WordAddress(0, 64, 0) == 0);
  }

  SECTION("Pixel writes obey the FRAME drawing mask")
  {
    GS gs;
    gs.writeRegister(
      GSRegisterAddress::FRAME_1,
      frameValue(0, 1, GSPixelStorageMode::PSMCT32));
    gs.writePSMCT32(0, 3, 4, 0x11223344);
    gs.writeRegister(
      GSRegisterAddress::FRAME_1,
      frameValue(
        0,
        1,
        GSPixelStorageMode::PSMCT32,
        0xff00ff00));
    gs.writePSMCT32(0, 3, 4, 0xaabbccdd);

    REQUIRE(gs.readPSMCT32(0, 3, 4) == 0x11bb33dd);
    REQUIRE(
      gs.localMemoryWord(gs.psmct32WordAddress(0, 3, 4)) ==
      0x11bb33dd);
  }

  SECTION("Invalid framebuffer configurations are rejected")
  {
    GS gs;
    REQUIRE_THROWS_WITH(
      gs.psmct32WordAddress(0, 0, 0),
      "GS PSMCT32 access requires a valid frame-buffer width.");

    gs.writeRegister(
      GSRegisterAddress::FRAME_1,
      frameValue(0, 1, 1));
    REQUIRE_THROWS_WITH(
      gs.psmct32WordAddress(0, 0, 0),
      "GS frame buffer is not configured for PSMCT32.");
    REQUIRE_THROWS_WITH(
      gs.localMemoryWord(gs.localMemoryWordCount()),
      "GS local-memory word address is outside range.");
  }
}

TEST_CASE("VIF PATH2 to GS Integration Tests")
{
  SECTION("A DIRECT GIF packet configures GS drawing state")
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

    constexpr std::uint16_t WRITE_COUNT = 6;
    vif.ingestWord(vifCode(
      VIFCommandEncoding::DIRECT,
      WRITE_COUNT + 1));
    ingestQuadword(
      &vif,
      gifTag(
        WRITE_COUNT,
        true,
        1,
        GIFRegisterDescriptor::AD));
    ingestQuadword(
      &vif,
      adWrite(
        GSRegisterAddress::FRAME_1,
        frameValue(3, 2, GSPixelStorageMode::PSMCT32)));
    ingestQuadword(
      &vif,
      adWrite(
        GSRegisterAddress::SCISSOR_1,
        UINT64_C(10) |
        (UINT64_C(319) << 16) |
        (UINT64_C(20) << 32) |
        (UINT64_C(239) << 48)));
    ingestQuadword(
      &vif,
      adWrite(
        GSRegisterAddress::XYOFFSET_1,
        UINT64_C(0x1000) | (UINT64_C(0x2000) << 32)));
    ingestQuadword(
      &vif,
      adWrite(GSRegisterAddress::TEST_1, UINT64_C(1) << 16));
    ingestQuadword(
      &vif,
      adWrite(GSRegisterAddress::PRIM, 3));
    ingestQuadword(
      &vif,
      adWrite(
        GSRegisterAddress::RGBAQ,
        UINT64_C(0x3f80000080402010)));

    REQUIRE(gs.context(0).frame.basePointer == 3);
    REQUIRE(gs.context(0).frame.width == 2);
    REQUIRE(gs.context(0).scissor.x0 == 10);
    REQUIRE(gs.context(0).scissor.x1 == 319);
    REQUIRE(gs.context(0).scissor.y0 == 20);
    REQUIRE(gs.context(0).scissor.y1 == 239);
    REQUIRE(gs.context(0).offset.x == 0x1000);
    REQUIRE(gs.context(0).offset.y == 0x2000);
    REQUIRE(gs.context(0).test.depthTestEnabled);
    REQUIRE(gs.primitive().type == GSPrimitiveType::Triangle);
    REQUIRE(gs.color().red == 0x10);
    REQUIRE(gs.color().green == 0x20);
    REQUIRE(gs.color().blue == 0x40);
    REQUIRE(gs.color().alpha == 0x80);
    REQUIRE(gs.color().q == 0x3f800000);
    REQUIRE(!vif.awaitingPayload());
    REQUIRE(!decoder.packetInProgress());
  }

  SECTION("A GIF handler must attach between packets")
  {
    GS gs;
    GIFDecoder decoder;
    REQUIRE_THROWS_WITH(
      decoder.attachRegisterWriteHandler(nullptr),
      "Cannot attach a null GIF register-write handler.");
    decoder.ingestQuadword(gifTag(
      1,
      true,
      1,
      GIFRegisterDescriptor::NOP));
    REQUIRE_THROWS_WITH(
      decoder.attachRegisterWriteHandler(&gs),
      "Cannot attach a GIF register-write handler during a packet.");
  }
}
