#ifndef SIF_INTERRUPT_ROUTER_HPP
#define SIF_INTERRUPT_ROUTER_HPP

#include "sif.hpp"

class EEInterruptController;
class IOPInterruptController;

// The caller owns both controllers; requests do not clock or enter either CPU.
class SIFInterruptRouter final : public SIFSignalReceiver
{
  public:
    SIFInterruptRouter(
      EEInterruptController &eeInterrupts,
      IOPInterruptController &iopInterrupts);
    void requestSIFSignal(SIFSignal signal) noexcept override;

  private:
    EEInterruptController &eeInterrupts;
    IOPInterruptController &iopInterrupts;
};

#endif
