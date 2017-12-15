#include "catch.hpp"
#include "bit_ops.hpp"
#include "vpu.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"
#include "vpu_upper_instruction_utils.hpp"

TEST_CASE("VPU Clipping Microinstruction Tests")
{
  VPU vpu;
  vpu.useThreads = false;
  vpu.loadFPRegister(VPU_REGISTER_VF02, 15.0f, 2.4f, 3.0f, 14.5f);
  vpu.loadFPRegister(VPU_REGISTER_VF03, -25.0f, -16.4f, -100.0f, -2.0f);
  vector<uint8_t> instructions;

  SECTION("If the first vector's x field is greater than the abs of the second vector's w field, the +x clipping flag is set")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Y_BIT | VPU_DEST_Z_BIT, VPU_REGISTER_VF02, VPU_REGISTER_VF03, 0, VPU_CLIP);
    REQUIRE(hasFlag(vpu.clippingFlags, VPU_CLIP_FLAG_POS_X));
  }

  SECTION("If the first vector's x field is less than the negative abs of the second vector's w field, the -x clipping flag is set")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Y_BIT | VPU_DEST_Z_BIT, VPU_REGISTER_VF03, VPU_REGISTER_VF02, 0, VPU_CLIP);
    REQUIRE(hasFlag(vpu.clippingFlags, VPU_CLIP_FLAG_NEG_X));
  }

  SECTION("If the first vector's y field is greater than the abs of the second vector's w field, the +y clipping flag is set")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Y_BIT | VPU_DEST_Z_BIT, VPU_REGISTER_VF02, VPU_REGISTER_VF03, 0, VPU_CLIP);
    REQUIRE(hasFlag(vpu.clippingFlags, VPU_CLIP_FLAG_POS_Y));
  }

  SECTION("If the first vector's y field is less than the negative abs of the second vector's w field, the -y clipping flag is set")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Y_BIT | VPU_DEST_Z_BIT, VPU_REGISTER_VF03, VPU_REGISTER_VF02, 0, VPU_CLIP);
    REQUIRE(hasFlag(vpu.clippingFlags, VPU_CLIP_FLAG_NEG_Y));
  }

  SECTION("If the first vector's z field is greater than the abs of the second vector's w field, the +z clipping flag is set")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Y_BIT | VPU_DEST_Z_BIT, VPU_REGISTER_VF02, VPU_REGISTER_VF03, 0, VPU_CLIP);
    REQUIRE(hasFlag(vpu.clippingFlags, VPU_CLIP_FLAG_POS_Z));
  }

  SECTION("If the first vector's z field is less than the negative abs of the second vector's w field, the -z clipping flag is set")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Y_BIT | VPU_DEST_Z_BIT, VPU_REGISTER_VF03, VPU_REGISTER_VF02, 0, VPU_CLIP);
    REQUIRE(hasFlag(vpu.clippingFlags, VPU_CLIP_FLAG_NEG_Z));
  }

  SECTION("The clippingFlags are shifted 6 places after each update")
  {
    addSingleUpperInstruction(&instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Y_BIT | VPU_DEST_Z_BIT, VPU_REGISTER_VF03, VPU_REGISTER_VF02, 0, VPU_CLIP);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Y_BIT | VPU_DEST_Z_BIT, VPU_REGISTER_VF03, VPU_REGISTER_VF02, 0, VPU_CLIP);

    REQUIRE(((vpu.clippingFlags >> 6) & VPU_CLIP_MASK) == (vpu.clippingFlags & VPU_CLIP_MASK));
  }
}
