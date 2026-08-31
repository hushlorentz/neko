#include <cstdint>
#include <sstream>
#include <vector>

#include "catch.hpp"
#include "gif_diagnostics.hpp"

namespace
{
  GIFQuadword gifTag(
    std::uint16_t loopCount,
    bool endOfPacket,
    std::uint8_t registerCount,
    std::uint64_t registers)
  {
    const std::uint64_t low =
      loopCount |
      (static_cast<std::uint64_t>(endOfPacket) << 15) |
      (static_cast<std::uint64_t>(
        GIFDataFormat::Packed) << 58) |
      (static_cast<std::uint64_t>(registerCount) << 60);
    return GIFQuadword{{
      static_cast<std::uint32_t>(low),
      static_cast<std::uint32_t>(low >> 32),
      static_cast<std::uint32_t>(registers),
      static_cast<std::uint32_t>(registers >> 32)
    }};
  }
}

TEST_CASE("GIF Diagnostics Tests")
{
  SECTION("Tracing is opt-in and does not alter transfers")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    std::size_t eventCount = 0;

    const GIFPathTransferResult untraced =
      arbiter.transferQuadword(
        GIFPath::Path2,
        gifTag(0, true, 0, 0));

    REQUIRE(untraced.accepted);
    REQUIRE(untraced.decodeResult.packetComplete);
    REQUIRE(eventCount == 0);

    arbiter.setTraceCallback(
      [&eventCount](const GIFTraceEvent &) {
        ++eventCount;
      });
    const GIFPathTransferResult traced =
      arbiter.transferQuadword(
        GIFPath::Path2,
        gifTag(0, true, 0, 0));

    REQUIRE(traced.accepted);
    REQUIRE(traced.decodeResult.packetComplete);
    REQUIRE(eventCount == 7);
  }

  SECTION("A decoded packet produces structured events and a summary")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    GIFDiagnosticsRecorder recorder;
    arbiter.setTraceCallback(
      [&recorder](const GIFTraceEvent &event) {
        recorder.observe(event);
      });

    REQUIRE(arbiter.transferQuadword(
      GIFPath::Path2,
      gifTag(1, true, 1, GIFRegisterDescriptor::AD)).accepted);
    REQUIRE(arbiter.transferQuadword(
      GIFPath::Path2,
      GIFQuadword{{0x12345678, 0x9abcdef0, 0x42, 0}}).accepted);

    const std::vector<GIFTraceEvent> &events = recorder.events();
    REQUIRE(events.size() == 10);
    REQUIRE(events[3].type == GIFTraceEventType::TagDecoded);
    REQUIRE(events[3].tag.loopCount == 1);
    REQUIRE(events[3].tag.endOfPacket);
    REQUIRE(events[3].tag.format == GIFDataFormat::Packed);
    REQUIRE(events[3].tag.registerCount == 1);
    REQUIRE(events[3].tag.registers == GIFRegisterDescriptor::AD);
    REQUIRE(
      events[5].quadword ==
      GIFQuadword{{0x12345678, 0x9abcdef0, 0x42, 0}});
    REQUIRE(events[6].type == GIFTraceEventType::RegisterWrite);
    REQUIRE(events[6].registerWrite.address == 0x42);
    REQUIRE(
      events[6].registerWrite.data ==
      UINT64_C(0x9abcdef012345678));

    const GIFTransferSummary &summary = recorder.summary();
    REQUIRE(summary.pathRequests[1] == 2);
    REQUIRE(summary.pathSelections[1] == 1);
    REQUIRE(summary.transferredQuadwords[1] == 2);
    REQUIRE(summary.stalledTransfers[1] == 0);
    REQUIRE(summary.decodedTags == 1);
    REQUIRE(summary.registerWrites == 1);
    REQUIRE(summary.completedPrimitives == 1);
    REQUIRE(summary.completedPackets == 1);
    REQUIRE(summary.pathReleases == 1);

    std::ostringstream tagOutput;
    writeGIFTraceEventJsonLine(tagOutput, events[3]);
    REQUIRE(tagOutput.str() ==
      "{\"type\":\"tag_decoded\",\"path\":\"path2\","
      "\"tag\":{\"nloop\":1,\"eop\":true,\"pre\":false,"
      "\"prim\":0,\"flg\":0,\"nreg\":1,\"registers\":14,"
      "\"descriptors\":[14]}}\n");

    std::ostringstream summaryOutput;
    writeGIFTransferSummaryJsonLine(summaryOutput, summary);
    REQUIRE(summaryOutput.str() ==
      "{\"type\":\"gif_transfer_summary\","
      "\"requests\":{\"path1\":0,\"path2\":2,\"path3\":0},"
      "\"selections\":{\"path1\":0,\"path2\":1,\"path3\":0},"
      "\"transferred_qwords\":{\"path1\":0,\"path2\":2,"
      "\"path3\":0},\"stalled_transfers\":{\"path1\":0,"
      "\"path2\":0,\"path3\":0},\"decoded_tags\":1,"
      "\"register_writes\":1,\"completed_primitives\":1,"
      "\"completed_packets\":1,\"path_releases\":1,"
      "\"path3_mask_changes\":0,\"path3_interruptions\":0,"
      "\"path3_resumptions\":0}\n");
  }

  SECTION("Stalls and PATH3 mask transitions identify their paths")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    GIFDiagnosticsRecorder recorder(false);
    arbiter.setTraceCallback(
      [&recorder](const GIFTraceEvent &event) {
        recorder.observe(event);
      });

    REQUIRE(arbiter.transferQuadword(
      GIFPath::Path2,
      gifTag(0, false, 0, 0)).accepted);
    REQUIRE(!arbiter.transferQuadword(
      GIFPath::Path1,
      gifTag(0, true, 0, 0)).accepted);
    arbiter.setPath3MaskedByVIF(true);
    arbiter.setPath3MaskedByVIF(false);

    REQUIRE(recorder.events().empty());
    REQUIRE(recorder.summary().stalledTransfers[0] == 1);
    REQUIRE(recorder.summary().path3MaskChanges == 2);
  }
}
