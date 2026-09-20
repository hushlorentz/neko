#include <stdexcept>

#include "dmac_controller.hpp"
#include "ee_bus.hpp"
#include "gif_dmac_channel.hpp"

GIFDMACChannel::GIFDMACChannel(
  EEBus *bus,
  DMACController *controller) :
  eeBus(bus),
  dmacController(controller),
  channelState(
    "GIF",
    DMACChannelDirectionPolicy::Unrestricted)
{
  if (eeBus == nullptr || dmacController == nullptr)
  {
    throw std::invalid_argument(
      "GIF DMAC channel requires non-null DMAC components.");
  }
}

bool GIFDMACChannel::clockActive() const
{
  return
    dmacController->enabled() &&
    channelState.active();
}

void GIFDMACChannel::clock()
{
  if (!clockActive())
  {
    return;
  }
  path3Stalled = false;
  if (channelState.hasPendingQuadword())
  {
    transferQuadword();
    return;
  }
  if (channelState.normalMode())
  {
    completeTransfer();
    return;
  }
  readSourceChainTag();
}

std::uint32_t GIFDMACChannel::channelControl() const
{
  return channelState.channelControl();
}

void GIFDMACChannel::writeChannelControl(std::uint32_t value)
{
  if (channelState.writeChannelControl(value) ==
      DMACControlWriteEffect::ResetContinuation)
  {
    path3Stalled = false;
  }
}

std::uint32_t GIFDMACChannel::memoryAddress() const
{
  return channelState.memoryAddress();
}

void GIFDMACChannel::writeMemoryAddress(std::uint32_t value)
{
  channelState.writeMemoryAddress(value);
}

std::uint32_t GIFDMACChannel::quadwordCount() const
{
  return channelState.quadwordCount();
}

void GIFDMACChannel::writeQuadwordCount(std::uint32_t value)
{
  channelState.writeQuadwordCount(value);
}

std::uint32_t GIFDMACChannel::tagAddress() const
{
  return channelState.tagAddress();
}

void GIFDMACChannel::writeTagAddress(std::uint32_t value)
{
  channelState.writeTagAddress(value);
}

std::uint32_t GIFDMACChannel::addressStack(
  std::size_t index) const
{
  return channelState.addressStack(index);
}

void GIFDMACChannel::writeAddressStack(
  std::size_t index,
  std::uint32_t value)
{
  channelState.writeAddressStack(index, value);
}

bool GIFDMACChannel::stalledByPATH3() const
{
  return path3Stalled;
}

std::uint64_t
GIFDMACChannel::transferredQuadwordCount() const
{
  return transferredQuadwords;
}

void GIFDMACChannel::transferQuadword()
{
  const GIFQuadword quadword =
    eeBus->readQuadword(channelState.memoryAddress());
  if (!eeBus->writeQuadword(EEMemoryMap::GIF_FIFO, quadword))
  {
    path3Stalled = true;
    return;
  }

  ++transferredQuadwords;
  if (channelState.acceptQuadword() ==
      DMACChannelTransition::Complete)
  {
    completeTransfer();
  }
}

void GIFDMACChannel::readSourceChainTag()
{
  const std::uint32_t currentTagAddress =
    channelState.tagAddress();
  const GIFQuadword tag = eeBus->readQuadword(currentTagAddress);
  if (channelState.tagTransferEnabled() &&
      !eeBus->writeQuadword(EEMemoryMap::GIF_FIFO, tag))
  {
    path3Stalled = true;
    return;
  }
  if (channelState.tagTransferEnabled())
  {
    ++transferredQuadwords;
  }
  if (channelState.acceptSourceChainTag(tag[0], tag[1]) ==
      DMACChannelTransition::Complete)
  {
    completeTransfer();
  }
}

void GIFDMACChannel::completeTransfer()
{
  channelState.completeTransfer();
  dmacController->signalChannelCompletion(
    DMACStatus::CHANNEL_2);
  path3Stalled = false;
}
