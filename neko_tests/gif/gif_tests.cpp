#include <array>
#include <cstdint>

#include "catch.hpp"
#include "gif.hpp"

namespace
{
  GIFQuadword gifTag(
    std::uint16_t loopCount,
    bool endOfPacket,
    GIFDataFormat format,
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
      (static_cast<std::uint64_t>(format) << 58) |
      (static_cast<std::uint64_t>(registerCount & 0x0f) << 60);
    return GIFQuadword{{
      static_cast<std::uint32_t>(low),
      static_cast<std::uint32_t>(low >> 32),
      static_cast<std::uint32_t>(registers),
      static_cast<std::uint32_t>(registers >> 32)
    }};
  }

  GIFQuadword quadword(
    std::uint32_t word0,
    std::uint32_t word1,
    std::uint32_t word2,
    std::uint32_t word3)
  {
    return GIFQuadword{{word0, word1, word2, word3}};
  }

  std::uint64_t doubleword(
    std::uint32_t low,
    std::uint32_t high)
  {
    return
      static_cast<std::uint64_t>(low) |
      (static_cast<std::uint64_t>(high) << 32);
  }
}

TEST_CASE("GIF Tag Decoder Tests")
{
  SECTION("Every GIFtag field decodes from its hardware position")
  {
    const GIFTag tag = decodeGIFTag(gifTag(
      0x1234,
      true,
      GIFDataFormat::RegisterList,
      7,
      0xfedcba9876543210,
      true,
      0x456));

    REQUIRE(tag.loopCount == 0x1234);
    REQUIRE(tag.endOfPacket);
    REQUIRE(tag.primitiveEnabled);
    REQUIRE(tag.primitive == 0x456);
    REQUIRE(tag.format == GIFDataFormat::RegisterList);
    REQUIRE(tag.registerCount == 7);
    REQUIRE(tag.registers == 0xfedcba9876543210);
  }

  SECTION("An encoded NREG of zero means sixteen descriptors")
  {
    const GIFTag tag = decodeGIFTag(gifTag(
      1,
      false,
      GIFDataFormat::Packed,
      0,
      0x0123456789abcdef));

    REQUIRE(tag.registerCount == 16);
  }
}

