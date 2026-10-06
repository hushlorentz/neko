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
    auto scope = writer->scope("pendingLowerInstruction");
    writer->writeFieldU8(
      "unit",
      static_cast<std::uint8_t>(instruction.unit));
    writer->writeFieldU32("opCode", instruction.opCode);
    writer->writeFieldU8("sourceRegister1", instruction.sourceRegister1);
    writer->writeFieldU8("sourceRegister2", instruction.sourceRegister2);
    writer->writeFieldU8(
      "destinationRegister",
      instruction.destinationRegister);
    writer->writeFieldU8(
      "integerDestinationRegister",
      instruction.integerDestinationRegister);
    writer->writeFieldU8(
      "destinationFieldMask",
      instruction.destinationFieldMask);
    writer->writeFieldU8(
      "sourceFieldMask1",
      instruction.sourceFieldMask1);
    writer->writeFieldU8(
      "sourceFieldMask2",
      instruction.sourceFieldMask2);
    writer->writeFieldU16(
      "immediate",
      static_cast<std::uint16_t>(instruction.immediate));
    writer->writeFieldU32("immediateBits", instruction.immediateBits);
  }

  LowerInstruction readLowerInstruction(
    SaveStateReader *reader)
  {
    const auto require =
      [reader](bool condition, const std::string &detail)
      {
        reader->requireField(condition, detail);
      };
    auto scope = reader->scope("pendingLowerInstruction");
    LowerInstruction instruction;
    instruction.unit = readEnum<LowerExecutionUnit>(
      reader,
      static_cast<std::uint8_t>(LowerExecutionUnit::Branch),
      "unit");
    instruction.opCode = reader->readFieldU32("opCode");
    instruction.sourceRegister1 =
      reader->readFieldU8("sourceRegister1");
    instruction.sourceRegister2 =
      reader->readFieldU8("sourceRegister2");
    instruction.destinationRegister =
      reader->readFieldU8("destinationRegister");
    instruction.integerDestinationRegister =
      reader->readFieldU8("integerDestinationRegister");
    instruction.destinationFieldMask =
      reader->readFieldU8("destinationFieldMask");
    instruction.sourceFieldMask1 =
      reader->readFieldU8("sourceFieldMask1");
    instruction.sourceFieldMask2 =
      reader->readFieldU8("sourceFieldMask2");
    instruction.immediate =
      static_cast<std::int16_t>(
        reader->readFieldU16("immediate"));
    instruction.immediateBits =
      reader->readFieldU32("immediateBits");
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
    auto scope = writer->scope("streamCommand");
    writer->writeFieldU8(
      "kind",
      static_cast<std::uint8_t>(command.kind));
    writer->writeFieldU8(
      "unpackFormat",
      static_cast<std::uint8_t>(command.unpackFormat));
    writer->writeFieldU32("raw", command.raw);
    writer->writeFieldU16("immediate", command.immediate);
    writer->writeFieldU16("count", command.count);
    writer->writeFieldU16("address", command.address);
    writer->writeFieldU8("encodedCount", command.encodedCount);
    writer->writeFieldU8("command", command.command);
    writer->writeFieldBool("interrupt", command.interrupt);
    writer->writeFieldBool("masked", command.masked);
    writer->writeFieldBool("unsignedData", command.unsignedData);
    writer->writeFieldBool("addTops", command.addTops);
  }

  VIFCommand readVIFCommand(SaveStateReader *reader)
  {
    auto scope = reader->scope("streamCommand");
    VIFCommand command;
    command.kind = readEnum<VIFCommandKind>(
      reader,
      static_cast<std::uint8_t>(VIFCommandKind::UNPACK),
      "kind");
    command.unpackFormat = readEnum<VIFUnpackFormat>(
      reader,
      static_cast<std::uint8_t>(VIFUnpackFormat::V4_5),
      "unpackFormat");
    command.raw = reader->readFieldU32("raw");
    command.immediate = reader->readFieldU16("immediate");
    command.count = reader->readFieldU16("count");
    command.address = reader->readFieldU16("address");
    command.encodedCount = reader->readFieldU8("encodedCount");
    command.command = reader->readFieldU8("command");
    command.interrupt = reader->readFieldBool("interrupt");
    command.masked = reader->readFieldBool("masked");
    command.unsignedData =
      reader->readFieldBool("unsignedData");
    command.addTops =
      reader->readFieldBool("addTops");
    return command;
  }

}

