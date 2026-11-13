#include <array>
#include <stdexcept>

#include "catch.hpp"
#include "spu2_envelope.hpp"
#include "spu2_numeric_vectors.hpp"

namespace
{
  SPU2Envelope::Phase fixturePhase(SPU2NumericTest::EnvelopePhase phase)
  {
    using Fixture = SPU2NumericTest::EnvelopePhase;
    using Phase = SPU2Envelope::Phase;
    switch (phase)
    {
      case Fixture::Stopped: return Phase::Stopped;
      case Fixture::Attack: return Phase::Attack;
      case Fixture::Decay: return Phase::Decay;
      case Fixture::Sustain: return Phase::Sustain;
      case Fixture::Release: return Phase::Release;
    }
    throw std::logic_error("Invalid envelope fixture phase");
  }
}

TEST_CASE("SPU2 envelope ticks obey authored rate and transition vectors",
          "[iop][spu2][envelope]")
{
  for (const auto &vector : SPU2NumericTest::envelopeTicks)
  {
    INFO(vector.name);
    SPU2Envelope::State state{fixturePhase(vector.phase), vector.level, vector.counter};
    SPU2Envelope::advance(state, vector.adsr1, vector.adsr2);
    REQUIRE(state.phase == fixturePhase(vector.nextPhase));
    REQUIRE(state.level == vector.nextLevel);
    REQUIRE(state.counter == vector.nextCounter);
  }
}

TEST_CASE("SPU2 envelope retrigger clears level and fractional rate from every phase",
          "[iop][spu2][envelope]")
{
  using Phase = SPU2Envelope::Phase;
  const std::array<SPU2Envelope::State, 5> previous = {{
    {Phase::Stopped, 0, 0},
    {Phase::Attack, 123, 456},
    {Phase::Decay, 23456, 1234},
    {Phase::Sustain, 0, 0x7fff},
    {Phase::Release, 300, 0x4000}
  }};
  for (auto state : previous)
  {
    SPU2Envelope::keyOn(state);
    REQUIRE(state.phase == Phase::Attack);
    REQUIRE(state.level == 0);
    REQUIRE(state.counter == 0);
    SPU2Envelope::advance(state, 0, 0);
    REQUIRE(state.phase == Phase::Attack);
    REQUIRE(state.level == 14336);
    REQUIRE(state.counter == 0);
  }
}

TEST_CASE("SPU2 envelope key-off preserves live level and restarts release timing",
          "[iop][spu2][envelope]")
{
  using Phase = SPU2Envelope::Phase;
  for (const auto phase : {Phase::Attack, Phase::Decay, Phase::Sustain, Phase::Release})
  {
    SPU2Envelope::State state{phase, 123, 0x7fff};
    SPU2Envelope::keyOff(state);
    REQUIRE(state.phase == Phase::Release);
    REQUIRE(state.level == 123);
    REQUIRE(state.counter == 0);
    SPU2Envelope::advance(state, 0, 0x000c);
    REQUIRE(state.phase == Phase::Release);
    REQUIRE(state.level == 123);
    REQUIRE(state.counter == 0x4000);
    SPU2Envelope::keyOff(state);
    REQUIRE(state.level == 123);
    REQUIRE(state.counter == 0);
    SPU2Envelope::advance(state, 0, 0x000b);
    REQUIRE(state.level == 115);
    REQUIRE(state.counter == 0);
  }
  SPU2Envelope::State stopped;
  SPU2Envelope::keyOff(stopped);
  REQUIRE(stopped.phase == Phase::Stopped);
  REQUIRE(stopped.level == 0);
  REQUIRE(stopped.counter == 0);
}

TEST_CASE("SPU2 envelope live parameter writes retain phase and level but reset rate",
          "[iop][spu2][envelope]")
{
  using Phase = SPU2Envelope::Phase;
  for (const auto phase : {Phase::Attack, Phase::Decay, Phase::Sustain, Phase::Release})
  {
    SPU2Envelope::State state{phase, 1234, 0x7fff};
    SPU2Envelope::parametersWritten(state);
    REQUIRE(state.phase == phase);
    REQUIRE(state.level == 1234);
    REQUIRE(state.counter == 0);
  }
  SPU2Envelope::State state{Phase::Attack, 100, 0x7fff};
  SPU2Envelope::parametersWritten(state);
  SPU2Envelope::advance(state, 0x7f00, 0);
  REQUIRE(state.level == 100);
  REQUIRE(state.counter == 1);
  SPU2Envelope::parametersWritten(state);
  SPU2Envelope::advance(state, 0, 0);
  REQUIRE(state.level == 14436);
  REQUIRE(state.counter == 0);
}

