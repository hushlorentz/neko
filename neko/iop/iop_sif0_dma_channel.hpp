#ifndef IOP_SIF0_DMA_CHANNEL_HPP
#define IOP_SIF0_DMA_CHANNEL_HPP

#include "iop_dma_channel_state.hpp"

class IOPSIF0DMAChannel final
{
  public:
    IOPWord memoryAddress() const;
    IOPWord blockControl() const;
    IOPWord channelControl() const;
    IOPWord tagAddress() const;
    bool active() const;
    IOPDMAWriteOutcome writeMemoryAddress(IOPWord value);
    IOPDMAWriteOutcome writeBlockControl(IOPWord value);
    IOPDMAWriteOutcome writeChannelControl(IOPWord value);
    IOPDMAWriteOutcome writeTagAddress(IOPWord value);

  private:
    friend class NekoSaveStateCodec;

    IOPDMAChannelState registers;
    IOPWord tagAddressRegister = 0;
};

#endif
