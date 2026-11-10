#ifndef SIO2_CONTROLLER_HPP
#define SIO2_CONTROLLER_HPP

#include <array>
#include <cstddef>
#include <cstdint>

enum class SIO2AccessStatus : std::uint8_t
{
  Accepted,
  Unsupported
};

enum class SIO2TransferResult : std::uint8_t
{
  Unsupported,
  Idle,
  Pending,
  Completed,
  CompletedWithInterrupt
};

struct SIO2ReadResult
{
  SIO2AccessStatus status = SIO2AccessStatus::Unsupported;
  std::uint32_t value = 0;
};

enum class SIO2IngressPhase : std::uint8_t
{
  Empty,
  CPU,
  DMAOpen,
  DMASealed
};

enum class SIO2DMAResultPhase : std::uint8_t
{
  Empty,
  Pending,
  Ready
};

// Provisional programming and bounded absent-infrared/card profiles.
class SIO2Controller final
{
  public:
    static constexpr std::size_t PORT_CONTROL_COUNT = 8;
    static constexpr std::size_t COMMAND_DESCRIPTOR_COUNT = 16;
    static constexpr std::size_t TRANSMIT_INPUT_CAPACITY = 1023;
    static constexpr std::size_t DMA_INPUT_BYTES = 144;
    static constexpr std::size_t DMA_PROBE_REPLY_BYTES = 4;
    static constexpr std::size_t DMA_PROBE_PADDING_BYTES = 140;
    void reset();
    std::uint32_t readControl() const noexcept;
    SIO2AccessStatus writeControl(std::uint32_t value) noexcept;
    // Completion requests an IRQ only when the device cause was clear.
    SIO2TransferResult startTransfer(std::uint32_t controlValue) noexcept;
    SIO2TransferResult completePendingTransfer() noexcept;
    SIO2DMAResultPhase dmaResultPhase() const noexcept;
    std::size_t dmaResultSize() const noexcept;
    std::uint8_t dmaResultByte(std::size_t index) const;
    SIO2ReadResult readReceiveByte() noexcept;
    SIO2ReadResult readTransferStatus(std::size_t index) const;
    std::uint32_t readInterruptStatus() const noexcept;
    SIO2AccessStatus acknowledgeInterrupt(std::uint32_t value) noexcept;
    std::uint32_t readPortControl(std::size_t index) const;
    SIO2AccessStatus writePortControl(std::size_t index, std::uint32_t value);
    std::uint32_t readCommandDescriptor(std::size_t index) const;
    SIO2AccessStatus writeCommandDescriptor(std::size_t index, std::uint32_t value);
    SIO2AccessStatus writeTransmitByte(std::uint8_t value) noexcept;
    std::size_t transmitInputSize() const noexcept;
    SIO2IngressPhase transmitInputPhase() const noexcept;
    std::uint8_t transmitInputByte(std::size_t index) const;

  private:
    friend class NekoSaveStateCodec;
    friend class IOPDMAController;
    static bool supportsControl(std::uint32_t value) noexcept;
    bool hasDMAProbeProgramming() const noexcept;
    bool hasValidTransferState() const noexcept;
    std::uint32_t peekDMAResultWord() const noexcept;
    void consumeDMAResultWord() noexcept;
    void clearTransmitInput() noexcept;
    bool beginDMAInput() noexcept;
    bool canAcceptDMAWord() const noexcept;
    void acceptDMAWord(std::uint32_t value) noexcept;
    void finishDMAInput() noexcept;
    void cancelDMAInput() noexcept;
    std::uint32_t control = 0;
    std::array<std::uint32_t, PORT_CONTROL_COUNT> portControls{};
    std::array<std::uint32_t, COMMAND_DESCRIPTOR_COUNT> commandDescriptors{};
    std::array<std::uint8_t, TRANSMIT_INPUT_CAPACITY> transmitInput{};
    std::uint16_t transmitInputCount = 0;
    SIO2IngressPhase ingressPhase = SIO2IngressPhase::Empty;
    bool transferCompleted = false;
    std::uint8_t receiveRemaining = 0;
    bool interruptCause = false;
    SIO2DMAResultPhase resultPhase = SIO2DMAResultPhase::Empty;
    std::uint8_t resultCursor = 0;
};

#endif
