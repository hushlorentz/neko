#include <cfloat>

#include "catch.hpp"
#include "bit_ops.hpp"
#include "floating_point_ops.hpp"
#include "fp_register.hpp"

TEST_CASE("Testing the floating point conventions")
{
  uint8_t resultFlags = 0;

  SECTION("A VU float stores the IEEE single-precision bits for 1.0")
  {
    VUFloat value(1.0);

    REQUIRE(value.bits() == 0x3f800000u);
  }

  SECTION("A VU float stores FLT_MAX as the largest exponent-254 value")
  {
    VUFloat value(FLT_MAX);

    REQUIRE(value.bits() == 0x7f7fffffu);
  }
  
  SECTION("A VU float can represent exponent 128 without treating it as infinity")
  {
    VUFloat value;
    value.setBits(0x7f800000u);

    REQUIRE(static_cast<double>(value) > FLT_MAX);
  }

  SECTION("VU maximum round trips through its host conversion")
  {
    VUFloat maximum;
    maximum.setBits(0x7fffffffu);

    VUFloat roundTripped(static_cast<double>(maximum));

    REQUIRE(roundTripped.bits() == maximum.bits());
  }

  SECTION("Adding two VU maximum values overflows to VU maximum")
  {
    VUFloat maximum;
    maximum.setBits(0x7fffffffu);

    VUFloat result(addFP(maximum, maximum, &resultFlags));

    REQUIRE(result.bits() == 0x7fffffffu);
    REQUIRE(resultFlags == FP_FLAG_OVERFLOW);
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
    VUFloat result(mulFP(d1, d2, &resultFlags));

    REQUIRE(result.bits() == 0x7fffffffu);
    REQUIRE(resultFlags == FP_FLAG_OVERFLOW);
  }

  SECTION("Dividing two overflow results should result in 1")
  {
    double d1 = FLT_MAX;
    double d2 = 7.0;

    double d1Converted = mulFP(d1, d2, &resultFlags);
    double d2Converted = mulFP(d1, d2, &resultFlags);

    REQUIRE(divFP(d1Converted, d2Converted, &resultFlags) == 1);
  }

  SECTION("Multiplying a positive floating point number and negative floating point number that result in an overflow returns -MAX and sets the overflow flag.")
  {
    double d1 = FLT_MAX;
    double d2 = -7.0f;
    VUFloat result(mulFP(d1, d2, &resultFlags));

    REQUIRE(result.bits() == 0xffffffffu);
    REQUIRE(resultFlags == FP_FLAG_OVERFLOW);
  }

  SECTION("Dividing 0 by 0 returns 0 with the I bit flag set")
  {
    double d1 = 0;
    double d2 = 0;
    VUFloat result(divFP(d1, d2, &resultFlags));

    REQUIRE(result.bits() == 0);
    REQUIRE(resultFlags == FP_FLAG_I_BIT);
  }
  
  SECTION("Dividing -0 by -0 returns 0 with the I bit flag set")
  {
    double d1 = std::copysign(0.0, -1.0);
    double d2 = std::copysign(0.0, -1.0);
    VUFloat result(divFP(d1, d2, &resultFlags));

    REQUIRE(result.bits() == 0);
    REQUIRE(resultFlags == FP_FLAG_I_BIT);
  }

  SECTION("Dividing 0 by -0 returns -0 with the I bit flag set")
  {
    double d1 = 0;
    double d2 = std::copysign(0.0, -1.0);
    VUFloat result(divFP(d1, d2, &resultFlags));

    REQUIRE(result.bits() == FP_SIGN_BIT);
    REQUIRE(resultFlags == FP_FLAG_I_BIT);
  }

  SECTION("Dividing 5 by 0 returns MAX with the D bit flag set")
  {
    double d1 = 5;
    double d2 = 0;
    VUFloat result(divFP(d1, d2, &resultFlags));

    REQUIRE(result.bits() == 0x7fffffffu);
    REQUIRE(resultFlags == FP_FLAG_D_BIT);
  }

  SECTION("Dividing -5 by 0 returns -MAX with the D bit flag set and sign bit set")
  {
    double d1 = -5;
    double d2 = 0;
    VUFloat result(divFP(d1, d2, &resultFlags));

    REQUIRE(result.bits() == 0xffffffffu);
    REQUIRE(resultFlags == FP_FLAG_D_BIT);
  }

  SECTION("Dividing the smallest normalized value by 5 causes an underflow and returns 0 and sets the underflow flag")
  {
    double d1 = std::numeric_limits<double>::min();
    double d2 = 5;
    VUFloat result(divFP(d1, d2, &resultFlags));

    REQUIRE(result.bits() == 0);
    REQUIRE(resultFlags == FP_FLAG_UNDERFLOW);
  }

  SECTION("Dividing the smallest normalized value by -5 causes an underflow and returns 0 and sets the underflow flag and the sign bit is set.")
  {
    double d1 = std::numeric_limits<double>::min();
    double d2 = -5;
    VUFloat result(divFP(d1, d2, &resultFlags));

    REQUIRE(result.bits() == FP_SIGN_BIT);
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

TEST_CASE("Raw VU floating-point operation results")
{
  SECTION("Addition returns result bits and no exception flags")
  {
    VUFloatResult result = addFPRaw(0x3fc00000u, 0x40100000u);

    REQUIRE(result.bits == 0x40700000u);
    REQUIRE(result.flags == 0);
  }

  SECTION("Multiplication preserves the raw result sign")
  {
    VUFloatResult result = mulFPRaw(0xc0000000u, 0x40400000u);

    REQUIRE(result.bits == 0xc0c00000u);
    REQUIRE(result.flags == 0);
  }

  SECTION("Subtraction returns raw single-precision bits")
  {
    VUFloatResult result = subFPRaw(0x40b00000u, 0x3fc00000u);

    REQUIRE(result.bits == 0x40800000u);
    REQUIRE(result.flags == 0);
  }

  SECTION("Division returns raw single-precision bits")
  {
    VUFloatResult result = divFPRaw(0x40c00000u, 0x40000000u);

    REQUIRE(result.bits == 0x40400000u);
    REQUIRE(result.flags == 0);
  }

  SECTION("Overflow is returned alongside the saturated result")
  {
    VUFloatResult result = mulFPRaw(0x7fffffffu, 0x40000000u);

    REQUIRE(result.bits == 0x7fffffffu);
    REQUIRE(result.flags == FP_FLAG_OVERFLOW);
  }

  SECTION("Every operation starts with a fresh flag state")
  {
    REQUIRE(mulFPRaw(0x7fffffffu, 0x40000000u).flags ==
            FP_FLAG_OVERFLOW);
    REQUIRE(addFPRaw(0x3f800000u, 0x3f800000u).flags == 0);
  }
}

TEST_CASE("Exponent-zero VU operands are signed zero during calculations")
{
  SECTION("Multiplication preserves the flushed operand sign")
  {
    VUFloatResult result = mulFPRaw(0x00000001u, 0xc0000000u);

    REQUIRE(result.bits == FP_SIGN_BIT);
    REQUIRE(result.flags == 0);
  }

  SECTION("Two negative operands produce positive zero")
  {
    VUFloatResult result = mulFPRaw(0x807fffffu, 0xc0000000u);

    REQUIRE(result.bits == 0);
    REQUIRE(result.flags == 0);
  }

  SECTION("Flushing an input does not report result underflow")
  {
    VUFloatResult result = divFPRaw(0x80000001u, 0x40000000u);

    REQUIRE(result.bits == FP_SIGN_BIT);
    REQUIRE(result.flags == 0);
  }

  SECTION("An exponent-zero divisor is division by signed zero")
  {
    VUFloatResult result = divFPRaw(0x3f800000u, 0x80000001u);

    REQUIRE(result.bits == 0xffffffffu);
    REQUIRE(result.flags == FP_FLAG_D_BIT);
  }

  SECTION("Adding two negative exponent-zero operands returns negative zero")
  {
    VUFloatResult result = addFPRaw(0x80000001u, 0x807fffffu);

    REQUIRE(result.bits == FP_SIGN_BIT);
    REQUIRE(result.flags == 0);
  }
}

TEST_CASE("Exponent-255 encodings are finite VU operands")
{
  SECTION("Opposite exponent-255 powers cancel instead of producing NaN")
  {
    VUFloatResult result = addFPRaw(0x7f800000u, 0xff800000u);

    REQUIRE(result.bits == 0);
    REQUIRE(result.flags == 0);
  }

  SECTION("An IEEE NaN bit pattern participates in VU multiplication")
  {
    VUFloatResult result = mulFPRaw(0x7fc00000u, 0x3f000000u);

    REQUIRE(result.bits == 0x7f400000u);
    REQUIRE(result.flags == 0);
  }

  SECTION("The sign of an exponent-255 operand is preserved")
  {
    VUFloatResult result = mulFPRaw(0xffc00000u, 0x3f000000u);

    REQUIRE(result.bits == 0xff400000u);
    REQUIRE(result.flags == 0);
  }

  SECTION("Equal maximum VU values divide to one")
  {
    VUFloatResult result = divFPRaw(0x7fffffffu, 0x7fffffffu);

    REQUIRE(result.bits == 0x3f800000u);
    REQUIRE(result.flags == 0);
  }
}

TEST_CASE("VU exponent overflow and underflow boundaries")
{
  SECTION("The maximum VU value remains representable")
  {
    VUFloatResult result = mulFPRaw(0x7fffffffu, 0x3f800000u);

    REQUIRE(result.bits == 0x7fffffffu);
    REQUIRE(result.flags == 0);
  }

  SECTION("Values within exponent 255 do not overflow")
  {
    VUFloatResult result = mulFPRaw(0x7fc00000u, 0x3f800000u);

    REQUIRE(result.bits == 0x7fc00000u);
    REQUIRE(result.flags == 0);
  }

  SECTION("Requiring exponent 256 overflows to maximum")
  {
    VUFloatResult result = mulFPRaw(0x7f800000u, 0x40000000u);

    REQUIRE(result.bits == 0x7fffffffu);
    REQUIRE(result.flags == FP_FLAG_OVERFLOW);
  }

  SECTION("Overflow preserves the result sign")
  {
    VUFloatResult result = mulFPRaw(0xff800000u, 0x40000000u);

    REQUIRE(result.bits == 0xffffffffu);
    REQUIRE(result.flags == FP_FLAG_OVERFLOW);
  }

  SECTION("The minimum normalized VU value remains representable")
  {
    VUFloatResult result = mulFPRaw(0x00800000u, 0x3f800000u);

    REQUIRE(result.bits == 0x00800000u);
    REQUIRE(result.flags == 0);
  }

  SECTION("Requiring exponent negative 127 underflows to zero")
  {
    VUFloatResult result = mulFPRaw(0x00800000u, 0x3f000000u);

    REQUIRE(result.bits == 0);
    REQUIRE(result.flags == FP_FLAG_UNDERFLOW);
  }

  SECTION("Underflow preserves the result sign")
  {
    VUFloatResult result = mulFPRaw(0x80800000u, 0x3f000000u);

    REQUIRE(result.bits == FP_SIGN_BIT);
    REQUIRE(result.flags == FP_FLAG_UNDERFLOW);
  }

  SECTION("Exact cancellation does not underflow")
  {
    VUFloatResult result = subFPRaw(0x00800000u, 0x00800000u);

    REQUIRE(result.bits == 0);
    REQUIRE(result.flags == 0);
  }
}
