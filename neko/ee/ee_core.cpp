#include "ee_core.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>

#include "ee_bus.hpp"
#include "floating_point_ops.hpp"
#include "vpu/vpu.hpp"

constexpr std::size_t EECore::GENERAL_REGISTER_COUNT;
constexpr std::size_t EECore::FLOATING_POINT_REGISTER_COUNT;

namespace
{
  constexpr std::uint64_t WORD_SIGN_BIT =
    UINT64_C(0x80000000);
  constexpr std::uint64_t DOUBLEWORD_SIGN_BIT =
    UINT64_C(0x8000000000000000);
  constexpr std::uint64_t EE_STATE_FNV_OFFSET_BASIS =
    UINT64_C(14695981039346656037);
  constexpr std::uint64_t EE_STATE_FNV_PRIME =
    UINT64_C(1099511628211);
  constexpr std::uint8_t MULTIPLY_LATENCY = 4;
  constexpr std::uint8_t DIVIDE_LATENCY = 37;

  void hashEEStateValue(
    std::uint64_t *hash,
    std::uint64_t value)
  {
    for (std::uint8_t index = 0; index < 8; ++index)
    {
      *hash ^= static_cast<std::uint8_t>(value >> (index * 8));
      *hash *= EE_STATE_FNV_PRIME;
    }
  }

  std::uint64_t signExtend16(std::uint16_t value)
  {
    if ((value & 0x8000) != 0)
    {
      return UINT64_C(0xffffffffffff0000) | value;
    }
    return value;
  }

  std::uint64_t signExtendWord(std::uint32_t value)
  {
    if ((value & WORD_SIGN_BIT) != 0)
    {
      return UINT64_C(0xffffffff00000000) | value;
    }
    return value;
  }

  EERegister128 quadwordFromFPRegister(
    const FPRegister &value)
  {
    return {
      static_cast<std::uint64_t>(value.x.bits()) |
        (static_cast<std::uint64_t>(value.y.bits()) << 32),
      static_cast<std::uint64_t>(value.z.bits()) |
        (static_cast<std::uint64_t>(value.w.bits()) << 32)
    };
  }

  std::int32_t signedWord(std::uint32_t value)
  {
    std::int32_t result = 0;
    static_assert(sizeof(result) == sizeof(value), "");
    std::memcpy(&result, &value, sizeof(result));
    return result;
  }

  std::uint64_t multiplyWords(
    std::uint32_t left,
    std::uint32_t right,
    bool signedOperands)
  {
    if (!signedOperands)
    {
      return
        static_cast<std::uint64_t>(left) *
        static_cast<std::uint64_t>(right);
    }
    const std::int64_t product =
      static_cast<std::int64_t>(signedWord(left)) *
      static_cast<std::int64_t>(signedWord(right));
    return static_cast<std::uint64_t>(product);
  }

  std::uint64_t accumulatorValue(
    std::uint64_t hi,
    std::uint64_t lo)
  {
    return
      (static_cast<std::uint64_t>(
        static_cast<std::uint32_t>(hi)) << 32) |
      static_cast<std::uint32_t>(lo);
  }

  bool writesShiftAmount(EEOperation operation)
  {
    return
      operation == EEOperation::MoveToShiftAmount ||
      operation == EEOperation::MoveByteCountToShiftAmount ||
      operation == EEOperation::MoveHalfwordCountToShiftAmount;
  }

  bool isWordValue(std::uint64_t value)
  {
    return value == signExtendWord(
      static_cast<std::uint32_t>(value));
  }

  bool signedLess(
    std::uint64_t left,
    std::uint64_t right)
  {
    return
      (left ^ DOUBLEWORD_SIGN_BIT) <
      (right ^ DOUBLEWORD_SIGN_BIT);
  }

  bool addOverflow32(
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t result)
  {
    return
      ((~(left ^ right) & (left ^ result)) &
       UINT32_C(0x80000000)) != 0;
  }

  bool subtractOverflow32(
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t result)
  {
    return
      (((left ^ right) & (left ^ result)) &
       UINT32_C(0x80000000)) != 0;
  }

  bool addOverflow64(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t result)
  {
    return
      ((~(left ^ right) & (left ^ result)) &
       DOUBLEWORD_SIGN_BIT) != 0;
  }

  bool subtractOverflow64(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t result)
  {
    return
      (((left ^ right) & (left ^ result)) &
       DOUBLEWORD_SIGN_BIT) != 0;
  }

  std::uint32_t arithmeticShiftRight32(
    std::uint32_t value,
    std::uint8_t amount)
  {
    if (amount == 0)
    {
      return value;
    }
    std::uint32_t result = value >> amount;
    if ((value & UINT32_C(0x80000000)) != 0)
    {
      result |= UINT32_MAX << (32 - amount);
    }
    return result;
  }

  std::uint64_t arithmeticShiftRight64(
    std::uint64_t value,
    std::uint8_t amount)
  {
    if (amount == 0)
    {
      return value;
    }
    std::uint64_t result = value >> amount;
    if ((value & DOUBLEWORD_SIGN_BIT) != 0)
    {
      result |= UINT64_MAX << (64 - amount);
    }
    return result;
  }
}

void EECore::reset()
{
  generalRegisters.fill({});
  floatingPointRegisters.fill(0);
  floatingPointAccumulatorRegister = 0;
  cop1StatusRegister = 0;
  pc = EEReset::VECTOR;
  hiRegister = 0;
  loRegister = 0;
  hi1Register = 0;
  lo1Register = 0;
  saRegister = 0;
  cop0BadVAddr = 0;
  cop0Count = 0;
  cop0Compare = 0;
  cop0Status = EECOP0Status::RESET;
  cop0Cause = 0;
  cop0EPC = 0;
  cop0ErrorEPC = 0;
  exception = EEException::None;
  faultAddress = 0;
  state = EEExecutionState::Halted;
  haltReason = EEStopReason::None;
  cycles = 0;
  lastInstructionValid = false;
  lastAddress = 0;
  lastDecodedInstruction = {};
  rejectedInstructionValue = 0;
  pendingMac0 = {};
  pendingMac1 = {};
  pendingCOP1Load = {};
  recentShiftAmountAccesses = 0;
  recentShiftAmountReads = 0;
  branchDelayPending = false;
  branchDelayTarget = 0;
  branchInstructionAddress = 0;
  branchDelayFromLikely = false;
  instructionRetiredThisCycle = false;
  exceptionEnteredThisCycle = false;
  cycleTraceEventCount = 0;
}

void EECore::attachBus(EEBus *newBus)
{
  if (newBus == nullptr)
  {
    throw std::invalid_argument(
      "EE Core requires a non-null bus.");
  }
  if (bus != nullptr)
  {
    throw std::logic_error(
      "EE Core bus is already attached.");
  }
  bus = newBus;
}

void EECore::attachVU0(VPU *newVU0)
{
  if (newVU0 == nullptr)
  {
    throw std::invalid_argument(
      "EE Core requires a non-null VU0.");
  }
  if (vu0 != nullptr)
  {
    throw std::logic_error(
      "EE Core VU0 is already attached.");
  }
  if (newVU0->unitType() != VPUType::VU0)
  {
    throw std::invalid_argument(
      "EE Core COP2 must attach to VU0.");
  }
  vu0 = newVU0;
}

void EECore::attachVU1(VPU *newVU1)
{
  if (newVU1 == nullptr)
  {
    throw std::invalid_argument(
      "EE Core requires a non-null VU1.");
  }
  if (vu1 != nullptr)
  {
    throw std::logic_error(
      "EE Core VU1 is already attached.");
  }
  if (newVU1->unitType() != VPUType::VU1)
  {
    throw std::invalid_argument(
      "EE Core COP2 condition must attach to VU1.");
  }
  vu1 = newVU1;
}

EEInstructionFetchResult EECore::fetchInstruction()
{
  const std::uint32_t address = pc;
  if ((address & 3) != 0)
  {
    return raiseFetchException(
      EEException::AddressErrorLoadOrFetch,
      address);
  }

  std::uint32_t instruction = 0;
  if (!attachedBus().readInstruction32(
        address,
        &instruction))
  {
    return raiseFetchException(
      EEException::InstructionBusError,
      address);
  }

  pc += 4;
  return {true, address, instruction};
}

void EECore::startExecution(std::uint32_t startAddress)
{
  const bool resumePendingBranch =
    state == EEExecutionState::Halted &&
    haltReason == EEStopReason::HostHalt &&
    branchDelayPending &&
    startAddress == pc;
  const bool resumePendingMultiplyDivide =
    state == EEExecutionState::Halted &&
    haltReason == EEStopReason::HostHalt &&
    pendingMultiplyDivideActive() &&
    startAddress == pc;
  const bool resumePendingCOP1Load =
    state == EEExecutionState::Halted &&
    haltReason == EEStopReason::HostHalt &&
    pendingCOP1Load.active &&
    startAddress == pc;
  pc = startAddress;
  clearPendingException();
  state = EEExecutionState::Running;
  haltReason = EEStopReason::None;
  if (!resumePendingBranch)
  {
    lastInstructionValid = false;
    lastAddress = 0;
    lastDecodedInstruction = {};
    branchDelayPending = false;
    branchDelayTarget = 0;
    branchInstructionAddress = 0;
    branchDelayFromLikely = false;
  }
  if (!resumePendingMultiplyDivide)
  {
    pendingMac0 = {};
    pendingMac1 = {};
  }
  if (!resumePendingCOP1Load)
  {
    pendingCOP1Load = {};
  }
  rejectedInstructionValue = 0;
  exceptionEnteredThisCycle = false;
}

void EECore::prepareFreshExecution(
  std::uint32_t entryPoint,
  std::uint32_t stackPointer,
  std::uint32_t returnAddress)
{
  reset();
  pc = entryPoint;
  generalRegisters[29].low = stackPointer;
  generalRegisters[31].low = returnAddress;
}

void EECore::haltExecution()
{
  state = EEExecutionState::Halted;
  haltReason = EEStopReason::HostHalt;
}

void EECore::enterInterruptException()
{
  enterException(EEException::Interrupt, pc, pc, 0);
}

bool EECore::clockActive() const
{
  return state == EEExecutionState::Running;
}

