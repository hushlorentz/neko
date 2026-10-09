#ifndef IOP_CORE_HPP
#define IOP_CORE_HPP

#include <array>
#include <cstddef>

#include "iop_types.hpp"

class IOPBus;
struct IOPCoreTestAccess;

enum class IOPWriteEffect : std::uint8_t
{
  None,
  Write
};

enum class IOPControlFlowEffect : std::uint8_t
{
  Hold,
  Sequential,
  ScheduleBranch,
  SetProgramCounter
};

enum class IOPInstructionCompletion : std::uint8_t
{
  Halted,
  Retired
};

enum class IOPHILOAccessEffect : std::uint8_t
{
  None,
  ReadHI,
  ReadLO,
  WriteHI,
  WriteLO,
  WritePair
};

struct IOPDestinationEffect
{
  IOPWriteEffect kind = IOPWriteEffect::None;
  std::uint8_t destination = 0;
  IOPWord value = 0;
};

struct IOPValueEffect
{
  IOPWriteEffect kind = IOPWriteEffect::None;
  IOPWord value = 0;
};

struct IOPControlFlowRequest
{
  IOPControlFlowEffect kind = IOPControlFlowEffect::Hold;
  IOPAddress target = 0;
};

struct IOPInstructionEffects
{
  IOPDestinationEffect destination;
  IOPValueEffect hi;
  IOPValueEffect lo;
  IOPHILOAccessEffect hiLoAccess = IOPHILOAccessEffect::None;
  IOPControlFlowRequest controlFlow;
  IOPPendingException exception;
  IOPInstructionCompletion completion =
    IOPInstructionCompletion::Halted;
  IOPStopReason stopReason = IOPStopReason::ExecutionException;
};

struct IOPHILOState
{
  std::uint8_t hiWriteHazardInstructions = 0;
  std::uint8_t loWriteHazardInstructions = 0;
  bool unreadMultiplyDivideHI = false;
  bool unreadMultiplyDivideLO = false;
};

class IOPCore final
{
  public:
    static constexpr std::size_t GENERAL_REGISTER_COUNT = 32;

    IOPCore();

    void reset();
    void attachBus(IOPBus *bus);
    void startExecution(IOPAddress entryPoint);
    void stepInstruction();

    IOPAddressClassification classifyAddress(
      IOPAddress virtualAddress) const;
    IOPMemoryReadResult fetchInstruction() const;
    IOPMemoryReadResult readData8(
      IOPAddress virtualAddress) const;
    IOPMemoryReadResult readData16(
      IOPAddress virtualAddress) const;
    IOPMemoryReadResult readData32(
      IOPAddress virtualAddress) const;
    IOPMemoryWriteResult writeData8(
      IOPAddress virtualAddress,
      std::uint8_t value);
    IOPMemoryWriteResult writeData16(
      IOPAddress virtualAddress,
      std::uint16_t value);
    IOPMemoryWriteResult writeData32(
      IOPAddress virtualAddress,
      IOPWord value);

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
    std::uint64_t retiredInstructions() const;

  private:
    friend struct IOPCoreTestAccess;

    IOPInstructionEffects instructionEffects(
      const struct IOPDecodeResult &decoded) const;
    static IOPInstructionEffects fetchFailureEffects(
      const IOPMemoryReadResult &fetch);
    void commitInstructionEffects(
      IOPAddress instructionAddress,
      const IOPInstructionEffects &effects);
    void setGeneralRegister(
      std::size_t index,
      IOPWord value);
    IOPBus &attachedBus() const;
    IOPMemoryReadResult readMemory(
      IOPAddress virtualAddress,
      std::size_t width,
      bool instructionFetch) const;
    IOPMemoryWriteResult writeMemory(
      IOPAddress virtualAddress,
      std::size_t width,
      IOPWord value);

    IOPBus *bus = nullptr;
    IOPAddress pc = IOPReset::VECTOR;
    std::array<IOPWord, GENERAL_REGISTER_COUNT - 1>
      writableGeneralRegisters = {};
    IOPWord hiRegister = 0;
    IOPWord loRegister = 0;
    IOPHILOState hiLoState;
    IOPCOP0State cop0;
    IOPBranchContinuation branch;
    IOPDelayedResultContinuation delayedResult;
    IOPPendingException pendingException;
    IOPExecutionState state = IOPExecutionState::Halted;
    IOPStopReason haltReason = IOPStopReason::None;
    IOPCycleCount cycles = 0;
    std::uint64_t retiredInstructionTotal = 0;
};

#endif
