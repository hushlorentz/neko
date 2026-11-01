#include "ipu.hpp"
#include "interrupt_controller.hpp"

IPU::IPU(EEInterruptController &interrupts) :
  interruptController(interrupts)
{
}

std::uint32_t IPU::control() const
{
  // Only the empty, synchronously completed command profile is representable.
  return 0;
}

std::uint8_t IPU::bitPosition() const
{
  return bitPositionRegister;
}

bool IPU::writeControl(std::uint32_t value)
{
  constexpr std::uint32_t RESET = 1u << 30;
  if (value != 0 && value != RESET)
  {
    return false;
  }
  if (value == RESET)
  {
    bitPositionRegister = 0;
    // Provisional reset-completion IRQ, including an already idle IPU.
    interruptController.setSource(EEInterruptSource::IPU, true);
  }
  return true;
}

bool IPU::writeCommand(std::uint32_t value)
{
  if (value > 0x7f)
  {
    return false;
  }
  // BCLR has no input requirement; FIFO transport is still unsupported.
  bitPositionRegister = static_cast<std::uint8_t>(value);
  interruptController.setSource(EEInterruptSource::IPU, true);
  return true;
}