void EECore::clock()
{
  instructionRetiredThisCycle = false;
  exceptionEnteredThisCycle = false;
  cycleTraceEventCount = 0;
  if (!clockActive())
  {
    return;
  }

  ++cycles;
  std::uint8_t completedCOP1LoadRegister = 0;
  const bool completedCOP1Load =
    completePendingCOP1Load(&completedCOP1LoadRegister);
  if (interruptDeliverable())
  {
    enterInterruptException();
    return;
  }
  const bool hadPendingOperation =
    pendingMultiplyDivideActive();
  advancePendingMultiplyDivide(&pendingMac0, false);
  advancePendingMultiplyDivide(&pendingMac1, true);
  if (hadPendingOperation && pendingMultiplyDivideActive())
  {
    return;
  }

  const EEInstructionFetchResult fetched =
    fetchInstruction();
  if (!fetched.succeeded)
  {
    enterException(
      exception,
      fetched.address,
      fetched.address,
      0);
    return;
  }

  EEInstruction decoded;
  try
  {
    decoded = decodeEEInstruction(fetched.instruction);
  }
  catch (const EEInstructionDecodeError &error)
  {
    rejectedInstructionValue = fetched.instruction;
    if (error.failure() == EEInstructionDecodeFailure::Reserved)
    {
      enterException(
        EEException::ReservedInstruction,
        fetched.address,
        fetched.address,
        fetched.instruction);
      return;
    }
    pc = fetched.address;
    state = EEExecutionState::Halted;
    haltReason = EEStopReason::UnsupportedInstruction;
    return;
  }

  const bool wasDelaySlot = branchDelayPending;
  const std::uint32_t completedBranchTarget =
    branchDelayTarget;
  const FPRDependency fprDependency =
    completedCOP1Load ?
      instructionFPRDependency(
        decoded,
        completedCOP1LoadRegister) :
      FPRDependency::None;
  if (fprDependency != FPRDependency::None)
  {
    recordCycleTrace(
      CycleTraceKind::COP1LoadInterlock,
      fetched.address,
      fetched.instruction,
      completedCOP1LoadRegister,
      static_cast<std::uint8_t>(fprDependency));
    pc = fetched.address;
    return;
  }
  recordCycleTrace(
    CycleTraceKind::InstructionIssued,
    fetched.address,
    fetched.instruction,
    static_cast<std::uint8_t>(decoded.operation),
    wasDelaySlot);
  if (!executeInstruction(decoded, fetched.address))
  {
    return;
  }

  if (wasDelaySlot)
  {
    pc = completedBranchTarget;
    branchDelayPending = false;
    branchDelayTarget = 0;
    branchInstructionAddress = 0;
    branchDelayFromLikely = false;
  }
  recordShiftAmountAccess(decoded);
  lastDecodedInstruction = decoded;
  lastAddress = fetched.address;
  lastInstructionValid = true;
  instructionRetiredThisCycle = true;
}

