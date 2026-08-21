#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_program_runner.hpp"
#include "vpu_register_ids.hpp"

TEST_CASE("VU0 vector math integration program")
{
  VPU vpu;
  std::vector<std::uint8_t> initialMemory;
  vpu_integration::appendQword(&initialMemory, 1, 2, 3, 0);
  vpu_integration::appendQword(
    &initialMemory,
    0x3f800000,
    0x40000000,
    0x40800000,
    0x41000000);
  vpu_integration::appendQword(
    &initialMemory,
    0x3f000000,
    0x3f800000,
    0x40000000,
    0x40800000);
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
  config.microProgram = vpu_integration::readBinary("vector_math.bin");
  config.cycleBudget = 200;
  config.outputAddress = 3 * 16;
  config.outputSize = 3 * 16;

  VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(
    &expected,
    0x3f000000,
    0x40000000,
    0x41000000,
    0x42000000);
  vpu_integration::appendQword(
    &expected,
    0x3f800000,
    0x40800000,
    0x41800000,
    0x42800000);
  vpu_integration::appendQword(
    &expected,
    0x3fc00000,
    0x40800000,
    0x41400000,
    0x42200000);

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 44);
  REQUIRE(result.programCounter == 15 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 15);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI03) == 6);
}
