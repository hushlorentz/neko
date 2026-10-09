#include "iop_core.hpp"

#include <limits>
#include <stdexcept>

#include "iop_bus.hpp"
#include "iop_instruction.hpp"

namespace
{
  bool isAligned(IOPAddress address, std::size_t width)
  {
    return (address & (width - 1)) == 0;
  }

  IOPAccessOutcome accessOutcome(IOPBusStatus status)
  {
    switch (status)
    {
      case IOPBusStatus::Completed:
        return IOPAccessOutcome::Completed;
      case IOPBusStatus::Misaligned:
        return IOPAccessOutcome::Misaligned;
      case IOPBusStatus::ReadOnly:
        return IOPAccessOutcome::ReadOnly;
      case IOPBusStatus::Unmapped:
        return IOPAccessOutcome::Unmapped;
    }
    throw std::logic_error("Unknown IOP bus status.");
  }

  IOPWord signExtendedImmediate(std::uint16_t immediate)
  {
    const IOPWord value = immediate;
    return (value & UINT32_C(0x00008000)) != 0
      ? value | UINT32_C(0xffff0000)
      : value;
  }

  bool signedLessThan(IOPWord left, IOPWord right)
  {
    constexpr IOPWord SIGN_BIT = UINT32_C(0x80000000);
    return (left ^ SIGN_BIT) < (right ^ SIGN_BIT);
  }

  bool additionOverflows(
    IOPWord left,
    IOPWord right,
    IOPWord result)
  {
    constexpr IOPWord SIGN_BIT = UINT32_C(0x80000000);
    return ((~(left ^ right) & (left ^ result)) & SIGN_BIT) != 0;
  }

  bool subtractionOverflows(
    IOPWord left,
    IOPWord right,
    IOPWord result)
  {
    constexpr IOPWord SIGN_BIT = UINT32_C(0x80000000);
    return (((left ^ right) & (left ^ result)) & SIGN_BIT) != 0;
  }

  std::uint64_t signedMagnitude(IOPWord value)
  {
    return (value & UINT32_C(0x80000000)) == 0
      ? value
      : static_cast<std::uint64_t>(IOPWord(0) - value);
  }

  std::uint64_t signedProduct(IOPWord left, IOPWord right)
  {
    const std::uint64_t magnitude =
      signedMagnitude(left) * signedMagnitude(right);
    const bool negative =
      ((left ^ right) & UINT32_C(0x80000000)) != 0;
    return negative ? UINT64_C(0) - magnitude : magnitude;
  }

  struct IOPHILOResult
  {
    IOPWord hi = 0;
    IOPWord lo = 0;
  };

  IOPHILOResult signedDivision(IOPWord dividend, IOPWord divisor)
  {
    if (divisor == 0)
    {
      return {
        dividend,
        (dividend & UINT32_C(0x80000000)) == 0
          ? UINT32_C(0xffffffff)
          : 1
      };
    }
    if (dividend == UINT32_C(0x80000000) &&
        divisor == UINT32_C(0xffffffff))
    {
      return {0, UINT32_C(0x80000000)};
    }

    const IOPWord quotientMagnitude = static_cast<IOPWord>(
      signedMagnitude(dividend) / signedMagnitude(divisor));
    const IOPWord remainderMagnitude = static_cast<IOPWord>(
      signedMagnitude(dividend) % signedMagnitude(divisor));
    const bool quotientNegative =
      ((dividend ^ divisor) & UINT32_C(0x80000000)) != 0;
    const bool remainderNegative =
      (dividend & UINT32_C(0x80000000)) != 0;
    return {
      remainderNegative
        ? IOPWord(0) - remainderMagnitude
        : remainderMagnitude,
      quotientNegative
        ? IOPWord(0) - quotientMagnitude
        : quotientMagnitude
    };
  }

  IOPHILOResult unsignedDivision(IOPWord dividend, IOPWord divisor)
  {
    return divisor == 0
      ? IOPHILOResult{dividend, UINT32_C(0xffffffff)}
      : IOPHILOResult{dividend % divisor, dividend / divisor};
  }

