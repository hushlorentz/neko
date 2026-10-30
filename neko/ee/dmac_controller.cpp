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

bool DMACController::isAbsentChannelRegisterAddress(std::uint32_t address)
{
  // EE User's Manual 3.1 p59: optional TADR, ASR0/1 and SADR inventory.
  switch (address)
  {
    case 0x10008080:
    case 0x10009080:
    case 0x1000a080:
    case 0x1000b030:
    case 0x1000b040:
    case 0x1000b050:
    case 0x1000b080:
    case 0x1000b440:
    case 0x1000b450:
    case 0x1000b480:
    case 0x1000c030:
    case 0x1000c040:
    case 0x1000c050:
    case 0x1000c080:
    case 0x1000c440:
    case 0x1000c450:
    case 0x1000c480:
    case 0x1000c830:
    case 0x1000c840:
    case 0x1000c850:
    case 0x1000c880:
    case 0x1000d030:
    case 0x1000d040:
    case 0x1000d050:
    case 0x1000d440:
    case 0x1000d450:
      return true;
    default:
      return false;
  }
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

std::uint32_t DMACController::ringBufferSizeMask() const
{
  return ringBufferSizeMaskRegister;
}

void DMACController::writeRingBufferSizeMask(std::uint32_t value)
{
  ringBufferSizeMaskRegister = value & DMACRingBuffer::FIELD_MASK;
}

std::uint32_t DMACController::ringBufferBaseAddress() const
{
  return ringBufferBaseAddressRegister;
}

void DMACController::writeRingBufferBaseAddress(std::uint32_t value)
{
  ringBufferBaseAddressRegister = value & DMACRingBuffer::FIELD_MASK;
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

std::uint32_t DMACController::holdControl() const
{
  return holdControlRegister;
}

void DMACController::writeHoldControl(std::uint32_t value)
{
  holdControlRegister = value;
}

bool DMACController::held() const
{
  return (holdControlRegister & DMACHold::CPND) != 0;
}

bool DMACController::channelEnabled(std::uint32_t channel) const
{
  if (!validChannel(channel))
  {
    throw std::invalid_argument("Invalid DMAC priority channel.");
  }
  return enabled() && !held() &&
    ((priorityControlRegister & DMACPriority::ENABLE) == 0 ||
     (priorityControlRegister & (channel << 16)) != 0);
}

std::array<SIFDirection, 2> DMACController::sifGrantOrder() const
{
  return {{sifNextDirection, sifNextDirection == SIFDirection::SIF0 ?
    SIFDirection::SIF1 : SIFDirection::SIF0}};
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
  const auto result = sif1.transferBeat();
  if (result != SIF1DMABeatOutcome::Wait)
  {
    sifNextDirection = SIFDirection::SIF0;
  }
  return result;
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
  const auto result = sif0.transferBeat();
  if (result != SIF0DMABeatOutcome::Wait)
  {
    sifNextDirection = SIFDirection::SIF1;
  }
  return result;
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
