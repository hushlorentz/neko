#include "spu2_controller.hpp"

#include <algorithm>
#include <cassert>

#include "spu2_numeric.hpp"

namespace
{
  // Approved compatibility coefficients, not measured silicon behavior.
  // Provenance and exact indexing: PROJECT.md, bounded effects contract E4.
  constexpr std::array<std::int16_t, 39> downCoefficients{{
    -1,0,2,0,-10,0,35,0,-103,0,266,0,-616,0,1332,0,-2960,0,
    10246,16384,10246,0,-2960,0,1332,0,-616,0,266,0,-103,0,35,0,-10,0,2,0,-1
  }};
  const std::array<std::int16_t, 39> upCoefficients = []
  {
    auto coefficients = downCoefficients;
    for (auto &coefficient : coefficients)
      coefficient = SPU2Numeric::clipSample(std::int64_t{coefficient} * 2);
    return coefficients;
  }();

  enum class Tap : std::size_t
  {
    APF1Size = 0, APF2Size = 1, SameDestination = 2,
    Comb1Source = 4, Comb2Source = 6, SameSource = 8, DiffDestination = 10,
    Comb3Source = 12, Comb4Source = 14, DiffSource = 16,
    APF1Destination = 18, APF2Destination = 20
  };
  enum class Coefficient : std::size_t
  {
    IIR, Comb1, Comb2, Comb3, Comb4, Wall, APF1, APF2, InputLeft, InputRight
  };

  std::int64_t multiply(std::int64_t sample, std::int16_t coefficient) noexcept
  {
    return SPU2Numeric::floorQ15(sample * coefficient);
  }

  std::int16_t filter(const std::array<std::int16_t, 39> &history,
                      const std::array<std::int16_t, 39> &coefficients) noexcept
  {
    std::int64_t sum = 0;
    for (std::size_t tap = 0; tap < history.size(); ++tap)
      sum += std::int64_t{history[tap]} * coefficients[tap];
    return SPU2Numeric::clipSample(SPU2Numeric::floorQ15(sum));
  }

  void push(std::array<std::int16_t, 39> &history, std::int16_t input) noexcept
  {
    std::copy_backward(history.begin(), history.end() - 1, history.end());
    history[0] = input;
  }
}

SPU2Controller::EffectWindow SPU2Controller::effectWindow(std::size_t core) const noexcept
{
  assert(core < CORE_COUNT);
  const auto start = (std::uint32_t{effectStartAddresses[core * 2] & 0x0fu} << 16) |
    effectStartAddresses[core * 2 + 1];
  const auto end = (std::uint32_t{effectEndAddressHigh[core] & 0x0fu} << 16) | 0xffffu;
  return {start, start <= end ? end - start + 1 : 0};
}

std::uint32_t SPU2Controller::effectOffset(std::size_t core, std::size_t pair) const noexcept
{
  assert(core < CORE_COUNT && pair < 22);
  const auto index = core * 44 + pair * 2;
  return (std::uint32_t{effectOffsets[index]} << 16) | effectOffsets[index + 1];
}

std::uint32_t SPU2Controller::effectTapAddress(EffectWindow window,
  std::uint32_t position, std::int64_t offset) noexcept
{
  assert(window.size != 0 && position < window.size);
  const auto relative = (std::int64_t{position} + offset) % window.size;
  return window.start + static_cast<std::uint32_t>(
    relative < 0 ? relative + window.size : relative);
}