  IOPWord arithmeticShiftRight(IOPWord value, std::uint8_t amount)
  {
    const std::uint8_t shift = amount & 0x1f;
    if (shift == 0)
    {
      return value;
    }

    const IOPWord shifted = value >> shift;
    if ((value & UINT32_C(0x80000000)) == 0)
    {
      return shifted;
    }
    return shifted |
      (UINT32_MAX << (32 - shift));
  }

  IOPInstructionEffects retiredEffects()
  {
    IOPInstructionEffects effects;
    effects.controlFlow.kind = IOPControlFlowEffect::Sequential;
    effects.completion = IOPInstructionCompletion::Retired;
    effects.stopReason = IOPStopReason::None;
    return effects;
  }

  IOPInstructionEffects destinationEffects(
    std::uint8_t destination,
    IOPWord value)
  {
    IOPInstructionEffects effects = retiredEffects();
    effects.destination = {
      IOPWriteEffect::Write,
      destination,
      value
    };
    return effects;
  }

  IOPInstructionEffects arithmeticOverflowEffects()
  {
    IOPInstructionEffects effects;
    effects.exception.kind = IOPException::ArithmeticOverflow;
    effects.stopReason = IOPStopReason::ExecutionException;
    return effects;
  }

  IOPInstructionEffects reservedInstructionEffects()
  {
    IOPInstructionEffects effects;
    effects.exception.kind = IOPException::ReservedInstruction;
    effects.stopReason = IOPStopReason::ReservedInstruction;
    return effects;
  }

  IOPInstructionEffects addressErrorEffects(IOPAddress address)
  {
    IOPInstructionEffects effects;
    effects.exception.kind = IOPException::AddressErrorLoadOrFetch;
    effects.exception.badVirtualAddress = address;
    effects.stopReason = IOPStopReason::ExecutionException;
    return effects;
  }

  IOPInstructionEffects undefinedOperationEffects()
  {
    IOPInstructionEffects effects;
    effects.stopReason = IOPStopReason::UndefinedOperation;
    return effects;
  }

  IOPInstructionEffects hiLoReadEffects(
    IOPHILOAccessEffect access,
    std::uint8_t destination,
    IOPWord value)
  {
    IOPInstructionEffects effects =
      destinationEffects(destination, value);
    effects.hiLoAccess = access;
    return effects;
  }

  IOPInstructionEffects hiLoWriteEffects(
    IOPHILOAccessEffect access,
    IOPWord hi,
    IOPWord lo)
  {
    IOPInstructionEffects effects = retiredEffects();
    effects.hiLoAccess = access;
    if (access == IOPHILOAccessEffect::WriteHI ||
        access == IOPHILOAccessEffect::WritePair)
    {
      effects.hi = {IOPWriteEffect::Write, hi};
    }
    if (access == IOPHILOAccessEffect::WriteLO ||
        access == IOPHILOAccessEffect::WritePair)
    {
      effects.lo = {IOPWriteEffect::Write, lo};
    }
    return effects;
  }

  IOPInstructionEffects branchEffects(IOPAddress target)
  {
    IOPInstructionEffects effects = retiredEffects();
    effects.controlFlow.kind = IOPControlFlowEffect::ScheduleBranch;
    effects.controlFlow.target = target;
    return effects;
  }

  IOPInstructionEffects branchLinkEffects(
    IOPAddress target,
    std::uint8_t destination,
    IOPAddress link)
  {
    IOPInstructionEffects effects =
      destinationEffects(destination, link);
    effects.controlFlow.kind = IOPControlFlowEffect::ScheduleBranch;
    effects.controlFlow.target = target;
    return effects;
  }

  IOPAddress conditionalBranchTarget(
    IOPAddress instructionAddress,
    std::uint16_t immediate,
    bool taken)
  {
    const IOPAddress delaySlot =
      instructionAddress + sizeof(IOPWord);
    return taken
      ? delaySlot + (signExtendedImmediate(immediate) << 2)
      : delaySlot + sizeof(IOPWord);
  }

