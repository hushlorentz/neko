#include "catch.hpp"
#include "vpu.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"

namespace
{
  void appendInstruction(std::vector<uint8_t> *instructions, uint32_t instruction)
  {
    instructions->push_back((instruction >> 24) & 0xff);
    instructions->push_back((instruction >> 16) & 0xff);
    instructions->push_back((instruction >> 8) & 0xff);
    instructions->push_back(instruction & 0xff);
  }

  void runTerminatingInstruction(VPU *vpu, uint32_t terminationBit)
  {
    std::vector<uint8_t> instructions;
    appendInstruction(&instructions, terminationBit | VPU_NOP);
    appendInstruction(&instructions, VPU_LOWER_NOP);
    appendInstruction(&instructions, VPU_NOP);
    appendInstruction(&instructions, VPU_LOWER_NOP);
    vpu->uploadMicroInstructions(instructions);
    vpu->initMicroMode();
  }

  void runUpperInstruction(VPU *vpu, uint32_t destinationMask, uint8_t ft, uint8_t fs, uint8_t fd, uint16_t opCode)
  {
    uint32_t instruction =
      VPU_E_BIT |
      destinationMask |
      (ft << VPU_FT_REG_SHIFT) |
      (fs << VPU_FS_REG_SHIFT) |
      (fd << VPU_FD_REG_SHIFT) |
      opCode;

    std::vector<uint8_t> instructions;
    appendInstruction(&instructions, instruction);
    appendInstruction(&instructions, VPU_LOWER_NOP);
    appendInstruction(&instructions, VPU_NOP);
    appendInstruction(&instructions, VPU_LOWER_NOP);
    vpu->uploadMicroInstructions(instructions);
    vpu->initMicroMode();
  }
}

