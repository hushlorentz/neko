#include <cfloat>

#include "catch.hpp"
#include "bit_ops.hpp"
#include "floating_point_ops.hpp"
#include "fp_register.hpp"

TEST_CASE("Testing the floating point conventions")
{
  uint8_t resultFlags = 0;

  SECTION("Two floating point numbers can be added together")
  {
    float a1 = 3.5f;
    float a2 = 7.25f;

    REQUIRE(addFP(a1, a2, &resultFlags) == a1 + a2);
  }

  SECTION("Two floating point numbers can be multiplied together")
  {
    float a1 = 3.5f;
    float a2 = 7.0f;

    REQUIRE(mulFP(a1, a2, &resultFlags) == a1 * a2);
  }

  SECTION("Two floating point numbers can be subtracted")
  {
    float a1 = 5.6;
    float a2 = 7.8;

    REQUIRE(subFP(a1, a2, &resultFlags) == a1 - a2);
  }

  SECTION("Two floating point numbers can be divided")
  {
    float a1 = 10.5f;
    float a2 = 3.44f;

    REQUIRE(divFP(a1, a2, &resultFlags) == a1 / a2);
  }

  SECTION("Multiplying two positive floating point numbers that result in INF returns 2^(128), with a 0 sign bit, and sets the overflow flag.")
  {
    float a1 = std::numeric_limits<float>::infinity();
    float a2 = 7.0f;
    num_32bits num;

    num.float_representation = mulFP(a1, a2, &resultFlags);

    REQUIRE(num.components.mantissa == FP_MAX_MANTISSA);
    REQUIRE(num.components.exponent == FP_MAX_EXPONENT);
    REQUIRE(num.components.sign == 0);
    REQUIRE(resultFlags == FP_FLAG_OVERFLOW);
  }

  SECTION("Multiplying a positive floating point number and negative floating point number that result in INF returns 2^(128), with a 1 sign bit, and sets the overflow flag.")
  {
    float a1 = std::numeric_limits<float>::infinity();
    float a2 = -7.0f;
    num_32bits num;

    num.float_representation = mulFP(a1, a2, &resultFlags);

    REQUIRE(num.components.mantissa == FP_MAX_MANTISSA);
    REQUIRE(num.components.exponent == FP_MAX_EXPONENT);
    REQUIRE(num.components.sign == 1);
    REQUIRE(resultFlags == FP_FLAG_OVERFLOW);
  }

  SECTION("Dividing 0 by 0 returns 0 with the I bit flag set")
  {
    float a1 = 0;
    float a2 = 0;
    num_32bits num;

    num.float_representation = divFP(a1, a2, &resultFlags);

    REQUIRE(num.components.mantissa == 0);
    REQUIRE(num.components.exponent == 0);
    REQUIRE(num.components.sign == 0);
    REQUIRE(resultFlags == FP_FLAG_I_BIT);
  }
  
  SECTION("Dividing -0 by -0 returns 0 with the I bit flag set")
  {
    num_32bits a1;
    num_32bits a2;
    num_32bits num;

    a1.float_representation = 0;
    a1.components.sign = 1;
    a2.float_representation = 0;
    a2.components.sign = 1;

    num.float_representation = divFP(a1.float_representation, a2.float_representation, &resultFlags);

    REQUIRE(num.components.mantissa == 0);
    REQUIRE(num.components.exponent == 0);
    REQUIRE(num.components.sign == 0);
    REQUIRE(resultFlags == FP_FLAG_I_BIT);
  }

  SECTION("Dividing 0 by -0 returns -0 with the I bit flag set")
  {
    float a1 = 0;
    num_32bits a2;
    num_32bits num;

    a2.float_representation = 0;
    a2.components.sign = 1;

    num.float_representation = divFP(a1, a2.float_representation, &resultFlags);

    REQUIRE(num.components.mantissa == 0);
    REQUIRE(num.components.exponent == 0);
    REQUIRE(num.components.sign == 1);
    REQUIRE(resultFlags == FP_FLAG_I_BIT);
  }

  SECTION("Dividing 5 by 0 returns 2^128 with the D bit flag set")
  {
    float a1 = 5;
    float a2 = 0;
    num_32bits result;

    result.float_representation = divFP(a1, a2, &resultFlags);

    REQUIRE(result.components.mantissa == FP_MAX_MANTISSA);
    REQUIRE(result.components.exponent == FP_MAX_EXPONENT);
    REQUIRE(result.components.sign == 0);
    REQUIRE(resultFlags == FP_FLAG_D_BIT);
  }

  SECTION("Dividing -5 by 0 returns 2^128 with the D bit flag set and sign bit set")
  {
    float a1 = -5;
    float a2 = 0;
    num_32bits result;

    result.float_representation = divFP(a1, a2, &resultFlags);

    REQUIRE(result.components.mantissa == FP_MAX_MANTISSA);
    REQUIRE(result.components.exponent == FP_MAX_EXPONENT);
    REQUIRE(result.components.sign == 1);
    REQUIRE(resultFlags == FP_FLAG_D_BIT);
  }

  SECTION("Dividing the smallest normalized value by 5 causes an underflow and returns 0 and sets the underflow flag")
  {
    float a1 = std::numeric_limits<float>::min();
    float a2 = 5;
    num_32bits num;

    num.float_representation = divFP(a1, a2, &resultFlags);

    REQUIRE(num.float_representation == 0.0f);
    REQUIRE(num.components.sign == 0);
    REQUIRE(resultFlags == FP_FLAG_UNDERFLOW);
  }

  SECTION("Dividing the smallest normalized value by -5 causes an underflow and returns 0 and sets the underflow flag and the sign bit is set.")
  {
    float a1 = std::numeric_limits<float>::min();
    float a2 = -5;
    num_32bits num;

    num.float_representation = divFP(a1, a2, &resultFlags);

    REQUIRE(num.float_representation == 0.0f);
    REQUIRE(num.components.sign == 1);
    REQUIRE(resultFlags == FP_FLAG_UNDERFLOW);
  }

  SECTION("Converting 0.45f to a fixed point 0 precision number returns 0")
  {
    REQUIRE(floatToInteger0(0.45f) == 0);
  }

  SECTION("Converting -0.45f to a fixed point 0 precision number returns 0")
  {
    REQUIRE(floatToInteger0(-0.45f) == 0);
  }

  SECTION("Converting 123.45f to a fixed point 0 precision number returns 123")
  {
    REQUIRE(floatToInteger0(123.45f) == 123);
  }

  SECTION("Converting -123.45f to a fixed point 0 precision number returns -123")
  {
    REQUIRE(floatToInteger0(-123.45f) == -123);
  }

  SECTION("Converting -0.45f to a fixed point 4 precision number returns -7")
  {
    REQUIRE(floatToInteger4(-0.45f) == -7);
  }

  SECTION("Converting 0.45f to a fixed point 4 precision number returns 7")
  {
    REQUIRE(floatToInteger4(0.45f) == 7);
  }

  SECTION("Converting 0.55f to a fixed point 4 precision number returns 8")
  {
    REQUIRE(floatToInteger4(0.55f) == 8);
  }

  SECTION("Converting 123.45f to a fixed point 4 precision number returns 1975")
  {
    REQUIRE(floatToInteger4(123.45f) == 1975);
  }

  SECTION("Converting -0.45f to a fixed point 12 precision number returns -1843")
  {
    REQUIRE(floatToInteger12(-0.45f) == -1843);
  }

  SECTION("Converting 0.45f to a fixed point 12 precision number returns 1843")
  {
    REQUIRE(floatToInteger12(0.45f) == 1843);
  }

  SECTION("Converting 0.55f to a fixed point 12 precision number returns 2252")
  {
    REQUIRE(floatToInteger12(0.55f) == 2252);
  }

  SECTION("Converting 123.45f to a fixed point 12 precision number returns 505651")
  {
    REQUIRE(floatToInteger12(123.45f) == 505651);
  }

  SECTION("Converting -0.45f to a fixed point 15 precision number returns -14745")
  {
    REQUIRE(floatToInteger15(-0.45f) == -14745);
  }

  SECTION("Converting 0.45f to a fixed point 15 precision number returns 14745")
  {
    REQUIRE(floatToInteger15(0.45f) == 14745);
  }

  SECTION("Converting 0.55f to a fixed point 15 precision number returns 18022")
  {
    REQUIRE(floatToInteger15(0.55f) == 18022);
  }

  SECTION("Converting 123.45f to a fixed point 15 precision number returns 4045209")
  {
    REQUIRE(floatToInteger15(123.45f) == 4045209);
  }
}
