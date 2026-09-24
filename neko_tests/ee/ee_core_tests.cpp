#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "ee_core.hpp"
#include "floating_point_ops.hpp"
#include "ee_instruction.hpp"
#include "neko_system.hpp"

static_assert(
  std::is_final<EECore>::value,
  "EE instruction execution must remain concrete and non-overridable.");
static_assert(
  std::is_enum<EEIssueWidth>::value,
  "EE issue width must remain an explicit control type.");
static_assert(
  std::is_enum<EEIssueMemberPosition>::value,
  "EE issue-member position must remain an explicit control type.");
static_assert(
  std::is_enum<EEAcceptanceMode>::value,
  "EE acceptance mode must remain an explicit control type.");

class NonCopyableIssueMemberAttempt
{
  public:
    NonCopyableIssueMemberAttempt() = default;
    NonCopyableIssueMemberAttempt(
      const NonCopyableIssueMemberAttempt &) = delete;
    NonCopyableIssueMemberAttempt &operator=(
      const NonCopyableIssueMemberAttempt &) = delete;

    EEIssueMemberOutcome operator()(EEIssueMemberPosition)
    {
      ++attempts;
      return EEIssueMemberOutcome::Accepted;
    }

    std::uint8_t attempts = 0;
};

struct EEIssuePreview
{
  std::uint8_t candidateCount = 0;
  EEIssueSelection selection;
};

struct EECoreTestAccess
{
  static bool youngerAStageContinuationActive(
    const EECore &core)
  {
    return core.youngerAStageContinuation.active;
  }

  static bool pendingMac1Active(const EECore &core)
  {
    return core.pendingMac1.active;
  }

  static std::size_t cycleEventCount(const EECore &core)
  {
    return core.cycleEventCount;
  }

  static bool instructionIssueEventMatches(
    const EECore &core,
    std::size_t index,
    std::uint32_t address,
    EEOperation operation)
  {
    return
      index < core.cycleEventCount &&
      core.cycleEvents[index].kind ==
        EECore::CycleEventKind::InstructionIssued &&
      core.cycleEvents[index].payload.instructionIssued.address ==
        address &&
      core.cycleEvents[index].payload.instructionIssued.operation ==
        operation;
  }

  static bool cycleEventsStartWithInstructionIssue(
    const EECore &core)
  {
    return
      core.cycleEventCount == 1 &&
      core.cycleEvents[0].kind ==
        EECore::CycleEventKind::InstructionIssued &&
      core.cycleEvents[0].payload.instructionIssued.address == 0 &&
      core.cycleEvents[0].payload.instructionIssued.instruction == 0 &&
      core.cycleEvents[0].payload.instructionIssued.operation ==
        EEOperation::Nop &&
      core.cycleEvents[0].payload.instructionIssued.mode ==
        EEAcceptanceMode::Ordinary;
  }

  static bool fillCycleEventCapacityInOrder(EECore *core)
  {
    core->cycleEventCount = 0;
    for (std::size_t index = 0;
         index < EECore::CYCLE_EVENT_CAPACITY;
         ++index)
    {
      core->recordCycleEvent(EECore::InstructionIssuedEvent{
        static_cast<std::uint32_t>(index),
        0,
        EEOperation::Nop,
        EEAcceptanceMode::Ordinary});
    }
    if (core->cycleEventCount !=
        EECore::CYCLE_EVENT_CAPACITY)
    {
      return false;
    }
    for (std::size_t index = 0;
         index < core->cycleEventCount;
         ++index)
    {
      if (core->cycleEvents[index]
            .payload.instructionIssued.address != index)
      {
        return false;
      }
    }
    return true;
  }

  static void appendCycleEvent(EECore *core)
  {
    core->recordCycleEvent(EECore::InstructionIssuedEvent{
      0,
      0,
      EEOperation::Nop,
      EEAcceptanceMode::Ordinary});
  }

  static void setInFlightCOP1ProgramOrder(
    EECore *core,
    std::size_t slot,
    bool active,
    std::uint64_t programOrder)
  {
    EECore::InFlightCOP1Operation &operation =
      core->inFlightCOP1Operations.at(slot);
    operation = {};
    operation.active = active;
    operation.programOrder = programOrder;
  }

  static std::array<std::uint64_t, 16>
    inFlightCOP1ProgramOrder(const EECore &core)
  {
    const EECore::COP1ProgramOrderView order =
      core.inFlightCOP1ProgramOrder();
    std::array<std::uint64_t, 16> result = {};
    for (std::size_t index = 0; index < order.size(); ++index)
    {
      result[index] =
        core.inFlightCOP1Operations[order[index]].programOrder;
    }
    return result;
  }

  static std::size_t inFlightCOP1OperationCount(
    const EECore &core)
  {
    return core.inFlightCOP1ProgramOrder().size();
  }

  static bool packedMACContinuationIsFixedCapacity(
    EECore *core)
  {
    static_assert(
      std::is_trivially_copyable<
        EECore::PackedMACContinuation>::value,
      "Packed MAC continuation must remain inline state.");
    static_assert(
      EECore::PackedMACContinuation::CAPACITY == 2,
      "Packed MAC continuation capacity must permit two overlaps.");

    EECore::PackedMACContinuation &continuation =
      core->packedMACContinuation;
    continuation = {};
    continuation.initiationCycles = 2;
    EECore::InFlightPackedMACOperation &operation =
      continuation.operations[1];
    operation.active = true;
    operation.operation =
      EECore::PackedMACOperation::MultiplyWord;
    operation.programOrder = 7;
    operation.source = {
      UINT64_C(0x0123456789abcdef),
      UINT64_C(0xfedcba9876543210)
    };
    operation.target = {
      UINT64_C(0x1111222233334444),
      UINT64_C(0xaaaabbbbccccdddd)
    };
    operation.hiResult = {
      UINT64_C(0x1020304050607080),
      UINT64_C(0x90a0b0c0d0e0f000)
    };
    operation.loResult = {
      UINT64_C(0x0011223344556677),
      UINT64_C(0x8899aabbccddeeff)
    };
    operation.destinationRegister = 9;
    operation.generalRegisterResult = {
      UINT64_C(0x13579bdf2468ace0),
      UINT64_C(0x0eca8642fdb97531)
    };
    operation.remainingCycles = 4;

    return
      continuation.operations.size() == 2 &&
      continuation.initiationCycles == 2 &&
      operation.active &&
      operation.operation ==
        EECore::PackedMACOperation::MultiplyWord &&
      operation.programOrder == 7 &&
      operation.source.low ==
        UINT64_C(0x0123456789abcdef) &&
      operation.source.high ==
        UINT64_C(0xfedcba9876543210) &&
      operation.target.low ==
        UINT64_C(0x1111222233334444) &&
      operation.target.high ==
        UINT64_C(0xaaaabbbbccccdddd) &&
      operation.hiResult.low ==
        UINT64_C(0x1020304050607080) &&
      operation.hiResult.high ==
        UINT64_C(0x90a0b0c0d0e0f000) &&
      operation.loResult.low ==
        UINT64_C(0x0011223344556677) &&
      operation.loResult.high ==
        UINT64_C(0x8899aabbccddeeff) &&
      operation.destinationRegister == 9 &&
      operation.generalRegisterResult.low ==
        UINT64_C(0x13579bdf2468ace0) &&
      operation.generalRegisterResult.high ==
        UINT64_C(0x0eca8642fdb97531) &&
      operation.remainingCycles == 4;
  }

  static bool packedMACContinuationClears(
    EECore *core)
  {
    const auto seed =
      [core]()
      {
        core->packedMACContinuation = {};
        core->packedMACContinuation.initiationCycles = 2;
        EECore::InFlightPackedMACOperation &operation =
          core->packedMACContinuation.operations[0];
        operation.active = true;
        operation.operation =
          EECore::PackedMACOperation::MultiplyUnsignedWord;
        operation.programOrder = 3;
        operation.remainingCycles = 4;
      };
    const auto cleared =
      [core]()
      {
        return
          core->packedMACContinuation.initiationCycles == 0 &&
          !core->packedMACContinuation.operations[0].active &&
          core->packedMACContinuation.operations[0].operation ==
            EECore::PackedMACOperation::None &&
          core->packedMACContinuation.operations[0].programOrder ==
            0 &&
          core->packedMACContinuation.operations[0]
            .remainingCycles == 0;
      };

    seed();
    core->resetExecutionContinuation();
    if (!cleared())
    {
      return false;
    }
    seed();
    core->reset();
    return cleared();
  }

  static bool packedMACTimingPermitsTwoOverlaps(
    EECore *core)
  {
    const auto start =
      [core](std::uint64_t programOrder)
      {
        core->executingProgramOrder = programOrder;
        core->startPackedMACOperation(
          EECore::PackedMACOperation::MultiplyWord,
          {},
          {},
          {},
          {},
          static_cast<std::uint8_t>(programOrder),
          {});
        core->executingProgramOrder = 0;
      };
    const auto activeCount =
      [core]()
      {
        return static_cast<std::size_t>(std::count_if(
          core->packedMACContinuation.operations.begin(),
          core->packedMACContinuation.operations.end(),
          [](const EECore::InFlightPackedMACOperation &operation)
          {
            return operation.active;
          }));
      };

    core->packedMACContinuation = {};
    start(1);
    if (core->packedMACAdmissionAvailable() ||
        activeCount() != 1 ||
        core->packedMACContinuation.initiationCycles != 2)
    {
      return false;
    }

    core->advancePackedMACContinuation();
    if (core->packedMACAdmissionAvailable() ||
        core->packedMACContinuation.operations[0]
          .remainingCycles != 3 ||
        core->packedMACContinuation.initiationCycles != 1)
    {
      return false;
    }

    core->advancePackedMACContinuation();
    if (!core->packedMACAdmissionAvailable() ||
        core->packedMACContinuation.operations[0]
          .remainingCycles != 2 ||
        core->packedMACContinuation.initiationCycles != 0)
    {
      return false;
    }
    start(2);
    if (core->packedMACAdmissionAvailable() ||
        activeCount() != 2)
    {
      return false;
    }

    core->advancePackedMACContinuation();
    core->advancePackedMACContinuation();
    if (!core->packedMACAdmissionAvailable() ||
        activeCount() != 1)
    {
      return false;
    }
    start(3);
    return
      activeCount() == 2 &&
      !core->packedMACAdmissionAvailable();
  }

  static bool packedMACAndScalarMACExcludeEachOther(
    EECore *core)
  {
    EEInstruction mac0;
    mac0.operation = EEOperation::MultiplyWord;
    mac0.sourceRegister = 1;
    mac0.targetRegister = 2;
    mac0.destinationRegister = 3;
    EEInstruction mac1 = mac0;
    mac1.operation = EEOperation::MultiplyWord1;
    EEInstruction unrelated;
    unrelated.operation = EEOperation::AddUnsignedWord;
    unrelated.sourceRegister = 4;
    unrelated.targetRegister = 5;
    unrelated.destinationRegister = 6;

    core->packedMACContinuation = {};
    core->pendingMac0 = {};
    core->pendingMac0.active = true;
    core->pendingMac0.remainingCycles = 4;
    core->pendingMac0.resultDestination =
      EECore::MACResultDestination::HIAndLOAndGPR;
    if (core->packedMACAdmissionAvailable())
    {
      return false;
    }
    core->pendingMac0 = {};
    core->pendingMac1 = {};
    core->pendingMac1.active = true;
    core->pendingMac1.remainingCycles = 4;
    core->pendingMac1.resultDestination =
      EECore::MACResultDestination::HIAndLOAndGPR;
    if (core->packedMACAdmissionAvailable())
    {
      return false;
    }

    core->pendingMac1 = {};
    core->executingProgramOrder = 1;
    core->startPackedMACOperation(
      EECore::PackedMACOperation::MultiplyWord,
      {},
      {},
      {},
      {},
      7,
      {});
    core->executingProgramOrder = 0;
    return
      !core->issueCandidateReady(mac0, 0, 16) &&
      !core->issueCandidateReady(mac1, 0, 16) &&
      core->issueCandidateReady(unrelated, 0, 16);
  }

  static bool packedMACAgesThroughYoungerAContinuation(
    EECore *core)
  {
    core->startExecution(0);
    core->executingProgramOrder = 1;
    core->startPackedMACOperation(
      EECore::PackedMACOperation::MultiplyWord,
      {},
      {},
      {},
      {},
      7,
      {});
    core->executingProgramOrder = 0;

    EEInstruction younger;
    younger.operation = EEOperation::AddUnsignedWord;
    younger.sourceRegister = 1;
    younger.targetRegister = 2;
    younger.destinationRegister = 3;
    core->youngerAStageContinuation = {
      true,
      2,
      4,
      younger
    };

    core->clock();
    return
      !core->youngerAStageContinuation.active &&
      core->packedMACContinuation.operations[0].active &&
      core->packedMACContinuation.operations[0]
        .remainingCycles == 3;
  }

  static bool packedMACRejectsYoungerMAC1Continuation(
    EECore *core)
  {
    core->startExecution(0);
    core->executingProgramOrder = 1;
    core->startPackedMACOperation(
      EECore::PackedMACOperation::MultiplyWord,
      {},
      {},
      {},
      {},
      7,
      {});
    core->executingProgramOrder = 0;

    core->issueLatch = {};
    core->issueLatch.valid = true;
    core->issueLatch.address = 4;
    core->issueLatch.instruction.operation =
      EEOperation::MultiplyWord1;
    core->nextEEProgramOrder = 2;
    const EEIssueMemberOutcome outcome =
      core->acceptYoungerAStageContinuation();
    return
      outcome == EEIssueMemberOutcome::Stalled &&
      core->issueLatch.valid &&
      !core->youngerAStageContinuation.active &&
      core->nextEEProgramOrder == 2;
  }

  static bool packedMACHashIsCanonical(EECore *core)
  {
    core->packedMACContinuation = {};
    core->nextEEProgramOrder = 3;
    core->packedMACContinuation.initiationCycles = 2;
    EECore::InFlightPackedMACOperation &older =
      core->packedMACContinuation.operations[0];
    older.active = true;
    older.operation =
      EECore::PackedMACOperation::MultiplyWord;
    older.programOrder = 1;
    older.source = {2, 3};
    older.target = {4, 5};
    older.hiResult = {};
    older.loResult = {8, 15};
    older.destinationRegister = 3;
    older.generalRegisterResult = {8, 15};
    older.remainingCycles = 2;
    EECore::InFlightPackedMACOperation &newer =
      core->packedMACContinuation.operations[1];
    newer.active = true;
    newer.operation =
      EECore::PackedMACOperation::MultiplyAddWord;
    newer.programOrder = 2;
    newer.source = {1, 1};
    newer.target = {2, 2};
    newer.hiResult = {};
    newer.loResult = {10, 17};
    newer.destinationRegister = 4;
    newer.generalRegisterResult = {10, 17};
    newer.remainingCycles = 4;

    const std::uint64_t orderedHash = core->stateHash();
    std::swap(
      core->packedMACContinuation.operations[0],
      core->packedMACContinuation.operations[1]);
    const std::uint64_t swappedHash = core->stateHash();
    core->packedMACContinuation.operations[0].source.low = 6;
    const std::uint64_t changedHash = core->stateHash();
    return
      orderedHash == swappedHash &&
      swappedHash != changedHash;
  }

