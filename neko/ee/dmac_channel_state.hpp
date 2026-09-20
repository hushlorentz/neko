#ifndef DMAC_CHANNEL_STATE_HPP
#define DMAC_CHANNEL_STATE_HPP

#include <array>
#include <cstddef>
#include <cstdint>

enum class DMACTagID : std::uint8_t
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

namespace DMACChannelControl
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

enum class DMACChannelDirectionPolicy : std::uint8_t
{
  Unrestricted,
  FromMemory
};

enum class DMACControlWriteEffect : std::uint8_t
{
  None,
  ResetContinuation
};

enum class DMACChannelTransition : std::uint8_t
{
  Continue,
  Complete
};

class DMACChannelState
{
  public:
    DMACChannelState(
      const char *channelName,
      DMACChannelDirectionPolicy directionPolicy);

    bool active() const;
    bool normalMode() const;
    bool tagTransferEnabled() const;
    bool hasPendingQuadword() const;

    std::uint32_t channelControl() const;
    DMACControlWriteEffect writeChannelControl(
      std::uint32_t value);
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

    DMACChannelTransition acceptQuadword();
    DMACChannelTransition acceptSourceChainTag(
      std::uint32_t low,
      std::uint32_t high);
    void completeTransfer();

  private:
    friend class NekoSaveStateCodec;

    void requireStopped() const;
    std::uint32_t decodeAddress(
      std::uint32_t value,
      const char *registerName) const;
    void updateAddressStackField();

    const char *name;
    DMACChannelDirectionPolicy direction;
    std::uint32_t channelControlRegister = 0;
    std::uint32_t memoryAddressRegister = 0;
    std::uint32_t quadwordCountRegister = 0;
    std::uint32_t tagAddressRegister = 0;
    std::array<std::uint32_t, 2> addressStackRegisters = {};
    bool terminateAfterPacket = false;
    std::uint8_t addressStackDepth = 0;
};

#endif
