#include "save_state_internal.hpp"

#include <cassert>

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
static_assert(
  static_cast<std::uint8_t>(
    EEException::CoprocessorUnusable) == 11,
  "Version-29 EE exception ordinals changed.");

void NekoSaveStateCodec::writeEECore(
  SaveStateWriter *writer,
  const EECore &core)
{
  assert(core.memorySystem.stateValid());
  {
    auto registers = writer->scope("generalRegisters");
    for (std::size_t index = 0;
         index < core.generalRegisters.size();
         ++index)
    {
      auto element = writer->element(index);
      writer->writeFieldU64("low", core.generalRegisters[index].low);
      writer->writeFieldU64("high", core.generalRegisters[index].high);
    }
  }
  {
    auto registers = writer->scope("floatingPointRegisters");
    for (std::size_t index = 0;
         index < core.floatingPointRegisters.size();
         ++index)
    {
      auto element = writer->element(index);
      writer->writeFieldU32("bits", core.floatingPointRegisters[index]);
    }
  }
  writer->writeFieldU32(
    "floatingPointAccumulatorRegister",
    core.floatingPointAccumulatorRegister);
  writer->writeFieldU32("cop1StatusRegister", core.cop1StatusRegister);
  writer->writeFieldU32("pc", core.pc);
  writer->writeFieldU64("hiRegister", core.hiRegister);
  writer->writeFieldU64("loRegister", core.loRegister);
  writer->writeFieldU64("hi1Register", core.hi1Register);
  writer->writeFieldU64("lo1Register", core.lo1Register);
  writer->writeFieldU32("saRegister", core.saRegister);
  writer->writeFieldU32("cop0BadVAddr", core.cop0BadVAddr);
  writer->writeFieldU32("cop0Count", core.cop0Count);
  writer->writeFieldU32("cop0Compare", core.cop0Compare);
  writer->writeFieldU32("cop0Status", core.cop0Status);
  writer->writeFieldU32("cop0Cause", core.cop0Cause);
  writer->writeFieldU32("cop0EPC", core.cop0EPC);
  writer->writeFieldU32("cop0ErrorEPC", core.cop0ErrorEPC);
  writer->writeFieldU8(
    "exception",
    static_cast<std::uint8_t>(core.exception));
  writer->writeFieldU32("faultAddress", core.faultAddress);
  writer->writeFieldU8(
    "state",
    static_cast<std::uint8_t>(core.state));
  writer->writeFieldU8(
    "haltReason",
    static_cast<std::uint8_t>(core.haltReason));
  writer->writeFieldU64("cycles", core.cycles);
  writer->writeFieldBool(
    "lastInstructionValid",
    core.lastInstructionValid);
  writer->writeFieldU32("lastAddress", core.lastAddress);
  writer->writeFieldU32(
    "lastInstruction",
    core.lastDecodedInstruction.raw);
  writer->writeFieldU32(
    "rejectedInstructionValue",
    core.rejectedInstructionValue);
  const auto writePending =
    [writer](
      const char *name,
      const EECore::PendingMultiplyDivide &operation)
    {
      auto pending = writer->scope(name);
      writer->writeFieldBool("active", operation.active);
      writer->writeFieldU8(
        "remainingCycles",
        operation.remainingCycles);
      writer->writeFieldU64("hiResult", operation.hiResult);
      writer->writeFieldU64("loResult", operation.loResult);
      writer->writeFieldBool(
        "writesGeneralRegister",
        operation.resultDestination ==
          EECore::MACResultDestination::HIAndLOAndGPR);
      writer->writeFieldU8(
        "generalRegister",
        operation.generalRegister);
      writer->writeFieldU64(
        "generalRegisterResult",
        operation.generalRegisterResult);
    };
  writePending("pendingMac0", core.pendingMac0);
  writePending("pendingMac1", core.pendingMac1);
  writer->writeFieldU8(
    "issueLatchFailure",
    static_cast<std::uint8_t>(
      core.issueLatch.failure));
  writer->writeFieldBool("stagingLatchValid", core.stagingLatch.valid);
  writer->writeFieldU8(
    "stagingLatchFailure",
    static_cast<std::uint8_t>(
      core.stagingLatch.failure));
  writer->writeFieldU32("stagingLatchAddress", core.stagingLatch.address);
  writer->writeFieldU32(
    "stagingLatchInstruction",
    core.stagingLatch.instruction.raw);
  writer->writeFieldBool(
    "youngerAStageActive",
    core.youngerAStageContinuation.active);
  writer->writeFieldU32(
    "youngerAStageInstruction",
    core.youngerAStageContinuation.instruction.raw);
  writer->writeFieldU32(
    "youngerAStageAddress",
    core.youngerAStageContinuation.address);
  writer->writeFieldU8(
    "issueLatchTranslationOutcome",
    static_cast<std::uint8_t>(
      core.issueLatch.translationOutcome));
  writer->writeFieldU8(
    "stagingLatchTranslationOutcome",
    static_cast<std::uint8_t>(
      core.stagingLatch.translationOutcome));
  {
    auto reserved = writer->scope("reservedFrontEnd");
    for (std::size_t index = 0; index < 2; ++index)
    {
      auto element = writer->element(index);
      writer->writeFieldU8("value", 0);
    }
  }
  const EECore::COP1DividerOccupancy dividerOccupancy =
    core.derivedCOP1DividerOccupancy();
  {
    auto divider = writer->scope("dividerOccupancy");
    writer->writeFieldU8(
      "initiationCycles",
      dividerOccupancy.initiationCycles);
    writer->writeFieldU8(
      "operation",
      static_cast<std::uint8_t>(dividerOccupancy.operation));
  }
  writer->writeFieldBool("retiredCOP1OperateResource", false);
  writer->writeFieldU8(
    "shiftAmountAccessHistory",
    core.shiftAmountOrdering.accessHistory());
  writer->writeFieldU8(
    "shiftAmountReadHistory",
    core.shiftAmountOrdering.readHistory());
  writer->writeFieldBool("branchDelayPending", core.branchDelayPending);
  writer->writeFieldU32("branchDelayTarget", core.branchDelayTarget);
  writer->writeFieldU32(
    "branchInstructionAddress",
    core.branchInstructionAddress);
  writer->writeFieldBool(
    "branchDelayFromLikely",
    core.branchDelayFromLikely);
  writer->writeFieldBool("branchDelayTaken", core.branchDelayTaken);
  writer->writeFieldU8(
    "cop1DividerPostDelayInstructions",
    core.cop1DividerPostDelayInstructions);
  writer->writeFieldU32(
    "cop1DividerPostDelayBranchAddress",
    core.cop1DividerPostDelayBranchAddress);
  writer->writeFieldU32(
    "cop1DividerPostDelayTargetAddress",
    core.cop1DividerPostDelayTargetAddress);
  writer->writeFieldBool(
    "cop1DividerPostDelayTaken",
    core.cop1DividerPostDelayTaken);
  writer->writeFieldU8(
    "cop1DividerPostTargetInstructions",
    core.cop1DividerPostTargetInstructions);
  writer->writeFieldU32(
    "cop1DividerPostTargetAddress",
    core.cop1DividerPostTargetAddress);
  writer->writeFieldBool("issueLatchValid", core.issueLatch.valid);
  writer->writeFieldU32("issueLatchAddress", core.issueLatch.address);
  writer->writeFieldU32(
    "issueLatchInstruction",
    core.issueLatch.instruction.raw);
  writer->writeFieldU64("nextProgramOrder", core.nextEEProgramOrder);
  {
    auto operations = writer->scope("inFlightCOP1Operations");
    for (std::size_t index = 0;
         index < core.inFlightCOP1Operations.size();
         ++index)
  {
    auto element = writer->element(index);
    const EECore::InFlightCOP1Operation &operation =
      core.inFlightCOP1Operations[index];
    const bool memoryOperation =
      operation.active &&
      isCOP1MemoryMoveOperation(
        operation.instruction.operation);
    writer->writeFieldBool("active", operation.active);
    writer->writeFieldU64("programOrder", operation.programOrder);
    writer->writeFieldU8(
      "stage",
      static_cast<std::uint8_t>(operation.stage));
    writer->writeFieldU32(
      "instructionAddress",
      operation.instructionAddress);
    writer->writeFieldU32("instruction", operation.instruction.raw);
    writer->writeFieldU32("capturedFS", operation.capturedFS);
    writer->writeFieldU32("capturedFT", operation.capturedFT);
    writer->writeFieldU32(
      "capturedAccumulator",
      operation.capturedAccumulator);
    // Memory operations use otherwise-unused result slots for
    // branch-delay fault provenance without changing version-24 layout.
    writer->writeFieldU32(
      "serializedControl",
      memoryOperation
        ? operation.branchAddress
        : operation.capturedControl);
    writer->writeFieldU64("capturedGPR", operation.capturedGPR);
    writer->writeFieldU32("memoryAddress", operation.memoryAddress);
    writer->writeFieldU32(
      "capturedMemoryValue",
      operation.capturedMemoryValue);
    writer->writeFieldU8("destinationMask", operation.destination.mask);
    writer->writeFieldU8(
      "destinationFPR",
      operation.destination.fprRegister);
    writer->writeFieldU8(
      "destinationGPR",
      operation.destination.gprRegister);
    writer->writeFieldU32("rawResult", operation.rawResult);
    writer->writeFieldU8("affectedFlags", operation.affectedFlags);
    writer->writeFieldU8("raisedFlags", operation.raisedFlags);
    writer->writeFieldU8(
      "raisedStickyFlags",
      operation.raisedStickyFlags);
    writer->writeFieldBool(
      "serializedCondition",
      memoryOperation
        ? operation.branchDelaySlot
        : operation.conditionResult);
    writer->writeFieldU8(
      "remainingCycles",
      operation.remainingCycles);
    }
  }
  {
    auto continuation = writer->scope("packedMACContinuation");
    writer->writeFieldU8(
      "initiationCycles",
      core.packedMACContinuation.initiationCycles);
    const EECore::PackedMACProgramOrderView packedOrder =
      core.packedMACProgramOrder();
    auto operations = writer->scope("operations");
    for (std::size_t orderIndex = 0;
         orderIndex < EECore::PackedMACContinuation::CAPACITY;
         ++orderIndex)
    {
      auto element = writer->element(orderIndex);
      EECore::InFlightPackedMACOperation operation;
      if (orderIndex < packedOrder.size())
      {
        operation =
          core.packedMACContinuation.operations[
            packedOrder[orderIndex]];
      }
      writer->writeFieldBool("active", operation.active);
      writer->writeFieldU8(
        "operation",
        static_cast<std::uint8_t>(operation.operation));
      writer->writeFieldU64("programOrder", operation.programOrder);
      writer->writeFieldU64("sourceLow", operation.source.low);
      writer->writeFieldU64("sourceHigh", operation.source.high);
      writer->writeFieldU64("targetLow", operation.target.low);
      writer->writeFieldU64("targetHigh", operation.target.high);
      writer->writeFieldU64("hiResultLow", operation.hiResult.low);
      writer->writeFieldU64("hiResultHigh", operation.hiResult.high);
      writer->writeFieldU64("loResultLow", operation.loResult.low);
      writer->writeFieldU64("loResultHigh", operation.loResult.high);
      writer->writeFieldU8(
        "destinationRegister",
        operation.destinationRegister);
      writer->writeFieldU64(
        "generalRegisterResultLow",
        operation.generalRegisterResult.low);
      writer->writeFieldU64(
        "generalRegisterResultHigh",
        operation.generalRegisterResult.high);
      writer->writeFieldU8(
        "remainingCycles",
        operation.remainingCycles);
    }
  }
  {
    auto continuation = writer->scope("packedDivideContinuation");
    writer->writeFieldBool(
      "active",
      core.packedDivideContinuation.active);
    writer->writeFieldU8(
      "operation",
      static_cast<std::uint8_t>(
        core.packedDivideContinuation.operation));
    writer->writeFieldU64(
      "programOrder",
      core.packedDivideContinuation.programOrder);
    writer->writeFieldU64(
      "sourceLow",
      core.packedDivideContinuation.source.low);
    writer->writeFieldU64(
      "sourceHigh",
      core.packedDivideContinuation.source.high);
    writer->writeFieldU64(
      "targetLow",
      core.packedDivideContinuation.target.low);
    writer->writeFieldU64(
      "targetHigh",
      core.packedDivideContinuation.target.high);
    writer->writeFieldU64(
      "hiResultLow",
      core.packedDivideContinuation.hiResult.low);
    writer->writeFieldU64(
      "hiResultHigh",
      core.packedDivideContinuation.hiResult.high);
    writer->writeFieldU64(
      "loResultLow",
      core.packedDivideContinuation.loResult.low);
    writer->writeFieldU64(
      "loResultHigh",
      core.packedDivideContinuation.loResult.high);
    writer->writeFieldU8(
      "remainingCycles",
      core.packedDivideContinuation.remainingCycles);
  }
  const EECOP0Register memoryRegisters[] = {
    EECOP0Register::Index,
    EECOP0Register::Random,
    EECOP0Register::EntryLo0,
    EECOP0Register::EntryLo1,
    EECOP0Register::Context,
    EECOP0Register::PageMask,
    EECOP0Register::Wired,
    EECOP0Register::EntryHi,
    EECOP0Register::Config,
    EECOP0Register::TagLo,
    EECOP0Register::TagHi
  };
  {
    auto registers = writer->scope("memoryRegisters");
    for (std::size_t index = 0;
         index < sizeof(memoryRegisters) / sizeof(memoryRegisters[0]);
         ++index)
    {
      auto element = writer->element(index);
      writer->writeFieldU32(
        "value",
        core.memorySystem.cop0Register(memoryRegisters[index]));
    }
  }
  {
    auto entries = writer->scope("tlbEntries");
    for (std::size_t index = 0;
         index < EEMemorySystem::TLB_ENTRY_COUNT;
         ++index)
    {
      auto element = writer->element(index);
      const EETLBEntry &entry =
        core.memorySystem.tlbEntry(index);
      writer->writeFieldU32("pageMask", entry.pageMask);
      writer->writeFieldU32("entryHi", entry.entryHi);
      writer->writeFieldU32("evenPage", entry.evenPage.value);
      writer->writeFieldU32("oddPage", entry.oddPage.value);
    }
  }
  const auto writeCacheLine =
    [writer](const EECacheLine &line)
    {
      writer->writeFieldBytes(
        "data",
        line.data.data(),
        line.data.size());
      writer->writeFieldU32("physicalTag", line.physicalTag);
      writer->writeFieldBool("valid", line.valid);
      writer->writeFieldBool("dirty", line.dirty);
      writer->writeFieldBool(
        "leastRecentlyFilled",
        line.leastRecentlyFilled);
      writer->writeFieldBool("locked", line.locked);
    };
  {
    auto cache = writer->scope("instructionCache");
    for (std::size_t setIndex = 0;
         setIndex < core.memorySystem.instructionCache.size();
         ++setIndex)
    {
      auto set = writer->element(setIndex);
      for (std::size_t way = 0;
           way < core.memorySystem.instructionCache[setIndex].size();
           ++way)
      {
        auto line = writer->element(way);
        writeCacheLine(
          core.memorySystem.instructionCache[setIndex][way]);
      }
    }
  }
  {
    auto cache = writer->scope("dataCache");
    for (std::size_t setIndex = 0;
         setIndex < core.memorySystem.dataCache.size();
         ++setIndex)
    {
      auto set = writer->element(setIndex);
      for (std::size_t way = 0;
           way < core.memorySystem.dataCache[setIndex].size();
           ++way)
      {
        auto line = writer->element(way);
        writeCacheLine(core.memorySystem.dataCache[setIndex][way]);
      }
    }
  }
}

