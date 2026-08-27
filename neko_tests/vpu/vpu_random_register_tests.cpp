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

  std::uint32_t randomSource(
    std::uint32_t encoding,
    std::uint8_t source,
    std::uint8_t field)
  {
    return
      encoding |
      (static_cast<std::uint32_t>(field) << 21) |
      (static_cast<std::uint32_t>(source) << 11);
  }

  std::uint32_t randomDestination(
    std::uint32_t encoding,
    std::uint8_t destination,
    std::uint32_t fieldMask)
  {
    return
      encoding |
      fieldMask |
      (static_cast<std::uint32_t>(destination) << 16);
  }

  void runProgram(
    VPU *vpu,
    const std::vector<std::uint32_t> &lowerInstructions)
  {
    std::vector<std::uint8_t> instructions;
    for (std::size_t index = 0; index < lowerInstructions.size(); index++)
    {
      const bool final = index + 1 == lowerInstructions.size();
      appendPair(
        &instructions,
        (final ? VPU_E_BIT : 0) | VPU_NOP,
        lowerInstructions[index]);
    }
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu->uploadMicroInstructions(instructions);
    vpu->initMicroMode();
  }
}

TEST_CASE("VU random register operations")
{
  SECTION("Random instructions decode source and destination fields")
  {
    for (const auto &contract : {
      std::make_pair(VPU_RGET_ENCODING, VPU_RGET),
      std::make_pair(VPU_RNEXT_ENCODING, VPU_RNEXT)})
    {
      const LowerInstruction decoded = decodeLowerInstruction(
        randomDestination(
          contract.first,
          VPU_REGISTER_VF07,
          VPU_DEST_X_BIT | VPU_DEST_Z_BIT));
      REQUIRE(decoded.unit == LowerExecutionUnit::Random);
      REQUIRE(decoded.opCode == contract.second);
      REQUIRE(decoded.destinationRegister == VPU_REGISTER_VF07);
      REQUIRE(
        decoded.destinationFieldMask ==
        (FP_REGISTER_X_FIELD | FP_REGISTER_Z_FIELD));
    }

    for (const auto &contract : {
      std::make_pair(VPU_RINIT_ENCODING, VPU_RINIT),
      std::make_pair(VPU_RXOR_ENCODING, VPU_RXOR)})
    {
      const LowerInstruction decoded = decodeLowerInstruction(
        randomSource(contract.first, VPU_REGISTER_VF06, 2));
      REQUIRE(decoded.unit == LowerExecutionUnit::Random);
      REQUIRE(decoded.opCode == contract.second);
      REQUIRE(decoded.sourceRegister1 == VPU_REGISTER_VF06);
      REQUIRE(decoded.sourceFieldMask1 == FP_REGISTER_Z_FIELD);
    }
  }

  SECTION("RINIT masks the selected source to twenty-three bits")
  {
    VPU vpu;
    vpu.loadIntFPRegister(VPU_REGISTER_VF01, 0, 0, 0x12abcdef, 0);
    runProgram(
      &vpu,
      {
        randomSource(VPU_RINIT_ENCODING, VPU_REGISTER_VF01, 2),
        randomDestination(
          VPU_RGET_ENCODING,
          VPU_REGISTER_VF02,
          VPU_DEST_ALL_FIELDS)
      });

    REQUIRE(vpu.rRegisterBits() == 0x3fabcdef);
    const FPRegister *result = vpu.fpRegisterValue(VPU_REGISTER_VF02);
    REQUIRE(result->x.bits() == 0x3fabcdef);
    REQUIRE(result->y.bits() == 0x3fabcdef);
    REQUIRE(result->z.bits() == 0x3fabcdef);
    REQUIRE(result->w.bits() == 0x3fabcdef);
  }

  SECTION("RXOR combines only source mantissa bits")
  {
    VPU vpu;
    vpu.loadIntFPRegister(VPU_REGISTER_VF01, 0x00123456, 0, 0, 0);
    vpu.loadIntFPRegister(VPU_REGISTER_VF02, 0, 0xff00f00f, 0, 0);
    runProgram(
      &vpu,
      {
        randomSource(VPU_RINIT_ENCODING, VPU_REGISTER_VF01, 0),
        randomSource(VPU_RXOR_ENCODING, VPU_REGISTER_VF02, 1)
      });

    REQUIRE(vpu.rRegisterBits() == 0x3f92c459);
  }

  SECTION("RNEXT follows the documented M-sequence transition")
  {
    VPU vpu;
    vpu.loadIntFPRegister(VPU_REGISTER_VF01, 0x00400010, 0, 0, 0);
    runProgram(
      &vpu,
      {
        randomSource(VPU_RINIT_ENCODING, VPU_REGISTER_VF01, 0),
        randomDestination(
          VPU_RNEXT_ENCODING,
          VPU_REGISTER_VF02,
          VPU_DEST_X_BIT),
        randomDestination(
          VPU_RNEXT_ENCODING,
          VPU_REGISTER_VF03,
          VPU_DEST_Y_BIT)
      });

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x.bits() == 0x3f800020);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->y.bits() == 0x3f800040);
    REQUIRE(vpu.rRegisterBits() == 0x3f800040);
  }

  SECTION("RNEXT feedback uses source state bits four and twenty-two")
  {
    struct Transition
    {
      std::uint32_t input;
      std::uint32_t output;
    };

    for (const Transition &transition : {
      Transition{0x000000, 0x000000},
      Transition{0x000010, 0x000021},
      Transition{0x400000, 0x000001},
      Transition{0x400010, 0x000020},
      Transition{0x7fffff, 0x7ffffe}})
    {
      VPU vpu;
      vpu.loadIntFPRegister(
        VPU_REGISTER_VF01,
        static_cast<std::int32_t>(transition.input),
        0,
        0,
        0);
      runProgram(
        &vpu,
        {
          randomSource(VPU_RINIT_ENCODING, VPU_REGISTER_VF01, 0),
          randomDestination(
            VPU_RNEXT_ENCODING,
            VPU_REGISTER_VF02,
            VPU_DEST_X_BIT)
        });

      REQUIRE(
        vpu.rRegisterBits() ==
        (0x3f800000 | transition.output));
    }
  }

  SECTION("RGET writes at S and does not advance R")
  {
    VPU vpu;
    std::vector<std::uint8_t> instructions;
    vpu.loadIntFPRegister(VPU_REGISTER_VF01, 0x0055aa33, 0, 0, 0);
    appendPair(
      &instructions,
      VPU_NOP,
      randomSource(VPU_RINIT_ENCODING, VPU_REGISTER_VF01, 0));
    appendPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      randomDestination(
        VPU_RGET_ENCODING,
        VPU_REGISTER_VF02,
        VPU_DEST_X_BIT));
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();

    REQUIRE(vpu.run(6) == 6);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x.bits() == 0);
    vpu.tick();
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x.bits() == 0x3fd5aa33);
    REQUIRE(vpu.rRegisterBits() == 0x3fd5aa33);
  }

  SECTION("Random source operations wait for pending VF writes")
  {
    VPU vpu;
    vpu.loadIntFPRegister(VPU_REGISTER_VF01, 0x00123456, 0, 0, 0);
    runProgram(
      &vpu,
      {
        randomDestination(
          VPU_RGET_ENCODING,
          VPU_REGISTER_VF02,
          VPU_DEST_X_BIT),
        VPU_MOVE_ENCODING |
          VPU_DEST_X_BIT |
          (static_cast<std::uint32_t>(VPU_REGISTER_VF02) << 16) |
          (static_cast<std::uint32_t>(VPU_REGISTER_VF01) << 11),
        randomSource(VPU_RINIT_ENCODING, VPU_REGISTER_VF02, 0)
      });

    REQUIRE(vpu.rRegisterBits() == 0x3f923456);
  }

  SECTION("An upper write discards a simultaneous random VF write")
  {
    VPU vpu;
    std::vector<std::uint8_t> instructions;
    vpu.loadFPRegister(VPU_REGISTER_VF01, 1, 2, 3, 4);
    vpu.loadIntFPRegister(VPU_REGISTER_VF03, 0x10, 0, 0, 0);
    appendPair(
      &instructions,
      VPU_NOP,
      randomSource(VPU_RINIT_ENCODING, VPU_REGISTER_VF03, 0));
    const std::uint32_t upperMove =
      VPU_E_BIT |
      VPU_DEST_X_BIT |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF01) << VPU_FS_REG_SHIFT) |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF02) << VPU_FD_REG_SHIFT) |
      VPU_ADD;
    appendPair(
      &instructions,
      upperMove,
      randomDestination(
        VPU_RNEXT_ENCODING,
        VPU_REGISTER_VF02,
        VPU_DEST_Y_BIT));
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    const FPRegister *result = vpu.fpRegisterValue(VPU_REGISTER_VF02);
    REQUIRE(result->x.bits() == 0x3f800000);
    REQUIRE(result->y.bits() == 0);
    REQUIRE(vpu.rRegisterBits() == 0x3f800021);
  }
}
