#include <limits>
#include <stdexcept>

#include "neko_system.hpp"

namespace
{
  NekoIOPTraceMemory::Outcome traceOutcome(IOPAccessOutcome outcome)
  {
    using Outcome = NekoIOPTraceMemory::Outcome;
    switch (outcome)
    {
      case IOPAccessOutcome::Completed: return Outcome::Completed;
      case IOPAccessOutcome::ProtectionFailure: return Outcome::ProtectionFailure;
      case IOPAccessOutcome::Misaligned: return Outcome::Misaligned;
      case IOPAccessOutcome::ReadOnly: return Outcome::ReadOnly;
      case IOPAccessOutcome::Unmapped: return Outcome::Unmapped;
    }
    throw std::logic_error("Unknown IOP trace access outcome.");
  }

  NekoIOPTraceMemory::CacheRoute traceCacheRoute(IOPCacheRoute route)
  {
    using Route = NekoIOPTraceMemory::CacheRoute;
    switch (route)
    {
      case IOPCacheRoute::None: return Route::None;
      case IOPCacheRoute::Cached: return Route::Cached;
      case IOPCacheRoute::Uncached: return Route::Uncached;
    }
    throw std::logic_error("Unknown IOP trace cache route.");
  }

  NekoIOPTraceState::Execution traceState(IOPExecutionState state)
  {
    using State = NekoIOPTraceState::Execution;
    switch (state)
    {
      case IOPExecutionState::Halted: return State::Halted;
      case IOPExecutionState::Running: return State::Running;
    }
    throw std::logic_error("Unknown IOP trace execution state.");
  }

  NekoIOPTraceState::StopReason traceStopReason(IOPStopReason reason)
  {
    using Reason = NekoIOPTraceState::StopReason;
    switch (reason)
    {
      case IOPStopReason::None: return Reason::None;
      case IOPStopReason::HostHalt: return Reason::HostHalt;
      case IOPStopReason::UndefinedOperation: return Reason::UndefinedOperation;
      case IOPStopReason::ExecutionException: return Reason::ExecutionException;
      case IOPStopReason::StoreFailure: return Reason::StoreFailure;
    }
    throw std::logic_error("Unknown IOP trace stop reason.");
  }

  std::uint64_t timerSnapshot(const IOPTimerCounterState &state)
  {
    return nekoPackIOPTimerTraceSnapshot({
      state.count, static_cast<std::uint16_t>(state.mode),
      static_cast<std::uint8_t>(state.prescalerRemainder),
      state.gateWaiting, state.oneShotConsumed, state.pulseReleasePending});
  }
}

void NekoSystem::publishIOPTrace(std::uint64_t cycle)
{
  std::size_t cpuIndex = 0;
  std::size_t timerIndex = 0;
  std::size_t interruptIndex = 0;
  while (cpuIndex < iopCoreComponent.stepEventCount ||
         timerIndex < iopTimersComponent.observationCount ||
         interruptIndex < iopInterruptControllerComponent.observationCount)
  {
    constexpr auto absent = std::numeric_limits<std::size_t>::max();
    const auto cpuSequence = cpuIndex < iopCoreComponent.stepEventCount
      ? iopCoreComponent.stepEvents[cpuIndex].sequence : absent;
    const auto timerSequence = timerIndex < iopTimersComponent.observationCount
      ? iopTimersComponent.observations[timerIndex].sequence : absent;
    const auto interruptSequence =
      interruptIndex < iopInterruptControllerComponent.observationCount
        ? iopInterruptControllerComponent.observations[interruptIndex].sequence
        : absent;
    if (cpuSequence < timerSequence && cpuSequence < interruptSequence)
    {
      publishIOPCPUTrace(cycle, iopCoreComponent.stepEvents[cpuIndex++]);
    }
    else if (timerSequence < interruptSequence)
    {
      publishIOPTimerTrace(cycle, iopTimersComponent.observations[timerIndex++]);
    }
    else
    {
      publishIOPInterruptTrace(
        cycle, iopInterruptControllerComponent.observations[interruptIndex++]);
    }
  }
}

