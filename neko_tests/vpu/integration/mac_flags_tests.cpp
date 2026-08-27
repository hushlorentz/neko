#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_program_runner.hpp"

TEST_CASE("VU0 MAC flag integration program")
{
  VPU vpu;
  std::vector<std::uint8_t> memory;
  vpu_integration::appendQword(&memory, 1, 2, 3, 0);
  vpu_integration::appendQword(
    &memory,
    0xbf800000,
    0x00000000,
    0x40000000,
    0x40400000);
  vpu_integration::appendQword(&memory, 0, 0, 0, 0);
  vpu_integration::appendQword(
    &memory,
    0xdead0001,
    0xdead0002,
    0xdead0003,
    0xdead0004);
  vpu.writeDataMemory(0, memory);

  VPUProgramRunConfig config;
  config.microProgram = vpu_integration::readBinary("mac_flags.bin");
  config.cycleBudget = 200;
  config.outputAddress = 3 * 16;
  config.outputSize = 16;

  const VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(&expected, 0x84, 1, 0x87, 0);

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 35);
  REQUIRE(result.programCounter == 21 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 21);
  REQUIRE(result.outputMemory == expected);
}
