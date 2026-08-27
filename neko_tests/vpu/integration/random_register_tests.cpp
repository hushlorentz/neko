#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_program_runner.hpp"

TEST_CASE("VU0 random register integration program")
{
  VPU vpu;
  std::vector<std::uint8_t> memory;
  vpu_integration::appendQword(&memory, 1, 2, 0, 0);
  vpu_integration::appendQword(
    &memory,
    0x00400010,
    0x0000000f,
    0,
    0);
  vpu_integration::appendQword(
    &memory,
    0xdead0001,
    0xdead0002,
    0xdead0003,
    0xdead0004);
  vpu.writeDataMemory(0, memory);

  VPUProgramRunConfig config;
  config.microProgram =
    vpu_integration::readBinary("random_register.bin");
  config.cycleBudget = 200;
  config.outputAddress = 2 * 16;
  config.outputSize = 16;

  const VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(
    &expected,
    0x3fc00010,
    0x3fc00010,
    0x3f800020,
    0x3f80002f);

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 27);
  REQUIRE(result.programCounter == 11 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 11);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(vpu.rRegisterBits() == 0x3f80002f);
}
