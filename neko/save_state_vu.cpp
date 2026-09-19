#include "save_state_internal.hpp"

#include "vpu_flags.hpp"
#include "vpu_register_ids.hpp"

namespace
{
  constexpr std::uint32_t MAX_VIF_PAYLOAD_WORDS =
    65536u * 4u;
  constexpr std::uint32_t MAX_VIF_UNPACK_WORDS =
    256u * 4u;
  constexpr std::uint8_t MAX_PIPELINE_STAGE_INDEX = 64;

  void writeLowerInstruction(
    SaveStateWriter *writer,
    const LowerInstruction &instruction)
  {
    writer->writeU8(
      static_cast<std::uint8_t>(instruction.unit));
    writer->writeU32(instruction.opCode);
    writer->writeU8(instruction.sourceRegister1);
    writer->writeU8(instruction.sourceRegister2);
    writer->writeU8(instruction.destinationRegister);
    writer->writeU8(instruction.integerDestinationRegister);
    writer->writeU8(instruction.destinationFieldMask);
    writer->writeU8(instruction.sourceFieldMask1);
    writer->writeU8(instruction.sourceFieldMask2);
    writer->writeU16(
      static_cast<std::uint16_t>(instruction.immediate));
    writer->writeU32(instruction.immediateBits);
  }

  LowerInstruction readLowerInstruction(
    SaveStateReader *reader)
  {
    LowerInstruction instruction;
    instruction.unit = readEnum<LowerExecutionUnit>(
      reader,
      static_cast<std::uint8_t>(LowerExecutionUnit::Branch),
      "VU lower execution unit");
    instruction.opCode = reader->readU32();
    instruction.sourceRegister1 = reader->readU8();
    instruction.sourceRegister2 = reader->readU8();
    instruction.destinationRegister = reader->readU8();
    instruction.integerDestinationRegister = reader->readU8();
    instruction.destinationFieldMask = reader->readU8();
    instruction.sourceFieldMask1 = reader->readU8();
    instruction.sourceFieldMask2 = reader->readU8();
    instruction.immediate =
      static_cast<std::int16_t>(reader->readU16());
    instruction.immediateBits = reader->readU32();
    require(
      instruction.sourceRegister1 <= VPU_REGISTER_VF31 &&
      instruction.sourceRegister2 <= VPU_REGISTER_VF31 &&
      instruction.destinationRegister <= VPU_REGISTER_VF31 &&
      instruction.integerDestinationRegister <= VPU_REGISTER_VI15,
      "VU lower instruction register is invalid");
    require(
      instruction.destinationFieldMask <= FP_REGISTER_ALL_FIELDS &&
      instruction.sourceFieldMask1 <= FP_REGISTER_ALL_FIELDS &&
      instruction.sourceFieldMask2 <= FP_REGISTER_ALL_FIELDS,
      "VU lower instruction field mask is invalid");
    return instruction;
  }

  void writeVIFCommand(
    SaveStateWriter *writer,
    const VIFCommand &command)
  {
    writer->writeU8(
      static_cast<std::uint8_t>(command.kind));
    writer->writeU8(
      static_cast<std::uint8_t>(command.unpackFormat));
    writer->writeU32(command.raw);
    writer->writeU16(command.immediate);
    writer->writeU16(command.count);
    writer->writeU16(command.address);
    writer->writeU8(command.encodedCount);
    writer->writeU8(command.command);
    writer->writeBool(command.interrupt);
    writer->writeBool(command.masked);
    writer->writeBool(command.unsignedData);
    writer->writeBool(command.addTops);
  }

  VIFCommand readVIFCommand(SaveStateReader *reader)
  {
    VIFCommand command;
    command.kind = readEnum<VIFCommandKind>(
      reader,
      static_cast<std::uint8_t>(VIFCommandKind::UNPACK),
      "VIF command kind");
    command.unpackFormat = readEnum<VIFUnpackFormat>(
      reader,
      static_cast<std::uint8_t>(VIFUnpackFormat::V4_5),
      "VIF UNPACK format");
    command.raw = reader->readU32();
    command.immediate = reader->readU16();
    command.count = reader->readU16();
    command.address = reader->readU16();
    command.encodedCount = reader->readU8();
    command.command = reader->readU8();
    command.interrupt = reader->readBool("VIF command interrupt");
    command.masked = reader->readBool("VIF command mask");
    command.unsignedData =
      reader->readBool("VIF command unsigned flag");
    command.addTops =
      reader->readBool("VIF command TOPS flag");
    return command;
  }

}

