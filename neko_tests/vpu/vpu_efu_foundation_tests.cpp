#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu.hpp"
#include "vpu_lower_instruction.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"

namespace
{
  void appendWord(std::vector<std::uint8_t> *bytes, std::uint32_t word)
  {
    bytes->push_back(word & 0xff);
    bytes->push_back((word >> 8) & 0xff);
    bytes->push_back((word >> 16) & 0xff);
    bytes->push_back((word >> 24) & 0xff);
  }

  void appendPair(
    std::vector<std::uint8_t> *instructions,
    std::uint32_t upper,
    std::uint32_t lower)
  {
    appendWord(instructions, lower);
    appendWord(instructions, upper);
  }

  std::uint32_t esum(std::uint8_t source)
  {
    return
      VPU_ESUM_ENCODING |
      (static_cast<std::uint32_t>(source) << VPU_FS_REG_SHIFT);
  }

  std::uint32_t mfp(
    std::uint8_t destination,
    std::uint32_t fieldMask)
  {
    return
      VPU_MFP_ENCODING |
      fieldMask |
      (static_cast<std::uint32_t>(destination) << VPU_FT_REG_SHIFT);
  }

  void appendTermination(std::vector<std::uint8_t> *instructions)
  {
    appendPair(instructions, VPU_E_BIT | VPU_NOP, VPU_LOWER_NOP);
    appendPair(instructions, VPU_NOP, VPU_LOWER_NOP);
  }
}

TEST_CASE("VU1 EFU synchronization foundation")
{
  SECTION("ESUM and WAITP decode their fixed fields")
  {
    const LowerInstruction sum =
      decodeLowerInstruction(esum(VPU_REGISTER_VF07));
    REQUIRE(sum.unit == LowerExecutionUnit::EFU);
    REQUIRE(sum.opCode == VPU_ESUM);
    REQUIRE(sum.sourceRegister1 == VPU_REGISTER_VF07);
    REQUIRE(sum.sourceFieldMask1 == FP_REGISTER_ALL_FIELDS);

    const LowerInstruction wait =
      decodeLowerInstruction(VPU_WAITP_ENCODING);
    REQUIRE(wait.unit == LowerExecutionUnit::WaitP);
    REQUIRE(wait.opCode == VPU_WAITP);
  }

  SECTION("Reserved ESUM and WAITP encodings are rejected")
  {
    REQUIRE_THROWS_WITH(
      decodeLowerInstruction(
        esum(VPU_REGISTER_VF07) ^ VPU_DEST_X_BIT),
      "Unsupported VU lower instruction.");
    REQUIRE_THROWS_WITH(
      decodeLowerInstruction(
        VPU_WAITP_ENCODING ^ (1u << VPU_FS_REG_SHIFT)),
      "Unsupported VU lower instruction.");
  }

  SECTION("ESUM writes P only after its documented latency")
  {
    VPU vpu(VPUType::VU1);
    std::vector<std::uint8_t> instructions;
    vpu.loadPRegister(-1);
    vpu.loadFPRegister(VPU_REGISTER_VF01, 1, 2, 3, 4);
    appendPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      esum(VPU_REGISTER_VF01));
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();

    REQUIRE(vpu.run(13) == 13);
    REQUIRE(vpu.pRegisterBits() == 0xbf800000);
    vpu.tick();
    REQUIRE(vpu.pRegisterBits() == 0x41200000);
  }

  SECTION("MFP sees stale P without WAITP")
  {
    VPU vpu(VPUType::VU1);
    std::vector<std::uint8_t> instructions;
    vpu.loadPRegister(-1);
    vpu.loadFPRegister(VPU_REGISTER_VF01, 1, 2, 3, 4);
    appendPair(&instructions, VPU_NOP, esum(VPU_REGISTER_VF01));
    appendPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      mfp(VPU_REGISTER_VF02, VPU_DEST_X_BIT));
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.pRegisterBits() == 0x41200000);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x.bits() == 0xbf800000);
  }

  SECTION("WAITP makes the pending P result visible to MFP")
  {
    VPU vpu(VPUType::VU1);
    std::vector<std::uint8_t> instructions;
    vpu.loadPRegister(-1);
    vpu.loadFPRegister(VPU_REGISTER_VF01, 1, 2, 3, 4);
    appendPair(&instructions, VPU_NOP, esum(VPU_REGISTER_VF01));
    appendPair(&instructions, VPU_NOP, VPU_WAITP_ENCODING);
    appendPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      mfp(VPU_REGISTER_VF02, VPU_DEST_X_BIT));
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.pRegisterBits() == 0x41200000);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x.bits() == 0x41200000);
  }

  SECTION("A second EFU operation waits for the shared unit")
  {
    VPU vpu(VPUType::VU1);
    std::vector<std::uint8_t> instructions;
    vpu.loadFPRegister(VPU_REGISTER_VF01, 1, 2, 3, 4);
    vpu.loadFPRegister(VPU_REGISTER_VF02, 10, 20, 30, 40);
    appendPair(&instructions, VPU_NOP, esum(VPU_REGISTER_VF01));
    appendPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      esum(VPU_REGISTER_VF02));
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.pRegisterBits() == 0x42c80000);
    REQUIRE(vpu.elapsedCycles() == 26);
  }

  SECTION("ESUM waits for a pending source lane")
  {
    VPU vpu(VPUType::VU1);
    std::vector<std::uint8_t> instructions;
    vpu.loadFPRegister(VPU_REGISTER_VF01, 1, 2, 3, 4);
    const std::uint32_t move =
      VPU_MOVE_ENCODING |
      VPU_DEST_X_BIT |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF02) <<
       VPU_FT_REG_SHIFT) |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF01) <<
       VPU_FS_REG_SHIFT);
    appendPair(&instructions, VPU_NOP, move);
    appendPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      esum(VPU_REGISTER_VF02));
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.pRegisterBits() == 0x3f800000);
  }

  SECTION("Program termination drains an active EFU operation")
  {
    VPU vpu(VPUType::VU1);
    std::vector<std::uint8_t> instructions;
    vpu.loadFPRegister(VPU_REGISTER_VF01, 1, 2, 3, 4);
    appendPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      esum(VPU_REGISTER_VF01));
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.getState() == VPU_STATE_READY);
    REQUIRE(vpu.pRegisterBits() == 0x41200000);
  }

  SECTION("Force Break cancels pending EFU writeback")
  {
    VPU vpu(VPUType::VU1);
    std::vector<std::uint8_t> instructions;
    vpu.loadPRegister(-1);
    vpu.loadFPRegister(VPU_REGISTER_VF01, 1, 2, 3, 4);
    appendPair(&instructions, VPU_NOP, esum(VPU_REGISTER_VF01));
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    appendTermination(&instructions);
    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();

    vpu.run(3);
    vpu.forceBreak();

    REQUIRE(vpu.getState() == VPU_STATE_STOP);
    REQUIRE(vpu.pRegisterBits() == 0xbf800000);
  }

  SECTION("EFU operations and WAITP are rejected on VU0")
  {
    for (const std::uint32_t lower : {
      esum(VPU_REGISTER_VF01),
      static_cast<std::uint32_t>(VPU_WAITP_ENCODING)})
    {
      VPU vpu(VPUType::VU0);
      std::vector<std::uint8_t> instructions;
      appendPair(&instructions, VPU_E_BIT | VPU_NOP, lower);
      appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
      vpu.uploadMicroInstructions(instructions);

      REQUIRE_THROWS(vpu.initMicroMode());
    }
  }
}
