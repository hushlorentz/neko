#include <array>
#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "gif_diagnostics.hpp"
#include "gif_path3.hpp"
#include "gs.hpp"
#include "vif.hpp"

namespace
{
  class RecordingRegisterWriteHandler :
    public GIFRegisterWriteHandler
  {
    public:
      void writeRegister(
        std::uint8_t address,
        std::uint64_t data) override
      {
        GIFRegisterWrite write;
        write.address = address;
        write.data = data;
        writes.push_back(write);
      }

      std::vector<GIFRegisterWrite> writes;
  };

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

  std::uint32_t vifCode(
    std::uint8_t command,
    std::uint16_t immediate = 0)
  {
    return
      (static_cast<std::uint32_t>(command) << 24) |
      immediate;
  }

  std::vector<GIFQuadword> transferredOnPath(
    const std::vector<GIFTraceEvent> &events,
    GIFPath path)
  {
    std::vector<GIFQuadword> quadwords;
    for (const GIFTraceEvent &event : events)
    {
      if (event.type == GIFTraceEventType::QuadwordTransferred &&
          event.path == path)
      {
        quadwords.push_back(event.quadword);
      }
    }
    return quadwords;
  }
}

TEST_CASE("GIF PATH3 Transport Tests")
{
  SECTION("The transport validates its source boundary")
  {
    REQUIRE_THROWS_WITH(
      GIFPath3Transfer(nullptr),
      "GIF PATH3 requires a non-null decoder.");

    GIFDecoder decoder;
    GIFPath3Transfer path3(&decoder);
    REQUIRE_THROWS_WITH(
      path3.submitQuadwords(nullptr, 1),
      "GIF PATH3 requires non-null qword input.");

    const GIFPath3SubmissionResult empty =
      path3.submitQuadwords(nullptr, 0);
    REQUIRE(empty.transferredQuadwords == 0);
    REQUIRE(!empty.stalled);
    REQUIRE(path3.submissionAttemptCount() == 0);
  }

  SECTION("A batch stops before consuming a blocked qword")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    GIFPath3Transfer path3(arbiter);
    const std::array<GIFQuadword, 2> packet = {{
      gifTag(
        1,
        true,
        GIFDataFormat::Packed,
        1,
        GIFRegisterDescriptor::NOP),
      GIFQuadword{{1, 2, 3, 4}}
    }};

    REQUIRE(arbiter.transferQuadword(
      GIFPath::Path2,
      gifTag(0, false, GIFDataFormat::Packed, 0, 0)).accepted);

    const GIFPath3SubmissionResult stalled =
      path3.submitQuadwords(packet.data(), packet.size());
    REQUIRE(stalled.transferredQuadwords == 0);
    REQUIRE(stalled.stalled);
    REQUIRE(!stalled.packetComplete);
    REQUIRE(path3.submissionAttemptCount() == 1);
    REQUIRE(path3.transferredQuadwordCount() == 0);
    REQUIRE(decoder.packetInProgress());

    REQUIRE(arbiter.transferQuadword(
      GIFPath::Path2,
      gifTag(0, true, GIFDataFormat::Packed, 0, 0)).accepted);
    REQUIRE(arbiter.activePath() == GIFPath::Path3);

    const GIFPath3SubmissionResult resumed =
      path3.submitQuadwords(packet.data(), packet.size());
    REQUIRE(resumed.transferredQuadwords == 2);
    REQUIRE(!resumed.stalled);
    REQUIRE(resumed.packetComplete);
    REQUIRE(path3.submissionAttemptCount() == 3);
    REQUIRE(path3.transferredQuadwordCount() == 2);
    REQUIRE(path3.completedPacketCount() == 1);
    REQUIRE(decoder.awaitingTag());
  }

  SECTION("A VIF mask preserves the first pending qword")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    GIFPath3Transfer path3(arbiter);
    const GIFQuadword packet =
      gifTag(0, true, GIFDataFormat::Packed, 0, 0);
    arbiter.setPath3MaskedByVIF(true);

    const GIFPath3SubmissionResult masked =
      path3.submitQuadwords(&packet, 1);
    REQUIRE(masked.stalled);
    REQUIRE(masked.transferredQuadwords == 0);
    REQUIRE(arbiter.pathPending(GIFPath::Path3));
    REQUIRE(decoder.awaitingTag());

    arbiter.setPath3MaskedByVIF(false);
    const GIFPath3SubmissionResult resumed =
      path3.submitQuadwords(&packet, 1);
    REQUIRE(!resumed.stalled);
    REQUIRE(resumed.transferredQuadwords == 1);
    REQUIRE(resumed.packetComplete);
  }
}

