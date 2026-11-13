#include "spu2_controller.hpp"

#include "spu2_numeric.hpp"

std::array<std::int16_t, 4> SPU2Controller::renderVoiceMix(std::size_t core) noexcept
{
  // Lane order: dry L/R, wet L/R. Keep addition wide until each capture.
  std::array<std::int64_t, 4> sums{};
  for (std::size_t voice = 0; voice < VOICE_COUNT_PER_CORE; ++voice)
  {
    const auto index = core * VOICE_COUNT_PER_CORE + voice;
    const auto sample = advanceVoice(index);
    if (voice == 1 || voice == 3)
    {
      const auto address = static_cast<std::uint32_t>(
        0x400 + core * 0x800 + (voice == 3 ? 0x200 : 0) + inputReadPosition);
      writeSoundRAMHalfword(address, static_cast<std::uint16_t>(sample));
    }
    const std::array<std::int16_t, 2> gained = {{
      SPU2Numeric::applyGain(sample,
        SPU2Numeric::fixedVolumeGain(voiceVolumeControls[index * 2])),
      SPU2Numeric::applyGain(sample,
        SPU2Numeric::fixedVolumeGain(voiceVolumeControls[index * 2 + 1]))
    }};
    for (std::size_t lane = 0; lane < sums.size(); ++lane)
    {
      const auto maskIndex = core * 8 + (lane % 2) * 4 + (lane / 2) * 2 + voice / 16;
      if ((voiceMix[maskIndex] & (std::uint16_t{1} << (voice % 16))) != 0)
        sums[lane] += gained[lane % 2];
    }
  }
  std::array<std::int16_t, 4> mix{};
  for (std::size_t lane = 0; lane < mix.size(); ++lane)
  {
    mix[lane] = SPU2Numeric::clipSample(sums[lane]);
    const auto address = static_cast<std::uint32_t>(
      0x1000 + core * 0x800 + lane * 0x200 + inputReadPosition);
    writeSoundRAMHalfword(address, static_cast<std::uint16_t>(mix[lane]));
  }
  return mix;
}

SPU2StereoSample SPU2Controller::renderCoreOutput(std::size_t core,
  const std::array<std::int16_t, 4> &mix, const SPU2InputSample &input) const noexcept
{
  std::array<std::int16_t, 2> external{};
  if (core == 1)
  {
    external[0] = SPU2Numeric::signedSample(readSoundRAMHalfword(0x800 + inputReadPosition));
    external[1] = SPU2Numeric::signedSample(readSoundRAMHalfword(0xa00 + inputReadPosition));
  }
  const std::array<std::uint16_t, 2> pcm = {{input.left, input.right}};
  std::array<std::int16_t, 2> output{};
  for (std::size_t side = 0; side < output.size(); ++side)
  {
    std::int64_t sum = 0;
    if ((mainMix[core] & (0x800 >> side)) != 0)
      sum += mix[side];
    if ((mainMix[core] & (0x80 >> side)) != 0 && input.consumed)
    {
      sum += SPU2Numeric::applyGain(SPU2Numeric::signedSample(pcm[side]),
        SPU2Numeric::signedSample(effectInputVolumes[core * 6 + 4 + side]));
    }
    if ((mainMix[core] & (8 >> side)) != 0 && core == 1)
    {
      sum += SPU2Numeric::applyGain(external[side],
        SPU2Numeric::signedSample(effectInputVolumes[core * 6 + 2 + side]));
    }
    const auto mastered = SPU2Numeric::applyGain(SPU2Numeric::clipSample(sum),
      SPU2Numeric::fixedVolumeGain(masterVolumeControls[core * 2 + side]));
    output[side] = (coreAttributes[core] & 0x4000) != 0 ? mastered : 0;
  }
  return {output[0], output[1]};
}
