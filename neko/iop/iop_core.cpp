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

  IOPInstructionEffects delayedLoadEffects(
    std::uint8_t destination,
    IOPWord value)
  {
    IOPInstructionEffects effects = retiredEffects();
    if (destination != 0)
    {
      effects.delayedResult = {
        IOPDelayedResultSource::Load,
        destination,
        value
      };
    }
    return effects;
  }

  IOPInstructionEffects arithmeticOverflowEffects()
  {
    IOPInstructionEffects effects;
    effects.exception.kind = IOPException::ArithmeticOverflow;
    return effects;
  }

  IOPWord exceptionCode(IOPException exception)
  {
    switch (exception)
    {
      case IOPException::Interrupt:
        return 0;
      case IOPException::AddressErrorLoadOrFetch:
        return 4;
      case IOPException::AddressErrorStore:
        return 5;
      case IOPException::InstructionBusError:
        return 6;
      case IOPException::DataBusError:
        return 7;
      case IOPException::SystemCall:
        return 8;
      case IOPException::Breakpoint:
        return 9;
      case IOPException::ReservedInstruction:
        return 10;
      case IOPException::CoprocessorUnusable:
        return 11;
      case IOPException::ArithmeticOverflow:
        return 12;
      case IOPException::None:
        throw std::logic_error("Invalid IOP exception.");
    }
    throw std::logic_error("Unknown IOP exception.");
  }

  IOPInstructionEffects memoryReadFailureEffects(
    const IOPMemoryReadResult &read)
  {
    IOPInstructionEffects effects;
    effects.exception.kind = read.exception;
    effects.exception.badVirtualAddress = read.virtualAddress;
    return effects;
  }

  IOPInstructionEffects alignedStoreEffects(
    const IOPInstruction &instruction,
    IOPWord base,
    IOPWord value)
  {
    IOPInstructionEffects effects = retiredEffects();
    effects.store.virtualAddress =
      base + signExtendedImmediate(instruction.immediate);
    switch (instruction.operation)
    {
      case IOPOperation::StoreByte:
        effects.store.operation = IOPStoreOperation::Byte;
        effects.store.value = value & 0xff;
        break;
      case IOPOperation::StoreHalfword:
        effects.store.operation = IOPStoreOperation::Halfword;
        effects.store.value = value & 0xffff;
        break;
      case IOPOperation::StoreWord:
        effects.store.operation = IOPStoreOperation::Word;
        effects.store.value = value;
        break;
      default:
        throw std::logic_error("Unsupported IOP aligned store.");
    }
    return effects;
  }

  IOPInstructionEffects wordMergeStoreEffects(
    const IOPInstruction &instruction,
    IOPWord base,
    IOPWord value)
  {
    IOPInstructionEffects effects = retiredEffects();
    effects.store.operation = IOPStoreOperation::MaskedWord;
    effects.store.virtualAddress =
      base + signExtendedImmediate(instruction.immediate);
    const std::uint8_t offset = effects.store.virtualAddress & 3;
    switch (instruction.operation)
    {
      case IOPOperation::StoreWordLeft:
        effects.store.value = value >> (8 * (3 - offset));
        effects.store.byteEnable = 0xf >> (3 - offset);
        break;
      case IOPOperation::StoreWordRight:
        effects.store.value = value << (8 * offset);
        effects.store.byteEnable = (0xf << offset) & 0xf;
        break;
      default:
        throw std::logic_error("Unsupported IOP word merge store.");
    }
    return effects;
  }

  IOPStoreWidth storeAccessWidth(IOPStoreOperation operation)
  {
    switch (operation)
    {
      case IOPStoreOperation::None:
        return IOPStoreWidth::None;
      case IOPStoreOperation::Byte:
        return IOPStoreWidth::Byte;
      case IOPStoreOperation::Halfword:
        return IOPStoreWidth::Halfword;
      case IOPStoreOperation::Word:
      case IOPStoreOperation::MaskedWord:
        return IOPStoreWidth::Word;
    }
    throw std::logic_error("Unsupported IOP store operation.");
  }

  IOPInstructionEffects reservedInstructionEffects()
  {
    IOPInstructionEffects effects;
    effects.exception.kind = IOPException::ReservedInstruction;
    return effects;
  }

  IOPInstructionEffects addressErrorEffects(IOPAddress address)
  {
    IOPInstructionEffects effects;
    effects.exception.kind = IOPException::AddressErrorLoadOrFetch;
    effects.exception.badVirtualAddress = address;
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

  IOPInstructionEffects branchEffects(const IOPControlFlowRequest &request)
  {
    IOPInstructionEffects effects = retiredEffects();
    effects.controlFlow = request;
    return effects;
  }

  IOPInstructionEffects branchLinkEffects(
    const IOPControlFlowRequest &request,
    std::uint8_t destination,
    IOPAddress link)
  {
    IOPInstructionEffects effects =
      destinationEffects(destination, link);
    effects.controlFlow = request;
    return effects;
  }

  IOPControlFlowRequest conditionalBranchRequest(
    IOPAddress instructionAddress,
    std::uint16_t immediate,
    IOPBranchOutcome outcome)
  {
    const IOPAddress delaySlot =
      instructionAddress + sizeof(IOPWord);
    const IOPAddress target = outcome == IOPBranchOutcome::Taken
      ? delaySlot + (signExtendedImmediate(immediate) << 2)
      : delaySlot + sizeof(IOPWord);
    return {IOPControlFlowEffect::ScheduleBranch, target, outcome};
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

  bool multiplyDivideResultIsUnobserved(const IOPHILOState &state)
  {
    return state.unreadMultiplyDivideHI &&
      state.unreadMultiplyDivideLO;
  }

  bool pairWriteHasReadHazard(const IOPHILOState &state)
  {
    return state.hiWriteHazardInstructions != 0 ||
      state.loWriteHazardInstructions != 0;
  }
}

IOPCore::IOPCore()
{
  reset();
}

void IOPCore::enableObservations(NekoObservationOrder *order)
{
  clearObservations();
  observationOrder = order;
  observationsEnabled = true;
}

void IOPCore::disableObservations()
{
  clearObservations();
  observationsEnabled = false;
  observationOrder = nullptr;
}

void IOPCore::clearObservations()
{
  stepEventCount = 0;
}

void IOPCore::recordStepEvent(const StepEvent &event)
{
  if (!observationsEnabled)
  {
    return;
  }
  if (stepEventCount >= stepEvents.size())
  {
    throw std::logic_error("IOP step observation capacity exceeded.");
  }
  stepEvents[stepEventCount] = event;
  stepEvents[stepEventCount].sequence =
    observationOrder != nullptr ? observationOrder->next++ : stepEventCount;
  ++stepEventCount;
}

void IOPCore::recordMemoryRead(
  const IOPMemoryReadResult &result,
  std::uint8_t width,
  MemoryAccessKind kind)
{
  recordStepEvent(MemoryAccessEvent{
    kind, result.virtualAddress, result.physicalAddress,
    result.outcome == IOPAccessOutcome::Completed ? result.value : 0,
    result.outcome, result.cacheRoute, width, 0,
    result.cacheRoute != IOPCacheRoute::None});
}

void IOPCore::recordMemoryStore(
  const IOPStoreRequest &request,
  const IOPMemoryWriteResult &result)
{
  if (!observationsEnabled)
  {
    return;
  }
  const auto width = static_cast<std::uint8_t>(storeAccessWidth(request.operation));
  const bool merge = request.operation == IOPStoreOperation::MaskedWord;
  const IOPAddress addressMask = merge ? ~IOPAddress(3) : UINT32_MAX;
  const auto byteEnable = merge ? request.byteEnable :
    isAligned(request.virtualAddress, width)
      ? static_cast<std::uint8_t>(((1u << width) - 1) <<
          (request.virtualAddress & 3))
      : std::uint8_t(0);
  const IOPWord value = request.value & (UINT32_MAX >> ((4 - width) * 8));
  recordStepEvent(MemoryAccessEvent{
    MemoryAccessKind::DataStore,
    result.virtualAddress & addressMask,
    result.physicalAddress & addressMask, value,
    result.outcome, result.cacheRoute, width, byteEnable,
    result.cacheRoute != IOPCacheRoute::None});
}

void IOPCore::transitionExecution(
  IOPExecutionState nextState,
  IOPStopReason reason)
{
  const auto previousState = state;
  const auto previousReason = haltReason;
  state = nextState;
  haltReason = reason;
  if (previousState != state || previousReason != haltReason)
  {
    recordStepEvent(StateChangedEvent{previousState, state, haltReason, pc});
  }
}

void IOPCore::reset()
{
  clearObservations();
  caches.reset();
  cacheRomIdentity = bus != nullptr ? bus->romIdentity() : nullptr;
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
  directInterruptInputs = 0;
  cop0.exceptionProgramCounter = 0;
  branch = {};
  delayedResult = {};
  enteredException = {};
  rejectedStore = {};
  state = IOPExecutionState::Halted;
  haltReason = IOPStopReason::None;
  cycles = 0;
  retiredInstructionTotal = 0;
}

void IOPCore::attachBus(IOPBus *attached)
{
  if (bus != attached)
  {
    clearObservations();
    caches.reset();
    cacheRomIdentity = nullptr;
  }
  bus = attached;
  if (bus != nullptr)
  {
    synchronizeCacheConfiguration(*bus);
  }
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
  clearObservations();
  pc = entryPoint;
  branch = {};
  delayedResult = {};
  hiLoState = {};
  enteredException = {};
  rejectedStore = {};
  transitionExecution(IOPExecutionState::Running, IOPStopReason::None);
}

void IOPCore::haltExecution()
{
  clearObservations();
  transitionExecution(IOPExecutionState::Halted, IOPStopReason::HostHalt);
}

void IOPCore::resumeExecution()
{
  if (state != IOPExecutionState::Halted ||
      haltReason != IOPStopReason::HostHalt ||
      rejectedStore.width != IOPStoreWidth::None)
  {
    throw std::logic_error(
      "IOP Core can resume only after a host halt without a rejected store.");
  }
  clearObservations();
  transitionExecution(IOPExecutionState::Running, IOPStopReason::None);
}

void IOPCore::stepInstruction()
{
  if (state != IOPExecutionState::Running)
  {
    throw std::logic_error(
      "IOP Core cannot step while halted.");
  }

  clearObservations();
  enteredException = {};
  const IOPAddress instructionAddress = pc;
  synchronizeInterruptInputs();
  if (interruptDeliverable())
  {
    IOPInstructionEffects effects;
    effects.exception.kind = IOPException::Interrupt;
    commitInstructionEffects(instructionAddress, effects);
    return;
  }
  const IOPMemoryReadResult fetch = fetchInstruction();
  recordMemoryRead(fetch, sizeof(IOPWord), MemoryAccessKind::InstructionFetch);
  if (fetch.outcome != IOPAccessOutcome::Completed)
  {
    commitInstructionEffects(
      instructionAddress,
      memoryReadFailureEffects(fetch));
    return;
  }

  recordStepEvent(InstructionIssuedEvent{
    instructionAddress, fetch.value, branch.active});
  commitInstructionEffects(
    instructionAddress,
    instructionEffects(decodeIOPInstruction(fetch.value)));
}

IOPInstructionEffects IOPCore::instructionEffects(
  const IOPDecodeResult &decoded)
{
  if (decoded.disposition == IOPDecodeDisposition::Reserved)
  {
    return reservedInstructionEffects();
  }
  const IOPInstruction &instruction = decoded.instruction;
  const bool cop0Operation = instruction.opcode == 0x10;
  const bool otherCoprocessorOperation =
    instruction.operation == IOPOperation::CoprocessorOperation ||
    instruction.operation == IOPOperation::LoadWordToCoprocessor ||
    instruction.operation == IOPOperation::StoreWordFromCoprocessor;
  if (cop0Operation || otherCoprocessorOperation)
  {
    const std::uint8_t coprocessor = instruction.opcode & 3;
    const bool kernelCOP0 = cop0Operation &&
      (cop0.status & IOPCOP0Status::CURRENT_USER_MODE) == 0;
    if (!kernelCOP0 &&
        (cop0.status & (IOPWord(1) << (28 + coprocessor))) == 0)
    {
      IOPInstructionEffects effects;
      effects.exception.kind = IOPException::CoprocessorUnusable;
      effects.exception.coprocessor = coprocessor;
      return effects;
    }
  }
  if (decoded.disposition == IOPDecodeDisposition::ValidButDeferred)
  {
    return {};
  }
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
    case IOPOperation::SystemCall:
    case IOPOperation::Breakpoint:
    {
      IOPInstructionEffects effects;
      effects.exception.kind =
        instruction.operation == IOPOperation::SystemCall
          ? IOPException::SystemCall
          : IOPException::Breakpoint;
      return effects;
    }
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
      return branchEffects({IOPControlFlowEffect::ScheduleBranch, source});
    case IOPOperation::JumpAndLinkRegister:
      if ((source & 3) != 0 ||
          classifyAddress(source).outcome !=
            IOPAddressOutcome::Translated)
      {
        return addressErrorEffects(source);
      }
      return branchLinkEffects(
        {IOPControlFlowEffect::ScheduleBranch, source},
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
          multiplyDivideResultIsUnobserved(hiLoState))
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
          multiplyDivideResultIsUnobserved(hiLoState))
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
      if (pairWriteHasReadHazard(hiLoState))
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
      if (pairWriteHasReadHazard(hiLoState))
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
        conditionalBranchRequest(
          pc,
          instruction.immediate,
          (source & UINT32_C(0x80000000)) != 0
            ? IOPBranchOutcome::Taken : IOPBranchOutcome::NotTaken));
    case IOPOperation::BranchGreaterThanOrEqualZero:
      return branchEffects(
        conditionalBranchRequest(
          pc,
          instruction.immediate,
          (source & UINT32_C(0x80000000)) == 0
            ? IOPBranchOutcome::Taken : IOPBranchOutcome::NotTaken));
    case IOPOperation::BranchLessThanZeroAndLink:
      return branchLinkEffects(
        conditionalBranchRequest(
          pc,
          instruction.immediate,
          (source & UINT32_C(0x80000000)) != 0
            ? IOPBranchOutcome::Taken : IOPBranchOutcome::NotTaken),
        31,
        pc + 2 * sizeof(IOPWord));
    case IOPOperation::BranchGreaterThanOrEqualZeroAndLink:
      return branchLinkEffects(
        conditionalBranchRequest(
          pc,
          instruction.immediate,
          (source & UINT32_C(0x80000000)) == 0
            ? IOPBranchOutcome::Taken : IOPBranchOutcome::NotTaken),
        31,
        pc + 2 * sizeof(IOPWord));
    case IOPOperation::Jump:
      return branchEffects(
        {IOPControlFlowEffect::ScheduleBranch,
          ((pc + static_cast<IOPAddress>(sizeof(IOPWord))) & UINT32_C(0xf0000000)) |
            (instruction.target << 2)});
    case IOPOperation::JumpAndLink:
      return branchLinkEffects(
        {IOPControlFlowEffect::ScheduleBranch,
          ((pc + static_cast<IOPAddress>(sizeof(IOPWord))) & UINT32_C(0xf0000000)) |
            (instruction.target << 2)},
        31,
        pc + 2 * sizeof(IOPWord));
    case IOPOperation::BranchEqual:
      return branchEffects(
        conditionalBranchRequest(
          pc,
          instruction.immediate,
          source == target ? IOPBranchOutcome::Taken : IOPBranchOutcome::NotTaken));
    case IOPOperation::BranchNotEqual:
      return branchEffects(
        conditionalBranchRequest(
          pc,
          instruction.immediate,
          source != target ? IOPBranchOutcome::Taken : IOPBranchOutcome::NotTaken));
    case IOPOperation::BranchLessThanOrEqualZero:
      return branchEffects(
        conditionalBranchRequest(
          pc,
          instruction.immediate,
          (source == 0 || (source & UINT32_C(0x80000000)) != 0)
            ? IOPBranchOutcome::Taken : IOPBranchOutcome::NotTaken));
    case IOPOperation::BranchGreaterThanZero:
      return branchEffects(
        conditionalBranchRequest(
          pc,
          instruction.immediate,
          (source != 0 && (source & UINT32_C(0x80000000)) == 0)
            ? IOPBranchOutcome::Taken : IOPBranchOutcome::NotTaken));
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
    case IOPOperation::MoveFromCOP0:
    case IOPOperation::MoveToCOP0:
      return cop0TransferEffects(instruction, target);
    case IOPOperation::ReturnFromException:
    {
      IOPInstructionEffects effects = retiredEffects();
      effects.cop0Write = {
        IOPCOP0WriteTarget::StatusStack,
        (cop0.status >> 2) & 0xf
      };
      return effects;
    }
    case IOPOperation::LoadByte:
    case IOPOperation::LoadByteUnsigned:
    case IOPOperation::LoadHalfword:
    case IOPOperation::LoadHalfwordUnsigned:
    case IOPOperation::LoadWord:
      return alignedLoadEffects(instruction, source);
    case IOPOperation::LoadWordLeft:
    case IOPOperation::LoadWordRight:
      return wordMergeLoadEffects(instruction, source, target);
    case IOPOperation::StoreByte:
    case IOPOperation::StoreHalfword:
    case IOPOperation::StoreWord:
      return alignedStoreEffects(instruction, source, target);
    case IOPOperation::StoreWordLeft:
    case IOPOperation::StoreWordRight:
      return wordMergeStoreEffects(instruction, source, target);
    default:
      return {};
  }
}