TEST_CASE("GIF PATH3 Decode Integration Tests")
{
  GIFDecoder decoder;
  GS gs;
  decoder.attachRegisterWriteHandler(&gs);
  GIFPathArbiter arbiter(&decoder);
  GIFPath3Transfer path3(arbiter);
  GIFDiagnosticsRecorder diagnostics;
  arbiter.setTraceCallback(
    [&diagnostics](const GIFTraceEvent &event) {
      diagnostics.observe(event);
    });
  const std::array<GIFQuadword, 2> packet = {{
    gifTag(
      1,
      true,
      GIFDataFormat::Packed,
      1,
      GIFRegisterDescriptor::AD),
    GIFQuadword{{
      0x89abcdef,
      0x01234567,
      GSRegisterAddress::FRAME_1,
      0
    }}
  }};

  const GIFPath3SubmissionResult result =
    path3.submitQuadwords(packet.data(), packet.size());

  REQUIRE(result.transferredQuadwords == packet.size());
  REQUIRE(result.packetComplete);
  REQUIRE(
    gs.registerValue(GSRegisterAddress::FRAME_1) ==
    UINT64_C(0x0123456789abcdef));
  REQUIRE(diagnostics.summary().transferredQuadwords[2] == 2);
  REQUIRE(diagnostics.summary().decodedTags == 1);
  REQUIRE(diagnostics.summary().registerWrites == 1);
  REQUIRE(diagnostics.summary().completedPackets == 1);
}

