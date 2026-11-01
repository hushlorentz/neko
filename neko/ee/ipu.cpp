#include "ipu.hpp"
#include "interrupt_controller.hpp"

IPU::IPU(EEInterruptController &interrupts) :
  interruptController(interrupts)
{
}

std::uint32_t IPU::control() const
{
  return inputFIFOCount;
}

std::uint8_t IPU::bitPosition() const
{
  return bitPositionRegister;
}

std::uint32_t IPU::bitPositionStatus() const
{
  // No consuming command has moved input into an internal FP buffer yet.
  return bitPositionRegister |
    (static_cast<std::uint32_t>(inputFIFOCount) << 8);
}

bool IPU::inputWriteReady() const
{
  return inputFIFOCount < inputFIFO.size();
}

bool IPU::submitInput(const EEQuadword &value)
{
  if (!inputWriteReady())
  {
    return false;
  }
  inputFIFO[inputFIFOCount++] = value;
  return true;
}

void IPU::clearInput()
{
  inputFIFO.fill({});
  inputFIFOCount = 0;
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
    clearInput();
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
  clearInput();
  bitPositionRegister = static_cast<std::uint8_t>(value);
  interruptController.setSource(EEInterruptSource::IPU, true);
  return true;
}
