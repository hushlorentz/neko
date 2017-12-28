#include <cfloat>

#include "catch.hpp"
#include "bit_ops.hpp"
#include "floating_point_ops.hpp"
#include "fp_register.hpp"

TEST_CASE("Testing the floating point conventions")
{
  uint8_t resultFlags = 0;

  SECTION("The double union holds the correct values in its struct for 1.0")
  {
    double num = 1.0;
    Double du;
    du.d = num;

    REQUIRE(du.mantissa == 0);
    REQUIRE(du.exponent == FP_EXP_BIAS);
    REQUIRE(du.sign == 0);
  }

  SECTION("The double union holds the correct exponent for FLT_MAX")
  {
    double num = FLT_MAX;
    Double du;
    du.d = num;

    REQUIRE(du.exponent == 127 + FP_EXP_BIAS);
    REQUIRE(du.sign == 0);
  }
  
  SECTION("The double union can hold an exponent of 128")
  {
    Double du;
    du.exponent = FP_EXP_BIAS + 128;

    REQUIRE(du.d > FLT_MAX);
  }

  SECTION("Two floating point numbers can be added together")
  {
    double d1 = 3.5;
    double d2 = 7.25;

    REQUIRE(addFP(d1, d2, &resultFlags) == d1 + d2);
  }

  SECTION("Two floating point numbers can be multiplied together")
  {
    double d1 = 3.5;
    double d2 = 7.0;

    REQUIRE(mulFP(d1, d2, &resultFlags) == d1 * d2);
  }

  SECTION("Two floating point numbers can be subtracted")
  {
    double d1 = 5.6;
    double d2 = 7.8;

    REQUIRE(subFP(d1, d2, &resultFlags) == d1 - d2);
  }

  SECTION("Two floating point numbers can be divided")
  {
    double d1 = 10.5f;
    double d2 = 3.44f;

    REQUIRE(divFP(d1, d2, &resultFlags) == d1 / d2);
  }

  SECTION("Multiplying two positive floating point numbers that result in an overflow returns the max float, with a 0 sign bit, and sets the overflow flag.")
  {
    double d1 = FLT_MAX;
    double d2 = 7.0;
    Double num;

    num.d = mulFP(d1, d2, &resultFlags);

    REQUIRE(num.mantissa == FP_MAX_MANTISSA);
    REQUIRE(num.exponent == FP_MAX_EXPONENT_WITH_BIAS);
    REQUIRE(num.sign == 0);
    REQUIRE(resultFlags == FP_FLAG_OVERFLOW);
  }

  SECTION("Dividing two overflow results should result in 1")
  {
    double d1 = FLT_MAX;
    double d2 = 7.0;

    Double d1Converted;
    Double d2Converted;

    d1Converted.d = mulFP(d1, d2, &resultFlags);
    d2Converted.d = mulFP(d1, d2, &resultFlags);

    REQUIRE(divFP(d1Converted.d, d2Converted.d, &resultFlags) == 1);
  }

  SECTION("Multiplying a positive floating point number and negative floating point number that result in an overflow returns -MAX and sets the overflow flag.")
  {
    double d1 = FLT_MAX;
    double d2 = -7.0f;
    Double num;

    num.d = mulFP(d1, d2, &resultFlags);

    REQUIRE(num.mantissa == FP_MAX_MANTISSA);
    REQUIRE(num.exponent == FP_MAX_EXPONENT_WITH_BIAS);
    REQUIRE(num.sign == 1);
    REQUIRE(resultFlags == FP_FLAG_OVERFLOW);
  }

  SECTION("Dividing 0 by 0 returns 0 with the I bit flag set")
  {
    double d1 = 0;
    double d2 = 0;
    Double num;

    num.d = divFP(d1, d2, &resultFlags);

    REQUIRE(num.mantissa == 0);
    REQUIRE(num.exponent == 0);
    REQUIRE(num.sign == 0);
    REQUIRE(resultFlags == FP_FLAG_I_BIT);
  }
  
  SECTION("Dividing -0 by -0 returns 0 with the I bit flag set")
  {
    Double d1;
    Double d2;
    Double num;

    d1.d = 0;
    d1.sign = 1;
    d2.d = 0;
    d2.sign = 1;

    num.d = divFP(d1.d, d2.d, &resultFlags);

    REQUIRE(num.mantissa == 0);
    REQUIRE(num.exponent == 0);
    REQUIRE(num.sign == 0);
    REQUIRE(resultFlags == FP_FLAG_I_BIT);
  }

  SECTION("Dividing 0 by -0 returns -0 with the I bit flag set")
  {
    double d1 = 0;
    Double d2;
    Double num;

    d2.d = 0;
    d2.sign = 1;

    num.d = divFP(d1, d2.d, &resultFlags);

    REQUIRE(num.mantissa == 0);
    REQUIRE(num.exponent == 0);
    REQUIRE(num.sign == 1);
    REQUIRE(resultFlags == FP_FLAG_I_BIT);
  }

  SECTION("Dividing 5 by 0 returns MAX with the D bit flag set")
  {
    double d1 = 5;
    double d2 = 0;
    Double result;

    result.d = divFP(d1, d2, &resultFlags);

    REQUIRE(result.mantissa == FP_MAX_MANTISSA);
    REQUIRE(result.exponent == FP_MAX_EXPONENT_WITH_BIAS);
    REQUIRE(result.sign == 0);
    REQUIRE(resultFlags == FP_FLAG_D_BIT);
  }

  SECTION("Dividing -5 by 0 returns -MAX with the D bit flag set and sign bit set")
  {
    double d1 = -5;
    double d2 = 0;
    Double result;

    result.d = divFP(d1, d2, &resultFlags);

    REQUIRE(result.mantissa == FP_MAX_MANTISSA);
    REQUIRE(result.exponent == FP_MAX_EXPONENT_WITH_BIAS);
    REQUIRE(result.sign == 1);
    REQUIRE(resultFlags == FP_FLAG_D_BIT);
  }

  SECTION("Dividing the smallest normalized value by 5 causes an underflow and returns 0 and sets the underflow flag")
  {
    double d1 = std::numeric_limits<double>::min();
    double d2 = 5;
    Double num;

    num.d = divFP(d1, d2, &resultFlags);

    REQUIRE(num.d == 0);
    REQUIRE(num.sign == 0);
    REQUIRE(resultFlags == FP_FLAG_UNDERFLOW);
  }

  SECTION("Dividing the smallest normalized value by -5 causes an underflow and returns 0 and sets the underflow flag and the sign bit is set.")
  {
    double d1 = std::numeric_limits<double>::min();
    double d2 = -5;
    Double num;

    num.d = divFP(d1, d2, &resultFlags);

    REQUIRE(num.d == 0.0f);
    REQUIRE(num.sign == 1);
    REQUIRE(resultFlags == FP_FLAG_UNDERFLOW);
  }

  SECTION("Converting 0.45 to a fixed point 0 precision number returns 0")
  {
    REQUIRE(doubleToInteger0(0.45) == 0);
  }

  SECTION("Converting -0.45 to a fixed point 0 precision number returns 0")
  {
    REQUIRE(doubleToInteger0(-0.45) == 0);
  }

  SECTION("Converting 123.45 to a fixed point 0 precision number returns 123")
  {
    REQUIRE(doubleToInteger0(123.45) == 123);
  }

  SECTION("Converting -123.45 to a fixed point 0 precision number returns -123")
  {
    REQUIRE(doubleToInteger0(-123.45) == -123);
  }

  SECTION("Converting -0.45 to a fixed point 4 precision number returns -7")
  {
    REQUIRE(doubleToInteger4(-0.45) == -7);
  }

  SECTION("Converting 0.45 to a fixed point 4 precision number returns 7")
  {
    REQUIRE(doubleToInteger4(0.45) == 7);
  }

  SECTION("Converting 0.55 to a fixed point 4 precision number returns 8")
  {
    REQUIRE(doubleToInteger4(0.55) == 8);
  }

  SECTION("Converting 123.45 to a fixed point 4 precision number returns 1975")
  {
    REQUIRE(doubleToInteger4(123.45) == 1975);
  }

  SECTION("Converting -0.45 to a fixed point 12 precision number returns -1843")
  {
    REQUIRE(doubleToInteger12(-0.45) == -1843);
  }

  SECTION("Converting 0.45 to a fixed point 12 precision number returns 1843")
  {
    REQUIRE(doubleToInteger12(0.45) == 1843);
  }

  SECTION("Converting 0.55 to a fixed point 12 precision number returns 2252")
  {
    REQUIRE(doubleToInteger12(0.55) == 2252);
  }

  SECTION("Converting 123.45 to a fixed point 12 precision number returns 505651")
  {
    REQUIRE(doubleToInteger12(123.45) == 505651);
  }

  SECTION("Converting -0.45 to a fixed point 15 precision number returns -14745")
  {
    REQUIRE(doubleToInteger15(-0.45) == -14745);
  }

  SECTION("Converting 0.45 to a fixed point 15 precision number returns 14745")
  {
    REQUIRE(doubleToInteger15(0.45) == 14745);
  }

  SECTION("Converting 0.55 to a fixed point 15 precision number returns 18022")
  {
    REQUIRE(doubleToInteger15(0.55) == 18022);
  }

  SECTION("Converting 123.45 to a fixed point 15 precision number returns 4045209")
  {
    REQUIRE(doubleToInteger15(123.45) == 4045209);
  }

  SECTION("Converting a 0 precision fixed point number -12 to a floating point number returns -12.0")
  {
    REQUIRE(integer0ToDouble(-12) == -12.0);
  }

  SECTION("Converting a 0 precision fixed point number 1 to a floating point number returns 1.0")
  {
    REQUIRE(integer0ToDouble(1) == 1.0);
  }

  SECTION("Converting a 0 precision fixed point number 123 to a floating point number returns 123.0")
  {
    REQUIRE(integer0ToDouble(123) == 123.0);
  }

  SECTION("Converting a 0 precision fixed point number 1843 to a floating point number returns 1843.0")
  {
    REQUIRE(integer0ToDouble(1843) == 1843.0);
  }

  SECTION("Converting a 4 precision fixed point number -12 to a floating point number returns -0.75")
  {
    REQUIRE(integer4ToDouble(-12) == -0.75);
  }

  SECTION("Converting a 4 precision fixed point number 1 to a floating point number returns 0.0625")
  {
    REQUIRE(integer4ToDouble(1) == 0.0625);
  }

  SECTION("Converting a 4 precision fixed point number 123 to a floating point number returns 7.6875")
  {
    REQUIRE(integer4ToDouble(123) == 7.6875);
  }

  SECTION("Converting a 4 precision fixed point number 1843 to a floating point number returns 115.1875")
  {
    REQUIRE(integer4ToDouble(1843) == 115.1875);
  }

  SECTION("Converting a 12 precision fixed point number -12 to a floating point number returns -0.00293")
  {
    REQUIRE(integer12ToDouble(-12) == -0.0029296875);
  }

  SECTION("Converting a 12 precision fixed point number 1 to a floating point number returns 0.000244")
  {
    REQUIRE(integer12ToDouble(1) == 0.000244140625);
  }

  SECTION("Converting a 12 precision fixed point number 123 to a floating point number returns 0.030029")
  {
    REQUIRE(integer12ToDouble(123) == 0.030029296875);
  }

  SECTION("Converting a 12 precision fixed point number 1843 to a floating point number returns 0.44995")
  {
    REQUIRE(integer12ToDouble(1843) == 0.449951171875);
  }

  SECTION("Converting a 15 precision fixed point number -12 to a floating point number returns 0.000366")
  {
    REQUIRE(integer15ToDouble(-12) == -0.0003662109375);
  }

  SECTION("Converting a 15 precision fixed point number 1 to a floating point number returns 0.000031")
  {
    REQUIRE(integer15ToDouble(1) == 0.000030517578125);
  }

  SECTION("Converting a 15 precision fixed point number 123 to a floating point number returns 0.003754")
  {
    REQUIRE(integer15ToDouble(123) == 0.003753662109375);
  }

  SECTION("Converting a 15 precision fixed point number 1843 to a floating point number returns 0.056244")
  {
    REQUIRE(integer15ToDouble(1843) == 0.056243896484375);
  }
}
