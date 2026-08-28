#include <cstddef>
#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_program_runner.hpp"

namespace
{
  constexpr std::uint32_t HALF_BITS = 0x3f000000;
  constexpr std::uint32_t ONE_BITS = 0x3f800000;
  constexpr std::uint32_t ESIN_HALF_BITS = 0x3ef57742;
  constexpr std::uint32_t EEXP_ONE_BITS = 0x3ebc5abf;

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

TEST_CASE("VU1 EFU polynomial integration program")
{
  VPU vpu(VPUType::VU1);

  std::vector<std::uint8_t> memory;
  vpu_integration::appendQword(
    &memory,
    HALF_BITS,
    ONE_BITS,
    0,
    0);
  vpu_integration::appendQword(&memory, 2, 0, 0, 0);
  vpu.writeDataMemory(0, memory);

  VPUProgramRunConfig config;
  config.microProgram =
    vpu_integration::readBinary("efu_polynomials.bin");
  config.cycleBudget = 150;
  config.outputAddress = 32;
  config.outputSize = 16;

  const VPUProgramRunResult result = runVPUProgram(&vpu, config);

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 93);
  REQUIRE(result.programCounter == 11 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 11);
  REQUIRE(outputWord(result, 0) == ESIN_HALF_BITS);
  REQUIRE(outputWord(result, 4) == EEXP_ONE_BITS);
  REQUIRE(outputWord(result, 8) == 0);
  REQUIRE(outputWord(result, 12) == 0);
  REQUIRE(vpu.pRegisterBits() == EEXP_ONE_BITS);
}
