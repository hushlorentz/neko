#include "save_state_internal.hpp"

#include "floating_point_ops.hpp"

static_assert(
  static_cast<std::uint8_t>(
    EEOperation::DivideSingleCOP1) == 141,
  "Version-26 DIV.S save-state ordinal changed.");
static_assert(
  static_cast<std::uint8_t>(
    EEOperation::SquareRootSingleCOP1) == 142,
  "Version-26 SQRT.S save-state ordinal changed.");
static_assert(
  static_cast<std::uint8_t>(
    EEOperation::ReciprocalSquareRootSingleCOP1) == 143,
  "Version-26 RSQRT.S save-state ordinal changed.");

void NekoSaveStateCodec::writeEECore(
  SaveStateWriter *writer,
  const EECore &core)
{
  for (const EERegister128 &value : core.generalRegisters)
  {
    writer->writeU64(value.low);
    writer->writeU64(value.high);
  }
  for (const std::uint32_t value : core.floatingPointRegisters)
  {
    writer->writeU32(value);
  }
  writer->writeU32(core.floatingPointAccumulatorRegister);
  writer->writeU32(core.cop1StatusRegister);
  writer->writeU32(core.pc);
  writer->writeU64(core.hiRegister);
  writer->writeU64(core.loRegister);
  writer->writeU64(core.hi1Register);
  writer->writeU64(core.lo1Register);
  writer->writeU32(core.saRegister);
  writer->writeU32(core.cop0BadVAddr);
  writer->writeU32(core.cop0Count);
  writer->writeU32(core.cop0Compare);
  writer->writeU32(core.cop0Status);
  writer->writeU32(core.cop0Cause);
  writer->writeU32(core.cop0EPC);
  writer->writeU32(core.cop0ErrorEPC);
  writer->writeU8(
    static_cast<std::uint8_t>(core.exception));
  writer->writeU32(core.faultAddress);
  writer->writeU8(
    static_cast<std::uint8_t>(core.state));
  writer->writeU8(
    static_cast<std::uint8_t>(core.haltReason));
  writer->writeU64(core.cycles);
  writer->writeBool(core.lastInstructionValid);
  writer->writeU32(core.lastAddress);
  writer->writeU32(core.lastDecodedInstruction.raw);
  writer->writeU32(core.rejectedInstructionValue);
  const auto writePending =
    [writer](const EECore::PendingMultiplyDivide &operation)
    {
      writer->writeBool(operation.active);
      writer->writeU8(operation.remainingCycles);
      writer->writeU64(operation.hiResult);
      writer->writeU64(operation.loResult);
      writer->writeBool(
        operation.resultDestination ==
          EECore::MACResultDestination::HIAndLOAndGPR);
      writer->writeU8(operation.generalRegister);
      writer->writeU64(operation.generalRegisterResult);
    };
  writePending(core.pendingMac0);
  writePending(core.pendingMac1);
  writer->writeU8(
    static_cast<std::uint8_t>(
      core.issueLatch.failure));
  writer->writeBool(core.stagingLatch.valid);
  writer->writeU8(
    static_cast<std::uint8_t>(
      core.stagingLatch.failure));
  writer->writeU32(core.stagingLatch.address);
  writer->writeU32(core.stagingLatch.instruction.raw);
  writer->writeBool(
    core.youngerAStageContinuation.active);
  writer->writeU32(
    core.youngerAStageContinuation.instruction.raw);
  writer->writeU32(
    core.youngerAStageContinuation.address);
  for (std::size_t index = 0; index < 4; ++index)
  {
    writer->writeU8(0);
  }
  const EECore::COP1DividerOccupancy dividerOccupancy =
    core.derivedCOP1DividerOccupancy();
  writer->writeU8(dividerOccupancy.initiationCycles);
  writer->writeU8(
    static_cast<std::uint8_t>(
      dividerOccupancy.operation));
  writer->writeBool(false);
  writer->writeU8(
    core.shiftAmountOrdering.accessHistory());
  writer->writeU8(
    core.shiftAmountOrdering.readHistory());
  writer->writeBool(core.branchDelayPending);
  writer->writeU32(core.branchDelayTarget);
  writer->writeU32(core.branchInstructionAddress);
  writer->writeBool(core.branchDelayFromLikely);
  writer->writeBool(core.branchDelayTaken);
  writer->writeU8(core.cop1DividerPostDelayInstructions);
  writer->writeU32(core.cop1DividerPostDelayBranchAddress);
  writer->writeU32(core.cop1DividerPostDelayTargetAddress);
  writer->writeBool(core.cop1DividerPostDelayTaken);
  writer->writeU8(core.cop1DividerPostTargetInstructions);
  writer->writeU32(core.cop1DividerPostTargetAddress);
  writer->writeBool(core.issueLatch.valid);
  writer->writeU32(core.issueLatch.address);
  writer->writeU32(core.issueLatch.instruction.raw);
  writer->writeU64(core.nextEEProgramOrder);
  for (const EECore::InFlightCOP1Operation &operation :
       core.inFlightCOP1Operations)
  {
    const bool memoryOperation =
      operation.active &&
      isCOP1MemoryMoveOperation(
        operation.instruction.operation);
    writer->writeBool(operation.active);
    writer->writeU64(operation.programOrder);
    writer->writeU8(
      static_cast<std::uint8_t>(operation.stage));
    writer->writeU32(operation.instructionAddress);
    writer->writeU32(operation.instruction.raw);
    writer->writeU32(operation.capturedFS);
    writer->writeU32(operation.capturedFT);
    writer->writeU32(operation.capturedAccumulator);
    // Memory operations use otherwise-unused result slots for
    // branch-delay fault provenance without changing version-24 layout.
    writer->writeU32(
      memoryOperation
        ? operation.branchAddress
        : operation.capturedControl);
    writer->writeU64(operation.capturedGPR);
    writer->writeU32(operation.memoryAddress);
    writer->writeU32(operation.capturedMemoryValue);
    writer->writeU8(operation.destination.mask);
    writer->writeU8(operation.destination.fprRegister);
    writer->writeU8(operation.destination.gprRegister);
    writer->writeU32(operation.rawResult);
    writer->writeU8(operation.affectedFlags);
    writer->writeU8(operation.raisedFlags);
    writer->writeU8(operation.raisedStickyFlags);
    writer->writeBool(
      memoryOperation
        ? operation.branchDelaySlot
        : operation.conditionResult);
    writer->writeU8(operation.remainingCycles);
  }
  writer->writeU8(
    core.packedMACContinuation.initiationCycles);
  const EECore::PackedMACProgramOrderView packedOrder =
    core.packedMACProgramOrder();
  for (std::size_t orderIndex = 0;
       orderIndex < EECore::PackedMACContinuation::CAPACITY;
       ++orderIndex)
  {
    EECore::InFlightPackedMACOperation operation;
    if (orderIndex < packedOrder.size())
    {
      operation =
        core.packedMACContinuation.operations[
          packedOrder[orderIndex]];
    }
    writer->writeBool(operation.active);
    writer->writeU8(
      static_cast<std::uint8_t>(operation.operation));
    writer->writeU64(operation.programOrder);
    writer->writeU64(operation.source.low);
    writer->writeU64(operation.source.high);
    writer->writeU64(operation.target.low);
    writer->writeU64(operation.target.high);
    writer->writeU64(operation.hiResult.low);
    writer->writeU64(operation.hiResult.high);
    writer->writeU64(operation.loResult.low);
    writer->writeU64(operation.loResult.high);
    writer->writeU8(operation.destinationRegister);
    writer->writeU64(
      operation.generalRegisterResult.low);
    writer->writeU64(
      operation.generalRegisterResult.high);
    writer->writeU8(operation.remainingCycles);
  }
  writer->writeBool(
    core.packedDivideContinuation.active);
  writer->writeU8(
    static_cast<std::uint8_t>(
      core.packedDivideContinuation.operation));
  writer->writeU64(
    core.packedDivideContinuation.programOrder);
  writer->writeU64(
    core.packedDivideContinuation.source.low);
  writer->writeU64(
    core.packedDivideContinuation.source.high);
  writer->writeU64(
    core.packedDivideContinuation.target.low);
  writer->writeU64(
    core.packedDivideContinuation.target.high);
  writer->writeU64(
    core.packedDivideContinuation.hiResult.low);
  writer->writeU64(
    core.packedDivideContinuation.hiResult.high);
  writer->writeU64(
    core.packedDivideContinuation.loResult.low);
  writer->writeU64(
    core.packedDivideContinuation.loResult.high);
  writer->writeU8(
    core.packedDivideContinuation.remainingCycles);
}

