#include <cstdint>

#include "catch.hpp"
#include "ee_core.hpp"
#include "neko_system.hpp"

namespace
{
  std::uint32_t memoryInstruction(
    std::uint8_t opcode,
    std::uint8_t base,
    std::uint8_t target,
    std::uint16_t offset)
  {
    return
      (static_cast<std::uint32_t>(opcode) << 26) |
      (static_cast<std::uint32_t>(base) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      offset;
  }

  void setRegister(
    EECore *core,
    std::uint8_t index,
    std::uint64_t low)
  {
    core->setGeneralRegister(
      index,
      {low, UINT64_C(0xfeedfacecafebeef)});
  }

  void runInstruction(
    NekoSystem *system,
    std::uint32_t instruction)
  {
    system->eeBus().write32(0, instruction);
    system->eeCore().startExecution(0);
    system->clockMasterCycle();
  }
}

TEST_CASE("EE signed and unsigned byte loads")
{
  SECTION("LB sign extends and preserves the upper GPR doubleword")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, UINT64_C(0x1234000000000101));
    setRegister(&core, 2, 0);
    REQUIRE(system.eeBus().writeData8(0x100, 0x80));

    runInstruction(
      &system,
      memoryInstruction(0x20, 1, 2, 0xffff));

    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0xffffffffffffff80));
    REQUIRE(
      core.generalRegister(2).high ==
      UINT64_C(0xfeedfacecafebeef));
  }

  SECTION("LBU zero extends the loaded byte")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, 0x20000100);
    setRegister(&core, 2, UINT64_MAX);
    REQUIRE(system.eeBus().writeData8(0x100, 0xfe));

    runInstruction(
      &system,
      memoryInstruction(0x24, 1, 2, 0));

    REQUIRE(core.generalRegister(2).low == 0xfe);
    REQUIRE(
      core.generalRegister(2).high ==
      UINT64_C(0xfeedfacecafebeef));
  }
}

TEST_CASE("EE byte stores write the least-significant byte")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  setRegister(&core, 1, 0xa0000101);
  setRegister(&core, 2, UINT64_C(0x11223344556677ab));

  runInstruction(
    &system,
    memoryInstruction(0x28, 1, 2, 1));

  std::uint8_t value = 0;
  REQUIRE(system.eeBus().readData8(0x102, &value));
  REQUIRE(value == 0xab);
}

TEST_CASE("EE byte memory faults preserve architectural results")
{
  SECTION("An unmapped load preserves its destination")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, EEMemoryMap::MAIN_MEMORY_SIZE);
    setRegister(&core, 2, 0x1234);

    runInstruction(
      &system,
      memoryInstruction(0x20, 1, 2, 0));

    REQUIRE(core.executionState() == EEExecutionState::Running);
    REQUIRE(core.stopReason() == EEStopReason::None);
    REQUIRE(
      core.pendingException() ==
      EEException::DataBusErrorLoad);
    REQUIRE(
      core.exceptionAddress() ==
      EEMemoryMap::MAIN_MEMORY_SIZE);
    REQUIRE(
      core.programCounter() ==
      EEExceptionVector::BOOTSTRAP_GENERAL);
    REQUIRE(core.generalRegister(2).low == 0x1234);
  }

  SECTION("An unmapped store reports a distinct store fault")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, EEMemoryMap::MAIN_MEMORY_SIZE);
    setRegister(&core, 2, 0xab);

    runInstruction(
      &system,
      memoryInstruction(0x28, 1, 2, 0));

    REQUIRE(
      core.pendingException() ==
      EEException::DataBusErrorStore);
    REQUIRE(
      core.rejectedInstruction() ==
      memoryInstruction(0x28, 1, 2, 0));
  }
}

TEST_CASE("EE byte loads can execute in a branch delay slot")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  setRegister(&core, 1, 0x100);
  REQUIRE(system.eeBus().writeData8(0x100, 0x7f));
  system.eeBus().write32(
    0,
    memoryInstruction(0x04, 0, 0, 2));
  system.eeBus().write32(
    4,
    memoryInstruction(0x20, 1, 2, 0));
  system.eeBus().write32(
    12,
    memoryInstruction(0x0d, 0, 3, 1));
  core.startExecution(0);
  system.runMasterCycles(3);

  REQUIRE(core.generalRegister(2).low == 0x7f);
  REQUIRE(core.generalRegister(3).low == 1);
}

TEST_CASE("EE byte faults restart from the preceding branch")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  setRegister(&core, 1, EEMemoryMap::MAIN_MEMORY_SIZE);
  system.eeBus().write32(
    0,
    memoryInstruction(0x04, 0, 0, 2));
  system.eeBus().write32(
    4,
    memoryInstruction(0x20, 1, 2, 0));
  system.eeBus().write32(
    12,
    memoryInstruction(0x0d, 0, 3, 1));
  core.startExecution(0);
  system.runMasterCycles(2);

  REQUIRE(core.stopReason() == EEStopReason::None);
  REQUIRE(core.pendingException() == EEException::DataBusErrorLoad);
  REQUIRE(
    core.programCounter() ==
    EEExceptionVector::BOOTSTRAP_GENERAL);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);

  setRegister(&core, 1, 0x100);
  REQUIRE(system.eeBus().writeData8(0x100, 0x7f));
  core.clearPendingException();
  core.startExecution(core.cop0Register(EECOP0Register::EPC));
  system.runMasterCycles(3);

  REQUIRE(core.generalRegister(2).low == 0x7f);
  REQUIRE(core.generalRegister(3).low == 1);
}