  bool isControlTransfer(IOPOperation operation)
  {
    switch (operation)
    {
      case IOPOperation::JumpRegister:
      case IOPOperation::JumpAndLinkRegister:
      case IOPOperation::BranchLessThanZero:
      case IOPOperation::BranchGreaterThanOrEqualZero:
      case IOPOperation::BranchLessThanZeroAndLink:
      case IOPOperation::BranchGreaterThanOrEqualZeroAndLink:
      case IOPOperation::Jump:
      case IOPOperation::JumpAndLink:
      case IOPOperation::BranchEqual:
      case IOPOperation::BranchNotEqual:
      case IOPOperation::BranchLessThanOrEqualZero:
      case IOPOperation::BranchGreaterThanZero:
        return true;
      default:
        return false;
    }
  }

  bool pairWriteIsUndefined(const IOPHILOState &state)
  {
    return state.hiWriteHazardInstructions != 0 ||
      state.loWriteHazardInstructions != 0 ||
      state.unreadMultiplyDivideHI ||
      state.unreadMultiplyDivideLO;
  }

  bool hasUnreadMultiplyDivideResult(const IOPHILOState &state)
  {
    return state.unreadMultiplyDivideHI ||
      state.unreadMultiplyDivideLO;
  }
}

IOPCore::IOPCore()
{
  reset();
}

void IOPCore::reset()
{
  pc = IOPReset::VECTOR;
  writableGeneralRegisters.fill(0);
  hiRegister = 0;
  loRegister = 0;
  hiLoState = {};
  cop0.badVirtualAddress = 0;
  cop0.status =
    IOPCOP0Status::BOOT_EXCEPTION_VECTORS |
    IOPCOP0Status::TLB_SHUTDOWN;
  cop0.cause = 0;
  cop0.exceptionProgramCounter = 0;
  branch = {};
  delayedResult = {};
  pendingException = {};
  state = IOPExecutionState::Halted;
  haltReason = IOPStopReason::None;
  cycles = 0;
  retiredInstructionTotal = 0;
}

void IOPCore::attachBus(IOPBus *attached)
{
  bus = attached;
}

bool IOPCore::hasAttachedBus() const
{
  return bus != nullptr;
}

void IOPCore::prepareFreshExecution(
  IOPAddress entryPoint,
  IOPAddress stackPointer,
  IOPAddress returnAddress)
{
  reset();
  pc = entryPoint;
  setGeneralRegister(29, stackPointer);
  setGeneralRegister(31, returnAddress);
}

void IOPCore::startExecution(IOPAddress entryPoint)
{
  pc = entryPoint;
  branch = {};
  delayedResult = {};
  hiLoState = {};
  pendingException = {};
  state = IOPExecutionState::Running;
  haltReason = IOPStopReason::None;
}

void IOPCore::haltExecution()
{
  state = IOPExecutionState::Halted;
  haltReason = IOPStopReason::HostHalt;
}

void IOPCore::stepInstruction()
{
  if (state != IOPExecutionState::Running)
  {
    throw std::logic_error(
      "IOP Core cannot step while halted.");
  }

  const IOPAddress instructionAddress = pc;
  const IOPMemoryReadResult fetch = fetchInstruction();
  if (fetch.outcome != IOPAccessOutcome::Completed)
  {
    commitInstructionEffects(
      instructionAddress,
      fetchFailureEffects(fetch));
    return;
  }

  commitInstructionEffects(
    instructionAddress,
    instructionEffects(decodeIOPInstruction(fetch.value)));
}

