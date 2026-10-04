#include <stdexcept>
#include <string>

#include "dmac_channel_state.hpp"
#include "dmac_controller.hpp"
#include "scratchpad_dmac_channel.hpp"

namespace
{
  constexpr std::uint32_t MEMORY_ADDRESS_MASK = 0x7ffffff0;
  constexpr std::uint32_t TAG_ADDRESS_MASK = 0xfffffff0;
  constexpr std::uint32_t QWC_MASK = 0xffff;
  constexpr std::uint32_t SCRATCHPAD_ADDRESS_MASK = 0x3ff0;
  constexpr std::uint32_t CHANNEL_CONTROL_WRITABLE =
    DMACChannelControl::FROM_MEMORY |
    DMACChannelControl::MODE_MASK |
    DMACChannelControl::ADDRESS_STACK_MASK |
    DMACChannelControl::TAG_TRANSFER_ENABLE |
    DMACChannelControl::TAG_INTERRUPT_ENABLE |
    DMACChannelControl::START |
    DMACChannelControl::TAG_MASK;
}

ScratchpadDMACChannel::ScratchpadDMACChannel(
  ScratchpadDMACChannelKind kind,
  DMACController *controller) :
  channelKind(kind),
  dmacController(controller)
{
  if (channelKind !=
        ScratchpadDMACChannelKind::FromScratchpad &&
      channelKind !=
        ScratchpadDMACChannelKind::ToScratchpad)
  {
    throw std::invalid_argument(
      "Scratchpad DMAC channel kind is invalid.");
  }
  if (dmacController == nullptr)
  {
    throw std::invalid_argument(
      "Scratchpad DMAC channel requires a controller.");
  }
}

bool ScratchpadDMACChannel::clockActive() const
{
  return active() && dmacController->enabled();
}

void ScratchpadDMACChannel::clock()
{
}

bool ScratchpadDMACChannel::active() const
{
  return
    (channelControlRegister &
     DMACChannelControl::START) != 0;
}

std::uint32_t ScratchpadDMACChannel::channelControl() const
{
  return channelControlRegister;
}

void ScratchpadDMACChannel::writeChannelControl(
  std::uint32_t value)
{
  if ((value & ~CHANNEL_CONTROL_WRITABLE) != 0)
  {
    throw std::invalid_argument(
      std::string(name()) +
      " DMAC CHCR contains unsupported bits.");
  }
  if (active())
  {
    const std::uint32_t changedFields =
      (channelControlRegister ^ value) &
      ~DMACChannelControl::START;
    if (changedFields != 0)
    {
      throw std::logic_error(
        std::string(name()) +
        " DMAC control fields cannot change while active.");
    }
  }
  channelControlRegister = value;
}

std::uint32_t ScratchpadDMACChannel::memoryAddress() const
{
  return memoryAddressRegister;
}

void ScratchpadDMACChannel::writeMemoryAddress(
  std::uint32_t value)
{
  requireStopped();
  memoryAddressRegister = value & MEMORY_ADDRESS_MASK;
}

std::uint32_t ScratchpadDMACChannel::quadwordCount() const
{
  return quadwordCountRegister;
}

void ScratchpadDMACChannel::writeQuadwordCount(
  std::uint32_t value)
{
  requireStopped();
  quadwordCountRegister = value & QWC_MASK;
}

std::uint32_t ScratchpadDMACChannel::tagAddress() const
{
  requireTagAddress();
  return tagAddressRegister;
}

void ScratchpadDMACChannel::writeTagAddress(
  std::uint32_t value)
{
  requireTagAddress();
  requireStopped();
  tagAddressRegister = value & TAG_ADDRESS_MASK;
}

std::uint32_t ScratchpadDMACChannel::scratchpadAddress() const
{
  return scratchpadAddressRegister;
}

void ScratchpadDMACChannel::writeScratchpadAddress(
  std::uint32_t value)
{
  requireStopped();
  scratchpadAddressRegister =
    value & SCRATCHPAD_ADDRESS_MASK;
}

void ScratchpadDMACChannel::requireStopped() const
{
  if (active())
  {
    throw std::logic_error(
      std::string(name()) +
      " DMAC channel registers cannot change while active.");
  }
}

void ScratchpadDMACChannel::requireTagAddress() const
{
  if (channelKind !=
      ScratchpadDMACChannelKind::ToScratchpad)
  {
    throw std::logic_error(
      "fromSPR DMAC channel has no TADR.");
  }
}

const char *ScratchpadDMACChannel::name() const
{
  return
    channelKind ==
      ScratchpadDMACChannelKind::FromScratchpad ?
      "fromSPR" :
      "toSPR";
}
