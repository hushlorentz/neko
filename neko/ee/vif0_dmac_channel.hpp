#ifndef VIF0_DMAC_CHANNEL_HPP
#define VIF0_DMAC_CHANNEL_HPP

#include "dmac_channel_state.hpp"

class VIF0DMACChannel
{
  public:
    VIF0DMACChannel();

    static bool isRegisterAddress(std::uint32_t address);
    std::uint32_t channelControl() const;
    void writeChannelControl(std::uint32_t value);
    std::uint32_t memoryAddress() const;
    void writeMemoryAddress(std::uint32_t value);
    std::uint32_t quadwordCount() const;
    void writeQuadwordCount(std::uint32_t value);
    std::uint32_t tagAddress() const;
    void writeTagAddress(std::uint32_t value);
    std::uint32_t addressStack(std::size_t index) const;
    void writeAddressStack(std::size_t index, std::uint32_t value);

  private:
    DMACChannelState channelState;
};

#endif
