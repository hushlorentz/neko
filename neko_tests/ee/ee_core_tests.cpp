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
};

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
