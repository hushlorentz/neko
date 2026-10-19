#ifndef IOP_DMA_CONTROLLER_HPP
#define IOP_DMA_CONTROLLER_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "iop_interrupt_controller.hpp"

enum class IOPDMABank : std::uint8_t
{
  Primary,
  Secondary
};

enum class IOPDMAChannel : std::uint8_t
{
  SIF1 = 10
};

class IOPDMAController final
{
  public:
    explicit IOPDMAController(IOPInterruptController &interrupts);
    IOPDMAController(const IOPDMAController &) = delete;
    IOPDMAController &operator=(const IOPDMAController &) = delete;

    IOPWord priority(IOPDMABank bank) const;
    void writePriority(IOPDMABank bank, IOPWord value);
    IOPWord interruptStatus(IOPDMABank bank) const;
    void writeInterrupt(IOPDMABank bank, IOPWord value);
    bool channelEnabled(IOPDMAChannel channel) const;
    std::uint8_t channelPriority(IOPDMAChannel channel) const;
    void signalChannelCompletion(IOPDMAChannel channel);
    // Derived DMA request, not the independently acknowledged INTC latch.
    bool interruptPending() const;

  private:
    friend class NekoSaveStateCodec;

    struct Bank
    {
      IOPWord priority = 0;
      IOPWord control = 0;
      std::uint8_t flags = 0;
    };

    static std::size_t bankIndex(IOPDMABank bank);
    static std::uint8_t channelSlot(IOPDMAChannel channel);
    std::uint8_t activeFlags(std::size_t index) const;
    void publishInterrupt();

    IOPInterruptController &interrupts;
    std::array<Bank, 2> banks = {};
};

#endif
