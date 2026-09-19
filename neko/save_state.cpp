#include "save_state_internal.hpp"

#include <deque>

namespace
{
  class SaveStateContainerWriter
  {
    public:
      SaveStateContainerWriter()
      {
        writer.writeBytes(
          SAVE_STATE_MAGIC,
          sizeof(SAVE_STATE_MAGIC));
        writer.writeU32(SAVE_STATE_VERSION);
        payloadLengthOffset = writer.size();
        writer.writeU64(0);
        checksumOffset = writer.size();
        writer.writeU64(0);
      }

      SaveStateWriter *payload()
      {
        return &writer;
      }

      std::vector<std::uint8_t> finish()
      {
        writer.patchU64(
          payloadLengthOffset,
          writer.size() - SAVE_STATE_HEADER_SIZE);
        writer.patchU64(
          checksumOffset,
          writer.checksumFrom(SAVE_STATE_HEADER_SIZE));
        return writer.finish();
      }

    private:
      SaveStateWriter writer;
      std::size_t payloadLengthOffset = 0;
      std::size_t checksumOffset = 0;
  };

  class SaveStateContainerReader
  {
    public:
      explicit SaveStateContainerReader(
        const std::vector<std::uint8_t> &state) :
        reader(state)
      {
        reader.expectBytes(
          SAVE_STATE_MAGIC,
          sizeof(SAVE_STATE_MAGIC),
          "magic");
        const std::uint32_t version = reader.readU32();
        if (version != SAVE_STATE_VERSION)
        {
          SaveStateReader::invalid("version is incompatible");
        }
        const std::uint64_t payloadLength = reader.readU64();
        const std::uint64_t expectedChecksum =
          reader.readU64();
        if (payloadLength != reader.size() - reader.offset())
        {
          SaveStateReader::invalid(
            "payload length does not match the input");
        }
        if (expectedChecksum !=
            reader.checksumFrom(reader.offset()))
        {
          SaveStateReader::invalid(
            "payload checksum does not match");
        }
      }

      SaveStateReader *payload()
      {
        return &reader;
      }

      void requireEnd() const
      {
        reader.requireEnd();
      }

    private:
      SaveStateReader reader;
  };

  static_assert(
    noexcept(
      std::declval<std::deque<GIFQuadword> &>().swap(
        std::declval<std::deque<GIFQuadword> &>())),
    "Transactional save-state commit requires noexcept GIF FIFO swap.");
  static_assert(
    noexcept(
      std::declval<std::vector<std::uint8_t> &>().swap(
        std::declval<std::vector<std::uint8_t> &>())),
    "Transactional save-state commit requires noexcept byte-vector swap.");
  static_assert(
    noexcept(
      std::declval<std::list<Pipeline *> &>().swap(
        std::declval<std::list<Pipeline *> &>())),
    "Transactional save-state commit requires noexcept pipeline-list swap.");
}

void require(
  bool condition,
  const std::string &detail)
{
  if (!condition)
  {
    SaveStateReader::invalid(detail);
  }
}

void writeFPRegister(
  SaveStateWriter *writer,
  const FPRegister &value)
{
  writer->writeU32(value.x.bits());
  writer->writeU32(value.y.bits());
  writer->writeU32(value.z.bits());
  writer->writeU32(value.w.bits());
  writer->writeU8(value.xResultFlags);
  writer->writeU8(value.yResultFlags);
  writer->writeU8(value.zResultFlags);
  writer->writeU8(value.wResultFlags);
}

FPRegister readFPRegister(SaveStateReader *reader)
{
  FPRegister value;
  value.x.setBits(reader->readU32());
  value.y.setBits(reader->readU32());
  value.z.setBits(reader->readU32());
  value.w.setBits(reader->readU32());
  value.xResultFlags = reader->readU8();
  value.yResultFlags = reader->readU8();
  value.zResultFlags = reader->readU8();
  value.wResultFlags = reader->readU8();
  require(
    (value.xResultFlags & ~0x0f) == 0 &&
    (value.yResultFlags & ~0x0f) == 0 &&
    (value.zResultFlags & ~0x0f) == 0 &&
    (value.wResultFlags & ~0x0f) == 0,
    "VU floating-point result flags are invalid");
  return value;
}

std::vector<std::uint8_t> NekoSaveStateCodec::save(
  const NekoSystem &system)
{
  SaveStateContainerWriter container;
  writeSystem(container.payload(), system);
  return container.finish();
}

