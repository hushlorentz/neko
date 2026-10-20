#include "iop_dma_controller.hpp"

#include <stdexcept>
#include <string>

#include "iop_sif1_dma_channel.hpp"
#include "iop_sif0_dma_channel.hpp"

namespace
{
  const char *failureDescription(IOPDMAFailure failure)
  {
    switch (failure)
    {
      case IOPDMAFailure::None: return "unspecified transfer";
      case IOPDMAFailure::UnalignedHeader: return "unaligned header";
      case IOPDMAFailure::DestinationWrite: return "physical destination write";
      case IOPDMAFailure::DescriptorRead: return "physical descriptor read";
      case IOPDMAFailure::SourceRead: return "physical source read";
    }
    throw std::logic_error("Unknown IOP DMA failure.");
  }
}

IOPDMATransferError::IOPDMATransferError(const IOPDMABeatResult &result) :
  std::runtime_error(std::string("IOP ") +
    (result.channel == IOPDMAChannel::SIF0 ? "SIF0" : "SIF1") + " DMA rejected " +
    failureDescription(result.failure) +
    " at address " + std::to_string(result.address) +
    " with value " + std::to_string(result.value) +
    " (bus status " + std::to_string(static_cast<unsigned>(result.status)) + ")."),
  rejectedBeat(result)
{
}

const IOPDMABeatResult &IOPDMATransferError::result() const
{
  return rejectedBeat;
}

IOPDMAController::IOPDMAController(IOPInterruptController &controller) :
  interrupts(controller)
{
}

std::size_t IOPDMAController::bankIndex(IOPDMABank bank)
{
  switch (bank)
  {
    case IOPDMABank::Primary:
      return 0;
    case IOPDMABank::Secondary:
      return 1;
  }
  throw std::out_of_range("Unsupported IOP DMA bank.");
}

std::uint8_t IOPDMAController::channelSlot(IOPDMAChannel channel)
{
  switch (channel)
  {
    case IOPDMAChannel::SIF0:
      return 2;
    case IOPDMAChannel::SIF1:
      return 3;
  }
  throw std::out_of_range("Unsupported IOP DMA channel.");
}

IOPWord IOPDMAController::priority(IOPDMABank bank) const
{
  return banks[bankIndex(bank)].priority;
}

void IOPDMAController::writePriority(IOPDMABank bank, IOPWord value)
{
  banks[bankIndex(bank)].priority = value;
}

std::uint8_t IOPDMAController::activeFlags(std::size_t index) const
{
  const auto &bank = banks[index];
  const auto mask = static_cast<std::uint8_t>((bank.control >> 16) & 0x7f);
  // Selected SIF slots bypass their stored masks in the provisional profile.
  const auto effectiveMask = index == 1 ? mask | 0x0c : mask;
  return static_cast<std::uint8_t>(bank.flags & effectiveMask);
}

IOPWord IOPDMAController::interruptStatus(IOPDMABank selected) const
{
  const auto index = bankIndex(selected);
  const auto &bank = banks[index];
  const bool summary = (bank.control & 0x8000) != 0 ||
    ((bank.control & 0x800000) != 0 && activeFlags(index) != 0);
  return bank.control | (static_cast<IOPWord>(bank.flags) << 24) |
    (summary ? UINT32_C(0x80000000) : 0);
}

void IOPDMAController::writeInterrupt(IOPDMABank selected, IOPWord value)
{
  auto &bank = banks[bankIndex(selected)];
  bank.control = value & 0x00ffffff;
  bank.flags &= static_cast<std::uint8_t>(~((value >> 24) & 0x7f));
  publishInterrupt();
}

bool IOPDMAController::channelEnabled(IOPDMAChannel channel) const
{
  return (banks[1].priority & (IOPWord{8} << (4 * channelSlot(channel)))) != 0;
}

std::uint8_t IOPDMAController::channelPriority(IOPDMAChannel channel) const
{
  return static_cast<std::uint8_t>(
    (banks[1].priority >> (4 * channelSlot(channel))) & 7);
}

void IOPDMAController::validateSIFWiring(
  const IOPSIF1DMAChannel &channel, const IOPBus &bus, const SIF &sif) const
{
  if (bus.dmaController != this || bus.sif1DMA != &channel ||
      bus.sifComponent != &sif)
  {
    throw std::invalid_argument("IOP SIF DMA grant requires the attached owners.");
  }
}

IOPDMABeatResult IOPDMAController::inspectSIF(
  const IOPSIF1DMAChannel &channel, const IOPBus &bus, const SIF &sif) const
{
  validateSIFWiring(channel, bus, sif);
  if (!channelEnabled(IOPDMAChannel::SIF1) || !channel.active())
  {
    return {};
  }
  return channel.prepareBeat(bus, sif).result;
}

IOPDMABeatResult IOPDMAController::clockSIF(
  IOPSIF1DMAChannel &channel, IOPBus &bus, SIF &sif)
{
  validateSIFWiring(channel, bus, sif);
  if (!channelEnabled(IOPDMAChannel::SIF1) || !channel.active())
  {
    return {};
  }
  const auto result = channel.transferBeat(bus, sif);
  if (result.outcome == IOPDMABeatOutcome::Completed)
  {
    signalChannelCompletion(IOPDMAChannel::SIF1);
  }
  return result;
}

void IOPDMAController::signalChannelCompletion(IOPDMAChannel channel)
{
  banks[1].flags |= static_cast<std::uint8_t>(1 << channelSlot(channel));
  publishInterrupt();
}

void IOPDMAController::validateSIFWiring(
  const IOPSIF0DMAChannel &channel, const IOPBus &bus, const SIF &sif) const
{
  if (bus.dmaController != this || bus.sif0DMA != &channel ||
      bus.sifComponent != &sif)
  {
    throw std::invalid_argument("IOP SIF DMA grant requires the attached owners.");
  }
}

IOPDMABeatResult IOPDMAController::inspectSIF(
  const IOPSIF0DMAChannel &channel, const IOPBus &bus, const SIF &sif) const
{
  validateSIFWiring(channel, bus, sif);
  if (!channelEnabled(IOPDMAChannel::SIF0) || !channel.active())
  {
    IOPDMABeatResult result;
    result.channel = IOPDMAChannel::SIF0;
    return result;
  }
  return channel.prepareBeat(bus, sif).result;
}

IOPDMABeatResult IOPDMAController::clockSIF(
  IOPSIF0DMAChannel &channel, IOPBus &bus, SIF &sif)
{
  validateSIFWiring(channel, bus, sif);
  if (!channelEnabled(IOPDMAChannel::SIF0) || !channel.active())
  {
    IOPDMABeatResult result;
    result.channel = IOPDMAChannel::SIF0;
    return result;
  }
  const auto result = channel.transferBeat(bus, sif);
  if (result.outcome == IOPDMABeatOutcome::Completed)
  {
    signalChannelCompletion(IOPDMAChannel::SIF0);
  }
  return result;
}

bool IOPDMAController::interruptPending() const
{
  for (std::size_t index = 0; index < banks.size(); ++index)
  {
    if ((banks[index].control & 0x8000) != 0 || activeFlags(index) != 0)
    {
      return true;
    }
  }
  return false;
}

void IOPDMAController::publishInterrupt()
{
  if (interruptPending())
  {
    interrupts.assertSource(IOPInterruptSource::DMAC);
  }
}