TEST_CASE("GIF PATH3 Format Conformance Tests")
{
  SECTION("PACKED repeats descriptors across multiple loops")
  {
    GIFDecoder decoder;
    RecordingRegisterWriteHandler writes;
    decoder.attachRegisterWriteHandler(&writes);
    GIFPath3Transfer path3(&decoder);
    const std::array<GIFQuadword, 5> packet = {{
      gifTag(
        2,
        true,
        GIFDataFormat::Packed,
        2,
        GIFRegisterDescriptor::RGBAQ |
          (static_cast<std::uint64_t>(
            GIFRegisterDescriptor::AD) << 4)),
      GIFQuadword{{1, 2, 3, 4}},
      GIFQuadword{{0x11111111, 0x22222222, 0x40, 0}},
      GIFQuadword{{5, 6, 7, 8}},
      GIFQuadword{{0x33333333, 0x44444444, 0x41, 0}}
    }};

    const GIFPath3SubmissionResult result =
      path3.submitQuadwords(packet.data(), packet.size());

    REQUIRE(result.transferredQuadwords == packet.size());
    REQUIRE(result.packetComplete);
    REQUIRE(writes.writes.size() == 4);
    REQUIRE(writes.writes[0].address == GIFRegisterAddress::RGBAQ);
    REQUIRE(
      writes.writes[0].data ==
      UINT64_C(0x3f80000004030201));
    REQUIRE(writes.writes[1].address == 0x40);
    REQUIRE(
      writes.writes[1].data ==
      UINT64_C(0x2222222211111111));
    REQUIRE(writes.writes[2].address == GIFRegisterAddress::RGBAQ);
    REQUIRE(
      writes.writes[2].data ==
      UINT64_C(0x3f80000008070605));
    REQUIRE(writes.writes[3].address == 0x41);
    REQUIRE(
      writes.writes[3].data ==
      UINT64_C(0x4444444433333333));
  }

  SECTION("REGLIST consumes odd and even register-value counts")
  {
    GIFDecoder decoder;
    RecordingRegisterWriteHandler writes;
    decoder.attachRegisterWriteHandler(&writes);
    GIFPath3Transfer path3(&decoder);
    const std::array<GIFQuadword, 6> packets = {{
      gifTag(
        1,
        true,
        GIFDataFormat::RegisterList,
        3,
        GIFRegisterDescriptor::PRIM |
          (static_cast<std::uint64_t>(
            GIFRegisterDescriptor::RGBAQ) << 4) |
          (static_cast<std::uint64_t>(
            GIFRegisterDescriptor::UV) << 8)),
      GIFQuadword{{1, 0, 2, 0}},
      GIFQuadword{{3, 0, 0xdeadbeef, 0xfeedface}},
      gifTag(
        2,
        true,
        GIFDataFormat::RegisterList,
        2,
        GIFRegisterDescriptor::PRIM |
          (static_cast<std::uint64_t>(
            GIFRegisterDescriptor::RGBAQ) << 4)),
      GIFQuadword{{4, 0, 5, 0}},
      GIFQuadword{{6, 0, 7, 0}}
    }};

    const GIFPath3SubmissionResult result =
      path3.submitQuadwords(packets.data(), packets.size());

    REQUIRE(result.transferredQuadwords == packets.size());
    REQUIRE(result.packetComplete);
    REQUIRE(path3.completedPacketCount() == 2);
    REQUIRE(writes.writes.size() == 7);
    const std::array<std::uint8_t, 7> expectedAddresses = {{
      GIFRegisterDescriptor::PRIM,
      GIFRegisterDescriptor::RGBAQ,
      GIFRegisterDescriptor::UV,
      GIFRegisterDescriptor::PRIM,
      GIFRegisterDescriptor::RGBAQ,
      GIFRegisterDescriptor::PRIM,
      GIFRegisterDescriptor::RGBAQ
    }};
    const std::array<std::uint64_t, 7> expectedData = {{
      1, 2, 3, 4, 5, 6, 7
    }};
    for (std::size_t index = 0;
         index < writes.writes.size();
         ++index)
    {
      REQUIRE(
        writes.writes[index].address ==
        expectedAddresses[index]);
      REQUIRE(writes.writes[index].data == expectedData[index]);
    }
  }

  SECTION("IMAGE delivers every doubleword to HWREG in order")
  {
    GIFDecoder decoder;
    RecordingRegisterWriteHandler writes;
    decoder.attachRegisterWriteHandler(&writes);
    GIFPath3Transfer path3(&decoder);
    const std::array<GIFQuadword, 3> packet = {{
      gifTag(2, true, GIFDataFormat::Image, 0, 0),
      GIFQuadword{{1, 2, 3, 4}},
      GIFQuadword{{5, 6, 7, 8}}
    }};

    const GIFPath3SubmissionResult result =
      path3.submitQuadwords(packet.data(), packet.size());

    REQUIRE(result.transferredQuadwords == packet.size());
    REQUIRE(result.packetComplete);
    REQUIRE(writes.writes.size() == 4);
    const std::array<std::uint64_t, 4> expectedData = {{
      UINT64_C(0x0000000200000001),
      UINT64_C(0x0000000400000003),
      UINT64_C(0x0000000600000005),
      UINT64_C(0x0000000800000007)
    }};
    for (std::size_t index = 0;
         index < writes.writes.size();
         ++index)
    {
      REQUIRE(
        writes.writes[index].address ==
        GIFRegisterAddress::HWREG);
      REQUIRE(writes.writes[index].data == expectedData[index]);
    }
  }
}

