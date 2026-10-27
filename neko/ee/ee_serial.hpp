#ifndef EE_SERIAL_HPP
#define EE_SERIAL_HPP

#include <cstdint>

enum class EESerialTransmitResult : std::uint8_t
{
  Accepted,
  Full,
  ResetHeld
};

struct EESerialCompletion
{
  bool completed = false;
  std::uint8_t byte = 0;
};

// Provisional single-slot CPU-clock 8N1 baud profiles, not a general UART model.
class EESerial
{
  public:
    bool writeLineControl(std::uint32_t value) const noexcept;
    bool writeInterruptEnable(std::uint32_t value) const noexcept;
    bool writeBaudRate(std::uint32_t value) noexcept;
    bool writeFIFOControl(std::uint32_t value) noexcept;
    std::uint32_t readStatus() const noexcept;
    EESerialTransmitResult transmitByte(std::uint8_t byte) noexcept;
    // One elapsed master tick; completion is returned once, not retained.
    EESerialCompletion advance() noexcept;
    void reset() noexcept;
    std::uint64_t stateHash() const;

  private:
    friend class NekoSaveStateCodec;

    static bool isSupportedBaudRate(std::uint32_t value) noexcept;
    std::uint32_t selectedFrameCycles() const noexcept;
    void cancelFrame() noexcept;

    // Emulator policy: ten bits at 256 ticks times the admitted divisor.
    static constexpr std::uint32_t DEFAULT_TRANSMIT_FRAME_CYCLES = 2560;
    static constexpr std::uint32_t MAX_TRANSMIT_FRAME_CYCLES =
      DEFAULT_TRANSMIT_FRAME_CYCLES * 29;
    std::uint32_t remainingCycles = 0;
    std::uint8_t pendingByte = 0;
    bool resetHeld = false;
    std::uint8_t baudEncoding = 0;
};

#endif
