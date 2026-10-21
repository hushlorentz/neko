#include "sif0_dmac_channel.hpp"

#include <cassert>
#include <stdexcept>

#include "dmac_controller.hpp"
#include "ee_bus.hpp"
#include "ee_memory_system.hpp"
#include "sif.hpp"

SIF0DMACChannel::SIF0DMACChannel(DMACController &controller, SIF &sif,
  EEBus &bus, EEMemorySystem &memory) :
  dmacController(controller),
  sifComponent(sif),
  eeBus(bus),
  eeMemorySystem(memory),
  channelState("SIF0", DMACChannelDirectionPolicy::Unrestricted,
    DMACChannelProfile::SIF0)
{
}

bool SIF0DMACChannel::active() const
{
  return channelState.active();
}

bool SIF0DMACChannel::transferEnabled() const
{
  return active() && dmacController.channelEnabled(DMACStatus::CHANNEL_5);
}

std::uint32_t SIF0DMACChannel::channelControl() const
{
  return channelState.channelControl();
}

void SIF0DMACChannel::writeChannelControl(std::uint32_t value)
{
  const bool wasActive = active();
  const auto effect = channelState.writeChannelControl(value);
  if (effect == DMACControlWriteEffect::ResetContinuation)
  {
    if (!wasActive && active())
    {
      sifComponent.applyEEDMAEvent(SIFDirection::SIF0, SIFEEDMAEvent::Start);
    }
    else if (wasActive && !active())
    {
      sifComponent.applyEEDMAEvent(SIFDirection::SIF0, SIFEEDMAEvent::Abort);
    }
  }
}

std::uint32_t SIF0DMACChannel::memoryAddress() const
{
  return channelState.memoryAddress();
}

void SIF0DMACChannel::writeMemoryAddress(std::uint32_t value)
{
  channelState.writeMemoryAddress(value);
}

std::uint32_t SIF0DMACChannel::quadwordCount() const
{
  return channelState.quadwordCount();
}

void SIF0DMACChannel::writeQuadwordCount(std::uint32_t value)
{
  channelState.writeQuadwordCount(value);
}

SIF0DMABeatOutcome SIF0DMACChannel::writeDestinationQuadword(
  std::uint32_t address, const EEQuadword &value)
{
  if ((address & 0x80000000) != 0)
  {
    const auto result = eeMemorySystem.writeScratchpadDMA128(
      address & (EEMemorySystem::SCRATCHPAD_SIZE - 1), value);
    if (result == EEScratchpadAccessResult::Wait)
    {
      return SIF0DMABeatOutcome::Wait;
    }
    return result == EEScratchpadAccessResult::Completed ?
      SIF0DMABeatOutcome::Completed : SIF0DMABeatOutcome::BusError;
  }
  if (address >= EEMemoryMap::MAIN_MEMORY_SIZE ||
      !eeBus.writeDMAC128(address, value))
  {
    return SIF0DMABeatOutcome::BusError;
  }
  return SIF0DMABeatOutcome::Completed;
}

SIF0DMABeatOutcome SIF0DMACChannel::transferBeat()
{
  if (!channelState.normalMode())
  {
    throw std::logic_error("SIF0 destination-chain tag handling is not implemented.");
  }
  if (!channelState.hasPendingQuadword())
  {
    completeTransfer();
    return SIF0DMABeatOutcome::Completed;
  }
  SIFQuadword words = {};
  if (sifComponent.peekWords(SIFDirection::SIF0, words, 4) ==
      SIFTransferOutcome::Wait)
  {
    return SIF0DMABeatOutcome::Wait;
  }
  const EEQuadword value = {
    static_cast<std::uint64_t>(words[0]) |
      (static_cast<std::uint64_t>(words[1]) << 32),
    static_cast<std::uint64_t>(words[2]) |
      (static_cast<std::uint64_t>(words[3]) << 32)};
  const auto result = writeDestinationQuadword(channelState.memoryAddress(), value);
  if (result != SIF0DMABeatOutcome::Completed)
  {
    if (result == SIF0DMABeatOutcome::BusError)
    {
      interruptForBusError();
    }
    return result;
  }
  const auto consumed = sifComponent.consumeWords(SIFDirection::SIF0, 4);
  assert(consumed == SIFTransferOutcome::Completed);
  (void)consumed;
  const auto transition = channelState.acceptQuadword();
  dmacController.publishStallAddress(DMACStatus::CHANNEL_5,
    channelState.memoryAddress());
  if (transition == DMACChannelTransition::Complete)
  {
    completeTransfer();
  }
  return SIF0DMABeatOutcome::Completed;
}

void SIF0DMACChannel::completeTransfer()
{
  channelState.completeTransfer();
  dmacController.signalChannelCompletion(DMACStatus::CHANNEL_5);
  sifComponent.applyEEDMAEvent(SIFDirection::SIF0, SIFEEDMAEvent::Complete);
}

void SIF0DMACChannel::interruptForBusError()
{
  channelState.completeTransfer();
  dmacController.signalBusError(DMACStatus::CHANNEL_5);
  sifComponent.applyEEDMAEvent(SIFDirection::SIF0, SIFEEDMAEvent::BusError);
}