void NekoSaveStateCodec::readEECore(
  SaveStateReader *reader,
  EECore *core,
  EECore::COP1DividerOccupancy *dividerOccupancy)
{
  const auto require =
    [reader](bool condition, const std::string &detail)
    {
      reader->requireField(condition, detail);
    };
  {
    auto registers = reader->scope("generalRegisters");
    for (std::size_t index = 0;
         index < core->generalRegisters.size();
         ++index)
    {
      auto element = reader->element(index);
      core->generalRegisters[index].low =
        reader->readFieldU64("low");
      core->generalRegisters[index].high =
        reader->readFieldU64("high");
    }
  }
  {
    auto registers = reader->scope("floatingPointRegisters");
    for (std::size_t index = 0;
         index < core->floatingPointRegisters.size();
         ++index)
    {
      auto element = reader->element(index);
      core->floatingPointRegisters[index] =
        reader->readFieldU32("bits");
    }
  }
  core->floatingPointAccumulatorRegister =
    reader->readFieldU32("floatingPointAccumulatorRegister");
  core->cop1StatusRegister =
    reader->readFieldU32("cop1StatusRegister");
  core->pc = reader->readFieldU32("pc");
  core->hiRegister = reader->readFieldU64("hiRegister");
  core->loRegister = reader->readFieldU64("loRegister");
  core->hi1Register = reader->readFieldU64("hi1Register");
  core->lo1Register = reader->readFieldU64("lo1Register");
  core->saRegister = reader->readFieldU32("saRegister");
  core->cop0BadVAddr = reader->readFieldU32("cop0BadVAddr");
  core->cop0Count = reader->readFieldU32("cop0Count");
  core->cop0Compare = reader->readFieldU32("cop0Compare");
  core->cop0Status = reader->readFieldU32("cop0Status");
  core->cop0Cause = reader->readFieldU32("cop0Cause");
  core->cop0EPC = reader->readFieldU32("cop0EPC");
  core->cop0ErrorEPC = reader->readFieldU32("cop0ErrorEPC");
  core->exception = readEnum<EEException>(
    reader,
    static_cast<std::uint8_t>(
      EEException::TLBModified),
    "exception");
  core->faultAddress = reader->readFieldU32("faultAddress");
  core->state = readEnum<EEExecutionState>(
    reader,
    static_cast<std::uint8_t>(
      EEExecutionState::Running),
    "state");
  core->haltReason = readEnum<EEStopReason>(
    reader,
    static_cast<std::uint8_t>(
      EEStopReason::UndefinedOperation),
    "haltReason");
  core->cycles = reader->readFieldU64("cycles");
  core->lastInstructionValid =
    reader->readFieldBool("lastInstructionValid");
  core->lastAddress = reader->readFieldU32("lastAddress");
  const std::uint32_t lastInstruction =
    reader->readFieldU32("lastInstruction");
  core->rejectedInstructionValue =
    reader->readFieldU32("rejectedInstructionValue");
  const auto readPending =
    [reader, &require](
      EECore::PendingMultiplyDivide *operation,
      const char *name)
    {
      auto pending = reader->scope(name);
      operation->active = reader->readFieldBool("active");
      operation->remainingCycles =
        reader->readFieldU8("remainingCycles");
      operation->hiResult = reader->readFieldU64("hiResult");
      operation->loResult = reader->readFieldU64("loResult");
      operation->resultDestination =
        reader->readFieldBool("writesGeneralRegister")
          ? EECore::MACResultDestination::HIAndLOAndGPR
          : EECore::MACResultDestination::HIAndLO;
      operation->generalRegister =
        reader->readFieldU8("generalRegister");
      operation->generalRegisterResult =
        reader->readFieldU64("generalRegisterResult");
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
    "pendingMac0");
  readPending(
    &core->pendingMac1,
    "pendingMac1");
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
        EECore::IssueLatchFailure::TranslationError),
      "issueLatchFailure");
  core->stagingLatch.valid =
    reader->readFieldBool("stagingLatchValid");
  core->stagingLatch.failure =
    readEnum<EECore::IssueLatchFailure>(
      reader,
      static_cast<std::uint8_t>(
        EECore::IssueLatchFailure::TranslationError),
      "stagingLatchFailure");
  core->stagingLatch.address =
    reader->readFieldU32("stagingLatchAddress");
  const std::uint32_t stagingInstruction =
    reader->readFieldU32("stagingLatchInstruction");
  const bool youngerAStageActive =
    reader->readFieldBool("youngerAStageActive");
  const std::uint32_t youngerAStageInstruction =
    reader->readFieldU32("youngerAStageInstruction");
  const std::uint32_t youngerAStageAddress =
    reader->readFieldU32("youngerAStageAddress");
  core->issueLatch.translationOutcome =
    readEnum<EEAddressTranslationOutcome>(
      reader,
      static_cast<std::uint8_t>(
        EEAddressTranslationOutcome::UnsupportedCacheAttribute),
      "issueLatchTranslationOutcome");
  core->stagingLatch.translationOutcome =
    readEnum<EEAddressTranslationOutcome>(
      reader,
      static_cast<std::uint8_t>(
        EEAddressTranslationOutcome::UnsupportedCacheAttribute),
      "stagingLatchTranslationOutcome");
  {
    auto reserved = reader->scope("reservedFrontEnd");
    for (std::size_t index = 0; index < 2; ++index)
    {
      auto element = reader->element(index);
      require(
        reader->readFieldU8("value") == 0,
        "EE reserved front-end state is not empty");
    }
  }
  {
    auto divider = reader->scope("dividerOccupancy");
    dividerOccupancy->initiationCycles =
      reader->readFieldU8("initiationCycles");
    dividerOccupancy->operation =
      static_cast<EEOperation>(
        reader->readFieldU8("operation"));
  }
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
    reader->readFieldBool("retiredCOP1OperateResource");
  require(
    !retiredCOP1OperateResource,
    "retired EE COP1 operate resource state is not empty");
  const std::uint8_t recentShiftAmountAccesses =
    reader->readFieldU8("shiftAmountAccessHistory");
  const std::uint8_t recentShiftAmountReads =
    reader->readFieldU8("shiftAmountReadHistory");
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
    reader->readFieldBool("branchDelayPending");
  core->branchDelayTarget =
    reader->readFieldU32("branchDelayTarget");
  core->branchInstructionAddress =
    reader->readFieldU32("branchInstructionAddress");
  core->branchDelayFromLikely =
    reader->readFieldBool("branchDelayFromLikely");
  core->branchDelayTaken =
    reader->readFieldBool("branchDelayTaken");
  core->cop1DividerPostDelayInstructions =
    reader->readFieldU8("cop1DividerPostDelayInstructions");
  core->cop1DividerPostDelayBranchAddress =
    reader->readFieldU32("cop1DividerPostDelayBranchAddress");
  core->cop1DividerPostDelayTargetAddress =
    reader->readFieldU32("cop1DividerPostDelayTargetAddress");
  core->cop1DividerPostDelayTaken =
    reader->readFieldBool("cop1DividerPostDelayTaken");
  core->cop1DividerPostTargetInstructions =
    reader->readFieldU8("cop1DividerPostTargetInstructions");
  core->cop1DividerPostTargetAddress =
    reader->readFieldU32("cop1DividerPostTargetAddress");
  core->issueLatch.valid =
    reader->readFieldBool("issueLatchValid");
  core->issueLatch.address =
    reader->readFieldU32("issueLatchAddress");
  const std::uint32_t issueInstruction =
    reader->readFieldU32("issueLatchInstruction");
  core->nextEEProgramOrder =
    reader->readFieldU64("nextProgramOrder");
  require(
    core->nextEEProgramOrder != 0,
    "EE program-order counter is invalid");
  core->executingProgramOrder = 0;
  {
    auto operations = reader->scope("inFlightCOP1Operations");
    for (std::size_t index = 0;
         index < core->inFlightCOP1Operations.size();
         ++index)
  {
    auto element = reader->element(index);
    EECore::InFlightCOP1Operation &operation =
      core->inFlightCOP1Operations[index];
    operation = {};
    operation.active =
      reader->readFieldBool("active");
    operation.programOrder =
      reader->readFieldU64("programOrder");
    operation.stage =
      readEnum<EECore::COP1PipelineStage>(
        reader,
        static_cast<std::uint8_t>(
          EECore::COP1PipelineStage::S2),
        "stage");
    operation.instructionAddress =
      reader->readFieldU32("instructionAddress");
    const std::uint32_t instruction =
      reader->readFieldU32("instruction");
    operation.capturedFS = reader->readFieldU32("capturedFS");
    operation.capturedFT = reader->readFieldU32("capturedFT");
    operation.capturedAccumulator =
      reader->readFieldU32("capturedAccumulator");
    const std::uint32_t serializedControl =
      reader->readFieldU32("serializedControl");
    operation.capturedGPR =
      reader->readFieldU64("capturedGPR");
    operation.memoryAddress =
      reader->readFieldU32("memoryAddress");
    operation.capturedMemoryValue =
      reader->readFieldU32("capturedMemoryValue");
    operation.destination.mask =
      reader->readFieldU8("destinationMask");
    operation.destination.fprRegister =
      reader->readFieldU8("destinationFPR");
    operation.destination.gprRegister =
      reader->readFieldU8("destinationGPR");
    operation.rawResult = reader->readFieldU32("rawResult");
    operation.affectedFlags =
      reader->readFieldU8("affectedFlags");
    operation.raisedFlags =
      reader->readFieldU8("raisedFlags");
    operation.raisedStickyFlags =
      reader->readFieldU8("raisedStickyFlags");
    const bool serializedCondition =
      reader->readFieldBool("serializedCondition");
    operation.remainingCycles =
      reader->readFieldU8("remainingCycles");

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
  }
  core->packedMACContinuation = {};
  {
    auto continuation = reader->scope("packedMACContinuation");
    core->packedMACContinuation.initiationCycles =
      reader->readFieldU8("initiationCycles");
    auto operations = reader->scope("operations");
    for (std::size_t index = 0;
         index < core->packedMACContinuation.operations.size();
         ++index)
    {
      auto element = reader->element(index);
      EECore::InFlightPackedMACOperation &operation =
        core->packedMACContinuation.operations[index];
      operation.active = reader->readFieldBool("active");
      operation.operation =
        readEnum<EECore::PackedMACOperation>(
          reader,
          static_cast<std::uint8_t>(
            EECore::PackedMACOperation::
              HorizontalMultiplySubtractHalfword),
          "operation");
      operation.programOrder =
        reader->readFieldU64("programOrder");
      operation.source.low = reader->readFieldU64("sourceLow");
      operation.source.high = reader->readFieldU64("sourceHigh");
      operation.target.low = reader->readFieldU64("targetLow");
      operation.target.high = reader->readFieldU64("targetHigh");
      operation.hiResult.low =
        reader->readFieldU64("hiResultLow");
      operation.hiResult.high =
        reader->readFieldU64("hiResultHigh");
      operation.loResult.low =
        reader->readFieldU64("loResultLow");
      operation.loResult.high =
        reader->readFieldU64("loResultHigh");
      operation.destinationRegister =
        reader->readFieldU8("destinationRegister");
      operation.generalRegisterResult.low =
        reader->readFieldU64("generalRegisterResultLow");
      operation.generalRegisterResult.high =
        reader->readFieldU64("generalRegisterResultHigh");
      operation.remainingCycles =
        reader->readFieldU8("remainingCycles");
    }
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
  {
    auto continuation = reader->scope("packedDivideContinuation");
    core->packedDivideContinuation.active =
      reader->readFieldBool("active");
    core->packedDivideContinuation.operation =
      readEnum<EECore::PackedDivideOperation>(
        reader,
        static_cast<std::uint8_t>(
          EECore::PackedDivideOperation::DivideBroadcastWord),
        "operation");
    core->packedDivideContinuation.programOrder =
      reader->readFieldU64("programOrder");
    core->packedDivideContinuation.source.low =
      reader->readFieldU64("sourceLow");
    core->packedDivideContinuation.source.high =
      reader->readFieldU64("sourceHigh");
    core->packedDivideContinuation.target.low =
      reader->readFieldU64("targetLow");
    core->packedDivideContinuation.target.high =
      reader->readFieldU64("targetHigh");
    core->packedDivideContinuation.hiResult.low =
      reader->readFieldU64("hiResultLow");
    core->packedDivideContinuation.hiResult.high =
      reader->readFieldU64("hiResultHigh");
    core->packedDivideContinuation.loResult.low =
      reader->readFieldU64("loResultLow");
    core->packedDivideContinuation.loResult.high =
      reader->readFieldU64("loResultHigh");
    core->packedDivideContinuation.remainingCycles =
      reader->readFieldU8("remainingCycles");
  }
  const EECOP0Register memoryRegisters[] = {
    EECOP0Register::Index,
    EECOP0Register::Random,
    EECOP0Register::EntryLo0,
    EECOP0Register::EntryLo1,
    EECOP0Register::Context,
    EECOP0Register::PageMask,
    EECOP0Register::Wired,
    EECOP0Register::EntryHi,
    EECOP0Register::Config,
    EECOP0Register::TagLo,
    EECOP0Register::TagHi
  };
  {
    auto registers = reader->scope("memoryRegisters");
    for (std::size_t index = 0;
         index < sizeof(memoryRegisters) / sizeof(memoryRegisters[0]);
         ++index)
    {
      auto element = reader->element(index);
      const EECOP0Register registerIndex = memoryRegisters[index];
      const std::uint32_t value =
        reader->readFieldU32("value");
      require(
        EEMemorySystem::cop0RegisterStateValid(
          registerIndex,
          value),
        "EE memory-system COP0 register state is invalid");
      if (registerIndex == EECOP0Register::TagLo)
      {
        core->memorySystem.cop0TagLo = value;
        continue;
      }
      try
      {
        core->memorySystem.setCOP0Register(
          registerIndex,
          value);
      }
      catch (const std::invalid_argument &error)
      {
        throw std::runtime_error(error.what());
      }
    }
  }
  require(
    core->memorySystem.replacementStateValid(),
    "EE TLB Random/Wired replacement state is invalid");
  {
    auto entries = reader->scope("tlbEntries");
    for (std::size_t index = 0;
         index < EEMemorySystem::TLB_ENTRY_COUNT;
         ++index)
    {
      auto element = reader->element(index);
      const EETLBEntry entry = {
        reader->readFieldU32("pageMask"),
        reader->readFieldU32("entryHi"),
        {reader->readFieldU32("evenPage")},
        {reader->readFieldU32("oddPage")}
      };
      require(
        EEMemorySystem::pageMaskStateValid(entry.pageMask),
        "EE TLB PageMask state is invalid");
      require(
        EEMemorySystem::tlbEntryStateValid(entry),
        "EE TLB entry state is invalid");
      try
      {
        core->memorySystem.setTLBEntry(index, entry);
      }
      catch (const std::invalid_argument &error)
      {
        throw std::runtime_error(error.what());
      }
    }
  }
  const auto readCacheLine =
    [reader, &require](EECacheLine *line, EECacheKind kind)
    {
      reader->readFieldBytes(
        "data",
        line->data.data(),
        line->data.size());
      line->physicalTag =
        reader->readFieldU32("physicalTag");
      line->valid = reader->readFieldBool("valid");
      line->dirty = reader->readFieldBool("dirty");
      line->leastRecentlyFilled =
        reader->readFieldBool("leastRecentlyFilled");
      line->locked = reader->readFieldBool("locked");
      require(
        EEMemorySystem::cacheLineStateValid(*line, kind),
        kind == EECacheKind::Instruction
          ? "EE instruction-cache line state is invalid"
          : "EE data-cache line state is invalid");
    };
  {
    auto cache = reader->scope("instructionCache");
    for (std::size_t setIndex = 0;
         setIndex < core->memorySystem.instructionCache.size();
         ++setIndex)
    {
      auto set = reader->element(setIndex);
      for (std::size_t way = 0;
           way < core->memorySystem.instructionCache[setIndex].size();
           ++way)
      {
        auto line = reader->element(way);
        readCacheLine(
          &core->memorySystem.instructionCache[setIndex][way],
          EECacheKind::Instruction);
      }
    }
  }
  {
    auto cache = reader->scope("dataCache");
    for (std::size_t setIndex = 0;
         setIndex < core->memorySystem.dataCache.size();
         ++setIndex)
    {
      auto set = reader->element(setIndex);
      for (std::size_t way = 0;
           way < core->memorySystem.dataCache[setIndex].size();
           ++way)
      {
        auto line = reader->element(way);
        readCacheLine(
          &core->memorySystem.dataCache[setIndex][way],
          EECacheKind::Data);
      }
    }
  }
  require(
    core->memorySystem.stateValid(),
    "EE memory-system state is invalid");
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
    [&require](EECore::DecodedIssueLatch *latch,
       std::uint32_t instruction)
    {
      const auto translationFailureValid =
        [](EEAddressTranslationOutcome outcome)
        {
          switch (outcome)
          {
            case EEAddressTranslationOutcome::AddressErrorLoadOrFetch:
            case EEAddressTranslationOutcome::TLBRefillLoadOrFetch:
            case EEAddressTranslationOutcome::TLBInvalidLoadOrFetch:
            case EEAddressTranslationOutcome::
              UnsupportedScratchpadInstruction:
            case EEAddressTranslationOutcome::
              UnsupportedScratchpadPageSize:
            case EEAddressTranslationOutcome::
              UnsupportedCacheAttribute:
              return true;
            default:
              return false;
          }
        };
      latch->instruction = {};
      if (!latch->valid)
      {
        require(
          latch->address == 0 &&
            instruction == 0 &&
            latch->failure ==
              EECore::IssueLatchFailure::None &&
            latch->translationOutcome ==
              EEAddressTranslationOutcome::Translated,
          "EE inactive issue latch contains state");
        return;
      }

      if (latch->failure == EECore::IssueLatchFailure::None)
      {
        require(
          (latch->address & 3) == 0 &&
            latch->translationOutcome ==
              EEAddressTranslationOutcome::Translated,
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
          (latch->address & 3) != 0 &&
            instruction == 0 &&
            latch->translationOutcome ==
              EEAddressTranslationOutcome::Translated,
          "EE address-error issue latch is inconsistent");
        return;
      }
      if (latch->failure ==
            EECore::IssueLatchFailure::TranslationError)
      {
        require(
          (latch->address & 3) == 0 &&
            instruction == 0 &&
            translationFailureValid(
              latch->translationOutcome),
          "EE translation-error issue latch is inconsistent");
        return;
      }
      if (latch->failure == EECore::IssueLatchFailure::BusError)
      {
        require(
          (latch->address & 3) == 0 &&
            instruction == 0 &&
            latch->translationOutcome ==
              EEAddressTranslationOutcome::Translated,
          "EE bus-error issue latch is inconsistent");
        return;
      }

      require(
        (latch->address & 3) == 0 &&
          latch->translationOutcome ==
            EEAddressTranslationOutcome::Translated,
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