void NekoSaveStateCodec::load(
  NekoSystem *system,
  const std::vector<std::uint8_t> &state)
{
  SaveStateContainerReader container(state);
  SystemLoadTransaction transaction;
  readSystem(
    container.payload(),
    &transaction.parsed,
    &transaction.decodedTopology);
  container.requireEnd();
  validateSystem(transaction.parsed);

  transaction.reconciliation =
    reconcileSystem(
      transaction.parsed,
      transaction.decodedTopology,
      system);
  commitSystem(system, &transaction);
}

void NekoSaveStateCodec::writeSystem(
  SaveStateWriter *writer,
  const NekoSystem &system)
{
  // Version 24 payload order is part of the on-disk compatibility contract.
  writer->writeU16(system.inputState.buttons);
  writer->writeU8(system.inputState.leftStickX);
  writer->writeU8(system.inputState.leftStickY);
  writer->writeU8(system.inputState.rightStickX);
  writer->writeU8(system.inputState.rightStickY);

  writeMasterClock(writer, system);
  writeEECore(writer, system.eeCoreComponent);
  writer->writeU32(
    system.interruptControllerComponent.statusRegister);
  writer->writeU32(
    system.interruptControllerComponent.maskRegister);
  writer->writeByteVector(system.eeBusComponent.mainMemory);
  writeVPU(writer, system.vu0Component);
  writeVPU(writer, system.vu1Component);
  writeVIF(writer, system.vif0Component);
  writeVIF(writer, system.vif1Component);
  writeGIFDecoder(writer, system.gifDecoderComponent);
  writeGIFArbiter(writer, system.gifPathArbiterComponent);
  writeGIFPath1(writer, system.gifPath1Component);
  writeGIFPath3(writer, system.gifPath3Component);
  writeGS(writer, system.gsComponent);
  writeDMAC(writer, system.gifDMACComponent);
  writeVIF1DMAC(writer, system.vif1DMACComponent);
  writeGSDisplay(writer, system.gsDisplayComponent);
}

void NekoSaveStateCodec::readSystem(
  SaveStateReader *reader,
  NekoSystem *system,
  DecodedTopology *topology)
{
  system->inputState.buttons = reader->readU16();
  system->inputState.leftStickX = reader->readU8();
  system->inputState.leftStickY = reader->readU8();
  system->inputState.rightStickX = reader->readU8();
  system->inputState.rightStickY = reader->readU8();

  readMasterClock(
    reader,
    &system->masterClock,
    &topology->schedule);
  readEECore(
    reader,
    &system->eeCoreComponent,
    &topology->dividerOccupancy);
  system->interruptControllerComponent.statusRegister =
    reader->readU32();
  system->interruptControllerComponent.maskRegister =
    reader->readU32();
  system->eeBusComponent.mainMemory =
    reader->readByteVector(
      EEMemoryMap::MAIN_MEMORY_SIZE,
      "EE main memory");
  readVPU(
    reader,
    &system->vu0Component,
    &topology->vu0PipelineLists);
  readVPU(
    reader,
    &system->vu1Component,
    &topology->vu1PipelineLists);
  readVIF(reader, &system->vif0Component);
  readVIF(reader, &system->vif1Component);
  readGIFDecoder(reader, &system->gifDecoderComponent);
  readGIFArbiter(reader, &system->gifPathArbiterComponent);
  readGIFPath1(reader, &system->gifPath1Component);
  readGIFPath3(reader, &system->gifPath3Component);
  readGS(reader, &system->gsComponent);
  readDMAC(reader, &system->gifDMACComponent);
  readVIF1DMAC(reader, &system->vif1DMACComponent);
  readGSDisplay(reader, &system->gsDisplayComponent);
}

void NekoSaveStateCodec::validateSystem(
  const NekoSystem &system)
{
  require(
    system.vu0Component.type == VPUType::VU0 &&
    system.vu1Component.type == VPUType::VU1,
    "VU identities do not match the system wiring");
  require(
    system.vif0Component.type == VIFType::VIF0 &&
    system.vif1Component.type == VIFType::VIF1,
    "VIF identities do not match the system wiring");
  require(
    system.vif1Component.path3Mask ==
      system.gifPathArbiterComponent.vifPath3Mask,
    "VIF1 and GIF PATH3 mask state disagree");
  if (system.gifPathArbiterComponent.interruptedPath3)
  {
    const GIFDecoderState &suspended =
      system.gifPathArbiterComponent.suspendedPath3State;
    require(
      suspended.activePacket &&
      !suspended.waitingForTag &&
      suspended.tag.format == GIFDataFormat::Image,
      "suspended PATH3 state is invalid");
    require(
      system.gifPathArbiterComponent.queuedPaths[2],
      "interrupted PATH3 is not queued");
  }
}

