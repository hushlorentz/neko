#include <sstream>
#include <vector>

#include "catch.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_program_runner.hpp"

namespace
{
  void appendWord(std::vector<std::uint8_t> *bytes, std::uint32_t word)
  {
    bytes->push_back(word & 0xff);
    bytes->push_back((word >> 8) & 0xff);
    bytes->push_back((word >> 16) & 0xff);
    bytes->push_back((word >> 24) & 0xff);
  }

  void appendInstructionPair(
    std::vector<std::uint8_t> *program,
    std::uint32_t upper,
    std::uint32_t lower = VPU_LOWER_NOP)
  {
    appendWord(program, lower);
    appendWord(program, upper);
  }
}

TEST_CASE("VPU Program Runner Diagnostics")
{
  SECTION("A run result captures the final architectural state and output memory")
  {
    VPU vpu;
    VPUProgramRunConfig config;
    appendInstructionPair(&config.microProgram, VPU_E_BIT | VPU_NOP);
    appendInstructionPair(&config.microProgram, VPU_NOP);
    config.cycleBudget = 10;
    config.outputAddress = 4;
    config.outputSize = 4;
    vpu.writeDataMemory(4, {0x11, 0x22, 0x33, 0x44});

    VPUProgramRunResult result = runVPUProgram(&vpu, config);

    REQUIRE(result.state == VPU_STATE_READY);
    REQUIRE(result.programCounter == 16);
    REQUIRE(result.elapsedCycles < config.cycleBudget);
    REQUIRE(result.hasTerminationPosition);
    REQUIRE(result.terminationPosition == 2);
    REQUIRE(result.outputMemory ==
      std::vector<std::uint8_t>({0x11, 0x22, 0x33, 0x44}));
    REQUIRE(result.traceEvents.empty());
  }

  SECTION("Trace capture and JSONL output can be limited to a cycle range")
  {
    VPU vpu;
    VPUProgramRunConfig config;
    std::ostringstream traceOutput;
    appendInstructionPair(&config.microProgram, VPU_NOP);
    appendInstructionPair(&config.microProgram, VPU_NOP);
    appendInstructionPair(&config.microProgram, VPU_E_BIT | VPU_NOP);
    appendInstructionPair(&config.microProgram, VPU_NOP);
    config.cycleBudget = 10;
    config.captureTrace = true;
    config.traceStartCycle = 1;
    config.traceEndCycle = 1;
    config.traceOutput = &traceOutput;

    VPUProgramRunResult result = runVPUProgram(&vpu, config);

    REQUIRE(result.traceEvents.size() == 1);
    REQUIRE(result.traceEvents[0].type ==
      VPUTraceEventType::InstructionIssued);
    REQUIRE(result.traceEvents[0].cycle == 1);
    REQUIRE(result.traceEvents[0].instructionAddress == 8);
    REQUIRE(traceOutput.str() ==
      "{\"type\":\"instruction_issued\",\"cycle\":1,"
      "\"instruction_address\":8,\"upper_instruction\":767,"
      "\"lower_instruction\":2147484476,\"opcode\":0,"
      "\"destination_register\":0,\"destination_field_mask\":0}\n");
  }
}
