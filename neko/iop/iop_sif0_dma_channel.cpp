#include "iop_sif0_dma_channel.hpp"

#include <cassert>

IOPWord IOPSIF0DMAChannel::memoryAddress() const
{
  return registers.memoryAddressRegister;
}

IOPWord IOPSIF0DMAChannel::blockControl() const
{
  return registers.blockControlRegister;
}

IOPWord IOPSIF0DMAChannel::channelControl() const
{
  return registers.channelControlRegister;
}

IOPWord IOPSIF0DMAChannel::tagAddress() const
{
  return tagAddressRegister;
}

IOPWord IOPSIF0DMAChannel::remainingWords() const
{
  return packetWordsRemaining;
}

bool IOPSIF0DMAChannel::terminalPacket() const
{
  return packetTerminal;
}

bool IOPSIF0DMAChannel::active() const
{
  return registers.active();
}

IOPDMAWriteOutcome IOPSIF0DMAChannel::writeMemoryAddress(IOPWord value)
{
  return registers.writeAddress(registers.memoryAddressRegister, value);
}

IOPDMAWriteOutcome IOPSIF0DMAChannel::writeBlockControl(IOPWord value)
{
  return registers.writeBlockControl(value);
}

IOPDMAWriteOutcome IOPSIF0DMAChannel::writeChannelControl(IOPWord value)
{
  const bool wasActive = active();
  const auto result = registers.writeChannelControl(
    value, IOPDMAChannelProfile::SIF0Source);
  if (result == IOPDMAWriteOutcome::Completed && (!wasActive || !active()))
  {
    clearContinuation();
  }
  return result;
}

IOPDMAWriteOutcome IOPSIF0DMAChannel::writeTagAddress(IOPWord value)
{
  return registers.writeAddress(tagAddressRegister, value);
}

bool IOPSIF0DMAChannel::validContinuation() const
{
  return packetWordsRemaining <= 0x000fffff &&
    (active() || packetWordsRemaining == 0) &&
    (packetWordsRemaining != 0 || !packetTerminal);
}

IOPSIF0DMAChannel::PreparedBeat IOPSIF0DMAChannel::prepareBeat(
  const IOPBus &bus, const SIF &sif) const
{
  PreparedBeat beat;
  auto &result = beat.result;
  result.channel = IOPDMAChannel::SIF0;
  const bool descriptor = packetWordsRemaining == 0;
  if (descriptor)
  {
    std::array<IOPWord, 6> words = {};
    for (std::size_t index = 0; index < words.size(); ++index)
    {
      result.address = tagAddressRegister + static_cast<IOPWord>(index * 4);
      const auto read = bus.readDMAC32(result.address);
      if (read.status != IOPBusStatus::Completed)
      {
        result.outcome = IOPDMABeatOutcome::Rejected;
        result.failure = IOPDMAFailure::DescriptorRead;
        result.status = read.status;
        return beat;
      }
      words[index] = read.value;
    }
    beat.memoryAddress = words[0] & 0x00ffffff;
    result.value = words[0];
    if (!IOPDMAChannelState::validMemoryAddress(beat.memoryAddress))
    {
      result.outcome = IOPDMABeatOutcome::Rejected;
      result.failure = IOPDMAFailure::UnalignedHeader;
      result.status = IOPBusStatus::Misaligned;
      result.address = beat.memoryAddress;
      return beat;
    }
    result.address = tagAddressRegister;
    beat.prologue = {{words[2], words[3], words[4], words[5]}};
    beat.remainingWords = words[1] & 0x000fffff;
    beat.terminal = (words[0] & 0xc0000000) != 0;
  }
  else
  {
    result.address = registers.memoryAddressRegister;
    const auto read = bus.readDMAC32(result.address);
    if (read.status != IOPBusStatus::Completed)
    {
      result.outcome = IOPDMABeatOutcome::Rejected;
      result.failure = IOPDMAFailure::SourceRead;
      result.status = read.status;
      return beat;
    }
    result.value = read.value;
    beat.memoryAddress = (registers.memoryAddressRegister + 4) & 0x00ffffff;
    beat.remainingWords = packetWordsRemaining - 1;
    beat.terminal = packetTerminal;
  }
  const auto required = descriptor ? 4 :
    (beat.remainingWords == 0 ? 4 - sif.sif0PackingCursor() : 1);
  if (sif.freeWordCount() < required)
  {
    return beat;
  }
  result.outcome = beat.remainingWords == 0 && beat.terminal ?
    IOPDMABeatOutcome::Completed : IOPDMABeatOutcome::Accepted;
  return beat;
}

IOPDMABeatResult IOPSIF0DMAChannel::transferBeat(const IOPBus &bus, SIF &sif)
{
  const auto beat = prepareBeat(bus, sif);
  const auto &result = beat.result;
  if (result.outcome == IOPDMABeatOutcome::Wait ||
      result.outcome == IOPDMABeatOutcome::Rejected)
  {
    return result;
  }
  const bool descriptor = packetWordsRemaining == 0;
  const auto pushed = descriptor ? sif.pushSIF0Prologue(beat.prologue) :
    sif.pushSIF0PayloadWord(result.value, beat.remainingWords == 0 ?
      SIFPayloadBoundary::Final : SIFPayloadBoundary::Continuing);
  assert(pushed == SIFTransferOutcome::Completed);
  (void)pushed;
  registers.memoryAddressRegister = beat.memoryAddress;
  if (descriptor)
  {
    tagAddressRegister = (tagAddressRegister + 16) & 0x00ffffff;
  }
  packetWordsRemaining = beat.remainingWords;
  packetTerminal = packetWordsRemaining != 0 && beat.terminal;
  if (result.outcome == IOPDMABeatOutcome::Completed)
  {
    registers.channelControlRegister &= ~IOPDMAChannelState::TRANSFER_REQUEST;
    clearContinuation();
  }
  return result;
}

void IOPSIF0DMAChannel::clearContinuation()
{
  packetWordsRemaining = 0;
  packetTerminal = false;
}
