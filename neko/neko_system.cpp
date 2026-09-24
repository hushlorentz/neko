#include <new>
#include <stdexcept>

#include "neko_system.hpp"

NekoSystem::NekoSystem() :
  vu0Component(VPUType::VU0),
  vu1Component(VPUType::VU1),
  vif0Component(VIFType::VIF0),
  vif1Component(VIFType::VIF1),
  gifPathArbiterComponent(&gifDecoderComponent),
  gifPath1Component(gifPathArbiterComponent),
  gifPath3Component(gifPathArbiterComponent),
  gifRegisterFile(
    &gifPathArbiterComponent,
    &gifPath3Component),
  eeBusComponent(
    &vif0Component,
    &vif1Component,
    &gifRegisterFile,
    &gifPath3Component,
    &gsComponent,
    &interruptControllerComponent),
  gifDMACComponent(
    &eeBusComponent,
    &dmacControllerComponent),
  vif1DMACComponent(
    &eeBusComponent,
    &dmacControllerComponent),
  gsDisplayComponent(&gsComponent)
{
  masterClock.registerComponent(eeCoreComponent, 1);
  vif0Component.attachVPU(&vu0Component);
  vif1Component.attachVPU(&vu1Component);
  vif1Component.attachGIFPathArbiter(&gifPathArbiterComponent);
  gifPath1Component.attachVPU(&vu1Component);
  gifDecoderComponent.attachRegisterWriteHandler(&gsComponent);
  masterClock.registerComponent(
    vu0Component,
    VU_CLOCK_PERIOD);
  masterClock.registerComponent(
    vu1Component,
    VU_CLOCK_PERIOD);
  eeBusComponent.attachDMACController(
    &dmacControllerComponent);
  eeBusComponent.attachGIFDMACChannel(&gifDMACComponent);
  eeBusComponent.attachVIF1DMACChannel(&vif1DMACComponent);
  eeBusComponent.attachGSDisplay(&gsDisplayComponent);
  eeCoreComponent.attachBus(&eeBusComponent);
  eeCoreComponent.attachVU0(&vu0Component);
  eeCoreComponent.attachVU1(&vu1Component);
  masterClock.registerComponent(gifDMACComponent, 1);
  masterClock.registerComponent(vif1DMACComponent, 1);
  masterClock.registerComponent(gsDisplayComponent, 1);
}

void NekoSystem::reset()
{
  // Reset all pointer-linked hardware as one coherent machine.
  this->~NekoSystem();
  new (this) NekoSystem();
}

void NekoSystem::setInput(const NekoInputState &input)
{
  inputState = input;
  if (collectingTrace)
  {
    appendTrace(
      masterClock.currentCycle(),
      NekoTraceSubsystem::Input,
      NekoTraceEventType::StateChanged,
      input.buttons,
      static_cast<std::uint64_t>(input.leftStickX) |
        (static_cast<std::uint64_t>(input.leftStickY) << 8),
      static_cast<std::uint64_t>(input.rightStickX) |
        (static_cast<std::uint64_t>(input.rightStickY) << 8));
  }
}

const NekoInputState &NekoSystem::input() const
{
  return inputState;
}

NekoFrameResult NekoSystem::runFrame()
{
  const std::uint64_t startingBoundary =
    gsDisplayComponent.presentationBoundaryCount();
  std::uint64_t elapsedCycles = 0;
  while (gsDisplayComponent.presentationBoundaryCount() ==
         startingBoundary)
  {
    clockMasterCycle();
    ++elapsedCycles;
  }

  NekoFrameResult result;
  result.masterCycles = elapsedCycles;
  result.presentationBoundary =
    gsDisplayComponent.presentationBoundaryCount();
  result.video = videoOutput();
  result.videoHash = nekoFrameHash(result.video);
  result.eeStateHash = eeCoreComponent.stateHash();
  result.audio = audioOutput();
  return result;
}

GSPresentation NekoSystem::videoOutput() const
{
  return gsDisplayComponent.presentation();
}

