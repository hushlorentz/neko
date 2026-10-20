#ifndef IOP_DMA_CHANNEL_STATE_HPP
#define IOP_DMA_CHANNEL_STATE_HPP

#include "iop_types.hpp"

enum class IOPDMAWriteOutcome : std::uint8_t
{
  Completed,
  UnsupportedControl,
  ActiveTransfer,
  UnalignedAddress
};

enum class IOPDMAChannelProfile : std::uint8_t
{
  SIF0Source,
  SIF1Receiver
};

struct IOPDMAChannelState
{
  static constexpr IOPWord TRANSFER_REQUEST = 0x01000000;

  static bool validMemoryAddress(IOPWord value);
  static bool validChannelControl(IOPWord value, IOPDMAChannelProfile profile);
  bool active() const;
  IOPDMAWriteOutcome writeAddress(IOPWord &address, IOPWord value);
  IOPDMAWriteOutcome writeBlockControl(IOPWord value);
  IOPDMAWriteOutcome writeChannelControl(IOPWord value, IOPDMAChannelProfile profile);

  IOPWord memoryAddressRegister = 0;
  IOPWord blockControlRegister = 0;
  IOPWord channelControlRegister = 0;
};

#endif
