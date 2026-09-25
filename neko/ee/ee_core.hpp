#ifndef EE_CORE_HPP
#define EE_CORE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "clocked_component.hpp"
#include "ee_instruction.hpp"

class EEBus;
class VPU;
struct EECoreTestAccess;

struct EERegister128
{
  std::uint64_t low = 0;
  std::uint64_t high = 0;
};

constexpr bool operator==(
  const EERegister128 &left,
  const EERegister128 &right)
{
  return
    left.low == right.low &&
    left.high == right.high;
}

constexpr bool operator!=(
  const EERegister128 &left,
  const EERegister128 &right)
{
  return !(left == right);
}

enum class EEException : std::uint8_t
{
  None,
  AddressErrorLoadOrFetch,
  InstructionBusError,
  ArithmeticOverflow,
  DataBusErrorLoad,
  DataBusErrorStore,
  AddressErrorStore,
  Interrupt,
  ReservedInstruction,
  SystemCall,
  Breakpoint,
  CoprocessorUnusable
};

struct EEInstructionFetchResult
{
  bool succeeded = false;
  std::uint32_t address = 0;
  std::uint32_t instruction = 0;
};

enum class EEAcceptanceMode : std::uint8_t
{
  Ordinary,
  DelaySlot
};

struct EEAcceptanceRecord
{
  std::uint64_t programOrder = 0;
  std::uint32_t address = 0;
  EEInstruction instruction;
  EEAcceptanceMode mode = EEAcceptanceMode::Ordinary;
};

class EEAcceptanceRecords
{
  public:
    static constexpr std::size_t CAPACITY = 2;

    void clear();
    void append(const EEAcceptanceRecord &record);
    std::size_t size() const;
    std::uint64_t instructionCount() const;
    const EEAcceptanceRecord &operator[](
      std::size_t index) const;

  private:
    std::array<EEAcceptanceRecord, CAPACITY> records = {};
    std::size_t count = 0;
};

class EEShiftAmountOrderingWindow
{
  public:
    static bool historyBitsValid(
      std::uint8_t accesses,
      std::uint8_t reads);
    static bool readHistoryConsistent(
      std::uint8_t accesses,
      std::uint8_t reads);
    void clear();
    bool permits(EEOperation operation) const;
    void accept(EEOperation operation);
    std::uint8_t accessHistory() const;
    std::uint8_t readHistory() const;
    void restore(
      std::uint8_t accesses,
      std::uint8_t reads);

  private:
    std::uint8_t recentAccesses = 0;
    std::uint8_t recentReads = 0;
};

enum class EEIssueMemberOutcome : std::uint8_t
{
  Accepted,
  Stalled,
  Faulted,
  Cancelled
};

enum class EEIssueMemberPosition : std::uint8_t
{
  Older,
  Younger
};

enum class EEIssueWidth : std::uint8_t
{
  One = 1,
  Two = 2
};

enum class EEInstructionExecutionOutcome : std::uint8_t
{
  Completed,
  Delayed,
  Faulted,
  Halted,
  Rejected
};

struct EEIssueGroupExecutionResult
{
  EEIssueWidth width = EEIssueWidth::One;
  std::uint8_t attempted = 0;
  std::uint8_t accepted = 0;
  EEIssueMemberOutcome older =
    EEIssueMemberOutcome::Cancelled;
  EEIssueMemberOutcome younger =
    EEIssueMemberOutcome::Cancelled;

  EEIssueMemberOutcome outcome(
    EEIssueMemberPosition position) const
  {
    return position == EEIssueMemberPosition::Older
      ? older
      : younger;
  }
};

template <typename AttemptMember>
EEIssueGroupExecutionResult executeEEIssueGroupMembers(
  EEIssueWidth width,
  AttemptMember &&attemptMember)
{
  std::uint8_t memberCount = 0;
  switch (width)
  {
    case EEIssueWidth::One:
      memberCount = 1;
      break;
    case EEIssueWidth::Two:
      memberCount = 2;
      break;
    default:
      throw std::invalid_argument(
        "EE issue group width is invalid.");
  }

  EEIssueGroupExecutionResult result;
  result.width = width;
  for (std::uint8_t member = 0;
       member < memberCount;
       ++member)
  {
    ++result.attempted;
    const EEIssueMemberPosition position =
      member == 0
        ? EEIssueMemberPosition::Older
        : EEIssueMemberPosition::Younger;
    const EEIssueMemberOutcome outcome =
      attemptMember(position);
    if (position == EEIssueMemberPosition::Older)
    {
      result.older = outcome;
    }
    else
    {
      result.younger = outcome;
    }
    if (outcome != EEIssueMemberOutcome::Accepted)
    {
      return result;
    }
    ++result.accepted;
  }
  return result;
}

enum class EEExecutionState : std::uint8_t
{
  Halted,
  Running
};

enum class EEStopReason : std::uint8_t
{
  None,
  HostHalt,
  FetchException,
  ReservedInstruction,
  UnsupportedInstruction,
  ExecutionException,
  UndefinedOperation
};

enum class EECOP0Register : std::uint8_t
{
  BadVAddr = 8,
  Count = 9,
  Compare = 11,
  Status = 12,
  Cause = 13,
  EPC = 14,
  ErrorEPC = 30
};

namespace EECOP0Status
{
  constexpr std::uint32_t RESET = UINT32_C(0x70400004);
  constexpr std::uint32_t INTERRUPT_ENABLE = UINT32_C(1);
  constexpr std::uint32_t EXCEPTION_LEVEL = UINT32_C(1) << 1;
  constexpr std::uint32_t ERROR_LEVEL = UINT32_C(1) << 2;
  constexpr std::uint32_t INTC_MASK = UINT32_C(1) << 10;
  constexpr std::uint32_t DMAC_MASK = UINT32_C(1) << 11;
  constexpr std::uint32_t MASTER_INTERRUPT_ENABLE =
    UINT32_C(1) << 16;
  constexpr std::uint32_t BOOTSTRAP_EXCEPTION_VECTOR =
    UINT32_C(1) << 22;
  constexpr std::uint32_t COP1_USABLE = UINT32_C(1) << 29;
}

