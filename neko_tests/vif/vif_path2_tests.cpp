#include <cstdint>

#include "catch.hpp"
#include "gif.hpp"
#include "vif.hpp"

namespace
{
  constexpr std::uint32_t WORDS_PER_QUADWORD = 4;

  std::uint32_t vifCode(
    std::uint8_t command,
    std::uint16_t immediate = 0)
  {
    return
      (static_cast<std::uint32_t>(command) << 24) |
      immediate;
  }

  GIFQuadword gifTag(
    std::uint16_t loopCount,
    bool endOfPacket,
    GIFDataFormat format,
    std::uint8_t registerCount,
    std::uint64_t registers)
  {
    const std::uint64_t low =
      loopCount |
      (static_cast<std::uint64_t>(endOfPacket) << 15) |
      (static_cast<std::uint64_t>(format) << 58) |
      (static_cast<std::uint64_t>(registerCount) << 60);
    return GIFQuadword{{
      static_cast<std::uint32_t>(low),
      static_cast<std::uint32_t>(low >> 32),
      static_cast<std::uint32_t>(registers),
      static_cast<std::uint32_t>(registers >> 32)
    }};
  }

  void alignDirectCommand(VIF *vif)
  {
    while ((vif->wordsIngested() + 1) %
           WORDS_PER_QUADWORD != 0)
    {
      vif->ingestWord(vifCode(VIFCommandEncoding::NOP));
    }
  }

  VIFStreamWord ingestQuadword(
    VIF *vif,
    const GIFQuadword &quadword)
  {
    for (std::uint32_t index = 0;
         index < WORDS_PER_QUADWORD - 1;
         ++index)
    {
      const VIFStreamWord word = vif->ingestWord(quadword[index]);
      REQUIRE(!word.gifQuadwordDecoded);
    }
    return vif->ingestWord(quadword[WORDS_PER_QUADWORD - 1]);
  }
}

TEST_CASE("VIF PATH2 Attachment Tests")
{
  SECTION("Only VIF1 can attach a non-null GIF decoder")
  {
    GIFDecoder decoder;
    VIF vif0(VIFType::VIF0);
    VIF vif1(VIFType::VIF1);

    REQUIRE_THROWS_WITH(
      vif1.attachGIFDecoder(nullptr),
      "Cannot attach a null GIF decoder.");
    REQUIRE_THROWS_WITH(
      vif0.attachGIFDecoder(&decoder),
      "Only VIF1 can attach to GIF PATH2.");
    REQUIRE_NOTHROW(vif1.attachGIFDecoder(&decoder));
  }

  SECTION("DIRECT requires a decoder before payload state begins")
  {
    VIF vif(VIFType::VIF1);
    alignDirectCommand(&vif);

    REQUIRE_THROWS_WITH(
      vif.ingestWord(vifCode(VIFCommandEncoding::DIRECT, 1)),
      "VIF DIRECT requires an attached GIF decoder.");
    REQUIRE(!vif.awaitingPayload());
  }

  SECTION("The decoder cannot be replaced during a payload")
  {
    GIFDecoder first;
    GIFDecoder second;
    VIF vif(VIFType::VIF1);
    vif.attachGIFDecoder(&first);
    alignDirectCommand(&vif);
    vif.ingestWord(vifCode(VIFCommandEncoding::DIRECT, 1));

    REQUIRE_THROWS_WITH(
      vif.attachGIFDecoder(&second),
      "Cannot attach a GIF decoder while a VIF payload is in progress.");
  }
}

TEST_CASE("VIF PATH2 DIRECT Tests")
{
  SECTION("Fragmented DIRECT words decode a complete GIF packet")
  {
    GIFDecoder decoder;
    VIF vif(VIFType::VIF1);
    vif.attachGIFDecoder(&decoder);
    alignDirectCommand(&vif);
    const VIFStreamWord command =
      vif.ingestWord(vifCode(VIFCommandEncoding::DIRECT, 2));

    REQUIRE(command.payloadWordCount == 8);
    REQUIRE(!command.gifQuadwordDecoded);

    const VIFStreamWord tag = ingestQuadword(
      &vif,
      gifTag(
        1,
        true,
        GIFDataFormat::Packed,
        1,
        GIFRegisterDescriptor::AD));
    REQUIRE(tag.gifQuadwordDecoded);
    REQUIRE(tag.gifResult.tagDecoded);
    REQUIRE(tag.gifResult.writes.empty());
    REQUIRE(!tag.packetComplete);

    const VIFStreamWord data = ingestQuadword(
      &vif,
      GIFQuadword{{
        0x89abcdef,
        0x01234567,
        0x42,
        0
      }});
    REQUIRE(data.gifQuadwordDecoded);
    REQUIRE(data.gifResult.writes.size() == 1);
    REQUIRE(data.gifResult.writes[0].address == 0x42);
    REQUIRE(
      data.gifResult.writes[0].data ==
      UINT64_C(0x0123456789abcdef));
    REQUIRE(data.gifResult.packetComplete);
    REQUIRE(data.packetComplete);
    REQUIRE(!vif.awaitingPayload());
  }

  SECTION("A GIF primitive can continue across DIRECT commands")
  {
    GIFDecoder decoder;
    VIF vif(VIFType::VIF1);
    vif.attachGIFDecoder(&decoder);
    alignDirectCommand(&vif);
    vif.ingestWord(vifCode(VIFCommandEncoding::DIRECT, 1));
    const VIFStreamWord tag = ingestQuadword(
      &vif,
      gifTag(
        1,
        true,
        GIFDataFormat::Packed,
        1,
        GIFRegisterDescriptor::NOP));

    REQUIRE(tag.packetComplete);
    REQUIRE(decoder.quadwordsRemaining() == 1);

    alignDirectCommand(&vif);
    vif.ingestWord(vifCode(VIFCommandEncoding::DIRECT, 1));
    const VIFStreamWord data = ingestQuadword(
      &vif,
      GIFQuadword{{1, 2, 3, 4}});

    REQUIRE(data.gifResult.packetComplete);
    REQUIRE(decoder.awaitingTag());
  }

  SECTION("DIRECTHL transfers the same GIF data through PATH2")
  {
    GIFDecoder decoder;
    VIF vif(VIFType::VIF1);
    vif.attachGIFDecoder(&decoder);
    alignDirectCommand(&vif);
    vif.ingestWord(vifCode(VIFCommandEncoding::DIRECTHL, 2));
    ingestQuadword(
      &vif,
      gifTag(
        1,
        true,
        GIFDataFormat::Image,
        0,
        0));
    const VIFStreamWord image = ingestQuadword(
      &vif,
      GIFQuadword{{1, 2, 3, 4}});

    REQUIRE(image.gifResult.writes.size() == 2);
    REQUIRE(
      image.gifResult.writes[0].address ==
      GIFRegisterAddress::HWREG);
    REQUIRE(image.gifResult.writes[0].data == UINT64_C(0x200000001));
    REQUIRE(image.gifResult.writes[1].data == UINT64_C(0x400000003));
    REQUIRE(image.gifResult.packetComplete);
  }
}
