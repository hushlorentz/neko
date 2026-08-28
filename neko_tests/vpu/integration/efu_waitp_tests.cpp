#include <algorithm>
#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_program_runner.hpp"

TEST_CASE("VU1 EFU and WAITP integration program")
{
  VPU vpu(VPUType::VU1);
  vpu.loadPRegister(-1);

  std::vector<std::uint8_t> memory;
  vpu_integration::appendQword(
    &memory,
    0x3f800000,
    0x40000000,
    0x40400000,
    0x40800000);
  vpu_integration::appendQword(&memory, 2, 0, 0, 0);
  vpu.writeDataMemory(0, memory);

  VPUProgramRunConfig config;
  config.microProgram =
    vpu_integration::readBinary("efu_waitp.bin");
  config.cycleBudget = 100;
  config.outputAddress = 32;
  config.outputSize = 16;
  config.captureTrace = true;

  const VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(
    &expected,
    0xbf800000,
    0x41200000,
    0,
    0);

  const auto efuWritebacks = std::count_if(
    result.traceEvents.begin(),
    result.traceEvents.end(),
    [](const VPUTraceEvent &event)
    {
      return
        event.type == VPUTraceEventType::PipelineWriteback &&
        event.opCode == VPU_ESUM;
    });

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 30);
  REQUIRE(result.programCounter == 9 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 9);
  REQUIRE(vpu.pRegisterBits() == 0x41200000);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(efuWritebacks == 1);
}
