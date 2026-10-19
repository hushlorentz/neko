#include "sif_interrupt_router.hpp"

#include <exception>

#include "interrupt_controller.hpp"
#include "iop_interrupt_controller.hpp"

SIFInterruptRouter::SIFInterruptRouter(
  EEInterruptController &eeInterrupts,
  IOPInterruptController &iopInterrupts) :
  eeInterrupts(eeInterrupts),
  iopInterrupts(iopInterrupts)
{
}

void SIFInterruptRouter::requestSIFSignal(SIFSignal signal) noexcept
{
  switch (signal)
  {
    case SIFSignal::None:
      return;
    case SIFSignal::RequestIOP:
      iopInterrupts.assertSource(IOPInterruptSource::SBUS);
      return;
    case SIFSignal::RequestEE:
      eeInterrupts.setSource(EEInterruptSource::SBUS, true);
      return;
  }
  std::terminate();
}