IOPInstructionEffects IOPCore::alignedLoadEffects(
  const IOPInstruction &instruction,
  IOPWord base)
{
  const IOPAddress address =
    base + signExtendedImmediate(instruction.immediate);
  IOPMemoryReadResult read;
  std::uint8_t width = 0;
  switch (instruction.operation)
  {
    case IOPOperation::LoadByte:
    case IOPOperation::LoadByteUnsigned:
      width = sizeof(std::uint8_t);
      read = readData8(address);
      break;
    case IOPOperation::LoadHalfword:
    case IOPOperation::LoadHalfwordUnsigned:
      width = sizeof(std::uint16_t);
      read = readData16(address);
      break;
    case IOPOperation::LoadWord:
      width = sizeof(IOPWord);
      read = readData32(address);
      break;
    default:
      throw std::logic_error("Unsupported IOP aligned load.");
  }
  recordMemoryRead(read, width, MemoryAccessKind::DataLoad);
  if (read.outcome != IOPAccessOutcome::Completed)
  {
    return memoryReadFailureEffects(read);
  }

  IOPWord value = read.value;
  if (instruction.operation == IOPOperation::LoadByte &&
      (value & 0x80) != 0)
  {
    value |= UINT32_C(0xffffff00);
  }
  else if (instruction.operation == IOPOperation::LoadHalfword)
  {
    value = signExtendedImmediate(
      static_cast<std::uint16_t>(value));
  }
  return delayedLoadEffects(instruction.targetRegister, value);
}

