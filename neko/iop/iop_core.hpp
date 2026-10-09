#ifndef IOP_CORE_HPP
#define IOP_CORE_HPP

#include <array>
#include <cstddef>

#include "iop_types.hpp"

struct IOPCoreTestAccess;

class IOPCore final
{
  public:
    static constexpr std::size_t GENERAL_REGISTER_COUNT = 32;

    IOPCore();

    void reset();

    IOPAddress programCounter() const;
    IOPWord generalRegister(std::size_t index) const;
    IOPWord hi() const;
    IOPWord lo() const;

    IOPAddress badVirtualAddress() const;
    IOPWord status() const;
    IOPWord cause() const;
    IOPAddress exceptionProgramCounter() const;
    IOPWord processorRevision() const;

    const IOPBranchContinuation &
      branchContinuation() const;
    const IOPDelayedResultContinuation &
      delayedResultContinuation() const;
    const IOPPendingException &exception() const;

    IOPExecutionState executionState() const;
    IOPStopReason stopReason() const;
    IOPCycleCount elapsedCycles() const;

  private:
    friend struct IOPCoreTestAccess;

    void setGeneralRegister(
      std::size_t index,
      IOPWord value);

    IOPAddress pc = IOPReset::VECTOR;
    std::array<IOPWord, GENERAL_REGISTER_COUNT - 1>
      writableGeneralRegisters = {};
    IOPWord hiRegister = 0;
    IOPWord loRegister = 0;
    IOPCOP0State cop0;
    IOPBranchContinuation branch;
    IOPDelayedResultContinuation delayedResult;
    IOPPendingException pendingException;
    IOPExecutionState state = IOPExecutionState::Halted;
    IOPStopReason haltReason = IOPStopReason::None;
    IOPCycleCount cycles = 0;
};

#endif
