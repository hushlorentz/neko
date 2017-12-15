#include "catch.hpp"
#include "vpu.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"
#include "vpu_upper_instruction_utils.hpp"

TEST_CASE("VPU Microinstruction Operation Tests")
{
  VPU vpu;
  vpu.useThreads = false;
  vpu.loadFPRegister(VPU_REGISTER_VF03, -5.0f, -2.4f, -1.0f, 4.5f);
  vpu.loadFPRegister(VPU_REGISTER_VF05, 5.0f, -6.4f, 10.0f, -9.0f);
  vpu.loadFPRegister(VPU_REGISTER_VF06, -5.0f, 6.4f, -10.0f, 9.0f);
  vpu.loadFPRegister(VPU_REGISTER_VF07, 10, 10, 10, 10);
  vpu.loadFPRegister(VPU_REGISTER_VF08, -4.5f, 5, -2.75f, 4);
  vpu.loadFPRegister(VPU_REGISTER_VF09, 5.5f, -4, 3.75f, -3);
  vpu.loadFPRegister(VPU_REGISTER_VF10, -2.5f, 2.4f, 10.0f, 9.0f);
  vpu.loadFPRegister(VPU_REGISTER_VF11, -5, 2.4f, 10.0f, 12.5f);
  vpu.loadFPRegister(VPU_REGISTER_VF12, 60, 65, 70, 75);
  vector<uint8_t> instructions;

  SECTION("ABS stores the absolute value of src vector in dest vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, 0, VPU_ABS);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->x == 5.0f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->y == 2.4f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->z == 1.0f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->w == 4.5f);
    REQUIRE(vpu.elapsedCycles() == 7);
    REQUIRE(vpu.getState() == VPU_STATE_STOP);
  }

  SECTION("ADD stores the addition of the two src vectors in dest vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF10, VPU_REGISTER_VF03, VPU_REGISTER_VF04, VPU_ADD);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->x == -7.5f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->y == 0);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->z == 9);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->w == 13.5f);
    REQUIRE(vpu.elapsedCycles() == 7);
    REQUIRE(vpu.getState() == VPU_STATE_STOP);
  }

  SECTION("ADDing vectors of opposite direction, but equal magnitude sets the zero flags.")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF06, VPU_REGISTER_VF05, VPU_REGISTER_VF15, VPU_ADD);
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZX));
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZY));
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZZ));
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZW));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_Z));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_Z_STICKY));
  }

  SECTION("ADDing vectors of opposite direction, but equal magnitude and then doing a second addition unsets the zero flags.")
  {
    addSingleUpperInstruction(&instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF06, VPU_REGISTER_VF05, VPU_REGISTER_VF15, VPU_ADD);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF03, VPU_REGISTER_VF06, VPU_REGISTER_VF04, VPU_ADD);

    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_ZX));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_ZY));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_ZZ));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_ZW));
    REQUIRE(!vpu.hasStatusFlag(VPU_FLAG_Z));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_Z_STICKY));
  }

  SECTION("ADDing vectors sets the sign flags")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF06, VPU_REGISTER_VF03, VPU_REGISTER_VF06, VPU_ADD);
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_SX));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_SY));
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_SZ));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_SW));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_S));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_S_STICKY));
  }

  SECTION("ADDing vectors resets the sign flags")
  {
    addSingleUpperInstruction(&instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF06, VPU_REGISTER_VF06, VPU_REGISTER_VF03, VPU_ADD);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF06, VPU_REGISTER_VF05, VPU_REGISTER_VF15, VPU_ADD);

    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_SX));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_SY));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_SZ));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_SW));
    REQUIRE(!vpu.hasStatusFlag(VPU_FLAG_S));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_S_STICKY));
  }

  SECTION("ADDi stores the addition of the iRegister and the src vector in dest vector")
  {
    vpu.loadIRegister(-4.5f);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, 0, VPU_REGISTER_VF03, VPU_REGISTER_VF20, VPU_ADDi);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF20)->x == -9.5f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF20)->y == -6.9f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF20)->z == -5.5f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF20)->w == 0);
  }

  SECTION("ADDq stores the addition of the qRegister and the src vector in dest vector")
  {
    vpu.loadQRegister(5.0f);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Z_BIT | VPU_DEST_W_BIT, 0, VPU_REGISTER_VF05, VPU_REGISTER_VF21, VPU_ADDq);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF21)->x == 10.0f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF21)->y == 0);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF21)->z == 15.0f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF21)->w == -4.0f);
  }

  SECTION("ADDx stores the addition of the x field of the first src vector to each field of the second src vector in the dest vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_Y_BIT | VPU_DEST_W_BIT, VPU_REGISTER_VF06, VPU_REGISTER_VF05, VPU_REGISTER_VF07, VPU_ADDx);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->x == 10);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->y == -11.4f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->z == 10);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->w == -14);
  }

  SECTION("ADDy stores the addition of the y field of the first src vector to each field of the second src vector in the dest vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Z_BIT, VPU_REGISTER_VF06, VPU_REGISTER_VF05, VPU_REGISTER_VF07, VPU_ADDy);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->x == 11.4f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->y == 10);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->z == 16.4f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->w == 10);
  }

  SECTION("ADDz stores the addition of the z field of the first src vector to each field of the second src vector in the dest vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_Y_BIT | VPU_DEST_Z_BIT | VPU_DEST_W_BIT, VPU_REGISTER_VF06, VPU_REGISTER_VF05, VPU_REGISTER_VF07, VPU_ADDz);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->x == 10);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->y == -16.4f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->z == 0);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->w == -19);
  }

  SECTION("ADDw stores the addition of the w field of the first src vector to each field of the second src vector in the dest vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_W_BIT, VPU_REGISTER_VF11, VPU_REGISTER_VF05, VPU_REGISTER_VF07, VPU_ADDw);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->x == 17.5f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->y == 10);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->z == 10);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF07)->w == 3.5f);
  }

  SECTION("ADDA stores the addition of the first src vector to the second src vector in the accumulator")
  {
    vpu.loadAccumulator(100, 100, 100, 100);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Y_BIT, VPU_REGISTER_VF08, VPU_REGISTER_VF09, 0, VPU_ADDA);

    REQUIRE(vpu.accumulator.x == 1);
    REQUIRE(vpu.accumulator.y == 1);
    REQUIRE(vpu.accumulator.z == 100);
    REQUIRE(vpu.accumulator.w == 100);
  }

  SECTION("ADDAi stores the addition of the iRegister and the src vector in the accumulator")
  {
    vpu.loadIRegister(6.25f);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Z_BIT, 0, VPU_REGISTER_VF07, 0, VPU_ADDAi);

    REQUIRE(vpu.accumulator.x == 16.25f);
    REQUIRE(vpu.accumulator.y == 0);
    REQUIRE(vpu.accumulator.z == 16.25f);
    REQUIRE(vpu.accumulator.w == 0);
  }

  SECTION("ADDAq stores the addition of the qRegister and the src vector in the accumulator")
  {
    vpu.loadQRegister(-3.25);
    vpu.loadAccumulator(30, 51.25f, 0, 0);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_Z_BIT | VPU_DEST_W_BIT, 0, VPU_REGISTER_VF07, 0, VPU_ADDAq);

    REQUIRE(vpu.accumulator.x == 30);
    REQUIRE(vpu.accumulator.y == 51.25f);
    REQUIRE(vpu.accumulator.z == 6.75f);
    REQUIRE(vpu.accumulator.w == 6.75f);
  }

  SECTION("ADDAx stores the addition of the x field of the first src vector to the specified fields of the second src vector in the accumulator")
  {
    vpu.loadAccumulator(30, 30, 30, 30);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Z_BIT | VPU_DEST_W_BIT, VPU_REGISTER_VF11, VPU_REGISTER_VF06, 0, VPU_ADDAx);

    REQUIRE(vpu.accumulator.x == -10);
    REQUIRE(vpu.accumulator.y == 30);
    REQUIRE(vpu.accumulator.z == -15);
    REQUIRE(vpu.accumulator.w == 4);
  }

  SECTION("ADDAy stores the addition of the y field of the first src vector to the specified fields of the second src vector in the accumulator")
  {
    vpu.loadAccumulator(5, 3, 2, 6);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_Y_BIT | VPU_DEST_W_BIT, VPU_REGISTER_VF10, VPU_REGISTER_VF03, 0, VPU_ADDAy);

    REQUIRE(vpu.accumulator.x == 5);
    REQUIRE(vpu.accumulator.y == 0);
    REQUIRE(vpu.accumulator.z == 2);
    REQUIRE(vpu.accumulator.w == 6.9f);
  }

  SECTION("ADDAz stores the addition of the z field of the first src vector to the specified fields of the second src vector in the accumulator")
  {
    vpu.loadAccumulator(19, 20, 21, 22);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_Z_BIT, VPU_REGISTER_VF11, VPU_REGISTER_VF11, 0, VPU_ADDAz);

    REQUIRE(vpu.accumulator.x == 19);
    REQUIRE(vpu.accumulator.y == 20);
    REQUIRE(vpu.accumulator.z == 20);
    REQUIRE(vpu.accumulator.w == 22);
  }

  SECTION("ADDAw stores the addition of the w field of the first src vector to the specified fields of the second src vector in the accumulator")
  {
    vpu.loadAccumulator(1.5f, 1.5f, 1.5f, 1.5f);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF03, VPU_REGISTER_VF12, 0, VPU_ADDAw);

    REQUIRE(vpu.accumulator.x == 64.5f);
    REQUIRE(vpu.accumulator.y == 69.5f);
    REQUIRE(vpu.accumulator.z == 74.5f);
    REQUIRE(vpu.accumulator.w == 79.5f);
  }
}