namespace EECOP0Cause
{
  constexpr std::uint32_t EXCEPTION_CODE_MASK =
    UINT32_C(0x1f) << 2;
  constexpr std::uint32_t INTC_PENDING = UINT32_C(1) << 10;
  constexpr std::uint32_t DMAC_PENDING = UINT32_C(1) << 11;
  constexpr std::uint32_t BRANCH_DELAY = UINT32_C(1) << 31;
  constexpr std::uint32_t COPROCESSOR_ERROR_MASK =
    UINT32_C(0x3) << 28;
  constexpr std::uint32_t COPROCESSOR_1 =
    UINT32_C(1) << 28;
}

namespace EECOP1Control
{
  constexpr std::size_t IMPLEMENTATION_REVISION_REGISTER = 0;
  constexpr std::size_t STATUS_REGISTER = 31;
  constexpr std::uint32_t IMPLEMENTATION_REVISION =
    UINT32_C(0x00002e00);
  constexpr std::uint32_t STATUS_FIXED =
    UINT32_C(0x01000001);
  constexpr std::uint32_t STATUS_WRITABLE_MASK =
    UINT32_C(0x0083c078);
  constexpr std::uint32_t CONDITION =
    UINT32_C(1) << 23;
  constexpr std::uint32_t CAUSE_INVALID =
    UINT32_C(1) << 17;
  constexpr std::uint32_t CAUSE_DIVISION_BY_ZERO =
    UINT32_C(1) << 16;
  constexpr std::uint32_t CAUSE_OVERFLOW =
    UINT32_C(1) << 15;
  constexpr std::uint32_t CAUSE_UNDERFLOW =
    UINT32_C(1) << 14;
  constexpr std::uint32_t STICKY_INVALID =
    UINT32_C(1) << 6;
  constexpr std::uint32_t STICKY_DIVISION_BY_ZERO =
    UINT32_C(1) << 5;
  constexpr std::uint32_t STICKY_OVERFLOW =
    UINT32_C(1) << 4;
  constexpr std::uint32_t STICKY_UNDERFLOW =
    UINT32_C(1) << 3;
  constexpr std::uint32_t CAUSE_MASK =
    CAUSE_INVALID |
    CAUSE_DIVISION_BY_ZERO |
    CAUSE_OVERFLOW |
    CAUSE_UNDERFLOW;
  constexpr std::uint32_t STICKY_MASK =
    STICKY_INVALID |
    STICKY_DIVISION_BY_ZERO |
    STICKY_OVERFLOW |
    STICKY_UNDERFLOW;
}

namespace EEExceptionCode
{
  constexpr std::uint8_t INTERRUPT = 0;
  constexpr std::uint8_t ADDRESS_ERROR_LOAD_OR_FETCH = 4;
  constexpr std::uint8_t ADDRESS_ERROR_STORE = 5;
  constexpr std::uint8_t INSTRUCTION_BUS_ERROR = 6;
  constexpr std::uint8_t DATA_BUS_ERROR = 7;
  constexpr std::uint8_t SYSTEM_CALL = 8;
  constexpr std::uint8_t BREAKPOINT = 9;
  constexpr std::uint8_t RESERVED_INSTRUCTION = 10;
  constexpr std::uint8_t COPROCESSOR_UNUSABLE = 11;
  constexpr std::uint8_t ARITHMETIC_OVERFLOW = 12;
}

namespace EEExceptionVector
{
  constexpr std::uint32_t GENERAL = UINT32_C(0x80000180);
  constexpr std::uint32_t INTERRUPT = UINT32_C(0x80000200);
  constexpr std::uint32_t BOOTSTRAP_GENERAL =
    UINT32_C(0xbfc00380);
  constexpr std::uint32_t BOOTSTRAP_INTERRUPT =
    UINT32_C(0xbfc00400);
}

namespace EEReset
{
  constexpr std::uint32_t VECTOR = UINT32_C(0xbfc00000);
}

class EECore final : public ClockedComponent
{
  public:
    static constexpr std::size_t GENERAL_REGISTER_COUNT = 32;
    static constexpr std::size_t FLOATING_POINT_REGISTER_COUNT = 32;

    void reset();
    void attachBus(EEBus *bus);
    void attachVU0(VPU *vu0);
    void attachVU1(VPU *vu1);
    EEInstructionFetchResult fetchInstruction();
    void startExecution(std::uint32_t startAddress);
    void haltExecution();
    void enterInterruptException();
    bool clockActive() const override;
    void clock() override;
    EEExecutionState executionState() const;
    EEStopReason stopReason() const;
    std::uint64_t elapsedCycles() const;
    std::uint64_t stateHash() const;
    bool hasLastInstruction() const;
    std::uint32_t lastInstructionAddress() const;
    const EEInstruction &lastInstruction() const;
    const EEIssueSelection &lastIssueSelection() const;
    const EEAcceptanceRecords &
      acceptanceRecordsThisCycle() const;
    std::uint32_t rejectedInstruction() const;

    const EERegister128 &generalRegister(
      std::size_t index) const;
    void setGeneralRegister(
      std::size_t index,
      const EERegister128 &value);

    std::uint32_t floatingPointRegister(
      std::size_t index) const;
    void setFloatingPointRegister(
      std::size_t index,
      std::uint32_t value);
    std::uint32_t floatingPointAccumulator() const;
    void setFloatingPointAccumulator(std::uint32_t value);
    std::uint32_t cop1ControlRegister(
      std::size_t index) const;
    bool cop1Condition() const;
    void setCOP1ControlRegister(
      std::size_t index,
      std::uint32_t value);
    void updateCOP1ArithmeticFlags(
      std::uint8_t affectedFlags,
      std::uint8_t raisedFlags,
      std::uint8_t raisedStickyFlags = 0);

    std::uint32_t programCounter() const;
    void setProgramCounter(std::uint32_t value);

    std::uint64_t hi() const;
    void setHI(std::uint64_t value);
    std::uint64_t lo() const;
    void setLO(std::uint64_t value);
    std::uint64_t hi1() const;
    void setHI1(std::uint64_t value);
    std::uint64_t lo1() const;
    void setLO1(std::uint64_t value);

