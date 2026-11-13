#include "spu2_controller.hpp"

#include <algorithm>
#include <cassert>
#include <exception>
#include <stdexcept>

namespace
{
  std::size_t soundRAMByteOffset(std::uint32_t halfwordAddress)
  {
    if (halfwordAddress >= SPU2Controller::SOUND_RAM_HALFWORD_COUNT)
    {
      throw std::out_of_range("SPU2 sound RAM halfword address is outside memory.");
    }
    return static_cast<std::size_t>(halfwordAddress) * 2;
  }
}

constexpr std::size_t SPU2Controller::SOUND_RAM_SIZE;
constexpr std::size_t SPU2Controller::SOUND_RAM_HALFWORD_COUNT;
constexpr std::uint16_t SPU2Controller::INPUT_CLOCK_PERIOD;
constexpr std::uint16_t SPU2Controller::INPUT_SAMPLE_COUNT;
constexpr std::uint16_t SPU2Controller::INPUT_HALF_WORD_COUNT;

SPU2Controller::SPU2Controller() : soundRAM(SOUND_RAM_SIZE)
{
}

void SPU2Controller::reset() noexcept
{
  clearObservations();
  std::fill(soundRAM.begin(), soundRAM.end(), 0);
  inputClockPhase = 0;
  inputReadPosition = 0;
  inputStates = {};
  spdifConfiguration.fill(0);
  masterVolumeControls.fill(0);
  voiceVolumeControls.fill(0);
  voicePitchControls.fill(0);
  voiceADSRControls.fill(0);
  voiceStartAddresses.fill(0);
  voices = {};
  voiceEndMasks.fill(0xffffff);
  keyOnImages.fill(0);
  keyOffImages.fill(0);
  pendingKeyOn.fill(0);
  pendingKeyOff.fill(0);
  coreAttributes.fill(0);
  voiceMix.fill(0);
  mainMix.fill(0);
  effectInputVolumes.fill(0);
  effectEndAddressHigh.fill(0);
  transferStartAddress.fill(0);
  transferCursor.fill(0);
  voiceModeMasks.fill(0);
  autoDMAWriteSelection.fill(0);
}

std::uint16_t SPU2Controller::readSoundRAMHalfword(
  std::uint32_t halfwordAddress) const
{
  const auto offset = soundRAMByteOffset(halfwordAddress);
  return static_cast<std::uint16_t>(soundRAM[offset] |
    (std::uint16_t{soundRAM[offset + 1]} << 8));
}

