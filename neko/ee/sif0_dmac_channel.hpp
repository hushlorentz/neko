#ifndef SIF0_DMAC_CHANNEL_HPP
#define SIF0_DMAC_CHANNEL_HPP

#include "dmac_channel_state.hpp"

class DMACController;
class SIF;
class EEBus;
class EEMemorySystem;
struct EEQuadword;

enum class SIF0DMABeatOutcome : std::uint8_t
{
  Completed,
  Wait,
  BusError
};

class SIF0DMACChannel final
{
  public:
    SIF0DMACChannel(DMACController &controller, SIF &sif,
      EEBus &bus, EEMemorySystem &memory);
    SIF0DMACChannel(const SIF0DMACChannel &) = delete;
    SIF0DMACChannel &operator=(const SIF0DMACChannel &) = delete;

    bool active() const;
    bool transferEnabled() const;
    std::uint32_t channelControl() const;
    void writeChannelControl(std::uint32_t value);
    std::uint32_t memoryAddress() const;
    void writeMemoryAddress(std::uint32_t value);
    std::uint32_t quadwordCount() const;
    void writeQuadwordCount(std::uint32_t value);
    std::uint64_t stateHash() const;

  private:
    friend class NekoSaveStateCodec;
    friend class DMACController;

    SIF0DMABeatOutcome transferBeat();
    SIF0DMABeatOutcome writeDestinationQuadword(
      std::uint32_t address, const EEQuadword &value);
    void completeTransfer();
    void interruptForBusError();

    DMACController &dmacController;
    SIF &sifComponent;
    EEBus &eeBus;
    EEMemorySystem &eeMemorySystem;
    DMACChannelState channelState;
};

#endif