NekoSaveStateCodec::SystemReconciliation
NekoSaveStateCodec::reconcileSystem(
  const NekoSystem &source,
  const DecodedTopology &topology,
  NekoSystem *destination)
{
  SystemReconciliation reconciliation;
  reconciliation.schedule =
    reconcileMasterClockSchedule(
      topology.schedule,
      destination);
  reconciliation.vu0Lists = reconcilePipelineLists(
    topology.vu0PipelineLists,
    &destination->vu0Component.orchestrator);
  reconciliation.vu1Lists = reconcilePipelineLists(
    topology.vu1PipelineLists,
    &destination->vu1Component.orchestrator);
  reconciliation.dividerOccupancy =
    source.eeCoreComponent.derivedCOP1DividerOccupancy();
  return reconciliation;
}

void NekoSaveStateCodec::commitSystem(
  NekoSystem *destination,
  SystemLoadTransaction *transaction)
{
  NekoSystem *source = &transaction->parsed;
  SystemReconciliation *reconciliation =
    &transaction->reconciliation;
  destination->inputState = source->inputState;
  destination->masterClock.masterCycle =
    source->masterClock.masterCycle;
  destination->masterClock.components.swap(
    reconciliation->schedule);
  destination->eeCoreComponent.generalRegisters =
    source->eeCoreComponent.generalRegisters;
  destination->eeCoreComponent.floatingPointRegisters =
    source->eeCoreComponent.floatingPointRegisters;
  destination->eeCoreComponent.floatingPointAccumulatorRegister =
    source->eeCoreComponent.floatingPointAccumulatorRegister;
  destination->eeCoreComponent.cop1StatusRegister =
    source->eeCoreComponent.cop1StatusRegister;
  destination->eeCoreComponent.pc =
    source->eeCoreComponent.pc;
  destination->eeCoreComponent.hiRegister =
    source->eeCoreComponent.hiRegister;
  destination->eeCoreComponent.loRegister =
    source->eeCoreComponent.loRegister;
  destination->eeCoreComponent.hi1Register =
    source->eeCoreComponent.hi1Register;
  destination->eeCoreComponent.lo1Register =
    source->eeCoreComponent.lo1Register;
  destination->eeCoreComponent.saRegister =
    source->eeCoreComponent.saRegister;
  destination->eeCoreComponent.cop0BadVAddr =
    source->eeCoreComponent.cop0BadVAddr;
  destination->eeCoreComponent.cop0Count =
    source->eeCoreComponent.cop0Count;
  destination->eeCoreComponent.cop0Compare =
    source->eeCoreComponent.cop0Compare;
  destination->eeCoreComponent.cop0Status =
    source->eeCoreComponent.cop0Status;
  destination->eeCoreComponent.cop0Cause =
    source->eeCoreComponent.cop0Cause;
  destination->eeCoreComponent.cop0EPC =
    source->eeCoreComponent.cop0EPC;
  destination->eeCoreComponent.cop0ErrorEPC =
    source->eeCoreComponent.cop0ErrorEPC;
  destination->eeCoreComponent.exception =
    source->eeCoreComponent.exception;
  destination->eeCoreComponent.faultAddress =
    source->eeCoreComponent.faultAddress;
  destination->eeCoreComponent.state =
    source->eeCoreComponent.state;
  destination->eeCoreComponent.haltReason =
    source->eeCoreComponent.haltReason;
  destination->eeCoreComponent.cycles =
    source->eeCoreComponent.cycles;
  destination->eeCoreComponent.lastInstructionValid =
    source->eeCoreComponent.lastInstructionValid;
  destination->eeCoreComponent.lastAddress =
    source->eeCoreComponent.lastAddress;
  destination->eeCoreComponent.lastDecodedInstruction =
    source->eeCoreComponent.lastDecodedInstruction;
  destination->eeCoreComponent.rejectedInstructionValue =
    source->eeCoreComponent.rejectedInstructionValue;
  destination->eeCoreComponent.issueLatch =
    source->eeCoreComponent.issueLatch;
  destination->eeCoreComponent.stagingLatch =
    source->eeCoreComponent.stagingLatch;
  destination->eeCoreComponent.inFlightCOP1Operations =
    source->eeCoreComponent.inFlightCOP1Operations;
  destination->eeCoreComponent.nextEEProgramOrder =
    source->eeCoreComponent.nextEEProgramOrder;
  destination->eeCoreComponent.executingProgramOrder = 0;
  destination->eeCoreComponent.pendingMac0 =
    source->eeCoreComponent.pendingMac0;
  destination->eeCoreComponent.pendingMac1 =
    source->eeCoreComponent.pendingMac1;
  destination->eeCoreComponent.cop1DividerInitiationCycles =
    reconciliation->dividerOccupancy.initiationCycles;
  destination->eeCoreComponent.cop1DividerOperation =
    reconciliation->dividerOccupancy.operation;
  destination->eeCoreComponent.shiftAmountOrdering =
    source->eeCoreComponent.shiftAmountOrdering;
  destination->eeCoreComponent.branchDelayPending =
    source->eeCoreComponent.branchDelayPending;
  destination->eeCoreComponent.branchDelayTarget =
    source->eeCoreComponent.branchDelayTarget;
  destination->eeCoreComponent.branchInstructionAddress =
    source->eeCoreComponent.branchInstructionAddress;
  destination->eeCoreComponent.branchDelayFromLikely =
    source->eeCoreComponent.branchDelayFromLikely;
  destination->eeCoreComponent.branchDelayTaken =
    source->eeCoreComponent.branchDelayTaken;
  destination->eeCoreComponent.cop1DividerPostDelayInstructions =
    source->eeCoreComponent.cop1DividerPostDelayInstructions;
  destination->eeCoreComponent.cop1DividerPostDelayBranchAddress =
    source->eeCoreComponent.cop1DividerPostDelayBranchAddress;
  destination->eeCoreComponent.cop1DividerPostDelayTargetAddress =
    source->eeCoreComponent.cop1DividerPostDelayTargetAddress;
  destination->eeCoreComponent.cop1DividerPostDelayTaken =
    source->eeCoreComponent.cop1DividerPostDelayTaken;
  destination->eeCoreComponent.cop1DividerPostTargetInstructions =
    source->eeCoreComponent.cop1DividerPostTargetInstructions;
  destination->eeCoreComponent.cop1DividerPostTargetAddress =
    source->eeCoreComponent.cop1DividerPostTargetAddress;
  destination->eeCoreComponent.issueSelection = {};
  destination->eeCoreComponent.acceptanceRecords.clear();
  destination->eeCoreComponent.exceptionEnteredThisCycle = false;
  destination->interruptControllerComponent.statusRegister =
    source->interruptControllerComponent.statusRegister;
  destination->interruptControllerComponent.maskRegister =
    source->interruptControllerComponent.maskRegister;
  destination->eeBusComponent.mainMemory.swap(
    source->eeBusComponent.mainMemory);
  commitVPU(
    &destination->vu0Component,
    &source->vu0Component,
    &reconciliation->vu0Lists);
  commitVPU(
    &destination->vu1Component,
    &source->vu1Component,
    &reconciliation->vu1Lists);
  commitVIF(
    &destination->vif0Component,
    &source->vif0Component);
  commitVIF(
    &destination->vif1Component,
    &source->vif1Component);

  GIFDecoder &decoder = destination->gifDecoderComponent;
  const GIFDecoder &sourceDecoder = source->gifDecoderComponent;
  decoder.tag = sourceDecoder.tag;
  decoder.waitingForTag = sourceDecoder.waitingForTag;
  decoder.activePacket = sourceDecoder.activePacket;
  decoder.remainingQuadwords =
    sourceDecoder.remainingQuadwords;
  decoder.remainingRegisterValues =
    sourceDecoder.remainingRegisterValues;
  decoder.currentLoop = sourceDecoder.currentLoop;
  decoder.currentRegister = sourceDecoder.currentRegister;
  decoder.qValue = sourceDecoder.qValue;

  GIFPathArbiter &arbiter =
    destination->gifPathArbiterComponent;
  const GIFPathArbiter &sourceArbiter =
    source->gifPathArbiterComponent;
  arbiter.currentPath = sourceArbiter.currentPath;
  arbiter.queuedPaths = sourceArbiter.queuedPaths;
  arbiter.vifPath3Mask = sourceArbiter.vifPath3Mask;
  arbiter.modePath3Mask = sourceArbiter.modePath3Mask;
  arbiter.intermittentPath3 =
    sourceArbiter.intermittentPath3;
  arbiter.timedTransfers = sourceArbiter.timedTransfers;
  arbiter.interruptedPath3 =
    sourceArbiter.interruptedPath3;
  arbiter.queuedPath2CanInterruptPath3 =
    sourceArbiter.queuedPath2CanInterruptPath3;
  arbiter.path3ImageSliceQuadwords =
    sourceArbiter.path3ImageSliceQuadwords;
  arbiter.path3Count = sourceArbiter.path3Count;
  arbiter.path3Tag = sourceArbiter.path3Tag;
  arbiter.remainingIdleCycles =
    sourceArbiter.remainingIdleCycles;
  arbiter.suspendedPath3State =
    sourceArbiter.suspendedPath3State;
  arbiter.traceCallbackFailure = nullptr;

  GIFPath1Transfer &path1 = destination->gifPath1Component;
  path1.active = source->gifPath1Component.active;
  path1.qwordAddress =
    source->gifPath1Component.qwordAddress;
  path1.transferredQuadwords =
    source->gifPath1Component.transferredQuadwords;

  GIFPath3Transfer &path3 = destination->gifPath3Component;
  path3.submissionAttempts =
    source->gifPath3Component.submissionAttempts;
  path3.transferredQuadwords =
    source->gifPath3Component.transferredQuadwords;
  path3.completedPackets =
    source->gifPath3Component.completedPackets;
  path3.guestFIFO.swap(
    source->gifPath3Component.guestFIFO);

  commitGS(
    &destination->gsComponent,
    &source->gsComponent);

  GIFDMACChannel &dmac = destination->gifDMACComponent;
  const GIFDMACChannel &sourceDMAC =
    source->gifDMACComponent;
  dmac.channelControlRegister =
    sourceDMAC.channelControlRegister;
  dmac.memoryAddressRegister =
    sourceDMAC.memoryAddressRegister;
  dmac.quadwordCountRegister =
    sourceDMAC.quadwordCountRegister;
  dmac.tagAddressRegister =
    sourceDMAC.tagAddressRegister;
  dmac.addressStackRegisters =
    sourceDMAC.addressStackRegisters;
  dmac.globalControlRegister =
    sourceDMAC.globalControlRegister;
  dmac.statusRegister = sourceDMAC.statusRegister;
  dmac.statusMaskRegister = sourceDMAC.statusMaskRegister;
  dmac.terminateAfterPacket =
    sourceDMAC.terminateAfterPacket;
  dmac.path3Stalled = sourceDMAC.path3Stalled;
  dmac.addressStackDepth = sourceDMAC.addressStackDepth;
  dmac.transferredQuadwords =
    sourceDMAC.transferredQuadwords;

  VIF1DMACChannel &vif1DMAC =
    destination->vif1DMACComponent;
  const VIF1DMACChannel &sourceVIF1DMAC =
    source->vif1DMACComponent;
  vif1DMAC.channelControlRegister =
    sourceVIF1DMAC.channelControlRegister;
  vif1DMAC.memoryAddressRegister =
    sourceVIF1DMAC.memoryAddressRegister;
  vif1DMAC.quadwordCountRegister =
    sourceVIF1DMAC.quadwordCountRegister;
  vif1DMAC.tagAddressRegister =
    sourceVIF1DMAC.tagAddressRegister;
  vif1DMAC.addressStackRegisters =
    sourceVIF1DMAC.addressStackRegisters;
  vif1DMAC.terminateAfterPacket =
    sourceVIF1DMAC.terminateAfterPacket;
  vif1DMAC.vif1Stalled = sourceVIF1DMAC.vif1Stalled;
  vif1DMAC.addressStackDepth =
    sourceVIF1DMAC.addressStackDepth;
  vif1DMAC.transferredQuadwords =
    sourceVIF1DMAC.transferredQuadwords;

  GSDisplay &display = destination->gsDisplayComponent;
  const GSDisplay &sourceDisplay =
    source->gsDisplayComponent;
  display.circuits = sourceDisplay.circuits;
  display.videoTiming = sourceDisplay.videoTiming;
  display.modeRegister = sourceDisplay.modeRegister;
  display.syncModeRegister =
    sourceDisplay.syncModeRegister;
  display.backgroundColor = sourceDisplay.backgroundColor;
  display.interruptMaskRegister =
    sourceDisplay.interruptMaskRegister;
  display.cycleInFrame = sourceDisplay.cycleInFrame;
  display.frameBoundaries = sourceDisplay.frameBoundaries;
  display.verticalBlank = sourceDisplay.verticalBlank;
  display.oddField = sourceDisplay.oddField;
  display.vsyncInterrupt = sourceDisplay.vsyncInterrupt;
  display.verticalBlankStarted =
    sourceDisplay.verticalBlankStarted;
  display.verticalBlankEnded =
    sourceDisplay.verticalBlankEnded;
}

