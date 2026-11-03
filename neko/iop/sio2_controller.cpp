#include "sio2_controller.hpp"

void SIO2Controller::reset() noexcept
{
  control = 0;
}

std::uint32_t SIO2Controller::readControl() const noexcept
{
  return control;
}

SIO2AccessStatus SIO2Controller::writeStartupControl(
  std::uint32_t value) noexcept
{
  if (value != 0x3bc)
  {
    return SIO2AccessStatus::Unsupported;
  }
  control = value;
  return SIO2AccessStatus::Accepted;
}
