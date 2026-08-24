#ifndef CLOCK_SCHEDULER_HPP
#define CLOCK_SCHEDULER_HPP

#include <cstdint>

#include "clocked_component.hpp"

class ClockScheduler
{
  public:
    std::uint32_t run(
      ClockedComponent &component,
      std::uint32_t maxTicks) const;
    void runUntilInactive(ClockedComponent &component) const;
};

#endif
