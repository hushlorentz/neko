#include <cstdint>

#include "catch.hpp"
#include "ee_core.hpp"
#include "neko_system.hpp"

TEST_CASE("EE COP0 registers have deterministic reset state")
{
  NekoSystem system;
  const EECore &core = system.eeCore();

  REQUIRE(
    core.cop0Register(EECOP0Register::Status) ==
    EECOP0Status::RESET);
  REQUIRE(core.cop0Register(EECOP0Register::Cause) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::ErrorEPC) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::BadVAddr) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Count) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Compare) == 0);
  REQUIRE(core.programCounter() == EEReset::VECTOR);
  REQUIRE(core.executionState() == EEExecutionState::Halted);
}

TEST_CASE("EE COP0 register state can be inspected and restored")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  const struct
  {
    EECOP0Register registerIndex;
    std::uint32_t value;
  } contracts[] = {
    {EECOP0Register::BadVAddr, UINT32_C(0x81234567)},
    {EECOP0Register::Count, UINT32_C(0x12345678)},
    {EECOP0Register::Compare, UINT32_C(0x87654321)},
    {EECOP0Register::Status, UINT32_C(0xf0c79c1f)},
    {EECOP0Register::Cause, UINT32_C(0x80008030)},
    {EECOP0Register::EPC, UINT32_C(0x80001000)},
    {EECOP0Register::ErrorEPC, UINT32_C(0xbfc00000)}
  };

  for (const auto &contract : contracts)
  {
    core.setCOP0Register(
      contract.registerIndex,
      contract.value);
    REQUIRE(
      core.cop0Register(contract.registerIndex) ==
      contract.value);
  }

  core.reset();

  REQUIRE(
    core.cop0Register(EECOP0Register::Status) ==
    EECOP0Status::RESET);
  REQUIRE(core.cop0Register(EECOP0Register::Cause) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::ErrorEPC) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::BadVAddr) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Count) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Compare) == 0);
  REQUIRE(core.programCounter() == EEReset::VECTOR);
}

TEST_CASE("Neko system reset returns the EE to architectural Reset entry")
{
  NekoSystem system;
  system.eeCore().setProgramCounter(0x80001000);
  system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
  system.eeCore().setCOP0Register(
    EECOP0Register::ErrorEPC,
    UINT32_MAX);
  system.eeCore().startExecution(0);
  system.clockMasterCycle();

  system.reset();

  REQUIRE(system.eeCore().programCounter() == EEReset::VECTOR);
  REQUIRE(
    system.eeCore().cop0Register(EECOP0Register::Status) ==
    EECOP0Status::RESET);
  REQUIRE(
    system.eeCore().cop0Register(EECOP0Register::ErrorEPC) == 0);
  REQUIRE(
    system.eeCore().executionState() ==
    EEExecutionState::Halted);
  REQUIRE(system.eeCore().stopReason() == EEStopReason::None);
  REQUIRE(system.eeCore().elapsedCycles() == 0);
}

TEST_CASE("EE Reset fetch faults enter the bootstrap exception vector")
{
  NekoSystem system;
  system.eeCore().startExecution(
    system.eeCore().programCounter());

  system.clockMasterCycle();

  REQUIRE(
    system.eeCore().programCounter() ==
    EEExceptionVector::BOOTSTRAP_GENERAL);
  REQUIRE(
    system.eeCore().executionState() ==
    EEExecutionState::Running);
  REQUIRE(system.eeCore().stopReason() == EEStopReason::None);
  REQUIRE(
    system.eeCore().pendingException() ==
    EEException::InstructionBusError);
  REQUIRE(
    system.eeCore().exceptionAddress() ==
    EEReset::VECTOR);
  REQUIRE(
    system.eeCore().cop0Register(EECOP0Register::EPC) ==
    EEReset::VECTOR);
  REQUIRE_FALSE(system.eeCore().hasLastInstruction());
}

TEST_CASE("EE COP0 rejects unimplemented register identifiers")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  const EECOP0Register unsupported =
    static_cast<EECOP0Register>(7);

  REQUIRE_THROWS_WITH(
    core.cop0Register(unsupported),
    "EE COP0 register is not implemented.");
  REQUIRE_THROWS_WITH(
    core.setCOP0Register(unsupported, 0),
    "EE COP0 register is not implemented.");
}
