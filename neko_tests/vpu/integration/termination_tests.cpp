#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_program_runner.hpp"
#include "vpu_register_ids.hpp"

namespace
{
  std::vector<std::uint16_t> issuedAddresses(
    const std::vector<VPUTraceEvent> &events)
  {
    std::vector<std::uint16_t> addresses;
    for (const VPUTraceEvent &event : events)
    {
      if (event.type == VPUTraceEventType::InstructionIssued)
      {
        addresses.push_back(event.instructionAddress);
      }
    }
    return addresses;
  }
}

TEST_CASE("VU0 termination integration program")
{
  SECTION("E on a branch terminates at the target without executing it")
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
    config.microProgram = vpu_integration::readBinary("termination.bin");
    config.cycleBudget = 100;
    config.outputAddress = 16;
    config.outputSize = 16;
    config.captureTrace = true;

    VPUProgramRunResult result = runVPUProgram(&vpu, config);

    std::vector<std::uint8_t> expected;
    vpu_integration::appendQword(&expected, 1, 1, 1, 1);
    REQUIRE(result.state == VPU_STATE_READY);
    REQUIRE(result.elapsedCycles == 21);
    REQUIRE(result.programCounter == 8 * 8);
    REQUIRE(result.hasTerminationPosition);
    REQUIRE(result.terminationPosition == 8);
    REQUIRE(result.outputMemory == expected);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 3);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI05) == 0);
    REQUIRE(issuedAddresses(result.traceEvents) ==
      std::vector<std::uint16_t>({0, 8, 16, 24, 32, 40, 48}));
  }

  SECTION("E on a branch delay slot executes the branch target as its delay")
  {
    VPU vpu;
    std::vector<std::uint8_t> initialMemory;
    vpu_integration::appendQword(&initialMemory, 0, 0, 0, 0);
    vpu_integration::appendQword(&initialMemory, 1, 2, 2, 0);
    vpu_integration::appendQword(
      &initialMemory,
      0xdead0001,
      0xdead0002,
      0xdead0003,
      0xdead0004);
    vpu.writeDataMemory(0, initialMemory);

    VPUProgramRunConfig config;
    config.microProgram = vpu_integration::readBinary("termination.bin");
    config.startAddress = 10 * 8;
    config.cycleBudget = 100;
    config.outputAddress = 2 * 16;
    config.outputSize = 16;
    config.captureTrace = true;

    VPUProgramRunResult result = runVPUProgram(&vpu, config);

    std::vector<std::uint8_t> expected;
    vpu_integration::appendQword(&expected, 2, 2, 2, 2);
    REQUIRE(result.state == VPU_STATE_READY);
    REQUIRE(result.elapsedCycles == 23);
    REQUIRE(result.programCounter == 20 * 8);
    REQUIRE(result.hasTerminationPosition);
    REQUIRE(result.terminationPosition == 20);
    REQUIRE(result.outputMemory == expected);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 3);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI05) == 0);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI06) == 0);
    REQUIRE(issuedAddresses(result.traceEvents) ==
      std::vector<std::uint16_t>({80, 88, 96, 104, 112, 120, 128, 152}));
  }
}