void NekoSaveStateCodec::writeVPU(
  SaveStateWriter *writer,
  const VPU &vpu)
{
  writer->writeU8(static_cast<std::uint8_t>(vpu.type));
  writer->writeByteVector(vpu.microMem);
  writer->writeByteVector(vpu.vuMem);
  writer->writeU8(vpu.state);
  writer->writeU32(vpu.cycles);
  writer->writeU8(vpu.mode);
  writer->writeBool(vpu.macroIssueNeedsAdvance);
  writer->writeBool(vpu.macroTransferStallPending);
  writer->writeU16(vpu.microMemPC);
  writer->writeU16(vpu.terminationPositionCounter);
  writer->writeBool(vpu.terminationPositionValid);
  writer->writeBool(vpu.endDelaySlotPending);
  writer->writeBool(vpu.branchDelaySlotPending);
  writer->writeBool(vpu.pendingBranchTaken);
  writer->writeU16(vpu.pendingBranchTarget);
  writer->writeBool(vpu.pendingBranchLinkValid);
  writer->writeU8(vpu.pendingBranchLinkRegister);
  writer->writeU16(vpu.pendingBranchLinkValue);
  writer->writeBool(vpu.terminationRequested);
  writer->writeBool(vpu.haltAfterDrain);
  writer->writeBool(vpu.dEnabled);
  writer->writeBool(vpu.tEnabled);
  writer->writeBool(vpu.xgkickWaiting);
  writer->writeBool(vpu.xgkickTransferStarted);
  writer->writeBool(vpu.dBitStop);
  writer->writeBool(vpu.tBitStop);
  writer->writeBool(vpu.forceBreakStop);
  writer->writeBool(vpu.cop2WriteInterlockReleased);

  writer->writeSize(vpu.fpRegisters.size());
  for (const FPRegister &value : vpu.fpRegisters)
  {
    writeFPRegister(writer, value);
  }
  writer->writeSize(vpu.intRegisters.size());
  for (std::uint16_t value : vpu.intRegisters)
  {
    writer->writeU16(value);
  }
  writer->writeU32(vpu.iRegister.bits());
  writer->writeU32(vpu.qRegister.bits());
  writer->writeU32(vpu.pRegister.bits());
  writer->writeU32(vpu.rRegister);
  writer->writeU16(vpu.cmsarRegister);
  writer->writeU16(vpu.MACFlags);
  writer->writeU16(vpu.statusFlags);
  writeFPRegister(writer, vpu.accumulator);
  writer->writeU64(vpu.clippingFlags);
  writeOrchestrator(writer, vpu.orchestrator);
  writeFPRegister(writer, vpu.virtualDestRegister);
  writeFPRegister(writer, vpu.accumulatorForwardValue);
  writer->writeU8(vpu.pendingAccumulatorWrites);
  writer->writeBool(vpu.accumulatorForwardValid);
  writeLowerInstruction(writer, vpu.pendingLowerInstruction);
  writer->writeU16(vpu.pendingLowerInstructionAddress);
  writer->writeBool(vpu.lowerInstructionPending);
  writer->writeBool(vpu.pendingLowerInstructionReady);
  writer->writeBool(vpu.pendingLowerWritebackDiscarded);
  for (std::uint8_t value : vpu.pendingIntegerWrites)
  {
    writer->writeU8(value);
  }
  for (std::uint8_t value : vpu.pendingIALUWrites)
  {
    writer->writeU8(value);
  }
  for (std::uint16_t value : vpu.bypassedIntegerValues)
  {
    writer->writeU16(value);
  }
}

