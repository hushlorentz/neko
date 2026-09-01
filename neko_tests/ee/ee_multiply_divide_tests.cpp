#include <cstdint>

#include "catch.hpp"
#include "ee_core.hpp"
#include "neko_system.hpp"

namespace
{
  std::uint32_t registerInstruction(
    std::uint8_t function,
    std::uint8_t rs,
    std::uint8_t rt,
    std::uint8_t rd)
  {
    return
      (static_cast<std::uint32_t>(rs) << 21) |
      (static_cast<std::uint32_t>(rt) << 16) |
      (static_cast<std::uint32_t>(rd) << 11) |
      function;
  }

  std::uint32_t mmiInstruction(
    std::uint8_t function,
    std::uint8_t rs,
    std::uint8_t rt,
    std::uint8_t rd)
  {
    return
      UINT32_C(0x70000000) |
      registerInstruction(function, rs, rt, rd);
  }

  std::uint32_t regimmInstruction(
    std::uint8_t rt,
    std::uint8_t rs,
    std::uint16_t immediate)
  {
    return
      UINT32_C(0x04000000) |
      (static_cast<std::uint32_t>(rs) << 21) |
      (static_cast<std::uint32_t>(rt) << 16) |
      immediate;
  }

  void setWord(
    EECore *core,
    std::uint8_t index,
    std::uint32_t value)
  {
    const std::uint64_t extended =
      (value & UINT32_C(0x80000000)) != 0
        ? UINT64_C(0xffffffff00000000) | value
        : value;
    core->setGeneralRegister(
      index,
      {extended, UINT64_C(0xfeedfacecafebeef)});
  }

  void runToCompletion(
    NekoSystem *system,
    std::uint32_t instruction,
    std::uint64_t latency)
  {
    system->eeBus().write32(0, instruction);
    system->eeCore().startExecution(0);
    system->clockMasterCycle();
    system->runMasterCycles(latency);
  }
}

TEST_CASE("EE multiply and multiply-add execution")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  SECTION("Signed multiply writes rd and HI LO after four cycles")
  {
    setWord(&core, 1, UINT32_C(0xfffffffe));
    setWord(&core, 2, 3);
    core.setHI(0x1111);
    core.setLO(0x2222);
    core.setGeneralRegister(
      3,
      {0x3333, UINT64_C(0xfeedfacecafebeef)});
    system.eeBus().write32(
      0,
      registerInstruction(0x18, 1, 2, 3));
    system.eeBus().write32(
      4,
      registerInstruction(0x12, 0, 0, 4));
    core.startExecution(0);

    system.clockMasterCycle();
    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.hi() == 0x1111);
    REQUIRE(core.lo() == 0x2222);
    REQUIRE(core.generalRegister(3).low == 0x3333);

    system.runMasterCycles(3);
    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.lo() == 0x2222);

    system.clockMasterCycle();
    REQUIRE(core.programCounter() == 8);
    REQUIRE(core.hi() == UINT64_MAX);
    REQUIRE(core.lo() == UINT64_C(0xfffffffffffffffa));
    REQUIRE(
      core.generalRegister(3).low ==
      UINT64_C(0xfffffffffffffffa));
    REQUIRE(
      core.generalRegister(3).high ==
      UINT64_C(0xfeedfacecafebeef));
    REQUIRE(
      core.generalRegister(4).low ==
      UINT64_C(0xfffffffffffffffa));
  }

  SECTION("Unsigned multiply sign extends each result word")
  {
    setWord(&core, 1, UINT32_MAX);
    setWord(&core, 2, 2);

    runToCompletion(
      &system,
      registerInstruction(0x19, 1, 2, 3),
      4);

    REQUIRE(core.hi() == 1);
    REQUIRE(core.lo() == UINT64_C(0xfffffffffffffffe));
    REQUIRE(
      core.generalRegister(3).low ==
      UINT64_C(0xfffffffffffffffe));
  }

  SECTION("Multiply-add accumulates the low words of HI and LO")
  {
    setWord(&core, 1, 2);
    setWord(&core, 2, 3);
    core.setHI(UINT64_C(0xffffffff00000000));
    core.setLO(UINT64_C(0x1234567800000005));

    runToCompletion(
      &system,
      mmiInstruction(0x00, 1, 2, 3),
      4);

    REQUIRE(core.hi() == 0);
    REQUIRE(core.lo() == 11);
    REQUIRE(core.generalRegister(3).low == 11);
  }

  SECTION("Pipeline 1 has independent HI1 and LO1 state")
  {
    setWord(&core, 1, 4);
    setWord(&core, 2, 5);
    core.setHI(0x1111);
    core.setLO(0x2222);

    runToCompletion(
      &system,
      mmiInstruction(0x18, 1, 2, 3),
      4);

    REQUIRE(core.hi() == 0x1111);
    REQUIRE(core.lo() == 0x2222);
    REQUIRE(core.hi1() == 0);
    REQUIRE(core.lo1() == 20);
    REQUIRE(core.generalRegister(3).low == 20);
  }
}

