#include "iop_timers.hpp"

#include <stdexcept>

constexpr std::size_t IOPTimers::COUNTER_COUNT;

namespace
{
  const std::array<IOPTimerCapabilities, IOPTimers::COUNTER_COUNT>
    timerCapabilities = {{
      {16, IOPTimerSource::Pixel, IOPTimerGate::HBlank,
        IOPTimerPrescalerField::None, IOPInterruptSource::Timer0},
      {16, IOPTimerSource::HBlank, IOPTimerGate::VBlank,
        IOPTimerPrescalerField::None, IOPInterruptSource::Timer1},
      {16, IOPTimerSource::System, IOPTimerGate::None,
        IOPTimerPrescalerField::Bit9, IOPInterruptSource::Timer2},
      {32, IOPTimerSource::HBlank, IOPTimerGate::VBlank,
        IOPTimerPrescalerField::None, IOPInterruptSource::Timer3},
      {32, IOPTimerSource::System, IOPTimerGate::None,
        IOPTimerPrescalerField::Bits13And14, IOPInterruptSource::Timer4},
      {32, IOPTimerSource::System, IOPTimerGate::None,
        IOPTimerPrescalerField::Bits13And14, IOPInterruptSource::Timer5}
    }};

  IOPWord counterMask(const IOPTimerCapabilities &capability)
  {
    return UINT32_C(0xffffffff) >> (32 - capability.counterBits);
  }

  IOPTimerGateMode decodeGateMode(IOPTimerGate gate, IOPWord mode)
  {
    if ((mode & IOPTimerMode::GATE_ENABLE) == 0)
    {
      return IOPTimerGateMode::FreeRunning;
    }
    const IOPWord selector = (mode & IOPTimerMode::GATE_MODE_MASK) >> 1;
    if (gate == IOPTimerGate::None)
    {
      return (selector & 1) == 0 ?
        IOPTimerGateMode::Stopped : IOPTimerGateMode::FreeRunning;
    }
    constexpr std::array<IOPTimerGateMode, 4> gateModes = {{
      IOPTimerGateMode::CountOutsideBlank,
      IOPTimerGateMode::ClearOnBlankEnd,
      IOPTimerGateMode::CountInBlank,
      IOPTimerGateMode::StartOnBlankEnd
    }};
    return gateModes[selector];
  }

  std::uint16_t decodeDivisor(
    IOPTimerPrescalerField field, IOPWord mode)
  {
    switch (field)
    {
      case IOPTimerPrescalerField::None:
        return 1;
      case IOPTimerPrescalerField::Bit9:
        return (mode & IOPTimerMode::DIVIDE_BY_8) == 0 ? 1 : 8;
      case IOPTimerPrescalerField::Bits13And14:
      {
        constexpr std::array<std::uint16_t, 4> divisors = {{1, 8, 16, 256}};
        return divisors[(mode & IOPTimerMode::PRESCALER_MASK) >> 13];
      }
    }
    throw std::logic_error("Unknown IOP timer prescaler field.");
  }

  IOPTimerConfiguration decodeConfiguration(
    const IOPTimerCapabilities &capability, IOPWord mode)
  {
    IOPTimerConfiguration result;
    result.source = (mode & IOPTimerMode::ALTERNATE_SOURCE) == 0 ?
      IOPTimerSource::System : capability.alternateSource;
    result.gateMode = decodeGateMode(capability.gate, mode);
    result.divisor = decodeDivisor(capability.prescaler, mode);
    return result;
  }

  bool gateLevel(IOPTimerGate gate, const IOPTimerGateLevels &levels)
  {
    switch (gate)
    {
      case IOPTimerGate::None:
        return false;
      case IOPTimerGate::HBlank:
        return levels.horizontalBlank;
      case IOPTimerGate::VBlank:
        return levels.verticalBlank;
    }
    throw std::logic_error("Unknown IOP timer gate.");
  }

  void clearCounter(IOPTimerCounterState &state)
  {
    state.count = 0;
    state.prescalerRemainder = 0;
  }

  void applyGateTransition(
    IOPTimerCounterState &state, IOPTimerGateMode mode,
    bool previousBlank, bool currentBlank)
  {
    switch (mode)
    {
      case IOPTimerGateMode::CountInBlank:
        if (previousBlank != currentBlank)
        {
          clearCounter(state);
          if (currentBlank)
          {
            state.gateWaiting = false;
          }
        }
        if (!currentBlank)
        {
          clearCounter(state);
        }
        break;
      case IOPTimerGateMode::ClearOnBlankEnd:
        if (previousBlank && !currentBlank)
        {
          clearCounter(state);
        }
        break;
      case IOPTimerGateMode::StartOnBlankEnd:
        if (previousBlank && !currentBlank)
        {
          state.gateWaiting = false;
        }
        break;
      case IOPTimerGateMode::FreeRunning:
      case IOPTimerGateMode::CountOutsideBlank:
      case IOPTimerGateMode::Stopped:
        break;
    }
  }

  bool countingEnabled(
    const IOPTimerCounterState &state, IOPTimerGateMode mode, bool blank)
  {
    switch (mode)
    {
      case IOPTimerGateMode::FreeRunning:
      case IOPTimerGateMode::ClearOnBlankEnd:
        return true;
      case IOPTimerGateMode::CountOutsideBlank:
        return !blank;
      case IOPTimerGateMode::CountInBlank:
        return !state.gateWaiting && blank;
      case IOPTimerGateMode::StartOnBlankEnd:
        return !state.gateWaiting;
      case IOPTimerGateMode::Stopped:
        return false;
    }
    throw std::logic_error("Unknown IOP timer gate mode.");
  }