bool EECore::executeInstruction(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  if (!validateShiftAmountOrdering(instruction, address))
  {
    rejectedInstructionValue = instruction.raw;
    return false;
  }
  if (!validateDelaySlotInstruction(instruction, address))
  {
    rejectedInstructionValue = instruction.raw;
    return false;
  }

  const std::uint64_t source =
    generalRegisters[instruction.sourceRegister].low;
  const std::uint64_t target =
    generalRegisters[instruction.targetRegister].low;
  const std::uint64_t immediate =
    signExtend16(instruction.immediate);
  const std::uint8_t destination =
    instruction.destinationRegister;
  const std::uint8_t immediateDestination =
    instruction.targetRegister;

  switch (instruction.operation)
  {
    case EEOperation::Nop:
      return true;
    case EEOperation::ExceptionReturn:
      if ((cop0Status & EECOP0Status::ERROR_LEVEL) != 0)
      {
        pc = cop0ErrorEPC;
        cop0Status &= ~EECOP0Status::ERROR_LEVEL;
      }
      else
      {
        pc = cop0EPC;
        cop0Status &= ~EECOP0Status::EXCEPTION_LEVEL;
      }
      clearPendingException();
      return true;
    case EEOperation::SystemCall:
      enterException(
        EEException::SystemCall,
        address,
        address,
        instruction.raw);
      return false;
    case EEOperation::Breakpoint:
      enterException(
        EEException::Breakpoint,
        address,
        address,
        instruction.raw);
      return false;
    case EEOperation::MoveWordFromCOP1:
      if (!requireCOP1Usable(address, instruction.raw))
      {
        return false;
      }
      writeWord(
        immediateDestination,
        floatingPointRegisters[destination]);
      return true;
    case EEOperation::MoveWordToCOP1:
      if (!requireCOP1Usable(address, instruction.raw))
      {
        return false;
      }
      floatingPointRegisters[destination] =
        static_cast<std::uint32_t>(target);
      return true;
    case EEOperation::MoveControlWordFromCOP1:
      if (!requireCOP1Usable(address, instruction.raw))
      {
        return false;
      }
      writeWord(
        immediateDestination,
        cop1ControlRegister(destination));
      return true;
    case EEOperation::MoveControlWordToCOP1:
      if (!requireCOP1Usable(address, instruction.raw))
      {
        return false;
      }
      setCOP1ControlRegister(
        destination,
        static_cast<std::uint32_t>(target));
      return true;
    case EEOperation::AbsoluteSingleCOP1:
    case EEOperation::MoveSingleCOP1:
    case EEOperation::NegateSingleCOP1:
    {
      if (!requireCOP1Usable(address, instruction.raw))
      {
        return false;
      }
      const std::uint32_t sourceBits =
        floatingPointRegisters[destination];
      std::uint32_t resultBits = sourceBits;
      if (instruction.operation ==
          EEOperation::AbsoluteSingleCOP1)
      {
        resultBits &= UINT32_C(0x7fffffff);
      }
      else if (instruction.operation ==
               EEOperation::NegateSingleCOP1)
      {
        resultBits ^= UINT32_C(0x80000000);
      }
      floatingPointRegisters[instruction.shiftAmount] =
        resultBits;
      if (instruction.operation !=
          EEOperation::MoveSingleCOP1)
      {
        updateCOP1ArithmeticFlags(
          FP_FLAG_OVERFLOW | FP_FLAG_UNDERFLOW,
          0);
      }
      return true;
    }
    case EEOperation::ShiftLeftLogicalWord:
    case EEOperation::ShiftRightLogicalWord:
    case EEOperation::ShiftRightArithmeticWord:
    case EEOperation::ShiftLeftLogicalVariableWord:
    case EEOperation::ShiftRightLogicalVariableWord:
    case EEOperation::ShiftRightArithmeticVariableWord:
    {
      if (!requireWordValue(
            instruction.targetRegister,
            address,
            instruction.raw))
      {
        return false;
      }
      const std::uint8_t amount =
        instruction.operation ==
          EEOperation::ShiftLeftLogicalWord ||
        instruction.operation ==
          EEOperation::ShiftRightLogicalWord ||
        instruction.operation ==
          EEOperation::ShiftRightArithmeticWord
          ? instruction.shiftAmount
          : source & 0x1f;
      const std::uint32_t word =
        static_cast<std::uint32_t>(target);
      std::uint32_t result = 0;
      if (instruction.operation ==
            EEOperation::ShiftLeftLogicalWord ||
          instruction.operation ==
            EEOperation::ShiftLeftLogicalVariableWord)
      {
        result = word << amount;
      }
      else if (instruction.operation ==
                 EEOperation::ShiftRightLogicalWord ||
               instruction.operation ==
                 EEOperation::ShiftRightLogicalVariableWord)
      {
        result = word >> amount;
      }
      else
      {
        result = arithmeticShiftRight32(word, amount);
      }
      writeWord(destination, result);
      return true;
    }
    case EEOperation::ShiftLeftLogicalVariableDoubleword:
      writeLowDoubleword(
        destination,
        target << (source & 0x3f));
      return true;
    case EEOperation::ShiftRightLogicalVariableDoubleword:
      writeLowDoubleword(
        destination,
        target >> (source & 0x3f));
      return true;
    case EEOperation::ShiftRightArithmeticVariableDoubleword:
      writeLowDoubleword(
        destination,
        arithmeticShiftRight64(
          target,
          source & 0x3f));
      return true;
    case EEOperation::ShiftLeftLogicalDoubleword:
      writeLowDoubleword(
        destination,
        target << instruction.shiftAmount);
      return true;
    case EEOperation::ShiftRightLogicalDoubleword:
      writeLowDoubleword(
        destination,
        target >> instruction.shiftAmount);
      return true;
    case EEOperation::ShiftRightArithmeticDoubleword:
      writeLowDoubleword(
        destination,
        arithmeticShiftRight64(
          target,
          instruction.shiftAmount));
      return true;
    case EEOperation::ShiftLeftLogicalDoubleword32:
      writeLowDoubleword(
        destination,
        target << (instruction.shiftAmount + 32));
      return true;
    case EEOperation::ShiftRightLogicalDoubleword32:
      writeLowDoubleword(
        destination,
        target >> (instruction.shiftAmount + 32));
      return true;
    case EEOperation::ShiftRightArithmeticDoubleword32:
      writeLowDoubleword(
        destination,
        arithmeticShiftRight64(
          target,
          instruction.shiftAmount + 32));
      return true;
    case EEOperation::AddWord:
    case EEOperation::AddUnsignedWord:
    case EEOperation::SubtractWord:
    case EEOperation::SubtractUnsignedWord:
    {
      if (!requireWordValue(
            instruction.sourceRegister,
            address,
            instruction.raw) ||
          !requireWordValue(
            instruction.targetRegister,
            address,
            instruction.raw))
      {
        return false;
      }
      const std::uint32_t left =
        static_cast<std::uint32_t>(source);
      const std::uint32_t right =
        static_cast<std::uint32_t>(target);
      const bool subtract =
        instruction.operation == EEOperation::SubtractWord ||
        instruction.operation ==
          EEOperation::SubtractUnsignedWord;
      const std::uint32_t result =
        subtract ? left - right : left + right;
      const bool trapping =
        instruction.operation == EEOperation::AddWord ||
        instruction.operation == EEOperation::SubtractWord;
      const bool overflow = subtract
        ? subtractOverflow32(left, right, result)
        : addOverflow32(left, right, result);
      if (trapping && overflow)
      {
        return raiseArithmeticOverflow(
          address,
          instruction.raw);
      }
      writeWord(destination, result);
      return true;
    }
    case EEOperation::AddDoubleword:
    case EEOperation::AddUnsignedDoubleword:
    case EEOperation::SubtractDoubleword:
    case EEOperation::SubtractUnsignedDoubleword:
    {
      const bool subtract =
        instruction.operation ==
          EEOperation::SubtractDoubleword ||
        instruction.operation ==
          EEOperation::SubtractUnsignedDoubleword;
      const std::uint64_t result =
        subtract ? source - target : source + target;
      const bool trapping =
        instruction.operation == EEOperation::AddDoubleword ||
        instruction.operation ==
          EEOperation::SubtractDoubleword;
      const bool overflow = subtract
        ? subtractOverflow64(source, target, result)
        : addOverflow64(source, target, result);
      if (trapping && overflow)
      {
        return raiseArithmeticOverflow(
          address,
          instruction.raw);
      }
      writeLowDoubleword(destination, result);
      return true;
    }
    case EEOperation::And:
      writeLowDoubleword(destination, source & target);
      return true;
    case EEOperation::Or:
      writeLowDoubleword(destination, source | target);
      return true;
    case EEOperation::Xor:
      writeLowDoubleword(destination, source ^ target);
      return true;
    case EEOperation::Nor:
      writeLowDoubleword(destination, ~(source | target));
      return true;
    case EEOperation::SetLessThan:
      writeLowDoubleword(
        destination,
        signedLess(source, target) ? 1 : 0);
      return true;
    case EEOperation::SetLessThanUnsigned:
      writeLowDoubleword(
        destination,
        source < target ? 1 : 0);
      return true;
    case EEOperation::AddImmediateWord:
    case EEOperation::AddImmediateUnsignedWord:
    {
      if (!requireWordValue(
            instruction.sourceRegister,
            address,
            instruction.raw))
      {
        return false;
      }
      const std::uint32_t left =
        static_cast<std::uint32_t>(source);
      const std::uint32_t right =
        static_cast<std::uint32_t>(immediate);
      const std::uint32_t result = left + right;
      if (instruction.operation ==
            EEOperation::AddImmediateWord &&
          addOverflow32(left, right, result))
      {
        return raiseArithmeticOverflow(
          address,
          instruction.raw);
      }
      writeWord(immediateDestination, result);
      return true;
    }
    case EEOperation::AddImmediateDoubleword:
    case EEOperation::AddImmediateUnsignedDoubleword:
    {
      const std::uint64_t result = source + immediate;
      if (instruction.operation ==
            EEOperation::AddImmediateDoubleword &&
          addOverflow64(source, immediate, result))
      {
        return raiseArithmeticOverflow(
          address,
          instruction.raw);
      }
      writeLowDoubleword(immediateDestination, result);
      return true;
    }
    case EEOperation::SetLessThanImmediate:
      writeLowDoubleword(
        immediateDestination,
        signedLess(source, immediate) ? 1 : 0);
      return true;
    case EEOperation::SetLessThanImmediateUnsigned:
      writeLowDoubleword(
        immediateDestination,
        source < immediate ? 1 : 0);
      return true;
    case EEOperation::AndImmediate:
      writeLowDoubleword(
        immediateDestination,
        source & instruction.immediate);
      return true;
    case EEOperation::OrImmediate:
      writeLowDoubleword(
        immediateDestination,
        source | instruction.immediate);
      return true;
    case EEOperation::XorImmediate:
      writeLowDoubleword(
        immediateDestination,
        source ^ instruction.immediate);
      return true;
    case EEOperation::LoadUpperImmediate:
      writeLowDoubleword(
        immediateDestination,
        signExtendWord(
          static_cast<std::uint32_t>(
            instruction.immediate) << 16));
      return true;
    case EEOperation::MoveFromHI:
      writeLowDoubleword(destination, hiRegister);
      return true;
    case EEOperation::MoveToHI:
      hiRegister = source;
      return true;
    case EEOperation::MoveFromLO:
      writeLowDoubleword(destination, loRegister);
      return true;
    case EEOperation::MoveToLO:
      loRegister = source;
      return true;
    case EEOperation::MoveFromHI1:
      writeLowDoubleword(destination, hi1Register);
      return true;
    case EEOperation::MoveToHI1:
      hi1Register = source;
      return true;
    case EEOperation::MoveFromLO1:
      writeLowDoubleword(destination, lo1Register);
      return true;
    case EEOperation::MoveToLO1:
      lo1Register = source;
      return true;
    case EEOperation::MoveFromShiftAmount:
      writeLowDoubleword(destination, saRegister);
      return true;
    case EEOperation::MoveToShiftAmount:
      saRegister = static_cast<std::uint32_t>(source);
      return true;
    case EEOperation::MoveByteCountToShiftAmount:
      saRegister =
        ((static_cast<std::uint32_t>(source) ^
          instruction.immediate) & 0x0f) * 8;
      return true;
    case EEOperation::MoveHalfwordCountToShiftAmount:
      saRegister =
        ((static_cast<std::uint32_t>(source) ^
          instruction.immediate) & 0x07) * 16;
      return true;
    case EEOperation::LoadByte:
    case EEOperation::LoadByteUnsigned:
    {
      const std::uint32_t dataAddress =
        static_cast<std::uint32_t>(source + immediate);
      std::uint8_t value = 0;
      const bool succeeded =
        attachedBus().readData8(dataAddress, &value);
      recordMemoryTrace(
        dataAddress,
        1,
        false,
        succeeded,
        succeeded ? value : 0);
      if (!succeeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorLoad,
          address,
          dataAddress,
          instruction.raw);
      }
      writeLowDoubleword(
        immediateDestination,
        instruction.operation == EEOperation::LoadByte &&
          (value & 0x80) != 0
          ? UINT64_C(0xffffffffffffff00) | value
          : value);
      return true;
    }
    case EEOperation::StoreByte:
    {
      const std::uint32_t dataAddress =
        static_cast<std::uint32_t>(source + immediate);
      const std::uint8_t value =
        static_cast<std::uint8_t>(target);
      const bool succeeded =
        attachedBus().writeData8(dataAddress, value);
      recordMemoryTrace(
        dataAddress,
        1,
        true,
        succeeded,
        value);
      if (!succeeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorStore,
          address,
          dataAddress,
          instruction.raw);
      }
      return true;
    }
    case EEOperation::LoadHalfword:
    case EEOperation::LoadHalfwordUnsigned:
    {
      const std::uint32_t dataAddress =
        static_cast<std::uint32_t>(source + immediate);
      if ((dataAddress & 1) != 0)
      {
        return raiseDataAccessException(
          EEException::AddressErrorLoadOrFetch,
          address,
          dataAddress,
          instruction.raw);
      }
      std::uint16_t value = 0;
      const bool succeeded =
        attachedBus().readData16(dataAddress, &value);
      recordMemoryTrace(
        dataAddress,
        2,
        false,
        succeeded,
        succeeded ? value : 0);
      if (!succeeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorLoad,
          address,
          dataAddress,
          instruction.raw);
      }
      writeLowDoubleword(
        immediateDestination,
        instruction.operation == EEOperation::LoadHalfword
          ? signExtend16(value)
          : value);
      return true;
    }
    case EEOperation::StoreHalfword:
    {
      const std::uint32_t dataAddress =
        static_cast<std::uint32_t>(source + immediate);
      if ((dataAddress & 1) != 0)
      {
        return raiseDataAccessException(
          EEException::AddressErrorStore,
          address,
          dataAddress,
          instruction.raw);
      }
      const std::uint16_t value =
        static_cast<std::uint16_t>(target);
      const bool succeeded =
        attachedBus().writeData16(dataAddress, value);
      recordMemoryTrace(
        dataAddress,
        2,
        true,
        succeeded,
        value);
      if (!succeeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorStore,
          address,
          dataAddress,
          instruction.raw);
      }
      return true;
    }
    case EEOperation::LoadWord:
    case EEOperation::LoadWordUnsigned:
    {
      const std::uint32_t dataAddress =
        static_cast<std::uint32_t>(source + immediate);
      if ((dataAddress & 3) != 0)
      {
        return raiseDataAccessException(
          EEException::AddressErrorLoadOrFetch,
          address,
          dataAddress,
          instruction.raw);
      }
      std::uint32_t value = 0;
      const bool succeeded =
        attachedBus().readData32(dataAddress, &value);
      recordMemoryTrace(
        dataAddress,
        4,
        false,
        succeeded,
        succeeded ? value : 0);
      if (!succeeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorLoad,
          address,
          dataAddress,
          instruction.raw);
      }
      writeLowDoubleword(
        immediateDestination,
        instruction.operation == EEOperation::LoadWord
          ? signExtendWord(value)
          : value);
      return true;
    }
    case EEOperation::StoreWord:
    {
      const std::uint32_t dataAddress =
        static_cast<std::uint32_t>(source + immediate);
      if ((dataAddress & 3) != 0)
      {
        return raiseDataAccessException(
          EEException::AddressErrorStore,
          address,
          dataAddress,
          instruction.raw);
      }
      const std::uint32_t value =
        static_cast<std::uint32_t>(target);
      const bool succeeded =
        attachedBus().writeData32(dataAddress, value);
      recordMemoryTrace(
        dataAddress,
        4,
        true,
        succeeded,
        value);
      if (!succeeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorStore,
          address,
          dataAddress,
          instruction.raw);
      }
      return true;
    }
    case EEOperation::LoadWordToCOP1:
    {
      if (!requireCOP1Usable(address, instruction.raw))
      {
        return false;
      }
      const std::uint32_t dataAddress =
        static_cast<std::uint32_t>(source + immediate);
      if ((dataAddress & 3) != 0)
      {
        return raiseDataAccessException(
          EEException::AddressErrorLoadOrFetch,
          address,
          dataAddress,
          instruction.raw);
      }
      std::uint32_t value = 0;
      const bool succeeded =
        attachedBus().readData32(dataAddress, &value);
      recordMemoryTrace(
        dataAddress,
        4,
        false,
        succeeded,
        succeeded ? value : 0);
      if (!succeeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorLoad,
          address,
          dataAddress,
          instruction.raw);
      }
      pendingCOP1Load = {
        true,
        immediateDestination,
        value
      };
      return true;
    }
    case EEOperation::StoreWordFromCOP1:
    {
      if (!requireCOP1Usable(address, instruction.raw))
      {
        return false;
      }
      const std::uint32_t dataAddress =
        static_cast<std::uint32_t>(source + immediate);
      if ((dataAddress & 3) != 0)
      {
        return raiseDataAccessException(
          EEException::AddressErrorStore,
          address,
          dataAddress,
          instruction.raw);
      }
      const std::uint32_t value =
        floatingPointRegisters[immediateDestination];
      const bool succeeded =
        attachedBus().writeData32(dataAddress, value);
      recordMemoryTrace(
        dataAddress,
        4,
        true,
        succeeded,
        value);
      if (!succeeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorStore,
          address,
          dataAddress,
          instruction.raw);
      }
      return true;
    }
    case EEOperation::LoadWordLeft:
    case EEOperation::LoadWordRight:
    {
      const std::uint32_t dataAddress =
        static_cast<std::uint32_t>(source + immediate);
      const std::uint32_t alignedAddress =
        dataAddress & ~UINT32_C(3);
      std::uint32_t memory = 0;
      const bool succeeded =
        attachedBus().readData32(alignedAddress, &memory);
      recordMemoryTrace(
        alignedAddress,
        4,
        false,
        succeeded,
        succeeded ? memory : 0);
      if (!succeeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorLoad,
          address,
          dataAddress,
          instruction.raw);
      }
      const std::uint8_t byteOffset = dataAddress & 3;
      std::uint32_t result =
        static_cast<std::uint32_t>(target);
      if (instruction.operation == EEOperation::LoadWordLeft)
      {
        for (std::uint8_t memoryByte = 0;
             memoryByte <= byteOffset;
             ++memoryByte)
        {
          const std::uint8_t registerByte =
            3 - byteOffset + memoryByte;
          const std::uint32_t mask =
            UINT32_C(0xff) << (registerByte * 8);
          result =
            (result & ~mask) |
            (((memory >> (memoryByte * 8)) & 0xff) <<
             (registerByte * 8));
        }
        writeLowDoubleword(
          immediateDestination,
          signExtendWord(result));
      }
      else
      {
        for (std::uint8_t memoryByte = byteOffset;
             memoryByte < 4;
             ++memoryByte)
        {
          const std::uint8_t registerByte =
            memoryByte - byteOffset;
          const std::uint32_t mask =
            UINT32_C(0xff) << (registerByte * 8);
          result =
            (result & ~mask) |
            (((memory >> (memoryByte * 8)) & 0xff) <<
             (registerByte * 8));
        }
        writeLowDoubleword(
          immediateDestination,
          byteOffset == 0
            ? signExtendWord(result)
            : (target & UINT64_C(0xffffffff00000000)) |
              result);
      }
      return true;
    }
    case EEOperation::StoreWordLeft:
    case EEOperation::StoreWordRight:
    {
      const std::uint32_t dataAddress =
        static_cast<std::uint32_t>(source + immediate);
      const std::uint32_t alignedAddress =
        dataAddress & ~UINT32_C(3);
      std::uint32_t memory = 0;
      const bool readSucceeded =
        attachedBus().readData32(alignedAddress, &memory);
      recordMemoryTrace(
        alignedAddress,
        4,
        false,
        readSucceeded,
        readSucceeded ? memory : 0);
      if (!readSucceeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorStore,
          address,
          dataAddress,
          instruction.raw);
      }
      const std::uint8_t byteOffset = dataAddress & 3;
      const std::uint32_t registerValue =
        static_cast<std::uint32_t>(target);
      if (instruction.operation == EEOperation::StoreWordLeft)
      {
        for (std::uint8_t memoryByte = 0;
             memoryByte <= byteOffset;
             ++memoryByte)
        {
          const std::uint8_t registerByte =
            3 - byteOffset + memoryByte;
          const std::uint32_t mask =
            UINT32_C(0xff) << (memoryByte * 8);
          memory =
            (memory & ~mask) |
            (((registerValue >> (registerByte * 8)) & 0xff) <<
             (memoryByte * 8));
        }
      }
      else
      {
        for (std::uint8_t memoryByte = byteOffset;
             memoryByte < 4;
             ++memoryByte)
        {
          const std::uint8_t registerByte =
            memoryByte - byteOffset;
          const std::uint32_t mask =
            UINT32_C(0xff) << (memoryByte * 8);
          memory =
            (memory & ~mask) |
            (((registerValue >> (registerByte * 8)) & 0xff) <<
             (memoryByte * 8));
        }
      }
      const bool writeSucceeded =
        attachedBus().writeData32(alignedAddress, memory);
      recordMemoryTrace(
        alignedAddress,
        4,
        true,
        writeSucceeded,
        memory);
      if (!writeSucceeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorStore,
          address,
          dataAddress,
          instruction.raw);
      }
      return true;
    }
    case EEOperation::LoadDoubleword:
    {
      const std::uint32_t dataAddress =
        static_cast<std::uint32_t>(source + immediate);
      if ((dataAddress & 7) != 0)
      {
        return raiseDataAccessException(
          EEException::AddressErrorLoadOrFetch,
          address,
          dataAddress,
          instruction.raw);
      }
      std::uint64_t value = 0;
      const bool succeeded =
        attachedBus().readData64(dataAddress, &value);
      recordMemoryTrace(
        dataAddress,
        8,
        false,
        succeeded,
        succeeded ? value : 0);
      if (!succeeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorLoad,
          address,
          dataAddress,
          instruction.raw);
      }
      writeLowDoubleword(immediateDestination, value);
      return true;
    }
    case EEOperation::StoreDoubleword:
    {
      const std::uint32_t dataAddress =
        static_cast<std::uint32_t>(source + immediate);
      if ((dataAddress & 7) != 0)
      {
        return raiseDataAccessException(
          EEException::AddressErrorStore,
          address,
          dataAddress,
          instruction.raw);
      }
      const bool succeeded =
        attachedBus().writeData64(dataAddress, target);
      recordMemoryTrace(
        dataAddress,
        8,
        true,
        succeeded,
        target);
      if (!succeeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorStore,
          address,
          dataAddress,
          instruction.raw);
      }
      return true;
    }
    case EEOperation::LoadDoublewordLeft:
    case EEOperation::LoadDoublewordRight:
    {
      const std::uint32_t dataAddress =
        static_cast<std::uint32_t>(source + immediate);
      const std::uint32_t alignedAddress =
        dataAddress & ~UINT32_C(7);
      std::uint64_t memory = 0;
      const bool succeeded =
        attachedBus().readData64(alignedAddress, &memory);
      recordMemoryTrace(
        alignedAddress,
        8,
        false,
        succeeded,
        succeeded ? memory : 0);
      if (!succeeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorLoad,
          address,
          dataAddress,
          instruction.raw);
      }
      const std::uint8_t byteOffset = dataAddress & 7;
      std::uint64_t result = target;
      if (instruction.operation ==
          EEOperation::LoadDoublewordLeft)
      {
        for (std::uint8_t memoryByte = 0;
             memoryByte <= byteOffset;
             ++memoryByte)
        {
          const std::uint8_t registerByte =
            7 - byteOffset + memoryByte;
          const std::uint64_t mask =
            UINT64_C(0xff) << (registerByte * 8);
          result =
            (result & ~mask) |
            (((memory >> (memoryByte * 8)) & 0xff) <<
             (registerByte * 8));
        }
      }
      else
      {
        for (std::uint8_t memoryByte = byteOffset;
             memoryByte < 8;
             ++memoryByte)
        {
          const std::uint8_t registerByte =
            memoryByte - byteOffset;
          const std::uint64_t mask =
            UINT64_C(0xff) << (registerByte * 8);
          result =
            (result & ~mask) |
            (((memory >> (memoryByte * 8)) & 0xff) <<
             (registerByte * 8));
        }
      }
      writeLowDoubleword(immediateDestination, result);
      return true;
    }
    case EEOperation::StoreDoublewordLeft:
    case EEOperation::StoreDoublewordRight:
    {
      const std::uint32_t dataAddress =
        static_cast<std::uint32_t>(source + immediate);
      const std::uint32_t alignedAddress =
        dataAddress & ~UINT32_C(7);
      std::uint64_t memory = 0;
      const bool readSucceeded =
        attachedBus().readData64(alignedAddress, &memory);
      recordMemoryTrace(
        alignedAddress,
        8,
        false,
        readSucceeded,
        readSucceeded ? memory : 0);
      if (!readSucceeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorStore,
          address,
          dataAddress,
          instruction.raw);
      }
      const std::uint8_t byteOffset = dataAddress & 7;
      if (instruction.operation ==
          EEOperation::StoreDoublewordLeft)
      {
        for (std::uint8_t memoryByte = 0;
             memoryByte <= byteOffset;
             ++memoryByte)
        {
          const std::uint8_t registerByte =
            7 - byteOffset + memoryByte;
          const std::uint64_t mask =
            UINT64_C(0xff) << (memoryByte * 8);
          memory =
            (memory & ~mask) |
            (((target >> (registerByte * 8)) & 0xff) <<
             (memoryByte * 8));
        }
      }
      else
      {
        for (std::uint8_t memoryByte = byteOffset;
             memoryByte < 8;
             ++memoryByte)
        {
          const std::uint8_t registerByte =
            memoryByte - byteOffset;
          const std::uint64_t mask =
            UINT64_C(0xff) << (memoryByte * 8);
          memory =
            (memory & ~mask) |
            (((target >> (registerByte * 8)) & 0xff) <<
             (memoryByte * 8));
        }
      }
      const bool writeSucceeded =
        attachedBus().writeData64(alignedAddress, memory);
      recordMemoryTrace(
        alignedAddress,
        8,
        true,
        writeSucceeded,
        memory);
      if (!writeSucceeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorStore,
          address,
          dataAddress,
          instruction.raw);
      }
      return true;
    }
    case EEOperation::LoadQuadword:
    {
      const std::uint32_t dataAddress =
        static_cast<std::uint32_t>(source + immediate) &
        ~UINT32_C(0x0f);
      EEQuadword value = {};
      const bool succeeded =
        attachedBus().readData128(dataAddress, &value);
      recordMemoryTrace(
        dataAddress,
        16,
        false,
        succeeded,
        succeeded ? value.low : 0,
        succeeded ? value.high : 0);
      if (!succeeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorLoad,
          address,
          dataAddress,
          instruction.raw);
      }
      if (immediateDestination != 0)
      {
        generalRegisters[immediateDestination] = {
          value.low,
          value.high
        };
      }
      return true;
    }
    case EEOperation::StoreQuadword:
    {
      const std::uint32_t dataAddress =
        static_cast<std::uint32_t>(source + immediate) &
        ~UINT32_C(0x0f);
      const EERegister128 &value =
        generalRegisters[immediateDestination];
      const EEDataWriteResult writeResult =
        attachedBus().writeGuestData128(
          dataAddress,
          {value.low, value.high});
      const bool succeeded =
        writeResult == EEDataWriteResult::Completed;
      recordMemoryTrace(
        dataAddress,
        16,
        true,
        succeeded,
        value.low,
        value.high);
      if (writeResult == EEDataWriteResult::Stalled)
      {
        pc = address;
        return false;
      }
      if (!succeeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorStore,
          address,
          dataAddress,
          instruction.raw);
      }
      return true;
    }
    case EEOperation::LoadQuadwordToCOP2:
    {
      const std::uint32_t dataAddress =
        static_cast<std::uint32_t>(source + immediate);
      if ((dataAddress & 0x0f) != 0)
      {
        return raiseDataAccessException(
          EEException::AddressErrorLoadOrFetch,
          address,
          dataAddress,
          instruction.raw);
      }
      if (attachedVU0().macroRegisterNumberWritePending(
            immediateDestination))
      {
        pc = address;
        return false;
      }
      EEQuadword value = {};
      const bool succeeded =
        attachedBus().readData128(dataAddress, &value);
      recordMemoryTrace(
        dataAddress,
        16,
        false,
        succeeded,
        succeeded ? value.low : 0,
        succeeded ? value.high : 0);
      if (!succeeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorLoad,
          address,
          dataAddress,
          instruction.raw);
      }
      attachedVU0().loadFPRegisterBits(
        immediateDestination,
        static_cast<std::uint32_t>(value.low),
        static_cast<std::uint32_t>(value.low >> 32),
        static_cast<std::uint32_t>(value.high),
        static_cast<std::uint32_t>(value.high >> 32));
      attachedVU0().noteMacroTransferToVU();
      return true;
    }
    case EEOperation::StoreQuadwordFromCOP2:
    {
      const std::uint32_t dataAddress =
        static_cast<std::uint32_t>(source + immediate);
      if ((dataAddress & 0x0f) != 0)
      {
        return raiseDataAccessException(
          EEException::AddressErrorStore,
          address,
          dataAddress,
          instruction.raw);
      }
      if (attachedVU0().macroRegisterNumberWritePending(
            immediateDestination))
      {
        pc = address;
        return false;
      }
      const EERegister128 value = quadwordFromFPRegister(
        *attachedVU0().fpRegisterValue(immediateDestination));
      const EEDataWriteResult writeResult =
        attachedBus().writeGuestData128(
          dataAddress,
          {value.low, value.high});
      const bool succeeded =
        writeResult == EEDataWriteResult::Completed;
      recordMemoryTrace(
        dataAddress,
        16,
        true,
        succeeded,
        value.low,
        value.high);
      if (writeResult == EEDataWriteResult::Stalled)
      {
        pc = address;
        return false;
      }
      if (!succeeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorStore,
          address,
          dataAddress,
          instruction.raw);
      }
      return true;
    }
    case EEOperation::QuadwordMoveFromCOP2:
    {
      if (((instruction.raw & 1) != 0 &&
           attachedVU0().microModeActive()) ||
          attachedVU0().macroRegisterNumberWritePending(
            destination))
      {
        pc = address;
        return false;
      }
      if (immediateDestination != 0)
      {
        generalRegisters[immediateDestination] =
          quadwordFromFPRegister(
            *attachedVU0().fpRegisterValue(destination));
      }
      return true;
    }
    case EEOperation::QuadwordMoveToCOP2:
    {
      if (((instruction.raw & 1) != 0 &&
           !attachedVU0().cop2WriteAvailable()) ||
          attachedVU0().macroRegisterNumberWritePending(
            destination))
      {
        pc = address;
        return false;
      }
      const EERegister128 &value =
        generalRegisters[immediateDestination];
      attachedVU0().loadFPRegisterBits(
        destination,
        static_cast<std::uint32_t>(value.low),
        static_cast<std::uint32_t>(value.low >> 32),
        static_cast<std::uint32_t>(value.high),
        static_cast<std::uint32_t>(value.high >> 32));
      attachedVU0().noteMacroTransferToVU();
      return true;
    }
    case EEOperation::ControlMoveFromCOP2:
    {
      if (((instruction.raw & 1) != 0 &&
           attachedVU0().microModeActive()) ||
          (destination < 16 &&
           attachedVU0().macroRegisterNumberWritePending(
             destination)) ||
          (attachedVU0().macroModeActive() &&
           (destination == 16 || destination == 17)))
      {
        pc = address;
        return false;
      }
      std::uint32_t value = 0;
      if (!readCOP2ControlRegister(destination, &value))
      {
        return stopUndefinedOperation(
          address,
          instruction.raw);
      }
      if (destination < 16)
      {
        writeLowDoubleword(immediateDestination, value);
      }
      else
      {
        writeWord(immediateDestination, value);
      }
      return true;
    }
    case EEOperation::ControlMoveToCOP2:
      if (((instruction.raw & 1) != 0 &&
           !attachedVU0().cop2WriteAvailable()) ||
          (destination < 16 &&
           attachedVU0().macroRegisterNumberWritePending(
             destination)) ||
          (attachedVU0().macroModeActive() &&
           (destination == 16 ||
            destination == 18)))
      {
        pc = address;
        return false;
      }
      if (!writeCOP2ControlRegister(
            destination,
            static_cast<std::uint32_t>(target)))
      {
        return stopUndefinedOperation(
          address,
          instruction.raw);
      }
      attachedVU0().noteMacroTransferToVU();
      return true;
    case EEOperation::BranchCOP2False:
    case EEOperation::BranchCOP2FalseLikely:
    case EEOperation::BranchCOP2True:
    case EEOperation::BranchCOP2TrueLikely:
    {
      const bool conditionSignal =
        attachedVU1().clockActive();
      const bool branchOnTrue =
        instruction.operation == EEOperation::BranchCOP2True ||
        instruction.operation ==
          EEOperation::BranchCOP2TrueLikely;
      const bool likely =
        instruction.operation ==
          EEOperation::BranchCOP2FalseLikely ||
        instruction.operation ==
          EEOperation::BranchCOP2TrueLikely;
      const std::uint32_t branchTarget =
        address + 4 +
        static_cast<std::uint32_t>(
          signExtend16(instruction.immediate) << 2);
      scheduleBranch(
        conditionSignal == branchOnTrue,
        likely,
        branchTarget,
        address);
      return true;
    }
    case EEOperation::VectorCallMicroSubroutine:
    case EEOperation::VectorCallMicroSubroutineRegister:
    {
      VPU &vu0 = attachedVU0();
      const std::size_t callAddress =
        static_cast<std::size_t>(
          instruction.operation ==
            EEOperation::VectorCallMicroSubroutine
            ? instruction.cop2Immediate
            : vu0.callAddressRegister()) *
        8;
      if (callAddress > vu0.microMemorySize() - 8)
      {
        return stopUndefinedOperation(
          address,
          instruction.raw);
      }
      const std::uint16_t startAddress =
        static_cast<std::uint16_t>(callAddress);
      if (vu0.macroModeActive())
      {
        if (!vu0.startMicroModeFromMacro(startAddress))
        {
          pc = address;
          return false;
        }
      }
      else
      {
        if (vu0.microModeActive())
        {
          pc = address;
          return false;
        }
        vu0.startMicroMode(startAddress);
      }
      return true;
    }
    case EEOperation::VectorMacroArithmetic:
    {
      VPU &vu0 = attachedVU0();
      if (vu0.getState() == VPU_STATE_STOP)
      {
        return stopUndefinedOperation(
          address,
          instruction.raw);
      }
      if (!vu0.issueMacroInstruction(
            instruction.raw & UINT32_C(0x01ffffff)))
      {
        pc = address;
        return false;
      }
      return true;
    }
    case EEOperation::Jump:
      scheduleBranch(
        true,
        false,
        ((address + 4) & UINT32_C(0xf0000000)) |
          (instruction.target << 2),
        address);
      return true;
    case EEOperation::JumpAndLink:
      writeLowDoubleword(31, address + 8);
      scheduleBranch(
        true,
        false,
        ((address + 4) & UINT32_C(0xf0000000)) |
          (instruction.target << 2),
        address);
      return true;
    case EEOperation::JumpRegister:
      scheduleBranch(
        true,
        false,
        static_cast<std::uint32_t>(source),
        address);
      return true;
    case EEOperation::JumpAndLinkRegister:
      if (instruction.sourceRegister == destination)
      {
        return stopUndefinedOperation(
          address,
          instruction.raw);
      }
      writeLowDoubleword(destination, address + 8);
      scheduleBranch(
        true,
        false,
        static_cast<std::uint32_t>(source),
        address);
      return true;
    case EEOperation::BranchEqual:
    case EEOperation::BranchNotEqual:
    case EEOperation::BranchLessThanOrEqualZero:
    case EEOperation::BranchGreaterThanZero:
    case EEOperation::BranchLessThanZero:
    case EEOperation::BranchGreaterThanOrEqualZero:
    case EEOperation::BranchEqualLikely:
    case EEOperation::BranchNotEqualLikely:
    case EEOperation::BranchLessThanOrEqualZeroLikely:
    case EEOperation::BranchGreaterThanZeroLikely:
    case EEOperation::BranchLessThanZeroLikely:
    case EEOperation::BranchGreaterThanOrEqualZeroLikely:
    case EEOperation::BranchLessThanZeroAndLink:
    case EEOperation::BranchGreaterThanOrEqualZeroAndLink:
    case EEOperation::BranchLessThanZeroAndLinkLikely:
    case EEOperation::BranchGreaterThanOrEqualZeroAndLinkLikely:
    {
      const bool negative =
        (source & DOUBLEWORD_SIGN_BIT) != 0;
      bool condition = false;
      switch (instruction.operation)
      {
        case EEOperation::BranchEqual:
        case EEOperation::BranchEqualLikely:
          condition = source == target;
          break;
        case EEOperation::BranchNotEqual:
        case EEOperation::BranchNotEqualLikely:
          condition = source != target;
          break;
        case EEOperation::BranchLessThanOrEqualZero:
        case EEOperation::BranchLessThanOrEqualZeroLikely:
          condition = negative || source == 0;
          break;
        case EEOperation::BranchGreaterThanZero:
        case EEOperation::BranchGreaterThanZeroLikely:
          condition = !negative && source != 0;
          break;
        case EEOperation::BranchLessThanZero:
        case EEOperation::BranchLessThanZeroLikely:
        case EEOperation::BranchLessThanZeroAndLink:
        case EEOperation::BranchLessThanZeroAndLinkLikely:
          condition = negative;
          break;
        default:
          condition = !negative;
          break;
      }
      const bool likely =
        instruction.operation == EEOperation::BranchEqualLikely ||
        instruction.operation == EEOperation::BranchNotEqualLikely ||
        instruction.operation ==
          EEOperation::BranchLessThanOrEqualZeroLikely ||
        instruction.operation ==
          EEOperation::BranchGreaterThanZeroLikely ||
        instruction.operation ==
          EEOperation::BranchLessThanZeroLikely ||
        instruction.operation ==
          EEOperation::BranchGreaterThanOrEqualZeroLikely ||
        instruction.operation ==
          EEOperation::BranchLessThanZeroAndLinkLikely ||
        instruction.operation ==
          EEOperation::BranchGreaterThanOrEqualZeroAndLinkLikely;
      const bool link =
        instruction.operation ==
          EEOperation::BranchLessThanZeroAndLink ||
        instruction.operation ==
          EEOperation::BranchGreaterThanOrEqualZeroAndLink ||
        instruction.operation ==
          EEOperation::BranchLessThanZeroAndLinkLikely ||
        instruction.operation ==
          EEOperation::BranchGreaterThanOrEqualZeroAndLinkLikely;
      if (link && instruction.sourceRegister == 31)
      {
        return stopUndefinedOperation(
          address,
          instruction.raw);
      }
      if (link)
      {
        writeLowDoubleword(31, address + 8);
      }
      const std::uint32_t branchTarget =
        address + 4 +
        static_cast<std::uint32_t>(
          signExtend16(instruction.immediate) << 2);
      scheduleBranch(
        condition,
        likely,
        branchTarget,
        address);
      return true;
    }
    case EEOperation::MultiplyWord:
    case EEOperation::MultiplyUnsignedWord:
    case EEOperation::MultiplyWord1:
    case EEOperation::MultiplyUnsignedWord1:
    case EEOperation::MultiplyAddWord:
    case EEOperation::MultiplyAddUnsignedWord:
    case EEOperation::MultiplyAddWord1:
    case EEOperation::MultiplyAddUnsignedWord1:
    {
      if (!requireWordValue(
            instruction.sourceRegister,
            address,
            instruction.raw) ||
          !requireWordValue(
            instruction.targetRegister,
            address,
            instruction.raw))
      {
        return false;
      }
      const bool pipeline1 =
        instruction.operation == EEOperation::MultiplyWord1 ||
        instruction.operation ==
          EEOperation::MultiplyUnsignedWord1 ||
        instruction.operation == EEOperation::MultiplyAddWord1 ||
        instruction.operation ==
          EEOperation::MultiplyAddUnsignedWord1;
      const bool signedOperands =
        instruction.operation == EEOperation::MultiplyWord ||
        instruction.operation == EEOperation::MultiplyWord1 ||
        instruction.operation == EEOperation::MultiplyAddWord ||
        instruction.operation == EEOperation::MultiplyAddWord1;
      const bool accumulate =
        instruction.operation == EEOperation::MultiplyAddWord ||
        instruction.operation ==
          EEOperation::MultiplyAddUnsignedWord ||
        instruction.operation == EEOperation::MultiplyAddWord1 ||
        instruction.operation ==
          EEOperation::MultiplyAddUnsignedWord1;
      std::uint64_t result = multiplyWords(
        static_cast<std::uint32_t>(source),
        static_cast<std::uint32_t>(target),
        signedOperands);
      if (accumulate)
      {
        result += pipeline1
          ? accumulatorValue(hi1Register, lo1Register)
          : accumulatorValue(hiRegister, loRegister);
      }
      startPendingMultiplyDivide(
        pipeline1,
        MULTIPLY_LATENCY,
        signExtendWord(static_cast<std::uint32_t>(result >> 32)),
        signExtendWord(static_cast<std::uint32_t>(result)),
        destination,
        true);
      return true;
    }
    case EEOperation::DivideWord:
    case EEOperation::DivideUnsignedWord:
    case EEOperation::DivideWord1:
    case EEOperation::DivideUnsignedWord1:
    {
      if (!requireWordValue(
            instruction.sourceRegister,
            address,
            instruction.raw) ||
          !requireWordValue(
            instruction.targetRegister,
            address,
            instruction.raw))
      {
        return false;
      }
      const std::uint32_t dividend =
        static_cast<std::uint32_t>(source);
      const std::uint32_t divisor =
        static_cast<std::uint32_t>(target);
      if (divisor == 0)
      {
        return stopUndefinedOperation(
          address,
          instruction.raw);
      }
      const bool pipeline1 =
        instruction.operation == EEOperation::DivideWord1 ||
        instruction.operation == EEOperation::DivideUnsignedWord1;
      const bool signedOperands =
        instruction.operation == EEOperation::DivideWord ||
        instruction.operation == EEOperation::DivideWord1;
      std::uint32_t quotient = 0;
      std::uint32_t remainder = 0;
      if (signedOperands &&
          dividend == UINT32_C(0x80000000) &&
          divisor == UINT32_MAX)
      {
        quotient = dividend;
      }
      else if (signedOperands)
      {
        const std::int64_t signedDividend =
          signedWord(dividend);
        const std::int64_t signedDivisor =
          signedWord(divisor);
        quotient = static_cast<std::uint32_t>(
          signedDividend / signedDivisor);
        remainder = static_cast<std::uint32_t>(
          signedDividend % signedDivisor);
      }
      else
      {
        quotient = dividend / divisor;
        remainder = dividend % divisor;
      }
      startPendingMultiplyDivide(
        pipeline1,
        DIVIDE_LATENCY,
        signExtendWord(remainder),
        signExtendWord(quotient),
        0,
        false);
      return true;
    }
  }

  return stopUndefinedOperation(
    address,
    instruction.raw);
}