void NekoSaveStateCodec::writeVPU(
  SaveStateWriter *writer,
  const VPU &vpu)
{
  writer->writeFieldU8("type", static_cast<std::uint8_t>(vpu.type));
  writer->writeFieldByteVector("microMem", vpu.microMem);
  writer->writeFieldByteVector("vuMem", vpu.vuMem);
  writer->writeFieldU8("state", vpu.state);
  writer->writeFieldU32("cycles", vpu.cycles);
  writer->writeFieldU8("mode", vpu.mode);
  writer->writeFieldBool(
    "macroIssueNeedsAdvance",
    vpu.macroIssueNeedsAdvance);
  writer->writeFieldBool(
    "macroTransferStallPending",
    vpu.macroTransferStallPending);
  writer->writeFieldU16("microMemPC", vpu.microMemPC);
  writer->writeFieldU16(
    "terminationPositionCounter",
    vpu.terminationPositionCounter);
  writer->writeFieldBool(
    "terminationPositionValid",
    vpu.terminationPositionValid);
  writer->writeFieldBool("endDelaySlotPending", vpu.endDelaySlotPending);
  writer->writeFieldBool(
    "branchDelaySlotPending",
    vpu.branchDelaySlotPending);
  writer->writeFieldBool("pendingBranchTaken", vpu.pendingBranchTaken);
  writer->writeFieldU16("pendingBranchTarget", vpu.pendingBranchTarget);
  writer->writeFieldBool(
    "pendingBranchLinkValid",
    vpu.pendingBranchLinkValid);
  writer->writeFieldU8(
    "pendingBranchLinkRegister",
    vpu.pendingBranchLinkRegister);
  writer->writeFieldU16(
    "pendingBranchLinkValue",
    vpu.pendingBranchLinkValue);
  writer->writeFieldBool(
    "terminationRequested",
    vpu.terminationRequested);
  writer->writeFieldBool("haltAfterDrain", vpu.haltAfterDrain);
  writer->writeFieldBool("dEnabled", vpu.dEnabled);
  writer->writeFieldBool("tEnabled", vpu.tEnabled);
  writer->writeFieldBool("xgkickWaiting", vpu.xgkickWaiting);
  writer->writeFieldBool(
    "xgkickTransferStarted",
    vpu.xgkickTransferStarted);
  writer->writeFieldBool("dBitStop", vpu.dBitStop);
  writer->writeFieldBool("tBitStop", vpu.tBitStop);
  writer->writeFieldBool("forceBreakStop", vpu.forceBreakStop);
  writer->writeFieldBool(
    "cop2WriteInterlockReleased",
    vpu.cop2WriteInterlockReleased);

  writer->writeFieldSize("fpRegisterCount", vpu.fpRegisters.size());
  {
    auto registers = writer->scope("fpRegisters");
    for (std::size_t index = 0;
         index < vpu.fpRegisters.size();
         ++index)
    {
      auto element = writer->element(index);
      writeFPRegister(writer, vpu.fpRegisters[index]);
    }
  }
  writer->writeFieldSize("intRegisterCount", vpu.intRegisters.size());
  {
    auto registers = writer->scope("intRegisters");
    for (std::size_t index = 0;
         index < vpu.intRegisters.size();
         ++index)
    {
      auto element = writer->element(index);
      writer->writeFieldU16("value", vpu.intRegisters[index]);
    }
  }
  writer->writeFieldU32("iRegister", vpu.iRegister.bits());
  writer->writeFieldU32("qRegister", vpu.qRegister.bits());
  writer->writeFieldU32("pRegister", vpu.pRegister.bits());
  writer->writeFieldU32("rRegister", vpu.rRegister);
  writer->writeFieldU16("cmsarRegister", vpu.cmsarRegister);
  writer->writeFieldU16("macFlags", vpu.MACFlags);
  writer->writeFieldU16("statusFlags", vpu.statusFlags);
  {
    auto accumulator = writer->scope("accumulator");
    writeFPRegister(writer, vpu.accumulator);
  }
  writer->writeFieldU64("clippingFlags", vpu.clippingFlags);
  writeOrchestrator(writer, vpu.orchestrator);
  {
    auto field = writer->scope("virtualDestRegister");
    writeFPRegister(writer, vpu.virtualDestRegister);
  }
  {
    auto field = writer->scope("accumulatorForwardValue");
    writeFPRegister(writer, vpu.accumulatorForwardValue);
  }
  writer->writeFieldU8(
    "pendingAccumulatorWrites",
    vpu.pendingAccumulatorWrites);
  writer->writeFieldBool(
    "accumulatorForwardValid",
    vpu.accumulatorForwardValid);
  writeLowerInstruction(writer, vpu.pendingLowerInstruction);
  writer->writeFieldU16(
    "pendingLowerInstructionAddress",
    vpu.pendingLowerInstructionAddress);
  writer->writeFieldBool(
    "lowerInstructionPending",
    vpu.lowerInstructionPending);
  writer->writeFieldBool(
    "pendingLowerInstructionReady",
    vpu.pendingLowerInstructionReady);
  writer->writeFieldBool(
    "pendingLowerWritebackDiscarded",
    vpu.pendingLowerWritebackDiscarded);
  {
    auto values = writer->scope("pendingIntegerWrites");
    for (std::size_t index = 0;
         index < vpu.pendingIntegerWrites.size();
         ++index)
    {
      auto element = writer->element(index);
      writer->writeFieldU8("count", vpu.pendingIntegerWrites[index]);
    }
  }
  {
    auto values = writer->scope("pendingIALUWrites");
    for (std::size_t index = 0;
         index < vpu.pendingIALUWrites.size();
         ++index)
    {
      auto element = writer->element(index);
      writer->writeFieldU8("count", vpu.pendingIALUWrites[index]);
    }
  }
  {
    auto values = writer->scope("bypassedIntegerValues");
    for (std::size_t index = 0;
         index < vpu.bypassedIntegerValues.size();
         ++index)
    {
      auto element = writer->element(index);
      writer->writeFieldU16("value", vpu.bypassedIntegerValues[index]);
    }
  }
}