void NekoSaveStateCodec::readVPU(
  SaveStateReader *reader,
  VPU *vpu,
  PipelineListIndices *pipelineLists)
{
  const VPUType type = readEnum<VPUType>(
    reader,
    static_cast<std::uint8_t>(VPUType::VU1),
    "VU type");
  require(type == vpu->type, "VU type does not match its slot");
  vpu->microMem = reader->readByteVector(
    vpu->microMem.size(),
    "VU micro memory");
  vpu->vuMem = reader->readByteVector(
    vpu->vuMem.size(),
    "VU data memory");
  vpu->state = reader->readU8();
  vpu->cycles = reader->readU32();
  vpu->mode = reader->readU8();
  vpu->macroIssueNeedsAdvance =
    reader->readBool("VU macro issue-advance flag");
  vpu->macroTransferStallPending =
    reader->readBool("VU macro transfer-stall flag");
  vpu->microMemPC = reader->readU16();
  vpu->terminationPositionCounter = reader->readU16();
  vpu->terminationPositionValid =
    reader->readBool("VU termination-position flag");
  vpu->endDelaySlotPending =
    reader->readBool("VU end-delay flag");
  vpu->branchDelaySlotPending =
    reader->readBool("VU branch-delay flag");
  vpu->pendingBranchTaken =
    reader->readBool("VU pending-branch flag");
  vpu->pendingBranchTarget = reader->readU16();
  vpu->pendingBranchLinkValid =
    reader->readBool("VU branch-link flag");
  vpu->pendingBranchLinkRegister = reader->readU8();
  vpu->pendingBranchLinkValue = reader->readU16();
  vpu->terminationRequested =
    reader->readBool("VU termination-request flag");
  vpu->haltAfterDrain =
    reader->readBool("VU halt-after-drain flag");
  vpu->dEnabled = reader->readBool("VU D-bit enable");
  vpu->tEnabled = reader->readBool("VU T-bit enable");
  vpu->xgkickWaiting =
    reader->readBool("VU XGKICK wait flag");
  vpu->xgkickTransferStarted =
    reader->readBool("VU XGKICK start flag");
  vpu->dBitStop =
    reader->readBool("VU D-bit stop flag");
  vpu->tBitStop =
    reader->readBool("VU T-bit stop flag");
  vpu->forceBreakStop =
    reader->readBool("VU force-break stop flag");
  vpu->cop2WriteInterlockReleased =
    reader->readBool("VU COP2 write-interlock flag");

  const std::uint32_t fpRegisterCount = reader->readU32();
  require(
    fpRegisterCount == vpu->fpRegisters.size(),
    "VU floating-point register count is invalid");
  for (FPRegister &value : vpu->fpRegisters)
  {
    value = readFPRegister(reader);
  }
  const std::uint32_t intRegisterCount = reader->readU32();
  require(
    intRegisterCount == vpu->intRegisters.size(),
    "VU integer register count is invalid");
  for (std::uint16_t &value : vpu->intRegisters)
  {
    value = reader->readU16();
  }
  vpu->iRegister.setBits(reader->readU32());
  vpu->qRegister.setBits(reader->readU32());
  vpu->pRegister.setBits(reader->readU32());
  vpu->rRegister = reader->readU32();
  vpu->cmsarRegister = reader->readU16();
  vpu->MACFlags = reader->readU16();
  vpu->statusFlags = reader->readU16();
  vpu->accumulator = readFPRegister(reader);
  vpu->clippingFlags = reader->readU64();
  readOrchestrator(
    reader,
    &vpu->orchestrator,
    pipelineLists);
  vpu->virtualDestRegister = readFPRegister(reader);
  vpu->accumulatorForwardValue = readFPRegister(reader);
  vpu->pendingAccumulatorWrites = reader->readU8();
  vpu->accumulatorForwardValid =
    reader->readBool("VU accumulator-forward flag");
  vpu->pendingLowerInstruction =
    readLowerInstruction(reader);
  vpu->pendingLowerInstructionAddress = reader->readU16();
  vpu->lowerInstructionPending =
    reader->readBool("VU lower-pending flag");
  vpu->pendingLowerInstructionReady =
    reader->readBool("VU lower-ready flag");
  vpu->pendingLowerWritebackDiscarded =
    reader->readBool("VU lower-discard flag");
  for (std::uint8_t &value : vpu->pendingIntegerWrites)
  {
    value = reader->readU8();
  }
  for (std::uint8_t &value : vpu->pendingIALUWrites)
  {
    value = reader->readU8();
  }
  for (std::uint16_t &value : vpu->bypassedIntegerValues)
  {
    value = reader->readU16();
  }

  require(
    vpu->state >= VPU_STATE_READY &&
    vpu->state <= VPU_STATE_STOP,
    "VU run state is invalid");
  require(
    vpu->mode == VPU_MODE_MICRO ||
    vpu->mode == VPU_MODE_MACRO,
    "VU mode is invalid");
  require(
    !vpu->macroIssueNeedsAdvance ||
    (vpu->state == VPU_STATE_RUN &&
     vpu->mode == VPU_MODE_MACRO),
    "VU macro issue-advance state is invalid");
  require(
    vpu->microMemPC <= vpu->microMem.size() &&
    vpu->microMemPC % 8 == 0,
    "VU program counter is invalid");
  require(
    vpu->pendingBranchTarget < vpu->microMem.size() &&
    vpu->pendingBranchTarget % 8 == 0,
    "VU pending branch target is invalid");
  require(
    vpu->pendingBranchLinkRegister <= VPU_REGISTER_VI15,
    "VU branch-link register is invalid");
  require(
    vpu->statusFlags <= VPU_FLAG_DS,
    "VU status flags are invalid");
  require(
    !vpu->forceBreakStop ||
      (!vpu->dBitStop && !vpu->tBitStop),
    "VU stop cause is invalid");
  require(
    (!vpu->dBitStop &&
     !vpu->tBitStop &&
     !vpu->forceBreakStop) ||
      vpu->state == VPU_STATE_STOP,
    "VU stop cause does not match run state");
  require(
    !vpu->cop2WriteInterlockReleased ||
      vpu->state == VPU_STATE_RUN,
    "VU COP2 write interlock does not match run state");
  require(
    vpu->clippingFlags <= VPU_CLIPPING_FLAG_MASK,
    "VU clipping flags are invalid");
  require(
    vpu->pendingAccumulatorWrites <= MAX_PIPELINES,
    "VU pending accumulator count is invalid");
  for (std::uint8_t value : vpu->pendingIntegerWrites)
  {
    require(
      value <= MAX_PIPELINES,
      "VU pending integer-write count is invalid");
  }
  for (std::uint8_t value : vpu->pendingIALUWrites)
  {
    require(
      value <= MAX_PIPELINES,
      "VU pending IALU-write count is invalid");
  }
}