bool EECore::requireWordValue(
  std::uint8_t registerIndex,
  std::uint32_t address,
  std::uint32_t instruction)
{
  if (isWordValue(generalRegisters[registerIndex].low))
  {
    return true;
  }
  return stopUndefinedOperation(address, instruction);
}

void EECore::writeLowDoubleword(
  std::uint8_t registerIndex,
  std::uint64_t value)
{
  if (registerIndex != 0)
  {
    generalRegisters[registerIndex].low = value;
  }
}

void EECore::writeWord(
  std::uint8_t registerIndex,
  std::uint32_t value)
{
  writeLowDoubleword(registerIndex, signExtendWord(value));
}

bool EECore::raiseArithmeticOverflow(
  std::uint32_t address,
  std::uint32_t instruction)
{
  enterException(
    EEException::ArithmeticOverflow,
    address,
    address,
    instruction);
  return false;
}

bool EECore::requireCOP1Usable(
  std::uint32_t address,
  std::uint32_t instruction)
{
  if ((cop0Status & EECOP0Status::COP1_USABLE) != 0)
  {
    return true;
  }
  cop0Cause =
    (cop0Cause & ~EECOP0Cause::COPROCESSOR_ERROR_MASK) |
    EECOP0Cause::COPROCESSOR_1;
  enterException(
    EEException::CoprocessorUnusable,
    address,
    address,
    instruction);
  return false;
}

