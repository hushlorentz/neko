#ifndef VIF1_DMAC_CHANNEL_HPP
#define VIF1_DMAC_CHANNEL_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "clocked_component.hpp"

class EEBus;
class GIFDMACChannel;

class VIF1DMACChannel : public ClockedComponent
{
  public:
    VIF1DMACChannel(
      EEBus *bus,
      GIFDMACChannel *globalDMAC);

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

    void requireStopped() const;
    std::uint32_t decodeAddress(
      std::uint32_t value,
      const char *registerName) const;
    bool submitValue(const GIFQuadword &quadword);
    bool submitQuadword(std::uint32_t address);
    bool submitTag(std::uint32_t address);
    void transferQuadword();
    void readSourceChainTag();
    void configureSourceChainTag(
      std::uint32_t tagAddress,
      std::uint32_t low,
      std::uint32_t high);
    void completeTransfer();
    void updateAddressStackField();

    EEBus *eeBus;
    GIFDMACChannel *globalDMAC;
    std::uint32_t channelControlRegister = 0;
    std::uint32_t memoryAddressRegister = 0;
    std::uint32_t quadwordCountRegister = 0;
    std::uint32_t tagAddressRegister = 0;
    std::array<std::uint32_t, 2> addressStackRegisters = {};
    bool terminateAfterPacket = false;
    bool vif1Stalled = false;
    std::uint8_t addressStackDepth = 0;
    std::uint64_t transferredQuadwords = 0;
};

#endif
