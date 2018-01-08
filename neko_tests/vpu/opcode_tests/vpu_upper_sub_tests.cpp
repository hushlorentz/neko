#include "catch.hpp"
#include "vpu.hpp"
#include "vpu_flags.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"
#include "vpu_upper_instruction_utils.hpp"

TEST_CASE("VPU Microinstruction SUB Tests")
{
  VPU vpu;
  vpu.useThreads = false;
  vpu.loadFPRegister(VPU_REGISTER_VF03, -5.0f, 49, -1.0f, 4.5f);
  vpu.loadFPRegister(VPU_REGISTER_VF05, 5.0f, -6.5f, 10.0f, -9.0f);
  vpu.loadFPRegister(VPU_REGISTER_VF06, -5.0f, 6.5f, -10.0f, 9.0f);
  vpu.loadFPRegister(VPU_REGISTER_VF07, -5.0f, 6.5f, -10.0f, 9.0f);
  vpu.loadFPRegister(VPU_REGISTER_VF08, -4.5f, 5, -2.75f, 4);
  vpu.loadFPRegister(VPU_REGISTER_VF09, 5.5f, -4, 3.75f, -3);
  vpu.loadFPRegister(VPU_REGISTER_VF10, -2.5f, 2.5f, 10.0f, 9.0f);
  vpu.loadFPRegister(VPU_REGISTER_VF11, -5, 2.4f, 10.0f, 12.5f);
  vpu.loadFPRegister(VPU_REGISTER_VF12, 60, 65, 70, 75);
  vector<uint8_t> instructions;

  SECTION("SUB stores the subtraction of the ft vector from the fs vector in the fd vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF10, VPU_REGISTER_VF03, VPU_REGISTER_VF04, VPU_SUB);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->x == -2.5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->y == 46.5); 
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->z == -11);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->w == -4.5);
    REQUIRE(vpu.elapsedCycles() == 7);
  }

  SECTION("SUBing vectors of equal direction and equal magnitude sets the zero flags.")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF06, VPU_REGISTER_VF07, VPU_REGISTER_VF15, VPU_SUB);
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZX));
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZY));
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZZ));
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZW));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_Z));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_ZS));
  }

  SECTION("SUBing vectors sets the sign flags")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF10, VPU_REGISTER_VF03, VPU_REGISTER_VF06, VPU_ADD);
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_SX));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_SY));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_SZ));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_SW));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_S));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_SS));
  }

  SECTION("SUBi stores the subtraction of the iRegister and the src vector in dest vector")
  {
    vpu.loadIRegister(-4.5f);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, 0, VPU_REGISTER_VF03, VPU_REGISTER_VF20, VPU_SUBi);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF20)->x == -0.5f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF20)->y == 53.5f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF20)->z == 3.5f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF20)->w == 9);
  }

  SECTION("SUBq stores the subtraction of the qRegister and the src vector in dest vector")
  {
    vpu.loadQRegister(5);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Z_BIT | VPU_DEST_W_BIT, 0, VPU_REGISTER_VF05, VPU_REGISTER_VF21, VPU_SUBq);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF21)->x == 0);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF21)->y == 0);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF21)->z == 5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF21)->w == -14);
  }

  SECTION("SUBx stores the subtraction of the x field of the first src vector to each field of the second src vector in the dest vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_Y_BIT | VPU_DEST_W_BIT, VPU_REGISTER_VF06, VPU_REGISTER_VF05, VPU_REGISTER_VF07, VPU_SUBx);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->x == -5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->y == -1.5f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->z == -10);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->w == -4);
  }

  SECTION("SUBy stores the subtraction of the y field of the first src vector to each field of the second src vector in the dest vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Z_BIT, VPU_REGISTER_VF06, VPU_REGISTER_VF05, VPU_REGISTER_VF07, VPU_SUBy);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->x == -1.5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->y == 6.5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->z == 3.5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->w == 9.0);
  }

  SECTION("SUBz stores the subtraction of the z field of the first src vector to each field of the second src vector in the dest vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_Y_BIT | VPU_DEST_Z_BIT | VPU_DEST_W_BIT, VPU_REGISTER_VF06, VPU_REGISTER_VF05, VPU_REGISTER_VF07, VPU_SUBz);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->x == -5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->y == 3.5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->z == 20);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->w == 1);
  }

  SECTION("SUBw stores the subtraction of the w field of the first src vector to each field of the second src vector in the dest vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_W_BIT, VPU_REGISTER_VF11, VPU_REGISTER_VF05, VPU_REGISTER_VF07, VPU_SUBw);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->x == -7.5f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->y == 6.5f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->z == -10);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->w == -21.5);
  }

  SECTION("SUBA stores the subtraction of the first src vector to the second src vector in the accumulator")
  {
    vpu.loadAccumulator(100, 100, 100, 100);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF10, VPU_REGISTER_VF03, 0, VPU_SUBA);

    REQUIRE(vpu.accumulator.x == -2.5);
    REQUIRE(vpu.accumulator.y == 46.5); 
    REQUIRE(vpu.accumulator.z == -11);
    REQUIRE(vpu.accumulator.w == -4.5);
  }

  SECTION("SUBAi stores the subtraction of the iRegister and the src vector in the accumulator")
  {
    vpu.loadIRegister(-4.5f);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, 0, VPU_REGISTER_VF03, 0, VPU_SUBAi);

    REQUIRE(vpu.accumulator.x == -0.5f);
    REQUIRE(vpu.accumulator.y == 53.5f);
    REQUIRE(vpu.accumulator.z == 3.5f);
    REQUIRE(vpu.accumulator.w == 9);
  }

  SECTION("SUBAq stores the subtraction of the qRegister and the src vector in the accumulator")
  {
    vpu.loadQRegister(5);
    vpu.loadAccumulator(2.5, 2.5, 2.5, 2.5);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Z_BIT | VPU_DEST_W_BIT, 0, VPU_REGISTER_VF05, 0, VPU_SUBAq);

    REQUIRE(vpu.accumulator.x == 0);
    REQUIRE(vpu.accumulator.y == 2.5);
    REQUIRE(vpu.accumulator.z == 5);
    REQUIRE(vpu.accumulator.w == -14);
  }

  SECTION("SUBAx stores the subtraction of the x field of the first src vector to the specified fields of the second src vector in the accumulator")
  {
    vpu.loadAccumulator(30, 30, 30, 30);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_Y_BIT | VPU_DEST_W_BIT, VPU_REGISTER_VF06, VPU_REGISTER_VF05, 0, VPU_SUBAx);

    REQUIRE(vpu.accumulator.x == 30);
    REQUIRE(vpu.accumulator.y == -1.5f);
    REQUIRE(vpu.accumulator.z == 30);
    REQUIRE(vpu.accumulator.w == -4);
  }

  SECTION("SUBAy stores the subtraction of the y field of the first src vector to the specified fields of the second src vector in the accumulator")
  {
    vpu.loadAccumulator(5, 3, 2, 6);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Z_BIT, VPU_REGISTER_VF06, VPU_REGISTER_VF05, 0, VPU_SUBAy);

    REQUIRE(vpu.accumulator.x == -1.5);
    REQUIRE(vpu.accumulator.y == 3);
    REQUIRE(vpu.accumulator.z == 3.5);
    REQUIRE(vpu.accumulator.w == 6);
  }

  SECTION("SUBAz stores the subtraction of the z field of the first src vector to the specified fields of the second src vector in the accumulator")
  {
    vpu.loadAccumulator(19, 20, 21, 22);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_Y_BIT | VPU_DEST_Z_BIT | VPU_DEST_W_BIT, VPU_REGISTER_VF06, VPU_REGISTER_VF05, 0, VPU_SUBAz);

    REQUIRE(vpu.accumulator.x == 19);
    REQUIRE(vpu.accumulator.y == 3.5);
    REQUIRE(vpu.accumulator.z == 20);
    REQUIRE(vpu.accumulator.w == 1);
  }

  SECTION("SUBAw stores the subtraction of the w field of the first src vector to the specified fields of the second src vector in the accumulator")
  {
    vpu.loadAccumulator(1.5f, 1.5f, 1.5f, 1.5f);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_W_BIT, VPU_REGISTER_VF11, VPU_REGISTER_VF05, 0, VPU_SUBAw);

    REQUIRE(vpu.accumulator.x == -7.5f);
    REQUIRE(vpu.accumulator.y == 1.5f);
    REQUIRE(vpu.accumulator.z == 1.5f);
    REQUIRE(vpu.accumulator.w == -21.5);
  }
}