void NekoSaveStateCodec::readVPU(
  SaveStateReader *reader,
  VPU *vpu,
  PipelineListIndices *pipelineLists)
{
  const auto require =
    [reader](bool condition, const std::string &detail)
    {
      reader->requireField(condition, detail);
    };
  const VPUType type = readEnum<VPUType>(
    reader,
    static_cast<std::uint8_t>(VPUType::VU1),
    "type");
  require(type == vpu->type, "VU type does not match its slot");
  vpu->microMem =
    reader->readFieldByteVector("microMem", vpu->microMem.size());
  vpu->vuMem =
    reader->readFieldByteVector("vuMem", vpu->vuMem.size());
  vpu->state = reader->readFieldU8("state");
  vpu->cycles = reader->readFieldU32("cycles");
  vpu->mode = reader->readFieldU8("mode");
  vpu->macroIssueNeedsAdvance =
    reader->readFieldBool("macroIssueNeedsAdvance");
  vpu->macroTransferStallPending =
    reader->readFieldBool("macroTransferStallPending");
  vpu->microMemPC = reader->readFieldU16("microMemPC");
  vpu->terminationPositionCounter =
    reader->readFieldU16("terminationPositionCounter");
  vpu->terminationPositionValid =
    reader->readFieldBool("terminationPositionValid");
  vpu->endDelaySlotPending =
    reader->readFieldBool("endDelaySlotPending");
  vpu->branchDelaySlotPending =
    reader->readFieldBool("branchDelaySlotPending");
  vpu->pendingBranchTaken =
    reader->readFieldBool("pendingBranchTaken");
  vpu->pendingBranchTarget =
    reader->readFieldU16("pendingBranchTarget");
  vpu->pendingBranchLinkValid =
    reader->readFieldBool("pendingBranchLinkValid");
  vpu->pendingBranchLinkRegister =
    reader->readFieldU8("pendingBranchLinkRegister");
  vpu->pendingBranchLinkValue =
    reader->readFieldU16("pendingBranchLinkValue");
  vpu->terminationRequested =
    reader->readFieldBool("terminationRequested");
  vpu->haltAfterDrain =
    reader->readFieldBool("haltAfterDrain");
  vpu->dEnabled = reader->readFieldBool("dEnabled");
  vpu->tEnabled = reader->readFieldBool("tEnabled");
  vpu->xgkickWaiting =
    reader->readFieldBool("xgkickWaiting");
  vpu->xgkickTransferStarted =
    reader->readFieldBool("xgkickTransferStarted");
  vpu->dBitStop =
    reader->readFieldBool("dBitStop");
  vpu->tBitStop =
    reader->readFieldBool("tBitStop");
  vpu->forceBreakStop =
    reader->readFieldBool("forceBreakStop");
  vpu->cop2WriteInterlockReleased =
    reader->readFieldBool("cop2WriteInterlockReleased");

  const std::uint32_t fpRegisterCount =
    reader->readFieldU32("fpRegisterCount");
  require(
    fpRegisterCount == vpu->fpRegisters.size(),
    "VU floating-point register count is invalid");
  {
    auto registers = reader->scope("fpRegisters");
    for (std::size_t index = 0;
         index < vpu->fpRegisters.size();
         ++index)
    {
      auto element = reader->element(index);
      vpu->fpRegisters[index] = readFPRegister(reader);
    }
  }
  const std::uint32_t intRegisterCount =
    reader->readFieldU32("intRegisterCount");
  require(
    intRegisterCount == vpu->intRegisters.size(),
    "VU integer register count is invalid");
  {
    auto registers = reader->scope("intRegisters");
    for (std::size_t index = 0;
         index < vpu->intRegisters.size();
         ++index)
    {
      auto element = reader->element(index);
      vpu->intRegisters[index] = reader->readFieldU16("value");
    }
  }
  vpu->iRegister.setBits(reader->readFieldU32("iRegister"));
  vpu->qRegister.setBits(reader->readFieldU32("qRegister"));
  vpu->pRegister.setBits(reader->readFieldU32("pRegister"));
  vpu->rRegister = reader->readFieldU32("rRegister");
  vpu->cmsarRegister = reader->readFieldU16("cmsarRegister");
  vpu->MACFlags = reader->readFieldU16("macFlags");
  vpu->statusFlags = reader->readFieldU16("statusFlags");
  {
    auto accumulator = reader->scope("accumulator");
    vpu->accumulator = readFPRegister(reader);
  }
  vpu->clippingFlags = reader->readFieldU64("clippingFlags");
  readOrchestrator(
    reader,
    &vpu->orchestrator,
    pipelineLists);
  {
    auto field = reader->scope("virtualDestRegister");
    vpu->virtualDestRegister = readFPRegister(reader);
  }
  {
    auto field = reader->scope("accumulatorForwardValue");
    vpu->accumulatorForwardValue = readFPRegister(reader);
  }
  vpu->pendingAccumulatorWrites =
    reader->readFieldU8("pendingAccumulatorWrites");
  vpu->accumulatorForwardValid =
    reader->readFieldBool("accumulatorForwardValid");
  vpu->pendingLowerInstruction =
    readLowerInstruction(reader);
  vpu->pendingLowerInstructionAddress =
    reader->readFieldU16("pendingLowerInstructionAddress");
  vpu->lowerInstructionPending =
    reader->readFieldBool("lowerInstructionPending");
  vpu->pendingLowerInstructionReady =
    reader->readFieldBool("pendingLowerInstructionReady");
  vpu->pendingLowerWritebackDiscarded =
    reader->readFieldBool("pendingLowerWritebackDiscarded");
  {
    auto values = reader->scope("pendingIntegerWrites");
    for (std::size_t index = 0;
         index < vpu->pendingIntegerWrites.size();
         ++index)
    {
      auto element = reader->element(index);
      vpu->pendingIntegerWrites[index] =
        reader->readFieldU8("count");
    }
  }
  {
    auto values = reader->scope("pendingIALUWrites");
    for (std::size_t index = 0;
         index < vpu->pendingIALUWrites.size();
         ++index)
    {
      auto element = reader->element(index);
      vpu->pendingIALUWrites[index] =
        reader->readFieldU8("count");
    }
  }
  {
    auto values = reader->scope("bypassedIntegerValues");
    for (std::size_t index = 0;
         index < vpu->bypassedIntegerValues.size();
         ++index)
    {
      auto element = reader->element(index);
      vpu->bypassedIntegerValues[index] =
        reader->readFieldU16("value");
    }
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
  writer->writeFieldU8("type", pipeline.type);
  writer->writeFieldU16("opCode", pipeline.opCode);
  writer->writeFieldU32(
    "intResult",
    static_cast<std::uint32_t>(pipeline.intResult));
  {
    auto field = writer->scope("fpResult");
    writeFPRegister(writer, pipeline.fpResult);
  }
  {
    auto field = writer->scope("flagResult");
    writeFPRegister(writer, pipeline.flagResult);
  }
  {
    auto field = writer->scope("operationResult");
    writeFPRegister(writer, pipeline.operationResult);
  }
  {
    auto field = writer->scope("accumulatorValue");
    writeFPRegister(writer, pipeline.accumulatorValue);
  }
  {
    auto field = writer->scope("sourceValue1");
    writeFPRegister(writer, pipeline.sourceValue1);
  }
  {
    auto field = writer->scope("sourceValue2");
    writeFPRegister(writer, pipeline.sourceValue2);
  }
  writer->writeFieldU8(
    "ignoredResultFields",
    pipeline.ignoredResultFields);
  writer->writeFieldU8("srcReg1", pipeline.srcReg1);
  writer->writeFieldU8("srcReg2", pipeline.srcReg2);
  writer->writeFieldU8("destReg", pipeline.destReg);
  writer->writeFieldU8("integerDestReg", pipeline.integerDestReg);
  writer->writeFieldU8("destFieldMask", pipeline.destFieldMask);
  writer->writeFieldU8(
    "srcReg1FieldMask",
    pipeline.srcReg1FieldMask);
  writer->writeFieldU8(
    "srcReg2FieldMask",
    pipeline.srcReg2FieldMask);
  writer->writeFieldU16(
    "instructionAddress",
    pipeline.instructionAddress);
  writer->writeFieldU16("memoryAddress", pipeline.memoryAddress);
  writer->writeFieldU16(
    "immediate",
    static_cast<std::uint16_t>(pipeline.immediate));
  writer->writeFieldU32("immediateBits", pipeline.immediateBits);
  writer->writeFieldU32(
    "scalarResultBits",
    pipeline.scalarResultBits);
  writer->writeFieldU8(
    "scalarResultFlags",
    pipeline.scalarResultFlags);
  writer->writeFieldU16(
    "intSourceValue1",
    pipeline.intSourceValue1);
  writer->writeFieldU16(
    "intSourceValue2",
    pipeline.intSourceValue2);
  writer->writeFieldBool(
    "intSource1Sampled",
    pipeline.intSource1Sampled);
  writer->writeFieldBool(
    "intSource2Sampled",
    pipeline.intSource2Sampled);
  writer->writeFieldBool(
    "vectorSourcesSampled",
    pipeline.vectorSourcesSampled);
  writer->writeFieldBool("xgkickStarted", pipeline.xgkickStarted);
  writer->writeFieldBool(
    "discardWriteback",
    pipeline.writebackDisposition ==
      VUPipelineWritebackDisposition::Discard);
  writer->writeFieldU8(
    "currentStage",
    static_cast<std::uint8_t>(pipeline.currentStage));
  writer->writeFieldU8(
    "currentStageIndex",
    pipeline.currentStageIndex);
  writer->writeFieldU8(
    "executionStageCount",
    pipeline.executionStageCount);
  writer->writeFieldBool("complete", pipeline.complete);
}

void NekoSaveStateCodec::readPipeline(
  SaveStateReader *reader,
  Pipeline *pipeline)
{
  const auto require =
    [reader](bool condition, const std::string &detail)
    {
      reader->requireField(condition, detail);
    };
  pipeline->type = reader->readFieldU8("type");
  pipeline->opCode = reader->readFieldU16("opCode");
  pipeline->intResult =
    static_cast<std::int32_t>(reader->readFieldU32("intResult"));
  {
    auto field = reader->scope("fpResult");
    pipeline->fpResult = readFPRegister(reader);
  }
  {
    auto field = reader->scope("flagResult");
    pipeline->flagResult = readFPRegister(reader);
  }
  {
    auto field = reader->scope("operationResult");
    pipeline->operationResult = readFPRegister(reader);
  }
  {
    auto field = reader->scope("accumulatorValue");
    pipeline->accumulatorValue = readFPRegister(reader);
  }
  {
    auto field = reader->scope("sourceValue1");
    pipeline->sourceValue1 = readFPRegister(reader);
  }
  {
    auto field = reader->scope("sourceValue2");
    pipeline->sourceValue2 = readFPRegister(reader);
  }
  pipeline->ignoredResultFields =
    reader->readFieldU8("ignoredResultFields");
  pipeline->srcReg1 = reader->readFieldU8("srcReg1");
  pipeline->srcReg2 = reader->readFieldU8("srcReg2");
  pipeline->destReg = reader->readFieldU8("destReg");
  pipeline->integerDestReg =
    reader->readFieldU8("integerDestReg");
  pipeline->destFieldMask =
    reader->readFieldU8("destFieldMask");
  pipeline->srcReg1FieldMask =
    reader->readFieldU8("srcReg1FieldMask");
  pipeline->srcReg2FieldMask =
    reader->readFieldU8("srcReg2FieldMask");
  pipeline->instructionAddress =
    reader->readFieldU16("instructionAddress");
  pipeline->memoryAddress =
    reader->readFieldU16("memoryAddress");
  pipeline->immediate =
    static_cast<std::int16_t>(reader->readFieldU16("immediate"));
  pipeline->immediateBits =
    reader->readFieldU32("immediateBits");
  pipeline->scalarResultBits =
    reader->readFieldU32("scalarResultBits");
  pipeline->scalarResultFlags =
    reader->readFieldU8("scalarResultFlags");
  pipeline->intSourceValue1 =
    reader->readFieldU16("intSourceValue1");
  pipeline->intSourceValue2 =
    reader->readFieldU16("intSourceValue2");
  pipeline->intSource1Sampled =
    reader->readFieldBool("intSource1Sampled");
  pipeline->intSource2Sampled =
    reader->readFieldBool("intSource2Sampled");
  pipeline->vectorSourcesSampled =
    reader->readFieldBool("vectorSourcesSampled");
  pipeline->xgkickStarted =
    reader->readFieldBool("xgkickStarted");
  pipeline->writebackDisposition =
    reader->readFieldBool("discardWriteback")
      ? VUPipelineWritebackDisposition::Discard
      : VUPipelineWritebackDisposition::Commit;
  pipeline->currentStage = readEnum<VUPipelineStage>(
    reader,
    static_cast<std::uint8_t>(VUPipelineStage::P),
    "currentStage");
  pipeline->currentStageIndex =
    reader->readFieldU8("currentStageIndex");
  pipeline->executionStageCount =
    reader->readFieldU8("executionStageCount");
  pipeline->complete =
    reader->readFieldBool("complete");

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
  auto scope = writer->scope("orchestrator");
  writer->writeFieldBool("stalling", orchestrator.stalling);
  writer->writeFieldSize(
    "pipelineCount",
    orchestrator.pipelines.size());
  {
    auto pipelines = writer->scope("pipelines");
    for (std::size_t index = 0;
         index < orchestrator.pipelines.size();
         ++index)
    {
      auto element = writer->element(index);
      writePipeline(writer, orchestrator.pipelines[index]);
    }
  }
  const std::list<Pipeline *> *lists[] = {
    &orchestrator.executing,
    &orchestrator.waiting,
    &orchestrator.pool
  };
  const char *listNames[] = {
    "executing",
    "waiting",
    "pool"
  };
  for (std::size_t listIndex = 0; listIndex < 3; ++listIndex)
  {
    auto listScope = writer->scope(listNames[listIndex]);
    writer->writeFieldSize("count", lists[listIndex]->size());
    std::size_t elementIndex = 0;
    for (const Pipeline *pipeline : *lists[listIndex])
    {
      auto element = writer->element(elementIndex++);
      writer->writeFieldU8("pipelineIndex", pipelineIndex(
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
  const auto require =
    [reader](bool condition, const std::string &detail)
    {
      reader->requireField(condition, detail);
    };
  auto scope = reader->scope("orchestrator");
  orchestrator->stalling =
    reader->readFieldBool("stalling");
  const std::uint32_t pipelineCount =
    reader->readFieldU32("pipelineCount");
  require(
    pipelineCount == orchestrator->pipelines.size(),
    "VU pipeline-array size is invalid");
  {
    auto pipelines = reader->scope("pipelines");
    for (std::size_t index = 0;
         index < orchestrator->pipelines.size();
         ++index)
    {
      auto element = reader->element(index);
      readPipeline(reader, &orchestrator->pipelines[index]);
    }
  }

  std::array<bool, MAX_PIPELINES> used = {};
  for (std::vector<std::uint8_t> &list : *pipelineLists)
  {
    list.clear();
  }
  std::size_t membershipCount = 0;
  const char *listNames[] = {
    "executing",
    "waiting",
    "pool"
  };
  for (std::size_t listIndex = 0; listIndex < 3; ++listIndex)
  {
    auto listScope = reader->scope(listNames[listIndex]);
    std::vector<std::uint8_t> &list =
      (*pipelineLists)[listIndex];
    const std::uint32_t count = reader->readFieldU32("count");
    require(count <= MAX_PIPELINES, "VU pipeline-list size is invalid");
    list.reserve(count);
    membershipCount += count;
    for (std::uint32_t index = 0; index < count; ++index)
    {
      auto element = reader->element(index);
      const std::uint8_t pipeline =
        reader->readFieldU8("pipelineIndex");
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
  writer->writeFieldU8("type", static_cast<std::uint8_t>(vif.type));
  writer->writeFieldU16("cycleRegister", vif.cycleRegister);
  writer->writeFieldU8("modeRegister", vif.modeRegister);
  writer->writeFieldU32("maskRegister", vif.maskRegister);
  {
    auto registers = writer->scope("rowRegisters");
    for (std::size_t index = 0;
         index < vif.rowRegisters.size();
         ++index)
    {
      auto element = writer->element(index);
      writer->writeFieldU32("value", vif.rowRegisters[index]);
    }
  }
  {
    auto registers = writer->scope("columnRegisters");
    for (std::size_t index = 0;
         index < vif.columnRegisters.size();
         ++index)
    {
      auto element = writer->element(index);
      writer->writeFieldU32("value", vif.columnRegisters[index]);
    }
  }
  writer->writeFieldU16("topRegister", vif.topRegister);
  writer->writeFieldU16("itopRegister", vif.itopRegister);
  writer->writeFieldU16("itopsRegister", vif.itopsRegister);
  writer->writeFieldU16("baseRegister", vif.baseRegister);
  writer->writeFieldU16("offsetRegister", vif.offsetRegister);
  writer->writeFieldU16("topsRegister", vif.topsRegister);
  writer->writeFieldU16("markRegister", vif.markRegister);
  writer->writeFieldBool("dbf", vif.dbf);
  writer->writeFieldBool("path3Mask", vif.path3Mask);
  writer->writeFieldBool("markFlag", vif.markFlag);
  writer->writeFieldBool("interruptFlag", vif.interruptFlag);
  writer->writeFieldU32("codeRegister", vif.codeRegister);
  writeVIFCommand(writer, vif.streamCommand);
  writer->writeFieldU32(
    "streamPayloadWordCount",
    vif.streamPayloadWordCount);
  writer->writeFieldU32(
    "streamPayloadWordsRemaining",
    vif.streamPayloadWordsRemaining);
  writer->writeFieldU64(
    "streamWordsIngested",
    vif.streamWordsIngested);
  writer->writeFieldU32(
    "mpgLowerInstruction",
    vif.mpgLowerInstruction);
  writer->writeFieldBool(
    "mpgLowerInstructionPending",
    vif.mpgLowerInstructionPending);
  {
    auto values = writer->scope("directQuadword");
    for (std::size_t index = 0;
         index < vif.directQuadword.size();
         ++index)
    {
      auto element = writer->element(index);
      writer->writeFieldU32("value", vif.directQuadword[index]);
    }
  }
  writer->writeFieldSize("unpackPayloadCount", vif.unpackPayload.size());
  {
    auto values = writer->scope("unpackPayload");
    for (std::size_t index = 0;
         index < vif.unpackPayload.size();
         ++index)
    {
      auto element = writer->element(index);
      writer->writeFieldU32("value", vif.unpackPayload[index]);
    }
  }
  writer->writeFieldSize("fifoWordCount", vif.fifoWords.size());
  {
    auto words = writer->scope("fifoWords");
    std::size_t index = 0;
    for (std::uint32_t value : vif.fifoWords)
    {
      auto element = writer->element(index++);
      writer->writeFieldU32("value", value);
    }
  }
}

void NekoSaveStateCodec::readVIF(
  SaveStateReader *reader,
  VIF *vif)
{
  const auto require =
    [reader](bool condition, const std::string &detail)
    {
      reader->requireField(condition, detail);
    };
  const VIFType type = readEnum<VIFType>(
    reader,
    static_cast<std::uint8_t>(VIFType::VIF1),
    "type");
  require(type == vif->type, "VIF type does not match its slot");
  vif->cycleRegister = reader->readFieldU16("cycleRegister");
  vif->modeRegister = reader->readFieldU8("modeRegister");
  vif->maskRegister = reader->readFieldU32("maskRegister");
  {
    auto registers = reader->scope("rowRegisters");
    for (std::size_t index = 0;
         index < vif->rowRegisters.size();
         ++index)
    {
      auto element = reader->element(index);
      vif->rowRegisters[index] = reader->readFieldU32("value");
    }
  }
  {
    auto registers = reader->scope("columnRegisters");
    for (std::size_t index = 0;
         index < vif->columnRegisters.size();
         ++index)
    {
      auto element = reader->element(index);
      vif->columnRegisters[index] =
        reader->readFieldU32("value");
    }
  }
  vif->topRegister = reader->readFieldU16("topRegister");
  vif->itopRegister = reader->readFieldU16("itopRegister");
  vif->itopsRegister = reader->readFieldU16("itopsRegister");
  vif->baseRegister = reader->readFieldU16("baseRegister");
  vif->offsetRegister = reader->readFieldU16("offsetRegister");
  vif->topsRegister = reader->readFieldU16("topsRegister");
  vif->markRegister = reader->readFieldU16("markRegister");
  vif->dbf = reader->readFieldBool("dbf");
  vif->path3Mask = reader->readFieldBool("path3Mask");
  vif->markFlag = reader->readFieldBool("markFlag");
  vif->interruptFlag = reader->readFieldBool("interruptFlag");
  vif->codeRegister = reader->readFieldU32("codeRegister");
  vif->streamCommand = readVIFCommand(reader);
  vif->streamPayloadWordCount =
    reader->readFieldU32("streamPayloadWordCount");
  vif->streamPayloadWordsRemaining =
    reader->readFieldU32("streamPayloadWordsRemaining");
  vif->streamWordsIngested =
    reader->readFieldU64("streamWordsIngested");
  vif->mpgLowerInstruction =
    reader->readFieldU32("mpgLowerInstruction");
  vif->mpgLowerInstructionPending =
    reader->readFieldBool("mpgLowerInstructionPending");
  {
    auto values = reader->scope("directQuadword");
    for (std::size_t index = 0;
         index < vif->directQuadword.size();
         ++index)
    {
      auto element = reader->element(index);
      vif->directQuadword[index] = reader->readFieldU32("value");
    }
  }
  const std::uint32_t unpackCount =
    reader->readFieldU32("unpackPayloadCount");
  require(
    unpackCount <= MAX_VIF_UNPACK_WORDS,
    "VIF UNPACK payload size is invalid");
  std::vector<std::uint32_t> unpackPayload;
  unpackPayload.reserve(unpackCount);
  {
    auto values = reader->scope("unpackPayload");
    for (std::uint32_t index = 0;
         index < unpackCount;
         ++index)
    {
      auto element = reader->element(index);
      unpackPayload.push_back(reader->readFieldU32("value"));
    }
  }
  vif->unpackPayload.swap(unpackPayload);
  const std::uint32_t fifoWordCount =
    reader->readFieldU32("fifoWordCount");
  require(
    fifoWordCount <= vif->fifoCapacity() * 4,
    "VIF FIFO size is invalid");
  std::deque<std::uint32_t> fifoWords;
  {
    auto words = reader->scope("fifoWords");
    for (std::uint32_t index = 0;
         index < fifoWordCount;
         ++index)
    {
      auto element = reader->element(index);
      fifoWords.push_back(reader->readFieldU32("value"));
    }
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
