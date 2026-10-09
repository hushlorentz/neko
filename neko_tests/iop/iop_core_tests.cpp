#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "catch.hpp"
#include "iop_core.hpp"

static_assert(
  std::is_final<IOPCore>::value,
  "IOP instruction execution must remain concrete and non-overridable.");
static_assert(
  sizeof(IOPWord) == 4,
  "IOP architectural words must remain exactly 32 bits.");
static_assert(
  sizeof(IOPAddress) == 4,
  "IOP architectural addresses must remain exactly 32 bits.");

struct IOPCoreTestAccess
{
  static void fillState(IOPCore *core)
  {
    core->pc = UINT32_C(0x12345678);
    for (std::size_t index = 0;
         index < core->writableGeneralRegisters.size();
         ++index)
    {
      core->writableGeneralRegisters[index] =
        static_cast<IOPWord>(index + 1);
    }
    core->hiRegister = UINT32_C(0x89abcdef);
    core->loRegister = UINT32_C(0xfedcba98);
    core->cop0.badVirtualAddress = UINT32_C(0x10203040);
    core->cop0.status = UINT32_C(0xffffffff);
    core->cop0.cause = UINT32_C(0xffffffff);
    core->cop0.exceptionProgramCounter =
      UINT32_C(0x50607080);
    core->branch = {
      true,
      UINT32_C(0x1000),
      UINT32_C(0x2000)
    };
    core->delayedResult = {
      IOPDelayedResultSource::Load,
      7,
      UINT32_C(0x13579bdf)
    };
    core->pendingException = {
      IOPException::DataBusError,
      UINT32_C(0x2468ace0),
      true,
      2
    };
    core->state = IOPExecutionState::Running;
    core->haltReason = IOPStopReason::ExecutionException;
    core->cycles = UINT64_C(0x123456789abcdef0);
  }

  static void setGeneralRegister(
    IOPCore *core,
    std::size_t index,
    IOPWord value)
  {
    core->setGeneralRegister(index, value);
  }
};

TEST_CASE("IOP core reset establishes deterministic architectural state")
{
  IOPCore core;
  IOPCoreTestAccess::fillState(&core);

  core.reset();

  REQUIRE(core.programCounter() == IOPReset::VECTOR);
  for (std::size_t index = 0;
       index < IOPCore::GENERAL_REGISTER_COUNT;
       ++index)
  {
    REQUIRE(core.generalRegister(index) == 0);
  }
  REQUIRE(core.hi() == 0);
  REQUIRE(core.lo() == 0);
  REQUIRE(core.badVirtualAddress() == 0);
  REQUIRE(
    core.status() ==
    (IOPCOP0Status::BOOT_EXCEPTION_VECTORS |
     IOPCOP0Status::TLB_SHUTDOWN));
  REQUIRE(core.cause() == 0);
  REQUIRE(core.exceptionProgramCounter() == 0);
  REQUIRE(core.processorRevision() == IOPCOP0::PROCESSOR_REVISION);

  REQUIRE_FALSE(core.branchContinuation().active);
  REQUIRE(core.branchContinuation().instructionAddress == 0);
  REQUIRE(core.branchContinuation().targetAddress == 0);
  REQUIRE(
    core.delayedResultContinuation().source ==
    IOPDelayedResultSource::None);
  REQUIRE(core.delayedResultContinuation().destination == 0);
  REQUIRE(core.delayedResultContinuation().value == 0);
  REQUIRE(core.exception().kind == IOPException::None);
  REQUIRE(core.exception().badVirtualAddress == 0);
  REQUIRE_FALSE(core.exception().inBranchDelaySlot);
  REQUIRE(core.exception().coprocessor == 0);

  REQUIRE(core.executionState() == IOPExecutionState::Halted);
  REQUIRE(core.stopReason() == IOPStopReason::None);
  REQUIRE(core.elapsedCycles() == 0);
}

TEST_CASE("IOP core inspection rejects an invalid register index")
{
  const IOPCore core;

  REQUIRE(core.generalRegister(0) == 0);
  REQUIRE(core.generalRegister(31) == 0);
  REQUIRE_THROWS_WITH(
    core.generalRegister(IOPCore::GENERAL_REGISTER_COUNT),
    "IOP general register index is out of range.");
}

TEST_CASE("IOP core structurally protects general register zero")
{
  IOPCore core;

  IOPCoreTestAccess::setGeneralRegister(
    &core,
    0,
    UINT32_C(0xffffffff));
  IOPCoreTestAccess::setGeneralRegister(
    &core,
    31,
    UINT32_C(0x12345678));

  REQUIRE(core.generalRegister(0) == 0);
  REQUIRE(
    core.generalRegister(31) ==
    UINT32_C(0x12345678));
}
