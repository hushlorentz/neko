#include <cstdint>

#include "catch.hpp"
#include "clock_scheduler.hpp"
#include "gif_path_arbiter.hpp"

namespace
{
  GIFQuadword gifTag(bool endOfPacket)
  {
    const std::uint64_t low =
      static_cast<std::uint64_t>(endOfPacket) << 15;
    return GIFQuadword{{
      static_cast<std::uint32_t>(low),
      static_cast<std::uint32_t>(low >> 32),
      0,
      0
    }};
  }

  GIFQuadword timedGIFTag(
    std::uint16_t loopCount,
    bool endOfPacket,
    bool primitiveEnabled,
    GIFDataFormat format)
  {
    const std::uint64_t low =
      loopCount |
      (static_cast<std::uint64_t>(endOfPacket) << 15) |
      (static_cast<std::uint64_t>(primitiveEnabled) << 46) |
      (static_cast<std::uint64_t>(format) << 58) |
      (1ull << 60);
    return GIFQuadword{{
      static_cast<std::uint32_t>(low),
      static_cast<std::uint32_t>(low >> 32),
      GIFRegisterDescriptor::NOP,
      0
    }};
  }
}

TEST_CASE("GIF Path Arbitration Tests")
{
  SECTION("The arbiter validates requests and starts idle")
  {
    REQUIRE_THROWS_WITH(
      GIFPathArbiter(nullptr),
      "GIF path arbitration requires a non-null decoder.");

    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    REQUIRE(arbiter.activePath() == GIFPath::Idle);
    REQUIRE(arbiter.pathsIdle());
    REQUIRE_THROWS_WITH(
      arbiter.requestPath(GIFPath::Idle),
      "The idle GIF path cannot request a transfer.");
    REQUIRE_THROWS_WITH(
      arbiter.pathPending(GIFPath::Idle),
      "Invalid GIF transfer path.");
  }

  SECTION("Packets retain ownership until EOP")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);

    const GIFPathTransferResult first =
      arbiter.transferQuadword(
        GIFPath::Path2,
        gifTag(false));
    REQUIRE(first.accepted);
    REQUIRE(!first.decodeResult.packetComplete);
    REQUIRE(arbiter.activePath() == GIFPath::Path2);

    const GIFPathTransferResult rejected =
      arbiter.transferQuadword(
        GIFPath::Path1,
        gifTag(true));
    REQUIRE(!rejected.accepted);
    REQUIRE(arbiter.activePath() == GIFPath::Path2);
    REQUIRE(arbiter.pathPending(GIFPath::Path1));
    REQUIRE(decoder.packetInProgress());

    const GIFPathTransferResult completed =
      arbiter.transferQuadword(
        GIFPath::Path2,
        gifTag(true));
    REQUIRE(completed.accepted);
    REQUIRE(completed.decodeResult.packetComplete);
    REQUIRE(arbiter.activePath() == GIFPath::Path1);
  }

  SECTION("Queued paths are selected in PATH1 to PATH3 priority order")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    REQUIRE(arbiter.requestPath(GIFPath::Path3));
    REQUIRE(!arbiter.requestPath(GIFPath::Path2));
    REQUIRE(!arbiter.requestPath(GIFPath::Path1));

    arbiter.transferQuadword(GIFPath::Path3, gifTag(true));
    REQUIRE(arbiter.activePath() == GIFPath::Path1);
    arbiter.transferQuadword(GIFPath::Path1, gifTag(true));
    REQUIRE(arbiter.activePath() == GIFPath::Path2);
    arbiter.transferQuadword(GIFPath::Path2, gifTag(true));
    REQUIRE(arbiter.activePath() == GIFPath::Idle);
    REQUIRE(arbiter.pathsIdle());
  }

  SECTION("PATH1 and PATH2 idleness excludes PATH3")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    arbiter.requestPath(GIFPath::Path3);

    REQUIRE(arbiter.pathsIdle(false));
    REQUIRE(!arbiter.pathsIdle(true));
  }

  SECTION("The VIF PATH3 mask defers queued PATH3 work")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    arbiter.setPath3MaskedByVIF(true);

    REQUIRE(!arbiter.requestPath(GIFPath::Path3));
    REQUIRE(arbiter.activePath() == GIFPath::Idle);
    REQUIRE(arbiter.pathPending(GIFPath::Path3));
    REQUIRE(arbiter.path3MaskedByVIF());

    arbiter.setPath3MaskedByVIF(false);
    REQUIRE(arbiter.activePath() == GIFPath::Path3);
    REQUIRE(!arbiter.path3MaskedByVIF());
  }
}

