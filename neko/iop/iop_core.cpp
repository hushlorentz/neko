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

void IOPCore::startExecution(IOPAddress entryPoint)
{
  pc = entryPoint;
  branch = {};
  delayedResult = {};
  pendingException = {};
  state = IOPExecutionState::Running;
  haltReason = IOPStopReason::None;
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
  const IOPDecodeResult &decoded)
{
  IOPInstructionEffects effects;
  switch (decoded.disposition)
  {
    case IOPDecodeDisposition::Reserved:
      effects.exception.kind = IOPException::ReservedInstruction;
      effects.stopReason = IOPStopReason::ReservedInstruction;
      return effects;
    case IOPDecodeDisposition::ValidButDeferred:
      return effects;
    case IOPDecodeDisposition::Supported:
      break;
  }

  if (decoded.instruction.operation == IOPOperation::Nop)
  {
    effects.controlFlow.kind =
      IOPControlFlowEffect::Sequential;
    effects.completion = IOPInstructionCompletion::Retired;
    effects.stopReason = IOPStopReason::None;
  }
  return effects;
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
  if (branch.active ||
      delayedResult.source != IOPDelayedResultSource::None)
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

  if (effects.exception.kind != IOPException::None)
  {
    pendingException = effects.exception;
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

  switch (effects.controlFlow.kind)
  {
    case IOPControlFlowEffect::Sequential:
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
