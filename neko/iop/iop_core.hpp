#ifndef IOP_CORE_HPP
#define IOP_CORE_HPP

#include <array>
#include <cstddef>
#include <memory>

#include "iop_cache.hpp"
#include "observation_order.hpp"
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

enum class IOPBranchOutcome : std::uint8_t
{
  NotTaken,
  Taken
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
  IOPBranchOutcome branchOutcome = IOPBranchOutcome::Taken;
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

    // Core-local initialization, not an in-system guest hardware reset.
    void reset();
    void attachBus(IOPBus *bus);
    bool hasAttachedBus() const;
    void prepareFreshExecution(
      IOPAddress entryPoint,
      IOPAddress stackPointer,
      IOPAddress returnAddress);
    // Restarts continuation even at the current PC; use resumeExecution to resume.
    void startExecution(IOPAddress entryPoint);
    void haltExecution();
    // Requires a host halt without a rejected store; preserves continuation.
    void resumeExecution();
    void stepInstruction();
    // Direct pin levels are independent of the bus controller's Hardware0 level.
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
    friend class NekoSystem;
    friend class NekoSaveStateCodec;
    friend struct IOPCoreTestAccess;
    friend struct IOPCacheTestAccess;
    friend struct IOPObservationTestAccess;

    enum class StepEventKind : std::uint8_t
    {
      InstructionIssued,
      InstructionRetired,
      BranchScheduled,
      MemoryAccess,
      ExceptionEntered,
      InterruptDelivered,
      StateChanged
    };

    enum class MemoryAccessKind : std::uint8_t
    {
      InstructionFetch,
      DataLoad,
      DataStore
    };

    struct InstructionIssuedEvent
    {
      IOPAddress address = 0;
      IOPWord instruction = 0;
      bool inBranchDelaySlot = false;
    };

    struct InstructionRetiredEvent
    {
      IOPAddress address = 0;
      IOPAddress nextAddress = 0;
      std::uint64_t instructions = 0;
      IOPCycleCount cycles = 0;
    };

    struct BranchScheduledEvent
    {
      IOPAddress address = 0;
      IOPAddress target = 0;
      IOPBranchOutcome outcome = IOPBranchOutcome::NotTaken;
    };

    struct MemoryAccessEvent
    {
      MemoryAccessKind kind = MemoryAccessKind::InstructionFetch;
      IOPAddress virtualAddress = 0;
      IOPAddress physicalAddress = 0;
      IOPWord value = 0;
      IOPAccessOutcome outcome = IOPAccessOutcome::Unmapped;
      IOPCacheRoute cacheRoute = IOPCacheRoute::None;
      std::uint8_t width = 0;
      std::uint8_t byteEnable = 0;
      bool physicalAddressValid = false;
    };

    struct ExceptionEnteredEvent
    {
      IOPAddress epc = 0;
      IOPAddress vector = 0;
      IOPWord cause = 0;
    };

    struct InterruptDeliveredEvent
    {
      IOPAddress address = 0;
      IOPWord status = 0;
      IOPWord cause = 0;
      IOPAddress vector = 0;
    };

    struct StateChangedEvent
    {
      IOPExecutionState before = IOPExecutionState::Halted;
      IOPExecutionState after = IOPExecutionState::Halted;
      IOPStopReason reason = IOPStopReason::None;
      IOPAddress address = 0;
    };

    union StepEventPayload
    {
      InstructionIssuedEvent instructionIssued;
      InstructionRetiredEvent instructionRetired;
      BranchScheduledEvent branchScheduled;
      MemoryAccessEvent memoryAccess;
      ExceptionEnteredEvent exceptionEntered;
      InterruptDeliveredEvent interruptDelivered;
      StateChangedEvent stateChanged;

      StepEventPayload(const InstructionIssuedEvent &event) :
        instructionIssued(event) {}
      StepEventPayload(const InstructionRetiredEvent &event) :
        instructionRetired(event) {}
      StepEventPayload(const BranchScheduledEvent &event) :
        branchScheduled(event) {}
      StepEventPayload(const MemoryAccessEvent &event) :
        memoryAccess(event) {}
      StepEventPayload(const ExceptionEnteredEvent &event) :
        exceptionEntered(event) {}
      StepEventPayload(const InterruptDeliveredEvent &event) :
        interruptDelivered(event) {}
      StepEventPayload(const StateChangedEvent &event) :
        stateChanged(event) {}
    };

    struct StepEvent
    {
      StepEvent() :
        kind(StepEventKind::InstructionIssued),
        payload(InstructionIssuedEvent{}) {}
      StepEvent(const InstructionIssuedEvent &event) :
        kind(StepEventKind::InstructionIssued), payload(event) {}
      StepEvent(const InstructionRetiredEvent &event) :
        kind(StepEventKind::InstructionRetired), payload(event) {}
      StepEvent(const BranchScheduledEvent &event) :
        kind(StepEventKind::BranchScheduled), payload(event) {}
      StepEvent(const MemoryAccessEvent &event) :
        kind(StepEventKind::MemoryAccess), payload(event) {}
      StepEvent(const ExceptionEnteredEvent &event) :
        kind(StepEventKind::ExceptionEntered), payload(event) {}
      StepEvent(const InterruptDeliveredEvent &event) :
        kind(StepEventKind::InterruptDelivered), payload(event) {}
      StepEvent(const StateChangedEvent &event) :
        kind(StepEventKind::StateChanged), payload(event) {}

      StepEventKind kind;
      StepEventPayload payload;
      std::size_t sequence = 0;
    };

    // Fetch + issue + data access/branch + retirement/exception/halt.
    // Interrupt admission has no fetch or issue and produces only two events.
    static constexpr std::size_t STEP_EVENT_CAPACITY = 4;
    std::array<StepEvent, STEP_EVENT_CAPACITY> stepEvents = {};
    std::size_t stepEventCount = 0;
    bool observationsEnabled = false;
    NekoObservationOrder *observationOrder = nullptr;

    void enableObservations(NekoObservationOrder *order = nullptr);
    void disableObservations();
    void clearObservations();
    void recordStepEvent(const StepEvent &event);
    void recordMemoryRead(
      const IOPMemoryReadResult &result,
      std::uint8_t width,
      MemoryAccessKind kind);
    void recordMemoryStore(
      const IOPStoreRequest &request,
      const IOPMemoryWriteResult &result);
    void transitionExecution(
      IOPExecutionState nextState,
      IOPStopReason reason);

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
    // Called on every retirement, including non-HI/LO instructions.
    void commitHILOEffects(const IOPInstructionEffects &effects);
    void synchronizeInterruptInputs();
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
    IOPAccessOutcome commitIsolatedStore(
      const IOPCacheAccessPlan &plan,
      IOPAddress physicalWord,
      IOPWord value,
      std::uint8_t byteEnable);
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
    IOPWord directInterruptInputs = 0;
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
