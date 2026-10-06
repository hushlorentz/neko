#include "save_state_internal.hpp"

#include <cassert>
#include <deque>

namespace
{
  class SaveStateContainerWriter
  {
    public:
      explicit SaveStateContainerWriter(
        SaveStateObserver *observer) :
        writer(observer)
      {
        {
          auto container = writer.scope("container");
          writer.writeFieldBytes(
            "magic",
            SAVE_STATE_MAGIC,
            sizeof(SAVE_STATE_MAGIC));
          writer.writeFieldU32("version", SAVE_STATE_VERSION);
          payloadLengthOffset = writer.size();
          writer.writeU64(0);
          checksumOffset = writer.size();
          writer.writeU64(0);
        }
        writer.setPayloadOrigin(SAVE_STATE_HEADER_SIZE);
      }

      SaveStateWriter *payload()
      {
        return &writer;
      }

      std::vector<std::uint8_t> finish()
      {
        {
          auto container = writer.scope("container");
          writer.patchFieldU64(
            "payloadLength",
            payloadLengthOffset,
            writer.size() - SAVE_STATE_HEADER_SIZE);
          writer.patchFieldU64(
            "checksum",
            checksumOffset,
            writer.checksumFrom(SAVE_STATE_HEADER_SIZE));
        }
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
        const std::vector<std::uint8_t> &state,
        SaveStateObserver *observer) :
        reader(state, observer)
      {
        {
          auto container = reader.scope("container");
          reader.expectFieldBytes(
            "magic",
            SAVE_STATE_MAGIC,
            sizeof(SAVE_STATE_MAGIC));
          const std::uint32_t version =
            reader.readFieldU32("version");
          reader.requireField(
            version == SAVE_STATE_VERSION,
            "version is incompatible");
          const std::uint64_t payloadLength =
            reader.readFieldU64("payloadLength");
          reader.requireField(
            payloadLength ==
              reader.size() - SAVE_STATE_HEADER_SIZE,
            "payload length does not match the input");
          const std::uint64_t expectedChecksum =
            reader.readFieldU64("checksum");
          reader.requireField(
            expectedChecksum ==
              reader.checksumFrom(SAVE_STATE_HEADER_SIZE),
            "payload checksum does not match");
        }
        reader.setPayloadOrigin(SAVE_STATE_HEADER_SIZE);
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
  writer->writeFieldU32("x", value.x.bits());
  writer->writeFieldU32("y", value.y.bits());
  writer->writeFieldU32("z", value.z.bits());
  writer->writeFieldU32("w", value.w.bits());
  writer->writeFieldU8("xResultFlags", value.xResultFlags);
  writer->writeFieldU8("yResultFlags", value.yResultFlags);
  writer->writeFieldU8("zResultFlags", value.zResultFlags);
  writer->writeFieldU8("wResultFlags", value.wResultFlags);
}

FPRegister readFPRegister(SaveStateReader *reader)
{
  FPRegister value;
  value.x.setBits(reader->readFieldU32("x"));
  value.y.setBits(reader->readFieldU32("y"));
  value.z.setBits(reader->readFieldU32("z"));
  value.w.setBits(reader->readFieldU32("w"));
  value.xResultFlags = reader->readFieldU8("xResultFlags");
  reader->requireField(
    (value.xResultFlags & ~0x0f) == 0,
    "VU floating-point result flags are invalid");
  value.yResultFlags = reader->readFieldU8("yResultFlags");
  reader->requireField(
    (value.yResultFlags & ~0x0f) == 0,
    "VU floating-point result flags are invalid");
  value.zResultFlags = reader->readFieldU8("zResultFlags");
  reader->requireField(
    (value.zResultFlags & ~0x0f) == 0,
    "VU floating-point result flags are invalid");
  value.wResultFlags = reader->readFieldU8("wResultFlags");
  reader->requireField(
    (value.wResultFlags & ~0x0f) == 0,
    "VU floating-point result flags are invalid");
  return value;
}

void NekoSaveStateCodec::writeScratchpadDMAState(
  SaveStateWriter *writer,
  const NekoSystem &system)
{
  writer->writeFieldRange(
    "scratchpad",
    [&]()
    {
      for (const EEQuadword &value :
           system.eeCoreComponent.memorySystem.scratchpad)
      {
        writer->writeU64(value.low);
        writer->writeU64(value.high);
      }
    });
  writer->writeFieldU32(
    "interleaveSizeRegister",
    system.dmacControllerComponent.interleaveSizeRegister);

  const auto writeChannel =
    [writer](
      const char *name,
      const ScratchpadDMACChannel &channel)
    {
      auto channelScope = writer->scope(name);
      assert(channel.stateValid());
      writer->writeFieldU32(
        "channelControlRegister",
        channel.channelControlRegister);
      writer->writeFieldU32(
        "memoryAddressRegister",
        channel.memoryAddressRegister);
      writer->writeFieldU32(
        "quadwordCountRegister",
        channel.quadwordCountRegister);
      writer->writeFieldU32(
        "tagAddressRegister",
        channel.tagAddressRegister);
      writer->writeFieldU32(
        "scratchpadAddressRegister",
        channel.scratchpadAddressRegister);
      writer->writeFieldU16(
        "interleaveQuadwordsRemaining",
        channel.interleaveQuadwordsRemaining);
    };
  writeChannel(
    "fromScratchpadChannel",
    system.fromScratchpadDMACComponent);
  writeChannel(
    "toScratchpadChannel",
    system.toScratchpadDMACComponent);
}

void NekoSaveStateCodec::readScratchpadDMAState(
  SaveStateReader *reader,
  NekoSystem *system)
{
  reader->readFieldRange(
    "scratchpad",
    system->eeCoreComponent.memorySystem.scratchpad.size() *
      sizeof(std::uint64_t) * 2,
    [&]()
    {
      for (EEQuadword &value :
           system->eeCoreComponent.memorySystem.scratchpad)
      {
        value.low = reader->readU64();
        value.high = reader->readU64();
      }
    });

  DMACController &controller =
    system->dmacControllerComponent;
  controller.interleaveSizeRegister =
    reader->readFieldU32("interleaveSizeRegister");
  reader->requireField(
    (controller.interleaveSizeRegister &
     ~(DMACInterleave::SKIP_MASK |
       DMACInterleave::TRANSFER_MASK)) == 0,
    "DMAC interleave size is invalid");

  const auto readChannel =
    [reader](
      const char *name,
      ScratchpadDMACChannel *channel,
      bool tagAddressSupported)
    {
      auto channelScope = reader->scope(name);
      channel->channelControlRegister =
        reader->readFieldU32("channelControlRegister");
      channel->memoryAddressRegister =
        reader->readFieldU32("memoryAddressRegister");
      channel->quadwordCountRegister =
        reader->readFieldU32("quadwordCountRegister");
      channel->tagAddressRegister =
        reader->readFieldU32("tagAddressRegister");
      channel->scratchpadAddressRegister =
        reader->readFieldU32("scratchpadAddressRegister");
      channel->interleaveQuadwordsRemaining =
        reader->readFieldU16("interleaveQuadwordsRemaining");

      reader->requireField(
        channel->channelControlStateValid(),
        "SPR DMAC channel control is invalid");
      reader->requireField(
        channel->memoryAddressStateValid(),
        "SPR DMAC memory address is invalid");
      reader->requireField(
        channel->quadwordCountStateValid(),
        "SPR DMAC qword count is invalid");
      reader->requireField(
        channel->tagAddressStateValid() &&
          (tagAddressSupported ||
           channel->tagAddressRegister == 0),
        "SPR DMAC tag address is invalid");
      reader->requireField(
        channel->scratchpadAddressStateValid(),
        "SPR DMAC scratchpad address is invalid");
      reader->requireField(
        channel->interleaveQuadwordsRemaining <= 0xff,
        "SPR DMAC interleave continuation is invalid");
      reader->requireField(
        channel->interleaveContinuationStateValid(),
        "SPR DMAC interleave continuation is inconsistent");
      reader->requireField(
        channel->stateValid(),
        "SPR DMAC state is invalid");
    };
  readChannel(
    "fromScratchpadChannel",
    &system->fromScratchpadDMACComponent,
    false);
  readChannel(
    "toScratchpadChannel",
    &system->toScratchpadDMACComponent,
    true);
}

std::vector<std::uint8_t> NekoSaveStateCodec::save(
  const NekoSystem &system,
  SaveStateObserver *observer)
{
  SaveStateContainerWriter container(observer);
  writeSystem(container.payload(), system);
  return container.finish();
}

void NekoSaveStateCodec::load(
  NekoSystem *system,
  const std::vector<std::uint8_t> &state,
  SaveStateObserver *observer)
{
  SaveStateContainerReader container(state, observer);
  SystemLoadTransaction transaction;
  readSystem(
    container.payload(),
    &transaction.parsed,
    &transaction.decodedTopology);
  container.requireEnd();
  container.payload()->clearFieldContext();
  validateSystem(
    container.payload(),
    transaction.parsed);

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
  auto root = writer->scope("system");
  // Component order remains stable across versioned payload extensions.
  {
    auto input = writer->scope("input");
    writer->writeFieldU16("buttons", system.inputState.buttons);
    writer->writeFieldU8("leftStickX", system.inputState.leftStickX);
    writer->writeFieldU8("leftStickY", system.inputState.leftStickY);
    writer->writeFieldU8("rightStickX", system.inputState.rightStickX);
    writer->writeFieldU8("rightStickY", system.inputState.rightStickY);
  }
  {
    auto component = writer->scope("masterClock");
    writeMasterClock(writer, system);
  }
  {
    auto component = writer->scope("eeCore");
    writeEECore(writer, system.eeCoreComponent);
  }
  {
    auto component = writer->scope("interruptController");
    writer->writeFieldU32(
      "statusRegister",
      system.interruptControllerComponent.statusRegister);
    writer->writeFieldU32(
      "maskRegister",
      system.interruptControllerComponent.maskRegister);
  }
  {
    auto component = writer->scope("eeBus");
    writer->writeFieldByteVector(
      "mainMemory",
      system.eeBusComponent.mainMemory);
  }
  {
    auto component = writer->scope("vu0");
    writeVPU(writer, system.vu0Component);
  }
  {
    auto component = writer->scope("vu1");
    writeVPU(writer, system.vu1Component);
  }
  {
    auto component = writer->scope("vif0");
    writeVIF(writer, system.vif0Component);
  }
  {
    auto component = writer->scope("vif1");
    writeVIF(writer, system.vif1Component);
  }
  {
    auto component = writer->scope("gifDecoder");
    writeGIFDecoder(writer, system.gifDecoderComponent);
  }
  {
    auto component = writer->scope("gifArbiter");
    writeGIFArbiter(writer, system.gifPathArbiterComponent);
  }
  {
    auto component = writer->scope("gifPath1");
    writeGIFPath1(writer, system.gifPath1Component);
  }
  {
    auto component = writer->scope("gifPath3");
    writeGIFPath3(writer, system.gifPath3Component);
  }
  {
    auto component = writer->scope("gs");
    writeGS(writer, system.gsComponent);
  }
  {
    auto component = writer->scope("gifDMAC");
    writeDMAC(
      writer,
      system.gifDMACComponent,
      system.dmacControllerComponent);
  }
  {
    auto component = writer->scope("vif1DMAC");
    writeVIF1DMAC(writer, system.vif1DMACComponent);
  }
  {
    auto component = writer->scope("gsDisplay");
    writeGSDisplay(writer, system.gsDisplayComponent);
  }
  {
    auto component = writer->scope("scratchpadDMA");
    writeScratchpadDMAState(writer, system);
  }
}

void NekoSaveStateCodec::readSystem(
  SaveStateReader *reader,
  NekoSystem *system,
  DecodedTopology *topology)
{
  auto root = reader->scope("system");
  {
    auto input = reader->scope("input");
    system->inputState.buttons = reader->readFieldU16("buttons");
    system->inputState.leftStickX = reader->readFieldU8("leftStickX");
    system->inputState.leftStickY = reader->readFieldU8("leftStickY");
    system->inputState.rightStickX = reader->readFieldU8("rightStickX");
    system->inputState.rightStickY = reader->readFieldU8("rightStickY");
  }
  {
    auto component = reader->scope("masterClock");
    readMasterClock(reader, &system->masterClock, &topology->schedule);
  }
  {
    auto component = reader->scope("eeCore");
    readEECore(
      reader,
      &system->eeCoreComponent,
      &topology->dividerOccupancy);
  }
  {
    auto component = reader->scope("interruptController");
    system->interruptControllerComponent.statusRegister =
      reader->readFieldU32("statusRegister");
    system->interruptControllerComponent.maskRegister =
      reader->readFieldU32("maskRegister");
  }
  {
    auto component = reader->scope("eeBus");
    system->eeBusComponent.mainMemory =
      reader->readFieldByteVector(
        "mainMemory",
        EEMemoryMap::MAIN_MEMORY_SIZE);
  }
  {
    auto component = reader->scope("vu0");
    readVPU(reader, &system->vu0Component, &topology->vu0PipelineLists);
  }
  {
    auto component = reader->scope("vu1");
    readVPU(reader, &system->vu1Component, &topology->vu1PipelineLists);
  }
  {
    auto component = reader->scope("vif0");
    readVIF(reader, &system->vif0Component);
  }
  {
    auto component = reader->scope("vif1");
    readVIF(reader, &system->vif1Component);
  }
  {
    auto component = reader->scope("gifDecoder");
    readGIFDecoder(reader, &system->gifDecoderComponent);
  }
  {
    auto component = reader->scope("gifArbiter");
    readGIFArbiter(reader, &system->gifPathArbiterComponent);
  }
  {
    auto component = reader->scope("gifPath1");
    readGIFPath1(reader, &system->gifPath1Component);
  }
  {
    auto component = reader->scope("gifPath3");
    readGIFPath3(reader, &system->gifPath3Component);
  }
  {
    auto component = reader->scope("gs");
    readGS(reader, &system->gsComponent);
  }
  {
    auto component = reader->scope("gifDMAC");
    readDMAC(
      reader,
      &system->gifDMACComponent,
      &system->dmacControllerComponent);
  }
  {
    auto component = reader->scope("vif1DMAC");
    readVIF1DMAC(reader, &system->vif1DMACComponent);
  }
  {
    auto component = reader->scope("gsDisplay");
    readGSDisplay(reader, &system->gsDisplayComponent);
  }
  {
    auto component = reader->scope("scratchpadDMA");
    readScratchpadDMAState(reader, system);
  }
}

void NekoSaveStateCodec::validateSystem(
  SaveStateReader *reader,
  const NekoSystem &system)
{
  reader->requireField(
    system.vu0Component.type == VPUType::VU0 &&
    system.vu1Component.type == VPUType::VU1,
    "VU identities do not match the system wiring");
  reader->requireField(
    system.vif0Component.type == VIFType::VIF0 &&
    system.vif1Component.type == VIFType::VIF1,
    "VIF identities do not match the system wiring");
  reader->requireField(
    system.vif1Component.path3Mask ==
      system.gifPathArbiterComponent.vifPath3Mask,
    "VIF1 and GIF PATH3 mask state disagree");
  if (system.gifPathArbiterComponent.interruptedPath3)
  {
    const GIFDecoderState &suspended =
      system.gifPathArbiterComponent.suspendedPath3State;
    reader->requireField(
      suspended.activePacket &&
      !suspended.waitingForTag &&
      suspended.tag.format == GIFDataFormat::Image,
      "suspended PATH3 state is invalid");
    reader->requireField(
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
  destination->eeCoreComponent.memorySystem =
    source->eeCoreComponent.memorySystem;
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
  destination->eeCoreComponent.youngerAStageContinuation =
    source->eeCoreComponent.youngerAStageContinuation;
  destination->eeCoreComponent.inFlightCOP1Operations =
    source->eeCoreComponent.inFlightCOP1Operations;
  destination->eeCoreComponent.nextEEProgramOrder =
    source->eeCoreComponent.nextEEProgramOrder;
  destination->eeCoreComponent.executingProgramOrder = 0;
  destination->eeCoreComponent.pendingMac0 =
    source->eeCoreComponent.pendingMac0;
  destination->eeCoreComponent.pendingMac1 =
    source->eeCoreComponent.pendingMac1;
  destination->eeCoreComponent.packedMACContinuation =
    source->eeCoreComponent.packedMACContinuation;
  destination->eeCoreComponent.packedDivideContinuation =
    source->eeCoreComponent.packedDivideContinuation;
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
  arbiter.queuedPath2Interruption =
    sourceArbiter.queuedPath2Interruption;
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
  dmac.channelState.channelControlRegister =
    sourceDMAC.channelState.channelControlRegister;
  dmac.channelState.memoryAddressRegister =
    sourceDMAC.channelState.memoryAddressRegister;
  dmac.channelState.quadwordCountRegister =
    sourceDMAC.channelState.quadwordCountRegister;
  dmac.channelState.tagAddressRegister =
    sourceDMAC.channelState.tagAddressRegister;
  dmac.channelState.addressStackRegisters =
    sourceDMAC.channelState.addressStackRegisters;
  dmac.channelState.terminateAfterPacket =
    sourceDMAC.channelState.terminateAfterPacket;
  dmac.path3Stalled = sourceDMAC.path3Stalled;
  dmac.channelState.addressStackDepth =
    sourceDMAC.channelState.addressStackDepth;
  dmac.transferredQuadwords =
    sourceDMAC.transferredQuadwords;

  destination->dmacControllerComponent.controlRegister =
    source->dmacControllerComponent.controlRegister;
  destination->dmacControllerComponent.statusRegister =
    source->dmacControllerComponent.statusRegister;
  destination->dmacControllerComponent.statusMaskRegister =
    source->dmacControllerComponent.statusMaskRegister;
  destination->dmacControllerComponent.interleaveSizeRegister =
    source->dmacControllerComponent.interleaveSizeRegister;

  const auto commitScratchpadChannel =
    [](ScratchpadDMACChannel *destinationChannel,
       const ScratchpadDMACChannel &sourceChannel)
    {
      destinationChannel->channelControlRegister =
        sourceChannel.channelControlRegister;
      destinationChannel->memoryAddressRegister =
        sourceChannel.memoryAddressRegister;
      destinationChannel->quadwordCountRegister =
        sourceChannel.quadwordCountRegister;
      destinationChannel->tagAddressRegister =
        sourceChannel.tagAddressRegister;
      destinationChannel->scratchpadAddressRegister =
        sourceChannel.scratchpadAddressRegister;
      destinationChannel->interleaveQuadwordsRemaining =
        sourceChannel.interleaveQuadwordsRemaining;
    };
  commitScratchpadChannel(
    &destination->fromScratchpadDMACComponent,
    source->fromScratchpadDMACComponent);
  commitScratchpadChannel(
    &destination->toScratchpadDMACComponent,
    source->toScratchpadDMACComponent);

  VIF1DMACChannel &vif1DMAC =
    destination->vif1DMACComponent;
  const VIF1DMACChannel &sourceVIF1DMAC =
    source->vif1DMACComponent;
  vif1DMAC.channelState.channelControlRegister =
    sourceVIF1DMAC.channelState.channelControlRegister;
  vif1DMAC.channelState.memoryAddressRegister =
    sourceVIF1DMAC.channelState.memoryAddressRegister;
  vif1DMAC.channelState.quadwordCountRegister =
    sourceVIF1DMAC.channelState.quadwordCountRegister;
  vif1DMAC.channelState.tagAddressRegister =
    sourceVIF1DMAC.channelState.tagAddressRegister;
  vif1DMAC.channelState.addressStackRegisters =
    sourceVIF1DMAC.channelState.addressStackRegisters;
  vif1DMAC.channelState.terminateAfterPacket =
    sourceVIF1DMAC.channelState.terminateAfterPacket;
  vif1DMAC.vif1Stalled = sourceVIF1DMAC.vif1Stalled;
  vif1DMAC.channelState.addressStackDepth =
    sourceVIF1DMAC.channelState.addressStackDepth;
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
  writer->writeFieldU64("masterCycle", system.masterClock.masterCycle);
  std::size_t serializedComponentCount = 0;
  for (const auto &scheduled : system.masterClock.components)
  {
    if (scheduled.component !=
          &system.fromScratchpadDMACComponent &&
        scheduled.component !=
          &system.toScratchpadDMACComponent)
    {
      ++serializedComponentCount;
    }
  }
  writer->writeFieldSize("componentCount", serializedComponentCount);
  auto components = writer->scope("components");
  std::size_t serializedIndex = 0;
  for (const auto &scheduled : system.masterClock.components)
  {
    if (scheduled.component ==
          &system.fromScratchpadDMACComponent ||
        scheduled.component ==
          &system.toScratchpadDMACComponent)
    {
      continue;
    }
    auto component = writer->element(serializedIndex++);
    writer->writeFieldU8(
      "id",
      componentID(system, scheduled.component));
    writer->writeFieldU64("period", scheduled.period);
    writer->writeFieldU64("phase", scheduled.phase);
  }
}

void NekoSaveStateCodec::readMasterClock(
  SaveStateReader *reader,
  MasterClockScheduler *clock,
  std::vector<ScheduledComponentState> *schedule)
{
  constexpr std::array<ScheduledComponentState, 6>
    LEGACY_COMPONENTS = {{
      {6, 1, 0},
      {1, NekoSystem::VU_CLOCK_PERIOD, 0},
      {2, NekoSystem::VU_CLOCK_PERIOD, 0},
      {4, 1, 0},
      {7, 1, 0},
      {5, 1, 0}
    }};
  constexpr std::array<ScheduledComponentState, 2>
    SCRATCHPAD_DMAC_COMPONENTS = {{
      {8, 1, 0},
      {9, 1, 0}
    }};

  clock->masterCycle = reader->readFieldU64("masterCycle");
  const std::uint32_t count =
    reader->readFieldU32("componentCount");
  reader->requireField(
    count == LEGACY_COMPONENTS.size() ||
      count == LEGACY_COMPONENTS.size() + 1 ||
      count ==
        LEGACY_COMPONENTS.size() +
        SCRATCHPAD_DMAC_COMPONENTS.size() ||
      count ==
        LEGACY_COMPONENTS.size() +
        SCRATCHPAD_DMAC_COMPONENTS.size() + 1,
    "master-clock component count is invalid");
  std::array<bool, 10> used = {};
  schedule->clear();
  schedule->reserve(count);
  auto components = reader->scope("components");
  for (std::uint32_t index = 0; index < count; ++index)
  {
    auto component = reader->element(index);
    const std::uint8_t id = reader->readFieldU8("id");
    reader->requireField(
      id >= 1 && id <= 9,
      "clock component ID is invalid");
    reader->requireField(
      !used[id],
      "clock component ID is duplicated");
    used[id] = true;
    const std::uint64_t period =
      reader->readFieldU64("period");
    reader->requireField(period != 0, "clock period is zero");
    const std::uint64_t phase =
      reader->readFieldU64("phase");
    reader->requireField(
      phase < period,
      "clock phase is outside its period");
    schedule->push_back({
      id,
      period,
      phase
    });
  }

  for (std::size_t index = 0;
       index < LEGACY_COMPONENTS.size();
       ++index)
  {
    const ScheduledComponentState &actual =
      (*schedule)[index];
    const ScheduledComponentState &expected =
      LEGACY_COMPONENTS[index];
    reader->requireField(
      actual.id == expected.id &&
        actual.period == expected.period &&
        actual.phase == expected.phase,
      "master-clock component schedule is invalid");
  }
  const bool hasScratchpadDMAC =
    schedule->size() >=
      LEGACY_COMPONENTS.size() +
      SCRATCHPAD_DMAC_COMPONENTS.size();
  if (hasScratchpadDMAC)
  {
    for (std::size_t index = 0;
         index < SCRATCHPAD_DMAC_COMPONENTS.size();
         ++index)
    {
      const ScheduledComponentState &actual =
        (*schedule)[LEGACY_COMPONENTS.size() + index];
      const ScheduledComponentState &expected =
        SCRATCHPAD_DMAC_COMPONENTS[index];
      reader->requireField(
        actual.id == expected.id &&
          actual.period == expected.period &&
          actual.phase == expected.phase,
        "scratchpad DMAC component schedule is invalid");
    }
  }
  const bool hasOptionalArbiter =
    schedule->size() ==
      LEGACY_COMPONENTS.size() + 1 ||
    schedule->size() ==
      LEGACY_COMPONENTS.size() +
      SCRATCHPAD_DMAC_COMPONENTS.size() + 1;
  if (hasOptionalArbiter)
  {
    reader->requireField(schedule->back().id == 3,
      "optional master-clock component is invalid");
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
  if (component == &system.fromScratchpadDMACComponent)
  {
    return 8;
  }
  if (component == &system.toScratchpadDMACComponent)
  {
    return 9;
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
    case 8:
      return &system->fromScratchpadDMACComponent;
    case 9:
      return &system->toScratchpadDMACComponent;
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
  result.reserve(source.size() + 2);
  const auto hasComponent =
    [&](std::uint8_t id)
    {
      for (const ScheduledComponentState &scheduled : source)
      {
        if (scheduled.id == id)
        {
          return true;
        }
      }
      return false;
    };
  const bool hasFromScratchpadDMAC = hasComponent(8);
  const bool hasToScratchpadDMAC = hasComponent(9);
  const auto appendScratchpadDMAC =
    [&]()
    {
      if (!hasFromScratchpadDMAC)
      {
        result.push_back({
          &destination->fromScratchpadDMACComponent,
          1,
          0
        });
      }
      if (!hasToScratchpadDMAC)
      {
        result.push_back({
          &destination->toScratchpadDMACComponent,
          1,
          0
        });
      }
    };
  for (const ScheduledComponentState &scheduled : source)
  {
    if (scheduled.id == 3)
    {
      appendScratchpadDMAC();
    }
    result.push_back({
      componentForID(destination, scheduled.id),
      scheduled.period,
      scheduled.phase
    });
  }
  if (source.empty() || source.back().id != 3)
  {
    appendScratchpadDMAC();
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
