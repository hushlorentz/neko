#include "spu2_controller.hpp"

#include <algorithm>
#include <cassert>

#include "spu2_numeric.hpp"

namespace
{
  constexpr std::uint32_t addressMask = 0xfffff;
  using Phase = SPU2Envelope::Phase;
}

bool SPU2Controller::startVoice(std::size_t index) noexcept
{
  assert(index < voices.size());
  if (!hasSupportedVoiceMode(index))
    return false;
  auto &voice = voices[index];
  const auto loop = voice.loopAddress;
  voice = {};
  voice.loopAddress = loop;
  voice.nextAddress = ((std::uint32_t{voiceStartAddresses[index * 2]} << 16) |
    voiceStartAddresses[index * 2 + 1]) + 1;
  SPU2Envelope::keyOn(voice.envelope);
  voiceEndMasks[index / VOICE_COUNT_PER_CORE] &=
    ~(std::uint32_t{1} << (index % VOICE_COUNT_PER_CORE));
  return true;
}

void SPU2Controller::stopVoice(VoiceState &voice) noexcept
{
  voice.envelope = {};
  voice.fifo.fill(0);
  voice.fifoHead = 0;
  voice.fifoCount = 0;
  voice.header = 0;
  voice.headerValid = false;
}

void SPU2Controller::writeVoiceLoopAddress(
  std::size_t index, std::uint32_t address) noexcept
{
  assert(index < voices.size());
  auto &voice = voices[index];
  if (voice.envelope.phase != Phase::Stopped && voice.age < 4)
    return;
  voice.loopAddress = address & 0xffff8;
  voice.loopOverride = true;
}

void SPU2Controller::refillVoice(std::size_t index) noexcept
{
  auto &voice = voices[index];
  assert(voice.fifoCount <= 12);
  const auto block = voice.nextAddress & 0xffff8;
  if (!voice.headerValid)
  {
    voice.header = readSoundRAMHalfword(block);
    voice.headerValid = true;
    if ((voice.header & 0x0400) != 0 && !voice.loopOverride)
      voice.loopAddress = block;
  }
  const auto header = SPU2Numeric::decodeADPCMHeader(voice.header);
  const auto samples = SPU2Numeric::decodeADPCMWord(
    header, readSoundRAMHalfword(voice.nextAddress), voice.history1, voice.history2);
  for (const auto sample : samples)
  {
    voice.fifo[(voice.fifoHead + voice.fifoCount) % voice.fifo.size()] = sample;
    ++voice.fifoCount;
  }
  voice.history2 = samples[2];
  voice.history1 = samples[3];
  if ((voice.nextAddress & 7) != 7)
  {
    ++voice.nextAddress;
    return;
  }
  const bool endpoint = (header.flags & 1) != 0;
  const auto nextBlock = endpoint ? voice.loopAddress : (block + 8) & addressMask;
  voice.nextAddress = (nextBlock + 1) & addressMask;
  voice.header = 0;
  voice.headerValid = false;
  if (endpoint)
  {
    voiceEndMasks[index / VOICE_COUNT_PER_CORE] |=
      std::uint32_t{1} << (index % VOICE_COUNT_PER_CORE);
    if ((header.flags & 2) == 0)
      stopVoice(voice);
  }
}

std::int16_t SPU2Controller::advanceVoice(std::size_t index) noexcept
{
  auto &voice = voices[index];
  if (voice.envelope.phase == Phase::Stopped)
    return 0;
  if (voice.fifoCount <= 12)
    refillVoice(index);
  voice.age = std::min<std::uint8_t>(4, voice.age + 1);
  if (voice.envelope.phase == Phase::Stopped)
    return 0;

  assert(voice.fifoCount >= 4);
  std::array<std::int16_t, 4> taps{};
  for (std::size_t tap = 0; tap < taps.size(); ++tap)
    taps[tap] = voice.fifo[(voice.fifoHead + tap) % voice.fifo.size()];
  const auto source = SPU2Numeric::interpolate(taps, voice.fraction);
  SPU2Envelope::advance(voice.envelope,
    voiceADSRControls[index * 2], voiceADSRControls[index * 2 + 1]);
  if (voice.envelope.phase == Phase::Stopped)
  {
    stopVoice(voice);
    return 0;
  }
  const auto sample = SPU2Numeric::applyGain(
    source, static_cast<std::int16_t>(voice.envelope.level));
  const auto pitch = SPU2Numeric::advancePitch(voice.fraction, voicePitchControls[index]);
  voice.fraction = pitch.fraction;
  assert(pitch.consumed <= voice.fifoCount);
  for (std::uint8_t count = 0; count < pitch.consumed; ++count)
  {
    voice.fifo[voice.fifoHead] = 0;
    voice.fifoHead = (voice.fifoHead + 1) % voice.fifo.size();
    --voice.fifoCount;
  }
  return sample;
}

