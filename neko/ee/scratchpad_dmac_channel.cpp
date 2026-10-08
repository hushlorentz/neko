#include <cassert>
#include <stdexcept>
#include <string>

#include "dmac_channel_state.hpp"
#include "dmac_controller.hpp"
#include "ee_bus.hpp"
#include "ee_memory_system.hpp"
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
  DMACController *controller,
  EEBus *bus,
  EEMemorySystem *memorySystem) :
  channelKind(kind),
  dmacController(controller),
  eeBus(bus),
  eeMemorySystem(memorySystem)
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
  if (eeBus == nullptr || eeMemorySystem == nullptr)
  {
    throw std::invalid_argument(
      "Scratchpad DMAC channel requires memory components.");
  }
}

bool ScratchpadDMACChannel::clockActive() const
{
  return active() && dmacController->enabled();
}

void ScratchpadDMACChannel::clock()
{
  if (!clockActive())
  {
    return;
  }
  if (quadwordCountRegister == 0)
  {
    completeTransfer();
    return;
  }
  const bool interleave =
    (channelControlRegister &
     DMACChannelControl::MODE_MASK) ==
    DMACChannelControl::INTERLEAVE_MODE &&
    dmacController->interleaveTransferQWC() != 0 &&
    dmacController->interleaveSkipQWC() != 0;
  transferQuadword(interleave);
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
  const std::uint32_t mode =
    value & DMACChannelControl::MODE_MASK;
  if (mode != 0 &&
      mode != DMACChannelControl::INTERLEAVE_MODE)
  {
    throw std::invalid_argument(
      std::string(name()) +
      " DMAC supports only normal and interleave modes.");
  }
  const bool wasActive = active();
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
  if (!wasActive && active() &&
      mode == DMACChannelControl::INTERLEAVE_MODE &&
      dmacController->interleaveTransferQWC() != 0 &&
      dmacController->interleaveSkipQWC() != 0)
  {
    interleaveQuadwordsRemaining =
      dmacController->interleaveTransferQWC();
  }
  else if (!active() || mode == 0)
  {
    interleaveQuadwordsRemaining = 0;
  }
  assert(stateValid());
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
  assert(stateValid());
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
  assert(stateValid());
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
  assert(stateValid());
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
  assert(stateValid());
}

void ScratchpadDMACChannel::transferQuadword(
  bool interleave)
{
  EEQuadword value = {};
  const EEScratchpadDMACMode mode =
    (channelControlRegister &
     DMACChannelControl::MODE_MASK) == 0 ?
      EEScratchpadDMACMode::Normal :
      EEScratchpadDMACMode::Interleave;
  if (channelKind ==
      ScratchpadDMACChannelKind::FromScratchpad)
  {
    const EEScratchpadAccessResult scratchpadResult =
      eeMemorySystem->readScratchpadDMA128(
        scratchpadAddressRegister,
        &value);
    if (scratchpadResult ==
        EEScratchpadAccessResult::Wait)
    {
      return;
    }
    if (scratchpadResult !=
        EEScratchpadAccessResult::Completed)
    {
      throw std::out_of_range(
        "fromSPR DMAC scratchpad address is invalid.");
    }
    if (!eeBus->writeScratchpadDMAC128(
          memoryAddressRegister,
          mode,
          value))
    {
      interruptForBusError();
      return;
    }
  }
  else
  {
    if (!eeBus->readScratchpadDMAC128(
          memoryAddressRegister,
          mode,
          &value))
    {
      interruptForBusError();
      return;
    }
    const EEScratchpadAccessResult scratchpadResult =
      eeMemorySystem->writeScratchpadDMA128(
        scratchpadAddressRegister,
        value);
    if (scratchpadResult ==
        EEScratchpadAccessResult::Wait)
    {
      return;
    }
    if (scratchpadResult !=
        EEScratchpadAccessResult::Completed)
    {
      throw std::out_of_range(
        "toSPR DMAC scratchpad address is invalid.");
    }
  }

  memoryAddressRegister =
    (memoryAddressRegister + 16) &
    MEMORY_ADDRESS_MASK;
  scratchpadAddressRegister =
    (scratchpadAddressRegister + 16) &
    SCRATCHPAD_ADDRESS_MASK;
  --quadwordCountRegister;
  if (interleave)
  {
    if (interleaveQuadwordsRemaining == 0)
    {
      interleaveQuadwordsRemaining =
        dmacController->interleaveTransferQWC();
    }
    --interleaveQuadwordsRemaining;
    if (interleaveQuadwordsRemaining == 0)
    {
      memoryAddressRegister =
        (memoryAddressRegister +
         static_cast<std::uint32_t>(
           dmacController->interleaveSkipQWC()) * 16) &
        MEMORY_ADDRESS_MASK;
      if (quadwordCountRegister != 0)
      {
        interleaveQuadwordsRemaining =
          dmacController->interleaveTransferQWC();
      }
    }
  }
  else
  {
    interleaveQuadwordsRemaining = 0;
  }
  if (quadwordCountRegister == 0)
  {
    completeTransfer();
  }
  assert(stateValid());
}