void NekoSaveStateCodec::writeMasterClock(
  SaveStateWriter *writer,
  const NekoSystem &system)
{
  writer->writeU64(system.masterClock.masterCycle);
  writer->writeSize(system.masterClock.components.size());
  for (const auto &scheduled : system.masterClock.components)
  {
    writer->writeU8(componentID(
      system,
      scheduled.component));
    writer->writeU64(scheduled.period);
    writer->writeU64(scheduled.phase);
  }
}

void NekoSaveStateCodec::readMasterClock(
  SaveStateReader *reader,
  MasterClockScheduler *clock,
  std::vector<ScheduledComponentState> *schedule)
{
  clock->masterCycle = reader->readU64();
  const std::uint32_t count = reader->readU32();
  require(count <= 7, "master-clock component count is invalid");
  std::array<bool, 8> used = {};
  schedule->clear();
  schedule->reserve(count);
  for (std::uint32_t index = 0; index < count; ++index)
  {
    const std::uint8_t id = reader->readU8();
    require(id >= 1 && id <= 7, "clock component ID is invalid");
    require(!used[id], "clock component ID is duplicated");
    used[id] = true;
    const std::uint64_t period = reader->readU64();
    const std::uint64_t phase = reader->readU64();
    require(period != 0, "clock period is zero");
    require(phase < period, "clock phase is outside its period");
    schedule->push_back({
      id,
      period,
      phase
    });
  }
}

