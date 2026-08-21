#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "catch.hpp"
#include "vpu_program_runner.hpp"
#include "vpu_register_ids.hpp"

namespace
{
  std::vector<std::uint8_t> readBinary(const std::string &path)
  {
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
      throw std::runtime_error("Could not open integration fixture: " + path);
    }

    return std::vector<std::uint8_t>(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
  }

  void appendWord(
    std::vector<std::uint8_t> *bytes,
    std::uint32_t value)
  {
    bytes->push_back(value & 0xff);
    bytes->push_back((value >> 8) & 0xff);
    bytes->push_back((value >> 16) & 0xff);
    bytes->push_back((value >> 24) & 0xff);
  }

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
        appendWord(&expected, value);
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
  appendWord(&parameters, count);
  appendWord(&parameters, firstValue);
  appendWord(&parameters, increment);
  appendWord(&parameters, 0);
  vpu.writeDataMemory(0, parameters);

  VPUProgramRunConfig config;
  config.microProgram = readBinary(
    std::string(NEKO_TEST_FIXTURE_DIR) + "/integer_fill.bin");
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