IOPInstructionEffects IOPCore::instructionEffects(
  const IOPDecodeResult &decoded) const
{
  switch (decoded.disposition)
  {
    case IOPDecodeDisposition::Reserved:
      return reservedInstructionEffects();
    case IOPDecodeDisposition::ValidButDeferred:
      return {};
    case IOPDecodeDisposition::Supported:
      break;
  }

  const IOPInstruction &instruction = decoded.instruction;
  if (branch.active && isControlTransfer(instruction.operation))
  {
    return reservedInstructionEffects();
  }
  const IOPWord source =
    generalRegister(instruction.sourceRegister);
  const IOPWord target =
    generalRegister(instruction.targetRegister);
  switch (instruction.operation)
  {
    case IOPOperation::Nop:
      return retiredEffects();
    case IOPOperation::ShiftLeftLogical:
      return destinationEffects(
        instruction.destinationRegister,
        target << instruction.shiftAmount);
    case IOPOperation::ShiftRightLogical:
      return destinationEffects(
        instruction.destinationRegister,
        target >> instruction.shiftAmount);
    case IOPOperation::ShiftRightArithmetic:
      return destinationEffects(
        instruction.destinationRegister,
        arithmeticShiftRight(target, instruction.shiftAmount));
    case IOPOperation::ShiftLeftLogicalVariable:
      return destinationEffects(
        instruction.destinationRegister,
        target << (source & 0x1f));
    case IOPOperation::ShiftRightLogicalVariable:
      return destinationEffects(
        instruction.destinationRegister,
        target >> (source & 0x1f));
    case IOPOperation::ShiftRightArithmeticVariable:
      return destinationEffects(
        instruction.destinationRegister,
        arithmeticShiftRight(
          target,
          static_cast<std::uint8_t>(source)));
    case IOPOperation::JumpRegister:
      return branchEffects(source);
    case IOPOperation::JumpAndLinkRegister:
      if ((source & 3) != 0 ||
          classifyAddress(source).outcome !=
            IOPAddressOutcome::Translated)
      {
        return addressErrorEffects(source);
      }
      return branchLinkEffects(
        source,
        instruction.destinationRegister,
        pc + 2 * sizeof(IOPWord));
    case IOPOperation::Add:
    {
      const IOPWord result = source + target;
      return additionOverflows(source, target, result)
        ? arithmeticOverflowEffects()
        : destinationEffects(
            instruction.destinationRegister,
            result);
    }
    case IOPOperation::AddUnsigned:
      return destinationEffects(
        instruction.destinationRegister,
        source + target);
    case IOPOperation::Subtract:
    {
      const IOPWord result = source - target;
      return subtractionOverflows(source, target, result)
        ? arithmeticOverflowEffects()
        : destinationEffects(
            instruction.destinationRegister,
            result);
    }
    case IOPOperation::SubtractUnsigned:
      return destinationEffects(
        instruction.destinationRegister,
        source - target);
    case IOPOperation::MoveFromHI:
      return hiLoReadEffects(
        IOPHILOAccessEffect::ReadHI,
        instruction.destinationRegister,
        hiRegister);
    case IOPOperation::MoveToHI:
      if (hiLoState.hiWriteHazardInstructions != 0 ||
          hasUnreadMultiplyDivideResult(hiLoState))
      {
        return undefinedOperationEffects();
      }
      return hiLoWriteEffects(
        IOPHILOAccessEffect::WriteHI,
        source,
        0);
    case IOPOperation::MoveFromLO:
      return hiLoReadEffects(
        IOPHILOAccessEffect::ReadLO,
        instruction.destinationRegister,
        loRegister);
    case IOPOperation::MoveToLO:
      if (hiLoState.loWriteHazardInstructions != 0 ||
          hasUnreadMultiplyDivideResult(hiLoState))
      {
        return undefinedOperationEffects();
      }
      return hiLoWriteEffects(
        IOPHILOAccessEffect::WriteLO,
        0,
        source);
    case IOPOperation::Multiply:
    case IOPOperation::MultiplyUnsigned:
    {
      if (pairWriteIsUndefined(hiLoState))
      {
        return undefinedOperationEffects();
      }
      const std::uint64_t result =
        instruction.operation == IOPOperation::Multiply
          ? signedProduct(source, target)
          : static_cast<std::uint64_t>(source) * target;
      return hiLoWriteEffects(
        IOPHILOAccessEffect::WritePair,
        static_cast<IOPWord>(result >> 32),
        static_cast<IOPWord>(result));
    }
    case IOPOperation::Divide:
    case IOPOperation::DivideUnsigned:
    {
      if (pairWriteIsUndefined(hiLoState))
      {
        return undefinedOperationEffects();
      }
      const IOPHILOResult result =
        instruction.operation == IOPOperation::Divide
          ? signedDivision(source, target)
          : unsignedDivision(source, target);
      return hiLoWriteEffects(
        IOPHILOAccessEffect::WritePair,
        result.hi,
        result.lo);
    }
    case IOPOperation::And:
      return destinationEffects(
        instruction.destinationRegister,
        source & target);
    case IOPOperation::Or:
      return destinationEffects(
        instruction.destinationRegister,
        source | target);
    case IOPOperation::Xor:
      return destinationEffects(
        instruction.destinationRegister,
        source ^ target);
    case IOPOperation::Nor:
      return destinationEffects(
        instruction.destinationRegister,
        ~(source | target));
    case IOPOperation::SetLessThan:
      return destinationEffects(
        instruction.destinationRegister,
        signedLessThan(source, target) ? 1 : 0);
    case IOPOperation::SetLessThanUnsigned:
      return destinationEffects(
        instruction.destinationRegister,
        source < target ? 1 : 0);
    case IOPOperation::BranchLessThanZero:
      return branchEffects(
        conditionalBranchTarget(
          pc,
          instruction.immediate,
          (source & UINT32_C(0x80000000)) != 0));
    case IOPOperation::BranchGreaterThanOrEqualZero:
      return branchEffects(
        conditionalBranchTarget(
          pc,
          instruction.immediate,
          (source & UINT32_C(0x80000000)) == 0));
    case IOPOperation::BranchLessThanZeroAndLink:
      return branchLinkEffects(
        conditionalBranchTarget(
          pc,
          instruction.immediate,
          (source & UINT32_C(0x80000000)) != 0),
        31,
        pc + 2 * sizeof(IOPWord));
    case IOPOperation::BranchGreaterThanOrEqualZeroAndLink:
      return branchLinkEffects(
        conditionalBranchTarget(
          pc,
          instruction.immediate,
          (source & UINT32_C(0x80000000)) == 0),
        31,
        pc + 2 * sizeof(IOPWord));
    case IOPOperation::Jump:
      return branchEffects(
        ((pc + sizeof(IOPWord)) & UINT32_C(0xf0000000)) |
        (instruction.target << 2));
    case IOPOperation::JumpAndLink:
      return branchLinkEffects(
        ((pc + sizeof(IOPWord)) & UINT32_C(0xf0000000)) |
          (instruction.target << 2),
        31,
        pc + 2 * sizeof(IOPWord));
    case IOPOperation::BranchEqual:
      return branchEffects(
        conditionalBranchTarget(
          pc,
          instruction.immediate,
          source == target));
    case IOPOperation::BranchNotEqual:
      return branchEffects(
        conditionalBranchTarget(
          pc,
          instruction.immediate,
          source != target));
    case IOPOperation::BranchLessThanOrEqualZero:
      return branchEffects(
        conditionalBranchTarget(
          pc,
          instruction.immediate,
          source == 0 ||
          (source & UINT32_C(0x80000000)) != 0));
    case IOPOperation::BranchGreaterThanZero:
      return branchEffects(
        conditionalBranchTarget(
          pc,
          instruction.immediate,
          source != 0 &&
          (source & UINT32_C(0x80000000)) == 0));
    case IOPOperation::SetLessThanImmediate:
      return destinationEffects(
        instruction.targetRegister,
        signedLessThan(
          source,
          signExtendedImmediate(instruction.immediate))
          ? 1
          : 0);
    case IOPOperation::SetLessThanImmediateUnsigned:
      return destinationEffects(
        instruction.targetRegister,
        source < signExtendedImmediate(instruction.immediate)
          ? 1
          : 0);
    case IOPOperation::AddImmediate:
    {
      const IOPWord immediate =
        signExtendedImmediate(instruction.immediate);
      const IOPWord result = source + immediate;
      return additionOverflows(source, immediate, result)
        ? arithmeticOverflowEffects()
        : destinationEffects(
            instruction.targetRegister,
            result);
    }
    case IOPOperation::AddImmediateUnsigned:
      return destinationEffects(
        instruction.targetRegister,
        source + signExtendedImmediate(instruction.immediate));
    case IOPOperation::AndImmediate:
      return destinationEffects(
        instruction.targetRegister,
        source & instruction.immediate);
    case IOPOperation::OrImmediate:
      return destinationEffects(
        instruction.targetRegister,
        source | instruction.immediate);
    case IOPOperation::XorImmediate:
      return destinationEffects(
        instruction.targetRegister,
        source ^ instruction.immediate);
    case IOPOperation::LoadUpperImmediate:
      return destinationEffects(
        instruction.targetRegister,
        static_cast<IOPWord>(instruction.immediate) << 16);
    default:
      return {};
  }
}

