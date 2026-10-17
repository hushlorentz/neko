#include "iop_interrupt_controller.hpp"

#include <stdexcept>

constexpr std::uint8_t IOPInterruptController::SOURCE_COUNT;
constexpr IOPWord IOPInterruptController::SOURCE_MASK;

void IOPInterruptController::reset()
{
  statusRegister = 0;
  maskRegister = 0;
  controlRegister = 0;
}

void IOPInterruptController::assertSource(IOPInterruptSource source)
{
  const std::uint8_t index = static_cast<std::uint8_t>(source);
  if (index >= SOURCE_COUNT)
  {
    throw std::out_of_range(
      "IOP interrupt source must be less than 26.");
  }
  statusRegister |= UINT32_C(1) << index;
}

void IOPInterruptController::writeStatus(IOPWord value)
{
  statusRegister &= value;
}

void IOPInterruptController::writeMask(IOPWord value)
{
  maskRegister = value & SOURCE_MASK;
}

IOPWord IOPInterruptController::readControl()
{
  const IOPWord value = controlRegister;
  controlRegister = 0;
  return value;
}

void IOPInterruptController::writeControl(IOPWord value)
{
  controlRegister = value & 1;
}

IOPWord IOPInterruptController::status() const
{
  return statusRegister;
}

IOPWord IOPInterruptController::mask() const
{
  return maskRegister;
}

IOPWord IOPInterruptController::control() const
{
  return controlRegister;
}

bool IOPInterruptController::interruptPending() const
{
  return controlRegister != 0 && (statusRegister & maskRegister) != 0;
}
