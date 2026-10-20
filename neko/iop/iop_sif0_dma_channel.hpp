#ifndef IOP_SIF0_DMA_CHANNEL_HPP
#define IOP_SIF0_DMA_CHANNEL_HPP

#include "iop_dma_channel_state.hpp"
#include "iop_dma_controller.hpp"
#include "sif.hpp"

class IOPSIF0DMAChannel final
{
  public:
    IOPWord memoryAddress() const;
    IOPWord blockControl() const;
    IOPWord channelControl() const;
    IOPWord tagAddress() const;
    IOPWord remainingWords() const;
    bool terminalPacket() const;
    bool active() const;
    IOPDMAWriteOutcome writeMemoryAddress(IOPWord value);
    IOPDMAWriteOutcome writeBlockControl(IOPWord value);
    IOPDMAWriteOutcome writeChannelControl(IOPWord value);
    IOPDMAWriteOutcome writeTagAddress(IOPWord value);

  private:
    friend class NekoSaveStateCodec;
    friend class IOPDMAController;

    struct PreparedBeat
    {
      IOPDMABeatResult result;
      SIFQuadword prologue = {};
      IOPWord memoryAddress = 0;
      IOPWord remainingWords = 0;
      bool terminal = false;
    };

    bool validContinuation() const;
    PreparedBeat prepareBeat(const IOPBus &bus, const SIF &sif) const;
    IOPDMABeatResult transferBeat(const IOPBus &bus, SIF &sif);
    void clearContinuation();

    IOPDMAChannelState registers;
    IOPWord tagAddressRegister = 0;
    IOPWord packetWordsRemaining = 0;
    bool packetTerminal = false;
};

#endif
