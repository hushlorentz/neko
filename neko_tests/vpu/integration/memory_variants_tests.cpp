#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_program_runner.hpp"
#include "vpu_register_ids.hpp"

TEST_CASE("VU0 memory variants integration program")
{
  VPU vpu;
  std::vector<std::uint8_t> memory;
  vpu_integration::appendQword(&memory, 3, 1, 0, 0);
  vpu_integration::appendQword(
    &memory, 0x11112222, 0x33334444, 0x55556666, 0x77778888);
  vpu_integration::appendQword(
    &memory, 0x9999aaaa, 0xbbbbcccc, 0xddddeeee, 0xffff0001);
  for (int index = 0; index < 4; index++)
  {
    vpu_integration::appendQword(
      &memory, 0xdead0001, 0xdead0002, 0xdead0003, 0xdead0004);
  }
  vpu.writeDataMemory(0, memory);

  VPUProgramRunConfig config;
  config.microProgram = vpu_integration::readBinary("memory_variants.bin");
  config.cycleBudget = 100;
  config.outputAddress = 16;
  config.outputSize = 96;
  config.captureTrace = true;

  const VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(
    &expected, 0x11112222, 0x33334444, 0x00000000, 0x77778888);
  vpu_integration::appendQword(
    &expected, 0x9999aaaa, 0x00006666, 0xddddeeee, 0xffff0001);
  vpu_integration::appendQword(
    &expected, 0xdead0001, 0xdead0002, 0xdead0003, 0xdead0004);
  vpu_integration::appendQword(
    &expected, 0xdead0001, 0xdead0002, 0xdead0003, 0xdead0004);
  vpu_integration::appendQword(
    &expected, 0x9999aaaa, 0x00000000, 0xdead0003, 0xdead0004);
  vpu_integration::appendQword(
    &expected, 0x00006666, 0xdead0002, 0xdead0003, 0x00006666);

  std::vector<std::uint16_t> variantWritebacks;
  for (const VPUTraceEvent &event : result.traceEvents)
  {
    if (event.type == VPUTraceEventType::PipelineWriteback &&
        (event.opCode == VPU_ILWR ||
         event.opCode == VPU_ISW ||
         event.opCode == VPU_ISWR ||
         event.opCode == VPU_LQD ||
         event.opCode == VPU_LQI ||
         event.opCode == VPU_SQ ||
         event.opCode == VPU_SQD))
    {
      variantWritebacks.push_back(event.instructionAddress);
    }
  }

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 24);
  REQUIRE(result.programCounter == 10 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 10);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI01) == 2);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI02) == 1);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI03) == 0x6666);
  REQUIRE(variantWritebacks ==
    std::vector<std::uint16_t>({16, 24, 32, 40, 48, 56, 64}));
}