    std::uint32_t shiftAmount() const;
    void setShiftAmount(std::uint32_t value);

    std::uint32_t cop0Register(
      EECOP0Register registerIndex) const;
    void setCOP0Register(
      EECOP0Register registerIndex,
      std::uint32_t value);

    bool exceptionPending() const;
    EEException pendingException() const;
    std::uint32_t exceptionAddress() const;
    void clearPendingException();

  private:
    friend class NekoSystem;
    friend class NekoSaveStateCodec;
    friend struct EECoreTestAccess;

    enum class CycleEventKind : std::uint8_t
    {
      InstructionIssued,
      BranchScheduled,
      MemoryAccess,
      ExceptionEntered,
      InterruptDelivered,
      COP1LoadInterlock,
      COP1ResourceInterlock,
      COP1DividerHazard,
      COP1StageTransition,
      COP1Retired
    };

    enum class COP1Dependency : std::uint8_t
    {
      None = 0,
      Read = 1 << 0,
      Write = 1 << 1,
      ReadWrite = Read | Write
    };

    enum class COP1ScoreboardResource : std::uint8_t
    {
      FPR,
      Accumulator,
      FCR31,
      Condition,
      GPR,
      MemoryException,
      Divider
    };

    enum class COP1ScoreboardAvailability : std::uint8_t
    {
      Committed,
      BypassReady,
      Unavailable
    };

    enum class COP1PipelineStage : std::uint8_t
    {
      R,
      T,
      X,
      Y,
      Z,
      S1,
      S2
    };

    enum class COP1CompletionReason : std::uint8_t
    {
      PipelineAdvance,
      Drain
    };

    enum class COP1CancellationScope : std::uint8_t
    {
      All,
      AtOrAfter,
      After
    };

    enum class COP1OperationAdvanceOutcome : std::uint8_t
    {
      Unchanged,
      Advanced,
      Completed,
      Faulted
    };

    enum class IssueLatchFailure : std::uint8_t
    {
      None,
      AddressError,
      BusError,
      ReservedInstruction,
      UnsupportedInstruction
    };

    enum class FrontEndFetchFailure : std::uint8_t
    {
      None,
      AddressError,
      BusError
    };

    enum class ExecutionStartMode : std::uint8_t
    {
      ResumeHostContinuation,
      Restart
    };

    enum class MACPipeline : std::uint8_t
    {
      MAC0,
      MAC1
    };

    enum class MACResultDestination : std::uint8_t
    {
      HIAndLO,
      HIAndLOAndGPR
    };

    enum class PackedMACOperation : std::uint8_t
    {
      None,
      MultiplyWord,
      MultiplyUnsignedWord,
      MultiplyAddWord,
      MultiplyAddUnsignedWord,
      MultiplySubtractWord,
      MultiplyHalfword,
      MultiplyAddHalfword,
      MultiplySubtractHalfword,
      HorizontalMultiplyAddHalfword,
      HorizontalMultiplySubtractHalfword
    };

    enum class PackedDivideOperation : std::uint8_t
    {
      None,
      DivideWord,
      DivideUnsignedWord
    };

    enum class COP1ScoreboardQuery : std::uint8_t
    {
      CandidateReadiness,
      YoungerIssueGroupMember
    };

    enum class MemoryAccessDirection : std::uint8_t
    {
      Read,
      Write
    };

    enum class MemoryAccessOutcome : std::uint8_t
    {
      Failed,
      Succeeded
    };

    enum class BranchOutcome : std::uint8_t
    {
      NotTaken,
      Taken
    };

    enum class BranchMode : std::uint8_t
    {
      Ordinary,
      Likely
    };

    enum class ExceptionRestartMode : std::uint8_t
    {
      Instruction,
      BranchDelaySlot
    };

    enum class ExceptionCoprocessor : std::uint8_t
    {
      None,
      COP1
    };

    enum COP1Destination : std::uint8_t
    {
      COP1_DESTINATION_NONE = 0,
      COP1_DESTINATION_FPR = 1 << 0,
      COP1_DESTINATION_ACCUMULATOR = 1 << 1,
      COP1_DESTINATION_FCR31 = 1 << 2,
      COP1_DESTINATION_CONDITION = 1 << 3,
      COP1_DESTINATION_GPR = 1 << 4,
      COP1_DESTINATION_MEMORY = 1 << 5
    };

    struct InstructionIssuedEvent
    {
      std::uint32_t address;
      std::uint32_t instruction;
      EEOperation operation;
      EEAcceptanceMode mode;
    };

    struct BranchScheduledEvent
    {
      std::uint32_t address;
      std::uint32_t target;
      BranchOutcome outcome;
      BranchMode mode;
    };

    struct MemoryAccessEvent
    {
      std::uint32_t address;
      std::uint8_t width;
      MemoryAccessDirection direction;
      MemoryAccessOutcome outcome;
      std::uint64_t low;
      std::uint64_t high;
    };

    struct ExceptionEnteredEvent
    {
      EEException type;
      std::uint32_t address;
      std::uint32_t vector;
      std::uint32_t cause;
    };

    struct InterruptDeliveredEvent
    {
      std::uint32_t instructionAddress;
      std::uint32_t status;
      std::uint32_t cause;
      std::uint32_t vector;
    };

    struct COP1LoadInterlockEvent
    {
      std::uint32_t instructionAddress;
      std::uint32_t instruction;
      std::uint8_t registerIndex;
      COP1Dependency dependency;
    };

    struct COP1ResourceInterlockEvent
    {
      std::uint32_t instructionAddress;
      std::uint32_t instruction;
      std::uint64_t resource;
      COP1Dependency dependency;
    };

    struct COP1DividerHazardEvent
    {
      std::uint32_t instructionAddress;
      std::uint32_t instruction;
      std::uint64_t reasons;
      std::uint32_t branchAddress;
      std::uint32_t targetAddress;
    };

    struct COP1StageTransitionEvent
    {
      std::uint64_t programOrder;
      std::uint32_t instructionAddress;
      std::uint32_t instruction;
      std::uint8_t fromStage;
      COP1PipelineStage toStage;
      std::uint8_t remainingCycles;
      std::uint8_t destinationMask;
      std::uint8_t destinationFPR;
      std::uint8_t destinationGPR;
    };

