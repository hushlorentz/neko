#include <stdexcept>
#include <string>

#include "dmac_channel_state.hpp"

namespace
{
  constexpr std::uint32_t ADDRESS_MASK = 0x7ffffff0;
  constexpr std::uint32_t SPR_BIT = UINT32_C(0x80000000);
  constexpr std::uint32_t QWC_MASK = 0xffff;
  constexpr std::uint32_t TAG_IRQ = UINT32_C(0x80000000);
  constexpr std::uint32_t CHANNEL_CONTROL_WRITABLE =
    DMACChannelControl::FROM_MEMORY |
    DMACChannelControl::MODE_MASK |
    DMACChannelControl::ADDRESS_STACK_MASK |
    DMACChannelControl::TAG_TRANSFER_ENABLE |
    DMACChannelControl::TAG_INTERRUPT_ENABLE |
    DMACChannelControl::START |
    DMACChannelControl::TAG_MASK;
}

DMACChannelState::DMACChannelState(
  const char *channelName,
  DMACChannelDirectionPolicy directionPolicy) :
  name(channelName),
  direction(directionPolicy)
{
  if (name == nullptr)
  {
    throw std::invalid_argument(
      "DMAC channel state requires a name.");
  }
}

bool DMACChannelState::active() const
{
  return
    (channelControlRegister &
     DMACChannelControl::START) != 0;
}

bool DMACChannelState::normalMode() const
{
  return
    (channelControlRegister &
     DMACChannelControl::MODE_MASK) == 0;
}

bool DMACChannelState::tagTransferEnabled() const
{
  return
    (channelControlRegister &
     DMACChannelControl::TAG_TRANSFER_ENABLE) != 0;
}

bool DMACChannelState::hasPendingQuadword() const
{
  return quadwordCountRegister != 0;
}

std::uint32_t DMACChannelState::channelControl() const
{
  return channelControlRegister;
}

DMACControlWriteEffect
DMACChannelState::writeChannelControl(std::uint32_t value)
{
  if ((value & ~CHANNEL_CONTROL_WRITABLE) != 0)
  {
    throw std::invalid_argument(
      std::string(name) +
      " DMAC CHCR contains unsupported bits.");
  }
  if (direction == DMACChannelDirectionPolicy::FromMemory &&
      (value & DMACChannelControl::FROM_MEMORY) == 0)
  {
    throw std::invalid_argument(
      std::string(name) +
      " DMAC supports only transfers from memory.");
  }
  const std::uint32_t mode =
    value & DMACChannelControl::MODE_MASK;
  if (mode != 0 &&
      mode != DMACChannelControl::CHAIN_MODE)
  {
    throw std::invalid_argument(
      std::string(name) +
      " DMAC supports only normal and source-chain modes.");
  }

  if (active())
  {
    const std::uint32_t changedFields =
      (channelControlRegister ^ value) &
      ~DMACChannelControl::START;
    if (changedFields != 0)
    {
      throw std::logic_error(
        std::string(name) +
        " DMAC control fields cannot change while active.");
    }
    if ((value & DMACChannelControl::START) != 0)
    {
      return DMACControlWriteEffect::None;
    }
    channelControlRegister &=
      ~DMACChannelControl::START;
    terminateAfterPacket = false;
    return DMACControlWriteEffect::ResetContinuation;
  }

  const std::uint8_t requestedStackDepth =
    static_cast<std::uint8_t>(
      (value &
       DMACChannelControl::ADDRESS_STACK_MASK) >> 4);
  if (requestedStackDepth > addressStackRegisters.size())
  {
    throw std::invalid_argument(
      std::string(name) +
      " DMAC address-stack pointer is invalid.");
  }
  channelControlRegister = value;
  addressStackDepth = requestedStackDepth;
  terminateAfterPacket = false;
  return DMACControlWriteEffect::ResetContinuation;
}

std::uint32_t DMACChannelState::memoryAddress() const
{
  return memoryAddressRegister;
}

void DMACChannelState::writeMemoryAddress(std::uint32_t value)
{
  requireStopped();
  memoryAddressRegister = decodeAddress(value, "MADR");
}

std::uint32_t DMACChannelState::quadwordCount() const
{
  return quadwordCountRegister;
}

void DMACChannelState::writeQuadwordCount(std::uint32_t value)
{
  requireStopped();
  quadwordCountRegister = value & QWC_MASK;
}

std::uint32_t DMACChannelState::tagAddress() const
{
  return tagAddressRegister;
}

void DMACChannelState::writeTagAddress(std::uint32_t value)
{
  requireStopped();
  tagAddressRegister = decodeAddress(value, "TADR");
}

