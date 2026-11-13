#ifndef SPU2_NUMERIC_VECTORS_HPP
#define SPU2_NUMERIC_VECTORS_HPP

#include <array>
#include <cstdint>

namespace SPU2NumericTest
{
  // Authored arithmetic examples, not hardware recordings or emulator outputs.
  // ADPCM floors the prediction after adding32, clips history after each sample,
  // and decodes each payload's nibbles from least to most significant.
  struct ADPCMWordVector
  {
    const char *name;
    std::uint16_t header;
    std::uint16_t payload;
    std::int16_t history1;
    std::int16_t history2;
    std::array<std::int16_t, 4> samples;
    std::uint8_t effectiveFlags;
  };

  constexpr std::array<ADPCMWordVector, 12> adpcmWords = {{
    {"signed nibbles", 0x0000, 0x8f17, 0, 0,
     {{28672, 4096, -4096, -32768}}, 0},
    {"shift is not endpoint", 0x0001, 0x8f17, 0, 0,
     {{14336, 2048, -2048, -16384}}, 0},
    {"endpoint is in flag byte", 0x0100, 0x8f17, 0, 0,
     {{28672, 4096, -4096, -32768}}, 1},
    {"positive prediction rounding", 0x001c, 0, 1, 0,
     {{1, 1, 1, 1}}, 0},
    {"negative prediction floor", 0x001c, 0, -1, 0,
     {{-1, -1, -1, -1}}, 0},
    {"positive clipping feeds history", 0x0040, 0x7fff, 32767, -32768,
     {{32767, 27647, 17887, 32767}}, 0},
    {"negative clipping feeds history", 0x0040, 0x8888, -32768, 32767,
     {{-32768, -32768, -32768, -32768}}, 0},
    {"predictor two", 0x002c, 0, 1000, -1000,
     {{2609, 3876, 4845, 5557}}, 0},
    {"predictor three", 0x003c, 0, 1000, -1000,
     {{2391, 2802, 2236, 1016}}, 0},
    {"reserved predictor and full shift", 0x00ff, 0x8f17, 1234, -5678,
     {{0, 0, -1, -1}}, 0},
    {"reserved flag bits ignored", 0xf800, 0x8f17, 0, 0,
     {{28672, 4096, -4096, -32768}}, 0},
    {"all lifecycle flags", 0x0700, 0x8f17, 0, 0,
     {{28672, 4096, -4096, -32768}}, 7}
  }};

  enum class EnvelopePhase : std::uint8_t
  {
    Stopped, Attack, Decay, Sustain, Release
  };

  struct EnvelopeTickVector
  {
    const char *name;
    std::uint16_t adsr1;
    std::uint16_t adsr2;
    EnvelopePhase phase;
    std::uint16_t level;
    std::uint32_t counter;
    EnvelopePhase nextPhase;
    std::uint16_t nextLevel;
    std::uint32_t nextCounter;
  };

  // These pin nonnegative, fractional-counter compatibility, not the manual's
  // possible negative transient or unverified physical maximum/infinite rates.
  constexpr std::array<EnvelopeTickVector, 17> envelopeTicks = {{
    {"fast attack", 0, 0, EnvelopePhase::Attack, 0, 0,
     EnvelopePhase::Attack, 14336, 0},
    {"attack clips and enters decay", 0, 0, EnvelopePhase::Attack, 28672, 0,
     EnvelopePhase::Decay, 32767, 0},
    {"exponential decay floors", 0, 0, EnvelopePhase::Decay, 32767, 0,
     EnvelopePhase::Decay, 16383, 0},
    {"decay crosses sustain target", 0, 0, EnvelopePhase::Decay, 4095, 0,
     EnvelopePhase::Sustain, 2047, 0},
    {"slow attack accumulates", 0x7f00, 0, EnvelopePhase::Attack, 0, 0,
     EnvelopePhase::Attack, 0, 1},
    {"slow attack counter expires", 0x7f00, 0, EnvelopePhase::Attack, 0, 0x7fff,
     EnvelopePhase::Attack, 4, 0},
    {"exponential attack at threshold", 0xac00, 0, EnvelopePhase::Attack, 0x6000, 0,
     EnvelopePhase::Attack, 0x6007, 0},
    {"exponential attack above threshold", 0xac00, 0, EnvelopePhase::Attack, 0x6001, 0,
     EnvelopePhase::Attack, 0x6001, 0x2000},
    {"exponential attack quarter increment", 0xac00, 0, EnvelopePhase::Attack,
     0x6001, 0x6000, EnvelopePhase::Attack, 0x6008, 0},
    {"increasing sustain", 0, 0x0bc0, EnvelopePhase::Sustain, 10, 0,
     EnvelopePhase::Sustain, 14, 0},
    {"linear sustain zero remains live", 0, 0x4bc0, EnvelopePhase::Sustain, 3, 0,
     EnvelopePhase::Sustain, 0, 0},
    {"exponential sustain negative fraction", 0, 0xcb00, EnvelopePhase::Sustain, 1, 0,
     EnvelopePhase::Sustain, 0, 0},
    {"linear release stops", 0, 0x000b, EnvelopePhase::Release, 3, 0,
     EnvelopePhase::Stopped, 0, 0},
    {"exponential release", 0, 0x002b, EnvelopePhase::Release, 4096, 0,
     EnvelopePhase::Release, 4095, 0},
    {"release minimum increment", 0, 0x003f, EnvelopePhase::Release, 1, 0x7fff,
     EnvelopePhase::Stopped, 0, 0},
    {"reserved sustain bit retained only", 0, 0x2000, EnvelopePhase::Sustain, 0, 0,
     EnvelopePhase::Sustain, 14336, 0},
    {"stopped is inert", 0xffff, 0xffff, EnvelopePhase::Stopped, 0, 0,
     EnvelopePhase::Stopped, 0, 0}
  }};