TEST_CASE("GIF PATH3 Fragmentation and Contention Tests")
{
  SECTION("One-qword submissions preserve decoder progress")
  {
    GIFDecoder decoder;
    GIFPath3Transfer path3(&decoder);
    const std::array<GIFQuadword, 3> packet = {{
      gifTag(
        2,
        true,
        GIFDataFormat::Packed,
        1,
        GIFRegisterDescriptor::NOP),
      GIFQuadword{{1, 2, 3, 4}},
      GIFQuadword{{5, 6, 7, 8}}
    }};

    const GIFPath3SubmissionResult tag =
      path3.submitQuadwords(&packet[0], 1);
    REQUIRE(!tag.packetComplete);
    REQUIRE(decoder.quadwordsRemaining() == 2);

    const GIFPath3SubmissionResult first =
      path3.submitQuadwords(&packet[1], 1);
    REQUIRE(!first.packetComplete);
    REQUIRE(decoder.quadwordsRemaining() == 1);

    const GIFPath3SubmissionResult second =
      path3.submitQuadwords(&packet[2], 1);
    REQUIRE(second.packetComplete);
    REQUIRE(decoder.awaitingTag());
    REQUIRE(path3.transferredQuadwordCount() == packet.size());
  }

  SECTION("PATH1 and PATH2 contention neither loses nor duplicates PATH3 data")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    GIFPath3Transfer path3(arbiter);
    GIFDiagnosticsRecorder diagnostics;
    arbiter.setTraceCallback(
      [&diagnostics](const GIFTraceEvent &event) {
        diagnostics.observe(event);
      });
    const std::array<GIFQuadword, 4> path3Input = {{
      gifTag(
        1,
        true,
        GIFDataFormat::Packed,
        1,
        GIFRegisterDescriptor::NOP),
      GIFQuadword{{1, 2, 3, 4}},
      gifTag(
        1,
        true,
        GIFDataFormat::Packed,
        1,
        GIFRegisterDescriptor::NOP),
      GIFQuadword{{5, 6, 7, 8}}
    }};

    REQUIRE(
      path3.submitQuadwords(path3Input.data(), 1)
        .transferredQuadwords == 1);
    REQUIRE(!arbiter.requestPath(GIFPath::Path2));
    REQUIRE(!arbiter.requestPath(GIFPath::Path1));
    REQUIRE(
      path3.submitQuadwords(&path3Input[1], 1)
        .transferredQuadwords == 1);
    REQUIRE(arbiter.activePath() == GIFPath::Path1);

    REQUIRE(path3.submitQuadwords(
      &path3Input[2], 2).stalled);
    REQUIRE(arbiter.transferQuadword(
      GIFPath::Path1,
      gifTag(0, true, GIFDataFormat::Packed, 0, 0)).accepted);
    REQUIRE(arbiter.activePath() == GIFPath::Path2);
    REQUIRE(path3.submitQuadwords(
      &path3Input[2], 2).stalled);
    REQUIRE(arbiter.transferQuadword(
      GIFPath::Path2,
      gifTag(0, true, GIFDataFormat::Packed, 0, 0)).accepted);
    REQUIRE(arbiter.activePath() == GIFPath::Path3);

    const GIFPath3SubmissionResult resumed =
      path3.submitQuadwords(&path3Input[2], 2);
    REQUIRE(resumed.transferredQuadwords == 2);
    REQUIRE(resumed.packetComplete);
    REQUIRE(
      transferredOnPath(
        diagnostics.events(),
        GIFPath::Path3) ==
      std::vector<GIFQuadword>(
        path3Input.begin(),
        path3Input.end()));
    REQUIRE(path3.transferredQuadwordCount() == path3Input.size());
    REQUIRE(path3.completedPacketCount() == 2);
    REQUIRE(diagnostics.summary().stalledTransfers[2] == 2);
  }
}

