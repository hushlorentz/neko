#include "catch.hpp"
#include "floating_point_ops.hpp"
#include "vpu.hpp"
#include "vpu_flags.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"
#include "vpu_upper_instruction_utils.hpp"

TEST_CASE("VPU Microinstruction MADD Tests")
{
  VPU vpu;
  vpu.useThreads = false;
  vpu.loadFPRegister(VPU_REGISTER_VF02, 1, 1, 1, 1);
  vpu.loadFPRegister(VPU_REGISTER_VF03, -5.0f, -2.5f, -1.0f, 4.5f);
  vpu.loadFPRegister(VPU_REGISTER_VF04, 5.0f, -6.5f, 10.0f, -9.0f);
  vpu.loadFPRegister(VPU_REGISTER_VF05, 2, -6.5f, 0, 9.0f);
  vector<uint8_t> instructions;

  SECTION("MADD adds two vectors and subtracts the result from the accumulator and stores the result in the third vector parameter")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MADD);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == 75.0f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == 91.75f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == 40.25f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == -15.5f);
  }

  SECTION("MADD sets the correct flags if accumulator contains 0 or a normalized value and there is no exception during the multiplication")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 0, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF05, VPU_REGISTER_VF02, VPU_MADD);

    REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZZ));
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_SW));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_Z));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_S));
  }

  SECTION("MADD sets the correct flags if accumulator contains 0 or a normalized value and there is an overflow exception during the multiplication")
  {
    Double max;
    max.d = std::numeric_limits<float>::max();
    vpu.loadFPRegister(VPU_REGISTER_VF06, max.d, -2.5f, -1.0f, 4.5f);

    vpu.loadAccumulator(100.0f, 75.5f, 0, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Y_BIT | VPU_DEST_Z_BIT, VPU_REGISTER_VF04, VPU_REGISTER_VF06, VPU_REGISTER_VF02, VPU_MADD);

    Double result;
    result.d = vpu.fpRegisterValue(VPU_REGISTER_VF02)->x;

    REQUIRE(vpu.hasMACFlag(VPU_FLAG_OX));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_OY));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_OZ));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_OW));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_O));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_OS));
    REQUIRE(result.mantissa == FP_MAX_MANTISSA);
    REQUIRE(result.exponent == FP_MAX_EXPONENT_WITH_BIAS);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == 1);
  }

  SECTION("MADD sets the correct flags if accumulator contains 0 or a normalized value and there is an underflow exception during the multiplication")
  {
    Double nonNormalized;
    nonNormalized.d = std::numeric_limits<double>::min();

    vpu.loadFPRegister(VPU_REGISTER_VF07, 2, 0.5, 3.4f, 9.0f);
    vpu.loadFPRegister(VPU_REGISTER_VF06, 25.5f, nonNormalized.d, -2.5f, -1.0f);
    vpu.loadAccumulator(100.0f, 5, 0, 25.0f);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF07, VPU_REGISTER_VF06, VPU_REGISTER_VF02, VPU_MADD);

    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_US));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_ZS));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_SS));
    REQUIRE(!vpu.hasStatusFlag(VPU_FLAG_U));
    REQUIRE(!vpu.hasStatusFlag(VPU_FLAG_O));
  }

  SECTION("MADD sets the correct flags if accumulator contains MAX and the multiplication does not throw an exception.")
  {
    Double max;
    max.exponent = FP_MAX_EXPONENT_WITH_BIAS;
    max.mantissa = FP_MAX_MANTISSA;

    vpu.loadFPRegister(VPU_REGISTER_VF07, 2, 0.5f, max.d, 9.0f);
    vpu.loadFPRegister(VPU_REGISTER_VF06, 25.5f, -2.9f, 1.0f, -1.0f);
    vpu.loadAccumulator(100.0f, 5, max.d, 25.0f);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF07, VPU_REGISTER_VF06, VPU_REGISTER_VF02, VPU_MADD);

    REQUIRE(vpu.hasMACFlag(VPU_FLAG_OZ));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_O));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_OS));
  }

  SECTION("MADD returns the result from the multiplication if there is an overflow and ignores the result of the addition.")
  {
    Double max;
    max.exponent = FP_MAX_EXPONENT_WITH_BIAS;
    max.mantissa = FP_MAX_MANTISSA;
    max.sign = FP_SIGN_NEG;

    vpu.loadFPRegister(VPU_REGISTER_VF07, max.d, 0.5f, 1.0f, 9.0f);
    vpu.loadFPRegister(VPU_REGISTER_VF06, 100, -2.5f, -1.0f, 4.5f);
    vpu.loadAccumulator(100.0f, 5, max.d, 25.0f);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Y_BIT, VPU_REGISTER_VF07, VPU_REGISTER_VF06, VPU_REGISTER_VF02, VPU_MADD);

    Double result;
    result.d = vpu.fpRegisterValue(VPU_REGISTER_VF02)->x;

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == 3.75);
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_OX));
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_SX));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_OY));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_OZ));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_OW));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_O));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_OS));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_S));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_SS));
    REQUIRE(result.mantissa == FP_MAX_MANTISSA);
    REQUIRE(result.exponent == FP_MAX_EXPONENT_WITH_BIAS);
    REQUIRE(result.sign == FP_SIGN_NEG);
  }

  SECTION("MADD returns the result from the addition if there is an underflow in multiplication.")
  {
    Double max;
    max.exponent = FP_MAX_EXPONENT_WITH_BIAS;
    max.mantissa = FP_MAX_MANTISSA;
    max.sign = FP_SIGN_NEG;

    Double nonNormalized;
    nonNormalized.d = std::numeric_limits<double>::min();

    vpu.loadFPRegister(VPU_REGISTER_VF07, 2, 0.5, 3.4f, 9.0f);
    vpu.loadFPRegister(VPU_REGISTER_VF06, 25.5f, nonNormalized.d, -2.5f, -1.0f);
    vpu.loadAccumulator(100.0f, max.d, 80, 25.0f);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_Y_BIT, VPU_REGISTER_VF07, VPU_REGISTER_VF06, VPU_REGISTER_VF02, VPU_MADD);

    Double result;
    result.d = vpu.fpRegisterValue(VPU_REGISTER_VF02)->y;

    REQUIRE(vpu.hasMACFlag(VPU_FLAG_OY));
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_SY));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_O));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_S));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_OS));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_SS));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_US));
    REQUIRE(result.mantissa == FP_MAX_MANTISSA);
    REQUIRE(result.exponent == FP_MAX_EXPONENT_WITH_BIAS);
    REQUIRE(result.sign == FP_SIGN_NEG);
  }

  SECTION("MADDi stores the result of the addition of the accumulator with the product of the fs register and the I register in the fd register.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    vpu.loadIRegister(0.25);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, 0, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MADDi);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == 98.75f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == 74.875f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == 50);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == 26.125f);
  }

  SECTION("MADDq stores the result of the addition of the accumulator with the product of the fs register and the Q register in the fd register.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    vpu.loadQRegister(0.25);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, 0, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MADDq);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == 98.75f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == 74.875f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == 50);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == 26.125f);
  }

  SECTION("MADDx stores the result of the addition of the accumulator with the product of the fs register and the x field of the ft register in the fd register.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MADDx);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == 75);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == 63);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == 45.25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == 47.5);
  }

  SECTION("MADDy stores the result of the addition of the accumulator with the product of the fs register and the y field of the ft register in the fd register.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MADDy);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == 132.5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == 91.75);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == 56.75);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == -4.25);
  }

  SECTION("MADDz stores the result of the addition of the accumulator with the product of the fs register and the z field of the ft register in the fd register.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MADDz);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == 50);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == 50.5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == 40.25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == 70);
  }

  SECTION("MADDw stores the result of the addition of the accumulator with the product of the fs register and the w field of the ft register in the fd register.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MADDw);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == 145);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == 98);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == 59.25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == -15.5);
  }

  SECTION("MADDA stores the result of the addition of the accumulator with the product of the fs register and the ft register in the accumulator.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MADDA);

    REQUIRE(vpu.accumulator.x == 75.0f);
    REQUIRE(vpu.accumulator.y == 91.75f);
    REQUIRE(vpu.accumulator.z == 40.25f);
    REQUIRE(vpu.accumulator.w == -15.5f);
  }

  SECTION("MADDAi stores the result of the addition of the accumulator with the product of the fs register and the I register in the accumulator.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    vpu.loadIRegister(0.25);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, 0, VPU_REGISTER_VF03, 0, VPU_MADDAi);

    REQUIRE(vpu.accumulator.x == 98.75f);
    REQUIRE(vpu.accumulator.y == 74.875f);
    REQUIRE(vpu.accumulator.z == 50);
    REQUIRE(vpu.accumulator.w == 26.125f);
  }

  SECTION("MADDAq stores the result of the addition of the accumulator with the product of the fs register and the Q register in the accumulator.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    vpu.loadQRegister(0.25);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, 0, VPU_REGISTER_VF03, 0, VPU_MADDAq);

    REQUIRE(vpu.accumulator.x == 98.75f);
    REQUIRE(vpu.accumulator.y == 74.875f);
    REQUIRE(vpu.accumulator.z == 50);
    REQUIRE(vpu.accumulator.w == 26.125f);
  }

  SECTION("MADDAx stores the result of the addition of the accumulator with the product of the fs register and the x field of the ft register in the accumulator.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, 0, VPU_MADDAx);

    REQUIRE(vpu.accumulator.x == 75);
    REQUIRE(vpu.accumulator.y == 63);
    REQUIRE(vpu.accumulator.z == 45.25);
    REQUIRE(vpu.accumulator.w == 47.5);
  }

  SECTION("MADDAy stores the result of the addition of the accumulator with the product of the fs register and the y field of the ft register in the accumulator.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, 0, VPU_MADDAy);

    REQUIRE(vpu.accumulator.x == 132.5);
    REQUIRE(vpu.accumulator.y == 91.75);
    REQUIRE(vpu.accumulator.z == 56.75);
    REQUIRE(vpu.accumulator.w == -4.25);
  }

  SECTION("MADDAz stores the result of the addition of the accumulator with the product of the fs register and the z field of the ft register in the accumulator.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MADDAz);

    REQUIRE(vpu.accumulator.x == 50);
    REQUIRE(vpu.accumulator.y == 50.5);
    REQUIRE(vpu.accumulator.z == 40.25);
    REQUIRE(vpu.accumulator.w == 70);
  }

  SECTION("MADDAw stores the result of the addition of the accumulator with the product of the fs register and the w field of the ft register in the accumulator.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MADDAw);

    REQUIRE(vpu.accumulator.x == 145);
    REQUIRE(vpu.accumulator.y == 98);
    REQUIRE(vpu.accumulator.z == 59.25);
    REQUIRE(vpu.accumulator.w == -15.5);
  }
}
