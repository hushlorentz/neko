#include "ee_core.hpp"

#include <cstring>
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
  constexpr std::uint8_t MULTIPLY_LATENCY = 4;
  constexpr std::uint8_t DIVIDE_LATENCY = 37;

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
  pendingMac0 = {};
  pendingMac1 = {};
  recentShiftAmountAccesses = 0;
  recentShiftAmountReads = 0;
  branchDelayPending = false;
  branchDelayTarget = 0;
  branchInstructionAddress = 0;
  branchDelayFromLikely = false;
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
    setExceptionRestartAddress(fetched.address);
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
    setExceptionRestartAddress(fetched.address);
    state = EEExecutionState::Halted;
    haltReason =
      error.failure() == EEInstructionDecodeFailure::Reserved
        ? EEStopReason::ReservedInstruction
        : EEStopReason::UnsupportedInstruction;
    rejectedInstructionValue = fetched.instruction;
    return;
  }

  const bool wasDelaySlot = branchDelayPending;
  const std::uint32_t completedBranchTarget =
    branchDelayTarget;
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
      if (!attachedBus().readData8(dataAddress, &value))
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
      if (!attachedBus().writeData8(
            dataAddress,
            static_cast<std::uint8_t>(target)))
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
      if (!attachedBus().readData16(dataAddress, &value))
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
      if (!attachedBus().writeData16(
            dataAddress,
            static_cast<std::uint16_t>(target)))
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
      if (!attachedBus().readData32(dataAddress, &value))
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
      if (!attachedBus().writeData32(
            dataAddress,
            static_cast<std::uint32_t>(target)))
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
      if (!attachedBus().readData32(
            alignedAddress,
            &memory))
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
      if (!attachedBus().readData32(
            alignedAddress,
            &memory))
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
      if (!attachedBus().writeData32(
            alignedAddress,
            memory))
      {
        return raiseDataAccessException(
          EEException::DataBusErrorStore,
          address,
          dataAddress,
          instruction.raw);
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
  setExceptionRestartAddress(address);
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

bool EECore::raiseDataAccessException(
  EEException type,
  std::uint32_t instructionAddress,
  std::uint32_t dataAddress,
  std::uint32_t instruction)
{
  setExceptionRestartAddress(instructionAddress);
  exception = type;
  faultAddress = dataAddress;
  state = EEExecutionState::Halted;
  haltReason = EEStopReason::ExecutionException;
  rejectedInstructionValue = instruction;
  return false;
}

void EECore::setExceptionRestartAddress(
  std::uint32_t instructionAddress)
{
  if (branchDelayPending)
  {
    pc = branchInstructionAddress;
    branchDelayPending = false;
    branchDelayTarget = 0;
    branchInstructionAddress = 0;
    branchDelayFromLikely = false;
    return;
  }
  pc = instructionAddress;
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