void NekoSaveStateCodec::commitVPU(
  VPU *destination,
  VPU *source,
  PipelineLists *lists)
{
  destination->traceCallbackFailure = nullptr;
  destination->microMem.swap(source->microMem);
  destination->vuMem.swap(source->vuMem);
  destination->state = source->state;
  destination->cycles = source->cycles;
  destination->mode = source->mode;
  destination->macroIssueNeedsAdvance =
    source->macroIssueNeedsAdvance;
  destination->macroTransferStallPending =
    source->macroTransferStallPending;
  destination->microMemPC = source->microMemPC;
  destination->terminationPositionCounter =
    source->terminationPositionCounter;
  destination->terminationPositionValid =
    source->terminationPositionValid;
  destination->endDelaySlotPending =
    source->endDelaySlotPending;
  destination->branchDelaySlotPending =
    source->branchDelaySlotPending;
  destination->pendingBranchTaken =
    source->pendingBranchTaken;
  destination->pendingBranchTarget =
    source->pendingBranchTarget;
  destination->pendingBranchLinkValid =
    source->pendingBranchLinkValid;
  destination->pendingBranchLinkRegister =
    source->pendingBranchLinkRegister;
  destination->pendingBranchLinkValue =
    source->pendingBranchLinkValue;
  destination->terminationRequested =
    source->terminationRequested;
  destination->haltAfterDrain = source->haltAfterDrain;
  destination->dEnabled = source->dEnabled;
  destination->tEnabled = source->tEnabled;
  destination->xgkickWaiting = source->xgkickWaiting;
  destination->xgkickTransferStarted =
    source->xgkickTransferStarted;
  destination->dBitStop = source->dBitStop;
  destination->tBitStop = source->tBitStop;
  destination->forceBreakStop = source->forceBreakStop;
  destination->cop2WriteInterlockReleased =
    source->cop2WriteInterlockReleased;
  destination->fpRegisters.swap(source->fpRegisters);
  destination->intRegisters.swap(source->intRegisters);
  destination->iRegister = source->iRegister;
  destination->qRegister = source->qRegister;
  destination->pRegister = source->pRegister;
  destination->rRegister = source->rRegister;
  destination->cmsarRegister = source->cmsarRegister;
  destination->MACFlags = source->MACFlags;
  destination->statusFlags = source->statusFlags;
  destination->accumulator = source->accumulator;
  destination->clippingFlags = source->clippingFlags;
  destination->orchestrator.pipelines =
    source->orchestrator.pipelines;
  destination->orchestrator.stalling =
    source->orchestrator.stalling;
  destination->orchestrator.executing.swap((*lists)[0]);
  destination->orchestrator.waiting.swap((*lists)[1]);
  destination->orchestrator.pool.swap((*lists)[2]);
  destination->virtualDestRegister =
    source->virtualDestRegister;
  destination->accumulatorForwardValue =
    source->accumulatorForwardValue;
  destination->pendingAccumulatorWrites =
    source->pendingAccumulatorWrites;
  destination->accumulatorForwardValid =
    source->accumulatorForwardValid;
  destination->pendingLowerInstruction =
    source->pendingLowerInstruction;
  destination->pendingLowerInstructionAddress =
    source->pendingLowerInstructionAddress;
  destination->lowerInstructionPending =
    source->lowerInstructionPending;
  destination->pendingLowerInstructionReady =
    source->pendingLowerInstructionReady;
  destination->pendingLowerWritebackDiscarded =
    source->pendingLowerWritebackDiscarded;
  destination->pendingIntegerWrites =
    source->pendingIntegerWrites;
  destination->pendingIALUWrites =
    source->pendingIALUWrites;
  destination->bypassedIntegerValues =
    source->bypassedIntegerValues;
}

