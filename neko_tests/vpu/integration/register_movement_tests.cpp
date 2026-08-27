#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_program_runner.hpp"
#include "vpu_register_ids.hpp"

TEST_CASE("VU0 register movement integration program")
{
  VPU vpu;
  std::vector<std::uint8_t> memory;
  vpu_integration::appendQword(&memory, 1, 2, 0, 0);
  vpu_integration::appendQword(
    &memory,
    0x11112222,
    0x33334444,
    0x5555ffff,
    0x77778888);
  vpu_integration::appendQword(
    &memory,
    0xdead0001,
    0xdead0002,
    0xdead0003,
    0xdead0004);
  vpu_integration::appendQword(
    &memory,
    0xdead0001,
    0xdead0002,
    0xdead0003,
    0xdead0004);
  vpu.writeDataMemory(0, memory);

  VPUProgramRunConfig config;
  config.microProgram =
    vpu_integration::readBinary("register_movement.bin");
  config.cycleBudget = 200;
  config.outputAddress = 2 * 16;
  config.outputSize = 2 * 16;

  const VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(
    &expected,
    0x11112222,
    0x33334444,
    0x00000000,
    0xffffffff);
  vpu_integration::appendQword(
    &expected,
    0x33334444,
    0x5555ffff,
    0x77778888,
    0x11112222);

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 37);
  REQUIRE(result.programCounter == 11 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 11);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI03) == 0xffff);
}