  static void startPackedMACWithoutAssignedOrder(
    EECore *core)
  {
    core->startPackedMACOperation(
      EECore::PackedMACOperation::MultiplyWord,
      {},
      {},
      {},
      {},
      0,
      {});
  }

  static void allocateCOP1WithoutAssignedOrder(EECore *core)
  {
    EEInstruction instruction;
    instruction.operation = EEOperation::AddSingleCOP1;
    core->allocateInFlightCOP1(instruction, 0);
  }

  static bool completionMatchesCOP1Family(
    EECore *core,
    EEOperation operation)
  {
    EECore::InFlightCOP1Operation pending;
    pending.active = true;
    pending.programOrder = 1;
    pending.instruction.operation = operation;
    const bool operateFamily =
      isCOP1StagedOperation(operation) ||
      isCOP1DividerOperation(operation);
    if (isCOP1StagedOperation(operation))
    {
      pending.stage = EECore::COP1PipelineStage::Z;
    }
    else if (isCOP1RegisterMoveOperation(operation) ||
             isCOP1MemoryMoveOperation(operation))
    {
      pending.stage = EECore::COP1PipelineStage::X;
    }
    core->completeInFlightCOP1(
      &pending,
      EECore::COP1CompletionReason::PipelineAdvance);
    return pending.stage ==
      (operateFamily
        ? EECore::COP1PipelineStage::S1
        : EECore::COP1PipelineStage::Y);
  }

  static void commitPendingCOP1(EECore *core)
  {
    EECore::InFlightCOP1Operation pending;
    pending.active = true;
    pending.programOrder = 1;
    pending.instruction.operation = EEOperation::AddSingleCOP1;
    core->commitInFlightCOP1(&pending);
  }

  static void completeCOP1BeforeFinalStage(EECore *core)
  {
    EECore::InFlightCOP1Operation pending;
    pending.active = true;
    pending.programOrder = 1;
    pending.stage = EECore::COP1PipelineStage::X;
    pending.instruction.operation = EEOperation::AddSingleCOP1;
    core->completeInFlightCOP1(
      &pending,
      EECore::COP1CompletionReason::PipelineAdvance);
  }

  static void releaseInactiveCOP1(EECore *core)
  {
    EECore::InFlightCOP1Operation inactive;
    core->releaseInFlightCOP1(&inactive);
  }

  static bool releaseCOP1ClearsSlot(EECore *core)
  {
    EECore::InFlightCOP1Operation pending;
    pending.active = true;
    pending.programOrder = 7;
    pending.stage = EECore::COP1PipelineStage::Y;
    pending.instruction.operation = EEOperation::MoveWordToCOP1;
    pending.rawResult = UINT32_C(0x12345678);
    core->releaseInFlightCOP1(&pending);
    return
      !pending.active &&
      pending.programOrder == 0 &&
      pending.stage == EECore::COP1PipelineStage::R &&
      pending.instruction.operation == EEOperation::Nop &&
      pending.rawResult == 0;
  }

  static bool cancelCOP1ReconcilesDividerOccupancy(
    EECore *core)
  {
    EECore::InFlightCOP1Operation &divider =
      core->inFlightCOP1Operations[0];
    divider.active = true;
    divider.programOrder = 2;
    divider.instruction.operation =
      EEOperation::DivideSingleCOP1;
    divider.remainingCycles = 5;
    core->cop1DividerInitiationCycles = 4;
    core->cop1DividerOperation =
      EEOperation::DivideSingleCOP1;

    core->cancelInFlightCOP1(
      EECore::COP1CancellationScope::AtOrAfter,
      2);
    return
      !divider.active &&
      core->cop1DividerInitiationCycles == 0 &&
      core->cop1DividerOperation == EEOperation::Nop;
  }

  static bool reconcileCOP1DerivesDividerOccupancy(
    EECore *core)
  {
    EECore::InFlightCOP1Operation &divider =
      core->inFlightCOP1Operations[0];
    divider.active = true;
    divider.programOrder = 1;
    divider.instruction.operation =
      EEOperation::DivideSingleCOP1;
    divider.remainingCycles = 5;
    core->cop1DividerInitiationCycles = 1;
    core->cop1DividerOperation =
      EEOperation::SquareRootSingleCOP1;

    core->reconcileCOP1DividerOccupancy();
    return
      core->cop1DividerInitiationCycles == 4 &&
      core->cop1DividerOperation ==
        EEOperation::DivideSingleCOP1;
  }

  static void setCOP1DividerOccupancyCache(
    EECore *core,
    std::uint8_t initiationCycles,
    EEOperation operation)
  {
    core->cop1DividerInitiationCycles = initiationCycles;
    core->cop1DividerOperation = operation;
  }

  static void reconcileCOP1DividerOccupancy(EECore *core)
  {
    core->reconcileCOP1DividerOccupancy();
  }

  static bool cancelCOP1PreservesRequestedBoundary(
    EECore *core,
    bool preserveBoundary)
  {
    core->inFlightCOP1Operations.fill({});
    for (std::size_t index = 0; index < 3; ++index)
    {
      EECore::InFlightCOP1Operation &operation =
        core->inFlightCOP1Operations[index];
      operation.active = true;
      operation.programOrder = index + 1;
      operation.instruction.operation =
        EEOperation::AddSingleCOP1;
      operation.stage = EECore::COP1PipelineStage::S1;
      operation.destination.mask =
        EECore::COP1_DESTINATION_FPR;
      operation.destination.fprRegister =
        static_cast<std::uint8_t>(index + 4);
      operation.rawResult =
        UINT32_C(0x3f800000) +
        static_cast<std::uint32_t>(index);
    }

    core->cancelInFlightCOP1(
      preserveBoundary
        ? EECore::COP1CancellationScope::After
        : EECore::COP1CancellationScope::AtOrAfter,
      2);
    const bool boundaryRemains = preserveBoundary;
    const bool preserved =
      core->inFlightCOP1Operations[0].active &&
      core->inFlightCOP1Operations[1].active ==
        boundaryRemains &&
      !core->inFlightCOP1Operations[2].active;
    core->retireReadyInFlightCOP1();
    return
      preserved &&
      core->floatingPointRegisters[4] ==
        UINT32_C(0x3f800000) &&
      core->floatingPointRegisters[5] ==
        (boundaryRemains
          ? UINT32_C(0x3f800001)
          : 0) &&
      core->floatingPointRegisters[6] == 0;
  }

  static void cancelCOP1WithoutAssignedOrder(EECore *core)
  {
    core->cancelInFlightCOP1(
      EECore::COP1CancellationScope::AtOrAfter);
  }

  static bool cancelAllCOP1(EECore *core)
  {
    for (std::size_t index = 0; index < 2; ++index)
    {
      EECore::InFlightCOP1Operation &operation =
        core->inFlightCOP1Operations[index];
      operation.active = true;
      operation.programOrder = index + 1;
      operation.instruction.operation =
        index == 0
          ? EEOperation::AddSingleCOP1
          : EEOperation::DivideSingleCOP1;
      operation.remainingCycles =
        index == 0 ? 0 : 5;
    }
    core->reconcileCOP1DividerOccupancy();

    core->cancelInFlightCOP1(
      EECore::COP1CancellationScope::All);
    return
      !core->inFlightCOP1Operations[0].active &&
      !core->inFlightCOP1Operations[1].active &&
      core->cop1DividerInitiationCycles == 0 &&
      core->cop1DividerOperation == EEOperation::Nop;
  }

  static EEIssuePreview previewIssueSelection(EECore *core)
  {
    core->fillIssueFrontEnd();
    const EECore::IssueCandidates candidates =
      core->constructIssueCandidates();
    const EECore::IssueCandidateReadiness readiness =
      core->evaluateIssueCandidateReadiness(candidates, 0);
    return {
      candidates.count(),
      core->selectReadyIssueCandidates(
        candidates,
        readiness)
    };
  }

  static EEIssueGroupExecutionResult executeIssueGroup(
    EECore *core,
    EEIssueWidth width)
  {
    core->acceptanceRecords.clear();
    core->fillIssueFrontEnd();
    return core->executeIssueGroup(width, 0);
  }