IOPInstructionEffects IOPCore::fetchFailureEffects(
  const IOPMemoryReadResult &fetch)
{
  IOPInstructionEffects effects;
  effects.exception.kind = fetch.exception;
  effects.exception.badVirtualAddress = fetch.virtualAddress;
  effects.stopReason = IOPStopReason::FetchFailure;
  return effects;
}

void IOPCore::commitInstructionEffects(
  IOPAddress instructionAddress,
  const IOPInstructionEffects &effects)
{
  if (delayedResult.source != IOPDelayedResultSource::None)
  {
    throw std::logic_error(
      "IOP continuation commit is not implemented.");
  }
  if (effects.completion == IOPInstructionCompletion::Retired &&
      effects.controlFlow.kind == IOPControlFlowEffect::Hold)
  {
    throw std::logic_error(
      "Retired IOP instruction has no control-flow effect.");
  }
  if (cycles == std::numeric_limits<IOPCycleCount>::max())
  {
    throw std::overflow_error("IOP cycle count overflow.");
  }
  if (effects.completion == IOPInstructionCompletion::Retired &&
      retiredInstructionTotal ==
        std::numeric_limits<std::uint64_t>::max())
  {
    throw std::overflow_error(
      "IOP retired instruction count overflow.");
  }
  ++cycles;

  const bool inBranchDelaySlot = branch.active;
  if (effects.exception.kind != IOPException::None)
  {
    pendingException = effects.exception;
    pendingException.inBranchDelaySlot = inBranchDelaySlot;
    state = IOPExecutionState::Halted;
    haltReason = effects.stopReason;
    return;
  }
  if (effects.completion == IOPInstructionCompletion::Halted)
  {
    state = IOPExecutionState::Halted;
    haltReason = effects.stopReason;
    return;
  }

  if (effects.destination.kind == IOPWriteEffect::Write)
  {
    setGeneralRegister(
      effects.destination.destination,
      effects.destination.value);
  }
  if (effects.hi.kind == IOPWriteEffect::Write)
  {
    hiRegister = effects.hi.value;
  }
  if (effects.lo.kind == IOPWriteEffect::Write)
  {
    loRegister = effects.lo.value;
  }
  if (hiLoState.hiWriteHazardInstructions != 0)
  {
    --hiLoState.hiWriteHazardInstructions;
  }
  if (hiLoState.loWriteHazardInstructions != 0)
  {
    --hiLoState.loWriteHazardInstructions;
  }
  switch (effects.hiLoAccess)
  {
    case IOPHILOAccessEffect::ReadHI:
      hiLoState.hiWriteHazardInstructions = 2;
      hiLoState.unreadMultiplyDivideHI = false;
      break;
    case IOPHILOAccessEffect::ReadLO:
      hiLoState.loWriteHazardInstructions = 2;
      hiLoState.unreadMultiplyDivideLO = false;
      break;
    case IOPHILOAccessEffect::WritePair:
      hiLoState.unreadMultiplyDivideHI = true;
      hiLoState.unreadMultiplyDivideLO = true;
      break;
    case IOPHILOAccessEffect::WriteHI:
    case IOPHILOAccessEffect::WriteLO:
      hiLoState.unreadMultiplyDivideHI = false;
      hiLoState.unreadMultiplyDivideLO = false;
      break;
    case IOPHILOAccessEffect::None:
      break;
  }

  switch (effects.controlFlow.kind)
  {
    case IOPControlFlowEffect::Sequential:
      if (inBranchDelaySlot)
      {
        pc = branch.targetAddress;
        branch = {};
      }
      else
      {
        pc = instructionAddress + sizeof(IOPWord);
      }
      break;
    case IOPControlFlowEffect::ScheduleBranch:
      branch = {
        true,
        instructionAddress,
        effects.controlFlow.target
      };
      pc = instructionAddress + sizeof(IOPWord);
      break;
    case IOPControlFlowEffect::SetProgramCounter:
      pc = effects.controlFlow.target;
      break;
    case IOPControlFlowEffect::Hold:
      break;
  }

  ++retiredInstructionTotal;
  state = IOPExecutionState::Running;
  haltReason = IOPStopReason::None;
}

