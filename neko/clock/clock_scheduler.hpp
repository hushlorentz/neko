#ifndef CLOCK_SCHEDULER_HPP
#define CLOCK_SCHEDULER_HPP

#include <cstdint>
#include <vector>

#include "clocked_component.hpp"

class ClockScheduler
{
  public:
    std::uint32_t run(
      ClockedComponent &component,
      std::uint32_t maxTicks) const;
    void runUntilInactive(ClockedComponent &component) const;
};

class MasterClockScheduler
{
  public:
    void registerComponent(
      ClockedComponent &component,
      std::uint64_t period,
      std::uint64_t phase = 0);
    void clock();
    std::uint64_t run(std::uint64_t cycleCount);
    std::uint64_t currentCycle() const;

  private:
    struct ScheduledComponent
    {
      ClockedComponent *component = nullptr;
      std::uint64_t period = 0;
      std::uint64_t phase = 0;
    };

    std::vector<ScheduledComponent> components;
    std::uint64_t masterCycle = 0;
};

#endif