  static EEInstructionExecutionOutcome executeInstruction(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeInstruction(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeWordShift(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeWordShift(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeByteMemory(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeByteMemory(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeHalfwordMemory(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeHalfwordMemory(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeWordMemory(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeWordMemory(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeWordMergeMemory(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeWordMergeMemory(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeDoublewordMemory(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeDoublewordMemory(instruction, 0);
  }

  static EEInstructionExecutionOutcome
    executeDoublewordMergeMemory(
      EECore *core,
      const EEInstruction &instruction)
  {
    return core->executeDoublewordMergeMemory(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeQuadwordMemory(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeQuadwordMemory(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeWordArithmetic(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeWordArithmetic(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeDoublewordArithmetic(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeDoublewordArithmetic(instruction, 0);
  }

  static EEInstructionExecutionOutcome
    executeImmediateWordArithmetic(
      EECore *core,
      const EEInstruction &instruction)
  {
    return core->executeImmediateWordArithmetic(instruction, 0);
  }

  static EEInstructionExecutionOutcome
    executeImmediateDoublewordArithmetic(
      EECore *core,
      const EEInstruction &instruction)
  {
    return core->executeImmediateDoublewordArithmetic(
      instruction,
      0);
  }

  static EEInstructionExecutionOutcome executeMACRegisterMove(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeMACRegisterMove(instruction);
  }

  static EEInstructionExecutionOutcome executeShiftAmountOperation(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeShiftAmountOperation(instruction);
  }

  static EEInstructionExecutionOutcome executeExceptionReturn(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeExceptionReturn(instruction);
  }

  static EEInstructionExecutionOutcome executeSoftwareException(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeSoftwareException(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeCOP1Memory(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeCOP1Memory(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeCOP1RegisterMove(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeCOP1RegisterMove(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeCOP1Divider(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeCOP1Divider(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeCOP1StagedOperation(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeCOP1StagedOperation(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeCOP1Branch(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeCOP1Branch(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeCOP2VectorMove(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeCOP2VectorMove(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeCOP2Memory(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeCOP2Memory(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeCOP2ControlMove(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeCOP2ControlMove(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeCOP2MicroCall(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeCOP2MicroCall(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeCOP2Macro(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeCOP2Macro(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeCOP2Branch(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeCOP2Branch(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeJump(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeJump(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeIntegerBranch(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeIntegerBranch(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeMultiply(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeMultiply(instruction, 0);
  }

  static EEInstructionExecutionOutcome executeDivide(
    EECore *core,
    const EEInstruction &instruction)
  {
    return core->executeDivide(instruction, 0);
  }

  static void setShiftAmountOrdering(
    EECore *core,
    std::uint8_t accesses,
    std::uint8_t reads)
  {
    core->shiftAmountOrdering.restore(accesses, reads);
  }

  static bool continuationInvariantPredicatesArePure(
    EECore *core)
  {
    core->state = EEExecutionState::Halted;
    core->haltReason = EEStopReason::HostHalt;
    core->nextEEProgramOrder = 3;

    core->pendingMac0 = {};
    core->pendingMac0.active = true;
    core->pendingMac0.remainingCycles = 4;
    core->pendingMac0.resultDestination =
      EECore::MACResultDestination::HIAndLOAndGPR;
    core->pendingMac0.generalRegister = 2;
    core->pendingMac1 = core->pendingMac0;
    core->pendingMac1.generalRegister = 3;

    core->inFlightCOP1Operations.fill({});
    for (std::size_t index = 0; index < 2; ++index)
    {
      EECore::InFlightCOP1Operation &operation =
        core->inFlightCOP1Operations[index];
      operation.active = true;
      operation.programOrder = index + 1;
      operation.instruction.operation =
        EEOperation::DivideSingleCOP1;
      operation.stage = EECore::COP1PipelineStage::R;
      operation.destination.fprRegister =
        static_cast<std::uint8_t>(index + 4);
      operation.remainingCycles = index == 0 ? 1 : 8;
    }
    core->cop1DividerInitiationCycles = 7;
    core->cop1DividerOperation =
      EEOperation::DivideSingleCOP1;

    const std::uint64_t stateHashBefore = core->stateHash();
    const std::uint8_t mac0CyclesBefore =
      core->pendingMac0.remainingCycles;
    const std::uint8_t mac1RegisterBefore =
      core->pendingMac1.generalRegister;
    const std::uint64_t firstProgramOrderBefore =
      core->inFlightCOP1Operations[0].programOrder;
    const std::uint8_t dividerCyclesBefore =
      core->cop1DividerInitiationCycles;
    const EECore::COP1ProgramOrderView order =
      core->inFlightCOP1ProgramOrder();
    const bool valid =
      EEShiftAmountOrderingWindow::historyBitsValid(7, 3) &&
      EEShiftAmountOrderingWindow::readHistoryConsistent(7, 3) &&
      EECore::pendingMultiplyDivideLatencyValid(
        core->pendingMac0) &&
      EECore::pendingMultiplyDivideRegisterValid(
        core->pendingMac0) &&
      EECore::pendingMultiplyDivideDestinationValid(
        core->pendingMac0) &&
      core->concurrentMultiplyDivideExecutionStateValid() &&
      core->concurrentMultiplyDivideLatenciesValid() &&
      core->concurrentMultiplyDestinationsValid() &&
      core->cop1ProgramOrderInRange(
        core->inFlightCOP1Operations[0]) &&
      core->cop1ProgramOrderUnique(order) &&
      core->cop1DividerResultCountValid(order) &&
      core->cop1DividerOverlapValid(order) &&
      EECore::cop1DividerInitiationIntervalValid({
        core->cop1DividerInitiationCycles,
        core->cop1DividerOperation
      }) &&
      EECore::cop1DividerOperationPresenceValid({
        core->cop1DividerInitiationCycles,
        core->cop1DividerOperation
      }) &&
      EECore::cop1DividerOperationFamilyValid({
        core->cop1DividerInitiationCycles,
        core->cop1DividerOperation
      }) &&
      core->cop1DividerOccupancyConsistent() &&
      core->stagedCOP1PipelineOrderValid(order);
    return
      valid &&
      core->stateHash() == stateHashBefore &&
      core->pendingMac0.remainingCycles ==
        mac0CyclesBefore &&
      core->pendingMac1.generalRegister ==
        mac1RegisterBefore &&
      core->inFlightCOP1Operations[0].programOrder ==
        firstProgramOrderBefore &&
      core->cop1DividerInitiationCycles ==
        dividerCyclesBefore;
  }
};

TEST_CASE(
  "EE architectural events are produced independently of trace collection")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  system.eeBus().write32(0, 0);
  core.startExecution(0);

  core.clock();

  REQUIRE_FALSE(system.traceEnabled());
  REQUIRE(system.trace().empty());
  REQUIRE(
    EECoreTestAccess::cycleEventsStartWithInstructionIssue(
      core));
}

TEST_CASE("EE cycle event storage is fixed-capacity and ordered")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  REQUIRE(
    EECoreTestAccess::fillCycleEventCapacityInOrder(
      &core));
  REQUIRE_THROWS_WITH(
    EECoreTestAccess::appendCycleEvent(&core),
    "EE produced too many cycle events.");
}

TEST_CASE("EE in-flight COP1 program order is allocation-free and slot-independent")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  EECoreTestAccess::setInFlightCOP1ProgramOrder(
    &core, 1, true, 9);
  EECoreTestAccess::setInFlightCOP1ProgramOrder(
    &core, 4, true, 2);
  EECoreTestAccess::setInFlightCOP1ProgramOrder(
    &core, 8, false, 1);
  EECoreTestAccess::setInFlightCOP1ProgramOrder(
    &core, 12, true, 7);

  REQUIRE(
    EECoreTestAccess::inFlightCOP1OperationCount(core) == 3);
  const std::array<std::uint64_t, 16> order =
    EECoreTestAccess::inFlightCOP1ProgramOrder(core);
  REQUIRE(order[0] == 2);
  REQUIRE(order[1] == 7);
  REQUIRE(order[2] == 9);
}

TEST_CASE("EE packed MAC continuation is fixed-capacity inline state")
{
  NekoSystem system;

  REQUIRE(
    EECoreTestAccess::packedMACContinuationIsFixedCapacity(
      &system.eeCore()));
}

TEST_CASE("EE packed MAC continuation clears across restart and reset")
{
  NekoSystem system;

  REQUIRE(
    EECoreTestAccess::packedMACContinuationClears(
      &system.eeCore()));
}

TEST_CASE("EE packed MAC timing permits two overlapping operations")
{
  NekoSystem system;

  REQUIRE(
    EECoreTestAccess::packedMACTimingPermitsTwoOverlaps(
      &system.eeCore()));
}

TEST_CASE("EE packed and scalar MAC continuations exclude each other")
{
  NekoSystem system;

  REQUIRE(
    EECoreTestAccess::packedMACAndScalarMACExcludeEachOther(
      &system.eeCore()));
  REQUIRE_THROWS_WITH(
    EECoreTestAccess::startPackedMACWithoutAssignedOrder(
      &system.eeCore()),
    "EE packed MAC allocation requires assigned program order.");
}

TEST_CASE("EE packed MAC continuation hash is canonical")
{
  NekoSystem system;

  REQUIRE(
    EECoreTestAccess::packedMACHashIsCanonical(
      &system.eeCore()));
}

TEST_CASE("EE packed MAC ages through a younger A-stage continuation")
{
  NekoSystem system;

  REQUIRE(
    EECoreTestAccess::packedMACAgesThroughYoungerAContinuation(
      &system.eeCore()));
}

TEST_CASE("EE packed MAC rejects a younger MAC1 continuation")
{
  NekoSystem system;

  REQUIRE(
    EECoreTestAccess::packedMACRejectsYoungerMAC1Continuation(
      &system.eeCore()));
}

TEST_CASE("EE in-flight COP1 lifecycle gates invalid transitions")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  REQUIRE_THROWS_WITH(
    EECoreTestAccess::allocateCOP1WithoutAssignedOrder(&core),
    "EE COP1 allocation requires assigned program order.");
  REQUIRE(
    EECoreTestAccess::completionMatchesCOP1Family(
      &core,
      EEOperation::AddSingleCOP1));
  REQUIRE(
    EECoreTestAccess::completionMatchesCOP1Family(
      &core,
      EEOperation::DivideSingleCOP1));
  REQUIRE(
    EECoreTestAccess::completionMatchesCOP1Family(
      &core,
      EEOperation::MoveWordToCOP1));
  REQUIRE(
    EECoreTestAccess::completionMatchesCOP1Family(
      &core,
      EEOperation::LoadWordToCOP1));
  REQUIRE_THROWS_WITH(
    EECoreTestAccess::completionMatchesCOP1Family(
      &core,
      EEOperation::Nop),
    "EE COP1 completion received an unmanaged operation.");
  REQUIRE_THROWS_WITH(
    EECoreTestAccess::commitPendingCOP1(&core),
    "EE COP1 commit requires completed work.");
  REQUIRE_THROWS_WITH(
    EECoreTestAccess::completeCOP1BeforeFinalStage(&core),
    "EE COP1 pipeline completion requires final-stage work.");
  REQUIRE_THROWS_WITH(
    EECoreTestAccess::releaseInactiveCOP1(&core),
    "EE COP1 release requires active work.");
  REQUIRE(EECoreTestAccess::releaseCOP1ClearsSlot(&core));
  REQUIRE(
    EECoreTestAccess::cancelCOP1ReconcilesDividerOccupancy(
      &core));
  REQUIRE(
    EECoreTestAccess::reconcileCOP1DerivesDividerOccupancy(
      &core));
  const std::uint64_t canonicalHash = core.stateHash();
  const std::vector<std::uint8_t> canonicalSaveState =
    system.saveState();
  EECoreTestAccess::setCOP1DividerOccupancyCache(
    &core,
    1,
    EEOperation::SquareRootSingleCOP1);
  REQUIRE(core.stateHash() == canonicalHash);
  REQUIRE(system.saveState() == canonicalSaveState);
  EECoreTestAccess::reconcileCOP1DividerOccupancy(&core);
}

TEST_CASE("EE continuation invariant predicates are pure")
{
  NekoSystem system;
  REQUIRE(
    EECoreTestAccess::continuationInvariantPredicatesArePure(
      &system.eeCore()));
  REQUIRE(
    EEShiftAmountOrderingWindow::historyBitsValid(7, 3));
  REQUIRE(
    EEShiftAmountOrderingWindow::readHistoryConsistent(7, 3));
  REQUIRE_FALSE(
    EEShiftAmountOrderingWindow::readHistoryConsistent(1, 2));
}

TEST_CASE("EE COP1 cancellation owns program-order boundaries")
{
  SECTION("Exceptions discard the boundary and younger work")
  {
    NekoSystem system;
    REQUIRE(
      EECoreTestAccess::cancelCOP1PreservesRequestedBoundary(
        &system.eeCore(),
        false));
  }

  SECTION("Redirects preserve the boundary and discard younger work")
  {
    NekoSystem system;
    REQUIRE(
      EECoreTestAccess::cancelCOP1PreservesRequestedBoundary(
        &system.eeCore(),
        true));
  }

  SECTION("Cancellation requires an architectural boundary")
  {
    NekoSystem system;
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::cancelCOP1WithoutAssignedOrder(
        &system.eeCore()),
      "EE COP1 cancellation requires assigned program order.");
  }

  SECTION("Full continuation flushes discard all work")
  {
    NekoSystem system;
    REQUIRE(EECoreTestAccess::cancelAllCOP1(&system.eeCore()));
  }
}

TEST_CASE("EE focused handlers reject incompatible operations")
{
  SECTION("Word shifts reject before word-state validation")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.startExecution(0);
    core.setGeneralRegister(
      2,
      {UINT64_C(0x123456789abcdef0), 0});
    EEInstruction instruction;
    instruction.operation = EEOperation::AddWord;
    instruction.targetRegister = 2;

    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeWordShift(
        &core,
        instruction),
      "EE word-shift handler received an incompatible operation.");
    REQUIRE(core.executionState() == EEExecutionState::Running);
    REQUIRE(core.stopReason() == EEStopReason::None);
  }

  SECTION("Scalar memory rejects before bus or register effects")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.startExecution(0);
    core.setGeneralRegister(2, {0x55, 0});
    EEInstruction instruction;
    instruction.operation = EEOperation::LoadHalfword;
    instruction.targetRegister = 2;

    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeByteMemory(
        &core,
        instruction),
      "EE byte-memory handler received an incompatible operation.");
    instruction.operation = EEOperation::LoadByte;
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeHalfwordMemory(
        &core,
        instruction),
      "EE halfword-memory handler received an incompatible "
      "operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeWordMemory(
        &core,
        instruction),
      "EE word-memory handler received an incompatible operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeWordMergeMemory(
        &core,
        instruction),
      "EE word-merge-memory handler received an incompatible "
      "operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeDoublewordMemory(
        &core,
        instruction),
      "EE doubleword-memory handler received an incompatible "
      "operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeDoublewordMergeMemory(
        &core,
        instruction),
      "EE doubleword-merge-memory handler received an incompatible "
      "operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeQuadwordMemory(
        &core,
        instruction),
      "EE quadword-memory handler received an incompatible "
      "operation.");
    REQUIRE(core.generalRegister(2).low == 0x55);
    REQUIRE(core.pendingException() == EEException::None);
  }

  SECTION("Lifecycle handlers reject before architectural effects")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.startExecution(0);
    core.setGeneralRegister(
      2,
      {UINT64_C(0x123456789abcdef0), 1});
    core.setGeneralRegister(3, {0x55, 0});
    EEInstruction instruction;
    instruction.operation = EEOperation::AddWord;
    instruction.sourceRegister = 2;
    instruction.targetRegister = 2;
    instruction.destinationRegister = 3;

    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeExceptionReturn(&core, instruction),
      "EE exception-return handler received an incompatible "
      "operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeSoftwareException(&core, instruction),
      "EE software-exception handler received an incompatible "
      "operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeCOP1Memory(&core, instruction),
      "EE COP1-memory handler received an incompatible operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeCOP1RegisterMove(
        &core,
        instruction),
      "EE COP1-register-move handler received an incompatible "
      "operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeCOP1Divider(&core, instruction),
      "EE COP1-divider handler received an incompatible operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeCOP1StagedOperation(
        &core,
        instruction),
      "EE COP1-staged-operation handler received an incompatible "
      "operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeCOP1Branch(&core, instruction),
      "EE COP1-branch handler received an incompatible operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeCOP2VectorMove(&core, instruction),
      "EE COP2-vector-move handler received an incompatible "
      "operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeCOP2Memory(&core, instruction),
      "EE COP2-memory handler received an incompatible operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeCOP2ControlMove(
        &core,
        instruction),
      "EE COP2-control-move handler received an incompatible "
      "operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeCOP2MicroCall(&core, instruction),
      "EE COP2-micro-call handler received an incompatible "
      "operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeCOP2Macro(&core, instruction),
      "EE COP2-macro handler received an incompatible operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeCOP2Branch(&core, instruction),
      "EE COP2-branch handler received an incompatible operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeJump(&core, instruction),
      "EE jump handler received an incompatible operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeIntegerBranch(&core, instruction),
      "EE integer-branch handler received an incompatible "
      "operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeMultiply(&core, instruction),
      "EE multiply handler received an incompatible operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeDivide(&core, instruction),
      "EE divide handler received an incompatible operation.");

    REQUIRE(core.executionState() == EEExecutionState::Running);
    REQUIRE(core.stopReason() == EEStopReason::None);
    REQUIRE(core.pendingException() == EEException::None);
    REQUIRE(core.generalRegister(3).low == 0x55);
  }

  SECTION("Scalar state handlers reject before architectural effects")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.startExecution(0);
    core.setGeneralRegister(
      2,
      {UINT64_C(0x123456789abcdef0), 1});
    core.setGeneralRegister(3, {0x55, 0});
    core.setHI(0x11);
    core.setLO(0x22);
    core.setShiftAmount(7);
    EEInstruction instruction;
    instruction.operation = EEOperation::And;
    instruction.sourceRegister = 2;
    instruction.targetRegister = 2;
    instruction.destinationRegister = 3;

    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeWordArithmetic(&core, instruction),
      "EE word-arithmetic handler received an incompatible "
      "operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeDoublewordArithmetic(
        &core,
        instruction),
      "EE doubleword-arithmetic handler received an incompatible "
      "operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeImmediateWordArithmetic(
        &core,
        instruction),
      "EE immediate-word-arithmetic handler received an "
      "incompatible operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeImmediateDoublewordArithmetic(
        &core,
        instruction),
      "EE immediate-doubleword-arithmetic handler received an "
      "incompatible operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeMACRegisterMove(&core, instruction),
      "EE MAC-register-move handler received an incompatible "
      "operation.");
    REQUIRE_THROWS_WITH(
      EECoreTestAccess::executeShiftAmountOperation(
        &core,
        instruction),
      "EE shift-amount handler received an incompatible operation.");

    REQUIRE(core.executionState() == EEExecutionState::Running);
    REQUIRE(core.stopReason() == EEStopReason::None);
    REQUIRE(core.pendingException() == EEException::None);
    REQUIRE(core.generalRegister(3).low == 0x55);
    REQUIRE(core.hi() == 0x11);
    REQUIRE(core.lo() == 0x22);
    REQUIRE(core.shiftAmount() == 7);
  }
}

TEST_CASE("EE instruction execution reports explicit outcomes")
{
  SECTION("Completed work")
  {
    NekoSystem system;
    system.eeCore().startExecution(0);
    EEInstruction instruction;
    instruction.operation = EEOperation::Nop;
    REQUIRE(
      EECoreTestAccess::executeInstruction(
        &system.eeCore(),
        instruction) ==
      EEInstructionExecutionOutcome::Completed);
  }

  SECTION("Delayed work")
  {
    NekoSystem system;
    system.eeCore().startExecution(0);
    system.vu0().startMicroMode(0);
    EEInstruction instruction;
    instruction.operation = EEOperation::QuadwordMoveToCOP2;
    instruction.raw = 1;
    REQUIRE(
      EECoreTestAccess::executeInstruction(
        &system.eeCore(),
        instruction) ==
      EEInstructionExecutionOutcome::Delayed);
  }

  SECTION("Faulted work")
  {
    NekoSystem system;
    system.eeCore().startExecution(0);
    EEInstruction instruction;
    instruction.operation = EEOperation::SystemCall;
    REQUIRE(
      EECoreTestAccess::executeInstruction(
        &system.eeCore(),
        instruction) ==
      EEInstructionExecutionOutcome::Faulted);
  }

  SECTION("Halted work")
  {
    NekoSystem system;
    system.eeCore().startExecution(0);
    EEInstruction instruction;
    instruction.operation = EEOperation::DivideWord;
    REQUIRE(
      EECoreTestAccess::executeInstruction(
        &system.eeCore(),
        instruction) ==
      EEInstructionExecutionOutcome::Halted);
  }

  SECTION("Rejected work")
  {
    NekoSystem system;
    system.eeCore().startExecution(0);
    EECoreTestAccess::setShiftAmountOrdering(
      &system.eeCore(),
      1,
      1);
    EEInstruction instruction;
    instruction.operation =
      EEOperation::MoveByteCountToShiftAmount;
    REQUIRE(
      EECoreTestAccess::executeInstruction(
        &system.eeCore(),
        instruction) ==
      EEInstructionExecutionOutcome::Rejected);
  }
}

TEST_CASE("EE acceptance records preserve issue-group order")
{
  EEAcceptanceRecords records;
  REQUIRE(records.size() == 0);

  records.append({
    7,
    0x100,
    decodeEEInstruction(UINT32_C(0x24020001)),
    EEAcceptanceMode::Ordinary
  });
  records.append({
    8,
    0x104,
    decodeEEInstruction(UINT32_C(0x24030002)),
    EEAcceptanceMode::DelaySlot
  });

  REQUIRE(records.size() == 2);
  REQUIRE(records.instructionCount() == 2);
  REQUIRE(records[0].programOrder == 7);
  REQUIRE(records[0].address == 0x100);
  REQUIRE(records[0].instruction.raw == 0x24020001);
  REQUIRE(records[0].mode == EEAcceptanceMode::Ordinary);
  REQUIRE(records[1].programOrder == 8);
  REQUIRE(records[1].address == 0x104);
  REQUIRE(records[1].instruction.raw == 0x24030002);
  REQUIRE(records[1].mode == EEAcceptanceMode::DelaySlot);
  REQUIRE_THROWS_AS(
    records.append({
      9,
      0x108,
      decodeEEInstruction(0),
      EEAcceptanceMode::Ordinary
    }),
    std::overflow_error);

  records.clear();
  REQUIRE(records.size() == 0);

  records.append({
    4,
    0x200,
    decodeEEInstruction(0),
    EEAcceptanceMode::Ordinary
  });
  REQUIRE_THROWS_AS(
    records.append({
      3,
      0x204,
      decodeEEInstruction(0),
      EEAcceptanceMode::Ordinary
    }),
    std::invalid_argument);
}

TEST_CASE("EE issue groups stop at precise member boundaries")
{
  SECTION("One-wide issue attempts only the older member")
  {
    std::uint8_t attempts = 0;
    const EEIssueGroupExecutionResult result =
      executeEEIssueGroupMembers(
        EEIssueWidth::One,
        [&attempts](EEIssueMemberPosition position)
        {
          ++attempts;
          REQUIRE(
            position == EEIssueMemberPosition::Older);
          return EEIssueMemberOutcome::Accepted;
        });

    REQUIRE(attempts == 1);
    REQUIRE(result.width == EEIssueWidth::One);
    REQUIRE(result.attempted == 1);
    REQUIRE(result.accepted == 1);
    REQUIRE(
      result.older ==
      EEIssueMemberOutcome::Accepted);
    REQUIRE(
      result.younger ==
      EEIssueMemberOutcome::Cancelled);
  }

  SECTION("Invalid issue widths are rejected")
  {
    NonCopyableIssueMemberAttempt attempt;
    REQUIRE_THROWS_WITH(
      executeEEIssueGroupMembers(
        static_cast<EEIssueWidth>(0),
        attempt),
      "EE issue group width is invalid.");
    REQUIRE_THROWS_WITH(
      executeEEIssueGroupMembers(
        static_cast<EEIssueWidth>(3),
        attempt),
      "EE issue group width is invalid.");
    REQUIRE(attempt.attempts == 0);
  }

  SECTION("The attempt callable is used without ownership or copying")
  {
    NonCopyableIssueMemberAttempt attempt;
    const EEIssueGroupExecutionResult result =
      executeEEIssueGroupMembers(
        EEIssueWidth::Two,
        attempt);

    REQUIRE(attempt.attempts == 2);
    REQUIRE(result.attempted == 2);
    REQUIRE(result.accepted == 2);
    REQUIRE(
      result.older ==
      EEIssueMemberOutcome::Accepted);
    REQUIRE(
      result.younger ==
      EEIssueMemberOutcome::Accepted);
  }

  SECTION("An older fault cancels the younger member")
  {
    std::uint8_t attempts = 0;
    const EEIssueGroupExecutionResult result =
      executeEEIssueGroupMembers(
        EEIssueWidth::Two,
        [&attempts](EEIssueMemberPosition position)
        {
          ++attempts;
          REQUIRE(
            position == EEIssueMemberPosition::Older);
          return EEIssueMemberOutcome::Faulted;
        });

    REQUIRE(attempts == 1);
    REQUIRE(result.attempted == 1);
    REQUIRE(result.accepted == 0);
    REQUIRE(
      result.older ==
      EEIssueMemberOutcome::Faulted);
    REQUIRE(
      result.younger ==
      EEIssueMemberOutcome::Cancelled);
  }

  SECTION("An older stall cancels the younger member")
  {
    const EEIssueGroupExecutionResult result =
      executeEEIssueGroupMembers(
        EEIssueWidth::Two,
        [](EEIssueMemberPosition position)
        {
          REQUIRE(
            position == EEIssueMemberPosition::Older);
          return EEIssueMemberOutcome::Stalled;
        });

    REQUIRE(result.attempted == 1);
    REQUIRE(result.accepted == 0);
    REQUIRE(
      result.older ==
      EEIssueMemberOutcome::Stalled);
    REQUIRE(
      result.younger ==
      EEIssueMemberOutcome::Cancelled);
  }

  SECTION("A younger fault preserves the older member")
  {
    std::uint32_t architecturalValue = 0;
    EEAcceptanceRecords records;
    const EEIssueGroupExecutionResult result =
      executeEEIssueGroupMembers(
        EEIssueWidth::Two,
        [&architecturalValue, &records](
          EEIssueMemberPosition position)
        {
          if (position == EEIssueMemberPosition::Older)
          {
            architecturalValue = 7;
            records.append({
              1,
              0,
              decodeEEInstruction(
                UINT32_C(0x24020007)),
              EEAcceptanceMode::Ordinary
            });
            return EEIssueMemberOutcome::Accepted;
          }
          REQUIRE(architecturalValue == 7);
          return EEIssueMemberOutcome::Faulted;
        });

    REQUIRE(architecturalValue == 7);
    REQUIRE(records.size() == 1);
    REQUIRE(records[0].instruction.raw == 0x24020007);
    REQUIRE(result.attempted == 2);
    REQUIRE(result.accepted == 1);
    REQUIRE(
      result.older ==
      EEIssueMemberOutcome::Accepted);
    REQUIRE(
      result.younger ==
      EEIssueMemberOutcome::Faulted);
  }
}

TEST_CASE("EE Core executes issue groups at precise boundaries")
{
  SECTION("An older interlock stalls and cancels the younger member")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::COP1_USABLE);
    core.setFloatingPointRegister(
      1,
      UINT32_C(0x3f800000));
    core.setFloatingPointRegister(
      2,
      UINT32_C(0x40000000));
    system.eeBus().write32(0, UINT32_C(0x460208c0));
    system.eeBus().write32(4, UINT32_C(0x24040001));
    system.eeBus().write32(8, UINT32_C(0x44051800));
    system.eeBus().write32(12, UINT32_C(0x24060002));
    core.startExecution(0);
    system.runMasterCycles(2);

    const EEIssueGroupExecutionResult result =
      EECoreTestAccess::executeIssueGroup(
        &core,
        EEIssueWidth::Two);

    REQUIRE(result.attempted == 1);
    REQUIRE(result.accepted == 0);
    REQUIRE(
      result.older ==
      EEIssueMemberOutcome::Stalled);
    REQUIRE(
      result.younger ==
      EEIssueMemberOutcome::Cancelled);
    REQUIRE(core.programCounter() == 8);
  }

  SECTION("An older exception suppresses younger effects")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Status, 0);
    system.eeBus().write32(0, UINT32_C(0x0000000c));
    system.eeBus().write32(4, UINT32_C(0x24020007));
    core.startExecution(0);

    const EEIssueGroupExecutionResult result =
      EECoreTestAccess::executeIssueGroup(
        &core,
        EEIssueWidth::Two);

    REQUIRE(result.attempted == 1);
    REQUIRE(result.accepted == 0);
    REQUIRE(
      result.older ==
      EEIssueMemberOutcome::Faulted);
    REQUIRE(
      result.younger ==
      EEIssueMemberOutcome::Cancelled);
    REQUIRE(core.generalRegister(2).low == 0);
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      0);
    REQUIRE(core.pendingException() == EEException::SystemCall);
    REQUIRE(core.exceptionAddress() == 0);
    REQUIRE_FALSE(core.hasLastInstruction());
  }

  SECTION("A younger exception preserves older effects")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Status, 0);
    system.eeBus().write32(0, UINT32_C(0x24020007));
    system.eeBus().write32(4, UINT32_C(0x0000000c));
    core.startExecution(0);

    const EEIssueGroupExecutionResult result =
      EECoreTestAccess::executeIssueGroup(
        &core,
        EEIssueWidth::Two);

    REQUIRE(result.attempted == 2);
    REQUIRE(result.accepted == 1);
    REQUIRE(
      result.older ==
      EEIssueMemberOutcome::Accepted);
    REQUIRE(
      result.younger ==
      EEIssueMemberOutcome::Faulted);
    REQUIRE(core.generalRegister(2).low == 7);
    const EEAcceptanceRecords &records =
      core.acceptanceRecordsThisCycle();
    REQUIRE(records.size() == 1);
    REQUIRE(records[0].address == 0);
    REQUIRE(records[0].instruction.raw == 0x24020007);
    REQUIRE(core.pendingException() == EEException::SystemCall);
    REQUIRE(core.exceptionAddress() == 4);
    REQUIRE(core.cop0Register(EECOP0Register::EPC) == 4);
    REQUIRE(core.lastInstructionAddress() == 0);
    REQUIRE(core.lastInstruction().raw == 0x24020007);
  }

  SECTION("A younger stall preserves the older member")
  {
    NekoSystem system;
    EEBus &bus = system.eeBus();
    EECore &core = system.eeCore();
    const EEQuadword interruptedNops = {
      UINT64_C(0x0000000080000000),
      0
    };
    REQUIRE(
      bus.writeGuestData128(
        EEMemoryMap::VIF0_FIFO,
        interruptedNops) ==
      EEDataWriteResult::Completed);
    bus.advanceGuestFIFOs();
    for (std::size_t index = 0; index < 7; ++index)
    {
      REQUIRE(
        bus.writeGuestData128(
          EEMemoryMap::VIF0_FIFO,
          {}) ==
        EEDataWriteResult::Completed);
    }

    core.setGeneralRegister(
      1,
      {EEMemoryMap::VIF0_FIFO, 0});
    core.setGeneralRegister(
      2,
      {
        UINT64_C(0x1111111122222222),
        UINT64_C(0x3333333344444444)
      });
    bus.write32(0, UINT32_C(0x24030001));
    bus.write32(4, UINT32_C(0x7c220000));
    core.startExecution(0);

    const EEIssueGroupExecutionResult result =
      EECoreTestAccess::executeIssueGroup(
        &core,
        EEIssueWidth::Two);

    REQUIRE(result.attempted == 2);
    REQUIRE(result.accepted == 1);
    REQUIRE(
      result.older ==
      EEIssueMemberOutcome::Accepted);
    REQUIRE(
      result.younger ==
      EEIssueMemberOutcome::Stalled);
    REQUIRE(core.generalRegister(3).low == 1);
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      1);
    REQUIRE(core.programCounter() == 4);
    REQUIRE(system.vif0().fifoQuadwordCount() == 8);
    REQUIRE(core.pendingException() == EEException::None);
  }

  SECTION("A younger stop preserves older effects and owns the reason")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    system.eeBus().write32(0, UINT32_C(0x24020007));
    system.eeBus().write32(4, UINT32_C(0xbc000000));
    core.startExecution(0);

    const EEIssueGroupExecutionResult result =
      EECoreTestAccess::executeIssueGroup(
        &core,
        EEIssueWidth::Two);

    REQUIRE(result.attempted == 2);
    REQUIRE(result.accepted == 1);
    REQUIRE(
      result.older ==
      EEIssueMemberOutcome::Accepted);
    REQUIRE(
      result.younger ==
      EEIssueMemberOutcome::Cancelled);
    REQUIRE(core.generalRegister(2).low == 7);
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      1);
    REQUIRE_FALSE(core.clockActive());
    REQUIRE(
      core.stopReason() ==
      EEStopReason::UnsupportedInstruction);
    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.rejectedInstruction() == 0xbc000000);
    REQUIRE(core.lastInstructionAddress() == 0);
  }

  SECTION("A younger exception preserves older delayed work")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::COP1_USABLE);
    core.setFloatingPointRegister(1, UINT32_C(0x3f800000));
    core.setFloatingPointRegister(2, UINT32_C(0x40000000));
    system.eeBus().write32(0, UINT32_C(0x460208c0));
    system.eeBus().write32(4, UINT32_C(0x0000000c));
    core.startExecution(0);

    const EEIssueGroupExecutionResult result =
      EECoreTestAccess::executeIssueGroup(
        &core,
        EEIssueWidth::Two);

    REQUIRE(result.accepted == 1);
    REQUIRE(
      result.older ==
      EEIssueMemberOutcome::Accepted);
    REQUIRE(
      result.younger ==
      EEIssueMemberOutcome::Faulted);
    REQUIRE(core.floatingPointRegister(3) == 0);
    REQUIRE(core.pendingException() == EEException::SystemCall);

    system.runMasterCycles(5);

    REQUIRE(
      core.floatingPointRegister(3) ==
      UINT32_C(0x40400000));
  }
}

TEST_CASE("EE Core architectural state")
{
  EECore core;

  SECTION("The EE exposes 32 zero-initialized 128-bit GPRs")
  {
    REQUIRE(EECore::GENERAL_REGISTER_COUNT == 32);
    for (std::size_t index = 0;
         index < EECore::GENERAL_REGISTER_COUNT;
         ++index)
    {
      REQUIRE(core.generalRegister(index).low == 0);
      REQUIRE(core.generalRegister(index).high == 0);
    }
  }

  SECTION("GPRs preserve all 128 bits")
  {
    const EERegister128 value = {
      UINT64_C(0x0123456789abcdef),
      UINT64_C(0xfedcba9876543210)
    };

    core.setGeneralRegister(1, value);

    REQUIRE(core.generalRegister(1) == value);
  }

  SECTION("Writes to GPR zero are ignored")
  {
    core.setGeneralRegister(
      0,
      {
        UINT64_C(0xffffffffffffffff),
        UINT64_C(0xffffffffffffffff)
      });

    REQUIRE(core.generalRegister(0) == EERegister128{});
  }

  SECTION("GPR indices are checked")
  {
    REQUIRE_THROWS(
      core.generalRegister(EECore::GENERAL_REGISTER_COUNT));
    REQUIRE_THROWS(
      core.setGeneralRegister(
        EECore::GENERAL_REGISTER_COUNT,
        {}));
  }

  SECTION("The EE exposes 32 independent raw-bit FPRs")
  {
    REQUIRE(EECore::FLOATING_POINT_REGISTER_COUNT == 32);
    for (std::size_t index = 0;
         index < EECore::FLOATING_POINT_REGISTER_COUNT;
         ++index)
    {
      REQUIRE(core.floatingPointRegister(index) == 0);
      core.setFloatingPointRegister(
        index,
        static_cast<std::uint32_t>(index + 1));
    }
    for (std::size_t index = 0;
         index < EECore::FLOATING_POINT_REGISTER_COUNT;
         ++index)
    {
      REQUIRE(
        core.floatingPointRegister(index) ==
        static_cast<std::uint32_t>(index + 1));
    }
  }

  SECTION("FPR indices are checked")
  {
    REQUIRE_THROWS(
      core.floatingPointRegister(
        EECore::FLOATING_POINT_REGISTER_COUNT));
    REQUIRE_THROWS(
      core.setFloatingPointRegister(
        EECore::FLOATING_POINT_REGISTER_COUNT,
        0));
  }

  SECTION("The COP1 accumulator preserves raw bits")
  {
    core.setFloatingPointAccumulator(UINT32_C(0x89abcdef));

    REQUIRE(
      core.floatingPointAccumulator() ==
      UINT32_C(0x89abcdef));
  }

  SECTION("FCR0 is constant and read-only")
  {
    REQUIRE(
      core.cop1ControlRegister(0) ==
      EECOP1Control::IMPLEMENTATION_REVISION);

    core.setCOP1ControlRegister(0, UINT32_MAX);

    REQUIRE(
      core.cop1ControlRegister(0) ==
      EECOP1Control::IMPLEMENTATION_REVISION);
  }

  SECTION("FCR31 exposes only writable and hardwired fields")
  {
    REQUIRE(
      core.cop1ControlRegister(31) ==
      EECOP1Control::STATUS_FIXED);

    core.setCOP1ControlRegister(31, UINT32_MAX);

    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::STATUS_WRITABLE_MASK));

    core.setCOP1ControlRegister(31, 0);

    REQUIRE(
      core.cop1ControlRegister(31) ==
      EECOP1Control::STATUS_FIXED);
  }

  SECTION("COP1 arithmetic flags update current and sticky fields")
  {
    core.updateCOP1ArithmeticFlags(
      FP_FLAG_OVERFLOW | FP_FLAG_UNDERFLOW,
      FP_FLAG_OVERFLOW);

    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_OVERFLOW |
       EECOP1Control::STICKY_OVERFLOW));

    core.updateCOP1ArithmeticFlags(
      FP_FLAG_OVERFLOW | FP_FLAG_UNDERFLOW,
      FP_FLAG_UNDERFLOW);

    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_UNDERFLOW |
       EECOP1Control::STICKY_OVERFLOW |
       EECOP1Control::STICKY_UNDERFLOW));
  }

  SECTION("COP1 flag updates preserve unaffected causes and all sticky flags")
  {
    core.updateCOP1ArithmeticFlags(
      FP_FLAG_I_BIT | FP_FLAG_D_BIT,
      FP_FLAG_I_BIT);
    core.updateCOP1ArithmeticFlags(
      FP_FLAG_OVERFLOW | FP_FLAG_UNDERFLOW,
      FP_FLAG_UNDERFLOW);

    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_INVALID |
       EECOP1Control::CAUSE_UNDERFLOW |
       EECOP1Control::STICKY_INVALID |
       EECOP1Control::STICKY_UNDERFLOW));

    core.updateCOP1ArithmeticFlags(
      FP_FLAG_I_BIT | FP_FLAG_D_BIT,
      0);

    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_UNDERFLOW |
       EECOP1Control::STICKY_INVALID |
       EECOP1Control::STICKY_UNDERFLOW));
  }

  SECTION("COP1 flag updates reject invalid masks")
  {
    REQUIRE_THROWS_WITH(
      core.updateCOP1ArithmeticFlags(UINT8_C(0x10), 0),
      "Invalid EE COP1 arithmetic flag update.");
    REQUIRE_THROWS_WITH(
      core.updateCOP1ArithmeticFlags(
        FP_FLAG_OVERFLOW,
        FP_FLAG_UNDERFLOW),
      "Invalid EE COP1 arithmetic flag update.");
  }

  SECTION("Reserved COP1 control registers are rejected")
  {
    REQUIRE_THROWS(core.cop1ControlRegister(1));
    REQUIRE_THROWS(core.cop1ControlRegister(30));
    REQUIRE_THROWS(core.cop1ControlRegister(32));
    REQUIRE_THROWS(core.setCOP1ControlRegister(1, 0));
    REQUIRE_THROWS(core.setCOP1ControlRegister(30, 0));
    REQUIRE_THROWS(core.setCOP1ControlRegister(32, 0));
  }

  SECTION("PC and integer special registers are independent")
  {
    core.setProgramCounter(0x81234560);
    core.setHI(UINT64_C(0x1111111122222222));
    core.setLO(UINT64_C(0x3333333344444444));
    core.setHI1(UINT64_C(0x5555555566666666));
    core.setLO1(UINT64_C(0x7777777788888888));
    core.setShiftAmount(0x99);

    REQUIRE(core.programCounter() == 0x81234560);
    REQUIRE(core.hi() == UINT64_C(0x1111111122222222));
    REQUIRE(core.lo() == UINT64_C(0x3333333344444444));
    REQUIRE(core.hi1() == UINT64_C(0x5555555566666666));
    REQUIRE(core.lo1() == UINT64_C(0x7777777788888888));
    REQUIRE(core.shiftAmount() == 0x99);
  }

  SECTION("Reset restores deterministic architectural state")
  {
    const std::uint64_t resetHash = core.stateHash();
    core.setGeneralRegister(
      31,
      {UINT64_C(0xaaaaaaaaaaaaaaaa),
       UINT64_C(0xbbbbbbbbbbbbbbbb)});
    core.setProgramCounter(0x12345678);
    core.setHI(1);
    core.setLO(2);
    core.setHI1(3);
    core.setLO1(4);
    core.setShiftAmount(5);
    core.setFloatingPointRegister(31, UINT32_C(0xaaaaaaaa));
    core.setFloatingPointAccumulator(UINT32_C(0xbbbbbbbb));
    core.setCOP1ControlRegister(31, UINT32_MAX);
    REQUIRE(core.stateHash() != resetHash);

    core.reset();

    for (std::size_t index = 0;
         index < EECore::GENERAL_REGISTER_COUNT;
         ++index)
    {
      REQUIRE(core.generalRegister(index) == EERegister128{});
    }
    REQUIRE(core.programCounter() == EEReset::VECTOR);
    REQUIRE(core.hi() == 0);
    REQUIRE(core.lo() == 0);
    REQUIRE(core.hi1() == 0);
    REQUIRE(core.lo1() == 0);
    REQUIRE(core.shiftAmount() == 0);
    for (std::size_t index = 0;
         index < EECore::FLOATING_POINT_REGISTER_COUNT;
         ++index)
    {
      REQUIRE(core.floatingPointRegister(index) == 0);
    }
    REQUIRE(core.floatingPointAccumulator() == 0);
    REQUIRE(
      core.cop1ControlRegister(0) ==
      EECOP1Control::IMPLEMENTATION_REVISION);
    REQUIRE(
      core.cop1ControlRegister(31) ==
      EECOP1Control::STATUS_FIXED);
    REQUIRE(core.stateHash() == resetHash);
  }
}