void NekoSaveStateCodec::writePipeline(
  SaveStateWriter *writer,
  const Pipeline &pipeline)
{
  writer->writeU8(pipeline.type);
  writer->writeU16(pipeline.opCode);
  writer->writeU32(
    static_cast<std::uint32_t>(pipeline.intResult));
  writeFPRegister(writer, pipeline.fpResult);
  writeFPRegister(writer, pipeline.flagResult);
  writeFPRegister(writer, pipeline.operationResult);
  writeFPRegister(writer, pipeline.accumulatorValue);
  writeFPRegister(writer, pipeline.sourceValue1);
  writeFPRegister(writer, pipeline.sourceValue2);
  writer->writeU8(pipeline.ignoredResultFields);
  writer->writeU8(pipeline.srcReg1);
  writer->writeU8(pipeline.srcReg2);
  writer->writeU8(pipeline.destReg);
  writer->writeU8(pipeline.integerDestReg);
  writer->writeU8(pipeline.destFieldMask);
  writer->writeU8(pipeline.srcReg1FieldMask);
  writer->writeU8(pipeline.srcReg2FieldMask);
  writer->writeU16(pipeline.instructionAddress);
  writer->writeU16(pipeline.memoryAddress);
  writer->writeU16(
    static_cast<std::uint16_t>(pipeline.immediate));
  writer->writeU32(pipeline.immediateBits);
  writer->writeU32(pipeline.scalarResultBits);
  writer->writeU8(pipeline.scalarResultFlags);
  writer->writeU16(pipeline.intSourceValue1);
  writer->writeU16(pipeline.intSourceValue2);
  writer->writeBool(pipeline.intSource1Sampled);
  writer->writeBool(pipeline.intSource2Sampled);
  writer->writeBool(pipeline.vectorSourcesSampled);
  writer->writeBool(pipeline.xgkickStarted);
  writer->writeBool(pipeline.discardWriteback);
  writer->writeU8(
    static_cast<std::uint8_t>(pipeline.currentStage));
  writer->writeU8(pipeline.currentStageIndex);
  writer->writeU8(pipeline.executionStageCount);
  writer->writeBool(pipeline.complete);
}

