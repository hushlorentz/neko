#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_program_runner.hpp"
#include "vpu_register_ids.hpp"

TEST_CASE("VU0 lane mask integration program")
{
  constexpr std::uint32_t sourceX = 0x11111111;
  constexpr std::uint32_t sourceW = 0x44444444;
  constexpr std::uint32_t integerSentinel = 0x7f;

  VPU vpu;
  std::vector<std::uint8_t> initialMemory;
  vpu_integration::appendQword(
    &initialMemory, 1, 2, integerSentinel, 0);
  vpu_integration::appendQword(
    &initialMemory,
    sourceX,
    0x22222222,
    0x33333333,
    sourceW);
  vpu_integration::appendQword(
    &initialMemory, 0xa0, 0xa1, 0xa2, 0xa3);
  vpu_integration::appendQword(
    &initialMemory, 0xb0, 0xb1, 0xb2, 0xb3);
  vpu_integration::appendQword(
    &initialMemory, 0xc0, 0xc1, 0xc2, 0xc3);
  vpu_integration::appendQword(
    &initialMemory, 0xd0, 0xd1, 0xd2, 0xd3);
  vpu_integration::appendQword(
    &initialMemory, 0xe0, 0xe1, 0xe2, 0xe3);
  vpu.writeDataMemory(0, initialMemory);

  VPUProgramRunConfig config;
  config.microProgram = vpu_integration::readBinary("lane_masks.bin");
  config.cycleBudget = 200;
  config.outputAddress = 2 * 16;
  config.outputSize = 5 * 16;

  VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(
    &expected, sourceX, 0xa1, 0xa2, 0xa3);
  vpu_integration::appendQword(
    &expected, 0xb0, integerSentinel, 0xb2, 0xb3);
  vpu_integration::appendQword(
    &expected, 0xc0, 0xc1, integerSentinel, 0xc3);
  vpu_integration::appendQword(
    &expected, 0xd0, 0xd1, 0xd2, sourceW);
  vpu_integration::appendQword(
    &expected, sourceX, 0xe1, 0xe2, sourceW);

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 48);
  REQUIRE(result.programCounter == 17 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 17);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI02) == 7);
}