NekoAudioFrame NekoSystem::audioOutput() const
{
  return {};
}

EEELFLoadResult NekoSystem::loadELF(
  const std::vector<std::uint8_t> &image)
{
  if (eeCoreComponent.executionState() !=
      EEExecutionState::Halted)
  {
    throw std::logic_error(
      "An ELF cannot be loaded while the EE is running.");
  }
  const EEELFLoadResult result =
    loadEEELF(image, &eeBusComponent);
  eeCoreComponent.prepareFreshExecution(
    result.entryPoint,
    EEGuestRuntime::STACK_POINTER,
    EEGuestRuntime::RETURN_ADDRESS);
  return result;
}

EEGuestExecutionResult NekoSystem::runELF(
  const std::vector<std::uint8_t> &image,
  std::uint64_t maxMasterCycles)
{
  EEGuestExecutionResult result;
  result.load = loadELF(image);
  eeCoreComponent.startExecution(result.load.entryPoint);

  const std::uint64_t startingEECycles =
    eeCoreComponent.elapsedCycles();
  eeCoreComponent.exceptionEnteredThisCycle = false;
  std::uint64_t masterCycles = 0;
  std::uint64_t instructions = 0;
  bool returned = false;
  while (eeCoreComponent.clockActive() &&
         masterCycles < maxMasterCycles &&
         !eeCoreComponent.exceptionEnteredThisCycle)
  {
    if (eeCoreComponent.programCounter() ==
        EEGuestRuntime::RETURN_ADDRESS)
    {
      if (eeCoreComponent.drainInFlightExecution())
      {
        eeCoreComponent.haltExecution();
        returned = true;
      }
      break;
    }
    clockMasterCycle();
    ++masterCycles;
    instructions +=
      eeCoreComponent.acceptanceRecords.instructionCount();
  }

  if (eeCoreComponent.clockActive() &&
      !eeCoreComponent.exceptionEnteredThisCycle &&
      eeCoreComponent.programCounter() ==
        EEGuestRuntime::RETURN_ADDRESS)
  {
    if (eeCoreComponent.drainInFlightExecution())
    {
      eeCoreComponent.haltExecution();
      returned = true;
    }
  }
  const bool cycleLimitReached =
    eeCoreComponent.clockActive() &&
    !eeCoreComponent.exceptionEnteredThisCycle &&
    masterCycles == maxMasterCycles;
  result.execution = makeEEExecutionResult(
    masterCycles,
    startingEECycles,
    instructions,
    cycleLimitReached);
  result.exitCode = static_cast<std::uint32_t>(
    eeCoreComponent.generalRegister(
      EEGuestRuntime::EXIT_CODE_REGISTER).low);

  if (returned)
  {
    result.outcome =
      result.exitCode == 0 ?
        EEGuestOutcome::Completed :
        EEGuestOutcome::GuestReportedFailure;
  }
  else if (cycleLimitReached)
  {
    result.outcome = EEGuestOutcome::CycleLimitReached;
  }
  else if (result.execution.pendingException != EEException::None)
  {
    result.outcome = EEGuestOutcome::Exception;
  }
  else
  {
    result.outcome = EEGuestOutcome::Stopped;
  }
  return result;
}

void NekoSystem::startTrace()
{
  traceEvents.clear();
  eeCoreComponent.cycleEventCount = 0;
  lastTracedEEStateHash = eeCoreComponent.stateHash();
  collectingTrace = true;
}

void NekoSystem::stopTrace()
{
  eeCoreComponent.cycleEventCount = 0;
  lastTracedEEStateHash = 0;
  collectingTrace = false;
}

void NekoSystem::clearTrace()
{
  traceEvents.clear();
  eeCoreComponent.cycleEventCount = 0;
  if (collectingTrace)
  {
    lastTracedEEStateHash = eeCoreComponent.stateHash();
  }
}

bool NekoSystem::traceEnabled() const
{
  return collectingTrace;
}

