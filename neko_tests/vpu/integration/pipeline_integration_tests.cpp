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

TEST_CASE("VU0 ACC and lane hazard integration program")
{
  VPU vpu;
  std::vector<std::uint8_t> memory;
  vpu_integration::appendQword(&memory, 1, 2, 3, 0);
  vpu_integration::appendQword(
    &memory, 0x3f800000, 0x40000000, 0x40400000, 0x40800000);
  vpu_integration::appendQword(
    &memory, 0x3f000000, 0x3f800000, 0x40000000, 0x40800000);
  for (std::uint8_t index = 0; index < 3; index++)
  {
    vpu_integration::appendQword(
      &memory, 0xdead0001, 0xdead0002, 0xdead0003, 0xdead0004);
  }
  vpu.writeDataMemory(0, memory);

  VPUProgramRunConfig config;
  config.microProgram =
    vpu_integration::readBinary("pipeline_acc_overlap.bin");
  config.cycleBudget = 100;
  config.outputAddress = 3 * 16;
  config.outputSize = 3 * 16;
  config.captureTrace = true;

  VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(
    &expected, 0x3f800000, 0x40800000, 0x41400000, 0x42000000);
  vpu_integration::appendQword(
    &expected, 0x00000000, 0x40000000, 0x00000000, 0x00000000);
  vpu_integration::appendQword(
    &expected, 0x40200000, 0x00000000, 0x00000000, 0x00000000);

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 37);
  REQUIRE(result.programCounter == 15 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 15);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(issuedAddresses(result.traceEvents) ==
    std::vector<std::uint16_t>({
      0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112
    }));
  REQUIRE(vpu.accumulator.x == 0.5);
  REQUIRE(vpu.accumulator.y == 2);
  REQUIRE(vpu.accumulator.z == 6);
  REQUIRE(vpu.accumulator.w == 16);
}

TEST_CASE("VU0 integer address and branch integration program")
{
  VPU vpu;
  std::vector<std::uint8_t> memory;
  vpu_integration::appendQword(&memory, 1, 1, 3, 0);
  vpu_integration::appendQword(&memory, 0, 0, 0, 0);
  vpu_integration::appendQword(&memory, 0x11, 0x22, 0x33, 0x44);
  vpu_integration::appendQword(
    &memory, 0xdead0001, 0xdead0002, 0xdead0003, 0xdead0004);
  vpu.writeDataMemory(0, memory);

  VPUProgramRunConfig config;
  config.microProgram =
    vpu_integration::readBinary("pipeline_integer_control.bin");
  config.cycleBudget = 100;
  config.outputAddress = 3 * 16;
  config.outputSize = 16;
  config.captureTrace = true;

  VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(&expected, 0x11, 0x22, 0x33, 4);

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 24);
  REQUIRE(result.programCounter == 13 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 13);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(issuedAddresses(result.traceEvents) ==
    std::vector<std::uint16_t>({
      0, 8, 16, 24, 32, 40, 48, 56, 72, 80, 88, 96
    }));
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 2);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI05) == 2);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI06) == 4);
}

TEST_CASE("VU0 LOI visibility integration program")
{
  VPU vpu;
  vpu.loadIRegister(1);
  vpu.loadFPRegister(VPU_REGISTER_VF02, 2, 3, 4, 5);
  std::vector<std::uint8_t> memory;
  vpu_integration::appendQword(&memory, 1, 0, 0, 0);
  vpu_integration::appendQword(
    &memory, 0xdead0001, 0xdead0002, 0xdead0003, 0xdead0004);
  vpu_integration::appendQword(
    &memory, 0xdead0001, 0xdead0002, 0xdead0003, 0xdead0004);
  vpu.writeDataMemory(0, memory);

  VPUProgramRunConfig config;
  config.microProgram =
    vpu_integration::readBinary("pipeline_loi_timing.bin");
  config.cycleBudget = 100;
  config.outputAddress = 16;
  config.outputSize = 32;
  config.captureTrace = true;

  VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(
    &expected, 0x40400000, 0, 0, 0);
  vpu_integration::appendQword(
    &expected, 0x41400000, 0, 0, 0);

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 19);
  REQUIRE(result.programCounter == 7 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 7);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(issuedAddresses(result.traceEvents) ==
    std::vector<std::uint16_t>({0, 8, 16, 24, 32, 40, 48}));
}

TEST_CASE("VU0 mixed pipeline termination drain integration program")
{
  VPU vpu;
  vpu.loadIRegister(1);
  std::vector<std::uint8_t> memory;
  vpu_integration::appendQword(&memory, 1, 2, 1, 0);
  vpu_integration::appendQword(
    &memory, 0x40000000, 0x40400000, 0x40800000, 0x40a00000);
  vpu_integration::appendQword(
    &memory, 0xdead0001, 0xdead0002, 0xdead0003, 0xdead0004);
  vpu.writeDataMemory(0, memory);

  VPUProgramRunConfig config;
  config.microProgram =
    vpu_integration::readBinary("pipeline_termination_drain.bin");
  config.cycleBudget = 100;
  config.outputAddress = 2 * 16;
  config.outputSize = 16;
  config.captureTrace = true;

  VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(
    &expected, 0x40000000, 0x40400000, 0x40800000, 0x40a00000);

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 19);
  REQUIRE(result.programCounter == 7 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 7);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(issuedAddresses(result.traceEvents) ==
    std::vector<std::uint16_t>({0, 8, 16, 24, 32, 40, 48}));
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI02) == 3);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 2);
  REQUIRE(vpu.accumulator.x == 4);
  REQUIRE(vpu.accumulator.y == 9);
  REQUIRE(vpu.accumulator.z == 16);
  REQUIRE(vpu.accumulator.w == 25);
  REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->x == 4);
  REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->y == 6);
  REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->z == 8);
  REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->w == 10);
  REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->x == 3);
  REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->y == 4);
  REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->z == 5);
  REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->w == 6);
}
