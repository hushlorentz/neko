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

  std::uint32_t flagImmediate(
    std::uint32_t encoding,
    std::uint32_t immediate)
  {
    return encoding | (immediate & 0x00ffffff);
  }

  std::uint32_t fcget(std::uint8_t destination)
  {
    return
      VPU_FCGET_ENCODING |
      (static_cast<std::uint32_t>(destination) << 16);
  }

  std::uint32_t iaddiu(
    std::uint8_t destination,
    std::uint8_t source,
    std::uint16_t immediate)
  {
    return
      VPU_IADDIU_ENCODING |
      (static_cast<std::uint32_t>((immediate >> 11) & 0xf) << 21) |
      (static_cast<std::uint32_t>(destination) << 16) |
      (static_cast<std::uint32_t>(source) << 11) |
      (immediate & 0x7ff);
  }

  std::uint32_t ibne(
    std::uint8_t left,
    std::uint8_t right,
    std::int16_t immediate)
  {
    return
      VPU_IBNE_ENCODING |
      (static_cast<std::uint32_t>(left) << 16) |
      (static_cast<std::uint32_t>(right) << 11) |
      (static_cast<std::uint16_t>(immediate) & 0x7ff);
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

TEST_CASE("VU clipping flag access")
{
  SECTION("Clipping flag instructions decode their fixed operands")
  {
    for (const auto &contract : {
      std::make_pair(VPU_FCEQ_ENCODING, VPU_FCEQ),
      std::make_pair(VPU_FCSET_ENCODING, VPU_FCSET),
      std::make_pair(VPU_FCAND_ENCODING, VPU_FCAND),
      std::make_pair(VPU_FCOR_ENCODING, VPU_FCOR)})
    {
      const LowerInstruction decoded = decodeLowerInstruction(
        flagImmediate(contract.first, 0xabcdef));
      REQUIRE(decoded.unit == LowerExecutionUnit::Flag);
      REQUIRE(decoded.opCode == contract.second);
      REQUIRE(decoded.immediateBits == 0xabcdef);
    }

    const LowerInstruction get =
      decodeLowerInstruction(fcget(VPU_REGISTER_VI07));
    REQUIRE(get.unit == LowerExecutionUnit::Flag);
    REQUIRE(get.opCode == VPU_FCGET);
    REQUIRE(get.integerDestinationRegister == VPU_REGISTER_VI07);
  }

  SECTION("FCAND detects any common set bit")
  {
    VPU vpu;
    vpu.clippingFlags = 0x123456;
    runSingle(&vpu, flagImmediate(VPU_FCAND_ENCODING, 0x000040));
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI01) == 1);

    runSingle(&vpu, flagImmediate(VPU_FCAND_ENCODING, 0x800000));
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI01) == 0);
  }

  SECTION("FCEQ compares all twenty-four clipping bits")
  {
    VPU vpu;
    vpu.clippingFlags = 0xff123456;
    runSingle(&vpu, flagImmediate(VPU_FCEQ_ENCODING, 0x123456));
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI01) == 1);

    runSingle(&vpu, flagImmediate(VPU_FCEQ_ENCODING, 0x123457));
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI01) == 0);
  }

  SECTION("FCOR reports whether the twenty-four-bit OR is all ones")
  {
    VPU vpu;
    vpu.clippingFlags = 0x000002;
    runSingle(&vpu, flagImmediate(VPU_FCOR_ENCODING, 0xfffffd));
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI01) == 1);

    runSingle(&vpu, flagImmediate(VPU_FCOR_ENCODING, 0xfffff9));
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI01) == 0);
  }

  SECTION("FCGET returns only the two most recent clipping checks")
  {
    VPU vpu;
    vpu.clippingFlags = 0xabcdef;
    runSingle(&vpu, fcget(VPU_REGISTER_VI07));
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI07) == 0x0def);

    runSingle(&vpu, fcget(VPU_REGISTER_VI00));
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI00) == 0);
  }

  SECTION("An immediately following branch observes a flag test result")
  {
    VPU vpu;
    std::vector<std::uint8_t> instructions;
    vpu.clippingFlags = 1;

    appendPair(
      &instructions,
      VPU_NOP,
      flagImmediate(VPU_FCAND_ENCODING, 1));
    appendPair(
      &instructions,
      VPU_NOP,
      ibne(VPU_REGISTER_VI01, VPU_REGISTER_VI00, 2));
    appendPair(
      &instructions,
      VPU_NOP,
      iaddiu(VPU_REGISTER_VI02, VPU_REGISTER_VI00, 1));
    appendPair(
      &instructions,
      VPU_NOP,
      iaddiu(VPU_REGISTER_VI02, VPU_REGISTER_VI00, 2));
    appendPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      iaddiu(VPU_REGISTER_VI02, VPU_REGISTER_VI00, 4));
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI02) == 4);
  }

  SECTION("FCSET becomes visible at S rather than issue")
  {
    VPU vpu;
    std::vector<std::uint8_t> instructions;
    appendPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      flagImmediate(VPU_FCSET_ENCODING, 0x654321));
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();

    REQUIRE(vpu.run(5) == 5);
    REQUIRE(vpu.clippingFlags == 0);
    vpu.tick();
    REQUIRE(vpu.clippingFlags == 0x654321);
  }

  SECTION("FCSET overrides a simultaneous upper CLIP result")
  {
    VPU vpu;
    std::vector<std::uint8_t> instructions;
    vpu.loadFPRegister(VPU_REGISTER_VF01, 10, 0, 0, 0);
    vpu.loadFPRegister(VPU_REGISTER_VF02, 0, 0, 0, 1);

    const std::uint32_t clip =
      VPU_E_BIT |
      VPU_DEST_X_BIT |
      VPU_DEST_Y_BIT |
      VPU_DEST_Z_BIT |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF01) << VPU_FT_REG_SHIFT) |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF02) << VPU_FS_REG_SHIFT) |
      VPU_CLIP;
    appendPair(
      &instructions,
      clip,
      flagImmediate(VPU_FCSET_ENCODING, 0x654321));
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.clippingFlags == 0x654321);
  }
}
