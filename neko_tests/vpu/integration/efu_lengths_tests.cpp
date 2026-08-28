#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_program_runner.hpp"

TEST_CASE("VU1 EFU length and reciprocal integration program")
{
  VPU vpu(VPUType::VU1);

  std::vector<std::uint8_t> memory;
  vpu_integration::appendQword(
    &memory,
    0x40000000,
    0,
    0,
    0xc1000000);
  vpu_integration::appendQword(&memory, 2, 0, 0, 0);
  vpu.writeDataMemory(0, memory);

  VPUProgramRunConfig config;
  config.microProgram =
    vpu_integration::readBinary("efu_lengths.bin");
  config.cycleBudget = 200;
  config.outputAddress = 32;
  config.outputSize = 16;

  const VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(
    &expected,
    0x40000000,
    0xbe000000,
    0x3e800000,
    0x3f000000);

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 96);
  REQUIRE(result.programCounter == 17 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 17);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(vpu.pRegisterBits() == 0x3f000000);
}
