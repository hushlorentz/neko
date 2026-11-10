#include "sio2_controller.hpp"

#include <cassert>
#include <stdexcept>

void SIO2Controller::reset()
{
  if (ingressPhase == SIO2IngressPhase::DMAOpen ||
      resultPhase != SIO2DMAResultPhase::Empty)
    throw std::logic_error("SIO2 reset requires coordinated cancellation of DMA input/results");
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
  if (!supportsControl(value) || resultPhase != SIO2DMAResultPhase::Empty)
  {
    return SIO2AccessStatus::Unsupported;
  }
  control = value;
  return SIO2AccessStatus::Accepted;
}

SIO2TransferResult SIO2Controller::startTransfer(
  std::uint32_t controlValue) noexcept
{
  if (resultPhase != SIO2DMAResultPhase::Empty)
    return SIO2TransferResult::Unsupported;
  if (controlValue == 0x3bd)
  {
    if (ingressPhase != SIO2IngressPhase::DMASealed ||
        transmitInputCount != DMA_INPUT_BYTES || receiveRemaining != 0 ||
        !hasDMAProbeProgramming() || transmitInput[0] != 0x81 ||
        transmitInput[1] != 0x11)
      return SIO2TransferResult::Unsupported;
    for (std::size_t index = 2; index < DMA_INPUT_BYTES; ++index)
      if (transmitInput[index] != 0)
        return SIO2TransferResult::Unsupported;
    clearTransmitInput();
    control = controlValue;
    transferCompleted = false;
    resultPhase = SIO2DMAResultPhase::Pending;
    return SIO2TransferResult::Pending;
  }
  if (ingressPhase != SIO2IngressPhase::CPU ||
      controlValue != 0xb1 || receiveRemaining != 0 ||
      (commandDescriptors[0] != 0x001c0740 && commandDescriptors[0] != 0x001c0741) ||
      commandDescriptors[1] != 0 ||
      transmitInputCount != 7 || transmitInput[0] != 0x61)
  {
    return SIO2TransferResult::Unsupported;
  }
  const auto result = interruptCause ? SIO2TransferResult::Completed
    : SIO2TransferResult::CompletedWithInterrupt;
  control = controlValue;
  clearTransmitInput();
  transferCompleted = true;
  receiveRemaining = 7;
  interruptCause = true;
  return result;
}

bool SIO2Controller::hasDMAProbeProgramming() const noexcept
{
  if (commandDescriptors[0] != 0x00100472 &&
      commandDescriptors[0] != 0x00100473)
    return false;
  for (std::size_t index = 1; index < commandDescriptors.size(); ++index)
    if (commandDescriptors[index] != 0)
      return false;
  const auto port = commandDescriptors[0] & 3;
  return portControls[port * 2] == 0xff020405 &&
    portControls[port * 2 + 1] == 0x0005ffff;
}

SIO2TransferResult SIO2Controller::completePendingTransfer() noexcept
{
  if (resultPhase != SIO2DMAResultPhase::Pending)
    return SIO2TransferResult::Idle;
  resultPhase = SIO2DMAResultPhase::Ready;
  transferCompleted = true;
  const auto result = interruptCause ? SIO2TransferResult::Completed
    : SIO2TransferResult::CompletedWithInterrupt;
  interruptCause = true;
  return result;
}

SIO2DMAResultPhase SIO2Controller::dmaResultPhase() const noexcept
{
  return resultPhase;
}

std::size_t SIO2Controller::dmaResultSize() const noexcept
{
  return resultPhase == SIO2DMAResultPhase::Ready
    ? DMA_PROBE_REPLY_BYTES + DMA_PROBE_PADDING_BYTES : 0;
}