void NekoSaveStateCodec::readPipeline(
  SaveStateReader *reader,
  Pipeline *pipeline)
{
  pipeline->type = reader->readU8();
  pipeline->opCode = reader->readU16();
  pipeline->intResult =
    static_cast<std::int32_t>(reader->readU32());
  pipeline->fpResult = readFPRegister(reader);
  pipeline->flagResult = readFPRegister(reader);
  pipeline->operationResult = readFPRegister(reader);
  pipeline->accumulatorValue = readFPRegister(reader);
  pipeline->sourceValue1 = readFPRegister(reader);
  pipeline->sourceValue2 = readFPRegister(reader);
  pipeline->ignoredResultFields = reader->readU8();
  pipeline->srcReg1 = reader->readU8();
  pipeline->srcReg2 = reader->readU8();
  pipeline->destReg = reader->readU8();
  pipeline->integerDestReg = reader->readU8();
  pipeline->destFieldMask = reader->readU8();
  pipeline->srcReg1FieldMask = reader->readU8();
  pipeline->srcReg2FieldMask = reader->readU8();
  pipeline->instructionAddress = reader->readU16();
  pipeline->memoryAddress = reader->readU16();
  pipeline->immediate =
    static_cast<std::int16_t>(reader->readU16());
  pipeline->immediateBits = reader->readU32();
  pipeline->scalarResultBits = reader->readU32();
  pipeline->scalarResultFlags = reader->readU8();
  pipeline->intSourceValue1 = reader->readU16();
  pipeline->intSourceValue2 = reader->readU16();
  pipeline->intSource1Sampled =
    reader->readBool("VU pipeline source-1 sample flag");
  pipeline->intSource2Sampled =
    reader->readBool("VU pipeline source-2 sample flag");
  pipeline->vectorSourcesSampled =
    reader->readBool("VU pipeline vector-source sample flag");
  pipeline->xgkickStarted =
    reader->readBool("VU pipeline XGKICK flag");
  pipeline->discardWriteback =
    reader->readBool("VU pipeline discard flag");
  pipeline->currentStage = readEnum<VUPipelineStage>(
    reader,
    static_cast<std::uint8_t>(VUPipelineStage::P),
    "VU pipeline stage");
  pipeline->currentStageIndex = reader->readU8();
  pipeline->executionStageCount = reader->readU8();
  pipeline->complete =
    reader->readBool("VU pipeline completion flag");

  require(
    pipeline->type <= VPU_PIPELINE_TYPE_VIF_CONTROL,
    "VU pipeline type is invalid");
  require(
    pipeline->ignoredResultFields <= FP_REGISTER_ALL_FIELDS &&
    pipeline->destFieldMask <= FP_REGISTER_ALL_FIELDS &&
    pipeline->srcReg1FieldMask <= FP_REGISTER_ALL_FIELDS &&
    pipeline->srcReg2FieldMask <= FP_REGISTER_ALL_FIELDS,
    "VU pipeline field mask is invalid");
  require(
    pipeline->srcReg1 <= VPU_REGISTER_VF31 &&
    pipeline->srcReg2 <= VPU_REGISTER_VF31 &&
    pipeline->destReg <= VPU_REGISTER_ACCUMULATOR &&
    pipeline->integerDestReg <= VPU_REGISTER_VI15,
    "VU pipeline register is invalid");
  require(
    (pipeline->scalarResultFlags & ~0x0f) == 0,
    "VU scalar result flags are invalid");
  require(
    pipeline->currentStageIndex <= MAX_PIPELINE_STAGE_INDEX &&
    pipeline->executionStageCount <= MAX_PIPELINE_STAGE_INDEX,
    "VU pipeline stage timing is invalid");
}

void NekoSaveStateCodec::writeOrchestrator(
  SaveStateWriter *writer,
  const PipelineOrchestrator &orchestrator)
{
  writer->writeBool(orchestrator.stalling);
  writer->writeSize(orchestrator.pipelines.size());
  for (const Pipeline &pipeline : orchestrator.pipelines)
  {
    writePipeline(writer, pipeline);
  }
  const std::list<Pipeline *> *lists[] = {
    &orchestrator.executing,
    &orchestrator.waiting,
    &orchestrator.pool
  };
  for (const std::list<Pipeline *> *list : lists)
  {
    writer->writeSize(list->size());
    for (const Pipeline *pipeline : *list)
    {
      writer->writeU8(pipelineIndex(
        orchestrator,
        pipeline));
    }
  }
}