bool EECore::stopUndefinedOperation(
  std::uint32_t address,
  std::uint32_t instruction)
{
  pc = address;
  state = EEExecutionState::Halted;
  haltReason = EEStopReason::UndefinedOperation;
  rejectedInstructionValue = instruction;
  return false;
}

bool EECore::pendingMultiplyDivideActive() const
{
  return pendingMac0.active || pendingMac1.active;
}

void EECore::advancePendingMultiplyDivide(
  PendingMultiplyDivide *operation,
  bool pipeline1)
{
  if (!operation->active)
  {
    return;
  }
  --operation->remainingCycles;
  if (operation->remainingCycles != 0)
  {
    return;
  }

  if (pipeline1)
  {
    hi1Register = operation->hiResult;
    lo1Register = operation->loResult;
  }
  else
  {
    hiRegister = operation->hiResult;
    loRegister = operation->loResult;
  }
  if (operation->writeGeneralRegister)
  {
    writeLowDoubleword(
      operation->generalRegister,
      operation->generalRegisterResult);
  }
  *operation = {};
}

void EECore::startPendingMultiplyDivide(
  bool pipeline1,
  std::uint8_t latency,
  std::uint64_t hiResult,
  std::uint64_t loResult,
  std::uint8_t generalRegister,
  bool writeGeneralRegister)
{
  PendingMultiplyDivide &operation =
    pipeline1 ? pendingMac1 : pendingMac0;
  operation.active = true;
  operation.remainingCycles = latency;
  operation.hiResult = hiResult;
  operation.loResult = loResult;
  operation.writeGeneralRegister = writeGeneralRegister;
  operation.generalRegister = generalRegister;
  operation.generalRegisterResult = loResult;
}