TEST_CASE("EE Core instruction fetching")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  EEBus &bus = system.eeBus();

  SECTION("Instructions are fetched little-endian through RAM aliases")
  {
    bus.write32(0x100, UINT32_C(0x01234567));
    core.setProgramCounter(0x80000100);

    const EEInstructionFetchResult result =
      core.fetchInstruction();

    REQUIRE(result.succeeded);
    REQUIRE(result.address == 0x80000100);
    REQUIRE(result.instruction == 0x01234567);
    REQUIRE(core.programCounter() == 0x80000104);
    REQUIRE_FALSE(core.exceptionPending());
  }

  SECTION("Misaligned instruction addresses raise AdEL")
  {
    core.setProgramCounter(0x102);

    const EEInstructionFetchResult result =
      core.fetchInstruction();

    REQUIRE_FALSE(result.succeeded);
    REQUIRE(result.address == 0x102);
    REQUIRE(result.instruction == 0);
    REQUIRE(core.programCounter() == 0x102);
    REQUIRE(core.exceptionPending());
    REQUIRE(
      core.pendingException() ==
      EEException::AddressErrorLoadOrFetch);
    REQUIRE(core.exceptionAddress() == 0x102);
  }

  SECTION("Unmapped instruction addresses raise an instruction bus error")
  {
    core.setProgramCounter(0xa2000000);

    const EEInstructionFetchResult result =
      core.fetchInstruction();

    REQUIRE_FALSE(result.succeeded);
    REQUIRE(core.programCounter() == 0xa2000000);
    REQUIRE(
      core.pendingException() ==
      EEException::InstructionBusError);
    REQUIRE(core.exceptionAddress() == 0xa2000000);
  }

  SECTION("MMIO registers are not executable memory")
  {
    core.setProgramCounter(EEMemoryMap::GIF_STAT);

    const EEInstructionFetchResult result =
      core.fetchInstruction();

    REQUIRE_FALSE(result.succeeded);
    REQUIRE(
      core.pendingException() ==
      EEException::InstructionBusError);
  }

  SECTION("A recorded fetch exception does not block later fetches")
  {
    core.setProgramCounter(2);
    REQUIRE_FALSE(core.fetchInstruction().succeeded);

    bus.write32(0x100, UINT32_C(0x89abcdef));
    core.setProgramCounter(0x100);
    const EEInstructionFetchResult result =
      core.fetchInstruction();

    REQUIRE(result.succeeded);
    REQUIRE(result.instruction == 0x89abcdef);
    REQUIRE(core.exceptionPending());
    REQUIRE(
      core.pendingException() ==
      EEException::AddressErrorLoadOrFetch);
  }

  SECTION("Public fetch rejects live front-end continuation")
  {
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::COP1_USABLE);
    core.setFloatingPointRegister(
      1,
      UINT32_C(0x3f800000));
    core.setFloatingPointRegister(
      2,
      UINT32_C(0x40000000));
    bus.write32(0, UINT32_C(0x460208c0));
    bus.write32(4, UINT32_C(0x24040001));
    bus.write32(8, UINT32_C(0x44051800));
    bus.write32(12, UINT32_C(0x24060002));
    core.startExecution(0);
    system.runMasterCycles(2);
    core.haltExecution();

    REQUIRE(core.programCounter() == 8);
    const std::uint64_t stateHash = core.stateHash();

    REQUIRE_THROWS_WITH(
      core.fetchInstruction(),
      "EE public instruction fetch requires an empty front end.");
    REQUIRE(core.programCounter() == 8);
    REQUIRE(core.stateHash() == stateHash);
  }

  SECTION("Public fetch rejects a pending delay-slot continuation")
  {
    bus.write32(0, UINT32_C(0x08000003));
    bus.write32(4, UINT32_C(0x0000000f));
    core.startExecution(0);
    system.runMasterCycles(2);

    REQUIRE(
      core.stopReason() ==
      EEStopReason::UndefinedOperation);
    REQUIRE(core.programCounter() == 4);
    const std::uint64_t stateHash = core.stateHash();

    REQUIRE_THROWS_WITH(
      core.fetchInstruction(),
      "EE public instruction fetch requires an empty front end.");
    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.stateHash() == stateHash);
  }

  SECTION("Reset clears a pending fetch exception")
  {
    core.setProgramCounter(2);
    REQUIRE_FALSE(core.fetchInstruction().succeeded);

    core.reset();

    REQUIRE_FALSE(core.exceptionPending());
    REQUIRE(core.pendingException() == EEException::None);
    REQUIRE(core.exceptionAddress() == 0);

    bus.write32(0, UINT32_C(0x12345678));
    core.setProgramCounter(0);
    REQUIRE(core.fetchInstruction().succeeded);
  }
}

