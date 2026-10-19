#ifndef IOP_SIF1_DMA_CHANNEL_HPP
#define IOP_SIF1_DMA_CHANNEL_HPP

#include "iop_types.hpp"

enum class IOPDMAWriteOutcome : std::uint8_t
{
  Completed,
  UnsupportedControl,
  ActiveTransfer,
  UnalignedAddress
};

class IOPSIF1DMAChannel final
{
  public:
    IOPWord memoryAddress() const;
    IOPWord blockControl() const;
    IOPWord channelControl() const;
    bool active() const;
    IOPDMAWriteOutcome writeMemoryAddress(IOPWord value);
    IOPDMAWriteOutcome writeBlockControl(IOPWord value);
    IOPDMAWriteOutcome writeChannelControl(IOPWord value);

  private:
    friend class NekoSaveStateCodec;

    static bool validMemoryAddress(IOPWord value);
    static bool validChannelControl(IOPWord value);

    IOPWord memoryAddressRegister = 0;
    IOPWord blockControlRegister = 0;
    IOPWord channelControlRegister = 0;
};

#endif
