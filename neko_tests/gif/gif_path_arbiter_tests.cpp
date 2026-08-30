#include <cstdint>

#include "catch.hpp"
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
