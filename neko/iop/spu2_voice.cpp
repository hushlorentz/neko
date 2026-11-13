#include "spu2_controller.hpp"

#include <algorithm>
#include <cassert>
#include <exception>

#include "spu2_numeric.hpp"

namespace
{
  constexpr std::uint32_t addressMask = 0xfffff;
  using Phase = SPU2Envelope::Phase;

  std::uint32_t commandHalf(std::size_t index, std::uint16_t value) noexcept
  {
    return index % 2 == 0 ? value : std::uint32_t{value & 0xffu} << 16;
  }

  void replacePendingHalf(std::uint32_t &pending, std::size_t index,
                          std::uint16_t value) noexcept
  {
    const auto mask = index % 2 == 0 ? 0xffffu : 0xff0000u;
    pending = (pending & ~mask) | commandHalf(index, value);
  }
}

std::uint16_t SPU2Controller::readKeyOn(std::size_t index) const
{
  return keyOnImages.at(index);
}

bool SPU2Controller::writeKeyOn(std::size_t index, std::uint16_t value)
{
  auto &image = keyOnImages.at(index);
  const auto core = index / 2;
  const auto mask = commandHalf(index, value);
  for (std::size_t voice = 0; voice < VOICE_COUNT_PER_CORE; ++voice)
    if ((mask & (std::uint32_t{1} << voice)) != 0 &&
        !hasSupportedVoiceMode(core * VOICE_COUNT_PER_CORE + voice))
      return false;
  image = value;
  replacePendingHalf(pendingKeyOn[core], index, value);
  return true;
}

std::uint16_t SPU2Controller::readKeyOff(std::size_t index) const
{
  return keyOffImages.at(index);
}

void SPU2Controller::writeKeyOff(std::size_t index, std::uint16_t value)
{
  keyOffImages.at(index) = value;
  replacePendingHalf(pendingKeyOff[index / 2], index, value);
}

void SPU2Controller::consumeCommands(std::size_t core) noexcept
{
  const auto off = pendingKeyOff[core];
  const auto on = pendingKeyOn[core];
  pendingKeyOff[core] = 0;
  pendingKeyOn[core] = 0;
  recordCommand(Observation::Kind::KeyOffConsumed, core, off);
  for (std::size_t voice = 0; voice < VOICE_COUNT_PER_CORE; ++voice)
    if ((off & (std::uint32_t{1} << voice)) != 0)
    {
      const auto index = core * VOICE_COUNT_PER_CORE + voice;
      auto &envelope = voices[index].envelope;
      const auto before = envelope;
      SPU2Envelope::keyOff(envelope);
      if (before.phase != Phase::Stopped)
        recordVoice(Observation::Kind::KeyOff, index, before);
    }
  recordCommand(Observation::Kind::KeyOnConsumed, core, on);
  for (std::size_t voice = 0; voice < VOICE_COUNT_PER_CORE; ++voice)
    if ((on & (std::uint32_t{1} << voice)) != 0)
    {
      const auto started = startVoice(core * VOICE_COUNT_PER_CORE + voice);
      assert(started);
      if (!started)
        std::terminate();
    }
}

std::uint16_t SPU2Controller::readVoiceLoopAddress(std::size_t index) const
{
  return static_cast<std::uint16_t>(voices.at(index / 2).loopAddress >>
    (index % 2 == 0 ? 16 : 0));
}

void SPU2Controller::writeVoiceLoopAddressHalfword(std::size_t index, std::uint16_t value)
{
  const auto old = voices.at(index / 2).loopAddress;
  const auto address = index % 2 == 0
    ? (old & 0xffff) | (std::uint32_t{value & 0xfu} << 16)
    : (old & 0xf0000) | (value & 0xfff8u);
  writeVoiceLoopAddress(index / 2, address);
}

