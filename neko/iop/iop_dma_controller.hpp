#ifndef IOP_DMA_CONTROLLER_HPP
#define IOP_DMA_CONTROLLER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "iop_bus.hpp"
#include "iop_dma_channel_state.hpp"
#include "iop_interrupt_controller.hpp"

class IOPSIF1DMAChannel;
class IOPSIF0DMAChannel;
class SIF;

enum class IOPDMABank : std::uint8_t
{
  Primary,
  Secondary
};

enum class IOPDMAChannel : std::uint8_t
{
  SIF0 = 9,
  SIF1 = 10
};

enum class IOPDMABeatOutcome : std::uint8_t
{
  Accepted,
  Completed, // Accepted terminal beat, not merely an individual word.
  Wait,
  Rejected
};

enum class IOPDMAFailure : std::uint8_t
{
  None,
  UnalignedHeader,
  DestinationWrite,
  DescriptorRead,
  SourceRead
};

struct IOPDMABeatResult
{
  IOPDMABeatOutcome outcome = IOPDMABeatOutcome::Wait;
  IOPDMAChannel channel = IOPDMAChannel::SIF1;
  IOPDMAFailure failure = IOPDMAFailure::None;
  IOPAddress address = 0;
  IOPWord value = 0;
  IOPBusStatus status = IOPBusStatus::Completed;
};

class IOPDMATransferError final : public std::runtime_error
{
  public:
    explicit IOPDMATransferError(const IOPDMABeatResult &result);
    const IOPDMABeatResult &result() const;

  private:
    IOPDMABeatResult rejectedBeat;
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
    // Provisional retained word; raw bit semantics remain unestablished.
    IOPWord auxiliaryControl() const;
    void writeAuxiliaryControl(IOPWord value);
    // Provisional disabled-FDMA initialization only; no retained state or effects.
    IOPDMAWriteOutcome writeDPCR3Initialization(IOPWord value) const;
    // Provisional channel-0/1/2 zero initialization; no retained state or effects.
    IOPDMAWriteOutcome writeChannelRegisterInitialization(IOPWord value) const;
    bool channelEnabled(IOPDMAChannel channel) const;
    std::uint8_t channelPriority(IOPDMAChannel channel) const;
    std::array<IOPDMAChannel, 2> sifGrantOrder() const;
    IOPDMAChannel sifTieBreakChannel() const;
    IOPDMABeatResult inspectSIF(
      const IOPSIF1DMAChannel &channel, const IOPBus &bus, const SIF &sif) const;
    IOPDMABeatResult clockSIF(IOPSIF1DMAChannel &channel, IOPBus &bus, SIF &sif);
    IOPDMABeatResult inspectSIF(
      const IOPSIF0DMAChannel &channel, const IOPBus &bus, const SIF &sif) const;
    // One explicitly requested endpoint grant; no peer stepping.
    IOPDMABeatResult clockSIF(IOPSIF0DMAChannel &channel, IOPBus &bus, SIF &sif);
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
    void validateSIFWiring(
      const IOPSIF1DMAChannel &channel, const IOPBus &bus, const SIF &sif) const;
    void validateSIFWiring(
      const IOPSIF0DMAChannel &channel, const IOPBus &bus, const SIF &sif) const;
    void publishInterrupt();

    IOPInterruptController &interrupts;
    std::array<Bank, 2> banks = {};
    IOPWord auxiliaryControlRegister = 0;
    IOPDMAChannel sifNextChannel = IOPDMAChannel::SIF0;
};

#endif