void ScratchpadDMACChannel::completeTransfer()
{
  channelControlRegister &= ~DMACChannelControl::START;
  interleaveQuadwordsRemaining = 0;
  dmacController->signalChannelCompletion(
    channelKind ==
      ScratchpadDMACChannelKind::FromScratchpad ?
      DMACStatus::CHANNEL_8 :
      DMACStatus::CHANNEL_9);
  assert(stateValid());
}

void ScratchpadDMACChannel::interruptForBusError()
{
  channelControlRegister &= ~DMACChannelControl::START;
  interleaveQuadwordsRemaining = 0;
  dmacController->signalBusError(
    channelKind ==
      ScratchpadDMACChannelKind::FromScratchpad ?
      DMACStatus::CHANNEL_8 :
      DMACStatus::CHANNEL_9);
  assert(stateValid());
}

bool ScratchpadDMACChannel::channelControlStateValid() const
{
  const std::uint32_t mode =
    channelControlRegister & DMACChannelControl::MODE_MASK;
  return
    (channelControlRegister & ~CHANNEL_CONTROL_WRITABLE) == 0 &&
    (mode == 0 ||
     mode == DMACChannelControl::INTERLEAVE_MODE);
}

bool ScratchpadDMACChannel::memoryAddressStateValid() const
{
  return (memoryAddressRegister & ~MEMORY_ADDRESS_MASK) == 0;
}

bool ScratchpadDMACChannel::quadwordCountStateValid() const
{
  return quadwordCountRegister <= QWC_MASK;
}

bool ScratchpadDMACChannel::tagAddressStateValid() const
{
  return
    (tagAddressRegister & ~TAG_ADDRESS_MASK) == 0 &&
    (channelKind == ScratchpadDMACChannelKind::ToScratchpad ||
     tagAddressRegister == 0);
}

bool ScratchpadDMACChannel::scratchpadAddressStateValid() const
{
  return
    (scratchpadAddressRegister & ~SCRATCHPAD_ADDRESS_MASK) == 0;
}

bool ScratchpadDMACChannel::interleaveContinuationStateValid() const
{
  return
    interleaveQuadwordsRemaining <= 0xff &&
    (interleaveQuadwordsRemaining == 0 ||
     (active() &&
      (channelControlRegister &
       DMACChannelControl::MODE_MASK) ==
        DMACChannelControl::INTERLEAVE_MODE));
}

bool ScratchpadDMACChannel::stateValid() const
{
  return
    channelControlStateValid() &&
    memoryAddressStateValid() &&
    quadwordCountStateValid() &&
    tagAddressStateValid() &&
    scratchpadAddressStateValid() &&
    interleaveContinuationStateValid();
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
