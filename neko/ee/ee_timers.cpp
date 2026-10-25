#include "ee_timers.hpp"
#include "clock_rates.hpp"

#include <stdexcept>

bool EETimers::decode(
  std::uint32_t address, std::size_t *index, std::uint32_t *offset)
{
  if (address < EETimerRegisterMap::BASE ||
      address >= EETimerRegisterMap::BASE +
        EETimerRegisterMap::STRIDE * TIMER_COUNT)
  {
    return false;
  }
  const std::uint32_t relative = address - EETimerRegisterMap::BASE;
  *index = relative / EETimerRegisterMap::STRIDE;
  *offset = relative % EETimerRegisterMap::STRIDE;
  return *offset == EETimerRegisterMap::COUNT ||
    *offset == EETimerRegisterMap::MODE ||
    *offset == EETimerRegisterMap::COMP ||
    (*offset == EETimerRegisterMap::HOLD && *index < 2);
}

bool EETimers::isRegisterAddress(std::uint32_t address)
{
  std::size_t index = 0;
  std::uint32_t offset = 0;
  return decode(address, &index, &offset);
}

std::uint32_t EETimers::readRegister(std::uint32_t address) const
{
  std::size_t index = 0;
  std::uint32_t offset = 0;
  if (!decode(address, &index, &offset))
  {
    throw std::invalid_argument("Invalid EE timer register address.");
  }
  const Timer &timer = timers[index];
  switch (offset)
  {
    case EETimerRegisterMap::COUNT: return timer.count;
    case EETimerRegisterMap::MODE: return timer.mode;
    case EETimerRegisterMap::COMP: return timer.compare;
    case EETimerRegisterMap::HOLD: return timer.hold;
  }
  throw std::logic_error("Decoded EE timer register is invalid.");
}

std::uint64_t EETimers::clockPeriod(unsigned clock)
{
  constexpr std::array<std::uint64_t, 4> periods{{
    PS2Clock::EE_BUS_CLOCK_PERIOD,
    PS2Clock::EE_BUS_CLOCK_PERIOD * 16,
    PS2Clock::EE_BUS_CLOCK_PERIOD * 256,
    0
  }};
  return periods.at(clock);
}

bool EETimers::increment(Timer *timer)
{
  timer->count = static_cast<std::uint16_t>(timer->count + 1);
  bool request = false;
  if (timer->count == 0 &&
      (timer->mode & EETimerMode::OVERFLOW_ENABLE) != 0 &&
      (timer->mode & EETimerMode::OVERFLOW_FLAG) == 0)
  {
    timer->mode |= EETimerMode::OVERFLOW_FLAG;
    request = true;
  }
  if (timer->count == timer->compare)
  {
    if ((timer->mode & EETimerMode::COMPARE_ENABLE) != 0 &&
        (timer->mode & EETimerMode::EQUAL_FLAG) == 0)
    {
      timer->mode |= EETimerMode::EQUAL_FLAG;
      request = true;
    }
    if ((timer->mode & EETimerMode::ZERO_RETURN) != 0)
    {
      timer->count = 0;
    }
  }
  return request;
}

std::uint8_t EETimers::advance(
  std::uint64_t masterCycle, const EETimerGateLevels &levels)
{
  std::uint8_t requests = 0;
  const bool hBlankRising = !previousLevels.hBlank && levels.hBlank;
  for (std::size_t index = 0; index < timers.size(); ++index)
  {
    Timer &timer = timers[index];
    const unsigned clock = timer.mode & EETimerMode::CLOCK;
    const bool gated = (timer.mode & EETimerMode::GATE) != 0;
    const bool vBlankGate = (timer.mode & EETimerMode::VBLANK_GATE) != 0;
    if (gated && !vBlankGate && clock == 3)
    {
      continue;
    }
    const bool previous = vBlankGate ?
      previousLevels.vBlank : previousLevels.hBlank;
    const bool current = vBlankGate ? levels.vBlank : levels.hBlank;
    const unsigned gateMode = (timer.mode & EETimerMode::GATE_MODE) >> 4;
    if (gated && previous != current &&
        (gateMode == 3 || (gateMode == 1 && current) ||
         (gateMode == 2 && !current)))
    {
      // Functional policy: a gate reset dominates a coincident clock pulse.
      timer.count = 0;
      continue;
    }
    if ((timer.mode & EETimerMode::COUNT_ENABLE) == 0 ||
        (gated && gateMode == 0 && current))
    {
      continue;
    }
    const auto period = clockPeriod(clock);
    const bool pulse = clock == 3 ? hBlankRising :
      masterCycle % period == 0;
    if (pulse && increment(&timer))
    {
      requests |= static_cast<std::uint8_t>(1u << index);
    }
  }
  previousLevels = levels;
  return requests;
}

void EETimers::captureSBUS() noexcept
{
  timers[0].hold = timers[0].count;
  timers[1].hold = timers[1].count;
}

void EETimers::writeRegister(std::uint32_t address, std::uint32_t value)
{
  std::size_t index = 0;
  std::uint32_t offset = 0;
  if (!decode(address, &index, &offset))
  {
    throw std::invalid_argument("Invalid EE timer register address.");
  }
  Timer &timer = timers[index];
  switch (offset)
  {
    case EETimerRegisterMap::COUNT:
      timer.count = static_cast<std::uint16_t>(value);
      return;
    case EETimerRegisterMap::MODE:
      timer.mode = static_cast<std::uint16_t>(
        (timer.mode & EETimerMode::FLAGS & ~value) |
        (value & EETimerMode::CONTROLS));
      return;
    case EETimerRegisterMap::COMP:
      timer.compare = static_cast<std::uint16_t>(value);
      return;
    case EETimerRegisterMap::HOLD:
      timer.hold = static_cast<std::uint16_t>(value);
      return;
  }
  throw std::logic_error("Decoded EE timer register is invalid.");
}