std::uint8_t SIO2Controller::dmaResultByte(std::size_t index) const
{
  if (index >= dmaResultSize())
    throw std::out_of_range("SIO2 DMA result byte is unavailable");
  return index < DMA_PROBE_REPLY_BYTES ? 0xff : 0;
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
  switch (resultPhase)
  {
    case SIO2DMAResultPhase::Empty: break;
    case SIO2DMAResultPhase::Pending:
    case SIO2DMAResultPhase::Ready:
      return ingressPhase == SIO2IngressPhase::Empty && transmitInputCount == 0 &&
        receiveRemaining == 0 && control == 0x3bd && hasDMAProbeProgramming() &&
        transferCompleted == (resultPhase == SIO2DMAResultPhase::Ready);
    default: return false;
  }
  switch (ingressPhase)
  {
    case SIO2IngressPhase::Empty:
      if (transmitInputCount != 0) return false;
      break;
    case SIO2IngressPhase::CPU:
      if (transmitInputCount == 0 || transmitInputCount > transmitInput.size()) return false;
      break;
    case SIO2IngressPhase::DMAOpen:
      if (transmitInputCount >= DMA_INPUT_BYTES || transmitInputCount % 4 != 0 ||
          receiveRemaining != 0) return false;
      break;
    case SIO2IngressPhase::DMASealed:
      if (transmitInputCount != DMA_INPUT_BYTES || receiveRemaining != 0) return false;
      break;
    default: return false;
  }
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

SIO2AccessStatus SIO2Controller::writePortControl(
  std::size_t index, std::uint32_t value)
{
  auto &port = portControls.at(index);
  if (resultPhase != SIO2DMAResultPhase::Empty)
    return SIO2AccessStatus::Unsupported;
  port = value;
  return SIO2AccessStatus::Accepted;
}

std::uint32_t SIO2Controller::readCommandDescriptor(std::size_t index) const
{
  return commandDescriptors.at(index);
}

SIO2AccessStatus SIO2Controller::writeCommandDescriptor(
  std::size_t index, std::uint32_t value)
{
  auto &descriptor = commandDescriptors.at(index);
  if (resultPhase != SIO2DMAResultPhase::Empty ||
      (index == 0 && ingressPhase == SIO2IngressPhase::DMAOpen))
    return SIO2AccessStatus::Unsupported;
  descriptor = value;
  if (index == 0)
  {
    clearTransmitInput();
  }
  return SIO2AccessStatus::Accepted;
}

void SIO2Controller::clearTransmitInput() noexcept
{
  transmitInput.fill(0);
  transmitInputCount = 0;
  ingressPhase = SIO2IngressPhase::Empty;
}

SIO2AccessStatus SIO2Controller::writeTransmitByte(std::uint8_t value) noexcept
{
  if (resultPhase != SIO2DMAResultPhase::Empty ||
      ingressPhase == SIO2IngressPhase::DMAOpen ||
      ingressPhase == SIO2IngressPhase::DMASealed ||
      transmitInputCount >= transmitInput.size())
  {
    return SIO2AccessStatus::Unsupported;
  }
  transmitInput[transmitInputCount++] = value;
  ingressPhase = SIO2IngressPhase::CPU;
  return SIO2AccessStatus::Accepted;
}

bool SIO2Controller::beginDMAInput() noexcept
{
  if (resultPhase != SIO2DMAResultPhase::Empty ||
      ingressPhase != SIO2IngressPhase::Empty || receiveRemaining != 0)
    return false;
  ingressPhase = SIO2IngressPhase::DMAOpen;
  return true;
}

bool SIO2Controller::canAcceptDMAWord() const noexcept
{
  return ingressPhase == SIO2IngressPhase::DMAOpen &&
    transmitInputCount <= DMA_INPUT_BYTES - 4;
}

void SIO2Controller::acceptDMAWord(std::uint32_t value) noexcept
{
  assert(canAcceptDMAWord());
  for (std::size_t lane = 0; lane < 4; ++lane)
    transmitInput[transmitInputCount++] = static_cast<std::uint8_t>(value >> (8 * lane));
}

void SIO2Controller::finishDMAInput() noexcept
{
  assert(ingressPhase == SIO2IngressPhase::DMAOpen &&
    transmitInputCount == DMA_INPUT_BYTES);
  ingressPhase = SIO2IngressPhase::DMASealed;
}

void SIO2Controller::cancelDMAInput() noexcept
{
  assert(ingressPhase == SIO2IngressPhase::DMAOpen);
  clearTransmitInput();
}

SIO2IngressPhase SIO2Controller::transmitInputPhase() const noexcept
{
  return ingressPhase;
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
