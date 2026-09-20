#include <stdexcept>

#include "dmac_controller.hpp"

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
  constexpr std::uint32_t CHANNELS =
    DMACStatus::CHANNEL_1 |
    DMACStatus::CHANNEL_2;
  constexpr std::uint32_t MASKS =
    DMACStatus::CHANNEL_1_MASK |
    DMACStatus::CHANNEL_2_MASK;
  statusRegister &= ~(value & CHANNELS);
  statusMaskRegister ^= value & MASKS;
}

bool DMACController::enabled() const
{
  return
    (controlRegister & DMACControl::DMA_ENABLE) != 0;
}

void DMACController::signalChannelCompletion(
  std::uint32_t channel)
{
  constexpr std::uint32_t CHANNELS =
    DMACStatus::CHANNEL_1 |
    DMACStatus::CHANNEL_2;
  if ((channel & CHANNELS) == 0 ||
      (channel & ~CHANNELS) != 0)
  {
    throw std::invalid_argument(
      "Invalid DMAC completion channel.");
  }
  statusRegister |= channel;
}

bool DMACController::interruptPending() const
{
  const bool vif1 =
    (statusRegister & DMACStatus::CHANNEL_1) != 0 &&
    (statusMaskRegister &
     DMACStatus::CHANNEL_1_MASK) != 0;
  const bool gif =
    (statusRegister & DMACStatus::CHANNEL_2) != 0 &&
    (statusMaskRegister &
     DMACStatus::CHANNEL_2_MASK) != 0;
  return vif1 || gif;
}
