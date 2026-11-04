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
  Completed,
  CompletedWithInterrupt
};

struct SIO2ReadResult
{
  SIO2AccessStatus status = SIO2AccessStatus::Unsupported;
  std::uint32_t value = 0;
};

// Provisional programming and a bounded single-packet absent-infrared profile.
class SIO2Controller final
{
  public:
    static constexpr std::size_t PORT_CONTROL_COUNT = 8;
    static constexpr std::size_t COMMAND_DESCRIPTOR_COUNT = 16;
    static constexpr std::size_t TRANSMIT_INPUT_CAPACITY = 1023;
    void reset() noexcept;
    std::uint32_t readControl() const noexcept;
    SIO2AccessStatus writeControl(std::uint32_t value) noexcept;
    // Completion requests an IRQ only when the device cause was clear.
    SIO2TransferResult startTransfer(std::uint32_t controlValue) noexcept;
    SIO2ReadResult readReceiveByte() noexcept;
    SIO2ReadResult readTransferStatus(std::size_t index) const;
    std::uint32_t readInterruptStatus() const noexcept;
    SIO2AccessStatus acknowledgeInterrupt(std::uint32_t value) noexcept;
    std::uint32_t readPortControl(std::size_t index) const;
    void writePortControl(std::size_t index, std::uint32_t value);
    std::uint32_t readCommandDescriptor(std::size_t index) const;
    void writeCommandDescriptor(std::size_t index, std::uint32_t value);
    SIO2AccessStatus writeTransmitByte(std::uint8_t value) noexcept;
    std::size_t transmitInputSize() const noexcept;
    std::uint8_t transmitInputByte(std::size_t index) const;

  private:
    friend class NekoSaveStateCodec;
    static bool supportsControl(std::uint32_t value) noexcept;
    bool hasValidTransferState() const noexcept;
    void clearTransmitInput() noexcept;
    std::uint32_t control = 0;
    std::array<std::uint32_t, PORT_CONTROL_COUNT> portControls{};
    std::array<std::uint32_t, COMMAND_DESCRIPTOR_COUNT> commandDescriptors{};
    std::array<std::uint8_t, TRANSMIT_INPUT_CAPACITY> transmitInput{};
    std::uint16_t transmitInputCount = 0;
    bool transferCompleted = false;
    std::uint8_t receiveRemaining = 0;
    bool interruptCause = false;
};

#endif
