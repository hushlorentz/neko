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
  gifRegisterFile(&gifPathArbiterComponent),
  eeBusComponent(
    &vif0Component,
    &vif1Component,
    &gifRegisterFile,
    &gifPath3Component,
    &gsComponent,
    &interruptControllerComponent),
  gifDMACComponent(&eeBusComponent),
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
  eeBusComponent.attachGIFDMACChannel(&gifDMACComponent);
  eeBusComponent.attachGSDisplay(&gsDisplayComponent);
  eeCoreComponent.attachBus(&eeBusComponent);
  masterClock.registerComponent(gifDMACComponent, 1);
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
      eeCoreComponent.haltExecution();
      returned = true;
      break;
    }
    clockMasterCycle();
    ++masterCycles;
    if (eeCoreComponent.instructionRetiredThisCycle)
    {
      ++instructions;
    }
  }

  if (eeCoreComponent.clockActive() &&
      !eeCoreComponent.exceptionEnteredThisCycle &&
      eeCoreComponent.programCounter() ==
        EEGuestRuntime::RETURN_ADDRESS)
  {
    eeCoreComponent.haltExecution();
    returned = true;
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
  eeCoreComponent.cycleTraceEventCount = 0;
  eeCoreComponent.cycleTraceEnabled = true;
  lastTracedEEStateHash = eeCoreComponent.stateHash();
  collectingTrace = true;
}

void NekoSystem::stopTrace()
{
  eeCoreComponent.cycleTraceEnabled = false;
  eeCoreComponent.cycleTraceEventCount = 0;
  lastTracedEEStateHash = 0;
  collectingTrace = false;
}

