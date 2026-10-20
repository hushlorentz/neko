#include "iop_sif0_dma_channel.hpp"

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
  return registers.writeChannelControl(value, IOPDMAChannelProfile::SIF0Source);
}

IOPDMAWriteOutcome IOPSIF0DMAChannel::writeTagAddress(IOPWord value)
{
  return registers.writeAddress(tagAddressRegister, value);
}