std::uint8_t NekoSaveStateCodec::componentID(
  const NekoSystem &system,
  const ClockedComponent *component)
{
  if (component == &system.vu0Component)
  {
    return 1;
  }
  if (component == &system.vu1Component)
  {
    return 2;
  }
  if (component == &system.gifPathArbiterComponent)
  {
    return 3;
  }
  if (component == &system.gifDMACComponent)
  {
    return 4;
  }
  if (component == &system.gsDisplayComponent)
  {
    return 5;
  }
  if (component == &system.eeCoreComponent)
  {
    return 6;
  }
  if (component == &system.vif1DMACComponent)
  {
    return 7;
  }
  throw std::runtime_error(
    "Cannot save a host-owned master-clock component.");
}

ClockedComponent *NekoSaveStateCodec::componentForID(
  NekoSystem *system,
  std::uint8_t id)
{
  switch (id)
  {
    case 1:
      return &system->vu0Component;
    case 2:
      return &system->vu1Component;
    case 3:
      return &system->gifPathArbiterComponent;
    case 4:
      return &system->gifDMACComponent;
    case 5:
      return &system->gsDisplayComponent;
    case 6:
      return &system->eeCoreComponent;
    case 7:
      return &system->vif1DMACComponent;
    default:
      SaveStateReader::invalid("clock component ID is invalid");
  }
  return nullptr;
}

std::vector<MasterClockScheduler::ScheduledComponent>
NekoSaveStateCodec::reconcileMasterClockSchedule(
  const std::vector<ScheduledComponentState> &source,
  NekoSystem *destination)
{
  std::vector<MasterClockScheduler::ScheduledComponent> result;
  result.reserve(source.size());
  for (const ScheduledComponentState &scheduled : source)
  {
    result.push_back({
      componentForID(destination, scheduled.id),
      scheduled.period,
      scheduled.phase
    });
  }
  return result;
}

std::vector<std::uint8_t> NekoSystem::saveState() const
{
  return NekoSaveStateCodec::save(*this);
}

void NekoSystem::loadState(
  const std::vector<std::uint8_t> &state)
{
  NekoSaveStateCodec::load(this, state);
}
