#include <stdexcept>
#include <string>

#include "ee_bus.hpp"
#include "gif_dmac_channel.hpp"

namespace
{
  constexpr std::uint32_t ADDRESS_MASK = 0x7ffffff0;
  constexpr std::uint32_t SPR_BIT = UINT32_C(0x80000000);
  constexpr std::uint32_t QWC_MASK = 0xffff;
  constexpr std::uint32_t TAG_IRQ = UINT32_C(0x80000000);
  constexpr std::uint32_t CHANNEL_CONTROL_WRITABLE =
    GIFDMACChannelControl::FROM_MEMORY |
    GIFDMACChannelControl::MODE_MASK |
    GIFDMACChannelControl::ADDRESS_STACK_MASK |
    GIFDMACChannelControl::TAG_TRANSFER_ENABLE |
    GIFDMACChannelControl::TAG_INTERRUPT_ENABLE |
    GIFDMACChannelControl::START |
    GIFDMACChannelControl::TAG_MASK;
}

GIFDMACChannel::GIFDMACChannel(EEBus *bus) :
  eeBus(bus)
{
  if (eeBus == nullptr)
  {
    throw std::invalid_argument(
      "GIF DMAC channel requires a non-null EE bus.");
  }
}

bool GIFDMACChannel::clockActive() const
{
  return
    (globalControlRegister & GIFDMACControl::DMA_ENABLE) != 0 &&
    (channelControlRegister &
     GIFDMACChannelControl::START) != 0;
}

void GIFDMACChannel::clock()
{
  if (!clockActive())
  {
    return;
  }
  path3Stalled = false;
  if (quadwordCountRegister != 0)
  {
    transferQuadword();
    return;
  }
  if ((channelControlRegister &
       GIFDMACChannelControl::MODE_MASK) == 0)
  {
    completeTransfer();
    return;
  }
  readSourceChainTag();
}

std::uint32_t GIFDMACChannel::channelControl() const
{
  return channelControlRegister;
}

void GIFDMACChannel::writeChannelControl(std::uint32_t value)
{
  if ((value & ~CHANNEL_CONTROL_WRITABLE) != 0)
  {
    throw std::invalid_argument(
      "GIF DMAC CHCR contains unsupported bits.");
  }
  const std::uint32_t mode =
    value & GIFDMACChannelControl::MODE_MASK;
  if (mode != 0 &&
      mode != GIFDMACChannelControl::CHAIN_MODE)
  {
    throw std::invalid_argument(
      "GIF DMAC supports only normal and source-chain modes.");
  }

  const bool active =
    (channelControlRegister &
     GIFDMACChannelControl::START) != 0;
  if (active)
  {
    const std::uint32_t changedFields =
      (channelControlRegister ^ value) &
      ~GIFDMACChannelControl::START;
    if (changedFields != 0)
    {
      throw std::logic_error(
        "GIF DMAC control fields cannot change while active.");
    }
    if ((value & GIFDMACChannelControl::START) == 0)
    {
      channelControlRegister &=
        ~GIFDMACChannelControl::START;
      path3Stalled = false;
    }
    return;
  }

  const std::uint8_t requestedStackDepth =
    static_cast<std::uint8_t>(
      (value &
       GIFDMACChannelControl::ADDRESS_STACK_MASK) >> 4);
  if (requestedStackDepth > addressStackRegisters.size())
  {
    throw std::invalid_argument(
      "GIF DMAC address-stack pointer is invalid.");
  }
  channelControlRegister = value;
  addressStackDepth = requestedStackDepth;
  terminateAfterPacket = false;
  path3Stalled = false;
}

std::uint32_t GIFDMACChannel::memoryAddress() const
{
  return memoryAddressRegister;
}

void GIFDMACChannel::writeMemoryAddress(std::uint32_t value)
{
  requireStopped();
  memoryAddressRegister = decodeAddress(value, "MADR");
}

std::uint32_t GIFDMACChannel::quadwordCount() const
{
  return quadwordCountRegister;
}

void GIFDMACChannel::writeQuadwordCount(std::uint32_t value)
{
  requireStopped();
  quadwordCountRegister = value & QWC_MASK;
}

std::uint32_t GIFDMACChannel::tagAddress() const
{
  return tagAddressRegister;
}

void GIFDMACChannel::writeTagAddress(std::uint32_t value)
{
  requireStopped();
  tagAddressRegister = decodeAddress(value, "TADR");
}

std::uint32_t GIFDMACChannel::addressStack(
  std::size_t index) const
{
  if (index >= addressStackRegisters.size())
  {
    throw std::out_of_range(
      "GIF DMAC address-stack index is out of range.");
  }
  return addressStackRegisters[index];
}

void GIFDMACChannel::writeAddressStack(
  std::size_t index,
  std::uint32_t value)
{
  requireStopped();
  if (index >= addressStackRegisters.size())
  {
    throw std::out_of_range(
      "GIF DMAC address-stack index is out of range.");
  }
  addressStackRegisters[index] =
    decodeAddress(value, "ASR");
}

std::uint32_t GIFDMACChannel::globalControl() const
{
  return globalControlRegister;
}

void GIFDMACChannel::writeGlobalControl(std::uint32_t value)
{
  if ((value & ~GIFDMACControl::DMA_ENABLE) != 0)
  {
    throw std::invalid_argument(
      "Only D_CTRL.DMAE is implemented.");
  }
  globalControlRegister = value;
}

std::uint32_t GIFDMACChannel::globalStatus() const
{
  return statusRegister | statusMaskRegister;
}

void GIFDMACChannel::writeGlobalStatus(std::uint32_t value)
{
  statusRegister &= ~(value & GIFDMACStatus::CHANNEL_2);
  statusMaskRegister ^=
    value & GIFDMACStatus::CHANNEL_2_MASK;
}

