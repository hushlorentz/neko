#ifndef SCRATCHPAD_DMAC_CHANNEL_HPP
#define SCRATCHPAD_DMAC_CHANNEL_HPP

#include <cstdint>

#include "clocked_component.hpp"

class DMACController;
class EEBus;
class EEMemorySystem;

enum class ScratchpadDMACChannelKind : std::uint8_t
{
  FromScratchpad,
  ToScratchpad
};

class ScratchpadDMACChannel : public ClockedComponent
{
  public:
    ScratchpadDMACChannel(
      ScratchpadDMACChannelKind kind,
      DMACController *controller,
      EEBus *bus,
      EEMemorySystem *memorySystem);

    bool clockActive() const override;
    void clock() override;

    bool active() const;
    std::uint32_t channelControl() const;
    void writeChannelControl(std::uint32_t value);
    std::uint32_t memoryAddress() const;
    void writeMemoryAddress(std::uint32_t value);
    std::uint32_t quadwordCount() const;
    void writeQuadwordCount(std::uint32_t value);
    std::uint32_t tagAddress() const;
    void writeTagAddress(std::uint32_t value);
    std::uint32_t scratchpadAddress() const;
    void writeScratchpadAddress(std::uint32_t value);

  private:
    friend class NekoSaveStateCodec;

    void requireStopped() const;
    void requireTagAddress() const;
    void transferQuadword(bool interleave);
    void completeTransfer();
    const char *name() const;

    ScratchpadDMACChannelKind channelKind;
    DMACController *dmacController;
    EEBus *eeBus;
    EEMemorySystem *eeMemorySystem;
    std::uint32_t channelControlRegister = 0;
    std::uint32_t memoryAddressRegister = 0;
    std::uint32_t quadwordCountRegister = 0;
    std::uint32_t tagAddressRegister = 0;
    std::uint32_t scratchpadAddressRegister = 0;
    std::uint16_t interleaveQuadwordsRemaining = 0;
};

#endif
