#include "catch.hpp"
#include "vpu.hpp"
#include "vpu_opcodes.hpp"

namespace
{
  void appendInstruction(std::vector<uint8_t> * instructions, uint32_t instruction)
  {
    instructions->push_back(instruction & 0xff);
    instructions->push_back((instruction >> 8) & 0xff);
    instructions->push_back((instruction >> 16) & 0xff);
    instructions->push_back((instruction >> 24) & 0xff);
  }

  void appendInstructionPair(
    std::vector<uint8_t> *instructions,
    uint32_t upper,
    uint32_t lower)
  {
    appendInstruction(instructions, lower);
    appendInstruction(instructions, upper);
  }
}

TEST_CASE("VPU Microinstruction Tests")
{
    VPU vpu;
    
    SECTION("VPU1 Microinstruction programs can be started by writing the execution address to the control register CMSAR1")
    {
      //WARN("Add this test");
    }

    SECTION("VPU0 Microinstruction programs can be started by executing the VCALLMS instruction")
    {
      //WARN("Add this test");
    }

    SECTION("VPU0 Microinstruction programs can be started by executing the VCALLMSR instruction")
    {
      //WARN("Add this test");
    }

    SECTION("The lower half of an instruction pair is fetched independently of the upper half")
    {
      std::vector<uint8_t> instructions;
      appendInstructionPair(
        &instructions,
        VPU_E_BIT | VPU_ADD,
        VPU_LOWER_NOP);
      appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

      vpu.uploadMicroInstructions(instructions);
      REQUIRE_NOTHROW(vpu.initMicroMode());
    }

    SECTION("Microinstruction programs execute synchronously by default")
    {
      std::vector<uint8_t> instructions;
      appendInstructionPair(
        &instructions,
        VPU_E_BIT | VPU_NOP,
        VPU_LOWER_NOP);
      appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

      vpu.uploadMicroInstructions(instructions);
      vpu.initMicroMode();

      REQUIRE(vpu.getState() != VPU_STATE_RUN);
      REQUIRE(vpu.elapsedCycles() > 0);
    }

    SECTION("Unsupported lower instructions are rejected rather than silently ignored")
    {
      std::vector<uint8_t> instructions;
      appendInstructionPair(
        &instructions,
        VPU_E_BIT | VPU_NOP,
        0x04000000);

      vpu.uploadMicroInstructions(instructions);
      REQUIRE_THROWS_WITH(vpu.initMicroMode(), "Unsupported VU lower instruction.");
      REQUIRE(vpu.getState() == VPU_STATE_STOP);
    }

    SECTION("Unsupported upper instructions are rejected rather than entering the pipeline")
    {
      std::vector<uint8_t> instructions;
      appendInstructionPair(
        &instructions,
        VPU_E_BIT | 0x30,
        VPU_LOWER_NOP);

      vpu.uploadMicroInstructions(instructions);
      REQUIRE_THROWS_WITH(vpu.initMicroMode(), "Unsupported VU upper instruction.");
      REQUIRE(vpu.getState() == VPU_STATE_STOP);
    }

    SECTION("Reserved upper fields are rejected for every fixed-field family")
    {
      const std::uint32_t nonCanonicalInstructions[] = {
        VPU_NOP | VPU_DEST_X_BIT,
        VPU_ADD | VPU_UPPER_RESERVED_BITS_MASK,
        VPU_ADDi | (1u << VPU_FT_REG_SHIFT),
        VPU_ADDq | (1u << VPU_FT_REG_SHIFT),
        VPU_ADDAi | (1u << VPU_FT_REG_SHIFT),
        VPU_ADDAq | (1u << VPU_FT_REG_SHIFT),
        VPU_MADDi | (1u << VPU_FT_REG_SHIFT),
        VPU_MADDq | (1u << VPU_FT_REG_SHIFT),
        VPU_MADDAi | (1u << VPU_FT_REG_SHIFT),
        VPU_MADDAq | (1u << VPU_FT_REG_SHIFT),
        VPU_MAXi | (1u << VPU_FT_REG_SHIFT),
        VPU_MINIi | (1u << VPU_FT_REG_SHIFT),
        VPU_MSUBi | (1u << VPU_FT_REG_SHIFT),
        VPU_MSUBq | (1u << VPU_FT_REG_SHIFT),
        VPU_MSUBAi | (1u << VPU_FT_REG_SHIFT),
        VPU_MSUBAq | (1u << VPU_FT_REG_SHIFT),
        VPU_MULi | (1u << VPU_FT_REG_SHIFT),
        VPU_MULq | (1u << VPU_FT_REG_SHIFT),
        VPU_MULAi | (1u << VPU_FT_REG_SHIFT),
        VPU_MULAq | (1u << VPU_FT_REG_SHIFT),
        VPU_SUBi | (1u << VPU_FT_REG_SHIFT),
        VPU_SUBq | (1u << VPU_FT_REG_SHIFT),
        VPU_SUBAi | (1u << VPU_FT_REG_SHIFT),
        VPU_SUBAq | (1u << VPU_FT_REG_SHIFT),
        VPU_CLIP | VPU_DEST_ALL_FIELDS,
        VPU_OPMULA | VPU_DEST_ALL_FIELDS,
        VPU_OPMSUB | VPU_DEST_ALL_FIELDS
      };

      for (const std::uint32_t upper : nonCanonicalInstructions)
      {
        CAPTURE(upper);
        VPU rejected;
        std::vector<uint8_t> instructions;
        appendInstructionPair(
          &instructions,
          VPU_E_BIT | upper,
          VPU_LOWER_NOP);
        rejected.uploadMicroInstructions(instructions);

        REQUIRE_THROWS_WITH(
          rejected.initMicroMode(),
          "Unsupported VU upper instruction.");
        REQUIRE(rejected.getState() == VPU_STATE_STOP);
      }
    }
}
