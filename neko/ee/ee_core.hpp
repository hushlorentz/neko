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
  DataBusErrorStore
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

class EECore : public ClockedComponent
{
  public:
    static constexpr std::size_t GENERAL_REGISTER_COUNT = 32;

    void reset();
    void attachBus(EEBus *bus);
    EEInstructionFetchResult fetchInstruction();
    void startExecution(std::uint32_t startAddress);
    void haltExecution();
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

    bool exceptionPending() const;
    EEException pendingException() const;
    std::uint32_t exceptionAddress() const;
    void clearPendingException();

  private:
    friend class NekoSaveStateCodec;

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
    std::uint32_t pc = 0;
    std::uint64_t hiRegister = 0;
    std::uint64_t loRegister = 0;
    std::uint64_t hi1Register = 0;
    std::uint64_t lo1Register = 0;
    std::uint32_t saRegister = 0;
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

    static void requireGeneralRegisterIndex(
      std::size_t index);
    EEBus &attachedBus() const;
    EEInstructionFetchResult raiseFetchException(
      EEException type,
      std::uint32_t address);
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
    bool raiseDataAccessException(
      EEException type,
      std::uint32_t instructionAddress,
      std::uint32_t dataAddress,
      std::uint32_t instruction);
    void setExceptionRestartAddress(
      std::uint32_t instructionAddress);
};

#endif