IOPInstructionEffects IOPCore::wordMergeLoadEffects(
  const IOPInstruction &instruction,
  IOPWord base,
  IOPWord target)
{
  const IOPAddress address =
    base + signExtendedImmediate(instruction.immediate);
  IOPMemoryReadResult read = readData32(address & ~IOPAddress(3));
  recordMemoryRead(read, sizeof(IOPWord), MemoryAccessKind::DataLoad);
  if (read.outcome != IOPAccessOutcome::Completed)
  {
    read.virtualAddress = address;
    return memoryReadFailureEffects(read);
  }
  if (delayedResult.source == IOPDelayedResultSource::Load &&
      delayedResult.destination == instruction.targetRegister)
  {
    target = delayedResult.value;
  }
  const std::uint8_t offset = address & 3;
  IOPWord value = 0;
  switch (instruction.operation)
  {
    case IOPOperation::LoadWordLeft:
    {
      const std::uint8_t shift = 8 * (3 - offset);
      const IOPWord mask = UINT32_MAX << shift;
      value = (target & ~mask) | (read.value << shift);
      break;
    }
    case IOPOperation::LoadWordRight:
    {
      const std::uint8_t shift = 8 * offset;
      const IOPWord mask = UINT32_MAX >> shift;
      value = (target & ~mask) | (read.value >> shift);
      break;
    }
    default:
      throw std::logic_error("Unsupported IOP word merge load.");
  }
  return delayedLoadEffects(instruction.targetRegister, value);
}

