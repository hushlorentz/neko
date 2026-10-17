#include <cstdint>
#include <type_traits>

#include "catch.hpp"
#include "gs.hpp"
#include "gs_display.hpp"
#include "iop_timer_timing.hpp"

static_assert(std::is_trivially_copyable<IOPTimerTiming>::value,
  "IOP timing phase must remain a fixed-capacity value.");

TEST_CASE("IOP timing produces exact rational pixel pulses without rounded drift",
  "[iop][timing]")
{
  IOPTimerTiming timing;
  std::uint64_t pixels = 0;
  for (std::uint64_t cycle = 1; cycle <= 8192 * 3; ++cycle)
  {
    const bool systemPulse = (cycle - 1) % 8 == 0;
    const IOPTimerGateLevels gates = {cycle % 7 < 3, cycle % 11 < 5};
    const auto sample = timing.advance(systemPulse, gates);
    pixels += sample.pixelClockPulse;
    REQUIRE(pixels == cycle * 13500000 / 294912000);
    REQUIRE(sample.systemClockPulse == systemPulse);
    REQUIRE(sample.gates.horizontalBlank == gates.horizontalBlank);
    REQUIRE(sample.gates.verticalBlank == gates.verticalBlank);
    REQUIRE(timing.pixelPhase() == cycle * 375 % 8192);
    if (cycle < 22)
    {
      REQUIRE_FALSE(sample.pixelClockPulse);
    }
    if (cycle == 22)
    {
      REQUIRE(sample.pixelClockPulse);
    }
  }
  REQUIRE(pixels == 1125);
  REQUIRE(timing.pixelPhase() == 0);

  for (unsigned int period = 0; period < 1000; ++period)
  {
    for (unsigned int cycle = 0; cycle < 8192; ++cycle)
    {
      pixels += timing.advance(false, {}).pixelClockPulse;
    }
    REQUIRE(pixels == 1125 + (period + 1) * 375);
    REQUIRE(timing.pixelPhase() == 0);
  }
}

TEST_CASE("IOP timing copies and reset preserve a complete deterministic phase",
  "[iop][timing]")
{
  IOPTimerTiming timing;
  for (unsigned int cycle = 0; cycle < 37; ++cycle)
  {
    timing.advance(false, {});
  }
  REQUIRE(timing.pixelPhase() != 0);
  IOPTimerTiming copy = timing;
  for (unsigned int cycle = 0; cycle < 1000; ++cycle)
  {
    const auto original = timing.advance(cycle % 8 == 0, {true, false});
    const auto repeated = copy.advance(cycle % 8 == 0, {true, false});
    REQUIRE(original.pixelClockPulse == repeated.pixelClockPulse);
    REQUIRE(original.systemClockPulse == repeated.systemClockPulse);
    REQUIRE(original.gates.horizontalBlank == repeated.gates.horizontalBlank);
    REQUIRE(timing.pixelPhase() == copy.pixelPhase());
  }
  timing.reset();
  REQUIRE(timing.pixelPhase() == 0);
  IOPTimerTiming fresh;
  for (unsigned int cycle = 0; cycle < 1000; ++cycle)
  {
    REQUIRE(timing.advance(false, {}).pixelClockPulse ==
      fresh.advance(false, {}).pixelClockPulse);
  }
}

TEST_CASE("IOP timing samples post-display levels without consuming EE blank edges",
  "[iop][timing][gs]")
{
  GS gs;
  GSDisplay display(&gs);
  display.configureTiming({3, 5, {3, 7, 2}});
  IOPTimerTiming timing;
  IOPTimers timers;
  timers.writeMode(0, 0x101); // Pixel source, count outside H-blank.
  timers.writeMode(1, 0x100); // H-blank source.
  timers.writeMode(3, 0x101); // H-blank source, count outside V-blank.
  IOPWord pixelCount = 0;
  IOPWord horizontalCount = 0;
  IOPWord gatedHorizontalCount = 0;
  bool oldHorizontalBlank = false;
  for (std::uint64_t cycle = 1; cycle <= 10000; ++cycle)
  {
    display.clock();
    const bool horizontalBlank = (cycle * 2 % 7) >= 3;
    const bool verticalBlank = (cycle % 5) >= 3;
    const bool pixel = cycle * 375 / 8192 != (cycle - 1) * 375 / 8192;
    if (pixel && !horizontalBlank)
    {
      ++pixelCount;
    }
    if (horizontalBlank && !oldHorizontalBlank)
    {
      ++horizontalCount;
      if (!verticalBlank)
      {
        ++gatedHorizontalCount;
      }
    }
    oldHorizontalBlank = horizontalBlank;
    REQUIRE(display.takeVerticalBlankStart() == (cycle % 5 == 3));
    REQUIRE(display.takeVerticalBlankEnd() == (cycle % 5 == 0));
    timers.advance(timing.advance((cycle - 1) % 8 == 0,
      {display.inHorizontalBlank(), display.inVerticalBlank()}));
    REQUIRE(timers.counter(0).count == pixelCount);
    REQUIRE(timers.counter(1).count == horizontalCount);
    REQUIRE(timers.counter(3).count == gatedHorizontalCount);
  }
}
