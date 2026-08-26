#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "floating_point_ops.hpp"
#include "vpu.hpp"
#include "vpu_flags.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_program_runner.hpp"

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

  const VPUTraceEvent &writebackAt(
    const std::vector<VPUTraceEvent> &events,
    std::uint16_t instructionAddress,
    std::uint16_t opCode)
  {
    for (const VPUTraceEvent &event : events)
    {
      if (event.type == VPUTraceEventType::PipelineWriteback &&
          event.instructionAddress == instructionAddress &&
          event.opCode == opCode)
      {
        return event;
      }
    }

    FAIL("Expected floating-point pipeline writeback was not traced");
    return events.front();
  }
}

TEST_CASE("VU0 decoded floating-point arithmetic truncates raw results")
{
  VPU vpu;
  std::vector<std::uint8_t> memory;
  vpu_integration::appendQword(&memory, 1, 2, 3, 0);
  vpu_integration::appendQword(
    &memory, 0x3f800000, 0x3f800000, 0x3f800001, 0xbf800001);
  vpu_integration::appendQword(
    &memory, 0x33c00000, 0x33000000, 0x3fc00000, 0x3fc00000);
  for (std::uint8_t index = 0; index < 3; index++)
  {
    vpu_integration::appendQword(
      &memory, 0xdead0001, 0xdead0002, 0xdead0003, 0xdead0004);
  }
  vpu.writeDataMemory(0, memory);

  VPUProgramRunConfig config;
  config.microProgram =
    vpu_integration::readBinary("floating_point_truncation.bin");
  config.cycleBudget = 100;
  config.outputAddress = 3 * 16;
  config.outputSize = 3 * 16;
  config.captureTrace = true;

  const VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(
    &expected, 0x3f800000, 0x3f800000, 0x40200000, 0x3efffffc);
  vpu_integration::appendQword(
    &expected, 0x3f7ffffe, 0x3f7fffff, 0xbefffffc, 0xc0200000);
  vpu_integration::appendQword(
    &expected, 0x33c00000, 0x33000000, 0x3fc00001, 0xbfc00001);

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 35);
  REQUIRE(result.programCounter == 13 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 13);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(issuedAddresses(result.traceEvents) ==
    std::vector<std::uint16_t>({
      0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96
    }));
  REQUIRE(vpu.hasMACFlag(VPU_FLAG_SW));
  REQUIRE_FALSE(vpu.hasMACFlag(VPU_FLAG_UW));
  REQUIRE_FALSE(vpu.hasMACFlag(VPU_FLAG_OW));
  REQUIRE(vpu.hasStatusFlag(VPU_FLAG_S));
  REQUIRE(vpu.hasStatusFlag(VPU_FLAG_SS));
  REQUIRE_FALSE(vpu.hasStatusFlag(VPU_FLAG_U));
  REQUIRE_FALSE(vpu.hasStatusFlag(VPU_FLAG_O));
}