TEST_CASE("GIF PACKED Decoder Tests")
{
  SECTION("Descriptor order repeats for each loop across fragmented calls")
  {
    GIFDecoder decoder;
    const GIFDecodeResult tagResult = decoder.ingestQuadword(gifTag(
      2,
      true,
      GIFDataFormat::Packed,
      2,
      0x21,
      true,
      0x345));

    REQUIRE(tagResult.tagDecoded);
    REQUIRE(tagResult.writes.size() == 1);
    REQUIRE(tagResult.writes[0].address == GIFRegisterAddress::PRIM);
    REQUIRE(tagResult.writes[0].data == 0x345);
    REQUIRE(decoder.quadwordsRemaining() == 4);

    const GIFDecodeResult rgba0 =
      decoder.ingestQuadword(quadword(1, 2, 3, 4));
    REQUIRE(rgba0.writes.size() == 1);
    REQUIRE(rgba0.writes[0].address == GIFRegisterAddress::RGBAQ);
    REQUIRE(rgba0.writes[0].data == 0x3f80000004030201);
    REQUIRE(decoder.registerIndex() == 1);

    const GIFDecodeResult st0 = decoder.ingestQuadword(
      quadword(5, 6, 0x11223344, 0));
    REQUIRE(st0.writes[0].address == GIFRegisterAddress::ST);
    REQUIRE(st0.writes[0].data == doubleword(5, 6));
    REQUIRE(decoder.loopIndex() == 1);

    const GIFDecodeResult rgba1 =
      decoder.ingestQuadword(quadword(7, 8, 9, 10));
    REQUIRE(rgba1.writes[0].data == 0x112233440a090807);

    const GIFDecodeResult st1 =
      decoder.ingestQuadword(quadword(11, 12, 0, 0));
    REQUIRE(st1.primitiveComplete);
    REQUIRE(st1.packetComplete);
    REQUIRE(decoder.awaitingTag());
    REQUIRE(!decoder.packetInProgress());
  }

  SECTION("PACKED formats produce the GS register layouts")
  {
    struct Contract
    {
      std::uint8_t descriptor;
      GIFQuadword input;
      std::uint8_t address;
      std::uint64_t data;
    };
    const std::array<Contract, 9> contracts = {{
      {
        GIFRegisterDescriptor::PRIM,
        quadword(0xabcdef12, 0, 0, 0),
        GIFRegisterAddress::PRIM,
        0x712
      },
      {
        GIFRegisterDescriptor::RGBAQ,
        quadword(0x101, 0x202, 0x303, 0x404),
        GIFRegisterAddress::RGBAQ,
        0x3f80000004030201
      },
      {
        GIFRegisterDescriptor::ST,
        quadword(0x11111111, 0x22222222, 0x33333333, 0),
        GIFRegisterAddress::ST,
        0x2222222211111111
      },
      {
        GIFRegisterDescriptor::UV,
        quadword(0xffff, 0x9234, 0, 0),
        GIFRegisterAddress::UV,
        0x12347fff
      },
      {
        GIFRegisterDescriptor::XYZF2,
        quadword(0x12345, 0x26789, 0xabcdef0, 0x5a0),
        GIFRegisterAddress::XYZF2,
        0x5aabcdef67892345
      },
      {
        GIFRegisterDescriptor::XYZF2,
        quadword(1, 2, 0x30, 0x8000),
        GIFRegisterAddress::XYZF3,
        0x0000000300020001
      },
      {
        GIFRegisterDescriptor::XYZ2,
        quadword(0x12345, 0x26789, 0x89abcdef, 0x8000),
        GIFRegisterAddress::XYZ3,
        0x89abcdef67892345
      },
      {
        GIFRegisterDescriptor::FOG,
        quadword(0, 0, 0, 0x5a0),
        GIFRegisterAddress::FOG,
        0x5a00000000000000
      },
      {
        GIFRegisterDescriptor::TEX0_1,
        quadword(0x89abcdef, 0x01234567, 0, 0),
        GIFRegisterDescriptor::TEX0_1,
        0x0123456789abcdef
      }
    }};

    for (const Contract &contract : contracts)
    {
      GIFDecoder decoder;
      decoder.ingestQuadword(gifTag(
        1,
        true,
        GIFDataFormat::Packed,
        1,
        contract.descriptor));
      const GIFDecodeResult result =
        decoder.ingestQuadword(contract.input);

      REQUIRE(result.writes.size() == 1);
      REQUIRE(result.writes[0].address == contract.address);
      REQUIRE(result.writes[0].data == contract.data);
      REQUIRE(result.packetComplete);
    }
  }

  SECTION("A+D uses the upper doubleword address and lower data")
  {
    GIFDecoder decoder;
    decoder.ingestQuadword(gifTag(
      1,
      true,
      GIFDataFormat::Packed,
      1,
      GIFRegisterDescriptor::AD));
    const GIFDecodeResult result = decoder.ingestQuadword(
      quadword(0x89abcdef, 0x01234567, 0x42, 0xffffffff));

    REQUIRE(result.writes.size() == 1);
    REQUIRE(result.writes[0].address == 0x42);
    REQUIRE(result.writes[0].data == 0x0123456789abcdef);
  }

  SECTION("Lower-doubleword descriptors retain their hardware addresses")
  {
    const std::array<std::uint8_t, 6> descriptors = {{
      GIFRegisterDescriptor::TEX0_1,
      GIFRegisterDescriptor::TEX0_2,
      GIFRegisterDescriptor::CLAMP_1,
      GIFRegisterDescriptor::CLAMP_2,
      GIFRegisterDescriptor::XYZF3,
      GIFRegisterDescriptor::XYZ3
    }};

    for (const std::uint8_t descriptor : descriptors)
    {
      GIFDecoder decoder;
      decoder.ingestQuadword(gifTag(
        1,
        true,
        GIFDataFormat::Packed,
        1,
        descriptor));
      const GIFDecodeResult result = decoder.ingestQuadword(
        quadword(0x89abcdef, 0x01234567, 0, 0));

      REQUIRE(result.writes.size() == 1);
      REQUIRE(result.writes[0].address == descriptor);
      REQUIRE(result.writes[0].data == 0x0123456789abcdef);
    }
  }

  SECTION("Q returns to one when each GIFtag is read")
  {
    GIFDecoder decoder;
    const std::uint64_t descriptors =
      GIFRegisterDescriptor::ST |
      (static_cast<std::uint64_t>(
        GIFRegisterDescriptor::RGBAQ) << 4);
    decoder.ingestQuadword(gifTag(
      1,
      false,
      GIFDataFormat::Packed,
      2,
      descriptors));
    decoder.ingestQuadword(quadword(0, 0, 0x12345678, 0));
    const GIFDecodeResult changedQ =
      decoder.ingestQuadword(quadword(1, 2, 3, 4));
    REQUIRE(changedQ.writes[0].data == 0x1234567804030201);

    decoder.ingestQuadword(gifTag(
      1,
      true,
      GIFDataFormat::Packed,
      1,
      GIFRegisterDescriptor::RGBAQ));
    const GIFDecodeResult resetQ =
      decoder.ingestQuadword(quadword(1, 2, 3, 4));
    REQUIRE(resetQ.writes[0].data == 0x3f80000004030201);
  }

  SECTION("NOP emits no write")
  {
    GIFDecoder decoder;
    decoder.ingestQuadword(gifTag(
      1,
      true,
      GIFDataFormat::Packed,
      1,
      GIFRegisterDescriptor::NOP));
    const GIFDecodeResult result =
      decoder.ingestQuadword(quadword(1, 2, 3, 4));

    REQUIRE(result.writes.empty());
    REQUIRE(result.packetComplete);
  }

  SECTION("A reserved active descriptor is rejected without changing state")
  {
    for (const GIFDataFormat format : {
      GIFDataFormat::Packed,
      GIFDataFormat::RegisterList})
    {
      GIFDecoder decoder;
      REQUIRE_THROWS_WITH(
        decoder.ingestQuadword(gifTag(
          1,
          true,
          format,
          1,
          GIFRegisterDescriptor::Reserved)),
        "GIF tag uses the reserved register descriptor.");
      REQUIRE(decoder.awaitingTag());
      REQUIRE(!decoder.packetInProgress());
    }
  }
}

