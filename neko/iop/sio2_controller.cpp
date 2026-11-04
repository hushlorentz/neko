#include "sio2_controller.hpp"

#include <stdexcept>

void SIO2Controller::reset() noexcept
{
  control = 0;
  portControls.fill(0);
  commandDescriptors.fill(0);
  clearTransmitInput();
  transferCompleted = false;
  receiveRemaining = 0;
  interruptCause = false;
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

SIO2AccessStatus SIO2Controller::startTransfer(
  std::uint32_t controlValue) noexcept
{
  if (controlValue != 0xb1 || receiveRemaining != 0 || interruptCause ||
      commandDescriptors[0] != 0x001c0740 || commandDescriptors[1] != 0 ||
      transmitInputCount != 7 || transmitInput[0] != 0x61)
  {
    return SIO2AccessStatus::Unsupported;
  }
  control = controlValue;
  clearTransmitInput();
  transferCompleted = true;
  receiveRemaining = 7;
  interruptCause = true;
  return SIO2AccessStatus::Accepted;
}

SIO2ReadResult SIO2Controller::readReceiveByte() noexcept
{
  if (receiveRemaining == 0)
  {
    return {};
  }
  --receiveRemaining;
  return {SIO2AccessStatus::Accepted, 0xff};
}

SIO2ReadResult SIO2Controller::readTransferStatus(std::size_t index) const
{
  static constexpr std::array<std::uint32_t, 3> status = {{0x1d100, 0xf, 0}};
  const auto value = status.at(index);
  return transferCompleted ? SIO2ReadResult{SIO2AccessStatus::Accepted, value}
    : SIO2ReadResult{};
}

std::uint32_t SIO2Controller::readInterruptStatus() const noexcept
{
  return interruptCause ? 1 : 0;
}

SIO2AccessStatus SIO2Controller::acknowledgeInterrupt(
  std::uint32_t value) noexcept
{
  if (value > 1)
  {
    return SIO2AccessStatus::Unsupported;
  }
  if (value == 1)
  {
    interruptCause = false;
  }
  return SIO2AccessStatus::Accepted;
}

bool SIO2Controller::hasValidTransferState() const noexcept
{
  if (receiveRemaining > 7)
  {
    return false;
  }
  if (!transferCompleted)
  {
    return receiveRemaining == 0 && !interruptCause &&
      (control == 0 || supportsControl(control));
  }
  return supportsControl(control) || control == 0xb1;
}

std::uint32_t SIO2Controller::readPortControl(std::size_t index) const
{
  return portControls.at(index);
}

void SIO2Controller::writePortControl(std::size_t index, std::uint32_t value)
{
  portControls.at(index) = value;
}

std::uint32_t SIO2Controller::readCommandDescriptor(std::size_t index) const
{
  return commandDescriptors.at(index);
}

void SIO2Controller::writeCommandDescriptor(std::size_t index, std::uint32_t value)
{
  commandDescriptors.at(index) = value;
  if (index == 0)
  {
    clearTransmitInput();
  }
}

void SIO2Controller::clearTransmitInput() noexcept
{
  transmitInput.fill(0);
  transmitInputCount = 0;
}

SIO2AccessStatus SIO2Controller::writeTransmitByte(std::uint8_t value) noexcept
{
  if (transmitInputCount >= transmitInput.size())
  {
    return SIO2AccessStatus::Unsupported;
  }
  transmitInput[transmitInputCount++] = value;
  return SIO2AccessStatus::Accepted;
}

std::size_t SIO2Controller::transmitInputSize() const noexcept
{
  return transmitInputCount;
}

std::uint8_t SIO2Controller::transmitInputByte(std::size_t index) const
{
  if (index >= transmitInputCount)
  {
    throw std::out_of_range("SIO2 transmit byte is outside the staged input");
  }
  return transmitInput[index];
}