TEST_CASE("VU0 decoded floating-point arithmetic reports VU exceptions")
{
  VPU vpu;
  std::vector<std::uint8_t> memory;
  vpu_integration::appendQword(&memory, 1, 2, 3, 0);
  vpu_integration::appendQword(
    &memory, 0x00000001, 0x807fffff, 0x7fffffff, 0x00800000);
  vpu_integration::appendQword(
    &memory, 0x40000000, 0x40000000, 0x40000000, 0x3f000000);
  vpu_integration::appendQword(
    &memory, 0xdead0001, 0xdead0002, 0xdead0003, 0xdead0004);
  vpu.writeDataMemory(0, memory);

  VPUProgramRunConfig config;
  config.microProgram =
    vpu_integration::readBinary("floating_point_exceptions.bin");
  config.cycleBudget = 100;
  config.outputAddress = 3 * 16;
  config.outputSize = 16;
  config.captureTrace = true;

  const VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(
    &expected, 0x00000000, 0x80000000, 0x7fffffff, 0x00000000);

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 23);
  REQUIRE(result.programCounter == 9 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 9);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(issuedAddresses(result.traceEvents) ==
    std::vector<std::uint16_t>({0, 8, 16, 24, 32, 40, 48, 56, 64}));

  REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZX));
  REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZY));
  REQUIRE_FALSE(vpu.hasMACFlag(VPU_FLAG_ZZ));
  REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZW));
  REQUIRE(vpu.hasMACFlag(VPU_FLAG_SY));
  REQUIRE(vpu.hasMACFlag(VPU_FLAG_OZ));
  REQUIRE(vpu.hasMACFlag(VPU_FLAG_UW));

  REQUIRE(vpu.hasStatusFlag(VPU_FLAG_Z));
  REQUIRE(vpu.hasStatusFlag(VPU_FLAG_S));
  REQUIRE(vpu.hasStatusFlag(VPU_FLAG_O));
  REQUIRE(vpu.hasStatusFlag(VPU_FLAG_U));
  REQUIRE(vpu.hasStatusFlag(VPU_FLAG_ZS));
  REQUIRE(vpu.hasStatusFlag(VPU_FLAG_SS));
  REQUIRE(vpu.hasStatusFlag(VPU_FLAG_OS));
  REQUIRE(vpu.hasStatusFlag(VPU_FLAG_US));
}