TEST_CASE("SPU2 envelope counters expire once and discard excess fractional credit",
          "[iop][spu2][envelope]")
{
  using Phase = SPU2Envelope::Phase;
  SPU2Envelope::State attack{Phase::Attack, 0, 0};
  SPU2Envelope::advance(attack, 0x3000, 0);
  REQUIRE(attack.level == 0);
  REQUIRE(attack.counter == 0x4000);
  SPU2Envelope::advance(attack, 0x3000, 0);
  REQUIRE(attack.level == 7);
  REQUIRE(attack.counter == 0);
  attack.counter = 0x7fff;
  SPU2Envelope::advance(attack, 0x3000, 0);
  REQUIRE(attack.level == 14);
  REQUIRE(attack.counter == 0);

  SPU2Envelope::State decay{Phase::Decay, 0x4000, 0};
  for (int tick = 0; tick < 15; ++tick)
  {
    SPU2Envelope::advance(decay, 0x00f0, 0);
  }
  REQUIRE(decay.level == 0x4000);
  REQUIRE(decay.counter == 0x7800);
  SPU2Envelope::advance(decay, 0x00f0, 0);
  REQUIRE(decay.phase == Phase::Decay);
  REQUIRE(decay.level == 0x3ffc);
  REQUIRE(decay.counter == 0);
}

TEST_CASE("SPU2 envelope sustain respects direction exponential threshold and minimum rate",
          "[iop][spu2][envelope]")
{
  using Phase = SPU2Envelope::Phase;
  SPU2Envelope::State falling{Phase::Sustain, 100, 0};
  SPU2Envelope::advance(falling, 0, 0x4bc0);
  REQUIRE(falling.level == 95);
  REQUIRE(falling.counter == 0);

  SPU2Envelope::State threshold{Phase::Sustain, 0x6000, 0};
  SPU2Envelope::advance(threshold, 0, 0x8b00);
  REQUIRE(threshold.level == 0x6007);
  REQUIRE(threshold.counter == 0);
  SPU2Envelope::advance(threshold, 0, 0x8b00);
  REQUIRE(threshold.level == 0x6007);
  REQUIRE(threshold.counter == 0x2000);

  SPU2Envelope::State slow{Phase::Sustain, 0x6001, 0x7fff};
  SPU2Envelope::advance(slow, 0, 0x9fc0);
  REQUIRE(slow.level == 0x6005);
  REQUIRE(slow.counter == 0);

  SPU2Envelope::State saturated{Phase::Sustain, 0x7fff, 0};
  SPU2Envelope::advance(saturated, 0, 0);
  REQUIRE(saturated.phase == Phase::Sustain);
  REQUIRE(saturated.level == 0x7fff);
}

TEST_CASE("SPU2 envelope advances at most one phase per tick and releases silence",
          "[iop][spu2][envelope]")
{
  using Phase = SPU2Envelope::Phase;
  SPU2Envelope::State state{Phase::Attack, 0x7ffe, 0};
  SPU2Envelope::advance(state, 0x000f, 0);
  REQUIRE(state.phase == Phase::Decay);
  REQUIRE(state.level == 0x7fff);
  SPU2Envelope::advance(state, 0x000f, 0);
  REQUIRE(state.phase == Phase::Sustain);
  REQUIRE(state.level == 0x3fff);
  SPU2Envelope::keyOff(state);
  SPU2Envelope::advance(state, 0, 0);
  REQUIRE(state.phase == Phase::Stopped);
  REQUIRE(state.level == 0);
  REQUIRE(state.counter == 0);
  SPU2Envelope::parametersWritten(state);
  SPU2Envelope::advance(state, 0xffff, 0xffff);
  REQUIRE(state.phase == Phase::Stopped);
  REQUIRE(state.level == 0);
  REQUIRE(state.counter == 0);
}
