#include "sio2_controller.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace
{
  constexpr std::array<std::uint32_t, 3> absentTransferStatus = {{0x1d100, 0xf, 0}};
}

constexpr std::size_t SIO2Controller::DMA_PROBE_PADDING_BYTES;

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
  cpuResult = {};
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
  const bool completedRewrite = value == 0x3bd && control == value &&
    transferCompleted && ingressPhase == SIO2IngressPhase::Empty &&
    transmitInputCount == 0 && resultCursor == 0 && !hasUnreadCPUResult() &&
    (dmaCardProfile() != nullptr || hasCPUCardReadProgramming() ||
      hasCPUMultitapSupportProgramming());
  if ((!supportsControl(value) && !completedRewrite) ||
      resultPhase != SIO2DMAResultPhase::Empty)
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
  if (ingressPhase == SIO2IngressPhase::CPU)
    return startCPUTransfer(controlValue);
  if (controlValue == 0x3bd && ingressPhase == SIO2IngressPhase::DMASealed)
    return startDMACardTransfer();
  return SIO2TransferResult::Unsupported;
}

SIO2TransferResult SIO2Controller::startDMACardTransfer() noexcept
{
  const auto *profile = dmaCardProfile();
  if (transmitInputCount != DMA_INPUT_BYTES || hasUnreadCPUResult() ||
      profile == nullptr || transmitInput[0] != 0x81 ||
      transmitInput[1] != profile->command)
    return SIO2TransferResult::Unsupported;
  for (std::size_t index = 2; index < DMA_INPUT_BYTES; ++index)
    if (transmitInput[index] != 0)
      return SIO2TransferResult::Unsupported;
  clearTransmitInput();
  cpuResult = {};
  control = 0x3bd;
  transferCompleted = false;
  resultPhase = SIO2DMAResultPhase::Pending;
  return SIO2TransferResult::Pending;
}

SIO2TransferResult SIO2Controller::startCPUTransfer(
  std::uint32_t controlValue) noexcept
{
  if (hasUnreadCPUResult())
    return SIO2TransferResult::Unsupported;
  const auto reply = classifyCPUTransfer(controlValue);
  if (reply.profile == CPUProfile::None)
    return SIO2TransferResult::Unsupported;
  return completeCPUTransfer(controlValue, reply);
}

SIO2Controller::CPUResult SIO2Controller::classifyCPUTransfer(
  std::uint32_t controlValue) const noexcept
{
  CPUResult reply;
  if (controlValue == 0xb1 &&
      (commandDescriptors[0] == 0x001c0740 || commandDescriptors[0] == 0x001c0741) &&
      commandDescriptors[1] == 0 && transmitInputCount == 7 &&
      transmitInput[0] == 0x61)
  {
    reply.profile = CPUProfile::Infrared;
    reply.length = 7;
  }
  else if (controlValue == 0x3bd && hasCPUCardReadProgramming() &&
      transmitInputCount == CPU_CARD_REPLY_BYTES &&
      transmitInput[0] == 0x81 && transmitInput[1] == 0x52 && transmitInput[2] == 0)
  {
    reply.profile = CPUProfile::CardRead;
    reply.length = CPU_CARD_REPLY_BYTES;
  }
  else if (controlValue == 0x3bd && hasCPUMultitapSupportProgramming() &&
      transmitInputCount == CPU_MULTITAP_REPLY_BYTES &&
      transmitInput[0] == 0x21 &&
      (transmitInput[1] == 0x12 || transmitInput[1] == 0x13))
  {
    for (std::size_t index = 2; index < CPU_MULTITAP_REPLY_BYTES; ++index)
      if (transmitInput[index] != 0)
        return {};
    reply.profile = transmitInput[1] == 0x12
      ? CPUProfile::MultitapSupport12 : CPUProfile::MultitapSupport13;
    reply.length = CPU_MULTITAP_REPLY_BYTES;
  }
  else
  {
    return {};
  }
  reply.endpoint = static_cast<std::uint8_t>(commandDescriptors[0] & 3);
  std::fill_n(reply.bytes.begin(), reply.length, std::uint8_t{0xff});
  reply.status = absentTransferStatus;
  return reply;
}

SIO2TransferResult SIO2Controller::completeCPUTransfer(
  std::uint32_t controlValue, const CPUResult &reply) noexcept
{
  const auto result = interruptCause ? SIO2TransferResult::Completed
    : SIO2TransferResult::CompletedWithInterrupt;
  control = controlValue;
  clearTransmitInput();
  transferCompleted = true;
  cpuResult = reply;
  interruptCause = true;
  return result;
}

bool SIO2Controller::hasCPUCardReadProgramming() const noexcept
{
  return hasPairedCPUProgramming(0x000c0342, 0xffc00505, 0x000201f4);
}

bool SIO2Controller::hasCPUMultitapSupportProgramming() const noexcept
{
  return hasPairedCPUProgramming(0x00180642, 0xff020505, 0x00030064);
}

bool SIO2Controller::hasPairedCPUProgramming(std::uint32_t firstDescriptor,
  std::uint32_t firstPortControl, std::uint32_t secondPortControl) const noexcept
{
  if (commandDescriptors[0] != firstDescriptor &&
      commandDescriptors[0] != firstDescriptor + 1)
    return false;
  for (std::size_t index = 1; index < commandDescriptors.size(); ++index)
    if (commandDescriptors[index] != 0)
      return false;
  const auto port = commandDescriptors[0] & 3;
  return portControls[port * 2] == firstPortControl &&
    portControls[port * 2 + 1] == secondPortControl;
}

