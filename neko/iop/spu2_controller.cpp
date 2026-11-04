#include "spu2_controller.hpp"

void SPU2Controller::reset() noexcept
{
  spdifConfiguration.fill(0);
  masterVolumeControls.fill(0);
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

bool SPU2Controller::writeUnknownB60Initialization(
  std::uint16_t value) const noexcept
{
  return value == 0;
}
