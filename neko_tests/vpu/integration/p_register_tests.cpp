#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_program_runner.hpp"

TEST_CASE("VU1 P register integration program")
{
  VPU vpu(VPUType::VU1);
  vpu.loadPRegister(-1.5);

  std::vector<std::uint8_t> memory;
  vpu_integration::appendQword(&memory, 1, 0, 0, 0);
  vpu_integration::appendQword(
    &memory,
    0xdead0001,
    0xdead0002,
    0xdead0003,
    0xdead0004);
  vpu.writeDataMemory(0, memory);

  VPUProgramRunConfig config;
  config.microProgram =
    vpu_integration::readBinary("p_register.bin");
  config.cycleBudget = 100;
  config.outputAddress = 16;
  config.outputSize = 16;

  const VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(
    &expected,
    0xbfc00000,
    0xbfc00000,
    0xbfc00000,
    0xbfc00000);

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 13);
  REQUIRE(result.programCounter == 5 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 5);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(vpu.pRegisterBits() == 0xbfc00000);
}
