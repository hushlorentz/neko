#include <stdexcept>

#include "ee_bus.hpp"
#include "ipu_dmac_channel.hpp"

IPUDMACChannel::IPUDMACChannel(IPUDMACChannelKind kind) :
  channelKind(kind),
  channelState(kind == IPUDMACChannelKind::FromIPU ? "fromIPU" : "toIPU",
    DMACChannelDirectionPolicy::Unrestricted, DMACChannelProfile::IPU)
{
  if (kind != IPUDMACChannelKind::FromIPU && kind != IPUDMACChannelKind::ToIPU)
  {
    throw std::invalid_argument("Unsupported IPU DMAC channel kind.");
  }
}

bool IPUDMACChannel::isRegisterAddress(std::uint32_t address)
{
  switch (address)
  {
    case EEMemoryMap::D3_CHCR:
    case EEMemoryMap::D3_MADR:
    case EEMemoryMap::D3_QWC:
    case EEMemoryMap::D4_CHCR:
    case EEMemoryMap::D4_MADR:
    case EEMemoryMap::D4_QWC:
    case EEMemoryMap::D4_TADR:
      return true;
    default:
      return false;
  }
}

IPUDMACChannelKind IPUDMACChannel::kind() const
{
  return channelKind;
}

std::uint32_t IPUDMACChannel::channelControl() const
{
  return channelState.channelControl();
}

void IPUDMACChannel::writeChannelControl(std::uint32_t value)
{
  if ((value & DMACChannelControl::START) != 0)
  {
    throw std::invalid_argument("IPU DMA execution is not implemented.");
  }
  channelState.writeChannelControl(value);
}

std::uint32_t IPUDMACChannel::memoryAddress() const
{
  return channelState.memoryAddress();
}

void IPUDMACChannel::writeMemoryAddress(std::uint32_t value)
{
  channelState.writeMemoryAddress(value);
}

std::uint32_t IPUDMACChannel::quadwordCount() const
{
  return channelState.quadwordCount();
}

void IPUDMACChannel::writeQuadwordCount(std::uint32_t value)
{
  channelState.writeQuadwordCount(value);
}

void IPUDMACChannel::requireTagAddress() const
{
  if (channelKind != IPUDMACChannelKind::ToIPU)
  {
    throw std::logic_error("fromIPU DMA has no tag-address register.");
  }
}

std::uint32_t IPUDMACChannel::tagAddress() const
{
  requireTagAddress();
  return channelState.tagAddress();
}

void IPUDMACChannel::writeTagAddress(std::uint32_t value)
{
  requireTagAddress();
  channelState.writeTagAddress(value);
}