    struct COP1RetiredEvent
    {
      std::uint64_t programOrder;
      std::uint32_t instructionAddress;
      std::uint32_t instruction;
      std::uint32_t rawResult;
      std::uint8_t destinationMask;
      std::uint8_t destinationFPR;
      std::uint8_t destinationGPR;
      std::uint8_t affectedFlags;
      std::uint8_t raisedFlags;
      std::uint8_t raisedStickyFlags;
      bool conditionResult;
    };

    struct ExceptionTransitionRequest
    {
      EEException type = EEException::None;
      std::uint32_t instructionAddress = 0;
      std::uint32_t faultAddress = 0;
      std::uint32_t instruction = 0;
      std::uint64_t programOrder = 0;
      ExceptionRestartMode restartMode =
        ExceptionRestartMode::Instruction;
      std::uint32_t branchAddress = 0;
      ExceptionCoprocessor coprocessor =
        ExceptionCoprocessor::None;
    };

    union CycleEventPayload
    {
      InstructionIssuedEvent instructionIssued;
      BranchScheduledEvent branchScheduled;
      MemoryAccessEvent memoryAccess;
      ExceptionEnteredEvent exceptionEntered;
      InterruptDeliveredEvent interruptDelivered;
      COP1LoadInterlockEvent cop1LoadInterlock;
      COP1ResourceInterlockEvent cop1ResourceInterlock;
      COP1DividerHazardEvent cop1DividerHazard;
      COP1StageTransitionEvent cop1StageTransition;
      COP1RetiredEvent cop1Retired;

      CycleEventPayload(
        const InstructionIssuedEvent &event) :
        instructionIssued(event)
      {
      }

      CycleEventPayload(
        const BranchScheduledEvent &event) :
        branchScheduled(event)
      {
      }

      CycleEventPayload(
        const MemoryAccessEvent &event) :
        memoryAccess(event)
      {
      }

      CycleEventPayload(
        const ExceptionEnteredEvent &event) :
        exceptionEntered(event)
      {
      }

      CycleEventPayload(
        const InterruptDeliveredEvent &event) :
        interruptDelivered(event)
      {
      }

      CycleEventPayload(
        const COP1LoadInterlockEvent &event) :
        cop1LoadInterlock(event)
      {
      }

      CycleEventPayload(
        const COP1ResourceInterlockEvent &event) :
        cop1ResourceInterlock(event)
      {
      }

      CycleEventPayload(
        const COP1DividerHazardEvent &event) :
        cop1DividerHazard(event)
      {
      }

      CycleEventPayload(
        const COP1StageTransitionEvent &event) :
        cop1StageTransition(event)
      {
      }

      CycleEventPayload(
        const COP1RetiredEvent &event) :
        cop1Retired(event)
      {
      }
    };

    struct CycleEvent
    {
      CycleEvent() :
        kind(CycleEventKind::InstructionIssued),
        payload(InstructionIssuedEvent{
          0,
          0,
          EEOperation::Nop,
          EEAcceptanceMode::Ordinary})
      {
      }

      CycleEvent(
        const InstructionIssuedEvent &event) :
        kind(CycleEventKind::InstructionIssued),
        payload(event)
      {
      }

      CycleEvent(
        const BranchScheduledEvent &event) :
        kind(CycleEventKind::BranchScheduled),
        payload(event)
      {
      }

      CycleEvent(
        const MemoryAccessEvent &event) :
        kind(CycleEventKind::MemoryAccess),
        payload(event)
      {
      }

      CycleEvent(
        const ExceptionEnteredEvent &event) :
        kind(CycleEventKind::ExceptionEntered),
        payload(event)
      {
      }

      CycleEvent(
        const InterruptDeliveredEvent &event) :
        kind(CycleEventKind::InterruptDelivered),
        payload(event)
      {
      }

      CycleEvent(
        const COP1LoadInterlockEvent &event) :
        kind(CycleEventKind::COP1LoadInterlock),
        payload(event)
      {
      }

      CycleEvent(
        const COP1ResourceInterlockEvent &event) :
        kind(CycleEventKind::COP1ResourceInterlock),
        payload(event)
      {
      }

      CycleEvent(
        const COP1DividerHazardEvent &event) :
        kind(CycleEventKind::COP1DividerHazard),
        payload(event)
      {
      }

      CycleEvent(
        const COP1StageTransitionEvent &event) :
        kind(CycleEventKind::COP1StageTransition),
        payload(event)
      {
      }

      CycleEvent(
        const COP1RetiredEvent &event) :
        kind(CycleEventKind::COP1Retired),
        payload(event)
      {
      }

      CycleEventKind kind;
      CycleEventPayload payload;
    };

    struct DecodedIssueLatch
    {
      bool valid = false;
      std::uint32_t address = 0;
      EEInstruction instruction;
      IssueLatchFailure failure = IssueLatchFailure::None;
    };

    struct YoungerAStageContinuation
    {
      bool active = false;
      std::uint64_t programOrder = 0;
      std::uint32_t address = 0;
      EEInstruction instruction;
    };

    struct FrontEndFetchResult
    {
      std::uint32_t address = 0;
      std::uint32_t instruction = 0;
      FrontEndFetchFailure failure =
        FrontEndFetchFailure::None;
    };

    struct IssueCandidates
    {
      const DecodedIssueLatch *older = nullptr;
      const DecodedIssueLatch *younger = nullptr;

      std::uint8_t count() const
      {
        return older == nullptr
          ? 0
          : static_cast<std::uint8_t>(
              younger == nullptr ? 1 : 2);
      }
    };

    struct IssueCandidateReadiness
    {
      bool older = false;
      bool younger = false;
      std::size_t availableCOP1Slots = 0;
    };

    struct COP1DestinationMetadata
    {
      std::uint8_t mask = COP1_DESTINATION_NONE;
      std::uint8_t fprRegister = 0;
      std::uint8_t gprRegister = 0;
    };

    static constexpr std::size_t COP1_IN_FLIGHT_CAPACITY = 16;

