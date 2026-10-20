#include "iop_dma_channel_state.hpp"

#include <stdexcept>

bool IOPDMAChannelState::validMemoryAddress(IOPWord value)
{
  return (value & ~IOPWord{0x00fffffc}) == 0;
}

bool IOPDMAChannelState::validChannelControl(
  IOPWord value, IOPDMAChannelProfile profile)
{
  IOPWord required = 0;
  switch (profile)
  {
    case IOPDMAChannelProfile::SIF0Source:
      required = 0x401;
      break;
    case IOPDMAChannelProfile::SIF1Receiver:
      required = 0x400;
      break;
    default:
      throw std::invalid_argument("Unsupported IOP DMA channel profile.");
  }
  const IOPWord modeMask =
    profile == IOPDMAChannelProfile::SIF0Source ? 0x401 : 0x601;
  return (value & ~IOPWord{0x01000701}) == 0 &&
    ((value & TRANSFER_REQUEST) == 0 || (value & modeMask) == required);
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
  if (active() && value != channelControlRegister &&
      value != (channelControlRegister & ~TRANSFER_REQUEST))
  {
    return IOPDMAWriteOutcome::ActiveTransfer;
  }
  channelControlRegister = value;
  return IOPDMAWriteOutcome::Completed;
}