void NekoSaveStateCodec::readEECore(
  SaveStateReader *reader,
  EECore *core,
  EECore::COP1DividerOccupancy *dividerOccupancy)
{
  for (EERegister128 &value : core->generalRegisters)
  {
    value.low = reader->readU64();
    value.high = reader->readU64();
  }
  for (std::uint32_t &value : core->floatingPointRegisters)
  {
    value = reader->readU32();
  }
  core->floatingPointAccumulatorRegister = reader->readU32();
  core->cop1StatusRegister = reader->readU32();
  core->pc = reader->readU32();
  core->hiRegister = reader->readU64();
  core->loRegister = reader->readU64();
  core->hi1Register = reader->readU64();
  core->lo1Register = reader->readU64();
  core->saRegister = reader->readU32();
  core->cop0BadVAddr = reader->readU32();
  core->cop0Count = reader->readU32();
  core->cop0Compare = reader->readU32();
  core->cop0Status = reader->readU32();
  core->cop0Cause = reader->readU32();
  core->cop0EPC = reader->readU32();
  core->cop0ErrorEPC = reader->readU32();
  core->exception = readEnum<EEException>(
    reader,
    static_cast<std::uint8_t>(
      EEException::CoprocessorUnusable),
    "EE exception");
  core->faultAddress = reader->readU32();
  core->state = readEnum<EEExecutionState>(
    reader,
    static_cast<std::uint8_t>(
      EEExecutionState::Running),
    "EE execution state");
  core->haltReason = readEnum<EEStopReason>(
    reader,
    static_cast<std::uint8_t>(
      EEStopReason::UndefinedOperation),
    "EE stop reason");
  core->cycles = reader->readU64();
  core->lastInstructionValid =
    reader->readBool("EE last instruction flag");
  core->lastAddress = reader->readU32();
  const std::uint32_t lastInstruction = reader->readU32();
  core->rejectedInstructionValue = reader->readU32();
  const auto readPending =
    [reader](
      EECore::PendingMultiplyDivide *operation,
      const char *label)
    {
      operation->active = reader->readBool(label);
      operation->remainingCycles = reader->readU8();
      operation->hiResult = reader->readU64();
      operation->loResult = reader->readU64();
      operation->resultDestination =
        reader->readBool(label)
          ? EECore::MACResultDestination::HIAndLOAndGPR
          : EECore::MACResultDestination::HIAndLO;
      operation->generalRegister = reader->readU8();
      operation->generalRegisterResult = reader->readU64();
      require(
        EECore::pendingMultiplyDivideLatencyValid(*operation),
        "EE pending multiply/divide latency is invalid");
      require(
        EECore::pendingMultiplyDivideRegisterValid(*operation),
        "EE pending multiply/divide register is invalid");
      require(
        EECore::pendingMultiplyDivideDestinationValid(*operation),
        "EE pending divide contains a destination register");
    };
  readPending(
    &core->pendingMac0,
    "EE MAC0 pending flag");
  readPending(
    &core->pendingMac1,
    "EE MAC1 pending flag");
  if (core->pendingMac0.active &&
      core->pendingMac1.active)
  {
    require(
      core->concurrentMultiplyDivideExecutionStateValid(),
      "EE concurrent multiply/divide execution state is invalid");
    require(
      core->concurrentMultiplyDivideLatenciesValid(),
      "EE concurrent multiply/divide latencies are invalid");
    require(
      core->concurrentMultiplyDestinationsValid(),
      "EE concurrent multiply destinations conflict");
  }
  core->issueLatch.failure =
    readEnum<EECore::IssueLatchFailure>(
      reader,
      static_cast<std::uint8_t>(
        EECore::IssueLatchFailure::UnsupportedInstruction),
      "EE decoded issue-latch failure");
  core->stagingLatch.valid =
    reader->readBool("EE staging-latch flag");
  core->stagingLatch.failure =
    readEnum<EECore::IssueLatchFailure>(
      reader,
      static_cast<std::uint8_t>(
        EECore::IssueLatchFailure::UnsupportedInstruction),
      "EE staging-latch failure");
  core->stagingLatch.address = reader->readU32();
  const std::uint32_t stagingInstruction =
    reader->readU32();
  const bool youngerAStageActive =
    reader->readBool("EE younger A-stage continuation flag");
  const std::uint32_t youngerAStageInstruction =
    reader->readU32();
  const std::uint32_t youngerAStageAddress =
    reader->readU32();
  for (std::size_t index = 0; index < 4; ++index)
  {
    require(
      reader->readU8() == 0,
      "EE reserved front-end state is not empty");
  }
  dividerOccupancy->initiationCycles = reader->readU8();
  dividerOccupancy->operation =
    static_cast<EEOperation>(reader->readU8());
  require(
    EECore::cop1DividerInitiationIntervalValid(
      *dividerOccupancy),
    "EE COP1 divider initiation interval is invalid");
  require(
    EECore::cop1DividerOperationPresenceValid(
      *dividerOccupancy),
    "EE unoccupied COP1 divider names an operation");
  require(
    EECore::cop1DividerOperationFamilyValid(
      *dividerOccupancy),
    "EE COP1 divider operation state is inconsistent");
  const bool retiredCOP1OperateResource =
    reader->readBool("retired EE COP1 operate resource flag");
  require(
    !retiredCOP1OperateResource,
    "retired EE COP1 operate resource state is not empty");
  const std::uint8_t recentShiftAmountAccesses =
    reader->readU8();
  const std::uint8_t recentShiftAmountReads =
    reader->readU8();
  require(
    EEShiftAmountOrderingWindow::historyBitsValid(
      recentShiftAmountAccesses,
      recentShiftAmountReads),
    "EE shift-amount ordering history is invalid");
  require(
    EEShiftAmountOrderingWindow::readHistoryConsistent(
      recentShiftAmountAccesses,
      recentShiftAmountReads),
    "EE shift-amount read history is inconsistent");
  core->shiftAmountOrdering.restore(
    recentShiftAmountAccesses,
    recentShiftAmountReads);
  core->branchDelayPending =
    reader->readBool("EE branch delay flag");
  core->branchDelayTarget = reader->readU32();
  core->branchInstructionAddress = reader->readU32();
  core->branchDelayFromLikely =
    reader->readBool("EE branch-likely delay flag");
  core->branchDelayTaken =
    reader->readBool("EE branch taken flag");
  core->cop1DividerPostDelayInstructions = reader->readU8();
  core->cop1DividerPostDelayBranchAddress = reader->readU32();
  core->cop1DividerPostDelayTargetAddress = reader->readU32();
  core->cop1DividerPostDelayTaken =
    reader->readBool("EE COP1 post-delay taken flag");
  core->cop1DividerPostTargetInstructions = reader->readU8();
  core->cop1DividerPostTargetAddress = reader->readU32();
  core->issueLatch.valid =
    reader->readBool("EE decoded issue-latch flag");
  core->issueLatch.address = reader->readU32();
  const std::uint32_t issueInstruction = reader->readU32();
  core->nextEEProgramOrder = reader->readU64();
  require(
    core->nextEEProgramOrder != 0,
    "EE program-order counter is invalid");
  core->executingProgramOrder = 0;
  for (EECore::InFlightCOP1Operation &operation :
       core->inFlightCOP1Operations)
  {
    operation = {};
    operation.active =
      reader->readBool("EE in-flight COP1 operation flag");
    operation.programOrder = reader->readU64();
    operation.stage =
      readEnum<EECore::COP1PipelineStage>(
        reader,
        static_cast<std::uint8_t>(
          EECore::COP1PipelineStage::S2),
        "EE COP1 pipeline stage");
    operation.instructionAddress = reader->readU32();
    const std::uint32_t instruction = reader->readU32();
    operation.capturedFS = reader->readU32();
    operation.capturedFT = reader->readU32();
    operation.capturedAccumulator = reader->readU32();
    const std::uint32_t serializedControl =
      reader->readU32();
    operation.capturedGPR = reader->readU64();
    operation.memoryAddress = reader->readU32();
    operation.capturedMemoryValue = reader->readU32();
    operation.destination.mask = reader->readU8();
    operation.destination.fprRegister = reader->readU8();
    operation.destination.gprRegister = reader->readU8();
    operation.rawResult = reader->readU32();
    operation.affectedFlags = reader->readU8();
    operation.raisedFlags = reader->readU8();
    operation.raisedStickyFlags = reader->readU8();
    const bool serializedCondition =
      reader->readBool("EE COP1 condition result");
    operation.remainingCycles = reader->readU8();

    constexpr std::uint8_t destinationMask =
      EECore::COP1_DESTINATION_FPR |
      EECore::COP1_DESTINATION_ACCUMULATOR |
      EECore::COP1_DESTINATION_FCR31 |
      EECore::COP1_DESTINATION_CONDITION |
      EECore::COP1_DESTINATION_GPR |
      EECore::COP1_DESTINATION_MEMORY;
    constexpr std::uint8_t supportedFlags =
      FP_FLAG_I_BIT |
      FP_FLAG_D_BIT |
      FP_FLAG_OVERFLOW |
      FP_FLAG_UNDERFLOW;
    require(
      (operation.destination.mask & ~destinationMask) == 0,
      "EE COP1 destination mask is invalid");
    require(
      operation.destination.fprRegister <
        EECore::FLOATING_POINT_REGISTER_COUNT,
      "EE COP1 destination FPR is invalid");
    require(
      operation.destination.gprRegister <
        EECore::GENERAL_REGISTER_COUNT,
      "EE COP1 destination GPR is invalid");
    require(
      (operation.affectedFlags & ~supportedFlags) == 0 &&
        (operation.raisedFlags &
         ~operation.affectedFlags) == 0 &&
        (operation.raisedStickyFlags &
         ~operation.affectedFlags) == 0,
      "EE COP1 result flags are invalid");

    operation.instruction = {};
    if (operation.active)
    {
      operation.instruction =
        decodeEEInstruction(instruction);
      require(
        core->cop1ProgramOrderInRange(operation),
        "EE COP1 program order is invalid");
      require(
        (operation.instructionAddress & 3) == 0,
        "EE COP1 instruction address is invalid");
      require(
        isCOP1ManagedPipelineOperation(
          operation.instruction.operation),
        "EE in-flight operation is not managed by the C1 pipeline");
      require(
        (operation.destination.mask &
         EECore::COP1_DESTINATION_FPR) != 0 ||
          operation.destination.fprRegister == 0,
        "EE COP1 result names an unused FPR destination");
      require(
        (operation.destination.mask &
         EECore::COP1_DESTINATION_GPR) != 0 ||
          operation.destination.gprRegister == 0,
        "EE COP1 result names an unused GPR destination");
      const bool memoryOperation =
        isCOP1MemoryMoveOperation(
          operation.instruction.operation);
      if (memoryOperation)
      {
        operation.branchAddress = serializedControl;
        operation.branchDelaySlot = serializedCondition;
      }
      else
      {
        operation.capturedControl = serializedControl;
        operation.conditionResult = serializedCondition;
      }
      require(
        memoryOperation ||
          (operation.memoryAddress == 0 &&
           operation.capturedMemoryValue == 0),
        "EE COP1 captured memory state is invalid");
      require(
        (operation.destination.mask &
         EECore::COP1_DESTINATION_MEMORY) == 0 ||
          operation.instruction.operation ==
            EEOperation::StoreWordFromCOP1,
        "EE COP1 memory destination is invalid");
      require(
        (operation.destination.mask &
         EECore::COP1_DESTINATION_CONDITION) != 0 ||
          !operation.conditionResult,
        "EE COP1 result contains an unused condition value");
      const bool load =
        memoryOperation &&
        isLoadOperation(operation.instruction.operation);
      const bool registerMove =
        isCOP1RegisterMoveOperation(
          operation.instruction.operation);
      const bool divider =
        isCOP1DividerOperation(
          operation.instruction.operation);
      const bool stagedOperation =
        isCOP1StagedOperation(
          operation.instruction.operation);
      require(
        divider || operation.remainingCycles == 0,
        "EE COP1 operation has an unexpected countdown");
      if (memoryOperation)
      {
        const bool addressReady =
          operation.stage != EECore::COP1PipelineStage::R;
        const bool transferReady =
          operation.stage >= EECore::COP1PipelineStage::X;
        const std::uint32_t expectedAddress =
          static_cast<std::uint32_t>(
            operation.capturedGPR +
            static_cast<std::int16_t>(
              operation.instruction.immediate));
        require(
          operation.stage <= EECore::COP1PipelineStage::Y &&
            operation.destination.mask ==
              (load
                ? EECore::COP1_DESTINATION_FPR
                : EECore::COP1_DESTINATION_MEMORY) &&
            operation.destination.fprRegister ==
              (load
                ? operation.instruction.targetRegister
                : 0) &&
            operation.destination.gprRegister == 0 &&
            operation.capturedFS == 0 &&
            operation.capturedFT == 0 &&
            operation.capturedAccumulator == 0 &&
            operation.capturedControl == 0 &&
            operation.affectedFlags == 0 &&
            operation.raisedFlags == 0 &&
            operation.raisedStickyFlags == 0 &&
            !operation.conditionResult &&
            (operation.branchDelaySlot
              ? (operation.branchAddress & 3) == 0 &&
                operation.instructionAddress ==
                  operation.branchAddress + 4
              : operation.branchAddress == 0) &&
            (addressReady
              ? operation.memoryAddress == expectedAddress
              : operation.memoryAddress == 0) &&
            (operation.stage < EECore::COP1PipelineStage::X ||
             (operation.memoryAddress & 3) == 0) &&
            (transferReady
              ? operation.rawResult ==
                  operation.capturedMemoryValue
              : (operation.rawResult == 0 &&
                 operation.capturedMemoryValue == 0)),
          "EE in-flight COP1 memory operation is inconsistent");
      }
      if (registerMove)
      {
        const bool cop1SourceCaptured =
          operation.stage != EECore::COP1PipelineStage::R;
        const bool resultComputed =
          operation.stage == EECore::COP1PipelineStage::Y;
        std::uint8_t expectedDestination =
          EECore::COP1_DESTINATION_NONE;
        std::uint8_t expectedFPR = 0;
        std::uint8_t expectedGPR = 0;
        std::uint32_t expectedResult = 0;
        bool usesFPRSource = false;
        bool usesControlSource = false;
        bool usesGPRSource = false;
        switch (operation.instruction.operation)
        {
          case EEOperation::MoveWordFromCOP1:
            expectedDestination =
              EECore::COP1_DESTINATION_GPR;
            expectedGPR =
              operation.instruction.targetRegister;
            usesFPRSource = true;
            expectedResult = operation.capturedFS;
            break;
          case EEOperation::MoveWordToCOP1:
            expectedDestination =
              EECore::COP1_DESTINATION_FPR;
            expectedFPR =
              operation.instruction.destinationRegister;
            usesGPRSource = true;
            expectedResult =
              static_cast<std::uint32_t>(
                operation.capturedGPR);
            break;
          case EEOperation::MoveControlWordFromCOP1:
            expectedDestination =
              EECore::COP1_DESTINATION_GPR;
            expectedGPR =
              operation.instruction.targetRegister;
            usesControlSource = true;
            expectedResult = operation.capturedControl;
            break;
          case EEOperation::MoveControlWordToCOP1:
            if (operation.instruction.destinationRegister ==
                EECOP1Control::STATUS_REGISTER)
            {
              expectedDestination =
                EECore::COP1_DESTINATION_FCR31;
            }
            usesGPRSource = true;
            expectedResult =
              static_cast<std::uint32_t>(
                operation.capturedGPR);
            break;
          case EEOperation::MoveSingleCOP1:
            expectedDestination =
              EECore::COP1_DESTINATION_FPR;
            expectedFPR =
              operation.instruction.shiftAmount;
            usesFPRSource = true;
            expectedResult = operation.capturedFS;
            break;
          default:
            break;
        }
        require(
          operation.stage <= EECore::COP1PipelineStage::Y &&
            operation.destination.mask == expectedDestination &&
            operation.destination.fprRegister == expectedFPR &&
            operation.destination.gprRegister == expectedGPR &&
            operation.capturedFT == 0 &&
            operation.capturedAccumulator == 0 &&
            operation.memoryAddress == 0 &&
            operation.capturedMemoryValue == 0 &&
            operation.affectedFlags == 0 &&
            operation.raisedFlags == 0 &&
            operation.raisedStickyFlags == 0 &&
            !operation.conditionResult &&
            (usesFPRSource
              ? (cop1SourceCaptured ||
                 operation.capturedFS == 0)
              : operation.capturedFS == 0) &&
            (usesControlSource
              ? (cop1SourceCaptured ||
                 operation.capturedControl == 0)
              : operation.capturedControl == 0) &&
            (usesGPRSource ||
             operation.capturedGPR == 0) &&
            (resultComputed
              ? operation.rawResult == expectedResult
              : operation.rawResult == 0),
          "EE in-flight COP1 register move state is inconsistent");
      }
      if (divider)
      {
        const EECOP1DividerTiming timing =
          cop1DividerTiming(
            operation.instruction.operation);
        EEFloatResult expectedResult;
        switch (operation.instruction.operation)
        {
          case EEOperation::DivideSingleCOP1:
            expectedResult =
              divEEFloatRaw(
                operation.capturedFS,
                operation.capturedFT);
            break;
          case EEOperation::SquareRootSingleCOP1:
            expectedResult =
              sqrtEEFloatRaw(operation.capturedFT);
            break;
          default:
            expectedResult =
              rsqrtEEFloatRaw(
                operation.capturedFS,
                operation.capturedFT);
            break;
        }
        const bool waitingForResult =
          operation.stage ==
            EECore::COP1PipelineStage::R &&
          operation.remainingCycles >= 1 &&
          operation.remainingCycles <= timing.latency;
        const bool canonicalOperands =
          operation.instruction.operation !=
              EEOperation::SquareRootSingleCOP1 ||
          operation.capturedFS == 0;
        require(
          waitingForResult &&
            canonicalOperands &&
            operation.destination.mask ==
              (EECore::COP1_DESTINATION_FPR |
               EECore::COP1_DESTINATION_FCR31) &&
            operation.destination.fprRegister ==
              operation.instruction.shiftAmount &&
            operation.rawResult == expectedResult.bits &&
            operation.affectedFlags ==
              (FP_FLAG_I_BIT | FP_FLAG_D_BIT) &&
            operation.raisedFlags == expectedResult.flags &&
            operation.raisedStickyFlags == 0,
          "EE in-flight COP1 divider state is inconsistent");
      }
      if (stagedOperation)
      {
        const bool operandsCaptured =
          operation.stage != EECore::COP1PipelineStage::R;
        const bool resultComputed =
          operation.stage == EECore::COP1PipelineStage::Z ||
          operation.stage == EECore::COP1PipelineStage::S1;
        std::uint32_t expectedResult = 0;
        std::uint8_t expectedFlags = 0;
        std::uint8_t expectedStickyFlags = 0;
        bool expectedCondition = false;
        if (resultComputed)
        {
          switch (operation.instruction.operation)
          {
            case EEOperation::AbsoluteSingleCOP1:
              expectedResult =
                operation.capturedFS & UINT32_C(0x7fffffff);
              break;
            case EEOperation::NegateSingleCOP1:
              expectedResult =
                operation.capturedFS ^ UINT32_C(0x80000000);
              break;
            case EEOperation::MaximumSingleCOP1:
            case EEOperation::MinimumSingleCOP1:
            {
              const EEFloatResult result =
                operation.instruction.operation ==
                  EEOperation::MaximumSingleCOP1
                  ? maxEEFloatRaw(
                      operation.capturedFS,
                      operation.capturedFT)
                  : minEEFloatRaw(
                      operation.capturedFS,
                      operation.capturedFT);
              expectedResult = result.bits;
              expectedFlags = result.flags;
              break;
            }
            case EEOperation::ConvertWordToSingleCOP1:
              expectedResult =
                fixedToFloatRaw(operation.capturedFS, 0);
              break;
            case EEOperation::ConvertSingleToWordCOP1:
            {
              const EEFloatResult result =
                convertEEFloatToWordRaw(operation.capturedFS);
              expectedResult = result.bits;
              expectedFlags = result.flags;
              break;
            }
            case EEOperation::AddSingleCOP1:
            case EEOperation::SubtractSingleCOP1:
            case EEOperation::AddSingleToAccumulatorCOP1:
            case EEOperation::SubtractSingleToAccumulatorCOP1:
            {
              const EEFloatResult result =
                operation.instruction.operation !=
                  EEOperation::SubtractSingleCOP1 &&
                operation.instruction.operation !=
                  EEOperation::SubtractSingleToAccumulatorCOP1
                  ? addFPRaw(
                      operation.capturedFS,
                      operation.capturedFT)
                  : subFPRaw(
                      operation.capturedFS,
                      operation.capturedFT);
              expectedResult = result.bits;
              expectedFlags = result.flags;
              break;
            }
            case EEOperation::MultiplySingleCOP1:
            case EEOperation::MultiplySingleToAccumulatorCOP1:
            {
              const EEFloatResult result =
                mulFPRaw(
                  operation.capturedFS,
                  operation.capturedFT);
              expectedResult = result.bits;
              expectedFlags = result.flags;
              break;
            }
            case EEOperation::MultiplyAddSingleCOP1:
            case EEOperation::MultiplyAddSingleToAccumulatorCOP1:
            case EEOperation::MultiplySubtractSingleCOP1:
            case EEOperation::MultiplySubtractSingleToAccumulatorCOP1:
            {
              const EECompoundFloatResult result =
                operation.instruction.operation ==
                    EEOperation::MultiplyAddSingleCOP1 ||
                  operation.instruction.operation ==
                    EEOperation::MultiplyAddSingleToAccumulatorCOP1
                  ? maddEEFloatRaw(
                      operation.capturedAccumulator,
                      operation.capturedFS,
                      operation.capturedFT)
                  : msubEEFloatRaw(
                      operation.capturedAccumulator,
                      operation.capturedFS,
                      operation.capturedFT);
              expectedResult = result.bits;
              expectedFlags = result.flags;
              expectedStickyFlags = result.stickyFlags;
              break;
            }
            case EEOperation::CompareFalseSingleCOP1:
            case EEOperation::CompareEqualSingleCOP1:
            case EEOperation::CompareLessThanSingleCOP1:
            case EEOperation::CompareLessThanOrEqualSingleCOP1:
            {
              const int comparison =
                compareEEFloatRaw(
                  operation.capturedFS,
                  operation.capturedFT);
              switch (operation.instruction.operation)
              {
                case EEOperation::CompareEqualSingleCOP1:
                  expectedCondition = comparison == 0;
                  break;
                case EEOperation::CompareLessThanSingleCOP1:
                  expectedCondition = comparison < 0;
                  break;
                case EEOperation::CompareLessThanOrEqualSingleCOP1:
                  expectedCondition = comparison <= 0;
                  break;
                default:
                  break;
              }
              break;
            }
            default:
              break;
          }
        }
        const bool singleSource =
          isCOP1SingleSourceStagedOperation(
            operation.instruction.operation);
        const bool wordToSingle =
          operation.instruction.operation ==
            EEOperation::ConvertWordToSingleCOP1;
        const bool singleToWord =
          operation.instruction.operation ==
            EEOperation::ConvertSingleToWordCOP1;
        const bool comparison =
          isCOP1ComparisonOperation(
            operation.instruction.operation);
        const EECOP1ResultDestination resultDestination =
          eeOperationMetadata(
            operation.instruction.operation).
              cop1ResultDestination;
        const bool compound =
          isCOP1CompoundOperation(
            operation.instruction.operation);
        const std::uint8_t expectedDestination =
          resultDestination ==
              EECOP1ResultDestination::Condition
            ? EECore::COP1_DESTINATION_CONDITION
            : resultDestination ==
                  EECOP1ResultDestination::Accumulator
              ? EECore::COP1_DESTINATION_ACCUMULATOR |
                EECore::COP1_DESTINATION_FCR31
              : EECore::COP1_DESTINATION_FPR |
                (wordToSingle
                  ? 0
                  : EECore::COP1_DESTINATION_FCR31);
        const std::uint8_t expectedAffectedFlags =
          wordToSingle || comparison
            ? 0
            : (singleToWord
              ? FP_FLAG_I_BIT
              : FP_FLAG_OVERFLOW | FP_FLAG_UNDERFLOW);
        require(
          operation.remainingCycles == 0 &&
            operation.stage <= EECore::COP1PipelineStage::S1 &&
            operation.destination.mask ==
              expectedDestination &&
            operation.destination.fprRegister ==
              operation.instruction.shiftAmount &&
            (compound ||
             operation.capturedAccumulator == 0) &&
            operation.capturedGPR == 0 &&
            operation.affectedFlags ==
              expectedAffectedFlags &&
            (resultComputed
              ? operation.raisedStickyFlags ==
                  expectedStickyFlags
              : operation.raisedStickyFlags == 0) &&
            (resultComputed
              ? operation.conditionResult == expectedCondition
              : !operation.conditionResult) &&
            (!singleSource || operation.capturedFT == 0) &&
            (operandsCaptured ||
             (operation.capturedFS == 0 &&
              operation.capturedFT == 0 &&
              operation.capturedAccumulator == 0 &&
              operation.capturedControl == 0)) &&
            (resultComputed
              ? (operation.rawResult == expectedResult &&
                 operation.raisedFlags == expectedFlags)
              : (operation.rawResult == 0 &&
                 operation.raisedFlags == 0)),
          "EE in-flight COP1 staged operation state is inconsistent");
      }
    }
    else
    {
      require(
        operation.programOrder == 0 &&
          operation.stage ==
            EECore::COP1PipelineStage::R &&
          operation.instructionAddress == 0 &&
          instruction == 0 &&
          operation.capturedFS == 0 &&
          operation.capturedFT == 0 &&
          operation.capturedAccumulator == 0 &&
          serializedControl == 0 &&
          operation.capturedControl == 0 &&
          !operation.branchDelaySlot &&
          operation.branchAddress == 0 &&
          operation.capturedGPR == 0 &&
          operation.memoryAddress == 0 &&
          operation.capturedMemoryValue == 0 &&
          operation.destination.mask ==
            EECore::COP1_DESTINATION_NONE &&
          operation.destination.fprRegister == 0 &&
          operation.destination.gprRegister == 0 &&
          operation.rawResult == 0 &&
          operation.affectedFlags == 0 &&
          operation.raisedFlags == 0 &&
          operation.raisedStickyFlags == 0 &&
          !serializedCondition &&
          !operation.conditionResult &&
          operation.remainingCycles == 0,
        "EE inactive COP1 operation contains state");
    }
  }
  core->packedMACContinuation = {};
  core->packedMACContinuation.initiationCycles =
    reader->readU8();
  for (EECore::InFlightPackedMACOperation &operation :
       core->packedMACContinuation.operations)
  {
    operation.active =
      reader->readBool("EE packed MAC operation flag");
    operation.operation =
      readEnum<EECore::PackedMACOperation>(
        reader,
        static_cast<std::uint8_t>(
          EECore::PackedMACOperation::
            HorizontalMultiplySubtractHalfword),
        "EE packed MAC operation");
    operation.programOrder = reader->readU64();
    operation.source.low = reader->readU64();
    operation.source.high = reader->readU64();
    operation.target.low = reader->readU64();
    operation.target.high = reader->readU64();
    operation.hiResult.low = reader->readU64();
    operation.hiResult.high = reader->readU64();
    operation.loResult.low = reader->readU64();
    operation.loResult.high = reader->readU64();
    operation.destinationRegister = reader->readU8();
    operation.generalRegisterResult.low = reader->readU64();
    operation.generalRegisterResult.high = reader->readU64();
    operation.remainingCycles = reader->readU8();
  }
  require(
    !core->packedMACContinuation.operations[1].active ||
      (core->packedMACContinuation.operations[0].active &&
       core->packedMACContinuation.operations[0].programOrder <
         core->packedMACContinuation.operations[1].programOrder),
    "EE packed MAC serialization order is not canonical");
  require(
    core->packedMACContinuationStateValid(),
    "EE packed MAC continuation state is invalid");
  core->packedDivideContinuation = {};
  core->packedDivideContinuation.active =
    reader->readBool("EE packed divide operation flag");
  core->packedDivideContinuation.operation =
    readEnum<EECore::PackedDivideOperation>(
      reader,
      static_cast<std::uint8_t>(
        EECore::PackedDivideOperation::DivideBroadcastWord),
      "EE packed divide operation");
  core->packedDivideContinuation.programOrder =
    reader->readU64();
  core->packedDivideContinuation.source.low =
    reader->readU64();
  core->packedDivideContinuation.source.high =
    reader->readU64();
  core->packedDivideContinuation.target.low =
    reader->readU64();
  core->packedDivideContinuation.target.high =
    reader->readU64();
  core->packedDivideContinuation.hiResult.low =
    reader->readU64();
  core->packedDivideContinuation.hiResult.high =
    reader->readU64();
  core->packedDivideContinuation.loResult.low =
    reader->readU64();
  core->packedDivideContinuation.loResult.high =
    reader->readU64();
  core->packedDivideContinuation.remainingCycles =
    reader->readU8();
  require(
    core->packedDivideContinuationStateValid(),
    "EE packed divide continuation state is invalid");
  const EECore::COP1ProgramOrderView programOrder =
    core->inFlightCOP1ProgramOrder();
  require(
    core->cop1ProgramOrderUnique(programOrder),
    "EE in-flight COP1 program order is duplicated");
  require(
    core->cop1DividerResultCountValid(programOrder),
    "EE COP1 divider has too many pending results");
  require(
    core->cop1DividerOverlapValid(programOrder),
    "EE COP1 divider overlap state is inconsistent");
  const EECore::COP1DividerOccupancy derivedDividerOccupancy =
    core->derivedCOP1DividerOccupancy();
  require(
    dividerOccupancy->initiationCycles ==
        derivedDividerOccupancy.initiationCycles &&
      dividerOccupancy->operation ==
        derivedDividerOccupancy.operation,
    "EE COP1 divider occupancy is inconsistent");
  for (std::size_t orderIndex = 0;
       orderIndex < programOrder.size();
       ++orderIndex)
  {
    const EECore::InFlightCOP1Operation &operation =
      core->inFlightCOP1Operations[
        programOrder[orderIndex]];
    if (isCOP1RegisterMoveOperation(
          operation.instruction.operation) &&
        operation.stage == EECore::COP1PipelineStage::Y)
    {
      bool blockedByOlderOperation = false;
      for (std::size_t olderIndex = 0;
           olderIndex < orderIndex;
           ++olderIndex)
      {
        const EECore::InFlightCOP1Operation &candidate =
          core->inFlightCOP1Operations[
            programOrder[olderIndex]];
        blockedByOlderOperation =
          blockedByOlderOperation ||
          !EECore::cop1RetirementReady(candidate);
      }
      require(
        blockedByOlderOperation,
        "EE COP1 register move W result has no older blocker");
    }
    const bool memoryOperation =
      isCOP1MemoryMoveOperation(
        operation.instruction.operation);
    if (memoryOperation &&
        operation.stage == EECore::COP1PipelineStage::Y)
    {
      bool blockedByOlderOperation = false;
      bool conflictsWithOlderWriter = false;
      for (std::size_t olderIndex = 0;
           olderIndex < orderIndex;
           ++olderIndex)
      {
        const EECore::InFlightCOP1Operation &candidate =
          core->inFlightCOP1Operations[
            programOrder[olderIndex]];
        blockedByOlderOperation =
          blockedByOlderOperation ||
          !EECore::cop1RetirementReady(candidate);
        conflictsWithOlderWriter =
          conflictsWithOlderWriter ||
          (isLoadOperation(
             operation.instruction.operation) &&
           (candidate.destination.mask &
            EECore::COP1_DESTINATION_FPR) != 0 &&
           candidate.destination.fprRegister ==
             operation.destination.fprRegister);
      }
      require(
        blockedByOlderOperation &&
          !conflictsWithOlderWriter,
        "EE COP1 memory W result has no valid older blocker");
    }
    if (isCOP1StagedOperation(
          operation.instruction.operation) &&
        operation.stage == EECore::COP1PipelineStage::S1)
    {
      std::size_t olderDividerCount = 0;
      bool reachableBehindOlderOperations = true;
      const EEInstructionDependencies operationDependencies =
        eeInstructionDependencies(operation.instruction);
      const EECore::InFlightCOP1Operation *
        forwardedSource = nullptr;
      for (std::size_t olderIndex = 0;
           olderIndex < orderIndex;
           ++olderIndex)
      {
        const EECore::InFlightCOP1Operation &candidate =
          core->inFlightCOP1Operations[
            programOrder[olderIndex]];
        if (isCOP1DividerOperation(
              candidate.instruction.operation))
        {
          ++olderDividerCount;
          const std::uint64_t orderDistance =
            operation.programOrder - candidate.programOrder;
          const EECOP1DividerTiming timing =
            cop1DividerTiming(
              candidate.instruction.operation);
          reachableBehindOlderOperations =
            reachableBehindOlderOperations &&
            orderDistance + 5 <= timing.latency &&
            candidate.remainingCycles <=
              timing.latency - (orderDistance + 5) &&
            ((operationDependencies.fprReads |
              operationDependencies.fprWrites) &
             (UINT32_C(1) <<
              candidate.destination.fprRegister)) == 0;
          continue;
        }
        reachableBehindOlderOperations =
          reachableBehindOlderOperations &&
          candidate.stage == EECore::COP1PipelineStage::S1 &&
          (candidate.instruction.operation ==
             EEOperation::ConvertWordToSingleCOP1) &&
          ((operationDependencies.fprReads |
            operationDependencies.fprWrites) &
           (UINT32_C(1) <<
            candidate.destination.fprRegister)) == 0;
        if ((candidate.destination.mask &
             EECore::COP1_DESTINATION_FPR) != 0 &&
            candidate.destination.fprRegister ==
              operation.instruction.destinationRegister)
        {
          forwardedSource = &candidate;
        }
      }
      require(
        operation.instruction.operation ==
            EEOperation::ConvertWordToSingleCOP1 &&
          olderDividerCount == 1 &&
          reachableBehindOlderOperations &&
          (forwardedSource == nullptr ||
           operation.capturedFS == forwardedSource->rawResult),
        "EE COP1 staged S1 result has no valid older blocker");
    }
  }
  require(
    core->stagedCOP1PipelineOrderValid(programOrder),
    "EE staged COP1 pipeline order is inconsistent");
  core->lastDecodedInstruction = {};
  if (core->lastInstructionValid)
  {
    core->lastDecodedInstruction =
      decodeEEInstruction(lastInstruction);
  }
  const auto restoreIssueLatch =
    [](EECore::DecodedIssueLatch *latch,
       std::uint32_t instruction)
    {
      latch->instruction = {};
      if (!latch->valid)
      {
        require(
          latch->address == 0 &&
            instruction == 0 &&
            latch->failure ==
              EECore::IssueLatchFailure::None,
          "EE inactive issue latch contains state");
        return;
      }

      if (latch->failure == EECore::IssueLatchFailure::None)
      {
        require(
          (latch->address & 3) == 0,
          "EE decoded issue latch address is invalid");
        latch->instruction =
          decodeEEInstruction(instruction);
        return;
      }

      latch->instruction.raw = instruction;
      if (latch->failure ==
            EECore::IssueLatchFailure::AddressError)
      {
        require(
          (latch->address & 3) != 0 && instruction == 0,
          "EE address-error issue latch is inconsistent");
        return;
      }
      if (latch->failure == EECore::IssueLatchFailure::BusError)
      {
        require(
          (latch->address & 3) == 0 && instruction == 0,
          "EE bus-error issue latch is inconsistent");
        return;
      }

      require(
        (latch->address & 3) == 0,
        "EE decode-failure issue latch address is invalid");
      bool matchingDecodeFailure = false;
      try
      {
        decodeEEInstruction(instruction);
      }
      catch (const EEInstructionDecodeError &error)
      {
        matchingDecodeFailure =
          (latch->failure ==
             EECore::IssueLatchFailure::ReservedInstruction &&
           error.failure() ==
             EEInstructionDecodeFailure::Reserved) ||
          (latch->failure ==
             EECore::IssueLatchFailure::UnsupportedInstruction &&
           error.failure() ==
             EEInstructionDecodeFailure::Unsupported);
      }
      require(
        matchingDecodeFailure,
        "EE decode-failure issue latch is inconsistent");
    };
  restoreIssueLatch(&core->issueLatch, issueInstruction);
  restoreIssueLatch(&core->stagingLatch, stagingInstruction);
  if (youngerAStageActive)
  {
    require(
      core->nextEEProgramOrder > 1,
      "EE younger A-stage continuation has no program order");
    require(
      (youngerAStageAddress & 3) == 0,
      "EE younger A-stage continuation address is unaligned");
    try
    {
      const EEInstruction instruction =
        decodeEEInstruction(youngerAStageInstruction);
      const EEInstructionRouting routing =
        eeInstructionRouting(instruction.operation);
      require(
        routing.category == EEInstructionCategory::ALU ||
          routing.category ==
            EEInstructionCategory::LeadingZeroCount ||
          routing.category == EEInstructionCategory::MAC1,
        "EE younger A-stage continuation operation is invalid");
      require(
        (routing.logicalPipes &
          static_cast<std::uint8_t>(
            EELogicalPipe::Pipe1)) != 0,
        "EE younger A-stage continuation cannot use Pipe 1");
      require(
        EECore::isActivatedOIssueOperation(
          instruction.operation),
        "EE younger A-stage continuation operation is inactive");
      core->youngerAStageContinuation = {
        true,
        core->nextEEProgramOrder - 1,
        youngerAStageAddress,
        instruction
      };
    }
    catch (const EEInstructionDecodeError &)
    {
      throw std::runtime_error(
        "EE younger A-stage continuation cannot be decoded");
    }
  }
  else
  {
    require(
      youngerAStageInstruction == 0 &&
        youngerAStageAddress == 0,
      "EE inactive younger A-stage continuation contains state");
    core->youngerAStageContinuation = {};
  }

  require(
    core->generalRegisters[0] == EERegister128{},
    "EE general-purpose register zero is not immutable");
  require(
    (core->cop1StatusRegister &
      ~EECOP1Control::STATUS_WRITABLE_MASK) == 0,
    "EE FCR31 contains non-writable bits");
  require(
    core->exception != EEException::None ||
      core->faultAddress == 0,
    "EE exception address is present without an exception");
  require(
    core->state == EEExecutionState::Running ||
      core->haltReason != EEStopReason::None ||
      core->cycles == 0,
    "halted EE state has no stop reason");
  require(
    core->state != EEExecutionState::Running ||
      core->haltReason == EEStopReason::None,
    "running EE state has a stop reason");
  require(
    core->lastInstructionValid ||
      (core->lastAddress == 0 && lastInstruction == 0),
    "EE invalid last instruction contains state");
  require(
    !core->issueLatch.valid ||
      (core->pc == core->issueLatch.address &&
       (core->state == EEExecutionState::Running ||
        (core->state == EEExecutionState::Halted &&
         core->haltReason == EEStopReason::HostHalt))),
    "EE decoded issue latch state is inconsistent");
  require(
    !core->stagingLatch.valid ||
      (core->issueLatch.valid &&
       core->issueLatch.failure ==
         EECore::IssueLatchFailure::None &&
       core->stagingLatch.address ==
         core->issueLatch.address + 4 &&
       !core->branchDelayPending),
    "EE staging latch state is inconsistent");
  require(
    !core->youngerAStageContinuation.active ||
      (core->state == EEExecutionState::Running ||
       (core->state == EEExecutionState::Halted &&
        core->haltReason == EEStopReason::HostHalt)),
    "EE younger A-stage continuation state is inconsistent");
  bool packedMACPrecedesYoungerAStage = true;
  bool packedMACYoungerPairReachable = true;
  bool packedDividePrecedesYoungerAStage = true;
  bool packedDivideYoungerPairReachable = true;
  if (core->youngerAStageContinuation.active)
  {
    for (const EECore::InFlightPackedMACOperation &operation :
         core->packedMACContinuation.operations)
    {
      packedMACPrecedesYoungerAStage =
        packedMACPrecedesYoungerAStage &&
        (!operation.active ||
         operation.programOrder <
           core->youngerAStageContinuation.programOrder);
    }
    if (core->packedMACContinuationActive())
    {
      const EECore::PackedMACProgramOrderView packedOrder =
        core->packedMACProgramOrder();
      const EECore::InFlightPackedMACOperation &operation =
        core->packedMACContinuation.operations[
          packedOrder[0]];
      packedMACYoungerPairReachable =
        packedOrder.size() == 1 &&
        core->packedMACContinuation.initiationCycles == 2 &&
        operation.remainingCycles == 4 &&
        operation.programOrder ==
          core->youngerAStageContinuation.programOrder - 1;
    }
    if (core->packedDivideContinuation.active)
    {
      packedDividePrecedesYoungerAStage =
        core->packedDivideContinuation.programOrder <
          core->youngerAStageContinuation.programOrder;
      const std::uint64_t programOrderDistance =
        core->youngerAStageContinuation.programOrder -
          core->packedDivideContinuation.programOrder;
      const std::uint8_t elapsedDivideCycles =
        static_cast<std::uint8_t>(
          37 -
          core->packedDivideContinuation.remainingCycles);
      packedDivideYoungerPairReachable =
        (elapsedDivideCycles == 0 &&
         programOrderDistance == 1) ||
        (elapsedDivideCycles != 0 &&
         programOrderDistance >= 2 &&
         programOrderDistance <=
           static_cast<std::uint64_t>(
             elapsedDivideCycles) * 2 + 1);
    }
  }
  require(
    !core->youngerAStageContinuation.active ||
      (!core->issueLatch.valid &&
       !core->stagingLatch.valid &&
       !core->branchDelayPending &&
       !core->pendingMac0.active &&
       !core->pendingMac1.active &&
       packedMACPrecedesYoungerAStage &&
       packedMACYoungerPairReachable &&
       packedDividePrecedesYoungerAStage &&
       packedDivideYoungerPairReachable &&
       !core->packedMACContinuationBlocks(
         core->youngerAStageContinuation.instruction) &&
       !core->packedMACBlocksScalarMAC(
         core->youngerAStageContinuation.instruction) &&
       !core->packedDivideContinuationBlocks(
         core->youngerAStageContinuation.instruction)),
    "EE younger A-stage continuation conflicts with other state");
  require(
    core->branchDelayPending ||
      (core->branchDelayTarget == 0 &&
       core->branchInstructionAddress == 0 &&
       !core->branchDelayFromLikely &&
       !core->branchDelayTaken),
    "EE inactive branch delay contains state");
  require(
    !core->branchDelayPending ||
      ((core->branchInstructionAddress & 3) == 0 &&
       core->pc == core->branchInstructionAddress + 4 &&
       core->lastInstructionValid &&
       core->lastAddress == core->branchInstructionAddress &&
       isEEBranchOperation(
         core->lastDecodedInstruction.operation) &&
       core->branchDelayFromLikely ==
         isEEBranchLikelyOperation(
           core->lastDecodedInstruction.operation)),
    "EE pending branch delay state is inconsistent");
  require(
    core->cop1DividerPostDelayInstructions <= 2 &&
      (core->cop1DividerPostDelayInstructions != 0 ||
       (core->cop1DividerPostDelayBranchAddress == 0 &&
        core->cop1DividerPostDelayTargetAddress == 0 &&
        !core->cop1DividerPostDelayTaken)) &&
      (core->cop1DividerPostDelayBranchAddress & 3) == 0 &&
      (core->cop1DividerPostDelayTargetAddress & 3) == 0,
    "EE COP1 post-delay hazard state is invalid");
  require(
    core->cop1DividerPostTargetInstructions <= 2 &&
      (core->cop1DividerPostTargetInstructions != 0 ||
       core->cop1DividerPostTargetAddress == 0) &&
      (core->cop1DividerPostTargetAddress & 3) == 0,
    "EE COP1 post-target hazard state is invalid");
  require(
    core->cop1DividerPostTargetInstructions <=
      core->cop1DividerPostDelayInstructions,
    "EE COP1 branch hazard windows are inconsistent");
  require(
    !core->cop1DividerPostDelayTaken ||
      (core->cop1DividerPostTargetInstructions != 0 &&
       core->cop1DividerPostDelayInstructions ==
         core->cop1DividerPostTargetInstructions &&
       core->cop1DividerPostDelayTargetAddress ==
         core->cop1DividerPostTargetAddress),
    "EE COP1 branch hazard provenance is inconsistent");
  require(
    core->cop1DividerPostDelayTaken ||
      core->cop1DividerPostDelayTargetAddress == 0,
    "EE untaken branch hazard contains a target");
}