void SPU2Controller::writeSoundRAMHalfword(
  std::uint32_t halfwordAddress, std::uint16_t value)
{
  const auto offset = soundRAMByteOffset(halfwordAddress);
  soundRAM[offset] = static_cast<std::uint8_t>(value);
  soundRAM[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

std::uint16_t SPU2Controller::readSPDIFConfiguration(std::size_t index) const
{
  return spdifConfiguration.at(index);
}

SPU2SampleTick SPU2Controller::advanceSampleClock() noexcept
{
  SPU2SampleTick result;
  if (++inputClockPhase != INPUT_CLOCK_PERIOD)
  {
    return result;
  }
  inputClockPhase = 0;
  std::array<SPU2InputWriteState, CORE_COUNT> before{};
  if (observationsEnabled)
  {
    for (std::size_t core = 0; core < CORE_COUNT; ++core)
      before[core] = inputWriteState(core);
  }
  result.sampleTick = true;
  for (std::size_t core = 0; core < CORE_COUNT; ++core)
  {
    if (autoDMAWriteSelection[core] != 0)
    {
      auto &sample = result.samples[core];
      sample.consumed = true;
      const auto address = static_cast<std::uint32_t>(
        0x2000 + core * 0x400 + inputReadPosition);
      sample.left = readSoundRAMHalfword(address);
      sample.right = readSoundRAMHalfword(address + 0x200);
    }
  }
  for (std::size_t core = 0; core < CORE_COUNT; ++core)
  {
    consumeCommands(core);
    const auto mix = renderVoiceMix(core);
    const auto output = renderCoreOutput(core, mix, result.samples[core]);
    result.outputs[core] = output;
    if (core == 0)
    {
      writeSoundRAMHalfword(0x800 + inputReadPosition,
        static_cast<std::uint16_t>(output.left));
      writeSoundRAMHalfword(0xa00 + inputReadPosition,
        static_cast<std::uint16_t>(output.right));
    }
  }
  inputReadPosition = (inputReadPosition + 1) % INPUT_SAMPLE_COUNT;
  for (auto &state : inputStates)
  {
    if (state.writeOpen && state.fillWords == INPUT_HALF_WORD_COUNT &&
        state.fillHalf == inputReadPosition / 256)
    {
      state.fillHalf ^= 1;
      state.fillWords = 0;
    }
  }
  if (observationsEnabled && inputReadPosition % 256 == 0)
  {
    for (std::size_t core = 0; core < CORE_COUNT; ++core)
      recordInputState(core, before[core]);
  }
  return result;
}

SPU2InputWriteState SPU2Controller::inputWriteState(std::size_t core) const
{
  const auto &state = inputStates.at(core);
  return {autoDMAWriteSelection[core] != 0, state.writeOpen,
    inputWriteRequested(core), state.fillHalf,
    static_cast<std::uint8_t>(inputReadPosition / 256), state.fillWords};
}

void SPU2Controller::enableObservations(NekoObservationOrder *order) noexcept
{
  clearObservations();
  observationOrder = order;
  observationsEnabled = true;
}

void SPU2Controller::disableObservations() noexcept
{
  clearObservations();
  observationsEnabled = false;
  observationOrder = nullptr;
}

void SPU2Controller::clearObservations() noexcept
{
  observationCount = 0;
}

SPU2Controller::Observation &SPU2Controller::appendObservation(
  Observation::Kind kind, std::size_t core) noexcept
{
  assert(observationsEnabled && observationCount < observations.size());
  if (!observationsEnabled || observationCount >= observations.size())
    std::terminate();
  auto &event = observations[observationCount];
  event = {};
  event.kind = kind;
  event.core = static_cast<std::uint8_t>(core);
  event.samplePosition = inputReadPosition;
  event.sequence = observationOrder != nullptr ? observationOrder->next++ : observationCount;
  ++observationCount;
  return event;
}

void SPU2Controller::recordCommand(
  Observation::Kind kind, std::size_t core, std::uint32_t mask) noexcept
{
  if (observationsEnabled && mask != 0)
    appendObservation(kind, core).mask = mask;
}

void SPU2Controller::recordVoice(Observation::Kind kind, std::size_t index,
  const SPU2Envelope::State &before) noexcept
{
  if (!observationsEnabled)
    return;
  auto &event = appendObservation(kind, index / VOICE_COUNT_PER_CORE);
  event.voice = static_cast<std::uint8_t>(index % VOICE_COUNT_PER_CORE);
  event.beforePhase = before.phase;
  event.beforeLevel = before.level;
  event.afterPhase = voices[index].envelope.phase;
  event.afterLevel = voices[index].envelope.level;
}

void SPU2Controller::recordInputState(
  std::size_t core, const SPU2InputWriteState &before) noexcept
{
  if (!observationsEnabled)
  {
    return;
  }
  const auto after = inputWriteState(core);
  if ((!before.selected && !after.selected) ||
      (before.selected == after.selected && before.writeOpen == after.writeOpen &&
       before.requested == after.requested && before.fillHalf == after.fillHalf &&
       before.readHalf == after.readHalf && before.fillWords == after.fillWords))
  {
    return;
  }
  auto &event = appendObservation(Observation::Kind::Input, core);
  event.before = before;
  event.after = after;
}

bool SPU2Controller::inputWriteRequested(std::size_t core) const
{
  const auto &state = inputStates.at(core);
  return state.writeOpen && state.fillWords < INPUT_HALF_WORD_COUNT &&
    state.fillHalf != inputReadPosition / 256;
}

bool SPU2Controller::beginInputWrite(std::size_t core)
{
  const auto before = observationsEnabled ? inputWriteState(core) : SPU2InputWriteState{};
  auto &state = inputStates.at(core);
  if (autoDMAWriteSelection[core] == 0)
  {
    return false;
  }
  state.writeOpen = true;
  state.fillHalf = static_cast<std::uint8_t>((inputReadPosition / 256) ^ 1);
  state.fillWords = 0;
  recordInputState(core, before);
  return true;
}

void SPU2Controller::cancelInputWrite(std::size_t core)
{
  const auto before = observationsEnabled ? inputWriteState(core) : SPU2InputWriteState{};
  auto &state = inputStates.at(core);
  state.writeOpen = false;
  state.fillHalf = 0;
  state.fillWords = 0;
  recordInputState(core, before);
}

bool SPU2Controller::writeInputWord(std::size_t core, std::uint32_t value)
{
  if (!inputWriteRequested(core))
  {
    return false;
  }
  auto &state = inputStates[core];
  const auto before = observationsEnabled ? inputWriteState(core) : SPU2InputWriteState{};
  // One stereo half is planar: 128 left words followed by 128 right words.
  const auto address = static_cast<std::uint32_t>(0x2000 + core * 0x400 +
    state.fillHalf * 0x100 + (state.fillWords / 128) * 0x200 +
    (state.fillWords % 128) * 2);
  writeSoundRAMHalfword(address, static_cast<std::uint16_t>(value));
  writeSoundRAMHalfword(address + 1, static_cast<std::uint16_t>(value >> 16));
  ++state.fillWords;
  if (state.fillWords == INPUT_HALF_WORD_COUNT)
  {
    recordInputState(core, before);
  }
  return true;
}

bool SPU2Controller::hasSupportedInputProfile(std::size_t core) const noexcept
{
  return spdifConfiguration == std::array<std::uint16_t, 5>{{
      0x0900, 0x0200, 0, 0x0008, 0xc032}} &&
    (coreAttributes[core] == 0xc000 || coreAttributes[core] == 0xc001);
}

bool SPU2Controller::hasValidInputState() const noexcept
{
  if (inputClockPhase >= INPUT_CLOCK_PERIOD ||
      inputReadPosition >= INPUT_SAMPLE_COUNT)
  {
    return false;
  }
  for (std::size_t core = 0; core < CORE_COUNT; ++core)
  {
    const auto &state = inputStates[core];
    const bool selected = autoDMAWriteSelection[core] != 0;
    if (state.fillHalf > 1 || state.fillWords > INPUT_HALF_WORD_COUNT ||
        (selected && (!state.everSelected || !hasSupportedInputProfile(core))) ||
        (state.writeOpen && !selected) ||
        (!state.writeOpen && (state.fillHalf != 0 || state.fillWords != 0)) ||
        (state.writeOpen && state.fillWords == INPUT_HALF_WORD_COUNT &&
          state.fillHalf == inputReadPosition / 256))
    {
      return false;
    }
  }
  return true;
}

bool SPU2Controller::writeSPDIFConfiguration(
  std::size_t index, std::uint16_t value)
{
  auto &configuration = spdifConfiguration.at(index);
  if (configuration != value &&
      (autoDMAWriteSelection[0] != 0 || autoDMAWriteSelection[1] != 0))
  {
    return false;
  }
  configuration = value;
  return true;
}

bool SPU2Controller::isFixedVolumeControl(std::uint16_t value) noexcept
{
  return (value & 0x8000) == 0;
}

std::uint16_t SPU2Controller::readMasterVolumeControl(std::size_t index) const
{
  return masterVolumeControls.at(index);
}

bool SPU2Controller::writeMasterVolumeControl(
  std::size_t index, std::uint16_t value)
{
  auto &control = masterVolumeControls.at(index);
  if (!isFixedVolumeControl(value))
  {
    return false;
  }
  control = value;
  return true;
}

std::uint16_t SPU2Controller::readVoiceVolumeControl(std::size_t index) const
{
  return voiceVolumeControls.at(index);
}

bool SPU2Controller::writeVoiceVolumeControl(
  std::size_t index, std::uint16_t value)
{
  auto &control = voiceVolumeControls.at(index);
  if (!isFixedVolumeControl(value))
  {
    return false;
  }
  control = value;
  return true;
}

std::uint16_t SPU2Controller::readVoicePitchControl(std::size_t index) const
{
  return voicePitchControls.at(index);
}

void SPU2Controller::writeVoicePitchControl(std::size_t index, std::uint16_t value)
{
  voicePitchControls.at(index) = value;
}

std::uint16_t SPU2Controller::readVoiceADSRControl(std::size_t index) const
{
  return voiceADSRControls.at(index);
}

void SPU2Controller::writeVoiceADSRControl(std::size_t index, std::uint16_t value)
{
  voiceADSRControls.at(index) = value;
  SPU2Envelope::parametersWritten(voices[index / 2].envelope);
}

std::uint16_t SPU2Controller::voiceStartAddressMask(std::size_t index) noexcept
{
  return index % 2 == 0 ? 0x000f : 0xfff8;
}

std::uint16_t SPU2Controller::readVoiceStartAddress(std::size_t index) const
{
  return voiceStartAddresses.at(index);
}

bool SPU2Controller::writeVoiceStartAddress(std::size_t index, std::uint16_t value)
{
  auto &address = voiceStartAddresses.at(index);
  if (isVoiceActive(index / 2))
    return false;
  address = static_cast<std::uint16_t>(value & voiceStartAddressMask(index));
  return true;
}

bool SPU2Controller::isSupportedCoreAttribute(std::uint16_t value) noexcept
{
  return (value & 0x3fee) == 0;
}

std::uint16_t SPU2Controller::readCoreAttribute(std::size_t core) const
{
  return coreAttributes.at(core);
}

bool SPU2Controller::writeCoreAttribute(
  std::size_t core, std::uint16_t value)
{
  auto &attribute = coreAttributes.at(core);
  if (!isSupportedCoreAttribute(value) ||
      (autoDMAWriteSelection[core] != 0 && attribute != value))
  {
    return false;
  }
  attribute = value;
  return true;
}

std::uint16_t SPU2Controller::readVoiceModeMask(std::size_t index) const
{
  return voiceModeMasks.at(index);
}

bool SPU2Controller::writeVoiceModeMask(std::size_t index, std::uint16_t value)
{
  auto &mask = voiceModeMasks.at(index);
  const auto core = index / 4;
  const auto first = (index % 2) * 16;
  for (std::size_t voice = first; voice < std::min(first + 16, VOICE_COUNT_PER_CORE); ++voice)
  {
    if ((value & (1u << (voice - first))) != 0 &&
        isVoiceActive(core * VOICE_COUNT_PER_CORE + voice) &&
        !(index % 4 == 0 && voice == 0))
      return false;
  }
  mask = value;
  return true;
}

std::uint16_t SPU2Controller::readVoiceMix(std::size_t index) const
{
  return voiceMix.at(index);
}

void SPU2Controller::writeVoiceMix(std::size_t index, std::uint16_t value)
{
  voiceMix.at(index) = value;
}

std::uint16_t SPU2Controller::readMainMix(std::size_t core) const
{
  return mainMix.at(core);
}

void SPU2Controller::writeMainMix(std::size_t core, std::uint16_t value)
{
  mainMix.at(core) = value;
}

std::uint16_t SPU2Controller::readEffectInputVolume(std::size_t index) const
{
  return effectInputVolumes.at(index);
}

void SPU2Controller::writeEffectInputVolume(
  std::size_t index, std::uint16_t value)
{
  effectInputVolumes.at(index) = value;
}

std::uint16_t SPU2Controller::readEffectEndAddressHigh(std::size_t core) const
{
  return effectEndAddressHigh.at(core);
}

void SPU2Controller::writeEffectEndAddressHigh(
  std::size_t core, std::uint16_t value)
{
  effectEndAddressHigh.at(core) = value;
}

std::uint16_t SPU2Controller::readTransferStartAddress(std::size_t index) const
{
  return transferStartAddress.at(index);
}

void SPU2Controller::writeTransferStartAddress(
  std::size_t index, std::uint16_t value)
{
  transferStartAddress.at(index) = value;
  const auto core = index / 2;
  transferCursor[core] =
    ((std::uint32_t{transferStartAddress[core * 2]} & 0xf) << 16) |
    transferStartAddress[core * 2 + 1];
}

bool SPU2Controller::writeTransferData(std::size_t core, std::uint16_t value)
{
  auto &cursor = transferCursor.at(core);
  if (!hasInactiveInputs())
  {
    return false;
  }
  writeSoundRAMHalfword(cursor, value);
  cursor = (cursor + 1) & static_cast<std::uint32_t>(SOUND_RAM_HALFWORD_COUNT - 1);
  return true;
}

SPU2ReadResult SPU2Controller::readCoreStatus(std::size_t core) const
{
  const auto mode = coreAttributes.at(core) & 0x30;
  if (hasActiveVoices())
    return {};
  if (mode == 0 && hasInactiveInput(core))
  {
    return {SPU2AccessStatus::Accepted, 0};
  }
  if (mode == 0x10 && hasInactiveInputs())
  {
    return {SPU2AccessStatus::Accepted, 0x80};
  }
  return {};
}

bool SPU2Controller::hasInactiveInput(std::size_t core) const
{
  return autoDMAWriteSelection.at(core) == 0 && !inputStates.at(core).writeOpen;
}

bool SPU2Controller::hasInactiveInputs() const
{
  for (std::size_t core = 0; core < CORE_COUNT; ++core)
  {
    if (!hasInactiveInput(core))
    {
      return false;
    }
  }
  return true;
}

bool SPU2Controller::isSupportedAutoDMAWriteSelection(
  std::size_t core, std::uint16_t value) noexcept
{
  return core < CORE_COUNT && (value == 0 || value == (1u << core));
}

std::uint16_t SPU2Controller::readAutoDMAWriteSelection(std::size_t core) const
{
  return autoDMAWriteSelection.at(core);
}

bool SPU2Controller::writeAutoDMAWriteSelection(
  std::size_t core, std::uint16_t value)
{
  auto &selection = autoDMAWriteSelection.at(core);
  if (!isSupportedAutoDMAWriteSelection(core, value))
  {
    return false;
  }
  if (selection == value)
  {
    return true;
  }
  if (value != 0 && !hasSupportedInputProfile(core))
  {
    return false;
  }
  if (value == 0)
  {
    cancelInputWrite(core);
  }
  const auto before = observationsEnabled ? inputWriteState(core) : SPU2InputWriteState{};
  selection = value;
  recordInputState(core, before);
  if (value != 0)
  {
    inputStates[core].everSelected = true;
    beginInputWrite(core);
  }
  return true;
}

bool SPU2Controller::writeUnknownInitialization(
  std::uint16_t value) const noexcept
{
  return value == 0;
}
