#include "spu2_controller.hpp"

void SPU2Controller::reset() noexcept
{
  spdifConfiguration.fill(0);
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