bool EECore::completePendingCOP1Load(
  std::uint8_t *registerIndex)
{
  if (!pendingCOP1Load.active)
  {
    return false;
  }
  *registerIndex = pendingCOP1Load.registerIndex;
  floatingPointRegisters[pendingCOP1Load.registerIndex] =
    pendingCOP1Load.value;
  pendingCOP1Load = {};
  return true;
}

EECore::FPRDependency EECore::instructionFPRDependency(
  const EEInstruction &instruction,
  std::uint8_t registerIndex)
{
  switch (instruction.operation)
  {
    case EEOperation::MoveWordFromCOP1:
      return instruction.destinationRegister == registerIndex ?
        FPRDependency::Read :
        FPRDependency::None;
    case EEOperation::MoveWordToCOP1:
      return instruction.destinationRegister == registerIndex ?
        FPRDependency::Write :
        FPRDependency::None;
    case EEOperation::StoreWordFromCOP1:
      return instruction.targetRegister == registerIndex ?
        FPRDependency::Read :
        FPRDependency::None;
    case EEOperation::LoadWordToCOP1:
      return instruction.targetRegister == registerIndex ?
        FPRDependency::Write :
        FPRDependency::None;
    case EEOperation::AbsoluteSingleCOP1:
    case EEOperation::MoveSingleCOP1:
    case EEOperation::NegateSingleCOP1:
      if (instruction.destinationRegister == registerIndex)
      {
        return FPRDependency::Read;
      }
      return instruction.shiftAmount == registerIndex ?
        FPRDependency::Write :
        FPRDependency::None;
    default:
      return FPRDependency::None;
  }
}

bool EECore::validateShiftAmountOrdering(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  const bool restore =
    instruction.operation == EEOperation::MoveToShiftAmount;
  const bool calculate =
    instruction.operation ==
      EEOperation::MoveByteCountToShiftAmount ||
    instruction.operation ==
      EEOperation::MoveHalfwordCountToShiftAmount;
  if ((restore && (recentShiftAmountAccesses & 0x07) != 0) ||
      (calculate && (recentShiftAmountReads & 0x07) != 0))
  {
    return stopUndefinedOperation(address, instruction.raw);
  }
  return true;
}

void EECore::recordShiftAmountAccess(
  const EEInstruction &instruction)
{
  const bool reads =
    instruction.operation == EEOperation::MoveFromShiftAmount;
  const bool accesses =
    reads ||
    instruction.operation ==
      EEOperation::MoveByteCountToShiftAmount ||
    instruction.operation ==
      EEOperation::MoveHalfwordCountToShiftAmount;
  recentShiftAmountAccesses =
    static_cast<std::uint8_t>(
      ((recentShiftAmountAccesses << 1) |
       (accesses ? 1 : 0)) & 0x07);
  recentShiftAmountReads =
    static_cast<std::uint8_t>(
      ((recentShiftAmountReads << 1) |
       (reads ? 1 : 0)) & 0x07);
}

bool EECore::validateDelaySlotInstruction(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  if (!branchDelayPending)
  {
    return true;
  }
  if (isEEBranchOperation(instruction.operation) ||
       instruction.operation == EEOperation::ExceptionReturn ||
       (branchDelayFromLikely &&
        writesShiftAmount(instruction.operation)))
  {
    return stopUndefinedOperation(address, instruction.raw);
  }
  return true;
}

void EECore::scheduleBranch(
  bool condition,
  bool likely,
  std::uint32_t target,
  std::uint32_t address)
{
  recordCycleTrace(
    CycleTraceKind::BranchScheduled,
    address,
    target,
    (condition ? UINT64_C(1) : 0) |
      (likely ? UINT64_C(2) : 0));
  if (likely && !condition)
  {
    pc = address + 8;
    return;
  }
  branchDelayPending = true;
  branchDelayTarget = condition ? target : address + 8;
  branchInstructionAddress = address;
  branchDelayFromLikely = likely;
}

void EECore::recordCycleTrace(
  CycleTraceKind kind,
  std::uint64_t value0,
  std::uint64_t value1,
  std::uint64_t value2,
  std::uint64_t value3)
{
  if (!cycleTraceEnabled)
  {
    return;
  }
  if (cycleTraceEventCount >= cycleTraceEvents.size())
  {
    throw std::logic_error(
      "EE produced too many trace events in one cycle.");
  }
  cycleTraceEvents[cycleTraceEventCount++] = {
    kind,
    value0,
    value1,
    value2,
    value3
  };
}

void EECore::recordMemoryTrace(
  std::uint32_t address,
  std::uint8_t width,
  bool write,
  bool succeeded,
  std::uint64_t low,
  std::uint64_t high)
{
  recordCycleTrace(
    CycleTraceKind::MemoryAccess,
    address,
    low,
    high,
    width |
      (write ? UINT64_C(1) << 8 : 0) |
      (succeeded ? UINT64_C(1) << 9 : 0));
}