TEST_CASE("EE Core scheduled execution")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  EEBus &bus = system.eeBus();

  SECTION("The EE is halted after reset")
  {
    REQUIRE_FALSE(core.clockActive());
    REQUIRE(
      core.executionState() ==
      EEExecutionState::Halted);
    REQUIRE(core.stopReason() == EEStopReason::None);
    REQUIRE(core.elapsedCycles() == 0);

    system.runMasterCycles(3);

    REQUIRE(core.programCounter() == EEReset::VECTOR);
    REQUIRE(core.elapsedCycles() == 0);
  }

  SECTION("The front end refills after two-wide issue")
  {
    bus.write32(0, 0);
    bus.write32(4, UINT32_C(0x00021900));
    bus.write32(8, UINT32_C(0x24030001));
    core.startExecution(0);

    system.runMasterCycles(3);

    REQUIRE(core.clockActive());
    REQUIRE(core.elapsedCycles() == 3);
    REQUIRE(core.programCounter() == 12);
    REQUIRE(core.hasLastInstruction());
    REQUIRE(core.lastInstructionAddress() == 8);
    REQUIRE(
      core.lastInstruction().operation ==
      EEOperation::AddImmediateUnsignedWord);
    REQUIRE(core.lastInstruction().raw == 0x24030001);
    REQUIRE(core.generalRegister(3).low == 1);
  }

  SECTION("Issue readiness previews an independent pair")
  {
    bus.write32(0, UINT32_C(0x24020001));
    bus.write32(4, UINT32_C(0x24030002));
    core.startExecution(0);

    system.clockMasterCycle();

    const EEIssueSelection selection =
      core.lastIssueSelection();
    REQUIRE(selection.instructionCount == 2);
    REQUIRE(
      selection.pairing ==
      EEIssuePairing::Concurrent);
    REQUIRE(core.programCounter() == 8);
  }

  SECTION("Independent register instructions issue together")
  {
    bus.write32(0, UINT32_C(0x24020001));
    bus.write32(4, UINT32_C(0x24030002));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(core.generalRegister(2).low == 1);
    REQUIRE(core.generalRegister(3).low == 2);
    const EEAcceptanceRecords &records =
      core.acceptanceRecordsThisCycle();
    REQUIRE(records.size() == 2);
    REQUIRE(records[0].address == 0);
    REQUIRE(records[1].address == 4);
    REQUIRE(records[0].programOrder == 1);
    REQUIRE(records[1].programOrder == 2);
    REQUIRE(core.lastInstructionAddress() == 4);
    REQUIRE(core.programCounter() == 8);
  }

  SECTION("Stepping observes a complete register-only pair")
  {
    bus.write32(0, UINT32_C(0x24020001));
    bus.write32(4, UINT32_C(0x24030002));
    core.startExecution(0);

    const EEExecutionResult result =
      system.stepEEInstruction(1);

    REQUIRE(result.masterCycles == 1);
    REQUIRE(result.eeCycles == 1);
    REQUIRE(result.instructions == 2);
    REQUIRE(core.generalRegister(2).low == 1);
    REQUIRE(core.generalRegister(3).low == 2);
    REQUIRE(core.programCounter() == 8);
  }

  SECTION("Reverse fixed-pipe order issues concurrently")
  {
    core.setHI1(9);
    bus.write32(0, UINT32_C(0x70001010));
    bus.write32(4, UINT32_C(0x24030002));
    core.startExecution(0);

    system.clockMasterCycle();

    const EEIssueSelection selection =
      core.lastIssueSelection();
    REQUIRE(selection.instructionCount == 2);
    REQUIRE(
      selection.assignment.olderPipe ==
      EELogicalPipe::Pipe1);
    REQUIRE(
      selection.assignment.youngerPipe ==
      EELogicalPipe::Pipe0);
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      2);
    REQUIRE(core.generalRegister(2).low == 9);
    REQUIRE(core.generalRegister(3).low == 2);
  }

  SECTION("An older COP1 GPR producer permits an independent partner")
  {
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::COP1_USABLE);
    core.setFloatingPointRegister(1, UINT32_C(0x3f800000));
    bus.write32(0, UINT32_C(0x44020800));
    bus.write32(4, UINT32_C(0x24030002));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.lastIssueSelection().instructionCount ==
      2);
    REQUIRE(
      core.lastIssueSelection().pairing ==
      EEIssuePairing::Concurrent);
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      2);
    REQUIRE(core.generalRegister(2).low == 0);
    REQUIRE(core.generalRegister(3).low == 2);
    REQUIRE(core.programCounter() == 8);
  }

  SECTION("An older COP1 Move issues with a younger COP1 Operate")
  {
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::COP1_USABLE);
    bus.write32(0, UINT32_C(0x44020800));
    bus.write32(4, UINT32_C(0x46031000));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.lastIssueSelection().pairing ==
      EEIssuePairing::ConcurrentWithStall);
    REQUIRE(
      core.lastIssueSelection().assignment.olderPipe ==
      EELogicalPipe::Pipe1);
    REQUIRE(
      core.lastIssueSelection().assignment.youngerPipe ==
      EELogicalPipe::Pipe0);
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      2);
    REQUIRE(
      core.acceptanceRecordsThisCycle()[0]
        .instruction.operation ==
      EEOperation::MoveWordFromCOP1);
    REQUIRE(
      core.acceptanceRecordsThisCycle()[1]
        .instruction.operation ==
      EEOperation::AddSingleCOP1);
    REQUIRE(core.programCounter() == 8);
  }

  SECTION("An older COP1 Operate issues with a younger COP1 Move")
  {
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::COP1_USABLE);
    bus.write32(0, UINT32_C(0x46031000));
    bus.write32(4, UINT32_C(0x44040800));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.lastIssueSelection().instructionCount ==
      2);
    REQUIRE(
      core.lastIssueSelection().pairing ==
      EEIssuePairing::ConcurrentWithStall);
    REQUIRE(
      core.lastIssueSelection().assignment.olderPipe ==
      EELogicalPipe::Pipe0);
    REQUIRE(
      core.lastIssueSelection().assignment.youngerPipe ==
      EELogicalPipe::Pipe1);
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      2);
    REQUIRE(
      core.acceptanceRecordsThisCycle()[0]
        .instruction.operation ==
      EEOperation::AddSingleCOP1);
    REQUIRE(
      core.acceptanceRecordsThisCycle()[1]
        .instruction.operation ==
      EEOperation::MoveWordFromCOP1);
    REQUIRE(core.programCounter() == 8);
  }

  SECTION("An older COP1 load issues with a younger COP1 Operate")
  {
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::COP1_USABLE);
    core.setGeneralRegister(1, {0x100, 0});
    bus.write32(
      0,
      (UINT32_C(0x31) << 26) |
      (UINT32_C(1) << 21) |
      (UINT32_C(6) << 16));
    bus.write32(4, UINT32_C(0x46031000));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.lastIssueSelection().pairing ==
      EEIssuePairing::ConcurrentWithStall);
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      2);
    REQUIRE(
      core.acceptanceRecordsThisCycle()[0]
        .instruction.operation ==
      EEOperation::LoadWordToCOP1);
    REQUIRE(
      core.acceptanceRecordsThisCycle()[1]
        .instruction.operation ==
      EEOperation::AddSingleCOP1);
    REQUIRE(core.programCounter() == 8);
  }

  SECTION("An older COP1 store issues with a younger COP1 Operate")
  {
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::COP1_USABLE);
    core.setGeneralRegister(1, {0x100, 0});
    bus.write32(
      0,
      (UINT32_C(0x39) << 26) |
      (UINT32_C(1) << 21) |
      (UINT32_C(6) << 16));
    bus.write32(4, UINT32_C(0x46031000));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.lastIssueSelection().pairing ==
      EEIssuePairing::ConcurrentWithStall);
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      2);
    REQUIRE(
      core.acceptanceRecordsThisCycle()[0]
        .instruction.operation ==
      EEOperation::StoreWordFromCOP1);
    REQUIRE(
      core.acceptanceRecordsThisCycle()[1]
        .instruction.operation ==
      EEOperation::AddSingleCOP1);
    REQUIRE(core.programCounter() == 8);
  }

  SECTION("An older COP1 Operate issues with a younger COP1 load")
  {
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::COP1_USABLE);
    core.setGeneralRegister(1, {0x100, 0});
    bus.write32(0, UINT32_C(0x46031000));
    bus.write32(
      4,
      (UINT32_C(0x31) << 26) |
      (UINT32_C(1) << 21) |
      (UINT32_C(6) << 16));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.lastIssueSelection().pairing ==
      EEIssuePairing::ConcurrentWithStall);
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      2);
    REQUIRE(
      core.acceptanceRecordsThisCycle()[0]
        .instruction.operation ==
      EEOperation::AddSingleCOP1);
    REQUIRE(
      core.acceptanceRecordsThisCycle()[1]
        .instruction.operation ==
      EEOperation::LoadWordToCOP1);
    REQUIRE(core.programCounter() == 8);
  }

  SECTION("An older COP1 Operate issues with a younger COP1 store")
  {
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::COP1_USABLE);
    core.setGeneralRegister(1, {0x100, 0});
    bus.write32(0, UINT32_C(0x46031000));
    bus.write32(
      4,
      (UINT32_C(0x39) << 26) |
      (UINT32_C(1) << 21) |
      (UINT32_C(6) << 16));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.lastIssueSelection().pairing ==
      EEIssuePairing::ConcurrentWithStall);
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      2);
    REQUIRE(
      core.acceptanceRecordsThisCycle()[0]
        .instruction.operation ==
      EEOperation::AddSingleCOP1);
    REQUIRE(
      core.acceptanceRecordsThisCycle()[1]
        .instruction.operation ==
      EEOperation::StoreWordFromCOP1);
    REQUIRE(core.programCounter() == 8);
  }

  SECTION("Ordinary NOP partners remain scalar")
  {
    bus.write32(0, 0);
    bus.write32(4, UINT32_C(0x24030002));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.lastIssueSelection().instructionCount ==
      2);
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      1);
    REQUIRE(core.generalRegister(3).low == 0);
    REQUIRE(core.programCounter() == 4);
  }

  SECTION("Synchronization partners remain scalar")
  {
    bus.write32(0, UINT32_C(0x0000040f));
    bus.write32(4, UINT32_C(0x24030002));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.lastIssueSelection().instructionCount ==
      2);
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      1);
    REQUIRE(core.generalRegister(3).low == 0);
    REQUIRE(core.programCounter() == 4);
  }

  SECTION("Independent same-pipe memory operations remain scalar")
  {
    core.setGeneralRegister(1, {0x100, 0});
    core.setGeneralRegister(4, {0x104, 0});
    bus.write32(0x100, UINT32_C(0x12345678));
    bus.write32(0x104, UINT32_C(0x89abcdef));
    bus.write32(0, UINT32_C(0x8c220000));
    bus.write32(4, UINT32_C(0x8c830000));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.lastIssueSelection().instructionCount ==
      1);
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      1);
    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0x0000000012345678));
    REQUIRE(core.generalRegister(3).low == 0);
    REQUIRE(core.programCounter() == 4);
  }

  SECTION("Dependent memory pairs remain scalar")
  {
    core.setGeneralRegister(1, {0x100, 0});
    bus.write32(0x100, UINT32_C(0x12345678));
    bus.write32(0, UINT32_C(0x8c220000));
    bus.write32(4, UINT32_C(0x24430001));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.lastIssueSelection().instructionCount ==
      1);
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      1);
    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0x0000000012345678));
    REQUIRE(core.generalRegister(3).low == 0);
    REQUIRE(core.programCounter() == 4);
  }

  SECTION("A load and independent register instruction issue together")
  {
    core.setGeneralRegister(1, {0x100, 0});
    bus.write32(0x100, UINT32_C(0x12345678));
    bus.write32(0, UINT32_C(0x8c220000));
    bus.write32(4, UINT32_C(0x24030001));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      2);
    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0x0000000012345678));
    REQUIRE(core.generalRegister(3).low == 1);
    REQUIRE(core.programCounter() == 8);
  }

  SECTION("A younger store commits after its older register partner")
  {
    core.setGeneralRegister(1, {0x100, 0});
    core.setGeneralRegister(2, {UINT32_C(0x12345678), 0});
    bus.write32(0, UINT32_C(0x24030001));
    bus.write32(4, UINT32_C(0xac220000));
    core.startExecution(0);

    system.clockMasterCycle();

    std::uint32_t stored = 0;
    REQUIRE(bus.readData32(0x100, &stored));
    REQUIRE(stored == UINT32_C(0x12345678));
    REQUIRE(core.generalRegister(3).low == 1);
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      2);
    REQUIRE(core.programCounter() == 8);
  }

  SECTION("A younger memory fault preserves its older partner")
  {
    core.setGeneralRegister(1, {0x101, 0});
    bus.write32(0, UINT32_C(0x24030001));
    bus.write32(4, UINT32_C(0x84220000));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(core.generalRegister(3).low == 1);
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      1);
    REQUIRE(
      core.pendingException() ==
      EEException::AddressErrorLoadOrFetch);
    REQUIRE(core.exceptionAddress() == 0x101);
    REQUIRE(core.cop0Register(EECOP0Register::EPC) == 4);
  }

  SECTION("A main-memory quadword store issues with its partner")
  {
    core.setGeneralRegister(1, {0x100, 0});
    core.setGeneralRegister(
      2,
      {
        UINT64_C(0x1122334455667788),
        UINT64_C(0x99aabbccddeeff00)
      });
    bus.write32(0, UINT32_C(0x24030001));
    bus.write32(4, UINT32_C(0x7c220000));
    core.startExecution(0);

    system.clockMasterCycle();

    EEQuadword stored;
    REQUIRE(bus.readData128(0x100, &stored));
    REQUIRE(stored.low == UINT64_C(0x1122334455667788));
    REQUIRE(stored.high == UINT64_C(0x99aabbccddeeff00));
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      2);
    REQUIRE(core.programCounter() == 8);
  }

  SECTION("Dependent shift-amount pairs remain scalar")
  {
    core.setShiftAmount(3);
    bus.write32(0, UINT32_C(0x00001028));
    bus.write32(4, UINT32_C(0x24430001));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.lastIssueSelection().instructionCount ==
      1);
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      1);
    REQUIRE(core.generalRegister(2).low == 3);
    REQUIRE(core.generalRegister(3).low == 0);
    REQUIRE(core.programCounter() == 4);
  }

  SECTION("An independent shift-amount operation issues in reverse pipe order")
  {
    core.setGeneralRegister(1, {7, 0});
    bus.write32(0, UINT32_C(0x24030001));
    bus.write32(4, UINT32_C(0x00200029));
    core.startExecution(0);

    system.clockMasterCycle();

    const EEIssueSelection selection =
      core.lastIssueSelection();
    REQUIRE(selection.instructionCount == 2);
    REQUIRE(
      selection.assignment.olderPipe ==
      EELogicalPipe::Pipe1);
    REQUIRE(
      selection.assignment.youngerPipe ==
      EELogicalPipe::Pipe0);
    REQUIRE(core.generalRegister(3).low == 1);
    REQUIRE(core.shiftAmount() == 7);
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      2);
    REQUIRE(core.programCounter() == 8);
  }

  SECTION("An older partner advances the younger SA ordering window")
  {
    core.setGeneralRegister(1, {1, 0});
    bus.write32(0, UINT32_C(0x00001028));
    bus.write32(4, 0);
    bus.write32(8, 0);
    bus.write32(12, UINT32_C(0x24030001));
    bus.write32(16, UINT32_C(0x04380000));
    core.startExecution(0);

    system.runMasterCycles(4);

    REQUIRE(core.generalRegister(3).low == 1);
    REQUIRE(core.shiftAmount() == 8);
    const EEAcceptanceRecords &records =
      core.acceptanceRecordsThisCycle();
    REQUIRE(records.size() == 2);
    REQUIRE(records[0].address == 12);
    REQUIRE(records[1].address == 16);
    REQUIRE(core.programCounter() == 20);
  }

  SECTION("Successful issue produces one acceptance record")
  {
    bus.write32(0, UINT32_C(0x24020001));
    bus.write32(4, UINT32_C(0x0000000c));
    core.startExecution(0);

    system.clockMasterCycle();

    const EEAcceptanceRecords &records =
      core.acceptanceRecordsThisCycle();
    REQUIRE(records.size() == 1);
    REQUIRE(records[0].programOrder == 1);
    REQUIRE(records[0].address == 0);
    REQUIRE(records[0].instruction.raw == 0x24020001);
    REQUIRE(
      records[0].mode ==
      EEAcceptanceMode::Ordinary);
    REQUIRE(core.lastInstructionAddress() == 0);
    REQUIRE(core.lastInstruction().raw == 0x24020001);
  }

  SECTION("A younger fetch fault waits for the older instruction")
  {
    const std::uint32_t finalMappedAddress =
      EEMemoryMap::MAIN_MEMORY_SIZE - 4;
    bus.write32(
      finalMappedAddress,
      UINT32_C(0x24020001));
    core.startExecution(finalMappedAddress);

    system.clockMasterCycle();

    REQUIRE_FALSE(core.exceptionPending());
    REQUIRE(core.generalRegister(2).low == 1);
    REQUIRE(
      core.lastIssueSelection().instructionCount ==
      1);
    REQUIRE(
      core.programCounter() ==
      EEMemoryMap::MAIN_MEMORY_SIZE);

    system.clockMasterCycle();

    REQUIRE(
      core.pendingException() ==
      EEException::InstructionBusError);
    REQUIRE(
      core.exceptionAddress() ==
      EEMemoryMap::MAIN_MEMORY_SIZE);
  }

  SECTION("A younger decode fault waits for the older instruction")
  {
    bus.write32(0, UINT32_C(0x24020001));
    bus.write32(4, UINT32_C(0x4c000000));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE_FALSE(core.exceptionPending());
    REQUIRE(core.generalRegister(2).low == 1);
    REQUIRE(
      core.lastIssueSelection().instructionCount ==
      1);
    REQUIRE(core.programCounter() == 4);

    system.clockMasterCycle();

    REQUIRE(
      core.pendingException() ==
      EEException::ReservedInstruction);
    REQUIRE(core.rejectedInstruction() == 0x4c000000);
  }

  SECTION("Memory pairs participate in readiness")
  {
    bus.write32(0, UINT32_C(0x8c220000));
    bus.write32(4, UINT32_C(0x24030001));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.lastIssueSelection().instructionCount ==
      2);
  }

  SECTION("Branch-likely annulment participates in readiness")
  {
    bus.write32(0, UINT32_C(0x50220001));
    bus.write32(4, UINT32_C(0x24030001));

    core.setGeneralRegister(1, {1, 0});
    core.setGeneralRegister(2, {2, 0});
    core.startExecution(0);
    system.clockMasterCycle();
    REQUIRE(
      core.lastIssueSelection().instructionCount ==
      1);

    core.setGeneralRegister(1, {2, 0});
    core.startExecution(0);
    system.clockMasterCycle();
    REQUIRE(
      core.lastIssueSelection().instructionCount ==
      2);
  }

  SECTION("A fetch exception enters the bootstrap handler")
  {
    core.startExecution(2);

    system.clockMasterCycle();

    REQUIRE(core.clockActive());
    REQUIRE(core.elapsedCycles() == 1);
    REQUIRE(
      core.programCounter() ==
      EEExceptionVector::BOOTSTRAP_GENERAL);
    REQUIRE(core.stopReason() == EEStopReason::None);
    REQUIRE(
      core.pendingException() ==
      EEException::AddressErrorLoadOrFetch);
    REQUIRE_FALSE(core.hasLastInstruction());
  }

  SECTION("Reserved encodings enter the bootstrap handler")
  {
    bus.write32(0, UINT32_C(0x4c000000));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(core.clockActive());
    REQUIRE(core.elapsedCycles() == 1);
    REQUIRE(
      core.programCounter() ==
      EEExceptionVector::BOOTSTRAP_GENERAL);
    REQUIRE(core.stopReason() == EEStopReason::None);
    REQUIRE(
      core.pendingException() ==
      EEException::ReservedInstruction);
    REQUIRE(core.rejectedInstruction() == 0x4c000000);
    REQUIRE_FALSE(core.hasLastInstruction());
  }

  SECTION("Deferred instruction families stop explicitly")
  {
    bus.write32(0, UINT32_C(0xbc000000));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      0);
    REQUIRE_FALSE(core.clockActive());
    REQUIRE(core.programCounter() == 0);
    REQUIRE(
      core.stopReason() ==
      EEStopReason::UnsupportedInstruction);
    REQUIRE(core.rejectedInstruction() == 0xbc000000);
  }

  SECTION("A synchronous stop does not replace prior acceptance")
  {
    bus.write32(0, 0);
    bus.write32(4, UINT32_C(0xbc000000));
    core.startExecution(0);

    system.clockMasterCycle();
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      1);

    system.clockMasterCycle();

    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      0);
    REQUIRE_FALSE(core.clockActive());
    REQUIRE(
      core.stopReason() ==
      EEStopReason::UnsupportedInstruction);
    REQUIRE(core.lastInstructionAddress() == 0);
    REQUIRE(core.lastInstruction().operation == EEOperation::Nop);
    REQUIRE(core.rejectedInstruction() == 0xbc000000);
  }

  SECTION("A synchronous exception produces no acceptance")
  {
    bus.write32(0, 0);
    bus.write32(4, UINT32_C(0x0000000c));
    core.startExecution(0);

    system.clockMasterCycle();
    system.clockMasterCycle();

    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      0);
    REQUIRE(core.pendingException() == EEException::SystemCall);
    REQUIRE(core.lastInstructionAddress() == 0);
    REQUIRE(core.lastInstruction().operation == EEOperation::Nop);
  }

  SECTION("A failed instruction does not consume program order")
  {
    core.setCOP0Register(EECOP0Register::Status, 0);
    bus.write32(0, UINT32_C(0x0000000c));
    bus.write32(EEExceptionVector::GENERAL, 0);
    bus.write32(
      EEExceptionVector::GENERAL + 4,
      UINT32_C(0x0000000c));
    core.startExecution(0);

    system.clockMasterCycle();
    REQUIRE(
      core.acceptanceRecordsThisCycle().size() ==
      0);

    system.clockMasterCycle();

    const EEAcceptanceRecords &records =
      core.acceptanceRecordsThisCycle();
    REQUIRE(records.size() == 1);
    REQUIRE(records[0].programOrder == 1);
    REQUIRE(records[0].address == EEExceptionVector::GENERAL);
  }

  SECTION("Host halt and restart preserve accumulated cycles")
  {
    bus.write32(0, 0);
    bus.write32(4, 0);
    core.startExecution(0);
    system.clockMasterCycle();

    core.haltExecution();
    REQUIRE_FALSE(core.clockActive());
    REQUIRE(core.stopReason() == EEStopReason::HostHalt);

    core.startExecution(4);
    system.clockMasterCycle();

    REQUIRE(core.clockActive());
    REQUIRE(core.elapsedCycles() == 2);
    REQUIRE(core.programCounter() == 8);
    REQUIRE(core.stopReason() == EEStopReason::None);
  }
}