TEST_CASE("GIF Cycle Timing Tests")
{
  SECTION("Selecting a path incurs one arbitration cycle")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    arbiter.setCycleTimingEnabled(true);
    ClockScheduler scheduler;

    REQUIRE(!arbiter.transferQuadword(
      GIFPath::Path3,
      gifTag(true)).accepted);
    REQUIRE(arbiter.activePath() == GIFPath::Path3);
    REQUIRE(arbiter.idleCyclesRemaining() == 1);
    REQUIRE(scheduler.run(arbiter, 1) == 1);
    REQUIRE(arbiter.transferQuadword(
      GIFPath::Path3,
      gifTag(true)).accepted);
  }

  SECTION("A GIFtag without a PRIM output incurs one idle cycle")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    arbiter.setCycleTimingEnabled(true);
    ClockScheduler scheduler;
    const GIFQuadword tag = timedGIFTag(
      1,
      true,
      false,
      GIFDataFormat::Packed);
    const GIFQuadword payload = {{0, 0, 0, 0}};

    REQUIRE(!arbiter.transferQuadword(
      GIFPath::Path3,
      tag).accepted);
    scheduler.runUntilInactive(arbiter);
    REQUIRE(arbiter.transferQuadword(
      GIFPath::Path3,
      tag).accepted);
    REQUIRE(arbiter.idleCyclesRemaining() == 1);
    REQUIRE(!arbiter.transferQuadword(
      GIFPath::Path3,
      payload).accepted);
    scheduler.runUntilInactive(arbiter);
    REQUIRE(arbiter.transferQuadword(
      GIFPath::Path3,
      payload).accepted);
  }

  SECTION("A PACKED GIFtag with PRE outputs PRIM without a tag idle cycle")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    arbiter.setCycleTimingEnabled(true);
    ClockScheduler scheduler;
    const GIFQuadword tag = timedGIFTag(
      1,
      true,
      true,
      GIFDataFormat::Packed);
    const GIFQuadword payload = {{0, 0, 0, 0}};

    REQUIRE(!arbiter.transferQuadword(
      GIFPath::Path3,
      tag).accepted);
    scheduler.runUntilInactive(arbiter);
    const GIFPathTransferResult decodedTag =
      arbiter.transferQuadword(GIFPath::Path3, tag);
    REQUIRE(decodedTag.accepted);
    REQUIRE(decodedTag.decodeResult.writes.size() == 1);
    REQUIRE(
      decodedTag.decodeResult.writes[0].address ==
      GIFRegisterAddress::PRIM);
    REQUIRE(arbiter.idleCyclesRemaining() == 0);
    REQUIRE(arbiter.transferQuadword(
      GIFPath::Path3,
      payload).accepted);
  }

  SECTION("A packet handoff incurs one arbitration cycle")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    arbiter.setCycleTimingEnabled(true);
    ClockScheduler scheduler;

    REQUIRE(arbiter.requestPath(GIFPath::Path3));
    scheduler.runUntilInactive(arbiter);
    REQUIRE(arbiter.transferQuadword(
      GIFPath::Path3,
      timedGIFTag(
        1,
        true,
        true,
        GIFDataFormat::Packed)).accepted);
    REQUIRE(!arbiter.requestPath(GIFPath::Path1));
    REQUIRE(arbiter.transferQuadword(
      GIFPath::Path3,
      GIFQuadword{{0, 0, 0, 0}}).accepted);
    REQUIRE(arbiter.activePath() == GIFPath::Path1);
    REQUIRE(arbiter.idleCyclesRemaining() == 1);

    REQUIRE(!arbiter.transferQuadword(
      GIFPath::Path1,
      gifTag(true)).accepted);
    REQUIRE(scheduler.run(arbiter, 1) == 1);
    REQUIRE(arbiter.transferQuadword(
      GIFPath::Path1,
      gifTag(true)).accepted);
  }
}

TEST_CASE("GIF Three-Path Priority Matrix Tests")
{
  struct PriorityContract
  {
    GIFPath initial;
    GIFPath firstRequest;
    GIFPath secondRequest;
    GIFPath expectedNext;
    GIFPath expectedLast;
  };
  const std::array<PriorityContract, 6> contracts = {{
    {
      GIFPath::Path1,
      GIFPath::Path2,
      GIFPath::Path3,
      GIFPath::Path2,
      GIFPath::Path3
    },
    {
      GIFPath::Path1,
      GIFPath::Path3,
      GIFPath::Path2,
      GIFPath::Path2,
      GIFPath::Path3
    },
    {
      GIFPath::Path2,
      GIFPath::Path1,
      GIFPath::Path3,
      GIFPath::Path1,
      GIFPath::Path3
    },
    {
      GIFPath::Path2,
      GIFPath::Path3,
      GIFPath::Path1,
      GIFPath::Path1,
      GIFPath::Path3
    },
    {
      GIFPath::Path3,
      GIFPath::Path1,
      GIFPath::Path2,
      GIFPath::Path1,
      GIFPath::Path2
    },
    {
      GIFPath::Path3,
      GIFPath::Path2,
      GIFPath::Path1,
      GIFPath::Path1,
      GIFPath::Path2
    }
  }};

  for (const PriorityContract &contract : contracts)
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    REQUIRE(arbiter.transferQuadword(
      contract.initial,
      gifTag(false)).accepted);
    REQUIRE(!arbiter.requestPath(contract.firstRequest));
    REQUIRE(!arbiter.requestPath(contract.secondRequest));
    REQUIRE(arbiter.activePath() == contract.initial);

    REQUIRE(arbiter.transferQuadword(
      contract.initial,
      gifTag(true)).accepted);
    REQUIRE(arbiter.activePath() == contract.expectedNext);

    REQUIRE(arbiter.transferQuadword(
      contract.expectedNext,
      gifTag(true)).accepted);
    REQUIRE(arbiter.activePath() == contract.expectedLast);

    REQUIRE(arbiter.transferQuadword(
      contract.expectedLast,
      gifTag(true)).accepted);
    REQUIRE(arbiter.pathsIdle());
  }
}
