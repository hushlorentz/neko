#include <cstdint>

#include "catch.hpp"
#include "neko_system.hpp"

namespace
{
  std::uint32_t immediateInstruction(
    std::uint8_t opcode,
    std::uint8_t source,
    std::uint8_t target,
    std::uint16_t immediate)
  {
    return
      (static_cast<std::uint32_t>(opcode) << 26) |
      (static_cast<std::uint32_t>(source) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      immediate;
  }

  std::uint32_t registerInstruction(
    std::uint8_t function,
    std::uint8_t source,
    std::uint8_t target,
    std::uint8_t destination)
  {
    return
      (static_cast<std::uint32_t>(source) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      (static_cast<std::uint32_t>(destination) << 11) |
      function;
  }
}

TEST_CASE("EE bounded execution reports cycle-limit progress")
{
  NekoSystem system;
  system.eeBus().write32(
    0,
    immediateInstruction(0x0d, 0, 1, 1));
  system.eeBus().write32(
    4,
    immediateInstruction(0x0d, 0, 2, 2));
  system.eeBus().write32(
    8,
    immediateInstruction(0x0d, 0, 3, 3));
  system.eeCore().startExecution(0);

  const EEExecutionResult result = system.runEE(2);

  REQUIRE(result.masterCycles == 2);
  REQUIRE(result.eeCycles == 2);
  REQUIRE(result.instructions == 2);
  REQUIRE(result.cycleLimitReached);
  REQUIRE(result.state == EEExecutionState::Running);
  REQUIRE(result.stopReason == EEStopReason::None);
  REQUIRE(result.programCounter == 8);
  REQUIRE(result.pendingException == EEException::None);
  REQUIRE(result.exceptionAddress == 0);
  REQUIRE(system.masterClockScheduler().currentCycle() == 2);
}

TEST_CASE("EE bounded execution reports architectural stops")
{
  NekoSystem system;
  system.eeBus().write32(0, 0);
  system.eeBus().write32(4, UINT32_C(0xbc000000));
  system.eeCore().startExecution(0);

  const EEExecutionResult result = system.runEE(20);

  REQUIRE(result.masterCycles == 2);
  REQUIRE(result.eeCycles == 2);
  REQUIRE(result.instructions == 1);
  REQUIRE_FALSE(result.cycleLimitReached);
  REQUIRE(result.state == EEExecutionState::Halted);
  REQUIRE(result.stopReason == EEStopReason::UnsupportedInstruction);
  REQUIRE(result.programCounter == 4);
  REQUIRE(result.pendingException == EEException::None);
}

TEST_CASE("EE bounded execution reports pending exceptions")
{
  NekoSystem system;
  system.eeCore().startExecution(2);

  const EEExecutionResult result = system.runEE(5);

  REQUIRE(result.masterCycles == 1);
  REQUIRE(result.eeCycles == 1);
  REQUIRE(result.instructions == 0);
  REQUIRE_FALSE(result.cycleLimitReached);
  REQUIRE(result.state == EEExecutionState::Running);
  REQUIRE(result.stopReason == EEStopReason::None);
  REQUIRE(
    result.pendingException ==
    EEException::AddressErrorLoadOrFetch);
  REQUIRE(result.exceptionAddress == 2);
  REQUIRE(
    result.programCounter ==
    EEExceptionVector::BOOTSTRAP_GENERAL);
}

TEST_CASE("EE instruction stepping follows repeated branch addresses")
{
  NekoSystem system;
  system.eeBus().write32(
    0,
    immediateInstruction(0x04, 0, 0, 0xffff));
  system.eeBus().write32(4, 0);
  system.eeCore().startExecution(0);

  const EEExecutionResult branch = system.stepEEInstruction(1);
  const EEExecutionResult delay = system.stepEEInstruction(1);
  const EEExecutionResult repeatedBranch =
    system.stepEEInstruction(1);

  REQUIRE(branch.instructions == 1);
  REQUIRE(branch.programCounter == 4);
  REQUIRE_FALSE(branch.cycleLimitReached);
  REQUIRE(delay.instructions == 1);
  REQUIRE(delay.programCounter == 0);
  REQUIRE_FALSE(delay.cycleLimitReached);
  REQUIRE(repeatedBranch.instructions == 1);
  REQUIRE(repeatedBranch.programCounter == 4);
  REQUIRE_FALSE(repeatedBranch.cycleLimitReached);
}

TEST_CASE("EE instruction stepping waits through execution latency")
{
  NekoSystem system;
  system.eeCore().setGeneralRegister(1, {3, 0});
  system.eeCore().setGeneralRegister(2, {4, 0});
  system.eeBus().write32(
    0,
    registerInstruction(0x18, 1, 2, 3));
  system.eeBus().write32(
    4,
    immediateInstruction(0x0d, 0, 4, 1));
  system.eeCore().startExecution(0);

  const EEExecutionResult multiply =
    system.stepEEInstruction(1);
  const EEExecutionResult following =
    system.stepEEInstruction(10);

  REQUIRE(multiply.masterCycles == 1);
  REQUIRE(multiply.instructions == 1);
  REQUIRE(following.masterCycles == 4);
  REQUIRE(following.eeCycles == 4);
  REQUIRE(following.instructions == 1);
  REQUIRE_FALSE(following.cycleLimitReached);
  REQUIRE(following.programCounter == 8);
  REQUIRE(system.eeCore().generalRegister(4).low == 1);
}

TEST_CASE("EE instruction stepping can stop at its cycle bound")
{
  NekoSystem system;
  system.eeCore().setGeneralRegister(1, {3, 0});
  system.eeCore().setGeneralRegister(2, {4, 0});
  system.eeBus().write32(
    0,
    registerInstruction(0x18, 1, 2, 3));
  system.eeBus().write32(4, 0);
  system.eeCore().startExecution(0);
  REQUIRE(system.stepEEInstruction(1).instructions == 1);

  const EEExecutionResult result =
    system.stepEEInstruction(2);

  REQUIRE(result.masterCycles == 2);
  REQUIRE(result.eeCycles == 2);
  REQUIRE(result.instructions == 0);
  REQUIRE(result.cycleLimitReached);
  REQUIRE(result.state == EEExecutionState::Running);
  REQUIRE(result.programCounter == 4);
}

TEST_CASE("EE bounded execution handles a zero cycle budget")
{
  NekoSystem system;
  system.eeBus().write32(0, 0);
  system.eeCore().startExecution(0);

  const EEExecutionResult result = system.runEE(0);

  REQUIRE(result.masterCycles == 0);
  REQUIRE(result.eeCycles == 0);
  REQUIRE(result.instructions == 0);
  REQUIRE(result.cycleLimitReached);
  REQUIRE(result.state == EEExecutionState::Running);
  REQUIRE(result.programCounter == 0);
}

TEST_CASE("EE execution control reports an already halted core")
{
  NekoSystem system;

  const EEExecutionResult step = system.stepEEInstruction(1);
  const EEExecutionResult run = system.runEE(1);

  REQUIRE(step.masterCycles == 0);
  REQUIRE(step.eeCycles == 0);
  REQUIRE(step.instructions == 0);
  REQUIRE_FALSE(step.cycleLimitReached);
  REQUIRE(step.state == EEExecutionState::Halted);
  REQUIRE(step.stopReason == EEStopReason::None);
  REQUIRE(step.programCounter == EEReset::VECTOR);
  REQUIRE(run.masterCycles == 0);
  REQUIRE(run.eeCycles == 0);
  REQUIRE(run.instructions == 0);
  REQUIRE_FALSE(run.cycleLimitReached);
  REQUIRE(run.state == EEExecutionState::Halted);
}
