#include "iop_interrupt_controller.hpp"

#include <stdexcept>

constexpr std::uint8_t IOPInterruptController::SOURCE_COUNT;
constexpr IOPWord IOPInterruptController::SOURCE_MASK;

void IOPInterruptController::reset()
{
  clearObservations();
  statusRegister = 0;
  maskRegister = 0;
  controlRegister = 0;
}

void IOPInterruptController::enableObservations(NekoObservationOrder *order)
{
  clearObservations();
  observationOrder = order;
  observationsEnabled = true;
}

void IOPInterruptController::disableObservations()
{
  clearObservations();
  observationsEnabled = false;
  observationOrder = nullptr;
}

void IOPInterruptController::clearObservations()
{
  observationCount = 0;
}

IOPInterruptController::Observation IOPInterruptController::snapshot() const
{
  return {statusRegister, maskRegister, controlRegister};
}

void IOPInterruptController::recordTransition(const Observation &before)
{
  if (!observationsEnabled ||
      (before.status == statusRegister && before.mask == maskRegister &&
       before.control == controlRegister))
  {
    return;
  }
  if (observationCount >= observations.size())
  {
    throw std::logic_error("IOP INTC observation capacity exceeded.");
  }
  observations[observationCount] = snapshot();
  observations[observationCount].sequence =
    observationOrder != nullptr ? observationOrder->next++ : observationCount;
  ++observationCount;
}

void IOPInterruptController::assertSource(IOPInterruptSource source)
{
  const std::uint8_t index = static_cast<std::uint8_t>(source);
  if (index >= SOURCE_COUNT)
  {
    throw std::out_of_range(
      "IOP interrupt source must be less than 26.");
  }
  const auto before = snapshot();
  statusRegister |= UINT32_C(1) << index;
  recordTransition(before);
}

void IOPInterruptController::writeStatus(IOPWord value)
{
  const auto before = snapshot();
  statusRegister &= value;
  recordTransition(before);
}

void IOPInterruptController::writeMask(IOPWord value)
{
  const auto before = snapshot();
  maskRegister = value & SOURCE_MASK;
  recordTransition(before);
}

IOPWord IOPInterruptController::readControl()
{
  const IOPWord value = controlRegister;
  const auto before = snapshot();
  controlRegister = 0;
  recordTransition(before);
  return value;
}

void IOPInterruptController::writeControl(IOPWord value)
{
  const auto before = snapshot();
  controlRegister = value & 1;
  recordTransition(before);
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
