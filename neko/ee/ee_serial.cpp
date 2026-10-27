#include "ee_serial.hpp"

namespace
{
  constexpr std::uint32_t TRANSMIT_FULL_STATUS = 0x8000;
}

bool EESerial::writeLineControl(std::uint32_t value) const noexcept
{
  return value == 0x20;
}

bool EESerial::writeInterruptEnable(std::uint32_t value) const noexcept
{
  return value == 0;
}

bool EESerial::isSupportedBaudRate(std::uint32_t value) noexcept
{
  return value == 0 || value == 0x1d;
}

bool EESerial::writeBaudRate(std::uint32_t value) noexcept
{
  if (!isSupportedBaudRate(value))
  {
    return false;
  }
  baudEncoding = static_cast<std::uint8_t>(value);
  return true;
}

std::uint32_t EESerial::selectedFrameCycles() const noexcept
{
  return baudEncoding == 0 ?
    DEFAULT_TRANSMIT_FRAME_CYCLES : MAX_TRANSMIT_FRAME_CYCLES;
}

bool EESerial::writeFIFOControl(std::uint32_t value) noexcept
{
  if (value == 0)
  {
    resetHeld = false;
    return true;
  }
  if (value == 7)
  {
    cancelFrame();
    resetHeld = true;
    return true;
  }
  return false;
}

std::uint32_t EESerial::readStatus() const noexcept
{
  return remainingCycles != 0 ? TRANSMIT_FULL_STATUS : 0;
}

EESerialTransmitResult EESerial::transmitByte(std::uint8_t byte) noexcept
{
  if (resetHeld)
  {
    return EESerialTransmitResult::ResetHeld;
  }
  if (remainingCycles != 0)
  {
    return EESerialTransmitResult::Full;
  }
  pendingByte = byte;
  remainingCycles = selectedFrameCycles();
  return EESerialTransmitResult::Accepted;
}

EESerialCompletion EESerial::advance() noexcept
{
  if (remainingCycles == 0)
  {
    return {};
  }
  --remainingCycles;
  if (remainingCycles != 0)
  {
    return {};
  }
  const EESerialCompletion result{true, pendingByte};
  pendingByte = 0;
  return result;
}

void EESerial::cancelFrame() noexcept
{
  remainingCycles = 0;
  pendingByte = 0;
}

void EESerial::reset() noexcept
{
  cancelFrame();
  resetHeld = false;
  baudEncoding = 0;
}
