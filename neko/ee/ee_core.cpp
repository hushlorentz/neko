#include "ee_core.hpp"

#include <limits>
#include <stdexcept>

#include "ee_bus.hpp"

constexpr std::size_t EECore::GENERAL_REGISTER_COUNT;

namespace
{
  constexpr std::uint64_t WORD_SIGN_BIT =
    UINT64_C(0x80000000);
  constexpr std::uint64_t DOUBLEWORD_SIGN_BIT =
    UINT64_C(0x8000000000000000);

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
  pc = 0;
  hiRegister = 0;
  loRegister = 0;
  hi1Register = 0;
  lo1Register = 0;
  saRegister = 0;
  exception = EEException::None;
  faultAddress = 0;
  state = EEExecutionState::Halted;
  haltReason = EEStopReason::None;
  cycles = 0;
  lastInstructionValid = false;
  lastAddress = 0;
  lastDecodedInstruction = {};
  rejectedInstructionValue = 0;
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

EEInstructionFetchResult EECore::fetchInstruction()
{
  const std::uint32_t address = pc;
  if (exceptionPending())
  {
    return {false, address, 0};
  }
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
  pc = startAddress;
  clearPendingException();
  state = EEExecutionState::Running;
  haltReason = EEStopReason::None;
  lastInstructionValid = false;
  lastAddress = 0;
  lastDecodedInstruction = {};
  rejectedInstructionValue = 0;
}

void EECore::haltExecution()
{
  state = EEExecutionState::Halted;
  haltReason = EEStopReason::HostHalt;
}

bool EECore::clockActive() const
{
  return state == EEExecutionState::Running;
}

void EECore::clock()
{
  if (!clockActive())
  {
    return;
  }

  ++cycles;
  const EEInstructionFetchResult fetched =
    fetchInstruction();
  if (!fetched.succeeded)
  {
    state = EEExecutionState::Halted;
    haltReason = EEStopReason::FetchException;
    return;
  }

  EEInstruction decoded;
  try
  {
    decoded = decodeEEInstruction(fetched.instruction);
  }
  catch (const EEInstructionDecodeError &error)
  {
    pc = fetched.address;
    state = EEExecutionState::Halted;
    haltReason =
      error.failure() == EEInstructionDecodeFailure::Reserved
        ? EEStopReason::ReservedInstruction
        : EEStopReason::UnsupportedInstruction;
    rejectedInstructionValue = fetched.instruction;
    return;
  }

  if (!executeInstruction(decoded, fetched.address))
  {
    return;
  }

  lastDecodedInstruction = decoded;
  lastAddress = fetched.address;
  lastInstructionValid = true;
}

bool EECore::executeInstruction(
  const EEInstruction &instruction,
  std::uint32_t address)
{
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
  pc = address;
  exception = EEException::ArithmeticOverflow;
  faultAddress = address;
  state = EEExecutionState::Halted;
  haltReason = EEStopReason::ExecutionException;
  rejectedInstructionValue = instruction;
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

EEInstructionFetchResult EECore::raiseFetchException(
  EEException type,
  std::uint32_t address)
{
  exception = type;
  faultAddress = address;
  return {false, address, 0};
}