void NekoSystem::publishIOPCPUTrace(
  std::uint64_t cycle, const IOPCore::StepEvent &event)
{
  using Kind = IOPCore::StepEventKind;
  using Type = NekoTraceEventType;
  constexpr auto subsystem = NekoTraceSubsystem::IOP;
  switch (event.kind)
  {
    case Kind::InstructionIssued:
    {
      const auto &issued = event.payload.instructionIssued;
      appendTrace(cycle, subsystem, Type::InstructionIssued,
        issued.address, issued.instruction, 0,
        issued.inBranchDelaySlot ? NekoIOPTraceInstruction::DELAY_SLOT : 0);
      return;
    }
    case Kind::InstructionRetired:
    {
      const auto &retired = event.payload.instructionRetired;
      appendTrace(cycle, subsystem, Type::InstructionRetired,
        retired.address, retired.nextAddress, retired.instructions, retired.cycles);
      return;
    }
    case Kind::BranchScheduled:
    {
      const auto &branch = event.payload.branchScheduled;
      appendTrace(cycle, subsystem, Type::BranchScheduled,
        branch.address, branch.target, branch.outcome == IOPBranchOutcome::Taken);
      return;
    }
    case Kind::MemoryAccess:
    {
      const auto &memory = event.payload.memoryAccess;
      NekoIOPTraceMemory::AccessKind access;
      switch (memory.kind)
      {
        case IOPCore::MemoryAccessKind::InstructionFetch:
          access = NekoIOPTraceMemory::AccessKind::InstructionFetch;
          break;
        case IOPCore::MemoryAccessKind::DataLoad:
          access = NekoIOPTraceMemory::AccessKind::DataLoad;
          break;
        case IOPCore::MemoryAccessKind::DataStore:
          access = NekoIOPTraceMemory::AccessKind::DataStore;
          break;
        default:
          throw std::logic_error("Unknown IOP trace memory kind.");
      }
      appendTrace(cycle, subsystem, Type::MemoryAccess,
        memory.virtualAddress, memory.value, 0, nekoPackIOPMemoryTraceMetadata({
          memory.physicalAddress, memory.width, access,
          traceOutcome(memory.outcome), traceCacheRoute(memory.cacheRoute),
          memory.physicalAddressValid, memory.byteEnable}));
      return;
    }
    case Kind::ExceptionEntered:
    {
      const auto &exception = event.payload.exceptionEntered;
      appendTrace(cycle, subsystem, Type::ExceptionEntered,
        (exception.cause >> 2) & 0x1f, exception.epc,
        exception.vector, exception.cause);
      return;
    }
    case Kind::InterruptDelivered:
    {
      const auto &interrupt = event.payload.interruptDelivered;
      appendTrace(cycle, subsystem, Type::InterruptDelivered,
        interrupt.address, interrupt.status, interrupt.cause, interrupt.vector);
      return;
    }
    case Kind::StateChanged:
    {
      const auto &state = event.payload.stateChanged;
      appendTrace(cycle, subsystem, Type::StateChanged,
        static_cast<std::uint8_t>(traceState(state.before)),
        static_cast<std::uint8_t>(traceState(state.after)),
        static_cast<std::uint8_t>(traceStopReason(state.reason)), state.address);
      return;
    }
  }
  throw std::logic_error("Unknown IOP trace event.");
}

void NekoSystem::publishIOPTimerTrace(
  std::uint64_t cycle, const IOPTimers::Observation &event)
{
  namespace Timer = NekoIOPTraceTimer;
  std::uint64_t metadata = event.index;
  if ((event.flags & IOPTimers::TargetReached) != 0) metadata |= Timer::TARGET_REACHED;
  if ((event.flags & IOPTimers::OverflowReached) != 0) metadata |= Timer::OVERFLOW_REACHED;
  if ((event.flags & IOPTimers::InterruptRequested) != 0) metadata |= Timer::INTERRUPT_REQUESTED;
  if ((event.flags & IOPTimers::PulseReleased) != 0) metadata |= Timer::PULSE_RELEASED;
  if ((event.flags & IOPTimers::GateChanged) != 0) metadata |= Timer::GATE_CHANGED;
  if ((event.flags & IOPTimers::ModeAcknowledged) != 0) metadata |= Timer::MODE_ACKNOWLEDGED;
  if ((event.flags & IOPTimers::RegisterWritten) != 0) metadata |= Timer::REGISTER_WRITTEN;
  if (event.sample.systemClockPulse) metadata |= Timer::SYSTEM_PULSE;
  if (event.sample.pixelClockPulse) metadata |= Timer::PIXEL_PULSE;
  if (event.gatesBefore.horizontalBlank) metadata |= Timer::HBLANK_BEFORE;
  if (event.sample.gates.horizontalBlank) metadata |= Timer::HBLANK_AFTER;
  if (event.gatesBefore.verticalBlank) metadata |= Timer::VBLANK_BEFORE;
  if (event.sample.gates.verticalBlank) metadata |= Timer::VBLANK_AFTER;
  Timer::Register reg;
  switch (event.registerKind)
  {
    case IOPTimers::Register::None: reg = Timer::Register::None; break;
    case IOPTimers::Register::Count: reg = Timer::Register::Count; break;
    case IOPTimers::Register::Mode: reg = Timer::Register::Mode; break;
    case IOPTimers::Register::Target: reg = Timer::Register::Target; break;
    default: throw std::logic_error("Unknown IOP trace timer register.");
  }
  metadata |= static_cast<std::uint64_t>(reg) << Timer::REGISTER_SHIFT;
  appendTrace(cycle, NekoTraceSubsystem::IOPTimers, NekoTraceEventType::TimerEvent,
    metadata, timerSnapshot(event.before), timerSnapshot(event.after),
    event.before.target | (static_cast<std::uint64_t>(event.after.target) << 32));
}

void NekoSystem::publishIOPInterruptTrace(
  std::uint64_t cycle, const IOPInterruptController::Observation &event)
{
  appendTrace(cycle, NekoTraceSubsystem::IOPInterruptController,
    NekoTraceEventType::InterruptChanged, event.status, event.mask, event.control,
    event.control != 0 && (event.status & event.mask) != 0);
}