IOPInstructionEffects IOPCore::cop0TransferEffects(
  const IOPInstruction &instruction,
  IOPWord target) const
{
  IOPWord value = 0;
  IOPCOP0WriteTarget writeTarget = IOPCOP0WriteTarget::None;
  switch (instruction.destinationRegister)
  {
    case 3:  // BPC
    case 5:  // BDA
    case 6:  // Unidentified IOP selector; bounded BIOS initialization policy.
    case 7:  // DCIC
    case 9:  // BDAM
    case 11: // BPCM
      // Only zero-valued initialization is modeled, without retained state.
      return instruction.operation == IOPOperation::MoveToCOP0 && target == 0
        ? retiredEffects() : reservedInstructionEffects();
    case 8:
      value = cop0.badVirtualAddress;
      break;
    case 12:
      value = (cop0.status & IOPCOP0Status::READ_MASK) |
        IOPCOP0Status::TLB_SHUTDOWN;
      writeTarget = IOPCOP0WriteTarget::Status;
      break;
    case 13:
      value = cop0.cause & IOPCOP0Cause::READ_MASK;
      writeTarget = IOPCOP0WriteTarget::Cause;
      break;
    case 14:
      value = cop0.exceptionProgramCounter;
      writeTarget = IOPCOP0WriteTarget::ExceptionProgramCounter;
      break;
    case 15:
      value = IOPCOP0::PROCESSOR_REVISION;
      break;
    default:
      return reservedInstructionEffects();
  }

  IOPInstructionEffects effects = retiredEffects();
  if (instruction.operation == IOPOperation::MoveFromCOP0)
  {
    if (instruction.targetRegister != 0)
    {
      effects.delayedResult = {
        IOPDelayedResultSource::COP0Transfer,
        instruction.targetRegister,
        value
      };
    }
  }
  else
  {
    effects.cop0Write = {writeTarget, target};
  }
  return effects;
}

