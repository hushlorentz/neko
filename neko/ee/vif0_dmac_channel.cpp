#include <stdexcept>

#include "ee_bus.hpp"
#include "vif0_dmac_channel.hpp"

VIF0DMACChannel::VIF0DMACChannel() :
  channelState("VIF0", DMACChannelDirectionPolicy::Unrestricted)
{
}

bool VIF0DMACChannel::isRegisterAddress(std::uint32_t address)
{
  switch (address)
  {
    case EEMemoryMap::D0_CHCR:
    case EEMemoryMap::D0_MADR:
    case EEMemoryMap::D0_QWC:
    case EEMemoryMap::D0_TADR:
    case EEMemoryMap::D0_ASR0:
    case EEMemoryMap::D0_ASR1:
      return true;
    default:
      return false;
  }
}

std::uint32_t VIF0DMACChannel::channelControl() const
{
  return channelState.channelControl();
}

void VIF0DMACChannel::writeChannelControl(std::uint32_t value)
{
  if ((value & DMACChannelControl::START) != 0)
  {
    throw std::invalid_argument("VIF0 DMA execution is not implemented.");
  }
  channelState.writeChannelControl(value);
}

std::uint32_t VIF0DMACChannel::memoryAddress() const
{
  return channelState.memoryAddress();
}

void VIF0DMACChannel::writeMemoryAddress(std::uint32_t value)
{
  channelState.writeMemoryAddress(value);
}

std::uint32_t VIF0DMACChannel::quadwordCount() const
{
  return channelState.quadwordCount();
}

void VIF0DMACChannel::writeQuadwordCount(std::uint32_t value)
{
  channelState.writeQuadwordCount(value);
}

std::uint32_t VIF0DMACChannel::tagAddress() const
{
  return channelState.tagAddress();
}

void VIF0DMACChannel::writeTagAddress(std::uint32_t value)
{
  channelState.writeTagAddress(value);
}

std::uint32_t VIF0DMACChannel::addressStack(std::size_t index) const
{
  return channelState.addressStack(index);
}

void VIF0DMACChannel::writeAddressStack(std::size_t index, std::uint32_t value)
{
  channelState.writeAddressStack(index, value);
}
