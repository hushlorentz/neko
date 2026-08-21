#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_program_runner.hpp"
#include "vpu_register_ids.hpp"

namespace
{
  std::vector<std::uint8_t> expectedFill(
    std::uint16_t count,
    std::uint16_t firstValue,
    std::uint16_t increment)
  {
    std::vector<std::uint8_t> expected;
    for (std::uint16_t index = 0; index < count; index++)
    {
      std::uint32_t value =
        static_cast<std::uint16_t>(firstValue + index * increment);
      for (std::uint8_t lane = 0; lane < 4; lane++)
      {
        vpu_integration::appendWord(&expected, value);
      }
    }
    return expected;
  }
}

TEST_CASE("VU0 integer fill integration program")
{
  constexpr std::uint16_t count = 3;
  constexpr std::uint16_t firstValue = 0x10;
  constexpr std::uint16_t increment = 3;

  VPU vpu;
  std::vector<std::uint8_t> parameters;
  vpu_integration::appendWord(&parameters, count);
  vpu_integration::appendWord(&parameters, firstValue);
  vpu_integration::appendWord(&parameters, increment);
  vpu_integration::appendWord(&parameters, 0);
  vpu.writeDataMemory(0, parameters);

  VPUProgramRunConfig config;
  config.microProgram = vpu_integration::readBinary("integer_fill.bin");
  config.cycleBudget = 200;
  config.outputSize = count * 16;

  VPUProgramRunResult result = runVPUProgram(&vpu, config);

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 52);
  REQUIRE(result.programCounter == 96);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 12);
  REQUIRE(result.outputMemory ==
    expectedFill(count, firstValue, increment));
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == count);
}
