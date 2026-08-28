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

  std::uint32_t mfp(
    std::uint8_t destination,
    std::uint32_t fieldMask)
  {
    return
      VPU_MFP_ENCODING |
      fieldMask |
      (static_cast<std::uint32_t>(destination) << VPU_FT_REG_SHIFT);
  }

  void runSingle(VPU *vpu, std::uint32_t upper, std::uint32_t lower)
  {
    std::vector<std::uint8_t> instructions;
    appendPair(&instructions, VPU_E_BIT | upper, lower);
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu->uploadMicroInstructions(instructions);
    vpu->initMicroMode();
  }
}

TEST_CASE("VU1 P register movement")
{
  SECTION("MFP decodes its destination and lane mask")
  {
    const LowerInstruction decoded = decodeLowerInstruction(
      mfp(
        VPU_REGISTER_VF07,
        VPU_DEST_X_BIT | VPU_DEST_Z_BIT));

    REQUIRE(decoded.unit == LowerExecutionUnit::FMAC);
    REQUIRE(decoded.opCode == VPU_MFP);
    REQUIRE(decoded.destinationRegister == VPU_REGISTER_VF07);
    REQUIRE(
      decoded.destinationFieldMask ==
      (FP_REGISTER_X_FIELD | FP_REGISTER_Z_FIELD));
  }

  SECTION("Reserved MFP source encodings are rejected")
  {
    REQUIRE_THROWS_WITH(
      decodeLowerInstruction(
        mfp(VPU_REGISTER_VF07, VPU_DEST_X_BIT) |
        (1u << VPU_FS_REG_SHIFT)),
      "Unsupported VU lower instruction.");
  }

  SECTION("MFP broadcasts the raw P value to selected lanes")
  {
    VPU vpu(VPUType::VU1);
    vpu.loadPRegister(-1.5);
    vpu.loadIntFPRegister(
      VPU_REGISTER_VF02,
      0x11111111,
      0x22222222,
      0x33333333,
      0x44444444);

    runSingle(
      &vpu,
      VPU_NOP,
      mfp(
        VPU_REGISTER_VF02,
        VPU_DEST_X_BIT | VPU_DEST_Z_BIT));

    const FPRegister *result = vpu.fpRegisterValue(VPU_REGISTER_VF02);
    REQUIRE(vpu.pRegisterBits() == 0xbfc00000);
    REQUIRE(result->x.bits() == 0xbfc00000);
    REQUIRE(result->y.bits() == 0x22222222);
    REQUIRE(result->z.bits() == 0xbfc00000);
    REQUIRE(result->w.bits() == 0x44444444);
  }

  SECTION("MFP writes at S")
  {
    VPU vpu(VPUType::VU1);
    std::vector<std::uint8_t> instructions;
    vpu.loadPRegister(2.5);
    appendPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      mfp(VPU_REGISTER_VF02, VPU_DEST_X_BIT));
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();

    REQUIRE(vpu.run(5) == 5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x.bits() == 0);
    vpu.tick();
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x.bits() == 0x40200000);
  }

  SECTION("MFP creates a normal VF destination hazard")
  {
    VPU vpu(VPUType::VU1);
    std::vector<std::uint8_t> instructions;
    vpu.loadPRegister(2);
    vpu.loadFPRegister(VPU_REGISTER_VF01, 1, 0, 0, 0);
    appendPair(
      &instructions,
      VPU_NOP,
      mfp(VPU_REGISTER_VF02, VPU_DEST_X_BIT));
    const std::uint32_t add =
      VPU_E_BIT |
      VPU_DEST_X_BIT |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF01) << VPU_FT_REG_SHIFT) |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF02) << VPU_FS_REG_SHIFT) |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF03) << VPU_FD_REG_SHIFT) |
      VPU_ADD;
    appendPair(&instructions, add, VPU_LOWER_NOP);
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->x.bits() == 0x40400000);
  }

  SECTION("MFP writes to VF00 are discarded")
  {
    VPU vpu(VPUType::VU1);
    vpu.loadPRegister(2.5);
    runSingle(
      &vpu,
      VPU_NOP,
      mfp(VPU_REGISTER_VF00, VPU_DEST_ALL_FIELDS));

    const FPRegister *vf00 = vpu.fpRegisterValue(VPU_REGISTER_VF00);
    REQUIRE(vf00->x.bits() == 0);
    REQUIRE(vf00->y.bits() == 0);
    REQUIRE(vf00->z.bits() == 0);
    REQUIRE(vf00->w.bits() == 0x3f800000);
  }

  SECTION("An upper write discards a simultaneous MFP write")
  {
    VPU vpu(VPUType::VU1);
    vpu.loadPRegister(10);
    vpu.loadFPRegister(VPU_REGISTER_VF01, 1, 2, 3, 4);
    const std::uint32_t upper =
      VPU_DEST_X_BIT |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF01) << VPU_FT_REG_SHIFT) |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF00) << VPU_FS_REG_SHIFT) |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF02) << VPU_FD_REG_SHIFT) |
      VPU_ADD;

    runSingle(
      &vpu,
      upper,
      mfp(VPU_REGISTER_VF02, VPU_DEST_Y_BIT));

    const FPRegister *result = vpu.fpRegisterValue(VPU_REGISTER_VF02);
    REQUIRE(result->x.bits() == 0x3f800000);
    REQUIRE(result->y.bits() == 0);
  }

  SECTION("MFP is rejected on VU0")
  {
    VPU vpu(VPUType::VU0);
    std::vector<std::uint8_t> instructions;
    appendPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      mfp(VPU_REGISTER_VF02, VPU_DEST_X_BIT));
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu.uploadMicroInstructions(instructions);

    REQUIRE_THROWS_WITH(
      vpu.initMicroMode(),
      "MFP is only supported on VU1.");
  }
}
