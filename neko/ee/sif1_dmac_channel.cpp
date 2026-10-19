#include "sif1_dmac_channel.hpp"

#include "dmac_controller.hpp"
#include "sif.hpp"

SIF1DMACChannel::SIF1DMACChannel(DMACController &controller, SIF &sif) :
  dmacController(controller),
  sifComponent(sif),
  channelState("SIF1", DMACChannelDirectionPolicy::Unrestricted,
    DMACSourceChainProfile::SIF1)
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
