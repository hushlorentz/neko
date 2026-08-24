#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_program_runner.hpp"
#include "vpu_register_ids.hpp"

TEST_CASE("VU0 branch paths integration program")
{
  VPU vpu;
  std::vector<std::uint8_t> initialMemory;
  vpu_integration::appendQword(&initialMemory, 1, 2, 1, 0);
  vpu_integration::appendQword(
    &initialMemory,
    0xdead0001,
    0xdead0002,
    0xdead0003,
    0xdead0004);
  vpu.writeDataMemory(0, initialMemory);

  VPUProgramRunConfig config;
  config.microProgram = vpu_integration::readBinary("branch_paths.bin");
  config.cycleBudget = 200;
  config.outputAddress = 16;
  config.outputSize = 16;

  VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(&expected, 3, 1, 2, 1);

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 36);
  REQUIRE(result.programCounter == 27 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 27);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI03) == 2);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI06) == 0);
}