void NekoSaveStateCodec::readOrchestrator(
  SaveStateReader *reader,
  PipelineOrchestrator *orchestrator,
  PipelineListIndices *pipelineLists)
{
  orchestrator->stalling =
    reader->readBool("VU orchestrator stall flag");
  const std::uint32_t pipelineCount = reader->readU32();
  require(
    pipelineCount == orchestrator->pipelines.size(),
    "VU pipeline-array size is invalid");
  for (Pipeline &pipeline : orchestrator->pipelines)
  {
    readPipeline(reader, &pipeline);
  }

  std::array<bool, MAX_PIPELINES> used = {};
  for (std::vector<std::uint8_t> &list : *pipelineLists)
  {
    list.clear();
  }
  std::size_t membershipCount = 0;
  for (std::vector<std::uint8_t> &list : *pipelineLists)
  {
    const std::uint32_t count = reader->readU32();
    require(count <= MAX_PIPELINES, "VU pipeline-list size is invalid");
    list.reserve(count);
    membershipCount += count;
    for (std::uint32_t index = 0; index < count; ++index)
    {
      const std::uint8_t pipeline = reader->readU8();
      require(
        pipeline < MAX_PIPELINES,
        "VU pipeline-list index is invalid");
      require(
        !used[pipeline],
        "VU pipeline appears in multiple lists");
      used[pipeline] = true;
      list.push_back(pipeline);
    }
  }
  require(
    membershipCount == MAX_PIPELINES,
    "VU pipeline-list membership is incomplete");
}

NekoSaveStateCodec::PipelineLists
NekoSaveStateCodec::reconcilePipelineLists(
  const PipelineListIndices &source,
  PipelineOrchestrator *destination)
{
  PipelineLists result;
  for (std::size_t listIndex = 0;
       listIndex < result.size();
       ++listIndex)
  {
    for (std::uint8_t pipeline : source[listIndex])
    {
      result[listIndex].push_back(
        &destination->pipelines[pipeline]);
    }
  }
  return result;
}

std::uint8_t NekoSaveStateCodec::pipelineIndex(
  const PipelineOrchestrator &orchestrator,
  const Pipeline *pipeline)
{
  const Pipeline *begin = orchestrator.pipelines.data();
  for (std::size_t index = 0;
       index < orchestrator.pipelines.size();
       ++index)
  {
    if (pipeline == begin + index)
    {
      return static_cast<std::uint8_t>(index);
    }
  }
  throw std::runtime_error(
    "Cannot save an invalid VU pipeline pointer.");
}

void NekoSaveStateCodec::writeVIF(
  SaveStateWriter *writer,
  const VIF &vif)
{
  writer->writeU8(static_cast<std::uint8_t>(vif.type));
  writer->writeU16(vif.cycleRegister);
  writer->writeU8(vif.modeRegister);
  writer->writeU32(vif.maskRegister);
  for (std::uint32_t value : vif.rowRegisters)
  {
    writer->writeU32(value);
  }
  for (std::uint32_t value : vif.columnRegisters)
  {
    writer->writeU32(value);
  }
  writer->writeU16(vif.topRegister);
  writer->writeU16(vif.itopRegister);
  writer->writeU16(vif.itopsRegister);
  writer->writeU16(vif.baseRegister);
  writer->writeU16(vif.offsetRegister);
  writer->writeU16(vif.topsRegister);
  writer->writeU16(vif.markRegister);
  writer->writeBool(vif.dbf);
  writer->writeBool(vif.path3Mask);
  writer->writeBool(vif.markFlag);
  writer->writeBool(vif.interruptFlag);
  writer->writeU32(vif.codeRegister);
  writeVIFCommand(writer, vif.streamCommand);
  writer->writeU32(vif.streamPayloadWordCount);
  writer->writeU32(vif.streamPayloadWordsRemaining);
  writer->writeU64(vif.streamWordsIngested);
  writer->writeU32(vif.mpgLowerInstruction);
  writer->writeBool(vif.mpgLowerInstructionPending);
  for (std::uint32_t value : vif.directQuadword)
  {
    writer->writeU32(value);
  }
  writer->writeSize(vif.unpackPayload.size());
  for (std::uint32_t value : vif.unpackPayload)
  {
    writer->writeU32(value);
  }
  writer->writeSize(vif.fifoWords.size());
  for (std::uint32_t value : vif.fifoWords)
  {
    writer->writeU32(value);
  }
}

