#include "ipu.hpp"
#include "interrupt_controller.hpp"

IPU::IPU(EEInterruptController &interrupts) :
  interruptController(interrupts)
{
}

std::uint32_t IPU::control() const
{
  // Only the empty, command-disabled profile is representable.
  return 0;
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
    // Provisional reset-completion IRQ, including an already idle IPU.
    interruptController.setSource(EEInterruptSource::IPU, true);
  }
  return true;
}
