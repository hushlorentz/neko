#ifndef IPU_DMAC_CHANNEL_HPP
#define IPU_DMAC_CHANNEL_HPP

#include "dmac_channel_state.hpp"

enum class IPUDMACChannelKind : std::uint8_t
{
  FromIPU,
  ToIPU
};

class IPUDMACChannel
{
  public:
    explicit IPUDMACChannel(IPUDMACChannelKind kind);

    static bool isRegisterAddress(std::uint32_t address);
    IPUDMACChannelKind kind() const;
    std::uint32_t channelControl() const;
    void writeChannelControl(std::uint32_t value);
    std::uint32_t memoryAddress() const;
    void writeMemoryAddress(std::uint32_t value);
    std::uint32_t quadwordCount() const;
    void writeQuadwordCount(std::uint32_t value);
    std::uint32_t tagAddress() const;
    void writeTagAddress(std::uint32_t value);

  private:
    void requireTagAddress() const;

    IPUDMACChannelKind channelKind;
    DMACChannelState channelState;
};

#endif
