#include "iop_timer_timing.hpp"
#include "clock_rates.hpp"

void IOPTimerTiming::reset()
{
  phase = 0;
}

IOPTimerTimingSample IOPTimerTiming::advance(
  bool systemClockPulse,
  const IOPTimerGateLevels &gates)
{
  const std::uint16_t next = phase + PS2Clock::IOP_PIXEL_INCREMENT;
  const bool pixelPulse = next >= PS2Clock::IOP_PIXEL_PERIOD;
  phase = pixelPulse ? next - PS2Clock::IOP_PIXEL_PERIOD : next;
  return {systemClockPulse, pixelPulse, gates};
}

std::uint16_t IOPTimerTiming::pixelPhase() const
{
  return phase;
}
