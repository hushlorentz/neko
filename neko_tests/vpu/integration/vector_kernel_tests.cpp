#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_program_runner.hpp"
#include "vpu_register_ids.hpp"

TEST_CASE("VU0 vector kernel capstone integration program")
{
  VPU vpu;
  std::vector<std::uint8_t> initialMemory;
  vpu_integration::appendQword(&initialMemory, 3, 5, 8, 1);
  vpu_integration::appendQword(
    &initialMemory,
    0x40000000,
    0x3f000000,
    0x40800000,
    0x3e800000);
  vpu_integration::appendQword(
    &initialMemory,
    0x3f800000,
    0xbf800000,
    0x40000000,
    0x3f000000);
  vpu_integration::appendQword(&initialMemory, 0, 0, 0, 0);
  vpu_integration::appendQword(
    &initialMemory,
    0x41200000,
    0x41200000,
    0x41200000,
    0x41200000);
  vpu_integration::appendQword(
    &initialMemory,
    0xbf800000,
    0x40800000,
    0x3f800000,
    0x41000000);
  vpu_integration::appendQword(
    &initialMemory,
    0x40400000,
    0x41f00000,
    0xbf800000,
    0x42c80000);
  vpu_integration::appendQword(
    &initialMemory,
    0x41200000,
    0xc0800000,
    0x40400000,
    0xc1000000);
  for (std::uint8_t output = 0; output < 3; output++)
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
  config.microProgram = vpu_integration::readBinary("vector_kernel.bin");
  config.cycleBudget = 500;
  config.outputAddress = 8 * 16;
  config.outputSize = 3 * 16;
  config.captureTrace = true;

  VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(
    &expected, 0, 0x3f800000, 0x40c00000, 0x40200000);
  vpu_integration::appendQword(
    &expected, 0x40e00000, 0x41200000, 0, 0x41200000);
  vpu_integration::appendQword(
    &expected, 0x41200000, 0, 0x41200000, 0);

  std::size_t loopEntries = 0;
  std::size_t loopBranches = 0;
  for (const VPUTraceEvent &event : result.traceEvents)
  {
    if (event.type != VPUTraceEventType::InstructionIssued)
    {
      continue;
    }
    loopEntries += event.instructionAddress == 9 * 8 ? 1 : 0;
    loopBranches += event.instructionAddress == 15 * 8 ? 1 : 0;
  }

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 85);
  REQUIRE(result.programCounter == 19 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 19);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI01) == 0);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI02) == 8);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI03) == 11);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI05) == 3);
  REQUIRE(loopEntries == 3);
  REQUIRE(loopBranches == 3);
}
