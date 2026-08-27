#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_program_runner.hpp"
#include "vpu_register_ids.hpp"

TEST_CASE("VU1 XGKICK integration program")
{
  VPU vpu(VPUType::VU1);
  std::vector<std::uint8_t> memory;
  vpu_integration::appendQword(&memory, 0x401, 2, 0, 0);
  vpu_integration::appendQword(
    &memory, 0xdead0001, 0xdead0002, 0xdead0003, 0xdead0004);
  vpu_integration::appendQword(
    &memory, 0xdead0001, 0xdead0002, 0xdead0003, 0xdead0004);
  vpu.writeDataMemory(0, memory);

  VPUProgramRunConfig config;
  config.microProgram = vpu_integration::readBinary("xgkick.bin");
  config.cycleBudget = 100;
  config.outputAddress = 32;
  config.outputSize = 16;
  config.captureTrace = true;

  const VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(&expected, 7, 0, 0, 0);

  std::uint32_t xgkickWritebackCycle = 0;
  std::uint16_t xgkickInstructionAddress = 0;
  for (const VPUTraceEvent &event : result.traceEvents)
  {
    if (event.type == VPUTraceEventType::PipelineWriteback &&
        event.opCode == VPU_XGKICK)
    {
      xgkickWritebackCycle = event.cycle;
      xgkickInstructionAddress = event.instructionAddress;
    }
  }

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 20);
  REQUIRE(result.programCounter == 8 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 8);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI02) == 3);
  REQUIRE(xgkickInstructionAddress == 16);
  REQUIRE(xgkickWritebackCycle == 11);
}