IOPAddressClassification IOPCore::classifyAddress(
  IOPAddress virtualAddress) const
{
  const bool userMode =
    (cop0.status & IOPCOP0Status::CURRENT_USER_MODE) != 0;
  if (userMode && virtualAddress >= UINT32_C(0x80000000))
  {
    return {
      IOPAddressOutcome::ProtectionFailure,
      virtualAddress,
      0,
      IOPCacheRoute::None
    };
  }

  if (virtualAddress < UINT32_C(0x80000000))
  {
    return {
      IOPAddressOutcome::Translated,
      virtualAddress,
      virtualAddress,
      IOPCacheRoute::Cached
    };
  }
  if (virtualAddress < UINT32_C(0xa0000000))
  {
    return {
      IOPAddressOutcome::Translated,
      virtualAddress,
      virtualAddress & UINT32_C(0x1fffffff),
      IOPCacheRoute::Cached
    };
  }
  if (virtualAddress < UINT32_C(0xc0000000))
  {
    return {
      IOPAddressOutcome::Translated,
      virtualAddress,
      virtualAddress & UINT32_C(0x1fffffff),
      IOPCacheRoute::Uncached
    };
  }
  return {
    IOPAddressOutcome::Translated,
    virtualAddress,
    virtualAddress,
    IOPCacheRoute::Uncached
  };
}

