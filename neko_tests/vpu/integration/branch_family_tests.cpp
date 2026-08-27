#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_program_runner.hpp"
#include "vpu_register_ids.hpp"

TEST_CASE("VU0 branch family integration program")
{
  VPU vpu;
  std::vector<std::uint8_t> memory;
  vpu_integration::appendQword(&memory, 1, 0, 0x0000ffff, 4);
  for (int index = 0; index < 4; index++)
  {
    vpu_integration::appendQword(
      &memory, 0xdead0001, 0xdead0002, 0xdead0003, 0xdead0004);
  }
  vpu.writeDataMemory(0, memory);

  VPUProgramRunConfig config;
  config.microProgram = vpu_integration::readBinary("branch_family.bin");
  config.cycleBudget = 100;
  config.outputAddress = 64;
  config.outputSize = 16;
  config.captureTrace = true;

  const VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(&expected, 8, 72, 0xffffffff, 1);

  std::vector<std::uint16_t> branchIssues;
  for (const VPUTraceEvent &event : result.traceEvents)
  {
    if (event.type == VPUTraceEventType::InstructionIssued)
    {
      const std::uint32_t encoding = event.lowerInstruction &
        VPU_LOWER_TYPE7_MASK;
      if (encoding == VPU_B_ENCODING ||
          encoding == VPU_BAL_ENCODING ||
          encoding == VPU_IBEQ_ENCODING ||
          encoding == VPU_IBNE_ENCODING ||
          encoding == VPU_IBGEZ_ENCODING ||
          encoding == VPU_IBGTZ_ENCODING ||
          encoding == VPU_IBLEZ_ENCODING ||
          encoding == VPU_IBLTZ_ENCODING)
      {
        branchIssues.push_back(event.instructionAddress);
      }
    }
  }

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 35);
  REQUIRE(result.programCounter == 35 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 35);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 8);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI10) == 5);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI15) == 72);
  REQUIRE(branchIssues ==
    std::vector<std::uint16_t>({32, 56, 80, 104, 128, 152, 176, 200}));
}
