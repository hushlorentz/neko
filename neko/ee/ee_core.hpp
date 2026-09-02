#ifndef EE_CORE_HPP
#define EE_CORE_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "clocked_component.hpp"
#include "ee_instruction.hpp"

class EEBus;

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
  Breakpoint
};

struct EEInstructionFetchResult
{
  bool succeeded = false;
  std::uint32_t address = 0;
  std::uint32_t instruction = 0;
};

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
}

namespace EECOP0Cause
{
  constexpr std::uint32_t EXCEPTION_CODE_MASK =
    UINT32_C(0x1f) << 2;
  constexpr std::uint32_t INTC_PENDING = UINT32_C(1) << 10;
  constexpr std::uint32_t DMAC_PENDING = UINT32_C(1) << 11;
  constexpr std::uint32_t BRANCH_DELAY = UINT32_C(1) << 31;
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

class EECore : public ClockedComponent
{
  public:
    static constexpr std::size_t GENERAL_REGISTER_COUNT = 32;

    void reset();
    void attachBus(EEBus *bus);
    EEInstructionFetchResult fetchInstruction();
    void startExecution(std::uint32_t startAddress);
    void haltExecution();
    void enterInterruptException();
    bool clockActive() const override;
    void clock() override;
    EEExecutionState executionState() const;
    EEStopReason stopReason() const;
    std::uint64_t elapsedCycles() const;
    bool hasLastInstruction() const;
    std::uint32_t lastInstructionAddress() const;
    const EEInstruction &lastInstruction() const;
    std::uint32_t rejectedInstruction() const;

    const EERegister128 &generalRegister(
      std::size_t index) const;
    void setGeneralRegister(
      std::size_t index,
      const EERegister128 &value);

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

    enum class CycleTraceKind : std::uint8_t
    {
      InstructionIssued,
      BranchScheduled,
      MemoryAccess,
      ExceptionEntered,
      InterruptDelivered
    };

    struct CycleTraceEvent
    {
      CycleTraceKind kind = CycleTraceKind::InstructionIssued;
      std::uint64_t value0 = 0;
      std::uint64_t value1 = 0;
      std::uint64_t value2 = 0;
      std::uint64_t value3 = 0;
    };

    struct PendingMultiplyDivide
    {
      bool active = false;
      std::uint8_t remainingCycles = 0;
      std::uint64_t hiResult = 0;
      std::uint64_t loResult = 0;
      bool writeGeneralRegister = false;
      std::uint8_t generalRegister = 0;
      std::uint64_t generalRegisterResult = 0;
    };

    std::array<EERegister128, GENERAL_REGISTER_COUNT>
      generalRegisters = {};
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
    EEException exception = EEException::None;
    std::uint32_t faultAddress = 0;
    EEExecutionState state = EEExecutionState::Halted;
    EEStopReason haltReason = EEStopReason::None;
    std::uint64_t cycles = 0;
    bool lastInstructionValid = false;
    std::uint32_t lastAddress = 0;
    EEInstruction lastDecodedInstruction;
    std::uint32_t rejectedInstructionValue = 0;
    PendingMultiplyDivide pendingMac0;
    PendingMultiplyDivide pendingMac1;
    std::uint8_t recentShiftAmountAccesses = 0;
    std::uint8_t recentShiftAmountReads = 0;
    bool branchDelayPending = false;
    std::uint32_t branchDelayTarget = 0;
    std::uint32_t branchInstructionAddress = 0;
    bool branchDelayFromLikely = false;
    bool instructionRetiredThisCycle = false;
    bool exceptionEnteredThisCycle = false;
    std::array<CycleTraceEvent, 8> cycleTraceEvents = {};
    std::size_t cycleTraceEventCount = 0;
    bool cycleTraceEnabled = false;

    static void requireGeneralRegisterIndex(
      std::size_t index);
    EEBus &attachedBus() const;
    EEInstructionFetchResult raiseFetchException(
      EEException type,
      std::uint32_t address);
    void enterException(
      EEException type,
      std::uint32_t instructionAddress,
      std::uint32_t exceptionAddress,
      std::uint32_t instruction);
    static std::uint8_t exceptionCode(EEException type);
    std::uint32_t exceptionVector(
      EEException type,
      bool alreadyExceptionLevel) const;
    void setInterruptLines(bool intc, bool dmac);
    bool interruptDeliverable() const;
    bool executeInstruction(
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
    bool raiseArithmeticOverflow(
      std::uint32_t address,
      std::uint32_t instruction);
    bool stopUndefinedOperation(
      std::uint32_t address,
      std::uint32_t instruction);
    bool pendingMultiplyDivideActive() const;
    void advancePendingMultiplyDivide(
      PendingMultiplyDivide *operation,
      bool pipeline1);
    void startPendingMultiplyDivide(
      bool pipeline1,
      std::uint8_t latency,
      std::uint64_t hiResult,
      std::uint64_t loResult,
      std::uint8_t generalRegister,
      bool writeGeneralRegister);
    bool validateShiftAmountOrdering(
      const EEInstruction &instruction,
      std::uint32_t address);
    void recordShiftAmountAccess(
      const EEInstruction &instruction);
    bool validateDelaySlotInstruction(
      const EEInstruction &instruction,
      std::uint32_t address);
    void scheduleBranch(
      bool condition,
      bool likely,
      std::uint32_t target,
      std::uint32_t address);
    void recordCycleTrace(
      CycleTraceKind kind,
      std::uint64_t value0,
      std::uint64_t value1 = 0,
      std::uint64_t value2 = 0,
      std::uint64_t value3 = 0);
    void recordMemoryTrace(
      std::uint32_t address,
      std::uint8_t width,
      bool write,
      bool succeeded,
      std::uint64_t low,
      std::uint64_t high = 0);
    bool raiseDataAccessException(
      EEException type,
      std::uint32_t instructionAddress,
      std::uint32_t dataAddress,
      std::uint32_t instruction);
};

#endif