  struct InterpolationVector
  {
    const char *name;
    std::uint16_t fraction;
    std::array<std::int16_t, 4> samples;
    std::int16_t output;
  };

  // Chronological FIFO taps, phase=(fraction&0xfff)>>4. Each Q15 product
  // floors separately before wide addition and signed16 clipping.
  constexpr std::array<InterpolationVector, 11> interpolations = {{
    {"first chronological tap", 0, {{32767, 0, 0, 0}}, 4806},
    {"second chronological tap", 0, {{0, 32767, 0, 0}}, 22962},
    {"third chronological tap", 0, {{0, 0, 32767, 0}}, 4870},
    {"negative fourth coefficient", 0, {{0, 0, 0, 32767}}, -1},
    {"fraction low bits ignored", 0x000f, {{0, 32767, 0, 0}}, 22962},
    {"positive DC floors each product", 0, {{32767, 32767, 32767, 32767}}, 32637},
    {"negative DC", 0, {{-32768, -32768, -32768, -32768}}, -32640},
    {"signed mixture phase zero", 0, {{32767, -32768, 12345, -12345}}, -16322},
    {"signed mixture before midpoint", 0x07ff,
     {{32767, -32768, 12345, -12345}}, -9708},
    {"signed mixture after midpoint", 0x0800,
     {{32767, -32768, 12345, -12345}}, -9596},
    {"signed mixture final phase", 0x0fff,
     {{32767, -32768, 12345, -12345}}, 1968}
  }};

  struct PitchVector
  {
    const char *name;
    std::uint16_t fraction;
    std::uint16_t rawPitch;
    std::uint8_t consumed;
    std::uint16_t nextFraction;
  };

  constexpr std::array<PitchVector, 6> pitches = {{
    {"zero holds fraction", 0x0fff, 0, 0, 0x0fff},
    {"unit pitch", 0, 0x1000, 1, 0},
    {"fractional carry", 0x0fff, 1, 1, 0},
    {"maximum from zero", 0, 0x3fff, 3, 0x0fff},
    {"maximum carry", 0x0fff, 0x3fff, 4, 0x0ffe},
    {"raw pitch saturates", 0x0800, 0x4000, 4, 0x07ff}
  }};

  struct FixedGainVector
  {
    const char *name;
    std::uint16_t control;
    std::int16_t sample;
    std::int16_t gain;
    std::int16_t output;
  };

  constexpr std::array<FixedGainVector, 6> fixedGains = {{
    {"zero gain", 0, -32768, 0, 0},
    {"positive maximum", 0x3fff, 32767, 32766, 32765},
    {"negative maximum clips", 0x4000, -32768, -32768, 32767},
    {"negative full scale", 0x4000, 32767, -32768, -32767},
    {"negative fractional floor", 0x2000, -1, 16384, -1},
    {"sign extend fifteen bits", 0x7fff, 32767, -2, -2}
  }};

  struct Q15GainVector
  {
    const char *name;
    std::int16_t sample;
    std::int16_t gain;
    std::int16_t output;
  };

  constexpr std::array<Q15GainVector, 5> q15Gains = {{
    {"positive full scale", 32767, 32767, 32766},
    {"positive clipping", -32768, -32768, 32767},
    {"negative fractional floor", -1, 16384, -1},
    {"negative tiny gain", 1, -1, -1},
    {"negative full scale", -32768, 32767, -32767}
  }};
}

#endif
