#include "iop_sif1_dma_channel.hpp"

namespace
{
  constexpr IOPWord TRANSFER_REQUEST = 0x01000000;
}

IOPWord IOPSIF1DMAChannel::memoryAddress() const
{
  return memoryAddressRegister;
}

IOPWord IOPSIF1DMAChannel::blockControl() const
{
  return blockControlRegister;
}

IOPWord IOPSIF1DMAChannel::channelControl() const
{
  return channelControlRegister;
}

bool IOPSIF1DMAChannel::active() const
{
  return (channelControlRegister & TRANSFER_REQUEST) != 0;
}

bool IOPSIF1DMAChannel::validMemoryAddress(IOPWord value)
{
  return (value & ~IOPWord{0x00fffffc}) == 0;
}

bool IOPSIF1DMAChannel::validChannelControl(IOPWord value)
{
  return (value & ~IOPWord{0x01000701}) == 0 &&
    ((value & TRANSFER_REQUEST) == 0 || (value & 0x601) == 0x400);
}

IOPDMAWriteOutcome IOPSIF1DMAChannel::writeMemoryAddress(IOPWord value)
{
  if (active())
  {
    return IOPDMAWriteOutcome::ActiveTransfer;
  }
  const auto address = value & 0x00ffffff;
  if (!validMemoryAddress(address))
  {
    return IOPDMAWriteOutcome::UnalignedAddress;
  }
  memoryAddressRegister = address;
  return IOPDMAWriteOutcome::Completed;
}

IOPDMAWriteOutcome IOPSIF1DMAChannel::writeBlockControl(IOPWord value)
{
  if (active())
  {
    return IOPDMAWriteOutcome::ActiveTransfer;
  }
  blockControlRegister = value;
  return IOPDMAWriteOutcome::Completed;
}

IOPDMAWriteOutcome IOPSIF1DMAChannel::writeChannelControl(IOPWord value)
{
  if (!validChannelControl(value))
  {
    return IOPDMAWriteOutcome::UnsupportedControl;
  }
  if (active() && value != channelControlRegister &&
      value != (channelControlRegister & ~TRANSFER_REQUEST))
  {
    return IOPDMAWriteOutcome::ActiveTransfer;
  }
  channelControlRegister = value;
  return IOPDMAWriteOutcome::Completed;
}
