#include <cstddef>
#include <cstdint>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "ee_core.hpp"
#include "floating_point_ops.hpp"
#include "ee_instruction.hpp"
#include "neko_system.hpp"

struct EECoreTestAccess
{
  static EEIssueGroupExecutionResult executeIssueGroup(
    EECore *core,
    std::uint8_t memberCount)
  {
    core->acceptanceRecords.clear();
    core->fillIssueFrontEnd();
    return core->executeIssueGroup(memberCount, 0);
  }
};

TEST_CASE("EE acceptance records preserve issue-group order")
{
  EEAcceptanceRecords records;
  REQUIRE(records.size() == 0);

  records.append({
    7,
    0x100,
    decodeEEInstruction(UINT32_C(0x24020001)),
    false
  });
  records.append({
    8,
    0x104,
    decodeEEInstruction(UINT32_C(0x24030002)),
    true
  });

  REQUIRE(records.size() == 2);
  REQUIRE(records.instructionCount() == 2);
  REQUIRE(records[0].programOrder == 7);
  REQUIRE(records[0].address == 0x100);
  REQUIRE(records[0].instruction.raw == 0x24020001);
  REQUIRE_FALSE(records[0].delaySlot);
  REQUIRE(records[1].programOrder == 8);
  REQUIRE(records[1].address == 0x104);
  REQUIRE(records[1].instruction.raw == 0x24030002);
  REQUIRE(records[1].delaySlot);
  REQUIRE_THROWS_AS(
    records.append({
      9,
      0x108,
      decodeEEInstruction(0),
      false
    }),
    std::overflow_error);

  records.clear();
  REQUIRE(records.size() == 0);

  records.append({
    4,
    0x200,
    decodeEEInstruction(0),
    false
  });
  REQUIRE_THROWS_AS(
    records.append({
      3,
      0x204,
      decodeEEInstruction(0),
      false
    }),
    std::invalid_argument);
}

TEST_CASE("EE issue groups stop at precise member boundaries")
{
  SECTION("An older failure suppresses the younger member")
  {
    std::uint8_t attempts = 0;
    const EEIssueGroupExecutionResult result =
      executeEEIssueGroupMembers(
        2,
        [&attempts](std::uint8_t member)
        {
          ++attempts;
          REQUIRE(member == 0);
          return EEIssueMemberExecution::Failed;
        });

    REQUIRE(attempts == 1);
    REQUIRE(result.attempted == 1);
    REQUIRE(result.accepted == 0);
    REQUIRE(result.stoppedMember == 0);
    REQUIRE(
      result.stop ==
      EEIssueMemberExecution::Failed);
  }

  SECTION("A younger failure preserves the older member")
  {
    std::uint32_t architecturalValue = 0;
    EEAcceptanceRecords records;
    const EEIssueGroupExecutionResult result =
      executeEEIssueGroupMembers(
        2,
        [&architecturalValue, &records](
          std::uint8_t member)
        {
          if (member == 0)
          {
            architecturalValue = 7;
            records.append({
              1,
              0,
              decodeEEInstruction(
                UINT32_C(0x24020007)),
              false
            });
            return EEIssueMemberExecution::Accepted;
          }
          REQUIRE(architecturalValue == 7);
          return EEIssueMemberExecution::Failed;
        });

    REQUIRE(architecturalValue == 7);
    REQUIRE(records.size() == 1);
    REQUIRE(records[0].instruction.raw == 0x24020007);
    REQUIRE(result.attempted == 2);
    REQUIRE(result.accepted == 1);
    REQUIRE(result.stoppedMember == 1);
    REQUIRE(
      result.stop ==
      EEIssueMemberExecution::Failed);
  }
}

TEST_CASE("EE Core executes issue groups at precise boundaries")
{
  SECTION("An older exception suppresses younger effects")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Status, 0);
    system.eeBus().write32(0, UINT32_C(0x0000000c));
    system.eeBus().write32(4, UINT32_C(0x24020007));
    core.startExecution(0);

    const EEIssueGroupExecutionResult result =
      EECoreTestAccess::executeIssueGroup(&core, 2);

    REQUIRE(result.attempted == 1);
    REQUIRE(result.accepted == 0);
    REQUIRE(result.stoppedMember == 0);
    REQUIRE(
      result.stop ==
      EEIssueMemberExecution::Failed);
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
      EECoreTestAccess::executeIssueGroup(&core, 2);

    REQUIRE(result.attempted == 2);
    REQUIRE(result.accepted == 1);
    REQUIRE(result.stoppedMember == 1);
    REQUIRE(
      result.stop ==
      EEIssueMemberExecution::Failed);
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

  SECTION("A younger stop preserves older effects and owns the reason")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    system.eeBus().write32(0, UINT32_C(0x24020007));
    system.eeBus().write32(4, UINT32_C(0xbc000000));
    core.startExecution(0);

    const EEIssueGroupExecutionResult result =
      EECoreTestAccess::executeIssueGroup(&core, 2);

    REQUIRE(result.attempted == 2);
    REQUIRE(result.accepted == 1);
    REQUIRE(result.stoppedMember == 1);
    REQUIRE(
      result.stop ==
      EEIssueMemberExecution::Failed);
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
      EECoreTestAccess::executeIssueGroup(&core, 2);

    REQUIRE(result.accepted == 1);
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
    REQUIRE_FALSE(records[0].delaySlot);
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
