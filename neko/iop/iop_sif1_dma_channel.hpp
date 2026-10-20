#ifndef IOP_SIF1_DMA_CHANNEL_HPP
#define IOP_SIF1_DMA_CHANNEL_HPP

#include "iop_dma_controller.hpp"

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
    IOPWord remainingWords() const;
    bool terminalPacket() const;
    bool active() const;
    IOPDMAWriteOutcome writeMemoryAddress(IOPWord value);
    IOPDMAWriteOutcome writeBlockControl(IOPWord value);
    IOPDMAWriteOutcome writeChannelControl(IOPWord value);

  private:
    friend class NekoSaveStateCodec;
    friend class IOPDMAController;

    struct PreparedBeat
    {
      IOPDMABeatResult result;
      IOPWord remainingWords = 0;
      bool terminal = false;
    };

    static bool validMemoryAddress(IOPWord value);
    static bool validChannelControl(IOPWord value);
    bool validContinuation() const;
    PreparedBeat prepareBeat(const IOPBus &bus, const SIF &sif) const;
    IOPDMABeatResult transferBeat(IOPBus &bus, SIF &sif);
    void clearContinuation();

    IOPWord memoryAddressRegister = 0;
    IOPWord blockControlRegister = 0;
    IOPWord channelControlRegister = 0;
    IOPWord packetWordsRemaining = 0;
    bool packetTerminal = false;
};

#endif
