#include <stdexcept>
#include <string>

#include "ee_bus.hpp"
#include "gif_dmac_channel.hpp"
#include "vif1_dmac_channel.hpp"

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

VIF1DMACChannel::VIF1DMACChannel(
  EEBus *bus,
  GIFDMACChannel *sharedDMAC) :
  eeBus(bus),
  globalDMAC(sharedDMAC)
{
  if (eeBus == nullptr || globalDMAC == nullptr)
  {
    throw std::invalid_argument(
      "VIF1 DMAC channel requires non-null DMAC components.");
  }
}

bool VIF1DMACChannel::clockActive() const
{
  return
    globalDMAC->dmaEnabled() &&
    (channelControlRegister &
     GIFDMACChannelControl::START) != 0;
}

void VIF1DMACChannel::clock()
{
  if (!clockActive())
  {
    return;
  }
  vif1Stalled = false;
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

std::uint32_t VIF1DMACChannel::channelControl() const
{
  return channelControlRegister;
}

void VIF1DMACChannel::writeChannelControl(std::uint32_t value)
{
  if ((value & ~CHANNEL_CONTROL_WRITABLE) != 0)
  {
    throw std::invalid_argument(
      "VIF1 DMAC CHCR contains unsupported bits.");
  }
  if ((value & GIFDMACChannelControl::FROM_MEMORY) == 0)
  {
    throw std::invalid_argument(
      "VIF1 DMAC supports only transfers from memory.");
  }
  const std::uint32_t mode =
    value & GIFDMACChannelControl::MODE_MASK;
  if (mode != 0 &&
      mode != GIFDMACChannelControl::CHAIN_MODE)
  {
    throw std::invalid_argument(
      "VIF1 DMAC supports only normal and source-chain modes.");
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
        "VIF1 DMAC control fields cannot change while active.");
    }
    if ((value & GIFDMACChannelControl::START) == 0)
    {
      channelControlRegister &=
        ~GIFDMACChannelControl::START;
      vif1Stalled = false;
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
      "VIF1 DMAC address-stack pointer is invalid.");
  }
  channelControlRegister = value;
  addressStackDepth = requestedStackDepth;
  terminateAfterPacket = false;
  vif1Stalled = false;
}

std::uint32_t VIF1DMACChannel::memoryAddress() const
{
  return memoryAddressRegister;
}

void VIF1DMACChannel::writeMemoryAddress(std::uint32_t value)
{
  requireStopped();
  memoryAddressRegister = decodeAddress(value, "MADR");
}

std::uint32_t VIF1DMACChannel::quadwordCount() const
{
  return quadwordCountRegister;
}

void VIF1DMACChannel::writeQuadwordCount(std::uint32_t value)
{
  requireStopped();
  quadwordCountRegister = value & QWC_MASK;
}

std::uint32_t VIF1DMACChannel::tagAddress() const
{
  return tagAddressRegister;
}

void VIF1DMACChannel::writeTagAddress(std::uint32_t value)
{
  requireStopped();
  tagAddressRegister = decodeAddress(value, "TADR");
}

std::uint32_t VIF1DMACChannel::addressStack(
  std::size_t index) const
{
  if (index >= addressStackRegisters.size())
  {
    throw std::out_of_range(
      "VIF1 DMAC address-stack index is out of range.");
  }
  return addressStackRegisters[index];
}

void VIF1DMACChannel::writeAddressStack(
  std::size_t index,
  std::uint32_t value)
{
  requireStopped();
  if (index >= addressStackRegisters.size())
  {
    throw std::out_of_range(
      "VIF1 DMAC address-stack index is out of range.");
  }
  addressStackRegisters[index] =
    decodeAddress(value, "ASR");
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

void VIF1DMACChannel::requireStopped() const
{
  if ((channelControlRegister &
       GIFDMACChannelControl::START) != 0)
  {
    throw std::logic_error(
      "VIF1 DMAC channel registers cannot change while active.");
  }
}

std::uint32_t VIF1DMACChannel::decodeAddress(
  std::uint32_t value,
  const char *registerName) const
{
  if ((value & SPR_BIT) != 0)
  {
    throw std::invalid_argument(
      std::string("VIF1 DMAC ") + registerName +
      " does not support scratchpad memory.");
  }
  return value & ADDRESS_MASK;
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
  if (!submitQuadword(memoryAddressRegister))
  {
    vif1Stalled = true;
    return;
  }

  memoryAddressRegister += 16;
  --quadwordCountRegister;
  ++transferredQuadwords;
  if (quadwordCountRegister == 0 &&
      (terminateAfterPacket ||
       (channelControlRegister &
        GIFDMACChannelControl::MODE_MASK) == 0))
  {
    completeTransfer();
  }
}

void VIF1DMACChannel::readSourceChainTag()
{
  const std::uint32_t currentTagAddress = tagAddressRegister;
  const GIFQuadword tag = eeBus->readQuadword(currentTagAddress);
  if ((channelControlRegister &
       GIFDMACChannelControl::TAG_TRANSFER_ENABLE) != 0 &&
      !submitTag(currentTagAddress))
  {
    vif1Stalled = true;
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

void VIF1DMACChannel::configureSourceChainTag(
  std::uint32_t currentTagAddress,
  std::uint32_t low,
  std::uint32_t high)
{
  if ((high & SPR_BIT) != 0)
  {
    throw std::invalid_argument(
      "VIF1 DMAC source-chain tags do not support scratchpad memory.");
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

void VIF1DMACChannel::completeTransfer()
{
  channelControlRegister &=
    ~GIFDMACChannelControl::START;
  globalDMAC->signalChannelCompletion(
    GIFDMACStatus::CHANNEL_1);
  terminateAfterPacket = false;
  vif1Stalled = false;
}

void VIF1DMACChannel::updateAddressStackField()
{
  channelControlRegister =
    (channelControlRegister &
     ~GIFDMACChannelControl::ADDRESS_STACK_MASK) |
    (static_cast<std::uint32_t>(addressStackDepth) << 4);
}
