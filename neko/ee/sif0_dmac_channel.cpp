#include "sif0_dmac_channel.hpp"

#include "dmac_controller.hpp"
#include "sif.hpp"

SIF0DMACChannel::SIF0DMACChannel(DMACController &controller, SIF &sif) :
  dmacController(controller),
  sifComponent(sif),
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
  return active() && dmacController.enabled();
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