void NekoSaveStateCodec::readVIF(
  SaveStateReader *reader,
  VIF *vif)
{
  const VIFType type = readEnum<VIFType>(
    reader,
    static_cast<std::uint8_t>(VIFType::VIF1),
    "VIF type");
  require(type == vif->type, "VIF type does not match its slot");
  vif->cycleRegister = reader->readU16();
  vif->modeRegister = reader->readU8();
  vif->maskRegister = reader->readU32();
  for (std::uint32_t &value : vif->rowRegisters)
  {
    value = reader->readU32();
  }
  for (std::uint32_t &value : vif->columnRegisters)
  {
    value = reader->readU32();
  }
  vif->topRegister = reader->readU16();
  vif->itopRegister = reader->readU16();
  vif->itopsRegister = reader->readU16();
  vif->baseRegister = reader->readU16();
  vif->offsetRegister = reader->readU16();
  vif->topsRegister = reader->readU16();
  vif->markRegister = reader->readU16();
  vif->dbf = reader->readBool("VIF double-buffer flag");
  vif->path3Mask = reader->readBool("VIF PATH3 mask");
  vif->markFlag = reader->readBool("VIF mark flag");
  vif->interruptFlag = reader->readBool("VIF interrupt flag");
  vif->codeRegister = reader->readU32();
  vif->streamCommand = readVIFCommand(reader);
  vif->streamPayloadWordCount = reader->readU32();
  vif->streamPayloadWordsRemaining = reader->readU32();
  vif->streamWordsIngested = reader->readU64();
  vif->mpgLowerInstruction = reader->readU32();
  vif->mpgLowerInstructionPending =
    reader->readBool("VIF MPG half-instruction flag");
  for (std::uint32_t &value : vif->directQuadword)
  {
    value = reader->readU32();
  }
  const std::uint32_t unpackCount = reader->readU32();
  require(
    unpackCount <= MAX_VIF_UNPACK_WORDS,
    "VIF UNPACK payload size is invalid");
  std::vector<std::uint32_t> unpackPayload;
  unpackPayload.reserve(unpackCount);
  for (std::uint32_t index = 0;
       index < unpackCount;
       ++index)
  {
    unpackPayload.push_back(reader->readU32());
  }
  vif->unpackPayload.swap(unpackPayload);
  const std::uint32_t fifoWordCount = reader->readU32();
  require(
    fifoWordCount <= vif->fifoCapacity() * 4,
    "VIF FIFO size is invalid");
  std::deque<std::uint32_t> fifoWords;
  for (std::uint32_t index = 0;
       index < fifoWordCount;
       ++index)
  {
    fifoWords.push_back(reader->readU32());
  }
  vif->fifoWords.swap(fifoWords);

  require(vif->modeRegister <= 2, "VIF mode is invalid");
  require(
    vif->streamPayloadWordCount <= MAX_VIF_PAYLOAD_WORDS &&
    vif->streamPayloadWordsRemaining <=
      vif->streamPayloadWordCount,
    "VIF payload progress is invalid");
}

void NekoSaveStateCodec::commitVIF(
  VIF *destination,
  VIF *source)
{
  destination->cycleRegister = source->cycleRegister;
  destination->modeRegister = source->modeRegister;
  destination->maskRegister = source->maskRegister;
  destination->rowRegisters = source->rowRegisters;
  destination->columnRegisters = source->columnRegisters;
  destination->topRegister = source->topRegister;
  destination->itopRegister = source->itopRegister;
  destination->itopsRegister = source->itopsRegister;
  destination->baseRegister = source->baseRegister;
  destination->offsetRegister = source->offsetRegister;
  destination->topsRegister = source->topsRegister;
  destination->markRegister = source->markRegister;
  destination->dbf = source->dbf;
  destination->path3Mask = source->path3Mask;
  destination->markFlag = source->markFlag;
  destination->interruptFlag = source->interruptFlag;
  destination->codeRegister = source->codeRegister;
  destination->streamCommand = source->streamCommand;
  destination->streamPayloadWordCount =
    source->streamPayloadWordCount;
  destination->streamPayloadWordsRemaining =
    source->streamPayloadWordsRemaining;
  destination->streamWordsIngested =
    source->streamWordsIngested;
  destination->mpgLowerInstruction =
    source->mpgLowerInstruction;
  destination->mpgLowerInstructionPending =
    source->mpgLowerInstructionPending;
  destination->directQuadword = source->directQuadword;
  destination->unpackPayload.swap(source->unpackPayload);
  destination->fifoWords.swap(source->fifoWords);
}

