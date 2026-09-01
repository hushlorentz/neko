#include <cstddef>
#include <cstdint>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "ee_core.hpp"
#include "ee_instruction.hpp"
#include "neko_system.hpp"

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

    core.reset();

    for (std::size_t index = 0;
         index < EECore::GENERAL_REGISTER_COUNT;
         ++index)
    {
      REQUIRE(core.generalRegister(index) == EERegister128{});
    }
    REQUIRE(core.programCounter() == 0);
    REQUIRE(core.hi() == 0);
    REQUIRE(core.lo() == 0);
    REQUIRE(core.hi1() == 0);
    REQUIRE(core.lo1() == 0);
    REQUIRE(core.shiftAmount() == 0);
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

  SECTION("A pending fetch exception blocks later fetches until cleared")
  {
    core.setProgramCounter(2);
    REQUIRE_FALSE(core.fetchInstruction().succeeded);

    bus.write32(0x100, UINT32_C(0x89abcdef));
    core.setProgramCounter(0x100);
    REQUIRE_FALSE(core.fetchInstruction().succeeded);
    REQUIRE(core.programCounter() == 0x100);

    core.clearPendingException();
    const EEInstructionFetchResult result =
      core.fetchInstruction();

    REQUIRE(result.succeeded);
    REQUIRE(result.instruction == 0x89abcdef);
    REQUIRE_FALSE(core.exceptionPending());
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

    REQUIRE(core.programCounter() == 0);
    REQUIRE(core.elapsedCycles() == 0);
  }

  SECTION("Each master cycle fetches and decodes one instruction")
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
  }

  SECTION("A fetch exception stops scheduled execution")
  {
    core.startExecution(2);

    system.clockMasterCycle();

    REQUIRE_FALSE(core.clockActive());
    REQUIRE(core.elapsedCycles() == 1);
    REQUIRE(core.programCounter() == 2);
    REQUIRE(
      core.stopReason() ==
      EEStopReason::FetchException);
    REQUIRE(
      core.pendingException() ==
      EEException::AddressErrorLoadOrFetch);
    REQUIRE_FALSE(core.hasLastInstruction());
  }

  SECTION("Reserved encodings stop without retiring")
  {
    bus.write32(0, UINT32_C(0x4c000000));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE_FALSE(core.clockActive());
    REQUIRE(core.elapsedCycles() == 1);
    REQUIRE(core.programCounter() == 0);
    REQUIRE(
      core.stopReason() ==
      EEStopReason::ReservedInstruction);
    REQUIRE(core.rejectedInstruction() == 0x4c000000);
    REQUIRE_FALSE(core.hasLastInstruction());
  }

  SECTION("Deferred instruction families stop explicitly")
  {
    bus.write32(0, UINT32_C(0xbc000000));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE_FALSE(core.clockActive());
    REQUIRE(core.programCounter() == 0);
    REQUIRE(
      core.stopReason() ==
      EEStopReason::UnsupportedInstruction);
    REQUIRE(core.rejectedInstruction() == 0xbc000000);
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
