#include "iop_program_runner.hpp"

#include <stdexcept>

#include "iop_core.hpp"

IOPProgramRunResult runIOPProgram(
  IOPCore *core,
  const IOPProgramRunConfig &config)
{
  if (core == nullptr)
  {
    throw std::invalid_argument(
      "IOP program runner requires a core.");
  }
  if (!core->hasAttachedBus())
  {
    throw std::invalid_argument(
      "IOP program runner requires an attached bus.");
  }

  core->prepareFreshExecution(
    config.entryPoint,
    config.stackPointer,
    config.returnAddress);
  core->startExecution(config.entryPoint);

  const IOPCycleCount startingCycles =
    core->elapsedCycles();
  const std::uint64_t startingInstructions =
    core->retiredInstructions();
  std::uint64_t masterCycles = 0;
  bool returned = false;
  bool cycleLimitReached = false;

  while (core->executionState() == IOPExecutionState::Running)
  {
    if (core->programCounter() == config.returnAddress)
    {
      core->haltExecution();
      returned = true;
      break;
    }
    if (config.maxMasterCycles - masterCycles <
        IOPProgramRuntime::MASTER_CYCLE_PERIOD)
    {
      // Master time continues between IOP clock edges.
      masterCycles = config.maxMasterCycles;
      cycleLimitReached = true;
      break;
    }
    core->stepInstruction();
    masterCycles += IOPProgramRuntime::MASTER_CYCLE_PERIOD;
  }

  IOPProgramRunResult result;
  result.entryPoint = config.entryPoint;
  result.stackPointer = config.stackPointer;
  result.returnAddress = config.returnAddress;
  result.masterCycles = masterCycles;
  result.iopCycles =
    core->elapsedCycles() - startingCycles;
  result.instructions =
    core->retiredInstructions() - startingInstructions;
  result.cycleLimitReached = cycleLimitReached;
  result.state = core->executionState();
  result.stopReason = core->stopReason();
  result.programCounter = core->programCounter();
  result.exception = core->exception();
  result.exitCode =
    core->generalRegister(
      IOPProgramRuntime::EXIT_CODE_REGISTER);

  if (returned)
  {
    result.outcome = result.exitCode == 0
      ? IOPProgramOutcome::Completed
      : IOPProgramOutcome::GuestReportedFailure;
  }
  else if (cycleLimitReached)
  {
    result.outcome = IOPProgramOutcome::CycleLimitReached;
  }
  else if (result.exception.kind != IOPException::None)
  {
    result.outcome = IOPProgramOutcome::Exception;
  }
  else
  {
    result.outcome = IOPProgramOutcome::Stopped;
  }
  return result;
}