    struct InFlightCOP1Operation
    {
      bool active = false;
      std::uint64_t programOrder = 0;
      COP1PipelineStage stage = COP1PipelineStage::R;
      std::uint32_t instructionAddress = 0;
      EEInstruction instruction;
      std::uint32_t capturedFS = 0;
      std::uint32_t capturedFT = 0;
      std::uint32_t capturedAccumulator = 0;
      std::uint32_t capturedControl = 0;
      bool branchDelaySlot = false;
      std::uint32_t branchAddress = 0;
      std::uint64_t capturedGPR = 0;
      std::uint32_t memoryAddress = 0;
      std::uint32_t capturedMemoryValue = 0;
      COP1DestinationMetadata destination;
      std::uint32_t rawResult = 0;
      std::uint8_t affectedFlags = 0;
      std::uint8_t raisedFlags = 0;
      std::uint8_t raisedStickyFlags = 0;
      bool conditionResult = false;
      std::uint8_t remainingCycles = 0;
    };

    struct COP1ProgramOrderView
    {
      std::size_t size() const
      {
        return count;
      }

      std::size_t operator[](std::size_t index) const
      {
        return slotIndices[index];
      }

      std::array<
        std::size_t,
        COP1_IN_FLIGHT_CAPACITY> slotIndices = {};
      std::size_t count = 0;
    };

    struct COP1AdvanceEligibility
    {
      std::array<
        const InFlightCOP1Operation *,
        COP1_IN_FLIGHT_CAPACITY> moveTStageBlockers = {};
    };

    struct COP1AdvanceResult
    {
      std::array<
        bool,
        COP1_IN_FLIGHT_CAPACITY> transitioned = {};
      std::array<
        COP1PipelineStage,
        COP1_IN_FLIGHT_CAPACITY> previousStages = {};
      bool exceptionEntered = false;
    };

    struct COP1OperationAdvanceResult
    {
      COP1OperationAdvanceOutcome outcome =
        COP1OperationAdvanceOutcome::Unchanged;
      COP1PipelineStage previousStage =
        COP1PipelineStage::R;
    };

    struct COP1RetirementResult
    {
      std::uint32_t completedLoadRegisters = 0;
    };

    struct COP1DividerOccupancy
    {
      std::uint8_t initiationCycles = 0;
      EEOperation operation = EEOperation::Nop;
    };

    struct COP1ScoreboardValue
    {
      COP1ScoreboardAvailability availability =
        COP1ScoreboardAvailability::Committed;
      std::uint32_t value = 0;
      std::uint64_t producerOrder = 0;
      EEOperation producerOperation = EEOperation::Nop;
      COP1PipelineStage producerStage =
        COP1PipelineStage::R;
    };

    struct COP1ScoreboardHazard
    {
      COP1ScoreboardResource resource =
        COP1ScoreboardResource::FPR;
      std::uint8_t registerIndex = 0;
      COP1Dependency dependency = COP1Dependency::None;
      bool completedLoad = false;
      EEOperation blockingOperation = EEOperation::Nop;
    };

    struct PendingMultiplyDivide
    {
      bool active = false;
      std::uint8_t remainingCycles = 0;
      std::uint64_t hiResult = 0;
      std::uint64_t loResult = 0;
      MACResultDestination resultDestination =
        MACResultDestination::HIAndLO;
      std::uint8_t generalRegister = 0;
      std::uint64_t generalRegisterResult = 0;
    };

    struct InFlightPackedMACOperation
    {
      bool active = false;
      PackedMACOperation operation =
        PackedMACOperation::None;
      std::uint64_t programOrder = 0;
      EERegister128 source;
      EERegister128 target;
      EERegister128 hiResult;
      EERegister128 loResult;
      std::uint8_t destinationRegister = 0;
      EERegister128 generalRegisterResult;
      std::uint8_t remainingCycles = 0;
    };

    struct PackedMACAccumulatorState
    {
      EERegister128 hi;
      EERegister128 lo;
    };

    struct PackedMACContinuation
    {
      static constexpr std::size_t CAPACITY = 2;

      std::array<
        InFlightPackedMACOperation,
        CAPACITY> operations = {};
      std::uint8_t initiationCycles = 0;
    };

    struct PackedDivideContinuation
    {
      bool active = false;
      PackedDivideOperation operation =
        PackedDivideOperation::None;
      std::uint64_t programOrder = 0;
      EERegister128 source;
      EERegister128 target;
      EERegister128 hiResult;
      EERegister128 loResult;
      std::uint8_t remainingCycles = 0;
    };

    struct PackedMACProgramOrderView
    {
      std::size_t size() const
      {
        return count;
      }

      std::size_t operator[](std::size_t index) const
      {
        return slotIndices[index];
      }

      std::array<
        std::size_t,
        PackedMACContinuation::CAPACITY> slotIndices = {};
      std::size_t count = 0;
    };

