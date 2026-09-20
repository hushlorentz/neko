#ifndef GIF_DMAC_CHANNEL_HPP
#define GIF_DMAC_CHANNEL_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "clocked_component.hpp"

class EEBus;
class DMACController;

enum class GIFDMATagID : std::uint8_t
{
  ReferenceEnd = 0,
  Count = 1,
  Next = 2,
  Reference = 3,
  ReferenceStall = 4,
  Call = 5,
  Return = 6,
  End = 7
};

namespace GIFDMACChannelControl
{
  constexpr std::uint32_t FROM_MEMORY = 1u;
  constexpr std::uint32_t MODE_MASK = 3u << 2;
  constexpr std::uint32_t CHAIN_MODE = 1u << 2;
  constexpr std::uint32_t ADDRESS_STACK_MASK = 3u << 4;
  constexpr std::uint32_t TAG_TRANSFER_ENABLE = 1u << 6;
  constexpr std::uint32_t TAG_INTERRUPT_ENABLE = 1u << 7;
  constexpr std::uint32_t START = 1u << 8;
  constexpr std::uint32_t TAG_MASK = UINT32_C(0xffff0000);
}

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

    void requireStopped() const;
    std::uint32_t decodeAddress(
      std::uint32_t value,
      const char *registerName) const;
    void transferQuadword();
    void readSourceChainTag();
    void configureSourceChainTag(
      std::uint32_t tagAddress,
      std::uint32_t low,
      std::uint32_t high);
    void completeTransfer();
    void updateAddressStackField();

    EEBus *eeBus;
    DMACController *dmacController;
    std::uint32_t channelControlRegister = 0;
    std::uint32_t memoryAddressRegister = 0;
    std::uint32_t quadwordCountRegister = 0;
    std::uint32_t tagAddressRegister = 0;
    std::array<std::uint32_t, 2> addressStackRegisters = {};
    bool terminateAfterPacket = false;
    bool path3Stalled = false;
    std::uint8_t addressStackDepth = 0;
    std::uint64_t transferredQuadwords = 0;
};

#endif