bool EECore::raiseDataAccessException(
  EEException type,
  std::uint32_t instructionAddress,
  std::uint32_t dataAddress,
  std::uint32_t instruction)
{
  enterException(
    type,
    instructionAddress,
    dataAddress,
    instruction);
  return false;
}

void EECore::enterException(
  EEException type,
  std::uint32_t instructionAddress,
  std::uint32_t address,
  std::uint32_t instruction)
{
  const bool alreadyExceptionLevel =
    (cop0Status & EECOP0Status::EXCEPTION_LEVEL) != 0;
  if (!alreadyExceptionLevel)
  {
    const bool delaySlot = branchDelayPending;
    cop0EPC = delaySlot
      ? branchInstructionAddress
      : instructionAddress;
    if (delaySlot)
    {
      cop0Cause |= EECOP0Cause::BRANCH_DELAY;
    }
    else
    {
      cop0Cause &= ~EECOP0Cause::BRANCH_DELAY;
    }
  }
  cop0Cause =
    (cop0Cause & ~EECOP0Cause::EXCEPTION_CODE_MASK) |
    (static_cast<std::uint32_t>(exceptionCode(type)) << 2);
  cop0Status |= EECOP0Status::EXCEPTION_LEVEL;
  if (type == EEException::AddressErrorLoadOrFetch ||
      type == EEException::AddressErrorStore)
  {
    cop0BadVAddr = address;
  }

  exception = type;
  faultAddress = address;
  pc = exceptionVector(type, alreadyExceptionLevel);
  state = EEExecutionState::Running;
  haltReason = EEStopReason::None;
  rejectedInstructionValue = instruction;
  exceptionEnteredThisCycle = true;
  if (type == EEException::Interrupt)
  {
    recordCycleTrace(
      CycleTraceKind::InterruptDelivered,
      instructionAddress,
      cop0Status,
      cop0Cause,
      pc);
  }
  recordCycleTrace(
    CycleTraceKind::ExceptionEntered,
    static_cast<std::uint8_t>(type),
    address,
    pc,
    cop0Cause);
  branchDelayPending = false;
  branchDelayTarget = 0;
  branchInstructionAddress = 0;
  branchDelayFromLikely = false;
}

std::uint8_t EECore::exceptionCode(EEException type)
{
  switch (type)
  {
    case EEException::Interrupt:
      return EEExceptionCode::INTERRUPT;
    case EEException::AddressErrorLoadOrFetch:
      return EEExceptionCode::ADDRESS_ERROR_LOAD_OR_FETCH;
    case EEException::AddressErrorStore:
      return EEExceptionCode::ADDRESS_ERROR_STORE;
    case EEException::InstructionBusError:
      return EEExceptionCode::INSTRUCTION_BUS_ERROR;
    case EEException::DataBusErrorLoad:
    case EEException::DataBusErrorStore:
      return EEExceptionCode::DATA_BUS_ERROR;
    case EEException::SystemCall:
      return EEExceptionCode::SYSTEM_CALL;
    case EEException::Breakpoint:
      return EEExceptionCode::BREAKPOINT;
    case EEException::ReservedInstruction:
      return EEExceptionCode::RESERVED_INSTRUCTION;
    case EEException::CoprocessorUnusable:
      return EEExceptionCode::COPROCESSOR_UNUSABLE;
    case EEException::ArithmeticOverflow:
      return EEExceptionCode::ARITHMETIC_OVERFLOW;
    case EEException::None:
      break;
  }
  throw std::invalid_argument(
    "EE exception type has no architectural code.");
}

std::uint32_t EECore::exceptionVector(
  EEException type,
  bool alreadyExceptionLevel) const
{
  const bool bootstrap =
    (cop0Status &
      EECOP0Status::BOOTSTRAP_EXCEPTION_VECTOR) != 0;
  const bool interrupt =
    type == EEException::Interrupt &&
    !alreadyExceptionLevel;
  if (bootstrap)
  {
    return interrupt
      ? EEExceptionVector::BOOTSTRAP_INTERRUPT
      : EEExceptionVector::BOOTSTRAP_GENERAL;
  }
  return interrupt
    ? EEExceptionVector::INTERRUPT
    : EEExceptionVector::GENERAL;
}

void EECore::setInterruptLines(bool intc, bool dmac)
{
  cop0Cause &=
    ~(EECOP0Cause::INTC_PENDING |
      EECOP0Cause::DMAC_PENDING);
  if (intc)
  {
    cop0Cause |= EECOP0Cause::INTC_PENDING;
  }
  if (dmac)
  {
    cop0Cause |= EECOP0Cause::DMAC_PENDING;
  }
}

bool EECore::interruptDeliverable() const
{
  constexpr std::uint32_t interruptLines =
    EECOP0Cause::INTC_PENDING |
    EECOP0Cause::DMAC_PENDING;
  constexpr std::uint32_t blockedLevels =
    EECOP0Status::EXCEPTION_LEVEL |
    EECOP0Status::ERROR_LEVEL;
  return
    (cop0Status & EECOP0Status::INTERRUPT_ENABLE) != 0 &&
    (cop0Status &
      EECOP0Status::MASTER_INTERRUPT_ENABLE) != 0 &&
    (cop0Status & blockedLevels) == 0 &&
    (cop0Cause & cop0Status & interruptLines) != 0;
}

EEExecutionState EECore::executionState() const
{
  return state;
}

EEStopReason EECore::stopReason() const
{
  return haltReason;
}

std::uint64_t EECore::elapsedCycles() const
{
  return cycles;
}

std::uint64_t EECore::stateHash() const
{
  std::uint64_t hash = EE_STATE_FNV_OFFSET_BASIS;
  for (const EERegister128 &value : generalRegisters)
  {
    hashEEStateValue(&hash, value.low);
    hashEEStateValue(&hash, value.high);
  }
  for (const std::uint32_t value : floatingPointRegisters)
  {
    hashEEStateValue(&hash, value);
  }
  hashEEStateValue(&hash, floatingPointAccumulatorRegister);
  hashEEStateValue(&hash, cop1StatusRegister);
  hashEEStateValue(&hash, pc);
  hashEEStateValue(&hash, hiRegister);
  hashEEStateValue(&hash, loRegister);
  hashEEStateValue(&hash, hi1Register);
  hashEEStateValue(&hash, lo1Register);
  hashEEStateValue(&hash, saRegister);
  hashEEStateValue(&hash, cop0BadVAddr);
  hashEEStateValue(&hash, cop0Count);
  hashEEStateValue(&hash, cop0Compare);
  hashEEStateValue(&hash, cop0Status);
  hashEEStateValue(&hash, cop0Cause);
  hashEEStateValue(&hash, cop0EPC);
  hashEEStateValue(&hash, cop0ErrorEPC);
  hashEEStateValue(
    &hash,
    static_cast<std::uint8_t>(exception));
  hashEEStateValue(&hash, faultAddress);
  hashEEStateValue(&hash, static_cast<std::uint8_t>(state));
  hashEEStateValue(
    &hash,
    static_cast<std::uint8_t>(haltReason));
  hashEEStateValue(&hash, cycles);
  hashEEStateValue(&hash, lastInstructionValid);
  hashEEStateValue(&hash, lastAddress);
  hashEEStateValue(&hash, lastDecodedInstruction.raw);
  hashEEStateValue(&hash, rejectedInstructionValue);
  const auto hashPending =
    [&hash](const PendingMultiplyDivide &operation)
    {
      hashEEStateValue(&hash, operation.active);
      hashEEStateValue(&hash, operation.remainingCycles);
      hashEEStateValue(&hash, operation.hiResult);
      hashEEStateValue(&hash, operation.loResult);
      hashEEStateValue(
        &hash,
        operation.writeGeneralRegister);
      hashEEStateValue(&hash, operation.generalRegister);
      hashEEStateValue(
        &hash,
        operation.generalRegisterResult);
    };
  hashPending(pendingMac0);
  hashPending(pendingMac1);
  hashEEStateValue(&hash, pendingCOP1Load.active);
  hashEEStateValue(&hash, pendingCOP1Load.registerIndex);
  hashEEStateValue(&hash, pendingCOP1Load.value);
  hashEEStateValue(&hash, recentShiftAmountAccesses);
  hashEEStateValue(&hash, recentShiftAmountReads);
  hashEEStateValue(&hash, branchDelayPending);
  hashEEStateValue(&hash, branchDelayTarget);
  hashEEStateValue(&hash, branchInstructionAddress);
  hashEEStateValue(&hash, branchDelayFromLikely);
  return hash;
}

bool EECore::hasLastInstruction() const
{
  return lastInstructionValid;
}

std::uint32_t EECore::lastInstructionAddress() const
{
  if (!lastInstructionValid)
  {
    throw std::logic_error(
      "EE Core has no decoded instruction.");
  }
  return lastAddress;
}

const EEInstruction &EECore::lastInstruction() const
{
  if (!lastInstructionValid)
  {
    throw std::logic_error(
      "EE Core has no decoded instruction.");
  }
  return lastDecodedInstruction;
}

std::uint32_t EECore::rejectedInstruction() const
{
  return rejectedInstructionValue;
}

const EERegister128 &EECore::generalRegister(
  std::size_t index) const
{
  requireGeneralRegisterIndex(index);
  return generalRegisters[index];
}

void EECore::setGeneralRegister(
  std::size_t index,
  const EERegister128 &value)
{
  requireGeneralRegisterIndex(index);
  if (index != 0)
  {
    generalRegisters[index] = value;
  }
}

std::uint32_t EECore::floatingPointRegister(
  std::size_t index) const
{
  requireFloatingPointRegisterIndex(index);
  return floatingPointRegisters[index];
}

void EECore::setFloatingPointRegister(
  std::size_t index,
  std::uint32_t value)
{
  requireFloatingPointRegisterIndex(index);
  floatingPointRegisters[index] = value;
}

std::uint32_t EECore::floatingPointAccumulator() const
{
  return floatingPointAccumulatorRegister;
}

void EECore::setFloatingPointAccumulator(
  std::uint32_t value)
{
  floatingPointAccumulatorRegister = value;
}

std::uint32_t EECore::cop1ControlRegister(
  std::size_t index) const
{
  switch (index)
  {
    case EECOP1Control::IMPLEMENTATION_REVISION_REGISTER:
      return EECOP1Control::IMPLEMENTATION_REVISION;
    case EECOP1Control::STATUS_REGISTER:
      return
        cop1StatusRegister |
        EECOP1Control::STATUS_FIXED;
    default:
      throw std::out_of_range(
        "EE COP1 control register is not implemented.");
  }
}

void EECore::setCOP1ControlRegister(
  std::size_t index,
  std::uint32_t value)
{
  switch (index)
  {
    case EECOP1Control::IMPLEMENTATION_REVISION_REGISTER:
      return;
    case EECOP1Control::STATUS_REGISTER:
      cop1StatusRegister =
        value & EECOP1Control::STATUS_WRITABLE_MASK;
      return;
    default:
      throw std::out_of_range(
        "EE COP1 control register is not implemented.");
  }
}

