#ifndef SIF1_DMAC_CHANNEL_HPP
#define SIF1_DMAC_CHANNEL_HPP

#include "dmac_channel_state.hpp"

class DMACController;
class SIF;

class SIF1DMACChannel final
{
  public:
    SIF1DMACChannel(DMACController &controller, SIF &sif);
    SIF1DMACChannel(const SIF1DMACChannel &) = delete;
    SIF1DMACChannel &operator=(const SIF1DMACChannel &) = delete;

    bool active() const;
    bool transferEnabled() const;
    std::uint32_t channelControl() const;
    void writeChannelControl(std::uint32_t value);
    std::uint32_t memoryAddress() const;
    void writeMemoryAddress(std::uint32_t value);
    std::uint32_t quadwordCount() const;
    void writeQuadwordCount(std::uint32_t value);
    std::uint32_t tagAddress() const;
    void writeTagAddress(std::uint32_t value);
    std::uint64_t stateHash() const;

  private:
    friend class NekoSaveStateCodec;

    DMACController &dmacController;
    SIF &sifComponent;
    DMACChannelState channelState;
};

#endif