  bool sourcePulse(
    IOPTimerSource source, const IOPTimerTimingSample &sample,
    const IOPTimerGateLevels &previous)
  {
    switch (source)
    {
      case IOPTimerSource::System:
        return sample.systemClockPulse;
      case IOPTimerSource::Pixel:
        return sample.pixelClockPulse;
      case IOPTimerSource::HBlank:
        return !previous.horizontalBlank && sample.gates.horizontalBlank;
    }
    throw std::logic_error("Unknown IOP timer source.");
  }

  void releasePulse(IOPTimerCounterState &state)
  {
    if (state.pulseReleasePending)
    {
      state.mode |= IOPTimerMode::REQUEST_IDLE;
      state.pulseReleasePending = false;
    }
  }

  IOPWord advanceCounter(
    IOPTimerCounterState &state, const IOPTimerCapabilities &capability,
    std::uint16_t divisor)
  {
    ++state.prescalerRemainder;
    if (state.prescalerRemainder < divisor)
    {
      return 0;
    }
    state.prescalerRemainder = 0;
    const IOPWord mask = counterMask(capability);
    const bool overflow = state.count == mask;
    state.count = (state.count + 1) & mask;
    const bool target = state.count == state.target;
    const IOPWord events =
      (overflow ? IOPTimerMode::OVERFLOW_REACHED : 0) |
      (target ? IOPTimerMode::TARGET_REACHED : 0);
    state.mode |= events;
    if (target && (state.mode & IOPTimerMode::RESET_ON_TARGET) != 0)
    {
      clearCounter(state);
    }
    return events;
  }

  bool requestInterrupt(IOPTimerCounterState &state, IOPWord events)
  {
    const bool qualifies =
      ((events & IOPTimerMode::TARGET_REACHED) != 0 &&
        (state.mode & IOPTimerMode::INTERRUPT_ON_TARGET) != 0) ||
      ((events & IOPTimerMode::OVERFLOW_REACHED) != 0 &&
        (state.mode & IOPTimerMode::INTERRUPT_ON_OVERFLOW) != 0);
    const bool repeat = (state.mode & IOPTimerMode::REPEAT_INTERRUPT) != 0;
    if (!qualifies || (!repeat && state.oneShotConsumed))
    {
      return false;
    }
    if (!repeat)
    {
      state.oneShotConsumed = true;
    }
    if ((state.mode & IOPTimerMode::TOGGLE_INTERRUPT) != 0)
    {
      state.mode ^= IOPTimerMode::REQUEST_IDLE;
      return (state.mode & IOPTimerMode::REQUEST_IDLE) == 0;
    }
    state.mode &= ~IOPTimerMode::REQUEST_IDLE;
    state.pulseReleasePending = true;
    return true;
  }
}

void IOPTimers::reset()
{
  counters = {};
  blanking = {};
}

void IOPTimers::writeCount(std::size_t index, IOPWord value)
{
  counters.at(index).count = value & counterMask(capabilities(index));
}

void IOPTimers::writeTarget(std::size_t index, IOPWord value)
{
  counters.at(index).target = value & counterMask(capabilities(index));
}

void IOPTimers::writeMode(std::size_t index, IOPWord value)
{
  auto &state = counters.at(index);
  const IOPWord mode =
    (value & IOPTimerMode::WRITABLE_MASK) | IOPTimerMode::REQUEST_IDLE;
  const auto selected = decodeConfiguration(capabilities(index), mode);
  state.count = 0;
  state.mode = mode;
  state.prescalerRemainder = 0;
  state.gateWaiting =
    selected.gateMode == IOPTimerGateMode::CountInBlank ||
    selected.gateMode == IOPTimerGateMode::StartOnBlankEnd;
  state.oneShotConsumed = false;
  state.pulseReleasePending = false;
}

IOPWord IOPTimers::readMode(std::size_t index)
{
  auto &state = counters.at(index);
  const IOPWord value = state.mode;
  state.mode =
    (state.mode & ~IOPTimerMode::EVENT_MASK) | IOPTimerMode::REQUEST_IDLE;
  state.oneShotConsumed = false;
  state.pulseReleasePending = false;
  return value;
}

IOPTimers::InterruptRequests IOPTimers::advance(
  const IOPTimerTimingSample &sample)
{
  // Release only older requests, before this sample can create another one.
  if (sample.systemClockPulse)
  {
    for (auto &state : counters)
    {
      releasePulse(state);
    }
  }
  InterruptRequests requests;
  for (std::size_t index = 0; index < COUNTER_COUNT; ++index)
  {
    auto &state = counters[index];
    const auto &capability = timerCapabilities[index];
    const auto selected = decodeConfiguration(capability, state.mode);
    const bool currentBlank = gateLevel(capability.gate, sample.gates);
    applyGateTransition(state, selected.gateMode,
      gateLevel(capability.gate, blanking), currentBlank);
    if (!countingEnabled(state, selected.gateMode, currentBlank) ||
      !sourcePulse(selected.source, sample, blanking))
    {
      continue;
    }
    const IOPWord events = advanceCounter(state, capability, selected.divisor);
    if (requestInterrupt(state, events))
    {
      requests.sources[requests.count++] = capability.interruptSource;
    }
  }
  blanking = sample.gates;
  return requests;
}

const IOPTimerCounterState &IOPTimers::counter(std::size_t index) const
{
  return counters.at(index);
}

const IOPTimerGateLevels &IOPTimers::gateLevels() const
{
  return blanking;
}

IOPTimerConfiguration IOPTimers::configuration(std::size_t index) const
{
  return decodeConfiguration(capabilities(index), counter(index).mode);
}

const IOPTimerCapabilities &IOPTimers::capabilities(std::size_t index)
{
  return timerCapabilities.at(index);
}
