#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_program_runner.hpp"
#include "vpu_register_ids.hpp"

TEST_CASE("VU0 indirect calls integration program")
{
  constexpr std::uint32_t subroutineAddress = 13 * 8;
  constexpr std::uint32_t outputAddress = 1;
  constexpr std::uint32_t finishAddress = 17 * 8;
  constexpr std::uint32_t returnAddress = 9 * 8;

  VPU vpu;
  std::vector<std::uint8_t> initialMemory;
  vpu_integration::appendQword(
    &initialMemory,
    subroutineAddress,
    outputAddress,
    finishAddress,
    1);
  vpu_integration::appendQword(
    &initialMemory,
    0xdead0001,
    0xdead0002,
    0xdead0003,
    0xdead0004);
  vpu.writeDataMemory(0, initialMemory);

  VPUProgramRunConfig config;
  config.microProgram = vpu_integration::readBinary("indirect_calls.bin");
  config.cycleBudget = 200;
  config.outputAddress = 16;
  config.outputSize = 16;

  VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(&expected, 3, 1, 1, returnAddress);

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 32);
  REQUIRE(result.programCounter == 24 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 24);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI02) == 2);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI15) == returnAddress);
}