void NekoSystem::clearTrace()
{
  traceEvents.clear();
  eeCoreComponent.cycleTraceEventCount = 0;
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

GIFDMACChannel &NekoSystem::gifDMAC()
{
  return gifDMACComponent;
}

const GIFDMACChannel &NekoSystem::gifDMAC() const
{
  return gifDMACComponent;
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

void NekoSystem::synchronizeInterrupts()
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

void NekoSystem::clockMasterCycle()
{
  synchronizeInterrupts();
  synchronizeEEInterruptLines();
  const std::uint64_t vu0Cycles = vu0Component.elapsedCycles();
  const std::uint64_t vu1Cycles = vu1Component.elapsedCycles();
  const std::uint64_t vif0Words = vif0Component.wordsIngested();
  const std::uint64_t vif1Words = vif1Component.wordsIngested();
  const std::uint64_t gifQuadwords =
    gifPath1Component.transferredQuadwordCount() +
    gifPath3Component.transferredQuadwordCount();
  const std::uint64_t dmacQuadwords =
    gifDMACComponent.transferredQuadwordCount();
  const std::uint32_t dmacControl =
    gifDMACComponent.channelControl();
  const std::uint32_t interruptStatus =
    interruptControllerComponent.status();
  const std::uint64_t pixels = gsComponent.pixelWriteCount();
  const std::uint64_t presentationBoundary =
    gsDisplayComponent.presentationBoundaryCount();
  masterClock.clock();
  synchronizeInterrupts();
  synchronizeEEInterruptLines();
  if (collectingTrace)
  {
    recordCycleTrace(
      masterClock.currentCycle(),
      vu0Cycles,
      vu1Cycles,
      vif0Words,
      vif1Words,
      gifQuadwords,
      dmacQuadwords,
      dmacControl,
      interruptStatus,
      pixels,
      presentationBoundary);
  }
  eeCoreComponent.cycleTraceEventCount = 0;
}

void NekoSystem::synchronizeEEInterruptLines()
{
  eeCoreComponent.setInterruptLines(
    interruptControllerComponent.interruptPending(),
    gifDMACComponent.interruptPending());
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
    if (eeCoreComponent.instructionRetiredThisCycle)
    {
      ++instructions;
    }
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
    if (eeCoreComponent.instructionRetiredThisCycle)
    {
      ++instructions;
    }
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
    gifDMACComponent.interruptPending();
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

void NekoSystem::recordCycleTrace(
  std::uint64_t cycle,
  std::uint64_t vu0Cycles,
  std::uint64_t vu1Cycles,
  std::uint64_t vif0Words,
  std::uint64_t vif1Words,
  std::uint64_t gifQuadwords,
  std::uint64_t dmacQuadwords,
  std::uint32_t dmacControl,
  std::uint32_t interruptStatus,
  std::uint64_t pixels,
  std::uint64_t presentationBoundary)
{
  for (std::size_t index = 0;
       index < eeCoreComponent.cycleTraceEventCount;
       ++index)
  {
    const EECore::CycleTraceEvent &event =
      eeCoreComponent.cycleTraceEvents[index];
    NekoTraceEventType type =
      NekoTraceEventType::InstructionIssued;
    switch (event.kind)
    {
      case EECore::CycleTraceKind::InstructionIssued:
        type = NekoTraceEventType::InstructionIssued;
        break;
      case EECore::CycleTraceKind::BranchScheduled:
        type = NekoTraceEventType::BranchScheduled;
        break;
      case EECore::CycleTraceKind::MemoryAccess:
        type = NekoTraceEventType::MemoryAccess;
        break;
      case EECore::CycleTraceKind::ExceptionEntered:
        type = NekoTraceEventType::ExceptionEntered;
        break;
      case EECore::CycleTraceKind::InterruptDelivered:
        type = NekoTraceEventType::InterruptDelivered;
        break;
    }
    appendTrace(
      cycle,
      NekoTraceSubsystem::EE,
      type,
      event.value0,
      event.value1,
      event.value2,
      event.value3);
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
  if (vu0Component.elapsedCycles() != vu0Cycles)
  {
    appendTrace(
      cycle,
      NekoTraceSubsystem::VU0,
      NekoTraceEventType::Progress,
      vu0Component.elapsedCycles(),
      vu0Component.programCounter(),
      vu0Component.getState());
  }
  if (vu1Component.elapsedCycles() != vu1Cycles)
  {
    appendTrace(
      cycle,
      NekoTraceSubsystem::VU1,
      NekoTraceEventType::Progress,
      vu1Component.elapsedCycles(),
      vu1Component.programCounter(),
      vu1Component.getState());
  }
  if (vif0Component.wordsIngested() != vif0Words)
  {
    appendTrace(
      cycle,
      NekoTraceSubsystem::VIF0,
      NekoTraceEventType::Progress,
      vif0Component.wordsIngested(),
      vif0Component.payloadWordsRemaining(),
      vif0Component.interruptPending());
  }
  if (vif1Component.wordsIngested() != vif1Words)
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
  if (currentGIFQuadwords != gifQuadwords)
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
      dmacQuadwords)
  {
    appendTrace(
      cycle,
      NekoTraceSubsystem::GIFDMAC,
      NekoTraceEventType::Progress,
      gifDMACComponent.transferredQuadwordCount(),
      gifDMACComponent.memoryAddress(),
      gifDMACComponent.quadwordCount());
  }
  if ((dmacControl & GIFDMACChannelControl::START) != 0 &&
      (gifDMACComponent.channelControl() &
       GIFDMACChannelControl::START) == 0)
  {
    appendTrace(
      cycle,
      NekoTraceSubsystem::GIFDMAC,
      NekoTraceEventType::TransferCompleted,
      gifDMACComponent.globalStatus());
  }
  if (gsComponent.pixelWriteCount() != pixels)
  {
    appendTrace(
      cycle,
      NekoTraceSubsystem::GS,
      NekoTraceEventType::Progress,
      gsComponent.pixelWriteCount());
  }
  if (interruptControllerComponent.status() != interruptStatus)
  {
    appendTrace(
      cycle,
      NekoTraceSubsystem::InterruptController,
      NekoTraceEventType::InterruptChanged,
      interruptControllerComponent.status(),
      interruptControllerComponent.mask());
  }
  if (gsDisplayComponent.presentationBoundaryCount() !=
      presentationBoundary)
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
