#include "iop_core.hpp"

#include <stdexcept>

IOPCore::IOPCore()
{
  reset();
}

void IOPCore::reset()
{
  pc = IOPReset::VECTOR;
  writableGeneralRegisters.fill(0);
  hiRegister = 0;
  loRegister = 0;
  cop0.badVirtualAddress = 0;
  cop0.status =
    IOPCOP0Status::BOOT_EXCEPTION_VECTORS |
    IOPCOP0Status::TLB_SHUTDOWN;
  cop0.cause = 0;
  cop0.exceptionProgramCounter = 0;
  branch = {};
  delayedResult = {};
  pendingException = {};
  state = IOPExecutionState::Halted;
  haltReason = IOPStopReason::None;
  cycles = 0;
}

IOPAddress IOPCore::programCounter() const
{
  return pc;
}

IOPWord IOPCore::generalRegister(std::size_t index) const
{
  if (index >= GENERAL_REGISTER_COUNT)
  {
    throw std::out_of_range(
      "IOP general register index is out of range.");
  }
  if (index == 0)
  {
    return 0;
  }
  return writableGeneralRegisters[index - 1];
}

void IOPCore::setGeneralRegister(
  std::size_t index,
  IOPWord value)
{
  if (index >= GENERAL_REGISTER_COUNT)
  {
    throw std::out_of_range(
      "IOP general register index is out of range.");
  }
  if (index != 0)
  {
    writableGeneralRegisters[index - 1] = value;
  }
}

IOPWord IOPCore::hi() const
{
  return hiRegister;
}

IOPWord IOPCore::lo() const
{
  return loRegister;
}

IOPAddress IOPCore::badVirtualAddress() const
{
  return cop0.badVirtualAddress;
}

IOPWord IOPCore::status() const
{
  return cop0.status;
}

IOPWord IOPCore::cause() const
{
  return cop0.cause;
}

IOPAddress IOPCore::exceptionProgramCounter() const
{
  return cop0.exceptionProgramCounter;
}

IOPWord IOPCore::processorRevision() const
{
  return IOPCOP0::PROCESSOR_REVISION;
}

const IOPBranchContinuation &
IOPCore::branchContinuation() const
{
  return branch;
}

const IOPDelayedResultContinuation &
IOPCore::delayedResultContinuation() const
{
  return delayedResult;
}

const IOPPendingException &IOPCore::exception() const
{
  return pendingException;
}

IOPExecutionState IOPCore::executionState() const
{
  return state;
}

IOPStopReason IOPCore::stopReason() const
{
  return haltReason;
}

IOPCycleCount IOPCore::elapsedCycles() const
{
  return cycles;
}
