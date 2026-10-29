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

void NekoSystem::publishFailureProbe(
  std::uint64_t cycle, const SIFDMAObservations &dma)
{
  publishEECPUTrace(cycle);
  std::size_t cpu = 0;
  std::size_t transfer = 0;
  while (cpu < iopCoreComponent.stepEventCount || transfer < dma.count)
  {
    if (cpu < iopCoreComponent.stepEventCount &&
        (transfer == dma.count ||
         iopCoreComponent.stepEvents[cpu].sequence < dma.entries[transfer].sequence))
    {
      publishIOPCPUTrace(cycle, iopCoreComponent.stepEvents[cpu++]);
    }
    else
    {
      const auto &event = dma.entries[transfer++].event;
      appendTrace(cycle, event.subsystem, event.type,
        event.value0, event.value1, event.value2, event.value3);
    }
  }
}

void NekoSystem::publishIOPAndSIFTrace(
  std::uint64_t cycle, const SIFDMAObservations &dma)
{
  std::size_t cpuIndex = 0;
  std::size_t timerIndex = 0;
  std::size_t interruptIndex = 0;
  std::size_t sifIndex = 0;
  std::size_t dmaIndex = 0;
  std::size_t cdvdIndex = 0;
  while (cpuIndex < iopCoreComponent.stepEventCount ||
         timerIndex < iopTimersComponent.observationCount ||
         interruptIndex < iopInterruptControllerComponent.observationCount ||
         sifIndex < sifComponent.observationCount ||
         dmaIndex < dma.count ||
         cdvdIndex < cdvdControllerComponent.observationCount)
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
    const auto sifSequence = sifIndex < sifComponent.observationCount
      ? sifComponent.observations[sifIndex].sequence : absent;
    const auto dmaSequence = dmaIndex < dma.count
      ? dma.entries[dmaIndex].sequence : absent;
    const auto cdvdSequence = cdvdIndex < cdvdControllerComponent.observationCount
      ? cdvdControllerComponent.observations[cdvdIndex].sequence : absent;
    if (cdvdSequence < cpuSequence && cdvdSequence < timerSequence &&
        cdvdSequence < interruptSequence && cdvdSequence < sifSequence &&
        cdvdSequence < dmaSequence)
    {
      publishCDVDTrace(cycle, cdvdControllerComponent.observations[cdvdIndex++]);
    }
    else if (dmaSequence < cpuSequence && dmaSequence < timerSequence &&
        dmaSequence < interruptSequence && dmaSequence < sifSequence)
    {
      const auto &event = dma.entries[dmaIndex++].event;
      appendTrace(cycle, event.subsystem, event.type,
        event.value0, event.value1, event.value2, event.value3);
    }
    else if (sifSequence < cpuSequence && sifSequence < timerSequence &&
        sifSequence < interruptSequence)
    {
      publishSIFTrace(cycle, sifComponent.observations[sifIndex++]);
    }
    else if (cpuSequence < timerSequence && cpuSequence < interruptSequence)
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

void NekoSystem::publishCDVDTrace(
  std::uint64_t cycle, const CDVDController::Observation &event)
{
  using Phase = NekoCDVDTrace::Phase;
  const auto phase = [](CDVDController::SubcommandPhase value)
  {
    switch (value)
    {
      case CDVDController::SubcommandPhase::Idle: return Phase::Idle;
      case CDVDController::SubcommandPhase::Pending: return Phase::Pending;
      case CDVDController::SubcommandPhase::Result: return Phase::Result;
    }
    throw std::logic_error("Unknown CD/DVD trace phase.");
  };
  NekoCDVDTrace::Command command;
  command.transport = event.transport == CDVDController::Transport::Normal
    ? NekoCDVDTrace::Transport::Normal : NekoCDVDTrace::Transport::Subcommand;
  command.opcode = event.opcode;
  command.beforePhase = phase(event.beforePhase);
  command.afterPhase = phase(event.afterPhase);
  command.beforeCause = event.beforeCause;
  command.afterCause = event.afterCause;
  command.rtcTicks = event.rtcTicks;
  appendTrace(cycle, NekoTraceSubsystem::CDVD,
    event.kind == CDVDController::ObservationKind::Accepted
      ? NekoTraceEventType::CommandAccepted : NekoTraceEventType::CommandCompleted,
    nekoPackCDVDTraceMetadata(command), command.rtcTicks,
    nekoPackCDVDTraceResult(event.result));
}

void NekoSystem::publishSIFTrace(
  std::uint64_t cycle, const SIF::Observation &event)
{
  using Register = NekoSIFTrace::Register;
  Register reg;
  if (event.port == SIF::ObservationPort::IOPDoorbell)
  {
    reg = Register::IOPDoorbell;
  }
  else
  {
    switch (event.reg)
    {
      case SIFRegister::MSCOM: reg = Register::MSCOM; break;
      case SIFRegister::SMCOM: reg = Register::SMCOM; break;
      case SIFRegister::MSFLAG: reg = Register::MSFLAG; break;
      case SIFRegister::SMFLAG: reg = Register::SMFLAG; break;
      case SIFRegister::CTRL: reg = Register::CTRL; break;
      default: throw std::logic_error("Unknown SIF trace register.");
    }
  }
  const auto side = event.port == SIF::ObservationPort::EE
    ? NekoSIFTrace::Side::EE : NekoSIFTrace::Side::IOP;
  const auto outcome = event.result.outcome == SIFWriteOutcome::Completed
    ? NekoSIFTrace::Outcome::Completed
    : NekoSIFTrace::Outcome::UnsupportedControl;
  const std::uint64_t metadata = static_cast<std::uint64_t>(reg) |
    (static_cast<std::uint64_t>(side) << NekoSIFTrace::SIDE_SHIFT) |
    (static_cast<std::uint64_t>(outcome) << NekoSIFTrace::OUTCOME_SHIFT);
  appendTrace(cycle, NekoTraceSubsystem::SIF, NekoTraceEventType::RegisterWrite,
    metadata, event.value, event.before, event.after);
  if (event.result.signal != SIFSignal::None)
  {
    const auto signal = event.result.signal == SIFSignal::RequestIOP
      ? NekoSIFTrace::Signal::RequestIOP : NekoSIFTrace::Signal::RequestEE;
    appendTrace(cycle, NekoTraceSubsystem::SIF,
      NekoTraceEventType::SignalRequested,
      static_cast<std::uint64_t>(signal), metadata, event.value);
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
