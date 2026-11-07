#include "spu2_controller.hpp"

#include <algorithm>
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
  std::fill(soundRAM.begin(), soundRAM.end(), 0);
  inputClockPhase = 0;
  inputReadPosition = 0;
  inputStates = {};
  spdifConfiguration.fill(0);
  masterVolumeControls.fill(0);
  coreAttributes.fill(0);
  voiceMix.fill(0);
  mainMix.fill(0);
  effectInputVolumes.fill(0);
  effectEndAddressHigh.fill(0);
  transferStartAddress.fill(0);
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

SPU2InputTick SPU2Controller::advanceInputClock() noexcept
{
  SPU2InputTick result;
  if (++inputClockPhase != INPUT_CLOCK_PERIOD)
  {
    return result;
  }
  inputClockPhase = 0;
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
  return result;
}

bool SPU2Controller::inputWriteRequested(std::size_t core) const
{
  const auto &state = inputStates.at(core);
  return state.writeOpen && state.fillWords < INPUT_HALF_WORD_COUNT &&
    state.fillHalf != inputReadPosition / 256;
}

bool SPU2Controller::beginInputWrite(std::size_t core)
{
  auto &state = inputStates.at(core);
  if (autoDMAWriteSelection[core] == 0)
  {
    return false;
  }
  state.writeOpen = true;
  state.fillHalf = static_cast<std::uint8_t>((inputReadPosition / 256) ^ 1);
  state.fillWords = 0;
  return true;
}

void SPU2Controller::cancelInputWrite(std::size_t core)
{
  auto &state = inputStates.at(core);
  state.writeOpen = false;
  state.fillHalf = 0;
  state.fillWords = 0;
}

bool SPU2Controller::writeInputWord(std::size_t core, std::uint32_t value)
{
  if (!inputWriteRequested(core))
  {
    return false;
  }
  auto &state = inputStates[core];
  // One stereo half is planar: 128 left words followed by 128 right words.
  const auto address = static_cast<std::uint32_t>(0x2000 + core * 0x400 +
    state.fillHalf * 0x100 + (state.fillWords / 128) * 0x200 +
    (state.fillWords % 128) * 2);
  writeSoundRAMHalfword(address, static_cast<std::uint16_t>(value));
  writeSoundRAMHalfword(address + 1, static_cast<std::uint16_t>(value >> 16));
  ++state.fillWords;
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

bool SPU2Controller::isFixedMasterVolumeControl(std::uint16_t value) noexcept
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
  if (!isFixedMasterVolumeControl(value))
  {
    return false;
  }
  control = value;
  return true;
}

bool SPU2Controller::isSupportedCoreAttribute(std::uint16_t value) noexcept
{
  return (value & 0x3ffe) == 0;
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
}

bool SPU2Controller::hasQuiescentCoreStatus(std::size_t core) const
{
  return !inputStates.at(core).everSelected;
}

void SPU2Controller::keyOffUnprogrammedVoices() const noexcept
{
  // Every selected voice is unprogrammed; no release or pending command exists.
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
  selection = value;
  if (value == 0)
  {
    cancelInputWrite(core);
  }
  else
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
