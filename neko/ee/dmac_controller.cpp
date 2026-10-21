#include <stdexcept>

#include "dmac_controller.hpp"
#include "dmac_channel_state.hpp"
#include "sif1_dmac_channel.hpp"
#include "sif0_dmac_channel.hpp"

namespace
{
  constexpr std::uint32_t CHANNELS =
    DMACStatus::CHANNEL_1 |
    DMACStatus::CHANNEL_2 |
    DMACStatus::CHANNEL_5 |
    DMACStatus::CHANNEL_6 |
    DMACStatus::CHANNEL_8 |
    DMACStatus::CHANNEL_9;
  constexpr std::uint32_t MASKS =
    DMACStatus::CHANNEL_1_MASK |
    DMACStatus::CHANNEL_2_MASK |
    DMACStatus::CHANNEL_5_MASK |
    DMACStatus::CHANNEL_6_MASK |
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
  if (!validControl(value))
  {
    throw std::invalid_argument(
      "Unsupported D_CTRL fields or excluded SIF0-source/SIF1-drain pair.");
  }
  controlRegister = value;
}

bool DMACController::validControl(std::uint32_t value)
{
  return (value & ~DMACControl::WRITABLE_MASK) == 0 &&
    !((value & DMACControl::STALL_SOURCE_MASK) == (1u << 4) &&
      (value & DMACControl::STALL_DRAIN_MASK) == (3u << 6));
}

std::uint32_t DMACController::stallAddress() const
{
  return stallAddressRegister;
}

void DMACController::writeStallAddress(std::uint32_t value)
{
  stallAddressRegister = value & 0x7ffffff0;
}

void DMACController::publishStallAddress(
  std::uint32_t channel, std::uint32_t address)
{
  const auto source = (controlRegister & DMACControl::STALL_SOURCE_MASK) >> 4;
  if ((source == 1 && channel == DMACStatus::CHANNEL_5) ||
      (source == 2 && channel == DMACStatus::CHANNEL_8))
  {
    writeStallAddress(address);
  }
}

bool DMACController::admitDrainSlice(
  std::uint32_t channel, const DMACChannelState &state)
{
  const auto drain = (controlRegister & DMACControl::STALL_DRAIN_MASK) >> 6;
  const bool selected =
    (drain == 1 && channel == DMACStatus::CHANNEL_1) ||
    (drain == 2 && channel == DMACStatus::CHANNEL_2) ||
    (drain == 3 && channel == DMACStatus::CHANNEL_6);
  if (!selected || !state.hasPendingQuadword() || state.sliceInProgress() ||
      (!state.normalMode() &&
       ((state.channelControl() >> 28) & 7) !=
         static_cast<std::uint32_t>(DMACTagID::ReferenceStall)))
  {
    return true;
  }
  // Provisional boundary policy: compare the actual aligned slice, not
  // eight qwords beyond a short or unaligned packet (EEUSER pp. 30, 38).
  const auto end = static_cast<std::uint64_t>(state.memoryAddress() & 0x7ffffff0) +
    static_cast<std::uint32_t>(state.nextSliceQuadwords()) * 16;
  if (end > stallAddressRegister)
  {
    statusRegister |= DMACStatus::STALL;
    return false;
  }
  return true;
}

std::uint32_t DMACController::status() const
{
  return statusRegister | statusMaskRegister;
}

void DMACController::writeStatus(std::uint32_t value)
{
  constexpr std::uint32_t STATUSES =
    CHANNELS | DMACStatus::BUS_ERROR | DMACStatus::STALL;
  statusRegister &= ~(value & STATUSES);
  statusMaskRegister ^= value & (MASKS | DMACStatus::STALL_MASK);
}

std::uint32_t DMACController::priorityControl() const
{
  return priorityControlRegister;
}

void DMACController::writePriorityControl(std::uint32_t value)
{
  priorityControlRegister = value & DMACPriority::WRITABLE_MASK;
}

DMACTagPriority DMACController::decodeTagPriority(std::uint32_t tag)
{
  switch ((tag >> 26) & 3)
  {
    case 0: return DMACTagPriority::Unchanged;
    case 2: return DMACTagPriority::Disable;
    case 3: return DMACTagPriority::Enable;
    default: throw std::invalid_argument("DMAC tag PCE=01 is reserved.");
  }
}

void DMACController::applyTagPriority(DMACTagPriority priority)
{
  switch (priority)
  {
    case DMACTagPriority::Unchanged:
      return;
    case DMACTagPriority::Disable:
      priorityControlRegister &= ~DMACPriority::ENABLE;
      return;
    case DMACTagPriority::Enable:
      priorityControlRegister |= DMACPriority::ENABLE;
      return;
  }
  throw std::invalid_argument("Invalid DMAC tag priority control.");
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

bool DMACController::channelEnabled(std::uint32_t channel) const
{
  if (!validChannel(channel))
  {
    throw std::invalid_argument("Invalid DMAC priority channel.");
  }
  return enabled() &&
    ((priorityControlRegister & DMACPriority::ENABLE) == 0 ||
     (priorityControlRegister & (channel << 16)) != 0);
}

SIF1DMABeatOutcome DMACController::clockSIF(SIF1DMACChannel &sif1)
{
  if (&sif1.dmacController != this)
  {
    throw std::invalid_argument("SIF grant requires this channel's DMAC controller.");
  }
  if (!sif1.transferEnabled())
  {
    return SIF1DMABeatOutcome::Wait;
  }
  return sif1.transferBeat();
}

SIF0DMABeatOutcome DMACController::clockSIF(SIF0DMACChannel &sif0)
{
  if (&sif0.dmacController != this)
  {
    throw std::invalid_argument("SIF grant requires this channel's DMAC controller.");
  }
  if (!sif0.transferEnabled())
  {
    return SIF0DMABeatOutcome::Wait;
  }
  return sif0.transferBeat();
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
     (CHANNELS | DMACStatus::STALL)) != 0;
}
