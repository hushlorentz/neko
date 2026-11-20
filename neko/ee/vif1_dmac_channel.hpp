#ifndef VIF1_DMAC_CHANNEL_HPP
#define VIF1_DMAC_CHANNEL_HPP

#include <cstddef>
#include <cstdint>

#include "clocked_component.hpp"
#include "dmac_channel_state.hpp"

class EEBus;
class DMACController;
class EEMemorySystem;

class VIF1DMACChannel : public ClockedComponent
{
  public:
    VIF1DMACChannel(
      EEBus *bus,
      DMACController *controller,
      EEMemorySystem *memorySystem);

    bool clockActive() const override;
    void clock() override;

    std::uint32_t channelControl() const;
    void writeChannelControl(std::uint32_t value);
    std::uint32_t memoryAddress() const;
    void writeMemoryAddress(std::uint32_t value);
    std::uint32_t quadwordCount() const;
    void writeQuadwordCount(std::uint32_t value);
    std::uint32_t tagAddress() const;
    void writeTagAddress(std::uint32_t value);
    std::uint32_t addressStack(std::size_t index) const;
    void writeAddressStack(
      std::size_t index,
      std::uint32_t value);

    bool stalledByVIF1() const;
    std::uint64_t transferredQuadwordCount() const;

  private:
    friend class NekoSaveStateCodec;

    bool submitValue(const GIFQuadword &quadword);
    bool readSourceQuadword(std::uint32_t address, GIFQuadword *value) const;
    void transferQuadword();
    void readSourceChainTag();
    void completeTransfer();

    EEBus *eeBus;
    DMACController *dmacController;
    EEMemorySystem *eeMemorySystem;
    DMACChannelState channelState;
    bool vif1Stalled = false;
    std::uint64_t transferredQuadwords = 0;
};

#endif