std::uint32_t DMACChannelState::addressStack(
  std::size_t index) const
{
  if (index >= addressStackRegisters.size())
  {
    throw std::out_of_range(
      std::string(name) +
      " DMAC address-stack index is out of range.");
  }
  return addressStackRegisters[index];
}

void DMACChannelState::writeAddressStack(
  std::size_t index,
  std::uint32_t value)
{
  requireStopped();
  if (index >= addressStackRegisters.size())
  {
    throw std::out_of_range(
      std::string(name) +
      " DMAC address-stack index is out of range.");
  }
  addressStackRegisters[index] =
    decodeAddress(value, "ASR");
}

DMACChannelTransition DMACChannelState::acceptQuadword()
{
  if (quadwordCountRegister == 0)
  {
    throw std::logic_error(
      std::string(name) +
      " DMAC cannot accept a qword with zero QWC.");
  }
  memoryAddressRegister += 16;
  --quadwordCountRegister;
  if (quadwordCountRegister == 0 &&
      (terminateAfterPacket || normalMode()))
  {
    return DMACChannelTransition::Complete;
  }
  return DMACChannelTransition::Continue;
}

DMACChannelTransition
DMACChannelState::acceptSourceChainTag(
  std::uint32_t low,
  std::uint32_t high)
{
  if ((high & SPR_BIT) != 0)
  {
    throw std::invalid_argument(
      std::string(name) +
      " DMAC source-chain tags do not support scratchpad memory.");
  }

  const std::uint32_t currentTagAddress =
    tagAddressRegister;
  const DMACTagID id = static_cast<DMACTagID>(
    (low >> 28) & 0x07);
  const std::uint32_t count = low & QWC_MASK;
  const std::uint32_t inlineDataAddress =
    currentTagAddress + 16;
  const std::uint32_t afterInlineData =
    inlineDataAddress + count * 16;
  const std::uint32_t tagAddress = high & ADDRESS_MASK;

  channelControlRegister =
    (channelControlRegister &
     ~DMACChannelControl::TAG_MASK) |
    (low & DMACChannelControl::TAG_MASK);
  quadwordCountRegister = count;
  terminateAfterPacket =
    (low & TAG_IRQ) != 0 &&
    (channelControlRegister &
     DMACChannelControl::TAG_INTERRUPT_ENABLE) != 0;

  switch (id)
  {
    case DMACTagID::ReferenceEnd:
      memoryAddressRegister = tagAddress;
      tagAddressRegister = currentTagAddress + 16;
      terminateAfterPacket = true;
      break;
    case DMACTagID::Count:
      memoryAddressRegister = inlineDataAddress;
      tagAddressRegister = afterInlineData;
      break;
    case DMACTagID::Next:
      memoryAddressRegister = inlineDataAddress;
      tagAddressRegister = tagAddress;
      break;
    case DMACTagID::Reference:
    case DMACTagID::ReferenceStall:
      memoryAddressRegister = tagAddress;
      tagAddressRegister = currentTagAddress + 16;
      break;
    case DMACTagID::Call:
      if (addressStackDepth >= addressStackRegisters.size())
      {
        return DMACChannelTransition::Complete;
      }
      addressStackRegisters[addressStackDepth] = afterInlineData;
      ++addressStackDepth;
      updateAddressStackField();
      memoryAddressRegister = inlineDataAddress;
      tagAddressRegister = tagAddress;
      break;
    case DMACTagID::Return:
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
    case DMACTagID::End:
      memoryAddressRegister = inlineDataAddress;
      tagAddressRegister = afterInlineData;
      terminateAfterPacket = true;
      break;
  }

  return
    quadwordCountRegister == 0 && terminateAfterPacket ?
      DMACChannelTransition::Complete :
      DMACChannelTransition::Continue;
}

void DMACChannelState::completeTransfer()
{
  channelControlRegister &=
    ~DMACChannelControl::START;
  terminateAfterPacket = false;
}

void DMACChannelState::requireStopped() const
{
  if (active())
  {
    throw std::logic_error(
      std::string(name) +
      " DMAC channel registers cannot change while active.");
  }
}

std::uint32_t DMACChannelState::decodeAddress(
  std::uint32_t value,
  const char *registerName) const
{
  if ((value & SPR_BIT) != 0)
  {
    throw std::invalid_argument(
      std::string(name) + " DMAC " + registerName +
      " does not support scratchpad memory.");
  }
  return value & ADDRESS_MASK;
}

void DMACChannelState::updateAddressStackField()
{
  channelControlRegister =
    (channelControlRegister &
     ~DMACChannelControl::ADDRESS_STACK_MASK) |
    (static_cast<std::uint32_t>(addressStackDepth) << 4);
}