const std::vector<NekoTraceEvent> &NekoSystem::trace() const
{
  return traceEvents;
}

std::uint64_t NekoSystem::traceHash() const
{
  return nekoTraceHash(traceEvents);
}

EECore &NekoSystem::eeCore()
{
  return eeCoreComponent;
}

const EECore &NekoSystem::eeCore() const
{
  return eeCoreComponent;
}

VPU &NekoSystem::vu0()
{
  return vu0Component;
}

const VPU &NekoSystem::vu0() const
{
  return vu0Component;
}

VPU &NekoSystem::vu1()
{
  return vu1Component;
}

const VPU &NekoSystem::vu1() const
{
  return vu1Component;
}

VIF &NekoSystem::vif0()
{
  return vif0Component;
}

const VIF &NekoSystem::vif0() const
{
  return vif0Component;
}

VIF &NekoSystem::vif1()
{
  return vif1Component;
}

const VIF &NekoSystem::vif1() const
{
  return vif1Component;
}

GIFDecoder &NekoSystem::gifDecoder()
{
  return gifDecoderComponent;
}

const GIFDecoder &NekoSystem::gifDecoder() const
{
  return gifDecoderComponent;
}

GIFPathArbiter &NekoSystem::gifPathArbiter()
{
  return gifPathArbiterComponent;
}

const GIFPathArbiter &NekoSystem::gifPathArbiter() const
{
  return gifPathArbiterComponent;
}

GIFPath1Transfer &NekoSystem::gifPath1()
{
  return gifPath1Component;
}

const GIFPath1Transfer &NekoSystem::gifPath1() const
{
  return gifPath1Component;
}

GIFPath3Transfer &NekoSystem::gifPath3()
{
  return gifPath3Component;
}

const GIFPath3Transfer &NekoSystem::gifPath3() const
{
  return gifPath3Component;
}

GS &NekoSystem::gs()
{
  return gsComponent;
}

const GS &NekoSystem::gs() const
{
  return gsComponent;
}

GIFRegisters &NekoSystem::gifRegisters()
{
  return gifRegisterFile;
}

const GIFRegisters &NekoSystem::gifRegisters() const
{
  return gifRegisterFile;
}

DMACController &NekoSystem::dmacController()
{
  return dmacControllerComponent;
}

const DMACController &NekoSystem::dmacController() const
{
  return dmacControllerComponent;
}

GIFDMACChannel &NekoSystem::gifDMAC()
{
  return gifDMACComponent;
}

const GIFDMACChannel &NekoSystem::gifDMAC() const
{
  return gifDMACComponent;
}

VIF1DMACChannel &NekoSystem::vif1DMAC()
{
  return vif1DMACComponent;
}

const VIF1DMACChannel &NekoSystem::vif1DMAC() const
{
  return vif1DMACComponent;
}

GSDisplay &NekoSystem::gsDisplay()
{
  return gsDisplayComponent;
}

const GSDisplay &NekoSystem::gsDisplay() const
{
  return gsDisplayComponent;
}

EEBus &NekoSystem::eeBus()
{
  return eeBusComponent;
}

const EEBus &NekoSystem::eeBus() const
{
  return eeBusComponent;
}

EEInterruptController &NekoSystem::interruptController()
{
  return interruptControllerComponent;
}

const EEInterruptController &
NekoSystem::interruptController() const
{
  return interruptControllerComponent;
}

void NekoSystem::latchComponentInterrupts()
{
  if (vif0Component.interruptPending())
  {
    interruptControllerComponent.setSource(
      EEInterruptSource::VIF0,
      true);
  }
  if (vif1Component.interruptPending())
  {
    interruptControllerComponent.setSource(
      EEInterruptSource::VIF1,
      true);
  }
  if (gsDisplayComponent.interruptPending())
  {
    interruptControllerComponent.setSource(
      EEInterruptSource::GS,
      true);
  }
  if (gsDisplayComponent.takeVerticalBlankStart())
  {
    interruptControllerComponent.setSource(
      EEInterruptSource::VBLANK_START,
      true);
  }
  if (gsDisplayComponent.takeVerticalBlankEnd())
  {
    interruptControllerComponent.setSource(
      EEInterruptSource::VBLANK_END,
      true);
  }
}