    std::array<EERegister128, GENERAL_REGISTER_COUNT>
      generalRegisters = {};
    std::array<
      std::uint32_t,
      FLOATING_POINT_REGISTER_COUNT>
      floatingPointRegisters = {};
    std::uint32_t floatingPointAccumulatorRegister = 0;
    std::uint32_t cop1StatusRegister = 0;
    std::uint32_t pc = EEReset::VECTOR;
    std::uint64_t hiRegister = 0;
    std::uint64_t loRegister = 0;
    std::uint64_t hi1Register = 0;
    std::uint64_t lo1Register = 0;
    std::uint32_t saRegister = 0;
    std::uint32_t cop0BadVAddr = 0;
    std::uint32_t cop0Count = 0;
    std::uint32_t cop0Compare = 0;
    std::uint32_t cop0Status = EECOP0Status::RESET;
    std::uint32_t cop0Cause = 0;
    std::uint32_t cop0EPC = 0;
    std::uint32_t cop0ErrorEPC = 0;
    EEBus *bus = nullptr;
    VPU *vu0 = nullptr;
    VPU *vu1 = nullptr;
    EEException exception = EEException::None;
    std::uint32_t faultAddress = 0;
    EEExecutionState state = EEExecutionState::Halted;
    EEStopReason haltReason = EEStopReason::None;
    std::uint64_t cycles = 0;
    bool lastInstructionValid = false;
    std::uint32_t lastAddress = 0;
    EEInstruction lastDecodedInstruction;
    EEIssueSelection issueSelection;
    std::uint32_t rejectedInstructionValue = 0;
    DecodedIssueLatch issueLatch;
    DecodedIssueLatch stagingLatch;
    YoungerAStageContinuation youngerAStageContinuation;
    std::array<
      InFlightCOP1Operation,
      COP1_IN_FLIGHT_CAPACITY> inFlightCOP1Operations = {};
    std::uint64_t nextEEProgramOrder = 1;
    std::uint64_t executingProgramOrder = 0;
    PendingMultiplyDivide pendingMac0;
    PendingMultiplyDivide pendingMac1;
    PackedMACContinuation packedMACContinuation;
    PackedDivideContinuation packedDivideContinuation;
    std::uint8_t cop1DividerInitiationCycles = 0;
    EEOperation cop1DividerOperation = EEOperation::Nop;
    EEShiftAmountOrderingWindow shiftAmountOrdering;
    bool branchDelayPending = false;
    std::uint32_t branchDelayTarget = 0;
    std::uint32_t branchInstructionAddress = 0;
    bool branchDelayFromLikely = false;
    bool branchDelayTaken = false;
    std::uint8_t cop1DividerPostDelayInstructions = 0;
    std::uint32_t cop1DividerPostDelayBranchAddress = 0;
    std::uint32_t cop1DividerPostDelayTargetAddress = 0;
    bool cop1DividerPostDelayTaken = false;
    std::uint8_t cop1DividerPostTargetInstructions = 0;
    std::uint32_t cop1DividerPostTargetAddress = 0;
    EEAcceptanceRecords acceptanceRecords;
    bool exceptionEnteredThisCycle = false;
    static constexpr std::size_t
      MAX_COP1_CYCLE_EVENTS =
        COP1_IN_FLIGHT_CAPACITY * 2;
    static constexpr std::size_t
      MAX_FRONT_END_CYCLE_EVENTS = 16;
    static constexpr std::size_t CYCLE_EVENT_CAPACITY =
      MAX_COP1_CYCLE_EVENTS + MAX_FRONT_END_CYCLE_EVENTS;
    static_assert(
      MAX_FRONT_END_CYCLE_EVENTS >= 8,
      "EE cycle event capacity must hold two issued "
      "instructions, branch or memory effects, divider "
      "diagnostics, and interrupt or exception entry.");
    std::array<CycleEvent, CYCLE_EVENT_CAPACITY>
      cycleEvents = {};
    std::size_t cycleEventCount = 0;

