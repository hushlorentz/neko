#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu.hpp"
#include "vpu_flags.hpp"
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

  std::uint32_t statusFlag(
    std::uint32_t encoding,
    std::uint8_t destination,
    std::uint16_t immediate)
  {
    return
      encoding |
      (static_cast<std::uint32_t>(immediate & 0x0800) << 10) |
      (static_cast<std::uint32_t>(destination) << 16) |
      (immediate & 0x07ff);
  }

  void establishStatusFlags(VPU *vpu)
  {
    std::vector<std::uint8_t> instructions;
    vpu->loadFPRegister(VPU_REGISTER_VF01, -1, 0, 2, 3);
    vpu->loadFPRegister(VPU_REGISTER_VF02, 0, 0, 0, 0);
    const std::uint32_t add =
      VPU_E_BIT |
      VPU_DEST_ALL_FIELDS |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF01) << VPU_FT_REG_SHIFT) |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF02) << VPU_FS_REG_SHIFT) |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF03) << VPU_FD_REG_SHIFT) |
      VPU_ADD;
    appendPair(&instructions, add, VPU_LOWER_NOP);
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu->uploadMicroInstructions(instructions);
    vpu->initMicroMode();
  }

  void runSingle(VPU *vpu, std::uint32_t lower)
  {
    std::vector<std::uint8_t> instructions;
    appendPair(&instructions, VPU_E_BIT | VPU_NOP, lower);
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu->uploadMicroInstructions(instructions);
    vpu->initMicroMode();
  }
}

TEST_CASE("VU status flag access")
{
  SECTION("Status flag instructions decode all twelve immediate bits")
  {
    for (const auto &contract : {
      std::make_pair(VPU_FSAND_ENCODING, VPU_FSAND),
      std::make_pair(VPU_FSEQ_ENCODING, VPU_FSEQ),
      std::make_pair(VPU_FSOR_ENCODING, VPU_FSOR)})
    {
      const LowerInstruction decoded = decodeLowerInstruction(statusFlag(
        contract.first,
        VPU_REGISTER_VI07,
        0x0abc));
      REQUIRE(decoded.unit == LowerExecutionUnit::Flag);
      REQUIRE(decoded.opCode == contract.second);
      REQUIRE(decoded.integerDestinationRegister == VPU_REGISTER_VI07);
      REQUIRE(decoded.immediateBits == 0x0abc);
    }

    const LowerInstruction set =
      decodeLowerInstruction(statusFlag(VPU_FSSET_ENCODING, 0, 0x0fc0));
    REQUIRE(set.opCode == VPU_FSSET);
    REQUIRE(set.immediateBits == 0x0fc0);
  }

  SECTION("Status tests inspect current and sticky arithmetic state")
  {
    VPU vpu;
    establishStatusFlags(&vpu);

    runSingle(
      &vpu,
      statusFlag(VPU_FSAND_ENCODING, VPU_REGISTER_VI01, 0x0082));
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI01) == 0x0082);

    runSingle(
      &vpu,
      statusFlag(VPU_FSEQ_ENCODING, VPU_REGISTER_VI02, 0x00c3));
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI02) == 1);

    runSingle(
      &vpu,
      statusFlag(VPU_FSOR_ENCODING, VPU_REGISTER_VI03, 0x0004));
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI03) == 0x00c7);
  }

  SECTION("FSSET replaces sticky flags while preserving current flags")
  {
    VPU vpu;
    establishStatusFlags(&vpu);
    runSingle(&vpu, statusFlag(VPU_FSSET_ENCODING, 0, 0x0fc0));

    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_Z));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_S));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_ZS));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_SS));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_US));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_OS));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_IS));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_DS));
  }

  SECTION("FSSET becomes visible at S")
  {
    VPU vpu;
    std::vector<std::uint8_t> instructions;
    appendPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      statusFlag(VPU_FSSET_ENCODING, 0, 0x0fc0));
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();

    REQUIRE(vpu.run(5) == 5);
    REQUIRE_FALSE(vpu.hasStatusFlag(VPU_FLAG_DS));
    vpu.tick();
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_DS));
  }

  SECTION("FSSET overrides simultaneous upper sticky-flag results")
  {
    VPU vpu;
    std::vector<std::uint8_t> instructions;
    vpu.loadFPRegister(VPU_REGISTER_VF01, -1, 0, 2, 3);
    vpu.loadFPRegister(VPU_REGISTER_VF02, 0, 0, 0, 0);
    const std::uint32_t add =
      VPU_E_BIT |
      VPU_DEST_ALL_FIELDS |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF01) << VPU_FT_REG_SHIFT) |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF02) << VPU_FS_REG_SHIFT) |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF03) << VPU_FD_REG_SHIFT) |
      VPU_ADD;
    appendPair(
      &instructions,
      add,
      statusFlag(VPU_FSSET_ENCODING, 0, VPU_FLAG_US));
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE_FALSE(vpu.hasStatusFlag(VPU_FLAG_ZS));
    REQUIRE_FALSE(vpu.hasStatusFlag(VPU_FLAG_SS));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_US));
  }
}
