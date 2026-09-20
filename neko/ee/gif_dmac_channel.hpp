#ifndef GIF_DMAC_CHANNEL_HPP
#define GIF_DMAC_CHANNEL_HPP

#include <cstddef>
#include <cstdint>

#include "clocked_component.hpp"
#include "dmac_channel_state.hpp"

class EEBus;
class DMACController;

using GIFDMATagID = DMACTagID;
namespace GIFDMACChannelControl = DMACChannelControl;

class GIFDMACChannel : public ClockedComponent
{
  public:
    GIFDMACChannel(
      EEBus *bus,
      DMACController *controller);

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

    bool stalledByPATH3() const;
    std::uint64_t transferredQuadwordCount() const;

  private:
    friend class NekoSaveStateCodec;

    void transferQuadword();
    void readSourceChainTag();
    void completeTransfer();

    EEBus *eeBus;
    DMACController *dmacController;
    DMACChannelState channelState;
    bool path3Stalled = false;
    std::uint64_t transferredQuadwords = 0;
};

#endif