TEST_CASE("VU0 decoded compound FMAC preserves intermediate truncation")
{
  VPU vpu;
  std::vector<std::uint8_t> memory;
  vpu_integration::appendQword(&memory, 1, 2, 3, 0);
  vpu_integration::appendQword(
    &memory, 0x3f800001, 0xbf800001, 0x3fffffff, 0x00800000);
  vpu_integration::appendQword(
    &memory, 0x3fc00000, 0x3fc00000, 0x3ffffffe, 0x3f000000);
  for (std::uint8_t index = 0; index < 3; index++)
  {
    vpu_integration::appendQword(
      &memory, 0xdead0001, 0xdead0002, 0xdead0003, 0xdead0004);
  }
  vpu.writeDataMemory(0, memory);

  VPUProgramRunConfig config;
  config.microProgram =
    vpu_integration::readBinary("floating_point_compound.bin");
  config.cycleBudget = 100;
  config.outputAddress = 3 * 16;
  config.outputSize = 3 * 16;
  config.captureTrace = true;

  const VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(
    &expected, 0x3fc00001, 0xbfc00001, 0x407ffffd, 0x00000000);
  vpu_integration::appendQword(
    &expected, 0x40400001, 0xc0400001, 0x40fffffd, 0x00000000);
  vpu_integration::appendQword(
    &expected, 0x3fc00001, 0xbfc00001, 0x407ffffd, 0x00000000);

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 35);
  REQUIRE(result.programCounter == 15 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 15);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(issuedAddresses(result.traceEvents) ==
    std::vector<std::uint16_t>({
      0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112
    }));

  REQUIRE(vpu.accumulator.x.bits() == 0x40400001);
  REQUIRE(vpu.accumulator.y.bits() == 0xc0400001);
  REQUIRE(vpu.accumulator.z.bits() == 0x40fffffd);
  REQUIRE(vpu.accumulator.w.bits() == 0x00000000);
  REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZW));
  REQUIRE(vpu.hasMACFlag(VPU_FLAG_SY));
  REQUIRE(vpu.hasStatusFlag(VPU_FLAG_Z));
  REQUIRE(vpu.hasStatusFlag(VPU_FLAG_S));
  REQUIRE(vpu.hasStatusFlag(VPU_FLAG_ZS));
  REQUIRE(vpu.hasStatusFlag(VPU_FLAG_SS));
  REQUIRE(vpu.hasStatusFlag(VPU_FLAG_US));
  REQUIRE_FALSE(vpu.hasStatusFlag(VPU_FLAG_U));
  REQUIRE_FALSE(vpu.hasStatusFlag(VPU_FLAG_O));

  const VPUTraceEvent &madd =
    writebackAt(result.traceEvents, 56, VPU_MADD);
  REQUIRE(madd.arithmetic.present);
  REQUIRE(madd.arithmetic.ignoredResultFields == FP_REGISTER_NO_FIELDS);
  REQUIRE(madd.arithmetic.lanes[0].multiplyBits == 0x3fc00001);
  REQUIRE(madd.arithmetic.lanes[0].multiplyFlags == 0);
  REQUIRE(madd.arithmetic.lanes[0].accumulatorBits == 0x3fc00001);
  REQUIRE(madd.arithmetic.lanes[0].resultBits == 0x40400001);
  REQUIRE(madd.arithmetic.lanes[0].resultFlags == 0);
  REQUIRE(madd.arithmetic.lanes[1].multiplyBits == 0xbfc00001);
  REQUIRE(madd.arithmetic.lanes[1].multiplyFlags == 0);
  REQUIRE(madd.arithmetic.lanes[1].accumulatorBits == 0xbfc00001);
  REQUIRE(madd.arithmetic.lanes[1].resultBits == 0xc0400001);
  REQUIRE(madd.arithmetic.lanes[1].resultFlags == 0);
  REQUIRE(madd.arithmetic.lanes[2].multiplyBits == 0x407ffffd);
  REQUIRE(madd.arithmetic.lanes[2].multiplyFlags == 0);
  REQUIRE(madd.arithmetic.lanes[2].accumulatorBits == 0x407ffffd);
  REQUIRE(madd.arithmetic.lanes[2].resultBits == 0x40fffffd);
  REQUIRE(madd.arithmetic.lanes[2].resultFlags == 0);
  REQUIRE(madd.arithmetic.lanes[3].multiplyBits == 0x00000000);
  REQUIRE(madd.arithmetic.lanes[3].multiplyFlags == FP_FLAG_UNDERFLOW);
  REQUIRE(madd.arithmetic.lanes[3].accumulatorBits == 0x00000000);
  REQUIRE(madd.arithmetic.lanes[3].resultBits == 0x00000000);
  REQUIRE(madd.arithmetic.lanes[3].resultFlags == 0);

  const VPUTraceEvent &msub =
    writebackAt(result.traceEvents, 72, VPU_MSUB);
  REQUIRE(msub.arithmetic.present);
  REQUIRE(msub.arithmetic.ignoredResultFields == FP_REGISTER_NO_FIELDS);
  REQUIRE(msub.arithmetic.lanes[0].multiplyBits == 0x3fc00001);
  REQUIRE(msub.arithmetic.lanes[0].multiplyFlags == 0);
  REQUIRE(msub.arithmetic.lanes[0].accumulatorBits == 0x40400001);
  REQUIRE(msub.arithmetic.lanes[0].resultBits == 0x3fc00001);
  REQUIRE(msub.arithmetic.lanes[0].resultFlags == 0);
  REQUIRE(msub.arithmetic.lanes[1].multiplyBits == 0xbfc00001);
  REQUIRE(msub.arithmetic.lanes[1].multiplyFlags == 0);
  REQUIRE(msub.arithmetic.lanes[1].accumulatorBits == 0xc0400001);
  REQUIRE(msub.arithmetic.lanes[1].resultBits == 0xbfc00001);
  REQUIRE(msub.arithmetic.lanes[1].resultFlags == 0);
  REQUIRE(msub.arithmetic.lanes[2].multiplyBits == 0x407ffffd);
  REQUIRE(msub.arithmetic.lanes[2].multiplyFlags == 0);
  REQUIRE(msub.arithmetic.lanes[2].accumulatorBits == 0x40fffffd);
  REQUIRE(msub.arithmetic.lanes[2].resultBits == 0x407ffffd);
  REQUIRE(msub.arithmetic.lanes[2].resultFlags == 0);
  REQUIRE(msub.arithmetic.lanes[3].multiplyBits == 0x00000000);
  REQUIRE(msub.arithmetic.lanes[3].multiplyFlags == FP_FLAG_UNDERFLOW);
  REQUIRE(msub.arithmetic.lanes[3].accumulatorBits == 0x00000000);
  REQUIRE(msub.arithmetic.lanes[3].resultBits == 0x00000000);
  REQUIRE(msub.arithmetic.lanes[3].resultFlags == 0);
}
