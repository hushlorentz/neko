#include <cstdint>

#include "catch.hpp"
#include "gif_registers.hpp"

namespace
{
  GIFQuadword gifTag(
    std::uint16_t loopCount,
    bool endOfPacket,
    GIFDataFormat format)
  {
    const std::uint64_t low =
      loopCount |
      (static_cast<std::uint64_t>(endOfPacket) << 15) |
      (static_cast<std::uint64_t>(format) << 58);
    return GIFQuadword{{
      static_cast<std::uint32_t>(low),
      static_cast<std::uint32_t>(low >> 32),
      0,
      0
    }};
  }
}

TEST_CASE("GIF Privileged Register Tests")
{
  SECTION("The register boundary validates its arbiter")
  {
    REQUIRE_THROWS_WITH(
      GIFRegisters(nullptr),
      "GIF registers require a non-null path arbiter.");
  }

  SECTION("GIF_MODE controls independent PATH3 mode and register masks")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    GIFRegisters registers(&arbiter);

    registers.writeMode(GIFMode::M3R | GIFMode::IMT);
    REQUIRE(arbiter.path3MaskedByMode());
    REQUIRE(!arbiter.path3MaskedByVIF());
    REQUIRE(arbiter.path3IntermittentMode());
    REQUIRE(
      (registers.readStatus() &
       (GIFStatus::M3R | GIFStatus::IMT)) ==
      (GIFStatus::M3R | GIFStatus::IMT));

    REQUIRE(!arbiter.requestPath(GIFPath::Path3));
    arbiter.setPath3MaskedByVIF(true);
    registers.writeMode(0);
    REQUIRE(!arbiter.path3MaskedByMode());
    REQUIRE(arbiter.path3MaskedByVIF());
    REQUIRE(!arbiter.path3IntermittentMode());
    REQUIRE(arbiter.activePath() == GIFPath::Idle);
    REQUIRE(
      (registers.readStatus() &
       (GIFStatus::M3R | GIFStatus::M3P | GIFStatus::IMT)) ==
      GIFStatus::M3P);

    arbiter.setPath3MaskedByVIF(false);
    REQUIRE(arbiter.activePath() == GIFPath::Path3);
  }

  SECTION("GIF_STAT reports active and queued paths")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    GIFRegisters registers(&arbiter);

    REQUIRE(registers.readStatus() == 0);
    REQUIRE(arbiter.requestPath(GIFPath::Path3));
    REQUIRE(!arbiter.requestPath(GIFPath::Path2));
    REQUIRE(!arbiter.requestPath(GIFPath::Path1));

    const std::uint32_t status = registers.readStatus();
    REQUIRE((status & GIFStatus::OPH) != 0);
    REQUIRE(
      (status & GIFStatus::APATH_MASK) ==
      (static_cast<std::uint32_t>(GIFPath::Path3) <<
       GIFStatus::APATH_SHIFT));
    REQUIRE((status & GIFStatus::P1Q) != 0);
    REQUIRE((status & GIFStatus::P2Q) != 0);
    REQUIRE((status & GIFStatus::P3Q) == 0);
  }

  SECTION("Interrupted IMAGE state is exposed and retained")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    GIFRegisters registers(&arbiter);
    registers.writeMode(GIFMode::IMT);
    const GIFQuadword tag =
      gifTag(10, true, GIFDataFormat::Image);
    const GIFQuadword payload = {{1, 2, 3, 4}};

    REQUIRE(arbiter.transferQuadword(
      GIFPath::Path3,
      tag).accepted);
    REQUIRE(!arbiter.requestPath(GIFPath::Path1));
    for (std::uint8_t index = 0; index < 8; ++index)
    {
      REQUIRE(arbiter.transferQuadword(
        GIFPath::Path3,
        payload).accepted);
    }

    const std::uint32_t interruptedStatus =
      registers.readStatus();
    REQUIRE((interruptedStatus & GIFStatus::IP3) != 0);
    REQUIRE((interruptedStatus & GIFStatus::P3Q) != 0);
    REQUIRE(
      (interruptedStatus & GIFStatus::APATH_MASK) ==
      (static_cast<std::uint32_t>(GIFPath::Path1) <<
       GIFStatus::APATH_SHIFT));
    REQUIRE(registers.readPath3Count() == 2);
    REQUIRE(registers.readPath3Tag() == (10u | (1u << 15)));

    REQUIRE(arbiter.transferQuadword(
      GIFPath::Path1,
      gifTag(0, true, GIFDataFormat::Packed)).accepted);
    REQUIRE((registers.readStatus() & GIFStatus::IP3) == 0);
    REQUIRE(registers.readPath3Count() == 2);
    REQUIRE(registers.readPath3Tag() == (10u | (1u << 15)));
  }
}