TEST_CASE("EE issue candidates are independent of selection policy")
{
  SECTION("Readiness can block a complete candidate window")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::COP1_USABLE);
    core.setFloatingPointRegister(
      1,
      UINT32_C(0x3f800000));
    core.setFloatingPointRegister(
      2,
      UINT32_C(0x40000000));
    system.eeBus().write32(0, UINT32_C(0x460208c0));
    system.eeBus().write32(4, UINT32_C(0x24040001));
    system.eeBus().write32(8, UINT32_C(0x44051800));
    system.eeBus().write32(12, UINT32_C(0x24060002));
    core.startExecution(0);
    system.runMasterCycles(2);

    const EEIssuePreview preview =
      EECoreTestAccess::previewIssueSelection(&core);
    REQUIRE(preview.candidateCount == 2);
    REQUIRE(preview.selection.instructionCount == 0);
  }

  SECTION("Structural policy can scalarize two ready candidates")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, {1, 0});
    core.setGeneralRegister(2, {2, 0});
    system.eeBus().write32(0, UINT32_C(0x50220001));
    system.eeBus().write32(4, UINT32_C(0x24030001));
    core.startExecution(0);

    const EEIssuePreview preview =
      EECoreTestAccess::previewIssueSelection(&core);
    REQUIRE(preview.candidateCount == 2);
    REQUIRE(preview.selection.instructionCount == 1);
  }
}

