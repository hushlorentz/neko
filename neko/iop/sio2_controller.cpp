#include "sio2_controller.hpp"

void SIO2Controller::reset() noexcept
{
  control = 0;
  portControls.fill(0);
}

std::uint32_t SIO2Controller::readControl() const noexcept
{
  return control;
}

bool SIO2Controller::supportsControl(std::uint32_t value) noexcept
{
  return value == 0xc || value == 0x3bc;
}

SIO2AccessStatus SIO2Controller::writeControl(std::uint32_t value) noexcept
{
  if (!supportsControl(value))
  {
    return SIO2AccessStatus::Unsupported;
  }
  control = value;
  return SIO2AccessStatus::Accepted;
}

std::uint32_t SIO2Controller::readPortControl(std::size_t index) const
{
  return portControls.at(index);
}

void SIO2Controller::writePortControl(std::size_t index, std::uint32_t value)
{
  portControls.at(index) = value;
}
