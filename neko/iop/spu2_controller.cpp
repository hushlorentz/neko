#include "spu2_controller.hpp"

void SPU2Controller::reset() noexcept
{
  spdifConfiguration.fill(0);
  masterVolumeControls.fill(0);
  coreAttributes.fill(0);
  voiceMix.fill(0);
  mainMix.fill(0);
}

std::uint16_t SPU2Controller::readSPDIFConfiguration(std::size_t index) const
{
  return spdifConfiguration.at(index);
}

void SPU2Controller::writeSPDIFConfiguration(
  std::size_t index, std::uint16_t value)
{
  spdifConfiguration.at(index) = value;
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
  if (!isSupportedCoreAttribute(value))
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

std::uint16_t SPU2Controller::readUnprogrammedCoreStatus() const noexcept
{
  return 0;
}

void SPU2Controller::keyOffUnprogrammedVoices() const noexcept
{
  // Every selected voice is unprogrammed; no release or pending command exists.
}

bool SPU2Controller::writeUnprogrammedAutoDMAControl(
  std::uint16_t value) const noexcept
{
  return value == 0;
}

bool SPU2Controller::writeUnknownInitialization(
  std::uint16_t value) const noexcept
{
  return value == 0;
}
