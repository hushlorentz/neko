#include "catch.hpp"
#include "floating_point_ops.hpp"
#include "vpu.hpp"
#include "vpu_flags.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"
#include "vpu_upper_instruction_utils.hpp"

TEST_CASE("VPU Microinstruction MSUB Tests")
{
  VPU vpu;
  vpu.useThreads = false;
  vpu.loadFPRegister(VPU_REGISTER_VF02, 1, 1, 1, 1);
  vpu.loadFPRegister(VPU_REGISTER_VF03, -5.0, -2.5, -1.0, 4.5);
  vpu.loadFPRegister(VPU_REGISTER_VF04, 5.0, -6.5, 10.0, -9.0);
  vpu.loadFPRegister(VPU_REGISTER_VF05, 2, -6.5, 0, 9.0);
  vector<uint8_t> instructions;

  SECTION("MSUB multiplies two vectors and subtracts the result from the accumulator and stores the result in the third vector parameter")
  {
    vpu.loadAccumulator(100.0, 75.5, 50.25, 25.0);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MSUB);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == -125);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == -59.25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == -60.25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == -65.5);
  }

  SECTION("MSUB sets the correct flags if accumulator contains 0 or a normalized value and there is no exception during the multiplication")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 0, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF05, VPU_REGISTER_VF02, VPU_MSUB);

    REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZZ));
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_SW));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_Z));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_S));
  }

  SECTION("MSUB sets the correct flags if accumulator contains 0 or a normalized value and there is an overflow exception during the multiplication")
  {
    Double max;
    max.d = std::numeric_limits<float>::max();
    vpu.loadFPRegister(VPU_REGISTER_VF06, max.d, -2.5f, -1.0f, 4.5f);

    vpu.loadAccumulator(100.0f, 75.5f, 0, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Y_BIT | VPU_DEST_Z_BIT, VPU_REGISTER_VF04, VPU_REGISTER_VF06, VPU_REGISTER_VF02, VPU_MSUB);

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

  SECTION("MSUB sets the correct flags if accumulator contains 0 or a normalized value and there is an underflow exception during the multiplication")
  {
    Double nonNormalized;
    nonNormalized.d = std::numeric_limits<double>::min();

    vpu.loadFPRegister(VPU_REGISTER_VF07, 2, 0.5, 3.4f, 9.0f);
    vpu.loadFPRegister(VPU_REGISTER_VF06, 25.5f, nonNormalized.d, -2.5f, -1.0f);
    vpu.loadAccumulator(100.0f, 5, 0, 25.0f);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF07, VPU_REGISTER_VF06, VPU_REGISTER_VF02, VPU_MSUB);

    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_US));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_ZS));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_SS));
    REQUIRE(!vpu.hasStatusFlag(VPU_FLAG_U));
    REQUIRE(!vpu.hasStatusFlag(VPU_FLAG_O));
  }

  SECTION("MSUB sets the correct flags if accumulator contains MAX and the multiplication does not throw an exception.")
  {
    Double max;
    max.exponent = FP_MAX_EXPONENT_WITH_BIAS;
    max.mantissa = FP_MAX_MANTISSA;

    vpu.loadFPRegister(VPU_REGISTER_VF07, 2, 0.5f, max.d, 9.0f);
    vpu.loadFPRegister(VPU_REGISTER_VF06, 25.5f, -2.9f, 1.0f, -1.0f);
    vpu.loadAccumulator(100.0f, 5, max.d, 25.0f);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF07, VPU_REGISTER_VF06, VPU_REGISTER_VF02, VPU_MSUB);

    REQUIRE(vpu.hasMACFlag(VPU_FLAG_OZ));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_O));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_OS));
  }

  SECTION("MSUB returns the result from the multiplication if there is an overflow and ignores the result of the subtraction. The sign of the result is opposite from the sign of the overflow. Positive Result.")
  {
    Double max;
    max.exponent = FP_MAX_EXPONENT_WITH_BIAS;
    max.mantissa = FP_MAX_MANTISSA;
    max.sign = FP_SIGN_NEG;

    vpu.loadFPRegister(VPU_REGISTER_VF07, max.d, 0.5f, 1.0f, 9.0f);
    vpu.loadFPRegister(VPU_REGISTER_VF06, 100, -2.5f, -1.0f, 4.5f);
    vpu.loadAccumulator(100.0f, 5, max.d, 25.0f);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Y_BIT, VPU_REGISTER_VF07, VPU_REGISTER_VF06, VPU_REGISTER_VF02, VPU_MSUB);

    Double result;
    result.d = vpu.fpRegisterValue(VPU_REGISTER_VF02)->x;

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == -6.25);
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_OX));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_SX));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_OY));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_OZ));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_OW));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_O));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_OS));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_S));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_SS));
    REQUIRE(result.mantissa == FP_MAX_MANTISSA);
    REQUIRE(result.exponent == FP_MAX_EXPONENT_WITH_BIAS);
    REQUIRE(result.sign == FP_SIGN_POS);
  }

  SECTION("MSUB returns the result from the multiplication if there is an overflow and ignores the result of the subtraction. The sign of the result is opposite from the sign of the overflow. Negative Result.")
  {
    Double max;
    max.exponent = FP_MAX_EXPONENT_WITH_BIAS;
    max.mantissa = FP_MAX_MANTISSA;
    max.sign = FP_SIGN_POS;

    vpu.loadFPRegister(VPU_REGISTER_VF07, max.d, 0.5f, 1.0f, 9.0f);
    vpu.loadFPRegister(VPU_REGISTER_VF06, 100, -2.5f, -1.0f, 4.5f);
    vpu.loadAccumulator(100.0f, 5, max.d, 25.0f);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_X_BIT | VPU_DEST_Y_BIT, VPU_REGISTER_VF07, VPU_REGISTER_VF06, VPU_REGISTER_VF02, VPU_MSUB);

    Double result;
    result.d = vpu.fpRegisterValue(VPU_REGISTER_VF02)->x;

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == -6.25);
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_OX));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_OY));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_OZ));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_OW));
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_SX));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_O));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_OS));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_SS));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_S));
    REQUIRE(result.mantissa == FP_MAX_MANTISSA);
    REQUIRE(result.exponent == FP_MAX_EXPONENT_WITH_BIAS);
    REQUIRE(result.sign == FP_SIGN_NEG);
  }

  SECTION("MSUB returns the result from the subtraction if there is an underflow in multiplication.")
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

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_Y_BIT, VPU_REGISTER_VF07, VPU_REGISTER_VF06, VPU_REGISTER_VF02, VPU_MSUB);

    Double result;
    result.d = vpu.fpRegisterValue(VPU_REGISTER_VF02)->y;

    REQUIRE(vpu.hasMACFlag(VPU_FLAG_OY));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_O));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_OS));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_US));
    REQUIRE(result.mantissa == FP_MAX_MANTISSA);
    REQUIRE(result.exponent == FP_MAX_EXPONENT_WITH_BIAS);
  }

  SECTION("MSUBi stores the result of the subtraction of the accumulator with the product of the fs register and the I register in the fd register.")
  {
    vpu.loadAccumulator(100.0, 75.5, 50.25, 25.0);
    vpu.loadIRegister(0.25);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, 0, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MSUBi);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == -101.25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == -76.125);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == -50.5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == -23.875);
  }

  SECTION("MSUBq stores the result of the subtraction of the accumulator with the product of the fs register and the Q register in the fd register.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    vpu.loadQRegister(0.25);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, 0, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MSUBq);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == -101.25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == -76.125);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == -50.5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == -23.875);
  }

  SECTION("MSUBx stores the result of the subtraction of the accumulator with the product of the fs register and the x field of the ft register in the fd register.")
  {
    vpu.loadAccumulator(100.0, 75.5, 50.25, 25.0);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MSUBx);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == -125);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == -88);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == -55.25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == -2.5);
  }

  SECTION("MSUBy stores the result of the subtraction of the accumulator with the product of the fs register and the y field of the ft register in the fd register.")
  {
    vpu.loadAccumulator(100.0, 75.5, 50.25, 25.0);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MSUBy);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == -67.5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == -59.25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == -43.75);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == -54.25);
  }

  SECTION("MSUBz stores the result of the subtraction of the accumulator with the product of the fs register and the z field of the ft register in the fd register.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MSUBz);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == -150);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == -100.5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == -60.25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == 20);
  }

  SECTION("MSUBw stores the result of the subtraction of the accumulator with the product of the fs register and the w field of the ft register in the fd register.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MSUBw);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == -55);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == -53);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == -41.25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == -65.5);
  }

  SECTION("MSUBA stores the result of the subtraction of the accumulator with the product of the fs register and the ft register in the accumulator.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MSUBA);

    REQUIRE(vpu.accumulator.x == -125);
    REQUIRE(vpu.accumulator.y == -59.25);
    REQUIRE(vpu.accumulator.z == -60.25);
    REQUIRE(vpu.accumulator.w == -65.5);
  }

  SECTION("MSUBAi stores the result of the subtraction of the accumulator with the product of the fs register and the I register in the accumulator.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    vpu.loadIRegister(0.25);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, 0, VPU_REGISTER_VF03, 0, VPU_MSUBAi);

    REQUIRE(vpu.accumulator.x == -101.25);
    REQUIRE(vpu.accumulator.y == -76.125);
    REQUIRE(vpu.accumulator.z == -50.5);
    REQUIRE(vpu.accumulator.w == -23.875);
  }

  SECTION("MSUBAq stores the result of the subtraction of the accumulator with the product of the fs register and the Q register in the accumulator.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    vpu.loadQRegister(0.25);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, 0, VPU_REGISTER_VF03, 0, VPU_MSUBAq);

    REQUIRE(vpu.accumulator.x == -101.25);
    REQUIRE(vpu.accumulator.y == -76.125);
    REQUIRE(vpu.accumulator.z == -50.5);
    REQUIRE(vpu.accumulator.w == -23.875);
  }

  SECTION("MSUBAx stores the result of the subtraction of the accumulator with the product of the fs register and the x field of the ft register in the accumulator.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, 0, VPU_MSUBAx);

    REQUIRE(vpu.accumulator.x == -125);
    REQUIRE(vpu.accumulator.y == -88);
    REQUIRE(vpu.accumulator.z == -55.25);
    REQUIRE(vpu.accumulator.w == -2.5);
  }

  SECTION("MSUBAy stores the result of the subtraction of the accumulator with the product of the fs register and the y field of the ft register in the accumulator.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, 0, VPU_MSUBAy);

    REQUIRE(vpu.accumulator.x == -67.5);
    REQUIRE(vpu.accumulator.y == -59.25);
    REQUIRE(vpu.accumulator.z == -43.75);
    REQUIRE(vpu.accumulator.w == -54.25);
  }

  SECTION("MSUBAz stores the result of the subtraction of the accumulator with the product of the fs register and the z field of the ft register in the accumulator.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MSUBAz);

    REQUIRE(vpu.accumulator.x == -150);
    REQUIRE(vpu.accumulator.y == -100.5);
    REQUIRE(vpu.accumulator.z == -60.25);
    REQUIRE(vpu.accumulator.w == 20);
  }

  SECTION("MSUBAw stores the result of the subtraction of the accumulator with the product of the fs register and the w field of the ft register in the accumulator.")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MSUBAw);

    REQUIRE(vpu.accumulator.x == -55);
    REQUIRE(vpu.accumulator.y == -53);
    REQUIRE(vpu.accumulator.z == -41.25);
    REQUIRE(vpu.accumulator.w == -65.5);
  }
}