void IOPCore::commitCOP0Write(const IOPCOP0WriteRequest &request)
{
  switch (request.target)
  {
    case IOPCOP0WriteTarget::None:
      break;
    case IOPCOP0WriteTarget::Status:
      cop0.status =
        (request.value & IOPCOP0Status::WRITE_MASK) |
        (cop0.status & IOPCOP0Status::CACHE_PROBE_RESULT) |
        (cop0.status & ~request.value & IOPCOP0Status::PARITY_ERROR) |
        IOPCOP0Status::TLB_SHUTDOWN;
      break;
    case IOPCOP0WriteTarget::StatusStack:
      cop0.status = (cop0.status & ~IOPWord(0xf)) | (request.value & 0xf);
      break;
    case IOPCOP0WriteTarget::Cause:
      cop0.cause =
        (cop0.cause & IOPCOP0Cause::READ_MASK &
          ~IOPCOP0Cause::SOFTWARE_INTERRUPT_PENDING) |
        (request.value & IOPCOP0Cause::SOFTWARE_INTERRUPT_PENDING);
      break;
    case IOPCOP0WriteTarget::ExceptionProgramCounter:
      cop0.exceptionProgramCounter = request.value;
      break;
  }
}

void IOPCore::commitHILOEffects(const IOPInstructionEffects &effects)
{
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
}

