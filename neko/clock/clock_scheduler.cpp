#include "clock_scheduler.hpp"

#include <stdexcept>

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

void MasterClockScheduler::registerComponent(
  ClockedComponent &component,
  std::uint64_t period,
  std::uint64_t phase)
{
  if (period == 0)
  {
    throw std::invalid_argument(
      "A scheduled clock period must be nonzero.");
  }
  if (phase >= period)
  {
    throw std::invalid_argument(
      "A scheduled clock phase must be less than its period.");
  }
  for (const ScheduledComponent &scheduled : components)
  {
    if (scheduled.component == &component)
    {
      throw std::invalid_argument(
        "A component cannot be registered more than once.");
    }
  }

  components.push_back(ScheduledComponent{
    &component,
    period,
    phase
  });
}

void MasterClockScheduler::clock()
{
  for (ScheduledComponent &scheduled : components)
  {
    if (masterCycle % scheduled.period == scheduled.phase &&
        scheduled.component->clockActive())
    {
      scheduled.component->clock();
    }
  }
  ++masterCycle;
}

std::uint64_t MasterClockScheduler::run(
  std::uint64_t cycleCount)
{
  for (std::uint64_t cycle = 0;
       cycle < cycleCount;
       ++cycle)
  {
    clock();
  }
  return cycleCount;
}

std::uint64_t MasterClockScheduler::currentCycle() const
{
  return masterCycle;
}
