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

// Provisional programming and bounded ingress, without transfer execution.
class SIO2Controller final
{
  public:
    static constexpr std::size_t PORT_CONTROL_COUNT = 8;
    static constexpr std::size_t COMMAND_DESCRIPTOR_COUNT = 16;
    static constexpr std::size_t TRANSMIT_INPUT_CAPACITY = 1023;
    void reset() noexcept;
    std::uint32_t readControl() const noexcept;
    SIO2AccessStatus writeControl(std::uint32_t value) noexcept;
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
    void clearTransmitInput() noexcept;
    std::uint32_t control = 0;
    std::array<std::uint32_t, PORT_CONTROL_COUNT> portControls{};
    std::array<std::uint32_t, COMMAND_DESCRIPTOR_COUNT> commandDescriptors{};
    std::array<std::uint8_t, TRANSMIT_INPUT_CAPACITY> transmitInput{};
    std::uint16_t transmitInputCount = 0;
};

#endif