SPU2ReadResult SPU2Controller::readVoiceNextAddress(std::size_t index) const
{
  const auto &voice = voices.at(index / 2);
  if (voice.envelope.phase != Phase::Stopped && voice.age < 4)
    return {};
  return {SPU2AccessStatus::Accepted, static_cast<std::uint16_t>(
    voice.nextAddress >> (index % 2 == 0 ? 16 : 0))};
}

std::uint16_t SPU2Controller::readVoiceEnvelope(std::size_t index) const
{
  return voices.at(index).envelope.level;
}

std::uint16_t SPU2Controller::readVoiceEndMask(std::size_t index) const
{
  return static_cast<std::uint16_t>(voiceEndMasks.at(index / 2) >>
    ((index % 2) * 16));
}

bool SPU2Controller::startVoice(std::size_t index) noexcept
{
  assert(index < voices.size());
  if (!hasSupportedVoiceMode(index))
    return false;
  auto &voice = voices[index];
  const auto before = voice.envelope;
  const auto loop = voice.loopAddress;
  voice = {};
  voice.loopAddress = loop;
  voice.nextAddress = ((std::uint32_t{voiceStartAddresses[index * 2]} << 16) |
    voiceStartAddresses[index * 2 + 1]) + 1;
  SPU2Envelope::keyOn(voice.envelope);
  voiceEndMasks[index / VOICE_COUNT_PER_CORE] &=
    ~(std::uint32_t{1} << (index % VOICE_COUNT_PER_CORE));
  recordVoice(Observation::Kind::KeyOn, index, before);
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
    const auto before = voice.envelope;
    voiceEndMasks[index / VOICE_COUNT_PER_CORE] |=
      std::uint32_t{1} << (index % VOICE_COUNT_PER_CORE);
    if ((header.flags & 2) == 0)
      stopVoice(voice);
    recordVoice(Observation::Kind::Endpoint, index, before);
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
  const auto before = voice.envelope;
  SPU2Envelope::advance(voice.envelope,
    voiceADSRControls[index * 2], voiceADSRControls[index * 2 + 1]);
  if (before.phase != voice.envelope.phase)
    recordVoice(voice.envelope.phase == Phase::Stopped
      ? Observation::Kind::EnvelopeZero : Observation::Kind::EnvelopePhase, index, before);
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

bool SPU2Controller::isVoiceActive(std::size_t index) const noexcept
{
  return voices[index].envelope.phase != Phase::Stopped ||
    (pendingKeyOn[index / VOICE_COUNT_PER_CORE] &
      (std::uint32_t{1} << (index % VOICE_COUNT_PER_CORE))) != 0;
}

bool SPU2Controller::hasActiveVoices() const noexcept
{
  for (const auto pending : pendingKeyOn)
    if (pending != 0)
      return true;
  return std::any_of(voices.begin(), voices.end(), [](const VoiceState &voice) {
    return voice.envelope.phase != Phase::Stopped;
  });
}

bool SPU2Controller::hasValidCommandState() const noexcept
{
  for (std::size_t core = 0; core < CORE_COUNT; ++core)
  {
    if (pendingKeyOn[core] > 0xffffff || pendingKeyOff[core] > 0xffffff)
      return false;
    for (std::size_t half = 0; half < 2; ++half)
    {
      const auto index = core * 2 + half;
      const auto mask = half == 0 ? 0xffffu : 0xff0000u;
      const auto on = pendingKeyOn[core] & mask;
      const auto off = pendingKeyOff[core] & mask;
      if ((on != 0 && on != commandHalf(index, keyOnImages[index])) ||
          (off != 0 && off != commandHalf(index, keyOffImages[index])))
        return false;
    }
    for (std::size_t voice = 0; voice < VOICE_COUNT_PER_CORE; ++voice)
      if ((pendingKeyOn[core] & (std::uint32_t{1} << voice)) != 0 &&
          !hasSupportedVoiceMode(core * VOICE_COUNT_PER_CORE + voice))
        return false;
  }
  return true;
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