    static void requireGeneralRegisterIndex(
      std::size_t index);
    static void requireFloatingPointRegisterIndex(
      std::size_t index);
    void prepareFreshExecution(
      std::uint32_t entryPoint,
      std::uint32_t stackPointer,
      std::uint32_t returnAddress);
    EEBus &attachedBus() const;
    VPU &attachedVU0() const;
    VPU &attachedVU1() const;
    bool readCOP2ControlRegister(
      std::uint8_t index,
      std::uint32_t *value) const;
    bool writeCOP2ControlRegister(
      std::uint8_t index,
      std::uint32_t value);
    std::uint32_t vpuStatusRegister() const;
    EEInstructionFetchResult raiseFetchException(
      EEException type,
      std::uint32_t address);
    ExceptionTransitionRequest makeExceptionTransitionRequest(
      EEException type,
      std::uint32_t instructionAddress,
      std::uint32_t faultAddress,
      std::uint32_t instruction,
      ExceptionCoprocessor coprocessor =
        ExceptionCoprocessor::None) const;
    void enterException(
      const ExceptionTransitionRequest &request);
    void cancelInFlightCOP1(
      COP1CancellationScope scope,
      std::uint64_t programOrder = 0);
    static std::uint8_t exceptionCode(EEException type);
    std::uint32_t exceptionVector(
      EEException type,
      bool alreadyExceptionLevel) const;
    void setInterruptLines(bool intc, bool dmac);
    bool interruptDeliverable() const;
    FrontEndFetchResult fetchIssueCandidate(
      std::uint32_t address) const;
    static DecodedIssueLatch decodeIssueCandidate(
      const FrontEndFetchResult &fetch);
    void ensureIssueLatch();
    void ensureStagingLatch();
    void fillIssueFrontEnd();
    void promoteStagingLatch();
    void clearIssueFrontEnd();
    void clearBranchDelayContinuation();
    void clearCOP1DividerBranchContext();
    ExecutionStartMode executionStartMode(
      std::uint32_t startAddress) const;
    void resetExecutionContinuation();
    bool frontEndContinuationActive() const;
    EEIssueMemberOutcome resolveIssueLatchFailure();
    EEIssueGroupExecutionResult executeIssueGroup(
      EEIssueWidth width,
      std::uint32_t completedLoadRegisters);
    EEIssueMemberOutcome executeIssueMember(
      std::uint32_t completedLoadRegisters,
      EEIssueMemberPosition position);
    EEIssueMemberOutcome acceptYoungerAStageContinuation();
    void executeYoungerAStageContinuation();
    void recordInstructionAcceptance(
      std::uint64_t programOrder,
      std::uint32_t address,
      const EEInstruction &instruction,
      EEAcceptanceMode mode);
    void applyInstructionAcceptanceEffects(
      const EEAcceptanceRecord &record);
    void updateIssueSelection(
      std::uint32_t completedLoadRegisters);
    IssueCandidates constructIssueCandidates() const;
    IssueCandidateReadiness evaluateIssueCandidateReadiness(
      const IssueCandidates &candidates,
      std::uint32_t completedLoadRegisters) const;
    EEIssueSelection selectReadyIssueCandidates(
      const IssueCandidates &candidates,
      const IssueCandidateReadiness &readiness) const;
    bool issueCandidateReady(
      const EEInstruction &instruction,
      std::uint32_t completedLoadRegisters,
      std::size_t availableCOP1Slots) const;
    bool pendingMultiplyDivideBlocks(
      const EEInstruction &instruction) const;
    bool cop1TransferReservedByStalledMove() const;
    bool cop1MemoryExceptionPending() const;
    bool issuePairStructurallySafe(
      const EEInstruction &older,
      const EEInstruction &younger,
      std::size_t availableCOP1Slots) const;
    bool issueSelectionCanExecuteConcurrently() const;
    bool issueSelectionUsesCompatiblePhysicalPipelines()
      const;
    static bool isActivatedOIssueOperation(
      EEOperation operation);
    bool memoryIssueCanJoinPair(
      const EEInstruction &instruction) const;
    bool branchLikelyTaken(
      const EEInstruction &instruction) const;
    bool cop2ScoreboardBlocks(
      const EEInstruction &instruction) const;
    EEInstructionExecutionOutcome executeInstruction(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeWordShift(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeDoublewordShift(
      const EEInstruction &instruction);
    EEInstructionExecutionOutcome executeRegisterLogical(
      const EEInstruction &instruction);
    EEInstructionExecutionOutcome executePackedLogical(
      const EEInstruction &instruction);
    EEInstructionExecutionOutcome executePackedArithmetic(
      const EEInstruction &instruction);
    EEInstructionExecutionOutcome executePackedRearrange(
      const EEInstruction &instruction);
    EEInstructionExecutionOutcome executePackedShift(
      const EEInstruction &instruction);
    EEInstructionExecutionOutcome executeFunnelShift(
      const EEInstruction &instruction);
    EEInstructionExecutionOutcome executePackedHILOTransfer(
      const EEInstruction &instruction);
    EEInstructionExecutionOutcome executePackedCompare(
      const EEInstruction &instruction);
    EEInstructionExecutionOutcome executePackedAbsolute(
      const EEInstruction &instruction);
    EEInstructionExecutionOutcome executePackedLeadingSignCount(
      const EEInstruction &instruction);
    EEInstructionExecutionOutcome executePackedMultiply(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executePackedDivide(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeRegisterCompare(
      const EEInstruction &instruction);
    EEInstructionExecutionOutcome executeImmediateCompare(
      const EEInstruction &instruction);
    EEInstructionExecutionOutcome executeImmediateLogical(
      const EEInstruction &instruction);
    EEInstructionExecutionOutcome executeWordArithmetic(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeDoublewordArithmetic(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeImmediateWordArithmetic(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome
      executeImmediateDoublewordArithmetic(
        const EEInstruction &instruction,
        std::uint32_t address);
    EEInstructionExecutionOutcome executeMACRegisterMove(
      const EEInstruction &instruction);
    EEInstructionExecutionOutcome executeShiftAmountOperation(
      const EEInstruction &instruction);
    EEInstructionExecutionOutcome executeByteMemory(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeHalfwordMemory(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeWordMemory(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeWordMergeMemory(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeDoublewordMemory(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeDoublewordMergeMemory(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeQuadwordMemory(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeExceptionReturn(
      const EEInstruction &instruction);
    EEInstructionExecutionOutcome executeSoftwareException(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeCOP1RegisterMove(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeCOP1Divider(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeCOP1StagedOperation(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeCOP1Branch(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeCOP1Memory(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeCOP2Memory(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeCOP2VectorMove(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeCOP2ControlMove(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeCOP2Branch(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeCOP2MicroCall(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeCOP2Macro(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeJump(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeIntegerBranch(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeMultiply(
      const EEInstruction &instruction,
      std::uint32_t address);
    EEInstructionExecutionOutcome executeDivide(
      const EEInstruction &instruction,
      std::uint32_t address);
    bool requireWordValue(
      std::uint8_t registerIndex,
      std::uint32_t address,
      std::uint32_t instruction);
    void writeLowDoubleword(
      std::uint8_t registerIndex,
      std::uint64_t value);
    void writeWord(
      std::uint8_t registerIndex,
      std::uint32_t value);
    EEInstructionExecutionOutcome raiseArithmeticOverflow(
      std::uint32_t address,
      std::uint32_t instruction);
    bool requireCOP1Usable(
      std::uint32_t address,
      std::uint32_t instruction);
    void setCOP1Condition(bool condition);
    void haltUndefinedOperation(
      std::uint32_t address,
      std::uint32_t instruction);
    bool pendingMACContinuationActive() const;
    bool multiplyDivideContinuationBlocks(
      const EEInstruction &instruction) const;
    bool packedMACContinuationActive() const;
    bool packedMACAdmissionAvailable() const;
    PackedMACProgramOrderView
      packedMACProgramOrder() const;
    static bool packedMACOperationStateValid(
      const InFlightPackedMACOperation &operation);
    bool packedMACContinuationStateValid() const;
    PackedMACAccumulatorState packedMACAccumulatorState() const;
    bool packedMACContinuationBlocks(
      const EEInstruction &instruction) const;
    bool packedMACBlocksScalarMAC(
      const EEInstruction &instruction) const;
    bool packedDivideContinuationBlocks(
      const EEInstruction &instruction) const;
    bool packedDivideContinuationStateValid() const;
    bool computePackedDivideResults(
      PackedDivideOperation operation,
      const EERegister128 &source,
      const EERegister128 &target,
      EERegister128 *hiResult,
      EERegister128 *loResult) const;
    void advancePackedDivideContinuation();
    void startPackedDivideOperation(
      PackedDivideOperation operation,
      const EERegister128 &source,
      const EERegister128 &target,
      const EERegister128 &hiResult,
      const EERegister128 &loResult);
    void advancePackedMACContinuation();
    void startPackedMACOperation(
      PackedMACOperation operation,
      const EERegister128 &source,
      const EERegister128 &target,
      const EERegister128 &hiResult,
      const EERegister128 &loResult,
      std::uint8_t destinationRegister,
      const EERegister128 &generalRegisterResult);
    void advancePendingMultiplyDivide(
      MACPipeline pipeline);
    void startPendingMultiplyDivide(
      MACPipeline pipeline,
      std::uint8_t latency,
      std::uint64_t hiResult,
      std::uint64_t loResult,
      std::uint8_t generalRegister,
      MACResultDestination resultDestination);
    InFlightCOP1Operation &allocateInFlightCOP1(
      const EEInstruction &instruction,
      std::uint32_t instructionAddress);
    COP1ProgramOrderView inFlightCOP1ProgramOrder() const;
    static bool cop1RetirementReady(
      const InFlightCOP1Operation &operation);
    static bool pendingMultiplyDivideLatencyValid(
      const PendingMultiplyDivide &operation);
    static bool pendingMultiplyDivideRegisterValid(
      const PendingMultiplyDivide &operation);
    static bool pendingMultiplyDivideDestinationValid(
      const PendingMultiplyDivide &operation);
    bool concurrentMultiplyDivideExecutionStateValid() const;
    bool concurrentMultiplyDivideLatenciesValid() const;
    bool concurrentMultiplyDestinationsValid() const;
    bool cop1ProgramOrderInRange(
      const InFlightCOP1Operation &operation) const;
    bool cop1ProgramOrderUnique(
      const COP1ProgramOrderView &programOrder) const;
    bool cop1DividerResultCountValid(
      const COP1ProgramOrderView &programOrder) const;
    bool cop1DividerOverlapValid(
      const COP1ProgramOrderView &programOrder) const;
    static bool cop1DividerInitiationIntervalValid(
      const COP1DividerOccupancy &occupancy);
    static bool cop1DividerOperationPresenceValid(
      const COP1DividerOccupancy &occupancy);
    static bool cop1DividerOperationFamilyValid(
      const COP1DividerOccupancy &occupancy);
    bool cop1DividerOccupancyConsistent() const;
    bool stagedCOP1PipelineOrderValid(
      const COP1ProgramOrderView &programOrder) const;
    static void completeInFlightCOP1(
      InFlightCOP1Operation *operation,
      COP1CompletionReason reason);
    static void releaseInFlightCOP1(
      InFlightCOP1Operation *operation);
    bool drainInFlightExecution();
    void drainIntegerMACContinuations();
    bool drainInFlightCOP1();
    void advancePendingCOP1(
      std::uint32_t *completedLoadRegisters);
    COP1AdvanceEligibility evaluateCOP1AdvanceEligibility()
      const;
    COP1AdvanceResult advanceInFlightCOP1Operations(
      const COP1ProgramOrderView &programOrder,
      const COP1AdvanceEligibility &eligibility);
    void recordCOP1StageTransitions(
      const COP1ProgramOrderView &programOrder,
      const COP1AdvanceResult &result);
    void recordCOP1MoveInterlocks(
      const COP1ProgramOrderView &programOrder,
      const COP1AdvanceEligibility &eligibility);
    COP1RetirementResult retireReadyInFlightCOP1();
    const InFlightCOP1Operation *
      cop1MoveTStageBlocker(
        const InFlightCOP1Operation &move) const;
    COP1OperationAdvanceResult advanceInFlightCOP1Operation(
      InFlightCOP1Operation *operation);
    COP1OperationAdvanceOutcome advanceStagedCOP1Operation(
      InFlightCOP1Operation *operation);
    COP1OperationAdvanceOutcome
      advanceRegisterMoveCOP1Operation(
        InFlightCOP1Operation *operation);
    COP1OperationAdvanceOutcome advanceMemoryCOP1Operation(
      InFlightCOP1Operation *operation);
    COP1OperationAdvanceOutcome advanceDividerCOP1Operation(
      InFlightCOP1Operation *operation);
    static void computeInFlightCOP1StagedOperation(
      InFlightCOP1Operation *operation);
    COP1DividerOccupancy derivedCOP1DividerOccupancy()
      const;
    void reconcileCOP1DividerOccupancy();
    void startPendingCOP1Divider(
      const EEInstruction &instruction,
      std::uint32_t capturedFS,
      std::uint32_t capturedFT,
      std::uint32_t result,
      std::uint8_t raisedFlags);
    void commitInFlightCOP1(
      InFlightCOP1Operation *operation);
    void retireInFlightCOP1(
      InFlightCOP1Operation *operation);
    void recordCOP1StageTransition(
      const InFlightCOP1Operation &operation,
      std::uint8_t fromStage,
      COP1PipelineStage toStage);
    void recordCOP1Retirement(
      const InFlightCOP1Operation &operation);
    bool cop1ScoreboardBlocks(
      const EEInstruction &instruction,
      std::uint32_t completedLoadRegisters,
      COP1ScoreboardHazard *hazard,
      COP1ScoreboardQuery query) const;
    COP1ScoreboardValue cop1ScoreboardValue(
      COP1ScoreboardResource resource,
      std::uint8_t registerIndex = 0) const;
    COP1ScoreboardValue cop1ScoreboardValueBefore(
      COP1ScoreboardResource resource,
      std::uint8_t registerIndex,
      std::uint64_t consumerOrder) const;
    std::uint32_t scoreboardFPRValue(
      std::uint8_t registerIndex) const;
    std::uint32_t scoreboardFPRValueForT(
      std::uint8_t registerIndex,
      std::uint64_t consumerOrder) const;
    std::uint32_t scoreboardAccumulatorValueForT(
      std::uint64_t consumerOrder) const;
    std::uint32_t scoreboardFCR31ValueForT(
      std::uint64_t consumerOrder) const;
    bool scoreboardCOP1Condition() const;
    static COP1Dependency dependencyForAccess(
      bool reads,
      bool writes);
    bool validateDelaySlotInstruction(
      const EEInstruction &instruction,
      std::uint32_t address);
    void scheduleBranch(
      BranchOutcome outcome,
      BranchMode mode,
      std::uint32_t target,
      std::uint32_t address);
    void recordCycleEvent(const CycleEvent &event);
    void recordMemoryTrace(
      std::uint32_t address,
      std::uint8_t width,
      MemoryAccessDirection direction,
      MemoryAccessOutcome outcome,
      std::uint64_t low,
      std::uint64_t high = 0);
    static MemoryAccessOutcome memoryAccessOutcome(
      bool succeeded);
    EEInstructionExecutionOutcome raiseDataAccessException(
      EEException type,
      std::uint32_t instructionAddress,
      std::uint32_t dataAddress,
      std::uint32_t instruction);
    bool raiseCOP1DataAccessException(
      const InFlightCOP1Operation &operation,
      EEException type,
      std::uint32_t dataAddress);
};

#endif