IOPMemoryReadResult IOPCore::fetchInstruction() const
{
  return readMemory(pc, sizeof(IOPWord), true);
}

IOPMemoryReadResult IOPCore::readData8(
  IOPAddress virtualAddress) const
{
  return readMemory(virtualAddress, sizeof(std::uint8_t), false);
}

IOPMemoryReadResult IOPCore::readData16(
  IOPAddress virtualAddress) const
{
  return readMemory(virtualAddress, sizeof(std::uint16_t), false);
}

IOPMemoryReadResult IOPCore::readData32(
  IOPAddress virtualAddress) const
{
  return readMemory(virtualAddress, sizeof(IOPWord), false);
}

IOPMemoryWriteResult IOPCore::writeData8(
  IOPAddress virtualAddress,
  std::uint8_t value)
{
  return writeMemory(
    virtualAddress,
    sizeof(value),
    static_cast<IOPWord>(value));
}

IOPMemoryWriteResult IOPCore::writeData16(
  IOPAddress virtualAddress,
  std::uint16_t value)
{
  return writeMemory(
    virtualAddress,
    sizeof(value),
    static_cast<IOPWord>(value));
}

IOPMemoryWriteResult IOPCore::writeData32(
  IOPAddress virtualAddress,
  IOPWord value)
{
  return writeMemory(virtualAddress, sizeof(value), value);
}

IOPBus &IOPCore::attachedBus() const
{
  if (bus == nullptr)
  {
    throw std::logic_error("IOP Core bus is not attached.");
  }
  return *bus;
}

IOPMemoryReadResult IOPCore::readMemory(
  IOPAddress virtualAddress,
  std::size_t width,
  bool instructionFetch) const
{
  IOPBus &physicalBus = attachedBus();
  const IOPAddressClassification classification =
    classifyAddress(virtualAddress);
  IOPMemoryReadResult result;
  result.virtualAddress = virtualAddress;
  result.physicalAddress = classification.physicalAddress;
  result.cacheRoute = classification.cacheRoute;

  if (classification.outcome == IOPAddressOutcome::ProtectionFailure)
  {
    result.outcome = IOPAccessOutcome::ProtectionFailure;
    result.exception = IOPException::AddressErrorLoadOrFetch;
    return result;
  }
  if (!isAligned(virtualAddress, width))
  {
    result.outcome = IOPAccessOutcome::Misaligned;
    result.exception = IOPException::AddressErrorLoadOrFetch;
    return result;
  }

  IOPBusReadResult busResult;
  switch (width)
  {
    case sizeof(std::uint8_t):
      busResult = physicalBus.read8(classification.physicalAddress);
      break;
    case sizeof(std::uint16_t):
      busResult = physicalBus.read16(classification.physicalAddress);
      break;
    case sizeof(IOPWord):
      busResult = physicalBus.read32(classification.physicalAddress);
      break;
    default:
      throw std::logic_error("Unsupported IOP memory read width.");
  }

  result.outcome = accessOutcome(busResult.status);
  result.value = busResult.value;
  if (result.outcome != IOPAccessOutcome::Completed)
  {
    result.exception = instructionFetch
      ? IOPException::InstructionBusError
      : IOPException::DataBusError;
  }
  return result;
}