void IOPCore::setInterruptInput(IOPInterruptInput input, bool asserted)
{
  const std::uint8_t index = static_cast<std::uint8_t>(input);
  if (index >= 6)
  {
    throw std::out_of_range("IOP hardware interrupt input is out of range.");
  }
  const IOPWord mask = IOPWord(1) << (10 + index);
  directInterruptInputs =
    (directInterruptInputs & ~mask) | (asserted ? mask : 0);
  synchronizeInterruptInputs();
}

void IOPCore::synchronizeInterruptInputs()
{
  IOPWord pending = directInterruptInputs;
  if (bus != nullptr && bus->interruptControllerPending())
  {
    pending |= IOPWord(1) << 10;
  }
  cop0.cause =
    (cop0.cause & ~IOPCOP0Cause::HARDWARE_INTERRUPT_PENDING) | pending;
}

bool IOPCore::interruptDeliverable() const
{
  return (cop0.status & IOPCOP0Status::INTERRUPT_ENABLE) != 0 &&
    (cop0.status & cop0.cause & IOPCOP0Status::INTERRUPT_MASK) != 0;
}

void IOPCore::enterException(
  IOPAddress instructionAddress,
  const IOPExceptionRecord &exception)
{
  const IOPWord code = exceptionCode(exception.kind);
  const IOPWord previousStatus = cop0.status;
  enteredException = exception;
  enteredException.inBranchDelaySlot = branch.active;
  cop0.exceptionProgramCounter = branch.active
    ? branch.instructionAddress
    : instructionAddress;
  cop0.cause =
    (cop0.cause & IOPCOP0Cause::INTERRUPT_PENDING) |
    (code << 2) |
    (branch.active ? IOPCOP0Cause::BRANCH_DELAY : 0);
  if (exception.kind == IOPException::CoprocessorUnusable)
  {
    cop0.cause |= IOPWord(exception.coprocessor & 3) << 28;
  }
  if (exception.kind == IOPException::AddressErrorLoadOrFetch ||
      exception.kind == IOPException::AddressErrorStore)
  {
    cop0.badVirtualAddress = exception.badVirtualAddress;
  }
  cop0.status =
    (cop0.status & ~IOPWord(0x3f)) |
    ((cop0.status & 0xf) << 2);
  pc = (cop0.status & IOPCOP0Status::BOOT_EXCEPTION_VECTORS) != 0
    ? IOPExceptionVector::BOOT_GENERAL
    : IOPExceptionVector::GENERAL;
  branch = {};
  delayedResult = {};
  state = IOPExecutionState::Running;
  haltReason = IOPStopReason::None;
  if (exception.kind == IOPException::Interrupt)
  {
    recordStepEvent(InterruptDeliveredEvent{
      instructionAddress, previousStatus, cop0.cause, pc});
  }
  recordStepEvent(ExceptionEnteredEvent{
    cop0.exceptionProgramCounter, pc, cop0.cause});
}

