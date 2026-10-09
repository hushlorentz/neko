#ifndef IOP_PROGRAM_RUNNER_HPP
#define IOP_PROGRAM_RUNNER_HPP

#include <cstddef>
#include <cstdint>

#include "iop_types.hpp"

class IOPCore;

namespace IOPProgramRuntime
{
  constexpr IOPAddress STACK_POINTER = UINT32_C(0x00200000);
  constexpr IOPAddress RETURN_ADDRESS = UINT32_C(0xfffffffc);
  constexpr std::size_t EXIT_CODE_REGISTER = 2;
  constexpr std::uint64_t MASTER_CYCLE_PERIOD = 8;
}

enum class IOPProgramOutcome : std::uint8_t
{
  Completed,
  GuestReportedFailure,
  CycleLimitReached,
  Exception,
  Stopped
};

struct IOPProgramRunConfig
{
  IOPAddress entryPoint = 0;
  IOPAddress stackPointer = IOPProgramRuntime::STACK_POINTER;
  IOPAddress returnAddress = IOPProgramRuntime::RETURN_ADDRESS;
  std::uint64_t maxMasterCycles = 0;
};

struct IOPProgramRunResult
{
  IOPAddress entryPoint = 0;
  IOPAddress stackPointer = 0;
  IOPAddress returnAddress = 0;
  std::uint64_t masterCycles = 0;
  IOPCycleCount iopCycles = 0;
  std::uint64_t instructions = 0;
  bool cycleLimitReached = false;
  IOPExecutionState state = IOPExecutionState::Halted;
  IOPStopReason stopReason = IOPStopReason::None;
  IOPAddress programCounter = 0;
  IOPPendingException exception;
  IOPProgramOutcome outcome = IOPProgramOutcome::Stopped;
  IOPWord exitCode = 0;
};

// Program bytes must already be installed through IOPBus host loading.
IOPProgramRunResult runIOPProgram(
  IOPCore *core,
  const IOPProgramRunConfig &config);

#endif