IOPMemoryWriteResult IOPCore::writeMemory(
  IOPAddress virtualAddress,
  std::size_t width,
  IOPWord value)
{
  IOPBus &physicalBus = attachedBus();
  const IOPAddressClassification classification =
    classifyAddress(virtualAddress);
  IOPMemoryWriteResult result;
  result.virtualAddress = virtualAddress;
  result.physicalAddress = classification.physicalAddress;
  result.cacheRoute = classification.cacheRoute;

  if (classification.outcome == IOPAddressOutcome::ProtectionFailure)
  {
    result.outcome = IOPAccessOutcome::ProtectionFailure;
    result.exception = IOPException::AddressErrorStore;
    return result;
  }
  if (!isAligned(virtualAddress, width))
  {
    result.outcome = IOPAccessOutcome::Misaligned;
    result.exception = IOPException::AddressErrorStore;
    return result;
  }

  IOPBusWriteResult busResult;
  switch (width)
  {
    case sizeof(std::uint8_t):
      busResult = physicalBus.write8(
        classification.physicalAddress,
        static_cast<std::uint8_t>(value));
      break;
    case sizeof(std::uint16_t):
      busResult = physicalBus.write16(
        classification.physicalAddress,
        static_cast<std::uint16_t>(value));
      break;
    case sizeof(IOPWord):
      busResult = physicalBus.write32(
        classification.physicalAddress,
        value);
      break;
    default:
      throw std::logic_error("Unsupported IOP memory write width.");
  }

  result.outcome = accessOutcome(busResult.status);
  if (result.outcome != IOPAccessOutcome::Completed)
  {
    result.exception = IOPException::DataBusError;
  }
  return result;
}

IOPAddress IOPCore::programCounter() const
{
  return pc;
}

IOPWord IOPCore::generalRegister(std::size_t index) const
{
  if (index >= GENERAL_REGISTER_COUNT)
  {
    throw std::out_of_range(
      "IOP general register index is out of range.");
  }
  if (index == 0)
  {
    return 0;
  }
  return writableGeneralRegisters[index - 1];
}

void IOPCore::setGeneralRegister(
  std::size_t index,
  IOPWord value)
{
  if (index >= GENERAL_REGISTER_COUNT)
  {
    throw std::out_of_range(
      "IOP general register index is out of range.");
  }
  if (index != 0)
  {
    writableGeneralRegisters[index - 1] = value;
  }
}

IOPWord IOPCore::hi() const
{
  return hiRegister;
}

IOPWord IOPCore::lo() const
{
  return loRegister;
}

IOPAddress IOPCore::badVirtualAddress() const
{
  return cop0.badVirtualAddress;
}

IOPWord IOPCore::status() const
{
  return cop0.status;
}

IOPWord IOPCore::cause() const
{
  return cop0.cause;
}

IOPAddress IOPCore::exceptionProgramCounter() const
{
  return cop0.exceptionProgramCounter;
}

IOPWord IOPCore::processorRevision() const
{
  return IOPCOP0::PROCESSOR_REVISION;
}

const IOPBranchContinuation &
IOPCore::branchContinuation() const
{
  return branch;
}

const IOPDelayedResultContinuation &
IOPCore::delayedResultContinuation() const
{
  return delayedResult;
}

const IOPPendingException &IOPCore::exception() const
{
  return pendingException;
}

IOPExecutionState IOPCore::executionState() const
{
  return state;
}

IOPStopReason IOPCore::stopReason() const
{
  return haltReason;
}

IOPCycleCount IOPCore::elapsedCycles() const
{
  return cycles;
}

std::uint64_t IOPCore::retiredInstructions() const
{
  return retiredInstructionTotal;
}