void IOPCore::commitInstructionEffects(
  IOPAddress instructionAddress,
  const IOPInstructionEffects &effects)
{
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
  IOPExceptionRecord selectedException = effects.exception;
  if (effects.completion == IOPInstructionCompletion::Retired &&
      selectedException.kind == IOPException::None &&
      effects.store.operation != IOPStoreOperation::None)
  {
    const IOPMemoryWriteResult write = writeStore(effects.store);
    if (write.exception != IOPException::None)
    {
      selectedException.kind = write.exception;
      selectedException.badVirtualAddress = write.virtualAddress;
    }
    else if (write.outcome != IOPAccessOutcome::Completed)
    {
      rejectedStore = {
        storeAccessWidth(effects.store.operation),
        effects.store.value,
        write,
        effects.store.byteEnable
      };
      transitionExecution(IOPExecutionState::Halted, IOPStopReason::StoreFailure);
      return;
    }
  }
  if (effects.completion == IOPInstructionCompletion::Retired ||
      selectedException.kind != IOPException::None)
  {
    // Sources were captured before older writeback; younger writes win.
    if (delayedResult.source != IOPDelayedResultSource::None)
    {
      setGeneralRegister(
        delayedResult.destination,
        delayedResult.value);
      delayedResult = {};
    }
  }
  if (selectedException.kind != IOPException::None)
  {
    enterException(instructionAddress, selectedException);
    return;
  }
  if (effects.completion == IOPInstructionCompletion::Halted)
  {
    transitionExecution(IOPExecutionState::Halted, effects.stopReason);
    return;
  }

  if (effects.destination.kind == IOPWriteEffect::Write)
  {
    setGeneralRegister(
      effects.destination.destination,
      effects.destination.value);
  }
  commitCOP0Write(effects.cop0Write);
  delayedResult = effects.delayedResult;
  commitHILOEffects(effects);

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
      recordStepEvent(BranchScheduledEvent{
        instructionAddress, effects.controlFlow.target,
        effects.controlFlow.branchOutcome});
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
  recordStepEvent(InstructionRetiredEvent{
    instructionAddress, pc, retiredInstructionTotal, cycles});
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

IOPMemoryReadResult IOPCore::fetchInstruction()
{
  return readMemory(pc, sizeof(IOPWord), IOPCacheAccess::InstructionFetch);
}

IOPMemoryReadResult IOPCore::readData8(
  IOPAddress virtualAddress)
{
  return readMemory(
    virtualAddress, sizeof(std::uint8_t), IOPCacheAccess::DataLoad);
}

IOPMemoryReadResult IOPCore::readData16(
  IOPAddress virtualAddress)
{
  return readMemory(
    virtualAddress, sizeof(std::uint16_t), IOPCacheAccess::DataLoad);
}

IOPMemoryReadResult IOPCore::readData32(
  IOPAddress virtualAddress)
{
  return readMemory(virtualAddress, sizeof(IOPWord), IOPCacheAccess::DataLoad);
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

IOPMemoryWriteResult IOPCore::writeStore(
  const IOPStoreRequest &request)
{
  IOPMemoryWriteResult result;
  switch (request.operation)
  {
    case IOPStoreOperation::Byte:
      result = writeData8(
        request.virtualAddress,
        static_cast<std::uint8_t>(request.value));
      break;
    case IOPStoreOperation::Halfword:
      result = writeData16(
        request.virtualAddress,
        static_cast<std::uint16_t>(request.value));
      break;
    case IOPStoreOperation::Word:
      result = writeData32(request.virtualAddress, request.value);
      break;
    case IOPStoreOperation::MaskedWord:
      result = writeMaskedStore(request);
      break;
    case IOPStoreOperation::None:
      throw std::logic_error("Unsupported IOP store width.");
    default:
      throw std::logic_error("Unknown IOP store operation.");
  }
  recordMemoryStore(request, result);
  return result;
}

IOPMemoryWriteResult IOPCore::writeMaskedStore(
  const IOPStoreRequest &request)
{
  const IOPAddressClassification classification =
    classifyAddress(request.virtualAddress);
  IOPMemoryWriteResult result;
  result.virtualAddress = request.virtualAddress;
  result.physicalAddress = classification.physicalAddress;
  result.cacheRoute = classification.cacheRoute;
  if (classification.outcome == IOPAddressOutcome::ProtectionFailure)
  {
    result.outcome = IOPAccessOutcome::ProtectionFailure;
    result.exception = IOPException::AddressErrorStore;
    return result;
  }
  IOPBus &physicalBus = attachedBus();
  auto aligned = classification;
  aligned.physicalAddress &= ~IOPAddress(3);
  const auto plan = planCacheAccess(
    IOPCacheAccess::DataStore, aligned, sizeof(IOPWord));
  if (plan.disposition == IOPCacheAccessDisposition::Isolated)
  {
    synchronizeCacheConfiguration(physicalBus);
    caches.commitIndexedStore(
      plan.bank, aligned.physicalAddress, request.value, request.byteEnable);
    result.outcome = IOPAccessOutcome::Completed;
    return result;
  }
  const IOPBusWriteResult write = physicalBus.writeMasked32(
    aligned.physicalAddress,
    request.value,
    request.byteEnable);
  result.outcome = accessOutcome(write.status);
  if (write.status == IOPBusStatus::Completed)
  {
    synchronizeCacheConfiguration(physicalBus);
    if (plan.disposition == IOPCacheAccessDisposition::Cache)
    {
      caches.commitStore(
        plan.bank, aligned.physicalAddress, request.value, request.byteEnable);
    }
  }
  return result;
}

IOPBus &IOPCore::attachedBus() const
{
  if (bus == nullptr)
  {
    throw std::logic_error("IOP Core bus is not attached.");
  }
  return *bus;
}

IOPCacheAccessPlan IOPCore::planCacheAccess(
  IOPCacheAccess access,
  const IOPAddressClassification &classification,
  std::size_t width) const
{
  const IOPCacheControl control = {
    (cop0.status & IOPCOP0Status::CACHE_ISOLATED) != 0,
    (cop0.status & IOPCOP0Status::CACHES_SWAPPED) != 0
  };
  return IOPCache::planAccess(
    access, control, classification.cacheRoute,
    attachedBus().regionForRange(classification.physicalAddress, width));
}

void IOPCore::synchronizeCacheConfiguration(IOPBus &physicalBus)
{
  if (cacheRomIdentity != physicalBus.romIdentity())
  {
    caches.reset();
    cacheRomIdentity = physicalBus.romIdentity();
  }
}

const IOPCache &IOPCore::cache() const
{
  return caches;
}

IOPMemoryReadResult IOPCore::readMemory(
  IOPAddress virtualAddress,
  std::size_t width,
  IOPCacheAccess access)
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

  const auto plan = planCacheAccess(access, classification, width);
  synchronizeCacheConfiguration(physicalBus);
  IOPBusReadResult busResult;
  if (plan.disposition == IOPCacheAccessDisposition::Isolated)
  {
    const auto read = caches.readIsolatedWord(
      plan.bank, classification.physicalAddress & ~IOPAddress(3));
    if (read.probe == IOPCacheProbe::Miss)
    {
      cop0.status |= IOPCOP0Status::CACHE_PROBE_RESULT;
    }
    else
    {
      cop0.status &= ~IOPCOP0Status::CACHE_PROBE_RESULT;
    }
    busResult = {IOPBusStatus::Completed, read.value};
  }
  else if (plan.disposition == IOPCacheAccessDisposition::Cache)
  {
    busResult = caches.readWord(
      plan.bank, physicalBus, classification.physicalAddress & ~IOPAddress(3));
  }
  else
  {
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
  }
  if (plan.disposition != IOPCacheAccessDisposition::Bypass &&
      busResult.status == IOPBusStatus::Completed)
  {
    const auto shift = (classification.physicalAddress & 3) * 8;
    const IOPWord mask = UINT32_MAX >> ((sizeof(IOPWord) - width) * 8);
    busResult.value = (busResult.value >> shift) & mask;
  }

  result.outcome = accessOutcome(busResult.status);
  result.value = busResult.value;
  if (result.outcome != IOPAccessOutcome::Completed)
  {
    result.exception = access == IOPCacheAccess::InstructionFetch
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

  const auto plan = planCacheAccess(
    IOPCacheAccess::DataStore, classification, width);
  const unsigned offset = classification.physicalAddress & 3;
  const auto byteEnable =
    static_cast<std::uint8_t>(((1u << width) - 1) << offset);
  const IOPAddress physicalWord = classification.physicalAddress & ~IOPAddress(3);
  if (plan.disposition == IOPCacheAccessDisposition::Isolated)
  {
    synchronizeCacheConfiguration(physicalBus);
    caches.commitIndexedStore(
      plan.bank, physicalWord, value << (8 * offset), byteEnable);
    result.outcome = IOPAccessOutcome::Completed;
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
  if (busResult.status == IOPBusStatus::Completed)
  {
    synchronizeCacheConfiguration(physicalBus);
    if (plan.disposition == IOPCacheAccessDisposition::Cache)
    {
      caches.commitStore(
        plan.bank, physicalWord, value << (8 * offset), byteEnable);
    }
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

const IOPStoreFailure &IOPCore::storeFailure() const
{
  return rejectedStore;
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

const IOPExceptionRecord &IOPCore::exception() const
{
  return enteredException;
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