TEST_CASE("EE divide execution")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  SECTION("Signed divide produces a truncated quotient and remainder")
  {
    setWord(&core, 1, UINT32_C(0xfffffff9));
    setWord(&core, 2, 3);

    runToCompletion(
      &system,
      registerInstruction(0x1a, 1, 2, 0),
      37);

    REQUIRE(core.lo() == UINT64_C(0xfffffffffffffffe));
    REQUIRE(core.hi() == UINT64_MAX);
  }

  SECTION("Signed minimum divided by negative one has defined results")
  {
    setWord(&core, 1, UINT32_C(0x80000000));
    setWord(&core, 2, UINT32_MAX);

    runToCompletion(
      &system,
      mmiInstruction(0x1a, 1, 2, 0),
      37);

    REQUIRE(core.lo1() == UINT64_C(0xffffffff80000000));
    REQUIRE(core.hi1() == 0);
    REQUIRE(core.exceptionPending() == false);
  }

  SECTION("The manual-defined zero-divisor result remains explicit")
  {
    setWord(&core, 1, 7);
    setWord(&core, 2, 0);

    system.eeBus().write32(
      0,
      registerInstruction(0x1b, 1, 2, 0));
    core.startExecution(0);
    system.clockMasterCycle();

    REQUIRE(core.executionState() == EEExecutionState::Halted);
    REQUIRE(core.stopReason() == EEStopReason::UndefinedOperation);
    REQUIRE(core.programCounter() == 0);
  }
}

TEST_CASE("EE HI LO and shift amount transfers")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  core.setGeneralRegister(
    1,
    {UINT64_C(0x123456789abcdef0), 0});
  system.eeBus().write32(
    0,
    registerInstruction(0x11, 1, 0, 0));
  system.eeBus().write32(
    4,
    registerInstruction(0x10, 0, 0, 2));
  system.eeBus().write32(
    8,
    regimmInstruction(0x18, 1, 3));
  system.eeBus().write32(
    12,
    registerInstruction(0x28, 0, 0, 3));
  system.eeBus().write32(
    16,
    0);
  system.eeBus().write32(
    20,
    0);
  system.eeBus().write32(
    24,
    0);
  system.eeBus().write32(
    28,
    regimmInstruction(0x19, 1, 5));
  system.eeBus().write32(
    32,
    registerInstruction(0x28, 0, 0, 4));
  core.startExecution(0);
  system.runMasterCycles(9);

  REQUIRE(core.hi() == UINT64_C(0x123456789abcdef0));
  REQUIRE(
    core.generalRegister(2).low ==
    UINT64_C(0x123456789abcdef0));
  REQUIRE(core.generalRegister(3).low == 24);
  REQUIRE(core.generalRegister(4).low == 80);
}

TEST_CASE("EE shift amount ordering restrictions are explicit")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  system.eeBus().write32(
    0,
    registerInstruction(0x28, 0, 0, 1));
  system.eeBus().write32(
    4,
    regimmInstruction(0x18, 0, 2));
  core.startExecution(0);
  system.runMasterCycles(2);

  REQUIRE(core.executionState() == EEExecutionState::Halted);
  REQUIRE(core.stopReason() == EEStopReason::UndefinedOperation);
  REQUIRE(core.programCounter() == 4);
}