const SIO2Controller::DMACardProfile *SIO2Controller::dmaCardProfile() const noexcept
{
  static constexpr DMACardProfile probe{0x11, DMA_PROBE_REPLY_BYTES};
  static constexpr DMACardProfile absentF3{0xf3, 5};
  const DMACardProfile *profile = nullptr;
  switch (commandDescriptors[0])
  {
    case 0x00100472:
    case 0x00100473: profile = &probe; break;
    case 0x00140572:
    case 0x00140573: profile = &absentF3; break;
    default: return nullptr;
  }
  for (std::size_t index = 1; index < commandDescriptors.size(); ++index)
    if (commandDescriptors[index] != 0)
      return nullptr;
  const auto port = commandDescriptors[0] & 3;
  return portControls[port * 2] == 0xff020405 &&
    portControls[port * 2 + 1] == 0x0005ffff ? profile : nullptr;
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
    ? DMA_INPUT_BYTES - resultCursor : 0;
}

std::size_t SIO2Controller::dmaResultLogicalSize() const noexcept
{
  if (resultPhase != SIO2DMAResultPhase::Ready)
    return 0;
  const auto *profile = dmaCardProfile();
  assert(profile != nullptr);
  return resultCursor < profile->replyBytes ? profile->replyBytes - resultCursor : 0;
}

std::uint8_t SIO2Controller::dmaResultByte(std::size_t index) const
{
  if (index >= dmaResultSize())
    throw std::out_of_range("SIO2 DMA result byte is unavailable");
  return index < dmaResultLogicalSize() ? 0xff : 0;
}

std::uint32_t SIO2Controller::peekDMAResultWord() const noexcept
{
  assert(dmaResultSize() >= 4);
  const auto logicalBytes = dmaResultLogicalSize();
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4 && index < logicalBytes; ++index)
    value |= std::uint32_t{0xff} << (index * 8);
  return value;
}

void SIO2Controller::consumeDMAResultWord() noexcept
{
  assert(dmaResultSize() >= 4);
  resultCursor += 4;
  if (resultCursor == DMA_INPUT_BYTES)
  {
    resultPhase = SIO2DMAResultPhase::Empty;
    resultCursor = 0;
  }
}

SIO2ReadResult SIO2Controller::readReceiveByte() noexcept
{
  if (!hasUnreadCPUResult())
  {
    return {};
  }
  return {SIO2AccessStatus::Accepted, cpuResult.bytes[cpuResult.cursor++]};
}

SIO2ReadResult SIO2Controller::readTransferStatus(std::size_t index) const
{
  const auto &status = cpuResult.profile == CPUProfile::None
    ? absentTransferStatus : cpuResult.status;
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

bool SIO2Controller::hasUnreadCPUResult() const noexcept
{
  return cpuResult.cursor < cpuResult.length;
}

bool SIO2Controller::hasValidCPUResult() const noexcept
{
  std::uint8_t expectedLength = 0;
  switch (cpuResult.profile)
  {
    case CPUProfile::None:
      if (cpuResult.endpoint != 0) return false;
      break;
    case CPUProfile::Infrared:
      if (cpuResult.endpoint > 1) return false;
      expectedLength = 7;
      break;
    case CPUProfile::CardRead:
      if (cpuResult.endpoint != 2 && cpuResult.endpoint != 3) return false;
      expectedLength = CPU_CARD_REPLY_BYTES;
      break;
    case CPUProfile::MultitapSupport12:
    case CPUProfile::MultitapSupport13:
      if (cpuResult.endpoint != 2 && cpuResult.endpoint != 3) return false;
      expectedLength = CPU_MULTITAP_REPLY_BYTES;
      break;
    default: return false;
  }
  if (cpuResult.length != expectedLength || cpuResult.cursor > cpuResult.length)
    return false;
  for (std::size_t index = 0; index < cpuResult.bytes.size(); ++index)
    if (cpuResult.bytes[index] != (index < expectedLength ? 0xff : 0))
      return false;
  const std::array<std::uint32_t, 3> emptyStatus{};
  return cpuResult.status == (expectedLength == 0 ? emptyStatus : absentTransferStatus);
}

bool SIO2Controller::hasValidTransferState() const noexcept
{
  if (!hasValidCPUResult() ||
      (cpuResult.profile != CPUProfile::None && !transferCompleted))
    return false;
  if (resultCursor % 4 != 0 || resultCursor >= DMA_INPUT_BYTES ||
      (resultPhase != SIO2DMAResultPhase::Ready && resultCursor != 0))
    return false;
  switch (resultPhase)
  {
    case SIO2DMAResultPhase::Empty: break;
    case SIO2DMAResultPhase::Pending:
    case SIO2DMAResultPhase::Ready:
      return ingressPhase == SIO2IngressPhase::Empty && transmitInputCount == 0 &&
        cpuResult.profile == CPUProfile::None &&
        control == 0x3bd && dmaCardProfile() != nullptr &&
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
          hasUnreadCPUResult()) return false;
      break;
    case SIO2IngressPhase::DMASealed:
      if (transmitInputCount != DMA_INPUT_BYTES || hasUnreadCPUResult()) return false;
      break;
    default: return false;
  }
  if (!transferCompleted)
  {
    return !hasUnreadCPUResult() && !interruptCause &&
      (control == 0 || supportsControl(control));
  }
  if (cpuResult.profile == CPUProfile::None)
    return supportsControl(control) || control == 0x3bd;
  return supportsControl(control) || control == 0xb1 ||
    (control == 0x3bd &&
      cpuResult.length - cpuResult.cursor <= CPU_MULTITAP_REPLY_BYTES);
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
      ingressPhase != SIO2IngressPhase::Empty || hasUnreadCPUResult())
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
