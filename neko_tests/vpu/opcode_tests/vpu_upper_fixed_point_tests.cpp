#include "catch.hpp"
#include "vpu.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"
#include "vpu_upper_instruction_utils.hpp"

TEST_CASE("VPU Microinstruction Fixed Point Tests")
{
  VPU vpu;
  vpu.useThreads = false;
  vpu.loadFPRegister(VPU_REGISTER_VF02, -5.2f, 1.2535f, 0.0042f, -10.10f);
  vpu.loadFPRegister(VPU_REGISTER_VF03, 5.0f, -6.4f, 10.0f, -9.0f);
  vpu.loadFPRegister(VPU_REGISTER_VF04, -0.45f, 0.45f, 0.55f, 123.45f);
  vpu.loadFPRegister(VPU_REGISTER_VF05, 10, 0.45f, 0.55f, 3);
  vpu.loadFPRegister(VPU_REGISTER_VF06, 10, 5.5f, 10, -6.6f);
  vpu.fpRegisterValue(VPU_REGISTER_VF07)->xInt = -12;
  vpu.fpRegisterValue(VPU_REGISTER_VF07)->yInt = 1;
  vpu.fpRegisterValue(VPU_REGISTER_VF07)->zInt = 123;
  vpu.fpRegisterValue(VPU_REGISTER_VF07)->wInt = 1843;
  vector<uint8_t> instructions;

  SECTION("FTOI0 converts floating point fields of the second source vector to fixed point and stores the result in the fields of the first vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF03, VPU_REGISTER_VF02, 0, VPU_FTOI0);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->xInt == -5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->yInt == 1);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->zInt == 0);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->wInt == -10);
  }

  SECTION("FTOI4 converts floating point fields of the second source vector to fixed point and stores the result in the fields of the first vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF03, VPU_REGISTER_VF04, 0, VPU_FTOI4);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->xInt == -7);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->yInt == 7);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->zInt == 8);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->wInt == 1975);
  }

  SECTION("FTOI12 converts floating point fields of the second source vector to fixed point and stores the result in the fields of the first vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF03, VPU_REGISTER_VF04, 0, VPU_FTOI12);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->xInt == -1843);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->yInt == 1843);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->zInt == 2252);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->wInt == 505651);
  }

  SECTION("FTOI15 converts floating point fields of the second source vector to fixed point and stores the result in the fields of the first vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_Y_BIT | VPU_DEST_Z_BIT, VPU_REGISTER_VF05, VPU_REGISTER_VF04, 0, VPU_FTOI15);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF05)->xInt == 1092616192);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF05)->yInt == 14745);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF05)->zInt == 18022);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF05)->wInt == 1077936128);
  }

  SECTION("ITOF0 converts fixed point fields of the second source vector to floating point and stores the result in the fields of the first vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_W_BIT, VPU_REGISTER_VF06, VPU_REGISTER_VF07, 0, VPU_ITOF0);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF06)->x == -12.0f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF06)->y == 5.5f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF06)->z == 10);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF06)->w == 1843.0f);
  }

  SECTION("ITOF4 converts fixed point fields of the second source vector to floating point and stores the result in the fields of the first vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_Y_BIT | VPU_DEST_W_BIT, VPU_REGISTER_VF06, VPU_REGISTER_VF07, 0, VPU_ITOF4);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF06)->x == 10);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF06)->y == 0.0625f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF06)->z == 10);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF06)->w == 115.1875f);
  }

  SECTION("ITOF12 converts fixed point fields of the second source vector to floating point and stores the result in the fields of the first vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Y_BIT | VPU_DEST_Z_BIT, VPU_REGISTER_VF06, VPU_REGISTER_VF07, 0, VPU_ITOF12);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF06)->x == -0.0029296875f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF06)->y == 0.000244140625f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF06)->z == 0.030029296875f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF06)->w == -6.6f);
  }

  SECTION("ITOF15 converts fixed point fields of the second source vector to floating point and stores the result in the fields of the first vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF06, VPU_REGISTER_VF07, 0, VPU_ITOF15);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF06)->x == -0.0003662109375f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF06)->y == 0.000030517578125f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF06)->z == 0.003753662109375f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF06)->w == 0.056243896484375f);
  }
}