TEST_CASE("VPU State Tests")
{
    VPU vpu;

    SECTION("VF00 always returns <0,0,0,1>")
    {
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF00)->x == 0.0f);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF00)->y == 0.0f);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF00)->z == 0.0f);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF00)->w == 1.0f);
    }

    SECTION("VI00 always returns 0")
    {
      REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI00) == 0);
    }

    SECTION("Host loads cannot modify VF00")
    {
      vpu.loadFPRegister(VPU_REGISTER_VF00, 1, 2, 3, 4);
      vpu.loadIntFPRegister(VPU_REGISTER_VF00, 1, 2, 3, 4);

      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF00)->x == 0);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF00)->y == 0);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF00)->z == 0);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF00)->w == 1);
    }

    SECTION("Instruction writeback cannot modify VF00")
    {
      vpu.loadFPRegister(VPU_REGISTER_VF01, 1, 2, 3, 4);
      vpu.loadFPRegister(VPU_REGISTER_VF02, 10, 20, 30, 40);

      runUpperInstruction(
        &vpu,
        VPU_DEST_ALL_FIELDS,
        VPU_REGISTER_VF01,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF00,
        VPU_ADD);

      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF00)->x == 0);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF00)->y == 0);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF00)->z == 0);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF00)->w == 1);
    }

    SECTION("Host loads cannot modify VI00")
    {
      vpu.loadIntRegister(VPU_REGISTER_VI00, 123);

      REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI00) == 0);
    }
    
    SECTION("VPU starts in the Ready state ")
    {
      REQUIRE(vpu.getState() == VPU_STATE_READY);
    }

    SECTION("VPU transitions to Ready state on reset")
    {
      //WARN("Add this test");
    }

    SECTION("VPU control register is initialised on reset")
    {
      //WARN("Add this test");
    }

    SECTION("VPU transitions from Ready to Run when a micro subroutine is started")
    {
      //WARN("Add this test");
    }

    SECTION("VPU transitions from Ready to Run when a macro instruction is executed")
    {
      //WARN("Add this test");
    }

    SECTION("VPU transitions from Ready to Stop when a ForceBreak occurs")
    {
      //WARN("Add this test");
    }

    SECTION("VPU cannot receive micro subroutine startup from the EE core while in Ready state")
    {
      //WARN("Add this test");
    }

    SECTION("VPU cannot receive macro instructions from the EE core in Ready state")
    {
      //WARN("Add this test");
    }

    SECTION("VPU transitions from Run to Ready at micro subroutine E bit termination")
    {
      runTerminatingInstruction(&vpu, VPU_E_BIT);

      REQUIRE(vpu.getState() == VPU_STATE_READY);
    }

    SECTION("The instruction pair after E executes before termination")
    {
      std::vector<uint8_t> instructions;
      uint32_t delayInstruction =
        VPU_DEST_ALL_FIELDS |
        (VPU_REGISTER_VF01 << VPU_FT_REG_SHIFT) |
        (VPU_REGISTER_VF02 << VPU_FS_REG_SHIFT) |
        (VPU_REGISTER_VF03 << VPU_FD_REG_SHIFT) |
        VPU_ADD;
      appendInstruction(&instructions, VPU_E_BIT | VPU_NOP);
      appendInstruction(&instructions, VPU_LOWER_NOP);
      appendInstruction(&instructions, delayInstruction);
      appendInstruction(&instructions, VPU_LOWER_NOP);
      vpu.loadFPRegister(VPU_REGISTER_VF01, 1, 2, 3, 4);
      vpu.loadFPRegister(VPU_REGISTER_VF02, 10, 20, 30, 40);
      vpu.uploadMicroInstructions(instructions);

      vpu.initMicroMode();

      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->x == 11);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->y == 22);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->z == 33);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->w == 44);
      REQUIRE(vpu.programCounter() == 16);
      REQUIRE(vpu.getState() == VPU_STATE_READY);
    }

    SECTION("Instructions after the E delay slot are not fetched")
    {
      std::vector<uint8_t> instructions;
      appendInstruction(&instructions, VPU_E_BIT | VPU_NOP);
      appendInstruction(&instructions, VPU_LOWER_NOP);
      appendInstruction(&instructions, VPU_NOP);
      appendInstruction(&instructions, VPU_LOWER_NOP);
      appendInstruction(&instructions, 0x30);
      appendInstruction(&instructions, VPU_LOWER_NOP);
      vpu.uploadMicroInstructions(instructions);

      REQUIRE_NOTHROW(vpu.initMicroMode());
      REQUIRE(vpu.programCounter() == 16);
      REQUIRE(vpu.getState() == VPU_STATE_READY);
    }

    SECTION("A dependent E delay-slot instruction completes after its stall")
    {
      std::vector<uint8_t> instructions;
      uint32_t terminatingInstruction =
        VPU_E_BIT |
        VPU_DEST_ALL_FIELDS |
        (VPU_REGISTER_VF01 << VPU_FT_REG_SHIFT) |
        (VPU_REGISTER_VF02 << VPU_FS_REG_SHIFT) |
        (VPU_REGISTER_VF03 << VPU_FD_REG_SHIFT) |
        VPU_ADD;
      uint32_t delayInstruction =
        VPU_DEST_ALL_FIELDS |
        (VPU_REGISTER_VF03 << VPU_FT_REG_SHIFT) |
        (VPU_REGISTER_VF04 << VPU_FS_REG_SHIFT) |
        (VPU_REGISTER_VF05 << VPU_FD_REG_SHIFT) |
        VPU_ADD;
      appendInstruction(&instructions, terminatingInstruction);
      appendInstruction(&instructions, VPU_LOWER_NOP);
      appendInstruction(&instructions, delayInstruction);
      appendInstruction(&instructions, VPU_LOWER_NOP);
      vpu.loadFPRegister(VPU_REGISTER_VF01, 1, 2, 3, 4);
      vpu.loadFPRegister(VPU_REGISTER_VF02, 10, 20, 30, 40);
      vpu.loadFPRegister(VPU_REGISTER_VF04, 100, 200, 300, 400);
      vpu.uploadMicroInstructions(instructions);

      vpu.initMicroMode();

      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF05)->x == 111);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF05)->y == 222);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF05)->z == 333);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF05)->w == 444);
      REQUIRE(vpu.getState() == VPU_STATE_READY);
    }

    SECTION("E cannot be set again in the E delay slot")
    {
      std::vector<uint8_t> instructions;
      appendInstruction(&instructions, VPU_E_BIT | VPU_NOP);
      appendInstruction(&instructions, VPU_LOWER_NOP);
      appendInstruction(&instructions, VPU_E_BIT | VPU_NOP);
      appendInstruction(&instructions, VPU_LOWER_NOP);
      vpu.uploadMicroInstructions(instructions);

      REQUIRE_THROWS_WITH(
        vpu.initMicroMode(),
        "E bit cannot be set in an E-bit delay slot.");
      REQUIRE(vpu.getState() == VPU_STATE_STOP);
    }

    SECTION("VPU transitions from Run to Ready at macro instruction execution termination")
    {
      //WARN("Add this test");
    }

    SECTION("VPU transitions from Run to Stop when a D bit halt occurs")
    {
      runTerminatingInstruction(&vpu, VPU_D_BIT);

      REQUIRE(vpu.getState() == VPU_STATE_STOP);
    }

    SECTION("D-bit termination does not execute an E-style delay slot")
    {
      std::vector<uint8_t> instructions;
      appendInstruction(&instructions, VPU_D_BIT | VPU_NOP);
      appendInstruction(&instructions, VPU_LOWER_NOP);
      appendInstruction(&instructions, 0x30);
      appendInstruction(&instructions, VPU_LOWER_NOP);
      vpu.uploadMicroInstructions(instructions);

      REQUIRE_NOTHROW(vpu.initMicroMode());
      REQUIRE(vpu.programCounter() == 8);
      REQUIRE(vpu.getState() == VPU_STATE_STOP);
    }

    SECTION("VPU transitions from Run to Stop when a T bit halt occurs")
    {
      runTerminatingInstruction(&vpu, VPU_T_BIT);

      REQUIRE(vpu.getState() == VPU_STATE_STOP);
    }

    SECTION("A halt bit takes priority when E and D are set together")
    {
      runTerminatingInstruction(&vpu, VPU_E_BIT | VPU_D_BIT);

      REQUIRE(vpu.getState() == VPU_STATE_STOP);
    }

    SECTION("VPU transitions from Run to Stop when a ForceBreak occurs")
    {
      //WARN("Add this test");
    }

    SECTION("VPU cannot receive micro program startup from the VIF while in Stop state")
    {
      //WARN("Add this test");
    }

    SECTION("VPU transitions from Stop to Run when the VCALLMS instruction is executed")
    {
      //WARN("Add this test");
    }

    SECTION("VPU transitions from Stop to Run when the VCALLMSR instruction is executed")
    {
      //WARN("Add this test");
    }

    SECTION("VPU transitions from Stop to Run when the CMSAR1 register is written to")
    {
      //WARN("Add this test");
    }
}
