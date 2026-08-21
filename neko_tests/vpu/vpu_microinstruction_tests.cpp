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
    
    SECTION("Microinstruction programs can be started by specifying the execution address in the VIFcode MSCAL")
    {
      //WARN("Add this test");
    }

    SECTION("Microinstruction programs can be started by specifying the execution address in the VIFcode MSCALF")
    {
      //WARN("Add this test");
    }

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
}
