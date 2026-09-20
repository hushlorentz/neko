#ifndef DMAC_CONTROLLER_HPP
#define DMAC_CONTROLLER_HPP

#include <cstdint>

namespace DMACControl
{
  constexpr std::uint32_t DMA_ENABLE = 1u;
}

namespace DMACStatus
{
  constexpr std::uint32_t CHANNEL_1 = 1u << 1;
  constexpr std::uint32_t CHANNEL_2 = 1u << 2;
  constexpr std::uint32_t CHANNEL_1_MASK = 1u << 17;
  constexpr std::uint32_t CHANNEL_2_MASK = 1u << 18;
}

class DMACController
{
  public:
    std::uint32_t control() const;
    void writeControl(std::uint32_t value);
    std::uint32_t status() const;
    void writeStatus(std::uint32_t value);
    bool enabled() const;
    void signalChannelCompletion(std::uint32_t channel);
    bool interruptPending() const;

  private:
    friend class NekoSaveStateCodec;

    std::uint32_t controlRegister = 0;
    std::uint32_t statusRegister = 0;
    std::uint32_t statusMaskRegister = 0;
};

#endif
