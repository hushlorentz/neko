#ifndef IOP_TIMER_TIMING_HPP
#define IOP_TIMER_TIMING_HPP

#include <cstdint>

#include "iop_timers.hpp"

// Advance once per master cycle, after display levels have advanced.
// System pulses come from the caller's scheduler, not CPU execution.
class IOPTimerTiming final
{
  public:
    void reset();
    IOPTimerTimingSample advance(
      bool systemClockPulse,
      const IOPTimerGateLevels &gates);
    std::uint16_t pixelPhase() const;

  private:
    friend class NekoSaveStateCodec;
    std::uint16_t phase = 0;
};

#endif
