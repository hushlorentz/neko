#include <cstdint>

#include "catch.hpp"
#include "ee_core.hpp"
#include "neko_system.hpp"

TEST_CASE("EE COP0 registers have deterministic reset state")
{
  NekoSystem system;
  const EECore &core = system.eeCore();

  REQUIRE(core.cop0Register(EECOP0Register::Index) == 0);
  REQUIRE(
    core.cop0Register(EECOP0Register::Random) ==
    EECOP0Random::RESET);
  REQUIRE(core.cop0Register(EECOP0Register::EntryLo0) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::EntryLo1) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Context) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::PageMask) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Wired) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::EntryHi) == 0);
  REQUIRE(
    core.cop0Register(EECOP0Register::Status) ==
    EECOP0Status::RESET);
  REQUIRE(core.cop0Register(EECOP0Register::Cause) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
  REQUIRE(
    core.cop0Register(EECOP0Register::Config) ==
    EECOP0Config::RESET);
  REQUIRE(core.cop0Register(EECOP0Register::TagLo) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::TagHi) == 0);
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

TEST_CASE("EE COP0 translation and cache registers canonicalize state")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  core.setCOP0Register(EECOP0Register::Index, UINT32_MAX);
  core.setCOP0Register(EECOP0Register::Random, 17);
  core.setCOP0Register(EECOP0Register::EntryLo0, UINT32_MAX);
  core.setCOP0Register(EECOP0Register::EntryLo1, UINT32_MAX);
  core.setCOP0Register(EECOP0Register::Context, UINT32_MAX);
  core.setCOP0Register(
    EECOP0Register::PageMask,
    UINT32_C(0x80000000) | EECOP0PageMask::SIZE_16_KIB);
  core.setCOP0Register(EECOP0Register::Wired, 47);
  core.setCOP0Register(EECOP0Register::EntryHi, UINT32_MAX);
  core.setCOP0Register(EECOP0Register::Config, UINT32_MAX);
  core.setCOP0Register(EECOP0Register::TagLo, UINT32_MAX);
  core.setCOP0Register(EECOP0Register::TagHi, UINT32_MAX);

  REQUIRE(
    core.cop0Register(EECOP0Register::Index) ==
    EECOP0Index::IMPLEMENTED_MASK);
  REQUIRE(core.cop0Register(EECOP0Register::Random) == 17);
  REQUIRE(
    core.cop0Register(EECOP0Register::EntryLo0) ==
    EECOP0EntryLo::ENTRY_LO_0_IMPLEMENTED_MASK);
  REQUIRE(
    core.cop0Register(EECOP0Register::EntryLo1) ==
    EECOP0EntryLo::ENTRY_LO_1_IMPLEMENTED_MASK);
  REQUIRE(
    core.cop0Register(EECOP0Register::Context) ==
    EECOP0Context::IMPLEMENTED_MASK);
  REQUIRE(
    core.cop0Register(EECOP0Register::PageMask) ==
    EECOP0PageMask::SIZE_16_KIB);
  REQUIRE(core.cop0Register(EECOP0Register::Wired) == 47);
  REQUIRE(
    core.cop0Register(EECOP0Register::EntryHi) ==
    EECOP0EntryHi::IMPLEMENTED_MASK);
  REQUIRE(
    core.cop0Register(EECOP0Register::Config) ==
    (EECOP0Config::WRITABLE_MASK | EECOP0Config::FIXED));
  REQUIRE(
    core.cop0Register(EECOP0Register::TagLo) ==
    EECOP0TagLo::IMPLEMENTED_MASK);
  REQUIRE(
    core.cop0Register(EECOP0Register::TagHi) ==
    UINT32_MAX);

  REQUIRE_THROWS_WITH(
    core.setCOP0Register(EECOP0Register::Random, 48),
    "EE COP0 Random index is outside the 48-entry TLB.");
  REQUIRE_THROWS_WITH(
    core.setCOP0Register(EECOP0Register::Wired, 48),
    "EE COP0 Wired index is outside the 48-entry TLB.");
  REQUIRE_THROWS_WITH(
    core.setCOP0Register(
      EECOP0Register::PageMask,
      UINT32_C(0x00002000)),
    "EE COP0 PageMask encoding is unsupported.");
}

TEST_CASE("EE COP0 translation and cache registers affect state hashes")
{
  const struct
  {
    EECOP0Register registerIndex;
    std::uint32_t value;
  } contracts[] = {
    {EECOP0Register::Index, 1},
    {EECOP0Register::Random, 46},
    {EECOP0Register::EntryLo0, 1},
    {EECOP0Register::EntryLo1, 1},
    {EECOP0Register::Context, UINT32_C(0x10)},
    {EECOP0Register::PageMask, EECOP0PageMask::SIZE_16_KIB},
    {EECOP0Register::Wired, 1},
    {EECOP0Register::EntryHi, 1},
    {EECOP0Register::Config, EECOP0Config::DATA_CACHE_ENABLE},
    {EECOP0Register::TagLo, EECOP0TagLo::VALID},
    {EECOP0Register::TagHi, 1}
  };

  for (const auto &contract : contracts)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    const std::uint64_t resetHash = core.stateHash();

    core.setCOP0Register(
      contract.registerIndex,
      contract.value);

    REQUIRE(core.stateHash() != resetHash);
    core.reset();
    REQUIRE(core.stateHash() == resetHash);
  }
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