void NekoSystem::publishEEInterruptLines()
{
  eeCoreComponent.setInterruptLines(
    interruptControllerComponent.interruptPending(),
    dmacControllerComponent.interruptPending());
}

NekoSystem::CycleObservationSnapshot
NekoSystem::captureCycleObservation() const
{
  return {
    vu0Component.elapsedCycles(),
    vu1Component.elapsedCycles(),
    vif0Component.wordsIngested(),
    vif1Component.wordsIngested(),
    gifPath1Component.transferredQuadwordCount() +
      gifPath3Component.transferredQuadwordCount(),
    gifDMACComponent.transferredQuadwordCount(),
    gifDMACComponent.channelControl(),
    interruptControllerComponent.status(),
    gsComponent.pixelWriteCount(),
    gsDisplayComponent.presentationBoundaryCount()
  };
}

void NekoSystem::clockMasterCycle()
{
  latchComponentInterrupts();
  publishEEInterruptLines();
  eeBusComponent.advanceGuestFIFOs();
  CycleObservationSnapshot beforeCycle;
  if (collectingTrace)
  {
    beforeCycle = captureCycleObservation();
  }
  masterClock.clock();
  latchComponentInterrupts();
  publishEEInterruptLines();
  if (collectingTrace)
  {
    publishCycleTrace(
      masterClock.currentCycle(),
      beforeCycle);
  }
  eeCoreComponent.cycleEventCount = 0;
}

std::uint64_t NekoSystem::runMasterCycles(std::uint64_t cycles)
{
  for (std::uint64_t cycle = 0; cycle < cycles; ++cycle)
  {
    clockMasterCycle();
  }
  return cycles;
}

EEExecutionResult NekoSystem::stepEEInstruction(
  std::uint64_t maxMasterCycles)
{
  const std::uint64_t startingEECycles =
    eeCoreComponent.elapsedCycles();
  eeCoreComponent.exceptionEnteredThisCycle = false;
  std::uint64_t masterCycles = 0;
  std::uint64_t instructions = 0;
  while (eeCoreComponent.clockActive() &&
         masterCycles < maxMasterCycles &&
         instructions == 0 &&
         !eeCoreComponent.exceptionEnteredThisCycle)
  {
    clockMasterCycle();
    ++masterCycles;
    instructions +=
      eeCoreComponent.acceptanceRecords.instructionCount();
  }

  return makeEEExecutionResult(
    masterCycles,
    startingEECycles,
    instructions,
    eeCoreComponent.clockActive() &&
      !eeCoreComponent.exceptionEnteredThisCycle &&
      instructions == 0 &&
      masterCycles == maxMasterCycles);
}

EEExecutionResult NekoSystem::runEE(
  std::uint64_t maxMasterCycles)
{
  const std::uint64_t startingEECycles =
    eeCoreComponent.elapsedCycles();
  eeCoreComponent.exceptionEnteredThisCycle = false;
  std::uint64_t masterCycles = 0;
  std::uint64_t instructions = 0;
  while (eeCoreComponent.clockActive() &&
         masterCycles < maxMasterCycles &&
         !eeCoreComponent.exceptionEnteredThisCycle)
  {
    clockMasterCycle();
    ++masterCycles;
    instructions +=
      eeCoreComponent.acceptanceRecords.instructionCount();
  }

  return makeEEExecutionResult(
    masterCycles,
    startingEECycles,
    instructions,
    eeCoreComponent.clockActive() &&
      !eeCoreComponent.exceptionEnteredThisCycle &&
      masterCycles == maxMasterCycles);
}

bool NekoSystem::interruptPending() const
{
  return
    interruptControllerComponent.interruptPending() ||
    dmacControllerComponent.interruptPending();
}

