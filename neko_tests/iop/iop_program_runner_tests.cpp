#include <cstdint>
#include <stdexcept>

#include "catch.hpp"
#include "iop_bus.hpp"
#include "iop_core.hpp"
#include "iop_program_runner.hpp"

namespace
{
  constexpr std::uint32_t registerInstruction(
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

  constexpr std::uint32_t immediateInstruction(
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

  constexpr std::uint32_t jumpInstruction(
    std::uint8_t opcode,
    std::uint32_t target)
  {
    return
      (static_cast<std::uint32_t>(opcode) << 26) |
      (target & UINT32_C(0x03ffffff));
  }

  void loadInstruction(
    IOPBus *bus,
    IOPAddress address,
    std::uint32_t instruction)
  {
    const std::uint8_t bytes[] = {
      static_cast<std::uint8_t>(instruction),
      static_cast<std::uint8_t>(instruction >> 8),
      static_cast<std::uint8_t>(instruction >> 16),
      static_cast<std::uint8_t>(instruction >> 24)
    };
    REQUIRE(
      bus->loadPhysical(address, bytes, sizeof(bytes)) ==
      IOPBusStatus::Completed);
  }
}

TEST_CASE("IOP program runner completes through the return contract",
  "[iop][runner]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  loadInstruction(&bus, 0, registerInstruction(0x08, 31, 0, 0));
  loadInstruction(&bus, 4, immediateInstruction(0x09, 0, 2, 0));

  IOPProgramRunConfig config;
  config.maxMasterCycles = 32;
  const IOPProgramRunResult result =
    runIOPProgram(&core, config);

  REQUIRE(result.entryPoint == 0);
  REQUIRE(result.stackPointer == IOPProgramRuntime::STACK_POINTER);
  REQUIRE(result.returnAddress == IOPProgramRuntime::RETURN_ADDRESS);
  REQUIRE(result.masterCycles == 16);
  REQUIRE(result.iopCycles == 2);
  REQUIRE(result.instructions == 2);
  REQUIRE_FALSE(result.cycleLimitReached);
  REQUIRE(result.state == IOPExecutionState::Halted);
  REQUIRE(result.stopReason == IOPStopReason::HostHalt);
  REQUIRE(result.programCounter == IOPProgramRuntime::RETURN_ADDRESS);
  REQUIRE(result.exception.kind == IOPException::None);
  REQUIRE(result.outcome == IOPProgramOutcome::Completed);
  REQUIRE(result.exitCode == 0);
  REQUIRE(
    core.generalRegister(29) ==
    IOPProgramRuntime::STACK_POINTER);
  REQUIRE(
    core.generalRegister(31) ==
    IOPProgramRuntime::RETURN_ADDRESS);
}

TEST_CASE("IOP program runner reports guest failure and custom ABI values",
  "[iop][runner]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  loadInstruction(
    &bus,
    0x100,
    immediateInstruction(0x09, 0, 2, 7));
  loadInstruction(
    &bus,
    0x104,
    registerInstruction(0x08, 31, 0, 0));
  loadInstruction(&bus, 0x108, 0);

  IOPProgramRunConfig config;
  config.entryPoint = 0x100;
  config.stackPointer = 0x1ff000;
  config.returnAddress = 0x200;
  config.maxMasterCycles = 32;
  const IOPProgramRunResult result =
    runIOPProgram(&core, config);

  REQUIRE(result.entryPoint == config.entryPoint);
  REQUIRE(result.stackPointer == config.stackPointer);
  REQUIRE(result.returnAddress == config.returnAddress);
  REQUIRE(result.masterCycles == 24);
  REQUIRE(result.iopCycles == 3);
  REQUIRE(result.instructions == 3);
  REQUIRE(result.programCounter == config.returnAddress);
  REQUIRE(result.outcome == IOPProgramOutcome::GuestReportedFailure);
  REQUIRE(result.exitCode == 7);
}

TEST_CASE("IOP program runner enforces its master cycle budget",
  "[iop][runner]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  loadInstruction(&bus, 0, jumpInstruction(0x02, 0));
  loadInstruction(&bus, 4, 0);

  IOPProgramRunConfig config;
  config.maxMasterCycles = 23;
  const IOPProgramRunResult result =
    runIOPProgram(&core, config);

  REQUIRE(result.masterCycles == 23);
  REQUIRE(result.iopCycles == 2);
  REQUIRE(result.instructions == 2);
  REQUIRE(result.cycleLimitReached);
  REQUIRE(result.state == IOPExecutionState::Running);
  REQUIRE(result.stopReason == IOPStopReason::None);
  REQUIRE(result.programCounter == 0);
  REQUIRE(result.outcome == IOPProgramOutcome::CycleLimitReached);
}

TEST_CASE("IOP program runner handles a zero cycle budget",
  "[iop][runner]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  loadInstruction(&bus, 0, 0);

  const IOPProgramRunResult result =
    runIOPProgram(&core, {});

  REQUIRE(result.masterCycles == 0);
  REQUIRE(result.iopCycles == 0);
  REQUIRE(result.instructions == 0);
  REQUIRE(result.cycleLimitReached);
  REQUIRE(result.state == IOPExecutionState::Running);
  REQUIRE(result.programCounter == 0);
  REQUIRE(result.outcome == IOPProgramOutcome::CycleLimitReached);
}

TEST_CASE("IOP program runner preserves stop and exception diagnostics",
  "[iop][runner]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  loadInstruction(&bus, 0, UINT32_C(0xffffffff));

  IOPProgramRunConfig config;
  config.maxMasterCycles = 8;
  const IOPProgramRunResult result =
    runIOPProgram(&core, config);

  REQUIRE(result.masterCycles == 8);
  REQUIRE(result.iopCycles == 1);
  REQUIRE(result.instructions == 0);
  REQUIRE_FALSE(result.cycleLimitReached);
  REQUIRE(result.state == IOPExecutionState::Halted);
  REQUIRE(result.stopReason == IOPStopReason::ReservedInstruction);
  REQUIRE(result.programCounter == 0);
  REQUIRE(result.exception.kind == IOPException::ReservedInstruction);
  REQUIRE(result.exception.badVirtualAddress == 0);
  REQUIRE_FALSE(result.exception.inBranchDelaySlot);
  REQUIRE(result.outcome == IOPProgramOutcome::Exception);
}

TEST_CASE("IOP program runner reports non-exception execution stops",
  "[iop][runner]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  loadInstruction(&bus, 0, UINT32_C(0x0000000c));

  IOPProgramRunConfig config;
  config.maxMasterCycles = 8;
  const IOPProgramRunResult result =
    runIOPProgram(&core, config);

  REQUIRE(result.masterCycles == 8);
  REQUIRE(result.iopCycles == 1);
  REQUIRE(result.instructions == 0);
  REQUIRE(result.state == IOPExecutionState::Halted);
  REQUIRE(result.stopReason == IOPStopReason::ExecutionException);
  REQUIRE(result.exception.kind == IOPException::None);
  REQUIRE(result.outcome == IOPProgramOutcome::Stopped);
}

TEST_CASE("IOP program runner rejects a missing core",
  "[iop][runner]")
{
  REQUIRE_THROWS_AS(
    runIOPProgram(nullptr, {}),
    std::invalid_argument);
}

TEST_CASE("IOP program runner rejects an unattached bus before mutation",
  "[iop][runner]")
{
  IOPCore core;
  const IOPAddress originalProgramCounter =
    core.programCounter();

  IOPProgramRunConfig config;
  config.entryPoint = 0x100;
  config.maxMasterCycles = 8;

  REQUIRE_THROWS_AS(
    runIOPProgram(&core, config),
    std::invalid_argument);
  REQUIRE(core.programCounter() == originalProgramCounter);
  REQUIRE(core.elapsedCycles() == 0);
  REQUIRE(core.executionState() == IOPExecutionState::Halted);
  REQUIRE(core.stopReason() == IOPStopReason::None);
}
