#include "catch.hpp"
#include "floating_point_ops.hpp"
#include "vpu.hpp"
#include "vpu_flags.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"
#include "vpu_upper_instruction_utils.hpp"

TEST_CASE("VPU Microinstruction MAX and MIN Tests")
{
  VPU vpu;
  vpu.useThreads = false;
  vpu.loadFPRegister(VPU_REGISTER_VF02, 1, 1, 1, 1);
  vpu.loadFPRegister(VPU_REGISTER_VF03, -5.0f, -2.5f, 11.0f, 4.5f);
  vpu.loadFPRegister(VPU_REGISTER_VF04, 5.0f, -6.5f, 10.0f, -9.0f);
  vpu.loadFPRegister(VPU_REGISTER_VF05, 100, 51, -1, 149);
  vpu.loadFPRegister(VPU_REGISTER_VF06, 99, 51, 0, 59);
  vpu.loadFPRegister(VPU_REGISTER_VF08, 25, 25, 25, 25);
  vector<uint8_t> instructions;

  SECTION("MAX stores the max value between the fields of the fs and ft vectors in the fd vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Z_BIT, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF08, VPU_MAX);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->x == 5.0f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->y == 25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->z == 11.0f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->w == 25);
  }

  SECTION("MAXi stores the max value between the fields of the fs vector and the I register in the fd vector")
  {
    vpu.loadIRegister(50);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_Y_BIT | VPU_DEST_Z_BIT | VPU_DEST_W_BIT, 0, VPU_REGISTER_VF05, VPU_REGISTER_VF08, VPU_MAXi);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->x == 25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->y == 51);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->z == 50);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->w == 149);
  }

  SECTION("MAXx stores the max value between the fields of the fs vector and the x field of the ft register in the fd vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Z_BIT | VPU_DEST_W_BIT, VPU_REGISTER_VF06, VPU_REGISTER_VF05, VPU_REGISTER_VF08, VPU_MAXx);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->x == 100);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->y == 25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->z == 99);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->w == 149);
  }

  SECTION("MAXy stores the max value between the fields of the fs vector and the y field of the ft register in the fd vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Y_BIT | VPU_DEST_Z_BIT, VPU_REGISTER_VF06, VPU_REGISTER_VF05, VPU_REGISTER_VF08, VPU_MAXy);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->x == 100);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->y == 51);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->z == 51);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->w == 25);
  }

  SECTION("MAXz stores the max value between the fields of the fs vector and the z field of the ft register in the fd vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF06, VPU_REGISTER_VF05, VPU_REGISTER_VF08, VPU_MAXz);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->x == 100);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->y == 51);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->z == 0);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->w == 149);
  }

  SECTION("MAXw stores the max value between the fields of the fs vector and the w field of the ft register in the fd vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF06, VPU_REGISTER_VF05, VPU_REGISTER_VF08, VPU_MAXw);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->x == 100);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->y == 59);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->z == 59);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->w == 149);
  }

  SECTION("MINI stores the min value between the fields of the fs and ft vectors in the fd vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Z_BIT, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF08, VPU_MINI);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->x == -5.0f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->y == 25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->z == 10.0f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->w == 25);
  }

  SECTION("MINIi stores the min value between the fields of the fs vector and the I register in the fd vector")
  {
    vpu.loadIRegister(50);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_Y_BIT | VPU_DEST_Z_BIT | VPU_DEST_W_BIT, 0, VPU_REGISTER_VF05, VPU_REGISTER_VF08, VPU_MINIi);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->x == 25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->y == 50);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->z == -1);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->w == 50);
  }

  SECTION("MINx stores the min value between the fields of the fs vector and the x field of the ft register in the fd vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Z_BIT | VPU_DEST_W_BIT, VPU_REGISTER_VF06, VPU_REGISTER_VF05, VPU_REGISTER_VF08, VPU_MINIx);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->x == 99);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->y == 25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->z == -1);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->w == 99);
  }

  SECTION("MINIy stores the min value between the fields of the fs vector and the y field of the ft register in the fd vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Y_BIT | VPU_DEST_Z_BIT, VPU_REGISTER_VF06, VPU_REGISTER_VF05, VPU_REGISTER_VF08, VPU_MINIy);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->x == 51);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->y == 51);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->z == -1);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->w == 25);
  }

  SECTION("MINIz stores the min value between the fields of the fs vector and the z field of the ft register in the fd vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF06, VPU_REGISTER_VF05, VPU_REGISTER_VF08, VPU_MINIz);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->x == 0);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->y == 0);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->z == -1);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->w == 0);
  }

  SECTION("MINIw stores the min value between the fields of the fs vector and the w field of the ft register in the fd vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF06, VPU_REGISTER_VF05, VPU_REGISTER_VF08, VPU_MINIw);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->x == 59);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->y == 51);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->z == -1);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF08)->w == 59);
  }
}
