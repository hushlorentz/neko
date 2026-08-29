#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu_integration_test_utils.hpp"
#include "vpu_program_runner.hpp"
#include "vpu_register_ids.hpp"
#include "vpu_xgkick_handler.hpp"

namespace
{
  class ImmediateXGKICKHandler : public VUXGKICKHandler
  {
    public:
      std::vector<std::uint16_t> addresses;

      bool path1TransferActive() const override
      {
        return false;
      }

      void startPath1Transfer(std::uint16_t qwordAddress) override
      {
        addresses.push_back(qwordAddress);
      }
  };
}

TEST_CASE("VU1 rotation and projection capstone program")
{
  constexpr std::uint16_t PACKET_ADDRESS = 8;
  constexpr std::uint16_t FIRST_POINT_ADDRESS = 11;
  constexpr std::uint16_t LOOP_INSTRUCTION_ADDRESS = 8 * 8;
  constexpr std::uint16_t BRANCH_INSTRUCTION_ADDRESS = 31 * 8;

  VPU vpu(VPUType::VU1);
  ImmediateXGKICKHandler xgkickHandler;
  std::vector<std::uint8_t> initialMemory;

  vpu_integration::appendQword(
    &initialMemory, 2, FIRST_POINT_ADDRESS, 2, PACKET_ADDRESS);
  vpu_integration::appendQword(
    &initialMemory, 0, 0xbf800000, 0, 0);
  vpu_integration::appendQword(
    &initialMemory, 0x3f800000, 0, 0, 0);
  vpu_integration::appendQword(
    &initialMemory, 0, 0, 0x3f800000, 0);
  vpu_integration::appendQword(
    &initialMemory, 0x45000000, 0x41200000, 0x45800000, 0x41a00000);
  vpu_integration::appendQword(&initialMemory, 0, 0, 0, 0);
  vpu_integration::appendQword(&initialMemory, 0, 0, 0, 0);
  vpu_integration::appendQword(&initialMemory, 0, 0, 0, 0);
  vpu_integration::appendQword(
    &initialMemory, 0x11111111, 0x22222222, 0x33333333, 0x44444444);
  vpu_integration::appendQword(
    &initialMemory, 0x55555555, 0x66666666, 0x77777777, 0x88888888);
  vpu_integration::appendQword(
    &initialMemory, 0x10203040, 0x3f800000, 0x00000001, 0x00000005);
  vpu_integration::appendQword(
    &initialMemory, 0x40000000, 0xbf800000, 0x40800000, 0);
  vpu_integration::appendQword(
    &initialMemory, 0x50607080, 0x3f800000, 0x00000001, 0x00000005);
  vpu_integration::appendQword(
    &initialMemory, 0xc0400000, 0x40000000, 0x41000000, 0);
  vpu.writeDataMemory(0, initialMemory);
  vpu.setXGKICKHandler(&xgkickHandler);

  VPUProgramRunConfig config;
  config.microProgram =
    vpu_integration::readBinary("rotation_vu1_capstone.bin");
  config.cycleBudget = 1000;
  config.outputAddress = PACKET_ADDRESS * 16;
  config.outputSize = 6 * 16;
  config.captureTrace = true;

  const VPUProgramRunResult result = runVPUProgram(&vpu, config);

  std::vector<std::uint8_t> expected;
  vpu_integration::appendQword(
    &expected, 0x11111111, 0x22222222, 0x33333333, 0x44444444);
  vpu_integration::appendQword(
    &expected, 0x55555555, 0x66666666, 0x77777777, 0x88888888);
  vpu_integration::appendQword(
    &expected, 0x10203040, 0x3f800000, 0x00000001, 0x00000005);
  vpu_integration::appendQword(
    &expected, 0x000020a0, 0x00004140, 0x00000ffc, 0);
  vpu_integration::appendQword(
    &expected, 0x50607080, 0x3f800000, 0x00000001, 0x00000005);
  vpu_integration::appendQword(
    &expected, 0xffffe0a0, 0xffffd140, 0x00000ff8, 0);

  std::size_t loopEntries = 0;
  std::size_t loopBranches = 0;
  for (const VPUTraceEvent &event : result.traceEvents)
  {
    if (event.type != VPUTraceEventType::InstructionIssued)
    {
      continue;
    }
    loopEntries += event.instructionAddress == LOOP_INSTRUCTION_ADDRESS;
    loopBranches += event.instructionAddress == BRANCH_INSTRUCTION_ADDRESS;
  }

  REQUIRE(result.state == VPU_STATE_READY);
  REQUIRE(result.elapsedCycles == 215);
  REQUIRE(result.programCounter == 37 * 8);
  REQUIRE(result.hasTerminationPosition);
  REQUIRE(result.terminationPosition == 37);
  REQUIRE(result.outputMemory == expected);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI01) == 0);
  REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI02) == 15);
  REQUIRE(loopEntries == 2);
  REQUIRE(loopBranches == 2);
  REQUIRE(
    xgkickHandler.addresses ==
    std::vector<std::uint16_t>{PACKET_ADDRESS});
}
