#include "catch.hpp"
#include "vpu.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"
#include "vpu_upper_instruction_utils.hpp"

TEST_CASE("VPU Upper Microinstruction Stall Tests")
{
  VPU vpu;
  vpu.useThreads = false;
  vpu.loadFPRegister(VPU_REGISTER_VF03, -5.0f, -2.4f, -1.0f, 4.5f);
  vpu.loadFPRegister(VPU_REGISTER_VF05, 5.0f, -6.4f, 10.0f, -9.0f);
  vector<uint8_t> instructions;

  SECTION("When an ABS reads from the destination of the previous instruction, a stall occurs")
  {
    addSingleUpperInstruction(&instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, 0, VPU_ABS);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF05, VPU_REGISTER_VF04, 0, VPU_ABS);

    REQUIRE(vpu.elapsedCycles() == 11);
    REQUIRE(vpu.getState() == VPU_STATE_STOP);
  }

  SECTION("ABS three stalls")
  {
    addSingleUpperInstruction(&instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, 0, VPU_ABS);
    addSingleUpperInstruction(&instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF05, VPU_REGISTER_VF04, 0, VPU_ABS);
    addSingleUpperInstruction(&instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF06, VPU_REGISTER_VF05, 0, VPU_ABS);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF07, VPU_REGISTER_VF06, 0, VPU_ABS);

    REQUIRE(vpu.elapsedCycles() == 19);
  }

  SECTION("ADD after ABS causes a stall")
  {
    addSingleUpperInstruction(&instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF06, VPU_REGISTER_VF05, 0, VPU_ABS);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT, VPU_REGISTER_VF09, VPU_REGISTER_VF06, VPU_REGISTER_VF04, VPU_ADD);
    REQUIRE(vpu.elapsedCycles() == 11);
  }

  SECTION("ADDx after ADD causes a stall")
  {
    addSingleUpperInstruction(&instructions, 0, VPU_DEST_X_BIT, VPU_REGISTER_VF06, VPU_REGISTER_VF05, VPU_REGISTER_VF07, VPU_ADD);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_Y_BIT, VPU_REGISTER_VF08, VPU_REGISTER_VF07, VPU_REGISTER_VF04, VPU_ADDx);
    REQUIRE(vpu.elapsedCycles() == 11);
  }
}
