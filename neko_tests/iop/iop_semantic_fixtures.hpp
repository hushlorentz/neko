#ifndef IOP_SEMANTIC_FIXTURES_HPP
#define IOP_SEMANTIC_FIXTURES_HPP

#include <array>
#include <cstddef>
#include <vector>

#include "iop_instruction.hpp"
#include "iop_program_runner.hpp"

class IOPBus;

namespace IOPSemanticFixtures
{
  struct Instruction
  {
    IOPAddress address;
    IOPWord word;
    IOPOperation operation;
    IOPDecodeDisposition disposition = IOPDecodeDisposition::Supported;
  };

  struct MemoryWord
  {
    IOPAddress address;
    IOPWord value;
  };

  struct RegisterValue
  {
    std::size_t index;
    IOPWord value;
  };

  struct ExpectedExecution
  {
    std::uint64_t instructions = 0;
    IOPCycleCount cycles = 0;
    // Runner budget accounting, not phase-zero whole-system timestamps.
    std::uint64_t masterCycles = 0;
    IOPProgramOutcome outcome = IOPProgramOutcome::Completed;
    IOPAddress programCounter = IOPProgramRuntime::RETURN_ADDRESS;
    IOPExceptionRecord exception;
    IOPWord status = 0x00600000;
    IOPWord cause = 0;
    IOPAddress exceptionProgramCounter = 0;
    IOPWord hi = 0;
    IOPWord lo = 0;
    // Other GPRs are zero except the runner's initial SP and return address.
    std::vector<RegisterValue> registers;
    // Words not listed here must retain their initial values.
    std::vector<MemoryWord> changedMemory;
  };

  struct Fixture
  {
    const char *name;
    IOPProgramRunConfig config = {
      0x1000, IOPProgramRuntime::STACK_POINTER,
      IOPProgramRuntime::RETURN_ADDRESS, 512
    };
    std::vector<Instruction> code;
    std::vector<MemoryWord> initialMemory;
    ExpectedExecution expected;
  };

  const std::array<Fixture, 8> &all();
  void load(IOPBus &bus, const Fixture &fixture);
}

#endif