bool GIFDMACChannel::interruptPending() const
{
  return
    (statusRegister & GIFDMACStatus::CHANNEL_2) != 0 &&
    (statusMaskRegister &
     GIFDMACStatus::CHANNEL_2_MASK) != 0;
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

void GIFDMACChannel::requireStopped() const
{
  if ((channelControlRegister &
       GIFDMACChannelControl::START) != 0)
  {
    throw std::logic_error(
      "GIF DMAC channel registers cannot change while active.");
  }
}

std::uint32_t GIFDMACChannel::decodeAddress(
  std::uint32_t value,
  const char *registerName) const
{
  if ((value & SPR_BIT) != 0)
  {
    throw std::invalid_argument(
      std::string("GIF DMAC ") + registerName +
      " does not support scratchpad memory.");
  }
  return value & ADDRESS_MASK;
}

void GIFDMACChannel::transferQuadword()
{
  const GIFQuadword quadword =
    eeBus->readQuadword(memoryAddressRegister);
  if (!eeBus->writeQuadword(EEMemoryMap::GIF_FIFO, quadword))
  {
    path3Stalled = true;
    return;
  }

  memoryAddressRegister += 16;
  --quadwordCountRegister;
  ++transferredQuadwords;
  if (quadwordCountRegister == 0 && terminateAfterPacket)
  {
    completeTransfer();
  }
  else if (quadwordCountRegister == 0 &&
           (channelControlRegister &
            GIFDMACChannelControl::MODE_MASK) == 0)
  {
    completeTransfer();
  }
}

void GIFDMACChannel::readSourceChainTag()
{
  const std::uint32_t currentTagAddress = tagAddressRegister;
  const GIFQuadword tag = eeBus->readQuadword(currentTagAddress);
  if ((channelControlRegister &
       GIFDMACChannelControl::TAG_TRANSFER_ENABLE) != 0 &&
      !eeBus->writeQuadword(EEMemoryMap::GIF_FIFO, tag))
  {
    path3Stalled = true;
    return;
  }
  if ((channelControlRegister &
       GIFDMACChannelControl::TAG_TRANSFER_ENABLE) != 0)
  {
    ++transferredQuadwords;
  }
  configureSourceChainTag(
    currentTagAddress,
    tag[0],
    tag[1]);
  if (quadwordCountRegister == 0 && terminateAfterPacket)
  {
    completeTransfer();
  }
}

void GIFDMACChannel::configureSourceChainTag(
  std::uint32_t currentTagAddress,
  std::uint32_t low,
  std::uint32_t high)
{
  if ((high & SPR_BIT) != 0)
  {
    throw std::invalid_argument(
      "GIF DMAC source-chain tags do not support scratchpad memory.");
  }

  const GIFDMATagID id = static_cast<GIFDMATagID>(
    (low >> 28) & 0x07);
  const std::uint32_t count = low & QWC_MASK;
  const std::uint32_t inlineDataAddress =
    currentTagAddress + 16;
  const std::uint32_t afterInlineData =
    inlineDataAddress + count * 16;
  const std::uint32_t tagAddress = high & ADDRESS_MASK;

  channelControlRegister =
    (channelControlRegister &
     ~GIFDMACChannelControl::TAG_MASK) |
    (low & GIFDMACChannelControl::TAG_MASK);
  quadwordCountRegister = count;
  terminateAfterPacket =
    (low & TAG_IRQ) != 0 &&
    (channelControlRegister &
     GIFDMACChannelControl::TAG_INTERRUPT_ENABLE) != 0;

  switch (id)
  {
    case GIFDMATagID::ReferenceEnd:
      memoryAddressRegister = tagAddress;
      tagAddressRegister = currentTagAddress + 16;
      terminateAfterPacket = true;
      break;
    case GIFDMATagID::Count:
      memoryAddressRegister = inlineDataAddress;
      tagAddressRegister = afterInlineData;
      break;
    case GIFDMATagID::Next:
      memoryAddressRegister = inlineDataAddress;
      tagAddressRegister = tagAddress;
      break;
    case GIFDMATagID::Reference:
    case GIFDMATagID::ReferenceStall:
      memoryAddressRegister = tagAddress;
      tagAddressRegister = currentTagAddress + 16;
      break;
    case GIFDMATagID::Call:
      if (addressStackDepth >= addressStackRegisters.size())
      {
        completeTransfer();
        return;
      }
      addressStackRegisters[addressStackDepth] = afterInlineData;
      ++addressStackDepth;
      updateAddressStackField();
      memoryAddressRegister = inlineDataAddress;
      tagAddressRegister = tagAddress;
      break;
    case GIFDMATagID::Return:
      memoryAddressRegister = inlineDataAddress;
      if (addressStackDepth == 0)
      {
        tagAddressRegister = afterInlineData;
        terminateAfterPacket = true;
      }
      else
      {
        --addressStackDepth;
        tagAddressRegister =
          addressStackRegisters[addressStackDepth];
        updateAddressStackField();
      }
      break;
    case GIFDMATagID::End:
      memoryAddressRegister = inlineDataAddress;
      tagAddressRegister = afterInlineData;
      terminateAfterPacket = true;
      break;
  }
}

void GIFDMACChannel::completeTransfer()
{
  channelControlRegister &=
    ~GIFDMACChannelControl::START;
  statusRegister |= GIFDMACStatus::CHANNEL_2;
  terminateAfterPacket = false;
  path3Stalled = false;
}

void GIFDMACChannel::updateAddressStackField()
{
  channelControlRegister =
    (channelControlRegister &
     ~GIFDMACChannelControl::ADDRESS_STACK_MASK) |
    (static_cast<std::uint32_t>(addressStackDepth) << 4);
}