TEST_CASE("GIF PATH3 VIF Mask Integration Tests")
{
  SECTION("A mask finishes the active packet and suppresses the next one")
  {
    GIFDecoder decoder;
    RecordingRegisterWriteHandler writes;
    decoder.attachRegisterWriteHandler(&writes);
    GIFPathArbiter arbiter(&decoder);
    GIFPath3Transfer path3(arbiter);
    VIF vif(VIFType::VIF1);
    vif.attachGIFPathArbiter(&arbiter);
    GIFDiagnosticsRecorder diagnostics;
    arbiter.setTraceCallback(
      [&diagnostics](const GIFTraceEvent &event) {
        diagnostics.observe(event);
      });
    const std::array<GIFQuadword, 3> firstPacket = {{
      gifTag(
        1,
        true,
        GIFDataFormat::Packed,
        2,
        GIFRegisterDescriptor::RGBAQ |
          (static_cast<std::uint64_t>(
            GIFRegisterDescriptor::AD) << 4)),
      GIFQuadword{{1, 2, 3, 4}},
      GIFQuadword{{0x11111111, 0x22222222, 0x42, 0}}
    }};
    const GIFQuadword secondPacket =
      gifTag(0, true, GIFDataFormat::Packed, 0, 0);

    REQUIRE(
      path3.submitQuadwords(firstPacket.data(), 2)
        .transferredQuadwords == 2);
    REQUIRE(decoder.registerIndex() == 1);
    REQUIRE(decoder.quadwordsRemaining() == 1);

    const VIFStreamWord mask = vif.ingestWord(vifCode(
      VIFCommandEncoding::MSKPATH3,
      VIFImmediateEncoding::MSKPATH3Mask));
    REQUIRE(!mask.stalled);
    REQUIRE(vif.path3Masked());
    REQUIRE(arbiter.path3MaskedByVIF());
    REQUIRE(arbiter.activePath() == GIFPath::Path3);

    const GIFPath3SubmissionResult completed =
      path3.submitQuadwords(&firstPacket[2], 1);
    REQUIRE(completed.transferredQuadwords == 1);
    REQUIRE(completed.packetComplete);
    REQUIRE(writes.writes.size() == 2);
    REQUIRE(writes.writes[0].address == GIFRegisterAddress::RGBAQ);
    REQUIRE(writes.writes[1].address == 0x42);
    REQUIRE(
      writes.writes[1].data ==
      UINT64_C(0x2222222211111111));
    REQUIRE(arbiter.activePath() == GIFPath::Idle);

    const GIFPath3SubmissionResult suppressed =
      path3.submitQuadwords(&secondPacket, 1);
    REQUIRE(suppressed.stalled);
    REQUIRE(suppressed.transferredQuadwords == 0);
    REQUIRE(arbiter.pathPending(GIFPath::Path3));
    REQUIRE(decoder.awaitingTag());

    const VIFStreamWord unmask =
      vif.ingestWord(vifCode(VIFCommandEncoding::MSKPATH3));
    REQUIRE(!unmask.stalled);
    REQUIRE(!vif.path3Masked());
    REQUIRE(arbiter.activePath() == GIFPath::Path3);

    const GIFPath3SubmissionResult resumed =
      path3.submitQuadwords(&secondPacket, 1);
    REQUIRE(!resumed.stalled);
    REQUIRE(resumed.packetComplete);
    REQUIRE(path3.transferredQuadwordCount() == 4);
    REQUIRE(path3.completedPacketCount() == 2);
    REQUIRE(diagnostics.summary().path3MaskChanges == 2);
    REQUIRE(diagnostics.summary().stalledTransfers[2] == 1);
  }

  SECTION("Unmasking does not bypass an active higher-priority packet")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    GIFPath3Transfer path3(arbiter);
    VIF vif(VIFType::VIF1);
    vif.attachGIFPathArbiter(&arbiter);
    const GIFQuadword packet =
      gifTag(0, true, GIFDataFormat::Packed, 0, 0);
    vif.ingestWord(vifCode(
      VIFCommandEncoding::MSKPATH3,
      VIFImmediateEncoding::MSKPATH3Mask));

    REQUIRE(path3.submitQuadwords(&packet, 1).stalled);
    REQUIRE(arbiter.requestPath(GIFPath::Path2));
    REQUIRE(!arbiter.requestPath(GIFPath::Path1));

    vif.ingestWord(vifCode(VIFCommandEncoding::MSKPATH3));
    REQUIRE(arbiter.activePath() == GIFPath::Path2);
    REQUIRE(arbiter.pathPending(GIFPath::Path1));
    REQUIRE(arbiter.pathPending(GIFPath::Path3));

    REQUIRE(arbiter.transferQuadword(
      GIFPath::Path2,
      packet).accepted);
    REQUIRE(arbiter.activePath() == GIFPath::Path1);
    REQUIRE(arbiter.transferQuadword(
      GIFPath::Path1,
      packet).accepted);
    REQUIRE(arbiter.activePath() == GIFPath::Path3);

    const GIFPath3SubmissionResult resumed =
      path3.submitQuadwords(&packet, 1);
    REQUIRE(resumed.transferredQuadwords == 1);
    REQUIRE(resumed.packetComplete);
    REQUIRE(arbiter.pathsIdle());
  }
}
