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
