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
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF03, VPU_REGISTER_VF04, 0, VPU_FTOI15);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->xInt == -14745);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->yInt == 14745);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->zInt == 18022);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->wInt == 4045209);
  }
}
