#include <stdexcept>

#include "dmac_controller.hpp"

namespace
{
  constexpr std::uint32_t CHANNELS =
    DMACStatus::CHANNEL_1 |
    DMACStatus::CHANNEL_2 |
    DMACStatus::CHANNEL_8 |
    DMACStatus::CHANNEL_9;
  constexpr std::uint32_t MASKS =
    DMACStatus::CHANNEL_1_MASK |
    DMACStatus::CHANNEL_2_MASK |
    DMACStatus::CHANNEL_8_MASK |
    DMACStatus::CHANNEL_9_MASK;

  bool validChannel(std::uint32_t channel)
  {
    return
      (channel & CHANNELS) != 0 &&
      (channel & ~CHANNELS) == 0 &&
      (channel & (channel - 1)) == 0;
  }
}

std::uint32_t DMACController::control() const
{
  return controlRegister;
}

void DMACController::writeControl(std::uint32_t value)
{
  if ((value & ~DMACControl::DMA_ENABLE) != 0)
  {
    throw std::invalid_argument(
      "Only D_CTRL.DMAE is implemented.");
  }
  controlRegister = value;
}

std::uint32_t DMACController::status() const
{
  return statusRegister | statusMaskRegister;
}

void DMACController::writeStatus(std::uint32_t value)
{
  constexpr std::uint32_t STATUSES =
    CHANNELS | DMACStatus::BUS_ERROR;
  statusRegister &= ~(value & STATUSES);
  statusMaskRegister ^= value & MASKS;
}

std::uint32_t DMACController::interleaveSize() const
{
  return interleaveSizeRegister;
}

void DMACController::writeInterleaveSize(
  std::uint32_t value)
{
  interleaveSizeRegister =
    value &
    (DMACInterleave::SKIP_MASK |
     DMACInterleave::TRANSFER_MASK);
}

std::uint8_t DMACController::interleaveSkipQWC() const
{
  return static_cast<std::uint8_t>(
    interleaveSizeRegister &
    DMACInterleave::SKIP_MASK);
}

std::uint8_t DMACController::interleaveTransferQWC() const
{
  return static_cast<std::uint8_t>(
    (interleaveSizeRegister &
     DMACInterleave::TRANSFER_MASK) >> 16);
}

bool DMACController::enabled() const
{
  return
    (controlRegister & DMACControl::DMA_ENABLE) != 0;
}

void DMACController::signalChannelCompletion(
  std::uint32_t channel)
{
  if (!validChannel(channel))
  {
    throw std::invalid_argument(
      "Invalid DMAC completion channel.");
  }
  statusRegister |= channel;
}

void DMACController::signalBusError(std::uint32_t channel)
{
  if (!validChannel(channel))
  {
    throw std::invalid_argument(
      "Invalid DMAC bus-error channel.");
  }
  statusRegister |= channel | DMACStatus::BUS_ERROR;
}

bool DMACController::interruptPending() const
{
  return
    (statusRegister & DMACStatus::BUS_ERROR) != 0 ||
    (statusRegister &
     (statusMaskRegister >> 16) &
     CHANNELS) != 0;
}