TEST_CASE("EE Wide pairs defer the younger A stage by one cycle")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  system.eeBus().write32(
    0,
    UINT32_C(0x70000000) |
      (UINT32_C(1) << 21) |
      (UINT32_C(2) << 16) |
      (UINT32_C(3) << 11) |
      (UINT32_C(0x12) << 6) |
      UINT32_C(0x09));
  system.eeBus().write32(4, UINT32_C(0x24040001));
  system.eeBus().write32(8, UINT32_C(0x24050002));
  core.setGeneralRegister(
    1,
    {
      UINT64_C(0xff00ff00ff00ff00),
      UINT64_C(0xffff0000ffff0000)
    });
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0x0f0f0f0f0f0f0f0f),
      UINT64_C(0x00ff00ff00ff00ff)
    });
  core.startExecution(0);

  core.clock();

  REQUIRE(core.lastIssueSelection().instructionCount == 2);
  REQUIRE(
    core.lastIssueSelection().continuation ==
    EEIssueContinuation::YoungerAStageOneCycle);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 2);
  REQUIRE(core.acceptanceRecordsThisCycle()[0].address == 0);
  REQUIRE(core.acceptanceRecordsThisCycle()[0].programOrder == 1);
  REQUIRE(core.acceptanceRecordsThisCycle()[1].address == 4);
  REQUIRE(core.acceptanceRecordsThisCycle()[1].programOrder == 2);
  REQUIRE(EECoreTestAccess::cycleEventCount(core) >= 2);
  REQUIRE(
    EECoreTestAccess::instructionIssueEventMatches(
      core,
      0,
      0,
      EEOperation::ParallelAnd));
  REQUIRE(
    EECoreTestAccess::instructionIssueEventMatches(
      core,
      1,
      4,
      EEOperation::AddImmediateUnsignedWord));
  REQUIRE(core.generalRegister(3).low == UINT64_C(0x0f000f000f000f00));
  REQUIRE(core.generalRegister(3).high == UINT64_C(0x00ff000000ff0000));
  REQUIRE(core.generalRegister(4).low == 0);
  REQUIRE(core.programCounter() == 8);

  core.clock();

  REQUIRE(core.acceptanceRecordsThisCycle().size() == 0);
  REQUIRE(core.generalRegister(4).low == 1);
  REQUIRE(core.generalRegister(5).low == 0);
  REQUIRE(core.programCounter() == 8);

  core.clock();

  REQUIRE(core.generalRegister(5).low == 2);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 1);
  REQUIRE(core.acceptanceRecordsThisCycle()[0].programOrder == 3);
}

TEST_CASE("EE Wide continuation participates in lifecycle control")
{
  const auto preparePair = [](NekoSystem *system)
  {
    system->eeBus().write32(
      0,
      UINT32_C(0x70000000) |
        (UINT32_C(1) << 21) |
        (UINT32_C(2) << 16) |
        (UINT32_C(3) << 11) |
        (UINT32_C(0x12) << 6) |
        UINT32_C(0x09));
    system->eeBus().write32(4, UINT32_C(0x24040001));
    system->eeCore().setGeneralRegister(
      1,
      {UINT64_MAX, UINT64_MAX});
    system->eeCore().setGeneralRegister(
      2,
      {UINT64_MAX, UINT64_MAX});
    system->eeCore().startExecution(0);
    system->clockMasterCycle();
    REQUIRE(
      EECoreTestAccess::youngerAStageContinuationActive(
        system->eeCore()));
  };

  SECTION("Host halt and same-PC resume preserve accepted work")
  {
    NekoSystem system;
    preparePair(&system);
    EECore &core = system.eeCore();

    core.haltExecution();
    system.clockMasterCycle();
    REQUIRE(core.generalRegister(4).low == 0);
    REQUIRE(
      EECoreTestAccess::youngerAStageContinuationActive(core));

    core.startExecution(core.programCounter());
    system.clockMasterCycle();
    REQUIRE(core.generalRegister(4).low == 1);
    REQUIRE_FALSE(
      EECoreTestAccess::youngerAStageContinuationActive(core));
  }

  SECTION("Fresh restart cancels accepted continuation")
  {
    NekoSystem system;
    preparePair(&system);
    EECore &core = system.eeCore();
    system.eeBus().write32(0x100, 0);
    core.haltExecution();

    core.startExecution(0x100);
    system.clockMasterCycle();

    REQUIRE(core.generalRegister(4).low == 0);
    REQUIRE_FALSE(
      EECoreTestAccess::youngerAStageContinuationActive(core));
  }

  SECTION("External PC redirect preserves accepted work")
  {
    NekoSystem system;
    preparePair(&system);
    EECore &core = system.eeCore();
    system.eeBus().write32(0x100, 0);

    core.setProgramCounter(0x100);
    system.clockMasterCycle();

    REQUIRE(core.generalRegister(4).low == 1);
    REQUIRE(core.programCounter() == 0x100);
    REQUIRE_FALSE(
      EECoreTestAccess::youngerAStageContinuationActive(core));
  }

  SECTION("Younger MAC1 work enters A stage on the next cycle")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    system.eeBus().write32(
      0,
      UINT32_C(0x70000000) |
        (UINT32_C(1) << 21) |
        (UINT32_C(2) << 16) |
        (UINT32_C(3) << 11) |
        (UINT32_C(0x12) << 6) |
        UINT32_C(0x09));
    system.eeBus().write32(
      4,
      UINT32_C(0x70000000) |
        (UINT32_C(4) << 21) |
        (UINT32_C(5) << 16) |
        (UINT32_C(6) << 11) |
        UINT32_C(0x18));
    core.setGeneralRegister(1, {UINT64_MAX, UINT64_MAX});
    core.setGeneralRegister(2, {UINT64_MAX, UINT64_MAX});
    core.setGeneralRegister(4, {6, 0});
    core.setGeneralRegister(5, {7, 0});
    core.startExecution(0);

    system.clockMasterCycle();
    REQUIRE_FALSE(EECoreTestAccess::pendingMac1Active(core));
    REQUIRE(
      EECoreTestAccess::youngerAStageContinuationActive(core));

    system.clockMasterCycle();
    REQUIRE(EECoreTestAccess::pendingMac1Active(core));
    REQUIRE_FALSE(
      EECoreTestAccess::youngerAStageContinuationActive(core));
  }

  SECTION("A deferred undefined MAC1 operation halts normally")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    system.eeBus().write32(
      0,
      UINT32_C(0x70000000) |
        (UINT32_C(1) << 21) |
        (UINT32_C(2) << 16) |
        (UINT32_C(3) << 11) |
        (UINT32_C(0x12) << 6) |
        UINT32_C(0x09));
    system.eeBus().write32(
      4,
      UINT32_C(0x70000000) |
        (UINT32_C(4) << 21) |
        (UINT32_C(5) << 16) |
        UINT32_C(0x1a));
    core.setGeneralRegister(1, {UINT64_MAX, UINT64_MAX});
    core.setGeneralRegister(2, {UINT64_MAX, UINT64_MAX});
    core.setGeneralRegister(4, {42, 0});
    core.setGeneralRegister(5, {0, 0});
    core.startExecution(0);
    system.clockMasterCycle();

    REQUIRE_NOTHROW(system.clockMasterCycle());
    REQUIRE_FALSE(core.clockActive());
    REQUIRE(core.stopReason() == EEStopReason::UndefinedOperation);
    REQUIRE_FALSE(
      EECoreTestAccess::youngerAStageContinuationActive(core));
  }
}

