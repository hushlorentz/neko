#ifndef IOP_CORE_HPP
#define IOP_CORE_HPP

#include <array>
#include <cstddef>
#include <memory>

#include "iop_cache.hpp"
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

enum class IOPStoreOperation : std::uint8_t
{
  None,
  Byte,
  Halfword,
  Word,
  MaskedWord
};

struct IOPStoreRequest
{
  IOPStoreOperation operation = IOPStoreOperation::None;
  IOPAddress virtualAddress = 0;
  IOPWord value = 0;
  std::uint8_t byteEnable = 0;
};

enum class IOPCOP0WriteTarget : std::uint8_t
{
  None,
  Status,
  StatusStack,
  Cause,
  ExceptionProgramCounter
};

struct IOPCOP0WriteRequest
{
  IOPCOP0WriteTarget target = IOPCOP0WriteTarget::None;
  IOPWord value = 0;
};

struct IOPInstructionEffects
{
  IOPDestinationEffect destination;
  IOPValueEffect hi;
  IOPValueEffect lo;
  IOPHILOAccessEffect hiLoAccess = IOPHILOAccessEffect::None;
  IOPControlFlowRequest controlFlow;
  IOPDelayedResultContinuation delayedResult;
  IOPStoreRequest store;
  IOPCOP0WriteRequest cop0Write;
  IOPExceptionRecord exception;
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
    bool hasAttachedBus() const;
    void prepareFreshExecution(
      IOPAddress entryPoint,
      IOPAddress stackPointer,
      IOPAddress returnAddress);
    void startExecution(IOPAddress entryPoint);
    void haltExecution();
    void resumeExecution();
    void stepInstruction();
    void setInterruptInput(IOPInterruptInput input, bool asserted);

    IOPAddressClassification classifyAddress(
      IOPAddress virtualAddress) const;
    IOPMemoryReadResult fetchInstruction();
    IOPMemoryReadResult readData8(
      IOPAddress virtualAddress);
    IOPMemoryReadResult readData16(
      IOPAddress virtualAddress);
    IOPMemoryReadResult readData32(
      IOPAddress virtualAddress);
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
    const IOPCache &cache() const;

    const IOPBranchContinuation &
      branchContinuation() const;
    const IOPDelayedResultContinuation &
      delayedResultContinuation() const;
    // Entry event from the last step, cleared when the next step begins.
    const IOPExceptionRecord &exception() const;
    const IOPStoreFailure &storeFailure() const;

    IOPExecutionState executionState() const;
    IOPStopReason stopReason() const;
    IOPCycleCount elapsedCycles() const;
    std::uint64_t retiredInstructions() const;

  private:
    friend struct IOPCoreTestAccess;
    friend struct IOPCacheTestAccess;

    IOPInstructionEffects instructionEffects(
      const struct IOPDecodeResult &decoded);
    IOPInstructionEffects alignedLoadEffects(
      const struct IOPInstruction &instruction,
      IOPWord base);
    IOPInstructionEffects wordMergeLoadEffects(
      const struct IOPInstruction &instruction,
      IOPWord base,
      IOPWord target);
    IOPInstructionEffects cop0TransferEffects(
      const struct IOPInstruction &instruction,
      IOPWord target) const;
    void commitCOP0Write(const IOPCOP0WriteRequest &request);
    bool interruptDeliverable() const;
    void enterException(
      IOPAddress instructionAddress,
      const IOPExceptionRecord &exception);
    void commitInstructionEffects(
      IOPAddress instructionAddress,
      const IOPInstructionEffects &effects);
    void setGeneralRegister(
      std::size_t index,
      IOPWord value);
    IOPBus &attachedBus() const;
    IOPCacheAccessPlan planCacheAccess(
      IOPCacheAccess access,
      const IOPAddressClassification &classification,
      std::size_t width) const;
    void synchronizeCacheConfiguration(IOPBus &physicalBus);
    IOPMemoryReadResult readMemory(
      IOPAddress virtualAddress,
      std::size_t width,
      IOPCacheAccess access);
    IOPMemoryWriteResult writeMemory(
      IOPAddress virtualAddress,
      std::size_t width,
      IOPWord value);
    IOPMemoryWriteResult writeStore(
      const IOPStoreRequest &request);
    IOPMemoryWriteResult writeMaskedStore(
      const IOPStoreRequest &request);

    IOPBus *bus = nullptr;
    IOPCache caches;
    std::shared_ptr<const BootROMImage> cacheRomIdentity;
    IOPAddress pc = IOPReset::VECTOR;
    std::array<IOPWord, GENERAL_REGISTER_COUNT - 1>
      writableGeneralRegisters = {};
    IOPWord hiRegister = 0;
    IOPWord loRegister = 0;
    IOPHILOState hiLoState;
    IOPCOP0State cop0;
    IOPBranchContinuation branch;
    IOPDelayedResultContinuation delayedResult;
    IOPExceptionRecord enteredException;
    IOPStoreFailure rejectedStore;
    IOPExecutionState state = IOPExecutionState::Halted;
    IOPStopReason haltReason = IOPStopReason::None;
    IOPCycleCount cycles = 0;
    std::uint64_t retiredInstructionTotal = 0;
};

#endif