std::int16_t SPU2Controller::processEffectSide(
  std::size_t core, EffectSide side, std::int16_t input) noexcept
{
  assert(core < CORE_COUNT);
  const auto window = effectWindow(core);
  const auto position = effects[core].position;
  assert(window.size != 0 && position < window.size);
  const auto channel = static_cast<std::size_t>(side);
  assert(channel < 2);
  const auto tap = [this, core](Tap role, std::size_t channel)
  {
    return std::int64_t{effectOffset(core, static_cast<std::size_t>(role) + channel)};
  };
  const auto read = [this, window, position](std::int64_t offset)
  {
    return SPU2Numeric::signedSample(readSoundRAMHalfword(
      effectTapAddress(window, position, offset)));
  };
  const auto coefficient = [this, core](Coefficient role)
  {
    return SPU2Numeric::signedSample(
      effectCoefficients[core * 10 + static_cast<std::size_t>(role)]);
  };
  const auto sameDestination = tap(Tap::SameDestination, channel);
  const auto diffDestination = tap(Tap::DiffDestination, channel);
  const auto apf1Destination = tap(Tap::APF1Destination, channel);
  const auto apf2Destination = tap(Tap::APF2Destination, channel);
  const auto sameSource = read(tap(Tap::SameSource, channel));
  const auto diffSource = read(tap(Tap::DiffSource, 1 - channel));
  const auto samePrevious = read(sameDestination - 1);
  const auto diffPrevious = read(diffDestination - 1);
  const std::array<std::int16_t, 4> combSamples{{
    read(tap(Tap::Comb1Source, channel)), read(tap(Tap::Comb2Source, channel)),
    read(tap(Tap::Comb3Source, channel)), read(tap(Tap::Comb4Source, channel))
  }};
  const auto apf1Delayed = read(apf1Destination - tap(Tap::APF1Size, 0));
  const auto apf2Delayed = read(apf2Destination - tap(Tap::APF2Size, 0));
  const auto u = multiply(input, coefficient(
    side == EffectSide::Left ? Coefficient::InputLeft : Coefficient::InputRight));
  const auto same = multiply(u + multiply(sameSource, coefficient(Coefficient::Wall)) -
    samePrevious, coefficient(Coefficient::IIR)) + samePrevious;
  const auto diff = multiply(u + multiply(diffSource, coefficient(Coefficient::Wall)) -
    diffPrevious, coefficient(Coefficient::IIR)) + diffPrevious;
  std::int64_t comb = 0;
  for (std::size_t index = 0; index < combSamples.size(); ++index)
    comb += multiply(combSamples[index], SPU2Numeric::signedSample(
      effectCoefficients[core * 10 + static_cast<std::size_t>(Coefficient::Comb1) + index]));
  const auto a1 = comb - multiply(apf1Delayed, coefficient(Coefficient::APF1));
  const auto b1 = apf1Delayed + multiply(a1, coefficient(Coefficient::APF1));
  const auto a2 = b1 - multiply(apf2Delayed, coefficient(Coefficient::APF2));
  const auto b2 = apf2Delayed + multiply(a2, coefficient(Coefficient::APF2));
  if ((coreAttributes[core] & 0x80) != 0)
  {
    const auto write = [this, window, position](std::int64_t offset, std::int64_t value)
    {
      writeSoundRAMHalfword(effectTapAddress(window, position, offset),
        static_cast<std::uint16_t>(SPU2Numeric::clipSample(value)));
    };
    write(sameDestination, same);
    write(diffDestination, diff);
    write(apf1Destination, a1);
    write(apf2Destination, a2);
  }
  return SPU2Numeric::clipSample(b2);
}

std::int16_t SPU2Controller::filterEffectInput(
  std::size_t core, EffectSide side) const noexcept
{
  assert(core < CORE_COUNT && static_cast<std::size_t>(side) < 2);
  return filter(effects[core].downHistory[static_cast<std::size_t>(side)], downCoefficients);
}

void SPU2Controller::pushEffectInputs(std::size_t core, SPU2StereoSample input) noexcept
{
  assert(core < CORE_COUNT);
  push(effects[core].downHistory[0], input.left);
  push(effects[core].downHistory[1], input.right);
}

SPU2StereoSample SPU2Controller::filterEffectOutput(
  std::size_t core, EffectSide side, std::int16_t input) noexcept
{
  assert(core < CORE_COUNT && static_cast<std::size_t>(side) < 2);
  auto &history = effects[core].upHistory;
  push(history[0], side == EffectSide::Left ? input : 0);
  push(history[1], side == EffectSide::Right ? input : 0);
  return {filter(history[0], upCoefficients), filter(history[1], upCoefficients)};
}

SPU2StereoSample SPU2Controller::advanceEffect(
  std::size_t core, SPU2StereoSample input) noexcept
{
  const auto side = inputReadPosition % 2 == 0 ? EffectSide::Left : EffectSide::Right;
  const auto window = effectWindow(core);
  const auto sample = window.size != 0
    ? processEffectSide(core, side, filterEffectInput(core, side)) : std::int16_t{0};
  pushEffectInputs(core, input);
  const auto output = filterEffectOutput(core, side, sample);
  if (window.size == 0)
    effects[core].position = 0;
  else if (side == EffectSide::Right)
    effects[core].position = (effects[core].position + 1) % window.size;
  return output;
}

bool SPU2Controller::hasValidEffectState() const noexcept
{
  for (std::size_t core = 0; core < CORE_COUNT; ++core)
  {
    const auto window = effectWindow(core);
    if (window.size == 0)
    {
      if (effects[core].position != 0 || (coreAttributes[core] & 0x80) != 0)
        return false;
    }
    else if (effects[core].position >= window.size)
      return false;
  }
  return true;
}
