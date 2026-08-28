#include <cstddef>
#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_program_runner.hpp"

namespace
{
  constexpr std::uint32_t ONE_BITS = 0x3f800000;
  constexpr std::uint32_t HALF_BITS = 0x3f000000;
  constexpr std::uint32_t QUARTER_BITS = 0x3e800000;
  constexpr std::uint32_t ATAN_ONE_BITS = 0x3f490fdb;
  constexpr std::uint32_t ATAN_HALF_BITS = 0x3eed633c;
  constexpr std::uint32_t ATAN_QUARTER_BITS = 0x3e7adbbf;

  std::uint32_t outputWord(
    const VPUProgramRunResult &result,
    std::size_t offset)
  {
    return
      result.outputMemory[offset] |
      (static_cast<std::uint32_t>(result.outputMemory[offset + 1]) << 8) |
      (static_cast<std::uint32_t>(result.outputMemory[offset + 2]) << 16) |
      (static_cast<std::uint32_t>(result.outputMemory[offset + 3]) << 24);
  }
}

TEST_CASE("VU1 EFU arctangent integration program")
{
  VPU vpu(VPUType::VU1);

  std::vector<std::uint8_t> memory;
  vpu_integration::appendQword(
    &memory,
    ONE_BITS,
    HALF_BITS,
    QUARTER_BITS,
    ONE_BITS);
  vpu_integration::appendQword(&memory, 2, 0, 0, 0);
  vpu.writeDataMemory(0, memory);

  VPUProgramRunConfig config;
  config.microProgram =
    vpu_integration::readBinary("efu_arctangent.bin");
  config.cycleBudget = 250;
  config.outputAddress = 32;
  config.outputSize = 16;

  const VPUProgramRunResult result = runVPUProgram(&vpu, config);

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 184);
  REQUIRE(result.programCounter == 14 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 14);
  REQUIRE(outputWord(result, 0) == ATAN_ONE_BITS);
  REQUIRE(outputWord(result, 4) == ATAN_HALF_BITS);
  REQUIRE(outputWord(result, 8) == ATAN_QUARTER_BITS);
  REQUIRE(outputWord(result, 12) == 0);
  REQUIRE(vpu.pRegisterBits() == ATAN_QUARTER_BITS);
}
