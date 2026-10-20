#include "sif1_dmac_channel.hpp"

#include <stdexcept>

#include "dmac_controller.hpp"
#include "ee_bus.hpp"
#include "ee_memory_system.hpp"
#include "sif.hpp"

SIF1DMACChannel::SIF1DMACChannel(DMACController &controller, SIF &sif,
  EEBus &bus, EEMemorySystem &memory) :
  dmacController(controller),
  sifComponent(sif),
  eeBus(bus),
  eeMemorySystem(memory),
  channelState("SIF1", DMACChannelDirectionPolicy::Unrestricted,
    DMACChannelProfile::SIF1)
{
}

bool SIF1DMACChannel::active() const
{
  return channelState.active();
}

bool SIF1DMACChannel::transferEnabled() const
{
  return active() && dmacController.enabled();
}

std::uint32_t SIF1DMACChannel::channelControl() const
{
  return channelState.channelControl();
}

void SIF1DMACChannel::writeChannelControl(std::uint32_t value)
{
  const bool wasActive = active();
  const auto effect = channelState.writeChannelControl(value);
  if (effect == DMACControlWriteEffect::ResetContinuation)
  {
    if (!wasActive && active())
    {
      sifComponent.applyEEDMAEvent(SIFDirection::SIF1, SIFEEDMAEvent::Start);
    }
    else if (wasActive && !active())
    {
      sifComponent.applyEEDMAEvent(SIFDirection::SIF1, SIFEEDMAEvent::Abort);
    }
  }
}

std::uint32_t SIF1DMACChannel::memoryAddress() const
{
  return channelState.memoryAddress();
}

void SIF1DMACChannel::writeMemoryAddress(std::uint32_t value)
{
  channelState.writeMemoryAddress(value);
}

std::uint32_t SIF1DMACChannel::quadwordCount() const
{
  return channelState.quadwordCount();
}

void SIF1DMACChannel::writeQuadwordCount(std::uint32_t value)
{
  channelState.writeQuadwordCount(value);
}

std::uint32_t SIF1DMACChannel::tagAddress() const
{
  return channelState.tagAddress();
}

void SIF1DMACChannel::writeTagAddress(std::uint32_t value)
{
  channelState.writeTagAddress(value);
}

SIF1DMABeatKind SIF1DMACChannel::nextBeatKind() const
{
  if (channelState.hasPendingQuadword())
  {
    return SIF1DMABeatKind::Payload;
  }
  if (channelState.normalMode())
  {
    return SIF1DMABeatKind::Completion;
  }
  return SIF1DMABeatKind::SourceTag;
}

SIF1DMABeatOutcome SIF1DMACChannel::transferBeat()
{
  switch (nextBeatKind())
  {
    case SIF1DMABeatKind::Payload:
      return transferQuadword();
    case SIF1DMABeatKind::SourceTag:
      return readSourceChainTag();
    case SIF1DMABeatKind::Completion:
      completeTransfer();
      return SIF1DMABeatOutcome::Completed;
  }
  throw std::logic_error("Unknown SIF1 DMA beat kind.");
}

SIF1DMABeatOutcome SIF1DMACChannel::readSourceQuadword(
  std::uint32_t address, EEQuadword *value) const
{
  if ((address & 0x80000000) != 0)
  {
    const auto result = eeMemorySystem.readScratchpadDMA128(
      address & (EEMemorySystem::SCRATCHPAD_SIZE - 1), value);
    if (result == EEScratchpadAccessResult::Wait)
    {
      return SIF1DMABeatOutcome::Wait;
    }
    return result == EEScratchpadAccessResult::Completed
      ? SIF1DMABeatOutcome::Completed : SIF1DMABeatOutcome::BusError;
  }
  if (address >= EEMemoryMap::MAIN_MEMORY_SIZE ||
      !eeBus.readDMAC128(address, value))
  {
    return SIF1DMABeatOutcome::BusError;
  }
  return SIF1DMABeatOutcome::Completed;
}

SIF1DMABeatOutcome SIF1DMACChannel::transferQuadword()
{
  EEQuadword value = {};
  const auto result = readSourceQuadword(channelState.memoryAddress(), &value);
  if (result != SIF1DMABeatOutcome::Completed)
  {
    if (result == SIF1DMABeatOutcome::BusError)
    {
      interruptForBusError();
    }
    return result;
  }
  const SIFQuadword words = {{
    static_cast<std::uint32_t>(value.low),
    static_cast<std::uint32_t>(value.low >> 32),
    static_cast<std::uint32_t>(value.high),
    static_cast<std::uint32_t>(value.high >> 32)}};
  if (sifComponent.pushSIF1Quadword(words) == SIFTransferOutcome::Wait)
  {
    return SIF1DMABeatOutcome::Wait;
  }
  if (channelState.acceptQuadword() == DMACChannelTransition::Complete)
  {
    completeTransfer();
  }
  return SIF1DMABeatOutcome::Completed;
}

SIF1DMABeatOutcome SIF1DMACChannel::readSourceChainTag()
{
  EEQuadword tag = {};
  const auto result = readSourceQuadword(channelState.tagAddress(), &tag);
  if (result != SIF1DMABeatOutcome::Completed)
  {
    if (result == SIF1DMABeatOutcome::BusError)
    {
      interruptForBusError();
    }
    return result;
  }
  if (channelState.acceptSourceChainTag(static_cast<std::uint32_t>(tag.low),
        static_cast<std::uint32_t>(tag.low >> 32)) == DMACChannelTransition::Complete)
  {
    completeTransfer();
  }
  return SIF1DMABeatOutcome::Completed;
}

void SIF1DMACChannel::completeTransfer()
{
  channelState.completeTransfer();
  dmacController.signalChannelCompletion(DMACStatus::CHANNEL_6);
  sifComponent.applyEEDMAEvent(SIFDirection::SIF1, SIFEEDMAEvent::Complete);
}

void SIF1DMACChannel::interruptForBusError()
{
  channelState.completeTransfer();
  dmacController.signalBusError(DMACStatus::CHANNEL_6);
  sifComponent.applyEEDMAEvent(SIFDirection::SIF1, SIFEEDMAEvent::BusError);
}
