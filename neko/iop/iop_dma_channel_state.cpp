#include "iop_dma_channel_state.hpp"

#include <stdexcept>

bool IOPDMAChannelState::validMemoryAddress(IOPWord value)
{
  return (value & ~IOPWord{0x00fffffc}) == 0;
}

bool IOPDMAChannelState::validChannelControl(
  IOPWord value, IOPDMAChannelProfile profile)
{
  IOPWord allowed = 0x01000701;
  bool supportedStart = false;
  switch (profile)
  {
    case IOPDMAChannelProfile::SIF0Source:
      supportedStart = (value & 0x401) == 0x401;
      break;
    case IOPDMAChannelProfile::SIF1Receiver:
      allowed |= 0x40000000;
      supportedStart = (value & 0x601) == 0x200 || (value & 0x601) == 0x400;
      break;
    default:
      throw std::invalid_argument("Unsupported IOP DMA channel profile.");
  }
  return (value & ~allowed) == 0 &&
    ((value & TRANSFER_REQUEST) == 0 || supportedStart);
}

bool IOPDMAChannelState::active() const
{
  return (channelControlRegister & TRANSFER_REQUEST) != 0;
}

IOPDMAWriteOutcome IOPDMAChannelState::writeAddress(IOPWord &address, IOPWord value)
{
  if (active())
  {
    return IOPDMAWriteOutcome::ActiveTransfer;
  }
  return programMemoryAddress(address, value);
}

IOPDMAWriteOutcome IOPDMAChannelState::programMemoryAddress(
  IOPWord &address, IOPWord value)
{
  const auto masked = value & 0x00ffffff;
  if (!validMemoryAddress(masked))
  {
    return IOPDMAWriteOutcome::UnalignedAddress;
  }
  address = masked;
  return IOPDMAWriteOutcome::Completed;
}

IOPDMAWriteOutcome IOPDMAChannelState::writeBlockControl(IOPWord value)
{
  if (active())
  {
    return IOPDMAWriteOutcome::ActiveTransfer;
  }
  blockControlRegister = value;
  return IOPDMAWriteOutcome::Completed;
}

IOPDMAWriteOutcome IOPDMAChannelState::writeChannelControl(
  IOPWord value, IOPDMAChannelProfile profile)
{
  if (!validChannelControl(value, profile))
  {
    return IOPDMAWriteOutcome::UnsupportedControl;
  }
  if (active() && (value & TRANSFER_REQUEST) != 0 &&
      value != channelControlRegister)
  {
    return IOPDMAWriteOutcome::ActiveTransfer;
  }
  channelControlRegister = value;
  return IOPDMAWriteOutcome::Completed;
}
