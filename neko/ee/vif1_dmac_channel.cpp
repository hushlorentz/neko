#include <stdexcept>

#include "dmac_controller.hpp"
#include "ee_bus.hpp"
#include "gif_dmac_channel.hpp"
#include "vif1_dmac_channel.hpp"

VIF1DMACChannel::VIF1DMACChannel(
  EEBus *bus,
  DMACController *controller) :
  eeBus(bus),
  dmacController(controller),
  channelState(
    "VIF1",
    DMACChannelDirectionPolicy::FromMemory)
{
  if (eeBus == nullptr || dmacController == nullptr)
  {
    throw std::invalid_argument(
      "VIF1 DMAC channel requires non-null DMAC components.");
  }
}

bool VIF1DMACChannel::clockActive() const
{
  return
    dmacController->enabled() &&
    channelState.active();
}

void VIF1DMACChannel::clock()
{
  if (!clockActive())
  {
    return;
  }
  vif1Stalled = false;
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

std::uint32_t VIF1DMACChannel::channelControl() const
{
  return channelState.channelControl();
}

void VIF1DMACChannel::writeChannelControl(std::uint32_t value)
{
  if (channelState.writeChannelControl(value) ==
      DMACControlWriteEffect::ResetContinuation)
  {
    vif1Stalled = false;
  }
}

std::uint32_t VIF1DMACChannel::memoryAddress() const
{
  return channelState.memoryAddress();
}

void VIF1DMACChannel::writeMemoryAddress(std::uint32_t value)
{
  channelState.writeMemoryAddress(value);
}

std::uint32_t VIF1DMACChannel::quadwordCount() const
{
  return channelState.quadwordCount();
}

void VIF1DMACChannel::writeQuadwordCount(std::uint32_t value)
{
  channelState.writeQuadwordCount(value);
}

std::uint32_t VIF1DMACChannel::tagAddress() const
{
  return channelState.tagAddress();
}

void VIF1DMACChannel::writeTagAddress(std::uint32_t value)
{
  channelState.writeTagAddress(value);
}

std::uint32_t VIF1DMACChannel::addressStack(
  std::size_t index) const
{
  return channelState.addressStack(index);
}

void VIF1DMACChannel::writeAddressStack(
  std::size_t index,
  std::uint32_t value)
{
  channelState.writeAddressStack(index, value);
}

bool VIF1DMACChannel::stalledByVIF1() const
{
  return vif1Stalled;
}

std::uint64_t
VIF1DMACChannel::transferredQuadwordCount() const
{
  return transferredQuadwords;
}

bool VIF1DMACChannel::submitValue(
  const GIFQuadword &quadword)
{
  return
    eeBus->writeGuestData128(
      EEMemoryMap::VIF1_FIFO,
      {
        quadword[0] |
          (static_cast<std::uint64_t>(quadword[1]) << 32),
        quadword[2] |
          (static_cast<std::uint64_t>(quadword[3]) << 32)
      }) == EEDataWriteResult::Completed;
}

bool VIF1DMACChannel::submitQuadword(std::uint32_t address)
{
  return submitValue(eeBus->readQuadword(address));
}

bool VIF1DMACChannel::submitTag(std::uint32_t address)
{
  const GIFQuadword tag = eeBus->readQuadword(address);
  return submitValue({{0, 0, tag[2], tag[3]}});
}

void VIF1DMACChannel::transferQuadword()
{
  if (!submitQuadword(channelState.memoryAddress()))
  {
    vif1Stalled = true;
    return;
  }

  ++transferredQuadwords;
  if (channelState.acceptQuadword() ==
      DMACChannelTransition::Complete)
  {
    completeTransfer();
  }
}

void VIF1DMACChannel::readSourceChainTag()
{
  const std::uint32_t currentTagAddress =
    channelState.tagAddress();
  const GIFQuadword tag = eeBus->readQuadword(currentTagAddress);
  if (channelState.tagTransferEnabled() &&
      !submitTag(currentTagAddress))
  {
    vif1Stalled = true;
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

void VIF1DMACChannel::completeTransfer()
{
  channelState.completeTransfer();
  dmacController->signalChannelCompletion(
    DMACStatus::CHANNEL_1);
  vif1Stalled = false;
}