EEExecutionResult NekoSystem::makeEEExecutionResult(
  std::uint64_t masterCycles,
  std::uint64_t startingEECycles,
  std::uint64_t instructions,
  bool cycleLimitReached) const
{
  return {
    masterCycles,
    eeCoreComponent.elapsedCycles() - startingEECycles,
    instructions,
    cycleLimitReached,
    eeCoreComponent.executionState(),
    eeCoreComponent.stopReason(),
    eeCoreComponent.programCounter(),
    eeCoreComponent.pendingException(),
    eeCoreComponent.exceptionAddress()
  };
}

void NekoSystem::publishCycleTrace(
  std::uint64_t cycle,
  const CycleObservationSnapshot &beforeCycle)
{
  for (std::size_t index = 0;
       index < eeCoreComponent.cycleEventCount;
       ++index)
  {
    const EECore::CycleEvent &event =
      eeCoreComponent.cycleEvents[index];
    NekoTraceEventType type =
      NekoTraceEventType::InstructionIssued;
    std::uint64_t value0 = 0;
    std::uint64_t value1 = 0;
    std::uint64_t value2 = 0;
    std::uint64_t value3 = 0;
    switch (event.kind)
    {
      case EECore::CycleEventKind::InstructionIssued:
      {
        const EECore::InstructionIssuedEvent &issued =
          event.payload.instructionIssued;
        type = NekoTraceEventType::InstructionIssued;
        value0 = issued.address;
        value1 = issued.instruction;
        value2 = static_cast<std::uint8_t>(issued.operation);
        value3 =
          issued.mode == EEAcceptanceMode::DelaySlot;
        break;
      }
      case EECore::CycleEventKind::BranchScheduled:
      {
        const EECore::BranchScheduledEvent &branch =
          event.payload.branchScheduled;
        type = NekoTraceEventType::BranchScheduled;
        value0 = branch.address;
        value1 = branch.target;
        value2 =
          (branch.outcome == EECore::BranchOutcome::Taken
            ? UINT64_C(1)
            : 0) |
          (branch.mode == EECore::BranchMode::Likely
            ? UINT64_C(2)
            : 0);
        break;
      }
      case EECore::CycleEventKind::MemoryAccess:
      {
        const EECore::MemoryAccessEvent &memory =
          event.payload.memoryAccess;
        type = NekoTraceEventType::MemoryAccess;
        value0 = memory.address;
        value1 = memory.low;
        value2 = memory.high;
        value3 =
          memory.width |
          (memory.direction ==
              EECore::MemoryAccessDirection::Write
            ? UINT64_C(1) << 8
            : 0) |
          (memory.outcome ==
              EECore::MemoryAccessOutcome::Succeeded
            ? UINT64_C(1) << 9
            : 0);
        break;
      }
      case EECore::CycleEventKind::ExceptionEntered:
      {
        const EECore::ExceptionEnteredEvent &exception =
          event.payload.exceptionEntered;
        type = NekoTraceEventType::ExceptionEntered;
        value0 = static_cast<std::uint8_t>(exception.type);
        value1 = exception.address;
        value2 = exception.vector;
        value3 = exception.cause;
        break;
      }
      case EECore::CycleEventKind::InterruptDelivered:
      {
        const EECore::InterruptDeliveredEvent &interrupt =
          event.payload.interruptDelivered;
        type = NekoTraceEventType::InterruptDelivered;
        value0 = interrupt.instructionAddress;
        value1 = interrupt.status;
        value2 = interrupt.cause;
        value3 = interrupt.vector;
        break;
      }
      case EECore::CycleEventKind::COP1LoadInterlock:
      {
        const EECore::COP1LoadInterlockEvent &interlock =
          event.payload.cop1LoadInterlock;
        type = NekoTraceEventType::COP1LoadInterlock;
        value0 = interlock.instructionAddress;
        value1 = interlock.instruction;
        value2 = interlock.registerIndex;
        value3 =
          static_cast<std::uint8_t>(interlock.dependency);
        break;
      }
      case EECore::CycleEventKind::COP1ResourceInterlock:
      {
        const EECore::COP1ResourceInterlockEvent &interlock =
          event.payload.cop1ResourceInterlock;
        type = NekoTraceEventType::COP1ResourceInterlock;
        value0 = interlock.instructionAddress;
        value1 = interlock.instruction;
        value2 = interlock.resource;
        value3 =
          static_cast<std::uint8_t>(interlock.dependency);
        break;
      }
      case EECore::CycleEventKind::COP1DividerHazard:
      {
        const EECore::COP1DividerHazardEvent &hazard =
          event.payload.cop1DividerHazard;
        type = NekoTraceEventType::COP1DividerHazard;
        value0 = hazard.instructionAddress;
        value1 = hazard.instruction;
        value2 = hazard.reasons;
        value3 =
          hazard.branchAddress |
          (static_cast<std::uint64_t>(
            hazard.targetAddress) << 32);
        break;
      }
      case EECore::CycleEventKind::COP1StageTransition:
      {
        const EECore::COP1StageTransitionEvent &transition =
          event.payload.cop1StageTransition;
        type = NekoTraceEventType::COP1StageTransition;
        value0 = transition.programOrder;
        value1 =
          transition.instructionAddress |
          (static_cast<std::uint64_t>(
            transition.instruction) << 32);
        value2 =
          transition.fromStage |
          (static_cast<std::uint64_t>(
            static_cast<std::uint8_t>(
              transition.toStage)) << 8) |
          (static_cast<std::uint64_t>(
            transition.remainingCycles) << 16);
        value3 =
          transition.destinationMask |
          (static_cast<std::uint64_t>(
            transition.destinationFPR) << 8) |
          (static_cast<std::uint64_t>(
            transition.destinationGPR) << 16);
        break;
      }
      case EECore::CycleEventKind::COP1Retired:
      {
        const EECore::COP1RetiredEvent &retirement =
          event.payload.cop1Retired;
        type = NekoTraceEventType::COP1Retired;
        value0 = retirement.programOrder;
        value1 =
          retirement.instructionAddress |
          (static_cast<std::uint64_t>(
            retirement.instruction) << 32);
        value2 =
          retirement.rawResult |
          (static_cast<std::uint64_t>(
            retirement.destinationMask) << 32) |
          (static_cast<std::uint64_t>(
            retirement.destinationFPR) << 40) |
          (static_cast<std::uint64_t>(
            retirement.destinationGPR) << 48);
        value3 =
          retirement.affectedFlags |
          (static_cast<std::uint64_t>(
            retirement.raisedFlags) << 8) |
          (static_cast<std::uint64_t>(
            retirement.raisedStickyFlags) << 16) |
          (retirement.conditionResult
            ? UINT64_C(1) << 24
            : 0);
        break;
      }
    }
    appendTrace(
      cycle,
      NekoTraceSubsystem::EE,
      type,
      value0,
      value1,
      value2,
      value3);
  }
  const std::uint64_t currentEEStateHash =
    eeCoreComponent.stateHash();
  if (currentEEStateHash != lastTracedEEStateHash)
  {
    appendTrace(
      cycle,
      NekoTraceSubsystem::EE,
      NekoTraceEventType::StateSnapshot,
      currentEEStateHash,
      eeCoreComponent.elapsedCycles(),
      eeCoreComponent.programCounter(),
      (static_cast<std::uint64_t>(
        eeCoreComponent.cop0Register(
          EECOP0Register::Status)) << 32) |
        eeCoreComponent.cop0Register(
          EECOP0Register::Cause));
    lastTracedEEStateHash = currentEEStateHash;
  }
  if (vu0Component.elapsedCycles() != beforeCycle.vu0Cycles)
  {
    appendTrace(
      cycle,
      NekoTraceSubsystem::VU0,
      NekoTraceEventType::Progress,
      vu0Component.elapsedCycles(),
      vu0Component.programCounter(),
      vu0Component.getState());
  }
  if (vu1Component.elapsedCycles() != beforeCycle.vu1Cycles)
  {
    appendTrace(
      cycle,
      NekoTraceSubsystem::VU1,
      NekoTraceEventType::Progress,
      vu1Component.elapsedCycles(),
      vu1Component.programCounter(),
      vu1Component.getState());
  }
  if (vif0Component.wordsIngested() != beforeCycle.vif0Words)
  {
    appendTrace(
      cycle,
      NekoTraceSubsystem::VIF0,
      NekoTraceEventType::Progress,
      vif0Component.wordsIngested(),
      vif0Component.payloadWordsRemaining(),
      vif0Component.interruptPending());
  }
  if (vif1Component.wordsIngested() != beforeCycle.vif1Words)
  {
    appendTrace(
      cycle,
      NekoTraceSubsystem::VIF1,
      NekoTraceEventType::Progress,
      vif1Component.wordsIngested(),
      vif1Component.payloadWordsRemaining(),
      vif1Component.interruptPending());
  }
  const std::uint64_t currentGIFQuadwords =
    gifPath1Component.transferredQuadwordCount() +
    gifPath3Component.transferredQuadwordCount();
  if (currentGIFQuadwords != beforeCycle.gifQuadwords)
  {
    appendTrace(
      cycle,
      NekoTraceSubsystem::GIF,
      NekoTraceEventType::Progress,
      currentGIFQuadwords,
      static_cast<std::uint8_t>(
        gifPathArbiterComponent.activePath()),
      gifDecoderComponent.quadwordsRemaining());
  }
  if (gifDMACComponent.transferredQuadwordCount() !=
      beforeCycle.dmacQuadwords)
  {
    appendTrace(
      cycle,
      NekoTraceSubsystem::GIFDMAC,
      NekoTraceEventType::Progress,
      gifDMACComponent.transferredQuadwordCount(),
      gifDMACComponent.memoryAddress(),
      gifDMACComponent.quadwordCount());
  }
  if ((beforeCycle.dmacControl &
       GIFDMACChannelControl::START) != 0 &&
      (gifDMACComponent.channelControl() &
       GIFDMACChannelControl::START) == 0)
  {
    appendTrace(
      cycle,
      NekoTraceSubsystem::GIFDMAC,
      NekoTraceEventType::TransferCompleted,
      dmacControllerComponent.status());
  }
  if (gsComponent.pixelWriteCount() != beforeCycle.pixels)
  {
    appendTrace(
      cycle,
      NekoTraceSubsystem::GS,
      NekoTraceEventType::Progress,
      gsComponent.pixelWriteCount());
  }
  if (interruptControllerComponent.status() !=
      beforeCycle.interruptStatus)
  {
    appendTrace(
      cycle,
      NekoTraceSubsystem::InterruptController,
      NekoTraceEventType::InterruptChanged,
      interruptControllerComponent.status(),
      interruptControllerComponent.mask());
  }
  if (gsDisplayComponent.presentationBoundaryCount() !=
      beforeCycle.presentationBoundary)
  {
    appendTrace(
      cycle,
      NekoTraceSubsystem::Display,
      NekoTraceEventType::PresentationBoundary,
      gsDisplayComponent.presentationBoundaryCount(),
      nekoFrameHash(videoOutput()));
  }
}

void NekoSystem::appendTrace(
  std::uint64_t cycle,
  NekoTraceSubsystem subsystem,
  NekoTraceEventType type,
  std::uint64_t value0,
  std::uint64_t value1,
  std::uint64_t value2,
  std::uint64_t value3)
{
  traceEvents.push_back({
    cycle,
    subsystem,
    type,
    value0,
    value1,
    value2,
    value3
  });
}

MasterClockScheduler &NekoSystem::masterClockScheduler()
{
  return masterClock;
}

const MasterClockScheduler &
NekoSystem::masterClockScheduler() const
{
  return masterClock;
}