bool SPU2Controller::hasLiveVoices() const noexcept
{
  return std::any_of(voices.begin(), voices.end(), [](const VoiceState &voice) {
    return voice.envelope.phase != Phase::Stopped;
  });
}

bool SPU2Controller::hasSupportedVoiceMode(std::size_t index) const noexcept
{
  const auto core = index / VOICE_COUNT_PER_CORE;
  const auto voice = index % VOICE_COUNT_PER_CORE;
  const auto half = voice / 16;
  const auto bit = std::uint16_t{1} << (voice % 16);
  return (voiceModeMasks[core * 4 + 2 + half] & bit) == 0 &&
    (voice == 0 || (voiceModeMasks[core * 4 + half] & bit) == 0);
}

bool SPU2Controller::hasValidVoiceState() const noexcept
{
  for (const auto mask : voiceEndMasks)
    if (mask > 0xffffff)
      return false;
  for (std::size_t index = 0; index < voices.size(); ++index)
  {
    const auto &voice = voices[index];
    const auto phase = voice.envelope.phase;
    if (static_cast<std::uint8_t>(phase) > static_cast<std::uint8_t>(Phase::Release) ||
        voice.envelope.level > 0x7fff || voice.envelope.counter >= 0x8000 ||
        voice.age > 4 || voice.fraction > 0xfff ||
        voice.nextAddress > addressMask || voice.loopAddress > addressMask ||
        (voice.loopAddress & 7) != 0 ||
        (voice.nextAddress != 0 && (voice.nextAddress & 7) == 0) ||
        voice.fifoHead >= voice.fifo.size() || voice.fifoCount > 16)
      return false;
    if (phase == Phase::Stopped)
    {
      if (voice.envelope.level != 0 || voice.envelope.counter != 0 ||
          voice.fifoCount != 0 || voice.fifoHead != 0 || voice.headerValid)
        return false;
    }
    else if (!hasSupportedVoiceMode(index) || voice.nextAddress == 0)
      return false;
    if (phase != Phase::Stopped && voice.age < 4)
    {
      const auto start = (std::uint32_t{voiceStartAddresses[index * 2]} << 16) |
        voiceStartAddresses[index * 2 + 1];
      const auto bit = std::uint32_t{1} << (index % VOICE_COUNT_PER_CORE);
      if (voice.nextAddress != start + voice.age + 1 ||
          voice.fifoHead + voice.fifoCount != voice.age * 4 ||
          voice.headerValid != (voice.age != 0) || voice.loopOverride ||
          (voiceEndMasks[index / VOICE_COUNT_PER_CORE] & bit) != 0)
        return false;
      if (voice.age == 0 &&
          (phase != Phase::Attack || voice.envelope.level != 0 ||
           voice.envelope.counter != 0 || voice.fraction != 0 ||
           voice.history1 != 0 || voice.history2 != 0))
        return false;
    }
    if (phase != Phase::Stopped &&
        voice.headerValid != ((voice.nextAddress & 7) != 1))
      return false;
    if (voice.headerValid)
    {
      if ((voice.nextAddress & 7) < 2)
        return false;
    }
    else if (voice.header != 0)
      return false;
    for (std::size_t slot = 0; slot < voice.fifo.size(); ++slot)
    {
      const auto relative = (slot + voice.fifo.size() - voice.fifoHead) % voice.fifo.size();
      if (relative >= voice.fifoCount && voice.fifo[slot] != 0)
        return false;
    }
  }
  return true;
}
