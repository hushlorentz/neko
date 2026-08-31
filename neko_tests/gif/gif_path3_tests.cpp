#include <array>
#include <cstdint>

#include "catch.hpp"
#include "gif_diagnostics.hpp"
#include "gif_path3.hpp"
#include "gs.hpp"

namespace
{
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
