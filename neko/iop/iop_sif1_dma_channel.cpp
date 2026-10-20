#include "iop_sif1_dma_channel.hpp"

#include <cassert>

#include "sif.hpp"

namespace
{
  constexpr IOPWord TRANSFER_REQUEST = 0x01000000;
}

IOPWord IOPSIF1DMAChannel::memoryAddress() const
{
  return registers.memoryAddressRegister;
}

IOPWord IOPSIF1DMAChannel::blockControl() const
{
  return registers.blockControlRegister;
}

IOPWord IOPSIF1DMAChannel::channelControl() const
{
  return registers.channelControlRegister;
}

IOPWord IOPSIF1DMAChannel::remainingWords() const
{
  return packetWordsRemaining;
}

bool IOPSIF1DMAChannel::terminalPacket() const
{
  return packetTerminal;
}

bool IOPSIF1DMAChannel::active() const
{
  return registers.active();
}

bool IOPSIF1DMAChannel::validMemoryAddress(IOPWord value)
{
  return IOPDMAChannelState::validMemoryAddress(value);
}

bool IOPSIF1DMAChannel::validChannelControl(IOPWord value)
{
  return IOPDMAChannelState::validChannelControl(
    value, IOPDMAChannelProfile::SIF1Receiver);
}

bool IOPSIF1DMAChannel::validContinuation() const
{
  return packetWordsRemaining <= 0x000ffffc &&
    (active() || packetWordsRemaining == 0) &&
    (packetWordsRemaining != 0 || !packetTerminal);
}

IOPSIF1DMAChannel::PreparedBeat IOPSIF1DMAChannel::prepareBeat(
  const IOPBus &bus, const SIF &sif) const
{
  PreparedBeat beat;
  SIFQuadword words = {};
  const bool header = packetWordsRemaining == 0;
  if (sif.peekWords(SIFDirection::SIF1, words, header ? 4 : 1) ==
      SIFTransferOutcome::Wait)
  {
    return beat;
  }
  auto &result = beat.result;
  result.value = words[0];
  result.outcome = IOPDMABeatOutcome::Accepted;
  if (header)
  {
    result.address = words[0] & 0x00ffffff;
    if (!validMemoryAddress(result.address))
    {
      result.outcome = IOPDMABeatOutcome::Rejected;
      result.failure = IOPDMAFailure::UnalignedHeader;
      result.status = IOPBusStatus::Misaligned;
      return beat;
    }
    beat.remainingWords = words[1] & 0x000ffffc;
    beat.terminal = (words[0] & 0xc0000000) != 0;
  }
  else
  {
    result.address = registers.memoryAddressRegister;
    result.status = bus.dmaWriteStatus(result.address);
    if (result.status != IOPBusStatus::Completed)
    {
      result.outcome = IOPDMABeatOutcome::Rejected;
      result.failure = IOPDMAFailure::DestinationWrite;
      return beat;
    }
    beat.remainingWords = packetWordsRemaining - 1;
    beat.terminal = packetTerminal;
  }
  if (beat.remainingWords == 0 && beat.terminal)
  {
    result.outcome = IOPDMABeatOutcome::Completed;
  }
  return beat;
}

IOPDMABeatResult IOPSIF1DMAChannel::transferBeat(IOPBus &bus, SIF &sif)
{
  auto beat = prepareBeat(bus, sif);
  auto &result = beat.result;
  if (result.outcome == IOPDMABeatOutcome::Wait ||
      result.outcome == IOPDMABeatOutcome::Rejected)
  {
    return result;
  }
  const bool header = packetWordsRemaining == 0;
  if (!header)
  {
    const auto write = bus.writeDMAC32(result.address, result.value);
    if (write.status != IOPBusStatus::Completed)
    {
      result.outcome = IOPDMABeatOutcome::Rejected;
      result.failure = IOPDMAFailure::DestinationWrite;
      result.status = write.status;
      return result;
    }
  }
  const auto consumed = sif.consumeWords(SIFDirection::SIF1, header ? 4 : 1);
  assert(consumed == SIFTransferOutcome::Completed);
  (void)consumed;
  registers.memoryAddressRegister = header ?
    result.address : (registers.memoryAddressRegister + 4) & 0x00ffffff;
  packetWordsRemaining = beat.remainingWords;
  packetTerminal = packetWordsRemaining != 0 && beat.terminal;
  if (result.outcome == IOPDMABeatOutcome::Completed)
  {
    registers.channelControlRegister &= ~TRANSFER_REQUEST;
    clearContinuation();
  }
  return result;
}

void IOPSIF1DMAChannel::clearContinuation()
{
  packetWordsRemaining = 0;
  packetTerminal = false;
}

IOPDMAWriteOutcome IOPSIF1DMAChannel::writeMemoryAddress(IOPWord value)
{
  return registers.writeAddress(registers.memoryAddressRegister, value);
}

IOPDMAWriteOutcome IOPSIF1DMAChannel::writeBlockControl(IOPWord value)
{
  return registers.writeBlockControl(value);
}

IOPDMAWriteOutcome IOPSIF1DMAChannel::writeChannelControl(IOPWord value)
{
  const bool wasActive = active();
  const auto result = registers.writeChannelControl(
    value, IOPDMAChannelProfile::SIF1Receiver);
  if (result != IOPDMAWriteOutcome::Completed)
  {
    return result;
  }
  if (!wasActive || !active())
  {
    clearContinuation();
  }
  return IOPDMAWriteOutcome::Completed;
}
