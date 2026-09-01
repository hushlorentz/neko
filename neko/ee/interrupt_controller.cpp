#include <stdexcept>

#include "interrupt_controller.hpp"

void EEInterruptController::setSource(
  std::uint8_t source,
  bool pending)
{
  if (source >= 32)
  {
    throw std::out_of_range(
      "EE interrupt source must be less than 32.");
  }
  const std::uint32_t sourceMask =
    EEInterruptSource::mask(source);
  if (pending)
  {
    statusRegister |= sourceMask;
  }
  else
  {
    statusRegister &= ~sourceMask;
  }
}

void EEInterruptController::acknowledge(std::uint32_t sources)
{
  statusRegister &= ~sources;
}

void EEInterruptController::toggleMask(std::uint32_t sources)
{
  maskRegister ^= sources;
}

std::uint32_t EEInterruptController::status() const
{
  return statusRegister;
}

std::uint32_t EEInterruptController::mask() const
{
  return maskRegister;
}

bool EEInterruptController::interruptPending() const
{
  return (statusRegister & maskRegister) != 0;
}