TEST_CASE("GIF REGLIST Decoder Tests")
{
  SECTION("Two register values are consumed per qword")
  {
    GIFDecoder decoder;
    const std::uint64_t descriptors =
      GIFRegisterDescriptor::PRIM |
      (static_cast<std::uint64_t>(GIFRegisterDescriptor::RGBAQ) << 4) |
      (static_cast<std::uint64_t>(GIFRegisterDescriptor::ST) << 8);
    const GIFDecodeResult tagResult = decoder.ingestQuadword(gifTag(
      1,
      true,
      GIFDataFormat::RegisterList,
      3,
      descriptors,
      true,
      0x777));

    REQUIRE(tagResult.writes.empty());
    REQUIRE(decoder.quadwordsRemaining() == 2);

    const GIFDecodeResult first =
      decoder.ingestQuadword(quadword(1, 2, 3, 4));
    REQUIRE(first.writes.size() == 2);
    REQUIRE(first.writes[0].address == GIFRegisterAddress::PRIM);
    REQUIRE(first.writes[0].data == doubleword(1, 2));
    REQUIRE(first.writes[1].address == GIFRegisterAddress::RGBAQ);
    REQUIRE(first.writes[1].data == doubleword(3, 4));

    const GIFDecodeResult second =
      decoder.ingestQuadword(quadword(5, 6, 0xdeadbeef, 0xcafebabe));
    REQUIRE(second.writes.size() == 1);
    REQUIRE(second.writes[0].address == GIFRegisterAddress::ST);
    REQUIRE(second.writes[0].data == doubleword(5, 6));
    REQUIRE(second.packetComplete);
  }

  SECTION("A+D and NOP descriptors are no-ops in REGLIST")
  {
    GIFDecoder decoder;
    decoder.ingestQuadword(gifTag(
      1,
      true,
      GIFDataFormat::RegisterList,
      2,
      GIFRegisterDescriptor::AD |
        (static_cast<std::uint64_t>(GIFRegisterDescriptor::NOP) << 4)));
    const GIFDecodeResult result =
      decoder.ingestQuadword(quadword(1, 2, 3, 4));

    REQUIRE(result.writes.empty());
    REQUIRE(result.packetComplete);
  }
}

TEST_CASE("GIF IMAGE and Packet Termination Tests")
{
  SECTION("IMAGE and disabled formats write both doublewords to HWREG")
  {
    for (const GIFDataFormat format : {
      GIFDataFormat::Image,
      GIFDataFormat::Disabled})
    {
      GIFDecoder decoder;
      decoder.ingestQuadword(gifTag(
        1,
        true,
        format,
        4,
        0x1234,
        true,
        0x456));
      const GIFDecodeResult result =
        decoder.ingestQuadword(quadword(1, 2, 3, 4));

      REQUIRE(result.writes.size() == 2);
      REQUIRE(result.writes[0].address == GIFRegisterAddress::HWREG);
      REQUIRE(result.writes[0].data == doubleword(1, 2));
      REQUIRE(result.writes[1].address == GIFRegisterAddress::HWREG);
      REQUIRE(result.writes[1].data == doubleword(3, 4));
      REQUIRE(result.packetComplete);
    }
  }

  SECTION("EOP completes only the final primitive in a packet")
  {
    GIFDecoder decoder;
    decoder.ingestQuadword(gifTag(
      1,
      false,
      GIFDataFormat::Packed,
      1,
      GIFRegisterDescriptor::NOP));
    const GIFDecodeResult first =
      decoder.ingestQuadword(quadword(0, 0, 0, 0));

    REQUIRE(first.primitiveComplete);
    REQUIRE(!first.packetComplete);
    REQUIRE(decoder.packetInProgress());

    decoder.ingestQuadword(gifTag(
      1,
      true,
      GIFDataFormat::Packed,
      1,
      GIFRegisterDescriptor::NOP));
    const GIFDecodeResult second =
      decoder.ingestQuadword(quadword(0, 0, 0, 0));

    REQUIRE(second.primitiveComplete);
    REQUIRE(second.packetComplete);
    REQUIRE(!decoder.packetInProgress());
  }

  SECTION("NLOOP zero ignores all fields except EOP")
  {
    GIFDecoder decoder;
    const GIFDecodeResult result = decoder.ingestQuadword(gifTag(
      0,
      true,
      GIFDataFormat::Packed,
      1,
      GIFRegisterDescriptor::Reserved,
      true,
      0x456));

    REQUIRE(result.tagDecoded);
    REQUIRE(result.writes.empty());
    REQUIRE(result.primitiveComplete);
    REQUIRE(result.packetComplete);
    REQUIRE(decoder.awaitingTag());
  }
}
