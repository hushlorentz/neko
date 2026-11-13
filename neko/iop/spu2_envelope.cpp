#include "spu2_envelope.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>

#include "spu2_numeric.hpp"

namespace
{
  using Phase = SPU2Envelope::Phase;
  using State = SPU2Envelope::State;

  enum class Curve : std::uint8_t
  {
    Linear, ExponentialIncrease, ExponentialDecrease
  };

  struct Rate
  {
    std::uint8_t shift;
    std::int16_t baseStep;
    Curve curve;
  };

  void assertValidState(const State &state) noexcept
  {
    assert(static_cast<std::uint8_t>(state.phase) <=
      static_cast<std::uint8_t>(Phase::Release));
    assert(state.level <= 0x7fff);
    assert(state.counter < 0x8000);
    assert(state.phase != Phase::Stopped || (state.level == 0 && state.counter == 0));
    static_cast<void>(state);
  }

  Rate rateFor(Phase phase, std::uint16_t adsr1, std::uint16_t adsr2) noexcept
  {
    switch (phase)
    {
      case Phase::Attack:
      {
        const auto rate = (adsr1 >> 8) & 0x7f;
        return {
          static_cast<std::uint8_t>(rate >> 2),
          static_cast<std::int16_t>(7 - (rate & 3)),
          (adsr1 & 0x8000) != 0 ? Curve::ExponentialIncrease : Curve::Linear
        };
      }
      case Phase::Decay:
        return {
          static_cast<std::uint8_t>((adsr1 >> 4) & 0x0f),
          -8, Curve::ExponentialDecrease
        };
      case Phase::Sustain:
      {
        const auto rate = (adsr2 >> 6) & 0x7f;
        const bool falling = (adsr2 & 0x4000) != 0;
        auto curve = Curve::Linear;
        if ((adsr2 & 0x8000) != 0)
          curve = falling ? Curve::ExponentialDecrease : Curve::ExponentialIncrease;
        return {
          static_cast<std::uint8_t>(rate >> 2),
          static_cast<std::int16_t>(falling ? -8 + (rate & 3) : 7 - (rate & 3)),
          curve
        };
      }
      case Phase::Release:
        return {
          static_cast<std::uint8_t>(adsr2 & 0x1f),
          -8, (adsr2 & 0x0020) != 0 ? Curve::ExponentialDecrease : Curve::Linear
        };
      case Phase::Stopped:
        break;
    }
    // A stopped envelope never requests a rate.
    std::abort();
  }

  void applyStep(State &state, std::int32_t step, std::uint16_t adsr1) noexcept
  {
    const auto level = static_cast<std::int32_t>(state.level) + step;
    state.level = static_cast<std::uint16_t>(
      std::max<std::int32_t>(0, std::min<std::int32_t>(0x7fff, level)));
    switch (state.phase)
    {
      case Phase::Attack:
        if (state.level == 0x7fff)
          state.phase = Phase::Decay;
        break;
      case Phase::Decay:
      {
        const auto target = std::min(0x7fff, ((adsr1 & 0x0f) + 1) * 0x800);
        if (state.level <= target)
          state.phase = Phase::Sustain;
        break;
      }
      case Phase::Release:
        if (state.level == 0)
          state.phase = Phase::Stopped;
        break;
      case Phase::Stopped:
      case Phase::Sustain:
        break;
    }
  }
}

namespace SPU2Envelope
{
  void keyOn(State &state) noexcept
  {
    state = {Phase::Attack, 0, 0};
  }

  void keyOff(State &state) noexcept
  {
    assertValidState(state);
    if (state.phase == Phase::Stopped)
      return;
    state.phase = Phase::Release;
    state.counter = 0;
  }

  void parametersWritten(State &state) noexcept
  {
    assertValidState(state);
    state.counter = 0;
  }

  void advance(State &state, std::uint16_t adsr1, std::uint16_t adsr2) noexcept
  {
    assertValidState(state);
    if (state.phase == Phase::Stopped)
      return;

    const auto rate = rateFor(state.phase, adsr1, adsr2);
    const auto counterShift = std::max(0, static_cast<int>(rate.shift) - 11);
    std::uint32_t increment = 0x8000u >> counterShift;
    if (rate.curve == Curve::ExponentialIncrease && state.level > 0x6000)
      increment /= 4;
    state.counter += std::max<std::uint32_t>(1, increment);
    if (state.counter < 0x8000)
      return;
    state.counter = 0;

    const auto stepShift = std::max(0, 11 - static_cast<int>(rate.shift));
    std::int32_t step = rate.baseStep * (std::int32_t{1} << stepShift);
    if (rate.curve == Curve::ExponentialDecrease)
    {
      // The unscaled step fits signed16; reuse the scalar Q15 floor boundary.
      step = SPU2Numeric::applyGain(
        static_cast<std::int16_t>(step), static_cast<std::int16_t>(state.level));
    }
    applyStep(state, step, adsr1);
  }
}
