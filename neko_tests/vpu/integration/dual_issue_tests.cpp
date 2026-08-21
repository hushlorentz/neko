#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_program_runner.hpp"
#include "vpu_register_ids.hpp"

TEST_CASE("VU0 sustained dual issue integration program")
{
  VPU vpu;
  std::vector<std::uint8_t> initialMemory;
  vpu_integration::appendQword(&initialMemory, 1, 2, 3, 1);
  vpu_integration::appendQword(
    &initialMemory,
    0x3f800000,
    0x40800000,
    0x41000000,
    0x41800000);
  vpu_integration::appendQword(
    &initialMemory,
    0x3f000000,
    0x40000000,
    0x41800000,
    0x41000000);
  for (std::uint8_t output = 0; output < 7; output++)
  {
    vpu_integration::appendQword(
      &initialMemory,
      0xdead0001,
      0xdead0002,
      0xdead0003,
      0xdead0004);
  }
  vpu.writeDataMemory(0, initialMemory);

  VPUProgramRunConfig config;
  config.microProgram = vpu_integration::readBinary("dual_issue.bin");
  config.cycleBudget = 200;
  config.outputAddress = 3 * 16;
  config.outputSize = 7 * 16;
  config.captureTrace = true;

  VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(
    &expected, 0x3fc00000, 0x40c00000, 0x41c00000, 0x41c00000);
  vpu_integration::appendQword(
    &expected, 0x3f000000, 0x40000000, 0xc1000000, 0x41000000);
  vpu_integration::appendQword(
    &expected, 0x3f000000, 0x41000000, 0x43000000, 0x43000000);
  vpu_integration::appendQword(
    &expected, 0x3f800000, 0x40800000, 0x41800000, 0x41800000);
  vpu_integration::appendQword(
    &expected, 0x3f000000, 0x40000000, 0x41000000, 0x41000000);
  vpu_integration::appendQword(
    &expected, 0x3fc00000, 0x40c00000, 0x41c00000, 0x41c00000);
  vpu_integration::appendQword(&expected, 7, 7, 7, 7);

  std::vector<VPUTraceEvent> dualIssues;
  for (const VPUTraceEvent &event : result.traceEvents)
  {
    if (event.type == VPUTraceEventType::InstructionIssued &&
        event.instructionAddress >= 12 * 8 &&
        event.instructionAddress < 18 * 8)
    {
      dualIssues.push_back(event);
    }
  }

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 64);
  REQUIRE(result.programCounter == 28 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 28);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI03) == 10);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI10) == 7);
  REQUIRE(dualIssues.size() == 6);
  for (std::size_t index = 0; index < dualIssues.size(); index++)
  {
    REQUIRE(dualIssues[index].instructionAddress == (index + 12) * 8);
    REQUIRE(dualIssues[index].upperInstruction != VPU_NOP);
    REQUIRE(dualIssues[index].lowerInstruction != VPU_LOWER_NOP);
    if (index > 0)
    {
      CAPTURE(index);
      CAPTURE(dualIssues[index - 1].cycle);
      CAPTURE(dualIssues[index].cycle);
      REQUIRE(dualIssues[index].cycle == dualIssues[index - 1].cycle + 1);
    }
  }
}
