#include <cstdint>
#include <utility>
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

  std::uint32_t vectorEFU(
    std::uint32_t encoding,
    std::uint8_t source)
  {
    return
      encoding |
      (static_cast<std::uint32_t>(source) << VPU_FS_REG_SHIFT);
  }

  std::uint32_t esadd(std::uint8_t source)
  {
    return vectorEFU(VPU_ESADD_ENCODING, source);
  }

  std::uint32_t scalarEFU(
    std::uint32_t encoding,
    std::uint8_t source,
    std::uint8_t field)
  {
    return
      encoding |
      (static_cast<std::uint32_t>(field) << 21) |
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
  SECTION("Reduction operations and WAITP decode their fixed fields")
  {
    const LowerInstruction sum =
      decodeLowerInstruction(esum(VPU_REGISTER_VF07));
    REQUIRE(sum.unit == LowerExecutionUnit::EFU);
    REQUIRE(sum.opCode == VPU_ESUM);
    REQUIRE(sum.sourceRegister1 == VPU_REGISTER_VF07);
    REQUIRE(sum.sourceFieldMask1 == FP_REGISTER_ALL_FIELDS);

    const LowerInstruction squareSum =
      decodeLowerInstruction(esadd(VPU_REGISTER_VF08));
    REQUIRE(squareSum.unit == LowerExecutionUnit::EFU);
    REQUIRE(squareSum.opCode == VPU_ESADD);
    REQUIRE(squareSum.sourceRegister1 == VPU_REGISTER_VF08);
    REQUIRE(
      squareSum.sourceFieldMask1 ==
      (FP_REGISTER_X_FIELD |
       FP_REGISTER_Y_FIELD |
       FP_REGISTER_Z_FIELD));

    const LowerInstruction squareRoot = decodeLowerInstruction(
      scalarEFU(VPU_ESQRT_ENCODING, VPU_REGISTER_VF09, 2));
    REQUIRE(squareRoot.unit == LowerExecutionUnit::EFU);
    REQUIRE(squareRoot.opCode == VPU_ESQRT);
    REQUIRE(squareRoot.sourceRegister1 == VPU_REGISTER_VF09);
    REQUIRE(squareRoot.sourceFieldMask1 == FP_REGISTER_Z_FIELD);

    const LowerInstruction reciprocalSquareRoot = decodeLowerInstruction(
      scalarEFU(VPU_ERSQRT_ENCODING, VPU_REGISTER_VF10, 3));
    REQUIRE(reciprocalSquareRoot.unit == LowerExecutionUnit::EFU);
    REQUIRE(reciprocalSquareRoot.opCode == VPU_ERSQRT);
    REQUIRE(reciprocalSquareRoot.sourceRegister1 == VPU_REGISTER_VF10);
    REQUIRE(reciprocalSquareRoot.sourceFieldMask1 == FP_REGISTER_W_FIELD);

    for (const auto &operation : {
      std::pair<std::uint32_t, std::uint16_t>{
        VPU_ELENG_ENCODING,
        VPU_ELENG},
      {VPU_ERLENG_ENCODING, VPU_ERLENG},
      {VPU_ERSADD_ENCODING, VPU_ERSADD}})
    {
      const LowerInstruction decoded = decodeLowerInstruction(
        vectorEFU(operation.first, VPU_REGISTER_VF11));
      REQUIRE(decoded.unit == LowerExecutionUnit::EFU);
      REQUIRE(decoded.opCode == operation.second);
      REQUIRE(decoded.sourceRegister1 == VPU_REGISTER_VF11);
      REQUIRE(
        decoded.sourceFieldMask1 ==
        (FP_REGISTER_X_FIELD |
         FP_REGISTER_Y_FIELD |
         FP_REGISTER_Z_FIELD));
    }

    const LowerInstruction reciprocal = decodeLowerInstruction(
      scalarEFU(VPU_ERCPR_ENCODING, VPU_REGISTER_VF12, 1));
    REQUIRE(reciprocal.unit == LowerExecutionUnit::EFU);
    REQUIRE(reciprocal.opCode == VPU_ERCPR);
    REQUIRE(reciprocal.sourceRegister1 == VPU_REGISTER_VF12);
    REQUIRE(reciprocal.sourceFieldMask1 == FP_REGISTER_Y_FIELD);

    const LowerInstruction wait =
      decodeLowerInstruction(VPU_WAITP_ENCODING);
    REQUIRE(wait.unit == LowerExecutionUnit::WaitP);
    REQUIRE(wait.opCode == VPU_WAITP);
  }

  SECTION("Reserved reduction and WAITP encodings are rejected")
  {
    REQUIRE_THROWS_WITH(
      decodeLowerInstruction(
        esum(VPU_REGISTER_VF07) ^ VPU_DEST_X_BIT),
      "Unsupported VU lower instruction.");
    REQUIRE_THROWS_WITH(
      decodeLowerInstruction(
        esadd(VPU_REGISTER_VF07) ^ VPU_DEST_X_BIT),
      "Unsupported VU lower instruction.");
    REQUIRE_THROWS_WITH(
      decodeLowerInstruction(
        scalarEFU(VPU_ESQRT_ENCODING, VPU_REGISTER_VF07, 0) |
        (1u << VPU_FT_REG_SHIFT)),
      "Unsupported VU lower instruction.");
    REQUIRE_THROWS_WITH(
      decodeLowerInstruction(
        VPU_WAITP_ENCODING ^ (1u << VPU_FS_REG_SHIFT)),
      "Unsupported VU lower instruction.");
  }

  SECTION("ESQRT reads the selected lane and uses absolute magnitude")
  {
    VPU vpu(VPUType::VU1);
    std::vector<std::uint8_t> instructions;
    vpu.loadPRegister(-1);
    vpu.loadFPRegister(VPU_REGISTER_VF01, 4, -9, 16, 25);
    appendPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      scalarEFU(VPU_ESQRT_ENCODING, VPU_REGISTER_VF01, 1));
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();

    REQUIRE(vpu.run(13) == 13);
    REQUIRE(vpu.pRegisterBits() == 0xbf800000);
    vpu.tick();
    REQUIRE(vpu.pRegisterBits() == 0x40400000);
  }

  SECTION("ERSQRT reads the selected lane and uses absolute magnitude")
  {
    VPU vpu(VPUType::VU1);
    std::vector<std::uint8_t> instructions;
    vpu.loadPRegister(-1);
    vpu.loadFPRegister(VPU_REGISTER_VF01, 4, 9, -16, 25);
    appendPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      scalarEFU(VPU_ERSQRT_ENCODING, VPU_REGISTER_VF01, 2));
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();

    REQUIRE(vpu.run(19) == 19);
    REQUIRE(vpu.pRegisterBits() == 0xbf800000);
    vpu.tick();
    REQUIRE(vpu.pRegisterBits() == 0x3e800000);
  }

  SECTION("ESADD squares XYZ in order, ignores W, and observes its latency")
  {
    VPU vpu(VPUType::VU1);
    std::vector<std::uint8_t> instructions;
    vpu.loadPRegister(-1);
    vpu.loadFPRegister(VPU_REGISTER_VF01, -3, 4, -12, 999);
    appendPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      esadd(VPU_REGISTER_VF01));
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();

    REQUIRE(vpu.run(12) == 12);
    REQUIRE(vpu.pRegisterBits() == 0xbf800000);
    vpu.tick();
    REQUIRE(vpu.pRegisterBits() == 0x43290000);
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
      vectorEFU(VPU_ELENG_ENCODING, VPU_REGISTER_VF01),
      scalarEFU(VPU_ERCPR_ENCODING, VPU_REGISTER_VF01, 0),
      vectorEFU(VPU_ERLENG_ENCODING, VPU_REGISTER_VF01),
      vectorEFU(VPU_ERSADD_ENCODING, VPU_REGISTER_VF01),
      scalarEFU(VPU_ESQRT_ENCODING, VPU_REGISTER_VF01, 0),
      scalarEFU(VPU_ERSQRT_ENCODING, VPU_REGISTER_VF01, 0),
      esadd(VPU_REGISTER_VF01),
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
