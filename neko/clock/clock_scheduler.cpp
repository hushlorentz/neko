#include "clock_scheduler.hpp"

std::uint32_t ClockScheduler::run(
  ClockedComponent &component,
  std::uint32_t maxTicks) const
{
  std::uint32_t executedTicks = 0;

  while (component.clockActive() && executedTicks < maxTicks)
  {
    component.clock();
    executedTicks++;
  }

  return executedTicks;
}

void ClockScheduler::runUntilInactive(ClockedComponent &component) const
{
  while (component.clockActive())
  {
    component.clock();
  }
}