TEST_CASE("EE packed equality participates in Wide issue")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  system.eeBus().write32(
    0,
    UINT32_C(0x70000000) |
      (UINT32_C(1) << 21) |
      (UINT32_C(2) << 16) |
      (UINT32_C(3) << 11) |
      (UINT32_C(0x02) << 6) |
      UINT32_C(0x28));
  system.eeBus().write32(4, UINT32_C(0x24040001));
  core.setGeneralRegister(
    1,
    {
      UINT64_C(0x1111111122222222),
      UINT64_C(0x3333333344444444)
    });
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0xaaaaaaaa22222222),
      UINT64_C(0x33333333bbbbbbbb)
    });
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(core.lastIssueSelection().instructionCount == 2);
  REQUIRE(
    core.lastIssueSelection().continuation ==
    EEIssueContinuation::YoungerAStageOneCycle);
  REQUIRE(
    core.generalRegister(3).low ==
    UINT64_C(0x00000000ffffffff));
  REQUIRE(
    core.generalRegister(3).high ==
    UINT64_C(0xffffffff00000000));
  REQUIRE(core.generalRegister(4).low == 0);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 2);

  system.clockMasterCycle();

  REQUIRE(core.generalRegister(4).low == 1);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 0);
}

TEST_CASE("EE wrapping packed arithmetic participates in Wide issue")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  system.eeBus().write32(
    0,
    UINT32_C(0x70000000) |
      (UINT32_C(1) << 21) |
      (UINT32_C(2) << 16) |
      (UINT32_C(3) << 11) |
      (UINT32_C(0x08) << 6) |
      UINT32_C(0x08));
  system.eeBus().write32(4, UINT32_C(0x24040001));
  core.setGeneralRegister(
    1,
    {
      UINT64_C(0xfffe7fff80000001),
      UINT64_C(0xffffffff7fffffff)
    });
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0x020380010001ffff),
      UINT64_C(0x0000000180000001)
    });
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(core.lastIssueSelection().instructionCount == 2);
  REQUIRE(
    core.lastIssueSelection().continuation ==
    EEIssueContinuation::YoungerAStageOneCycle);
  REQUIRE(
    core.generalRegister(3).low ==
    UINT64_C(0x0101ff008001ff00));
  REQUIRE(
    core.generalRegister(3).high ==
    UINT64_C(0xffffff00ffffff00));
  REQUIRE(core.generalRegister(4).low == 0);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 2);

  system.clockMasterCycle();

  REQUIRE(core.generalRegister(4).low == 1);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 0);
}

TEST_CASE("EE saturating packed arithmetic participates in Wide issue")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  system.eeBus().write32(
    0,
    UINT32_C(0x70000000) |
      (UINT32_C(1) << 21) |
      (UINT32_C(2) << 16) |
      (UINT32_C(3) << 11) |
      (UINT32_C(0x18) << 6) |
      UINT32_C(0x28));
  system.eeBus().write32(4, UINT32_C(0x24040001));
  core.setGeneralRegister(1, {UINT64_MAX, UINT64_MAX});
  core.setGeneralRegister(2, {UINT64_MAX, UINT64_MAX});
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(core.lastIssueSelection().instructionCount == 2);
  REQUIRE(
    core.lastIssueSelection().continuation ==
    EEIssueContinuation::YoungerAStageOneCycle);
  REQUIRE(
    core.generalRegister(3) ==
    EERegister128{UINT64_MAX, UINT64_MAX});
  REQUIRE(core.generalRegister(4).low == 0);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 2);

  system.clockMasterCycle();

  REQUIRE(core.generalRegister(4).low == 1);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 0);
}

TEST_CASE("EE packed rearrangement participates in Wide issue")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  system.eeBus().write32(
    0,
    UINT32_C(0x70000000) |
      (UINT32_C(1) << 21) |
      (UINT32_C(2) << 16) |
      (UINT32_C(3) << 11) |
      (UINT32_C(0x0a) << 6) |
      UINT32_C(0x29));
  system.eeBus().write32(4, UINT32_C(0x24040001));
  core.setGeneralRegister(
    1,
    {
      UINT64_C(0xa003a002a001a000),
      UINT64_C(0xa007a006a005a004)
    });
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0xb003b002b001b000),
      UINT64_C(0xb007b006b005b004)
    });
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(core.lastIssueSelection().instructionCount == 2);
  REQUIRE(
    core.lastIssueSelection().continuation ==
    EEIssueContinuation::YoungerAStageOneCycle);
  REQUIRE(
    core.generalRegister(3) ==
    EERegister128{UINT64_C(0xa002b002a000b000),
                  UINT64_C(0xa006b006a004b004)});
  REQUIRE(core.generalRegister(4).low == 0);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 2);

  system.clockMasterCycle();

  REQUIRE(core.generalRegister(4).low == 1);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 0);
}

TEST_CASE("EE packed pixel format participates in Wide issue")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  system.eeBus().write32(
    0,
    UINT32_C(0x70000000) |
      (UINT32_C(2) << 16) |
      (UINT32_C(3) << 11) |
      (UINT32_C(0x1e) << 6) |
      UINT32_C(0x08));
  system.eeBus().write32(4, UINT32_C(0x24040001));
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0xa5a57fffa5a50000),
      UINT64_C(0xa5a5d6b3a5a58000)
    });
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(core.lastIssueSelection().instructionCount == 2);
  REQUIRE(
    core.lastIssueSelection().continuation ==
    EEIssueContinuation::YoungerAStageOneCycle);
  REQUIRE(
    core.generalRegister(3) ==
    EERegister128{UINT64_C(0x00f8f8f800000000),
                  UINT64_C(0x80a8a89880000000)});
  REQUIRE(core.generalRegister(4).low == 0);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 2);

  system.clockMasterCycle();

  REQUIRE(core.generalRegister(4).low == 1);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 0);
}

TEST_CASE("EE funnel shift participates in Wide issue")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  system.eeBus().write32(
    0,
    UINT32_C(0x70000000) |
      (UINT32_C(1) << 21) |
      (UINT32_C(2) << 16) |
      (UINT32_C(3) << 11) |
      (UINT32_C(0x1b) << 6) |
      UINT32_C(0x28));
  system.eeBus().write32(4, UINT32_C(0x24040001));
  core.setGeneralRegister(
    1,
    {
      UINT64_C(0x1716151413121110),
      UINT64_C(0x1f1e1d1c1b1a1918)
    });
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0x0706050403020100),
      UINT64_C(0x0f0e0d0c0b0a0908)
    });
  core.setShiftAmount(8);
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(core.lastIssueSelection().instructionCount == 2);
  REQUIRE(
    core.lastIssueSelection().continuation ==
    EEIssueContinuation::YoungerAStageOneCycle);
  REQUIRE(
    core.generalRegister(3) ==
    EERegister128{UINT64_C(0x0807060504030201),
                  UINT64_C(0x100f0e0d0c0b0a09)});
  REQUIRE(core.generalRegister(4).low == 0);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 2);

  system.clockMasterCycle();

  REQUIRE(core.generalRegister(4).low == 1);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 0);
}

TEST_CASE("EE signed packed comparison participates in Wide issue")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  system.eeBus().write32(
    0,
    UINT32_C(0x70000000) |
      (UINT32_C(1) << 21) |
      (UINT32_C(2) << 16) |
      (UINT32_C(3) << 11) |
      (UINT32_C(0x02) << 6) |
      UINT32_C(0x08));
  system.eeBus().write32(4, UINT32_C(0x24040001));
  core.setGeneralRegister(
    1,
    {
      UINT64_C(0x800000007fffffff),
      UINT64_C(0xffffffff00000001)
    });
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0x800000017ffffffe),
      UINT64_C(0xfffffffe00000000)
    });
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(core.lastIssueSelection().instructionCount == 2);
  REQUIRE(
    core.lastIssueSelection().continuation ==
    EEIssueContinuation::YoungerAStageOneCycle);
  REQUIRE(
    core.generalRegister(3).low ==
    UINT64_C(0x00000000ffffffff));
  REQUIRE(
    core.generalRegister(3).high ==
    UINT64_MAX);
  REQUIRE(core.generalRegister(4).low == 0);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 2);

  system.clockMasterCycle();

  REQUIRE(core.generalRegister(4).low == 1);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 0);
}

TEST_CASE("EE signed packed min max participates in Wide issue")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  system.eeBus().write32(
    0,
    UINT32_C(0x70000000) |
      (UINT32_C(1) << 21) |
      (UINT32_C(2) << 16) |
      (UINT32_C(3) << 11) |
      (UINT32_C(0x03) << 6) |
      UINT32_C(0x08));
  system.eeBus().write32(4, UINT32_C(0x24040001));
  core.setGeneralRegister(
    1,
    {
      UINT64_C(0x800000007fffffff),
      UINT64_C(0xffffffff00000000)
    });
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0x800000017ffffffe),
      UINT64_C(0xfffffffe00000000)
    });
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(core.lastIssueSelection().instructionCount == 2);
  REQUIRE(
    core.lastIssueSelection().continuation ==
    EEIssueContinuation::YoungerAStageOneCycle);
  REQUIRE(
    core.generalRegister(3).low ==
    UINT64_C(0x800000017fffffff));
  REQUIRE(
    core.generalRegister(3).high ==
    UINT64_C(0xffffffff00000000));
  REQUIRE(core.generalRegister(4).low == 0);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 2);

  system.clockMasterCycle();

  REQUIRE(core.generalRegister(4).low == 1);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 0);
}

TEST_CASE("EE packed absolute participates in Wide issue")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  system.eeBus().write32(
    0,
    UINT32_C(0x70000000) |
      (UINT32_C(2) << 16) |
      (UINT32_C(3) << 11) |
      (UINT32_C(0x01) << 6) |
      UINT32_C(0x28));
  system.eeBus().write32(4, UINT32_C(0x24040001));
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0x800000007fffffff),
      UINT64_C(0xffffffff00000000)
    });
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(core.lastIssueSelection().instructionCount == 2);
  REQUIRE(
    core.lastIssueSelection().continuation ==
    EEIssueContinuation::YoungerAStageOneCycle);
  REQUIRE(
    core.generalRegister(3).low ==
    UINT64_C(0x7fffffff7fffffff));
  REQUIRE(
    core.generalRegister(3).high ==
    UINT64_C(0x0000000100000000));
  REQUIRE(core.generalRegister(4).low == 0);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 2);

  system.clockMasterCycle();

  REQUIRE(core.generalRegister(4).low == 1);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 0);
}

TEST_CASE("EE packed leading sign count continues after older Wide issue")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  system.eeBus().write32(
    0,
    UINT32_C(0x70000000) |
      (UINT32_C(1) << 21) |
      (UINT32_C(2) << 16) |
      (UINT32_C(3) << 11) |
      (UINT32_C(0x12) << 6) |
      UINT32_C(0x09));
  system.eeBus().write32(
    4,
    UINT32_C(0x70000000) |
      (UINT32_C(4) << 21) |
      (UINT32_C(5) << 11) |
      UINT32_C(0x04));
  core.setGeneralRegister(
    1,
    {
      UINT64_C(0xffff0000ffff0000),
      UINT64_C(0xaaaaaaaaaaaaaaaa)
    });
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0x00ff00ff00ff00ff),
      UINT64_C(0x5555555555555555)
    });
  core.setGeneralRegister(
    4,
    {
      UINT64_C(0x000fff0fff0ff00f),
      UINT64_MAX
    });
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(core.lastIssueSelection().instructionCount == 2);
  REQUIRE(
    core.lastIssueSelection().continuation ==
    EEIssueContinuation::YoungerAStageOneCycle);
  REQUIRE(
    core.generalRegister(3).low ==
    UINT64_C(0x00ff000000ff0000));
  REQUIRE(core.generalRegister(3).high == 0);
  REQUIRE(core.generalRegister(5).low == 0);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 2);

  system.clockMasterCycle();

  REQUIRE(
    core.generalRegister(5).low ==
    UINT64_C(0x0000000b00000007));
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 0);
}

TEST_CASE("EE run control owns resume restart and PC mutation")
{
  SECTION("Same-PC host resume preserves decoded continuation")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    system.eeBus().write32(0, UINT32_C(0x08000003));
    system.eeBus().write32(4, UINT32_C(0x0000000f));
    core.startExecution(0);
    system.clockMasterCycle();
    core.haltExecution();
    system.eeBus().write32(4, UINT32_C(0x24010001));

    core.startExecution(core.programCounter());
    system.clockMasterCycle();

    REQUIRE(
      core.stopReason() ==
      EEStopReason::UndefinedOperation);
    REQUIRE(core.rejectedInstruction() == 0x0000000f);
    REQUIRE(core.generalRegister(1).low == 0);
  }

  SECTION("Different-address start performs a fresh restart")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    system.eeBus().write32(0, UINT32_C(0x08000003));
    system.eeBus().write32(4, UINT32_C(0x0000000f));
    system.eeBus().write32(0x100, UINT32_C(0x24010001));
    core.startExecution(0);
    system.clockMasterCycle();
    core.haltExecution();

    core.startExecution(0x100);
    system.clockMasterCycle();

    REQUIRE(core.generalRegister(1).low == 1);
    REQUIRE(core.stopReason() == EEStopReason::None);
    REQUIRE(core.programCounter() == 0x104);
    const EEAcceptanceRecords &records =
      core.acceptanceRecordsThisCycle();
    REQUIRE(records.size() == 1);
    REQUIRE(records[0].programOrder == 1);
    REQUIRE(records[0].mode == EEAcceptanceMode::Ordinary);
  }

  SECTION("External PC mutation preserves order but drops branch context")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    system.eeBus().write32(0, UINT32_C(0x08000003));
    system.eeBus().write32(4, UINT32_C(0x0000000f));
    system.eeBus().write32(0x100, UINT32_C(0x24010001));
    core.startExecution(0);
    system.clockMasterCycle();
    core.haltExecution();

    core.setProgramCounter(0x100);
    NekoSystem restored;
    REQUIRE_NOTHROW(restored.loadState(system.saveState()));

    restored.eeCore().startExecution(0x100);
    restored.clockMasterCycle();

    REQUIRE(restored.eeCore().generalRegister(1).low == 1);
    REQUIRE(restored.eeCore().programCounter() == 0x104);
    const EEAcceptanceRecords &records =
      restored.eeCore().acceptanceRecordsThisCycle();
    REQUIRE(records.size() == 1);
    REQUIRE(records[0].programOrder == 2);
    REQUIRE(records[0].mode == EEAcceptanceMode::Ordinary);
  }
}