void EECore::updateCOP1ArithmeticFlags(
  std::uint8_t affectedFlags,
  std::uint8_t raisedFlags)
{
  constexpr std::uint8_t SUPPORTED_FLAGS =
    FP_FLAG_I_BIT |
    FP_FLAG_D_BIT |
    FP_FLAG_OVERFLOW |
    FP_FLAG_UNDERFLOW;
  if ((affectedFlags & ~SUPPORTED_FLAGS) != 0 ||
      (raisedFlags & ~affectedFlags) != 0)
  {
    throw std::invalid_argument(
      "Invalid EE COP1 arithmetic flag update.");
  }

  struct FlagMapping
  {
    std::uint8_t resultFlag;
    std::uint32_t causeFlag;
    std::uint32_t stickyFlag;
  };
  constexpr FlagMapping FLAG_MAPPINGS[] = {
    {
      FP_FLAG_I_BIT,
      EECOP1Control::CAUSE_INVALID,
      EECOP1Control::STICKY_INVALID
    },
    {
      FP_FLAG_D_BIT,
      EECOP1Control::CAUSE_DIVISION_BY_ZERO,
      EECOP1Control::STICKY_DIVISION_BY_ZERO
    },
    {
      FP_FLAG_OVERFLOW,
      EECOP1Control::CAUSE_OVERFLOW,
      EECOP1Control::STICKY_OVERFLOW
    },
    {
      FP_FLAG_UNDERFLOW,
      EECOP1Control::CAUSE_UNDERFLOW,
      EECOP1Control::STICKY_UNDERFLOW
    }
  };

  for (const FlagMapping &mapping : FLAG_MAPPINGS)
  {
    if ((affectedFlags & mapping.resultFlag) == 0)
    {
      continue;
    }
    cop1StatusRegister &= ~mapping.causeFlag;
    if ((raisedFlags & mapping.resultFlag) != 0)
    {
      cop1StatusRegister |=
        mapping.causeFlag |
        mapping.stickyFlag;
    }
  }
}

std::uint32_t EECore::programCounter() const
{
  return pc;
}

void EECore::setProgramCounter(std::uint32_t value)
{
  pc = value;
}

std::uint64_t EECore::hi() const
{
  return hiRegister;
}

void EECore::setHI(std::uint64_t value)
{
  hiRegister = value;
}

std::uint64_t EECore::lo() const
{
  return loRegister;
}

void EECore::setLO(std::uint64_t value)
{
  loRegister = value;
}

std::uint64_t EECore::hi1() const
{
  return hi1Register;
}

void EECore::setHI1(std::uint64_t value)
{
  hi1Register = value;
}

std::uint64_t EECore::lo1() const
{
  return lo1Register;
}

void EECore::setLO1(std::uint64_t value)
{
  lo1Register = value;
}

std::uint32_t EECore::shiftAmount() const
{
  return saRegister;
}

void EECore::setShiftAmount(std::uint32_t value)
{
  saRegister = value;
}

std::uint32_t EECore::cop0Register(
  EECOP0Register registerIndex) const
{
  switch (registerIndex)
  {
    case EECOP0Register::BadVAddr:
      return cop0BadVAddr;
    case EECOP0Register::Count:
      return cop0Count;
    case EECOP0Register::Compare:
      return cop0Compare;
    case EECOP0Register::Status:
      return cop0Status;
    case EECOP0Register::Cause:
      return cop0Cause;
    case EECOP0Register::EPC:
      return cop0EPC;
    case EECOP0Register::ErrorEPC:
      return cop0ErrorEPC;
  }
  throw std::out_of_range(
    "EE COP0 register is not implemented.");
}

void EECore::setCOP0Register(
  EECOP0Register registerIndex,
  std::uint32_t value)
{
  switch (registerIndex)
  {
    case EECOP0Register::BadVAddr:
      cop0BadVAddr = value;
      return;
    case EECOP0Register::Count:
      cop0Count = value;
      return;
    case EECOP0Register::Compare:
      cop0Compare = value;
      return;
    case EECOP0Register::Status:
      cop0Status = value;
      return;
    case EECOP0Register::Cause:
      cop0Cause = value;
      return;
    case EECOP0Register::EPC:
      cop0EPC = value;
      return;
    case EECOP0Register::ErrorEPC:
      cop0ErrorEPC = value;
      return;
  }
  throw std::out_of_range(
    "EE COP0 register is not implemented.");
}

bool EECore::exceptionPending() const
{
  return exception != EEException::None;
}

EEException EECore::pendingException() const
{
  return exception;
}

std::uint32_t EECore::exceptionAddress() const
{
  return faultAddress;
}

void EECore::clearPendingException()
{
  exception = EEException::None;
  faultAddress = 0;
}

void EECore::requireFloatingPointRegisterIndex(
  std::size_t index)
{
  if (index >= FLOATING_POINT_REGISTER_COUNT)
  {
    throw std::out_of_range(
      "EE floating-point register index is out of range.");
  }
}

void EECore::requireGeneralRegisterIndex(
  std::size_t index)
{
  if (index >= GENERAL_REGISTER_COUNT)
  {
    throw std::out_of_range(
      "EE general-purpose register index is out of range.");
  }
}

EEBus &EECore::attachedBus() const
{
  if (bus == nullptr)
  {
    throw std::logic_error(
      "EE Core bus is not attached.");
  }
  return *bus;
}

VPU &EECore::attachedVU0() const
{
  if (vu0 == nullptr)
  {
    throw std::logic_error(
      "EE Core VU0 is not attached.");
  }
  return *vu0;
}

VPU &EECore::attachedVU1() const
{
  if (vu1 == nullptr)
  {
    throw std::logic_error(
      "EE Core VU1 is not attached.");
  }
  return *vu1;
}

bool EECore::readCOP2ControlRegister(
  std::uint8_t index,
  std::uint32_t *value) const
{
  if (value == nullptr)
  {
    throw std::invalid_argument(
      "COP2 control read requires an output.");
  }
  if (index < 16)
  {
    *value = attachedVU0().intRegisterValue(index);
    return true;
  }
  switch (index)
  {
    case 16:
      *value = attachedVU0().statusFlagsValue();
      return true;
    case 17:
      *value = attachedVU0().macFlagsValue();
      return true;
    case 18:
      *value = attachedVU0().clippingFlagsValue();
      return true;
    case 20:
      *value = attachedVU0().randomRegisterValue();
      return true;
    case 21:
      *value = attachedVU0().iRegisterBits();
      return true;
    case 22:
      *value = attachedVU0().qRegisterBits();
      return true;
    case 26:
      if (!attachedVU0().hasTerminationPosition())
      {
        return false;
      }
      *value = attachedVU0().terminationPosition();
      return true;
    case 27:
      *value = attachedVU0().callAddressRegister();
      return true;
    case 28:
      *value =
        (attachedVU0().dBitEnabled() ? UINT32_C(1) << 2 : 0) |
        (attachedVU0().tBitEnabled() ? UINT32_C(1) << 3 : 0) |
        (attachedVU1().dBitEnabled() ? UINT32_C(1) << 10 : 0) |
        (attachedVU1().tBitEnabled() ? UINT32_C(1) << 11 : 0);
      return true;
    case 29:
      *value = vpuStatusRegister();
      return true;
    default:
      return false;
  }
}

bool EECore::writeCOP2ControlRegister(
  std::uint8_t index,
  std::uint32_t value)
{
  if (index < 16)
  {
    attachedVU0().loadIntRegister(
      index,
      static_cast<std::uint16_t>(value));
    return true;
  }
  switch (index)
  {
    case 16:
      attachedVU0().setStatusFlagsValue(value);
      return true;
    case 18:
      attachedVU0().setClippingFlagsValue(value);
      return true;
    case 20:
      if (attachedVU0().microModeActive())
      {
        return false;
      }
      attachedVU0().setRandomRegisterValue(value);
      return true;
    case 21:
      if (attachedVU0().microModeActive())
      {
        return false;
      }
      attachedVU0().setIRegisterBits(value);
      return true;
    case 22:
      if (attachedVU0().microModeActive())
      {
        return false;
      }
      attachedVU0().setQRegisterBits(value);
      return true;
    case 27:
      attachedVU0().setCallAddressRegister(value);
      return true;
    case 28:
      if ((value & (UINT32_C(1) << 1)) != 0)
      {
        attachedVU0().resetFromControl();
      }
      if ((value & (UINT32_C(1) << 9)) != 0)
      {
        attachedVU1().resetFromControl();
      }
      if ((value & UINT32_C(1)) != 0)
      {
        attachedVU0().forceBreak();
      }
      if ((value & (UINT32_C(1) << 8)) != 0)
      {
        attachedVU1().forceBreak();
      }
      attachedVU0().setDBitEnabled(
        (value & (UINT32_C(1) << 2)) != 0);
      attachedVU0().setTBitEnabled(
        (value & (UINT32_C(1) << 3)) != 0);
      attachedVU1().setDBitEnabled(
        (value & (UINT32_C(1) << 10)) != 0);
      attachedVU1().setTBitEnabled(
        (value & (UINT32_C(1) << 11)) != 0);
      return true;
    case 31:
    {
      if (attachedVU1().clockActive())
      {
        return true;
      }
      const std::uint16_t startAddress =
        static_cast<std::uint16_t>(value);
      if ((startAddress & 7) != 0 ||
          startAddress > attachedVU1().microMemorySize() - 8)
      {
        return false;
      }
      attachedVU1().startMicroMode(startAddress);
      return true;
    }
    default:
      return false;
  }
}

std::uint32_t EECore::vpuStatusRegister() const
{
  std::uint32_t value = 0;
  const auto addUnitStatus =
    [&value](const VPU &vpu, std::uint8_t shift)
    {
      if (vpu.microModeActive())
      {
        value |= UINT32_C(1) << shift;
      }
      if (vpu.stoppedByDBit())
      {
        value |= UINT32_C(1) << (shift + 1);
      }
      if (vpu.stoppedByTBit())
      {
        value |= UINT32_C(1) << (shift + 2);
      }
      if (vpu.stoppedByForceBreak())
      {
        value |= UINT32_C(1) << (shift + 3);
      }
    };
  addUnitStatus(attachedVU0(), 0);
  addUnitStatus(attachedVU1(), 8);
  if (attachedVU0().divisionUnitBusy())
  {
    value |= UINT32_C(1) << 5;
  }
  if (attachedVU1().waitingForXGKICK())
  {
    value |= UINT32_C(1) << 12;
  }
  if (attachedVU1().divisionUnitBusy())
  {
    value |= UINT32_C(1) << 13;
  }
  if (attachedVU1().elementaryFunctionUnitBusy())
  {
    value |= UINT32_C(1) << 14;
  }
  return value;
}

EEInstructionFetchResult EECore::raiseFetchException(
  EEException type,
  std::uint32_t address)
{
  exception = type;
  faultAddress = address;
  return {false, address, 0};
}
