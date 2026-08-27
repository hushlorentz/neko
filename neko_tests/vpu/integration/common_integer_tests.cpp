#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_program_runner.hpp"
#include "vpu_register_ids.hpp"

TEST_CASE("VU0 common integer integration program")
{
  VPU vpu;
  std::vector<std::uint8_t> memory;
  vpu_integration::appendQword(&memory, 0x0000fff8, 0x00000ff0, 1, 0);
  vpu_integration::appendQword(
    &memory, 0xdead0001, 0xdead0002, 0xdead0003, 0xdead0004);
  vpu_integration::appendQword(
    &memory, 0xdead0001, 0xdead0002, 0xdead0003, 0xdead0004);
  vpu.writeDataMemory(0, memory);

  VPUProgramRunConfig config;
  config.microProgram = vpu_integration::readBinary("common_integer.bin");
  config.cycleBudget = 100;
  config.outputAddress = 16;
  config.outputSize = 32;
  config.captureTrace = true;

  const VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(
    &expected, 0xffffffe8, 0x00007fe7, 0x00000fe0, 0xfffffff8);
  vpu_integration::appendQword(
    &expected, 0xfffff008, 0x00000000, 0x00000000, 0x00000000);

  std::vector<std::uint16_t> ialuWritebacks;
  for (const VPUTraceEvent &event : result.traceEvents)
  {
    if (event.type == VPUTraceEventType::PipelineWriteback &&
        (event.opCode == VPU_IADDI ||
         event.opCode == VPU_IADDIU ||
         event.opCode == VPU_IAND ||
         event.opCode == VPU_IOR ||
         event.opCode == VPU_ISUB))
    {
      ialuWritebacks.push_back(event.instructionAddress);
    }
  }

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 32);
  REQUIRE(result.programCounter == 17 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 17);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI08) == 3);
  REQUIRE(ialuWritebacks ==
    std::vector<std::uint16_t>({24, 32, 40, 48, 56}));
}
