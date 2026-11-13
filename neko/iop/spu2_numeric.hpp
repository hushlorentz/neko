#ifndef SPU2_NUMERIC_HPP
#define SPU2_NUMERIC_HPP

#include <array>
#include <cstdint>

namespace SPU2Numeric
{
  struct ADPCMHeader
  {
    std::uint8_t shift;
    std::uint8_t predictor;
    std::uint8_t flags;
  };

  struct PitchStep
  {
    std::uint8_t consumed;
    std::uint16_t fraction;
  };

  ADPCMHeader decodeADPCMHeader(std::uint16_t raw) noexcept;
  std::array<std::int16_t, 4> decodeADPCMWord(
    ADPCMHeader header, std::uint16_t payload,
    std::int16_t history1, std::int16_t history2) noexcept;
  std::int16_t interpolate(
    const std::array<std::int16_t, 4> &samples, std::uint16_t fraction) noexcept;
  PitchStep advancePitch(std::uint16_t fraction, std::uint16_t rawPitch) noexcept;
  // The caller must admit fixed controls (bit15 clear) before conversion.
  std::int16_t fixedVolumeGain(std::uint16_t control) noexcept;
  std::int16_t applyGain(std::int16_t sample, std::int16_t gain) noexcept;
}

#endif
