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

  std::uint32_t macFlag(
    std::uint32_t encoding,
    std::uint8_t destination,
    std::uint8_t source)
  {
    return
      encoding |
      (static_cast<std::uint32_t>(destination) << 16) |
      (static_cast<std::uint32_t>(source) << 11);
  }

  std::uint32_t mtir(
    std::uint8_t destination,
    std::uint8_t source,
    std::uint8_t field)
  {
    return
      VPU_MTIR_ENCODING |
      (static_cast<std::uint32_t>(field) << 21) |
      (static_cast<std::uint32_t>(destination) << 16) |
      (static_cast<std::uint32_t>(source) << 11);
  }

  void establishMACFlags(VPU *vpu)
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

TEST_CASE("VU MAC flag access")
{
  SECTION("MAC flag instructions decode both integer registers")
  {
    for (const auto &contract : {
      std::make_pair(VPU_FMAND_ENCODING, VPU_FMAND),
      std::make_pair(VPU_FMEQ_ENCODING, VPU_FMEQ),
      std::make_pair(VPU_FMOR_ENCODING, VPU_FMOR)})
    {
      const LowerInstruction decoded = decodeLowerInstruction(macFlag(
        contract.first,
        VPU_REGISTER_VI07,
        VPU_REGISTER_VI06));
      REQUIRE(decoded.unit == LowerExecutionUnit::Flag);
      REQUIRE(decoded.opCode == contract.second);
      REQUIRE(decoded.sourceRegister1 == VPU_REGISTER_VI06);
      REQUIRE(decoded.integerDestinationRegister == VPU_REGISTER_VI07);
    }
  }

  SECTION("FMAND returns the common MAC and source bits")
  {
    VPU vpu;
    establishMACFlags(&vpu);
    vpu.loadIntRegister(VPU_REGISTER_VI01, 0x008c);
    runSingle(
      &vpu,
      macFlag(
        VPU_FMAND_ENCODING,
        VPU_REGISTER_VI02,
        VPU_REGISTER_VI01));
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI02) == 0x0084);
  }

  SECTION("FMEQ returns a Boolean equality result")
  {
    VPU vpu;
    establishMACFlags(&vpu);
    vpu.loadIntRegister(VPU_REGISTER_VI01, 0x0084);
    runSingle(
      &vpu,
      macFlag(
        VPU_FMEQ_ENCODING,
        VPU_REGISTER_VI02,
        VPU_REGISTER_VI01));
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI02) == 1);

    vpu.loadIntRegister(VPU_REGISTER_VI01, 0x0085);
    runSingle(
      &vpu,
      macFlag(
        VPU_FMEQ_ENCODING,
        VPU_REGISTER_VI02,
        VPU_REGISTER_VI01));
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI02) == 0);
  }

  SECTION("FMOR returns the union of MAC and source bits")
  {
    VPU vpu;
    establishMACFlags(&vpu);
    vpu.loadIntRegister(VPU_REGISTER_VI01, 0x0003);
    runSingle(
      &vpu,
      macFlag(
        VPU_FMOR_ENCODING,
        VPU_REGISTER_VI02,
        VPU_REGISTER_VI01));
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI02) == 0x0087);
  }

  SECTION("MAC flag writes to VI00 are discarded")
  {
    VPU vpu;
    establishMACFlags(&vpu);
    vpu.loadIntRegister(VPU_REGISTER_VI01, 0xffff);
    runSingle(
      &vpu,
      macFlag(
        VPU_FMOR_ENCODING,
        VPU_REGISTER_VI00,
        VPU_REGISTER_VI01));
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI00) == 0);
  }

  SECTION("MAC flag operations wait for pending non-IALU sources")
  {
    VPU vpu;
    std::vector<std::uint8_t> instructions;
    establishMACFlags(&vpu);
    vpu.loadIntFPRegister(VPU_REGISTER_VF04, 0x008c, 0, 0, 0);

    appendPair(
      &instructions,
      VPU_NOP,
      mtir(VPU_REGISTER_VI01, VPU_REGISTER_VF04, 0));
    appendPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      macFlag(
        VPU_FMAND_ENCODING,
        VPU_REGISTER_VI02,
        VPU_REGISTER_VI01));
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI02) == 0x0084);
  }
}
