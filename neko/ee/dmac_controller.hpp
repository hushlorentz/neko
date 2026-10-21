#ifndef DMAC_CONTROLLER_HPP
#define DMAC_CONTROLLER_HPP

#include <cstdint>

class SIF1DMACChannel;
class DMACChannelState;
enum class SIF1DMABeatOutcome : std::uint8_t;
class SIF0DMACChannel;
enum class SIF0DMABeatOutcome : std::uint8_t;

namespace DMACControl
{
  constexpr std::uint32_t DMA_ENABLE = 1u;
  constexpr std::uint32_t STALL_SOURCE_MASK = 3u << 4;
  constexpr std::uint32_t STALL_DRAIN_MASK = 3u << 6;
  constexpr std::uint32_t WRITABLE_MASK =
    DMA_ENABLE | STALL_SOURCE_MASK | STALL_DRAIN_MASK;
}

namespace DMACStatus
{
  constexpr std::uint32_t CHANNEL_1 = 1u << 1;
  constexpr std::uint32_t CHANNEL_2 = 1u << 2;
  constexpr std::uint32_t CHANNEL_5 = 1u << 5;
  constexpr std::uint32_t CHANNEL_6 = 1u << 6;
  constexpr std::uint32_t CHANNEL_8 = 1u << 8;
  constexpr std::uint32_t CHANNEL_9 = 1u << 9;
  constexpr std::uint32_t BUS_ERROR = 1u << 15;
  constexpr std::uint32_t STALL = 1u << 13;
  constexpr std::uint32_t STALL_MASK = 1u << 29;
  constexpr std::uint32_t CHANNEL_1_MASK = 1u << 17;
  constexpr std::uint32_t CHANNEL_2_MASK = 1u << 18;
  constexpr std::uint32_t CHANNEL_5_MASK = 1u << 21;
  constexpr std::uint32_t CHANNEL_6_MASK = 1u << 22;
  constexpr std::uint32_t CHANNEL_8_MASK = 1u << 24;
  constexpr std::uint32_t CHANNEL_9_MASK = 1u << 25;
}

namespace DMACInterleave
{
  constexpr std::uint32_t SKIP_MASK = 0xff;
  constexpr std::uint32_t TRANSFER_MASK = 0xffu << 16;
}

namespace DMACPriority
{
  constexpr std::uint32_t COP_CONTROL_MASK = 0x3ff;
  constexpr std::uint32_t CHANNEL_ENABLE_MASK = 0x3ffu << 16;
  constexpr std::uint32_t ENABLE = UINT32_C(0x80000000);
  constexpr std::uint32_t WRITABLE_MASK =
    COP_CONTROL_MASK | CHANNEL_ENABLE_MASK | ENABLE;
}

enum class DMACTagPriority : std::uint8_t
{
  Unchanged,
  Disable,
  Enable
};

class DMACController
{
  public:
    std::uint32_t control() const;
    void writeControl(std::uint32_t value);
    std::uint32_t status() const;
    void writeStatus(std::uint32_t value);
    std::uint32_t priorityControl() const;
    void writePriorityControl(std::uint32_t value);
    std::uint32_t stallAddress() const;
    void writeStallAddress(std::uint32_t value);
    void publishStallAddress(std::uint32_t channel, std::uint32_t address);
    bool admitDrainSlice(std::uint32_t channel, const DMACChannelState &state);
    static bool validControl(std::uint32_t value);
    static DMACTagPriority decodeTagPriority(std::uint32_t tag);
    void applyTagPriority(DMACTagPriority priority);
    std::uint32_t interleaveSize() const;
    void writeInterleaveSize(std::uint32_t value);
    std::uint8_t interleaveSkipQWC() const;
    std::uint8_t interleaveTransferQWC() const;
    bool enabled() const;
    bool channelEnabled(std::uint32_t channel) const;
    SIF1DMABeatOutcome clockSIF(SIF1DMACChannel &sif1);
    // One explicitly requested normal receiver grant; no peer stepping.
    SIF0DMABeatOutcome clockSIF(SIF0DMACChannel &sif0);
    void signalChannelCompletion(std::uint32_t channel);
    void signalBusError(std::uint32_t channel);
    bool interruptPending() const;

  private:
    friend class NekoSaveStateCodec;
    friend class NekoSystem;

    std::uint32_t controlRegister = 0;
    std::uint32_t statusRegister = 0;
    std::uint32_t statusMaskRegister = 0;
    std::uint32_t priorityControlRegister = 0;
    std::uint32_t stallAddressRegister = 0;
    std::uint32_t interleaveSizeRegister = 0;
};

#endif
