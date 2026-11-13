#include "spu2_numeric.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>

#include "spu2_gaussian_coefficients.hpp"

namespace
{
  constexpr std::array<std::array<std::int32_t, 2>, 5> predictors = {{
    {{0, 0}}, {{60, 0}}, {{115, -52}}, {{98, -55}}, {{122, -60}}
  }};

  std::int64_t floorDivide(std::int64_t value, std::int64_t divisor) noexcept
  {
    return value / divisor - (value % divisor < 0 ? 1 : 0);
  }
}

namespace SPU2Numeric
{
  std::int16_t clipSample(std::int64_t value) noexcept
  {
    return static_cast<std::int16_t>(
      std::max<std::int64_t>(-32768, std::min<std::int64_t>(32767, value)));
  }

  std::int16_t signedSample(std::uint16_t value) noexcept
  {
    return static_cast<std::int16_t>(
      value < 0x8000 ? std::int32_t{value} : std::int32_t{value} - 0x10000);
  }

  ADPCMHeader decodeADPCMHeader(std::uint16_t raw) noexcept
  {
    const auto predictor = static_cast<std::uint8_t>((raw >> 4) & 0x0f);
    return {
      static_cast<std::uint8_t>(raw & 0x0f),
      static_cast<std::uint8_t>(predictor < predictors.size() ? predictor : 0),
      static_cast<std::uint8_t>((raw >> 8) & 7)
    };
  }

  std::array<std::int16_t, 4> decodeADPCMWord(
    ADPCMHeader header, std::uint16_t payload,
    std::int16_t history1, std::int16_t history2) noexcept
  {
    assert(header.predictor < predictors.size());
    assert(header.shift < 16);
    const auto &coefficients = predictors[header.predictor];
    std::array<std::int16_t, 4> samples{};
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
      const auto raw = static_cast<std::int32_t>((payload >> (index * 4)) & 0x0f);
      const auto nibble = raw < 8 ? raw : raw - 16;
      const auto residual = floorDivide(nibble * 4096, std::int64_t{1} << header.shift);
      const auto prediction = floorDivide(
        std::int64_t{history1} * coefficients[0] +
        std::int64_t{history2} * coefficients[1] + 32, 64);
      samples[index] = clipSample(residual + prediction);
      history2 = history1;
      history1 = samples[index];
    }
    return samples;
  }

  std::int16_t interpolate(
    const std::array<std::int16_t, 4> &samples, std::uint16_t fraction) noexcept
  {
    const auto &coefficients = SPU2Gaussian::coefficients[(fraction & 0x0fff) >> 4];
    std::int64_t sum = 0;
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
      sum += floorDivide(std::int64_t{samples[index]} * coefficients[index], 32768);
    }
    return clipSample(sum);
  }

  PitchStep advancePitch(std::uint16_t fraction, std::uint16_t rawPitch) noexcept
  {
    const auto total = static_cast<std::uint32_t>(fraction & 0x0fff) +
      std::min<std::uint16_t>(rawPitch, 0x3fff);
    return {
      static_cast<std::uint8_t>(total >> 12),
      static_cast<std::uint16_t>(total & 0x0fff)
    };
  }

  std::int16_t fixedVolumeGain(std::uint16_t control) noexcept
  {
    assert((control & 0x8000) == 0);
    const auto signedControl = static_cast<std::int32_t>(control) -
      ((control & 0x4000) != 0 ? 0x8000 : 0);
    return static_cast<std::int16_t>(signedControl * 2);
  }

  std::int16_t applyGain(std::int16_t sample, std::int16_t gain) noexcept
  {
    return clipSample(floorDivide(std::int64_t{sample} * gain, 32768));
  }
}
