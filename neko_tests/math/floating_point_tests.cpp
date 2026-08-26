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

TEST_CASE("VU add subtract and multiply truncate to 24 bits")
{
  SECTION("Addition discards bits below the result significand")
  {
    VUFloatResult result = addFPRaw(0x3f800000u, 0x33c00000u);

    REQUIRE(result.bits == 0x3f800000u);
    REQUIRE(result.flags == 0);
  }

  SECTION("Subtraction truncates toward zero across an exponent boundary")
  {
    VUFloatResult result = subFPRaw(0x3f800000u, 0x33000000u);

    REQUIRE(result.bits == 0x3f7fffffu);
    REQUIRE(result.flags == 0);
  }

  SECTION("A distant subtraction still contributes a borrow")
  {
    VUFloatResult result = subFPRaw(0x3f800000u, 0x00800000u);

    REQUIRE(result.bits == 0x3f7fffffu);
    REQUIRE(result.flags == 0);
  }

  SECTION("Negative results also truncate toward zero")
  {
    VUFloatResult result = addFPRaw(0xbf800000u, 0x00800000u);

    REQUIRE(result.bits == 0xbf7fffffu);
    REQUIRE(result.flags == 0);
  }

  SECTION("Multiplication truncates rather than rounding to nearest")
  {
    VUFloatResult result = mulFPRaw(0x3f800001u, 0x3fc00000u);

    REQUIRE(result.bits == 0x3fc00001u);
    REQUIRE(result.flags == 0);
  }

  SECTION("Multiplication preserves the truncated result sign")
  {
    VUFloatResult result = mulFPRaw(0xbf800001u, 0x3fc00000u);

    REQUIRE(result.bits == 0xbfc00001u);
    REQUIRE(result.flags == 0);
  }

  SECTION("Cancellation below the minimum exponent underflows")
  {
    VUFloatResult result = subFPRaw(0x00800001u, 0x00800000u);

    REQUIRE(result.bits == 0);
    REQUIRE(result.flags == FP_FLAG_UNDERFLOW);
  }
}

TEST_CASE("VU fixed-point conversions use raw saturation and truncation")
{
  SECTION("FTOI flushes exponent-zero encodings without preserving sign")
  {
    REQUIRE(floatToFixedRaw(0x00000001u, 15) == 0x00000000u);
    REQUIRE(floatToFixedRaw(0x807fffffu, 15) == 0x00000000u);
  }

  SECTION("FTOI truncates toward zero")
  {
    REQUIRE(floatToFixedRaw(0x3ee66666u, 4) == 0x00000007u);
    REQUIRE(floatToFixedRaw(0xbee66666u, 4) == 0xfffffff9u);
  }

  SECTION("FTOI0 preserves its largest in-range VU values")
  {
    REQUIRE(floatToFixedRaw(0x4effffffu, 0) == 0x7fffff80u);
    REQUIRE(floatToFixedRaw(0xcf000000u, 0) == 0x80000000u);
  }

  SECTION("FTOI0 saturates positive and negative overflow without flags")
  {
    REQUIRE(floatToFixedRaw(0x4f000000u, 0) == 0x7fffffffu);
    REQUIRE(floatToFixedRaw(0xcf000001u, 0) == 0x80000000u);
    REQUIRE(floatToFixedRaw(0x7fffffffu, 0) == 0x7fffffffu);
    REQUIRE(floatToFixedRaw(0xffffffffu, 0) == 0x80000000u);
  }

  SECTION("FTOI15 applies scaling before saturation")
  {
    REQUIRE(floatToFixedRaw(0x477fffffu, 15) == 0x7fffff80u);
    REQUIRE(floatToFixedRaw(0x47800000u, 15) == 0x7fffffffu);
    REQUIRE(floatToFixedRaw(0xc7800000u, 15) == 0x80000000u);
  }

  SECTION("ITOF0 truncates integers wider than 24 significant bits")
  {
    REQUIRE(fixedToFloatRaw(0x01000001u, 0) == 0x4b800000u);
    REQUIRE(fixedToFloatRaw(0xfeffffffu, 0) == 0xcb800000u);
    REQUIRE(fixedToFloatRaw(0x7fffffffu, 0) == 0x4effffffu);
    REQUIRE(fixedToFloatRaw(0x80000000u, 0) == 0xcf000000u);
  }

  SECTION("ITOF15 handles the complete signed fixed-point range")
  {
    REQUIRE(fixedToFloatRaw(0x00000001u, 15) == 0x38000000u);
    REQUIRE(fixedToFloatRaw(0x7fffffffu, 15) == 0x477fffffu);
    REQUIRE(fixedToFloatRaw(0x80000000u, 15) == 0xc7800000u);
  }
}

TEST_CASE("VU raw arithmetic regression corpus")
{
  using RawOperation = VUFloatResult (*)(std::uint32_t, std::uint32_t);
  struct ArithmeticVector
  {
    const char *name;
    RawOperation operation;
    std::uint32_t d1Bits;
    std::uint32_t d2Bits;
    std::uint32_t resultBits;
    std::uint8_t resultFlags;
  };

  const ArithmeticVector vectors[] = {
    {"add carry at encoded exponent 65", addFPRaw, 0x20ffffffu, 0x20ffffffu, 0x217fffffu, 0x0},
    {"add carry at encoded exponent 129", addFPRaw, 0x40ffffffu, 0x40ffffffu, 0x417fffffu, 0x0},
    {"add carry at encoded exponent 193", addFPRaw, 0x60ffffffu, 0x60ffffffu, 0x617fffffu, 0x0},
    {"add opposite signs across 64-bit alignment", addFPRaw, 0x20923456u, 0x80ffffffu, 0x20923455u, 0x0},
    {"add opposite signs across 65-bit alignment", addFPRaw, 0x40e54321u, 0xa0222222u, 0x40e54320u, 0x0},
    {"add opposite signs across 129-bit alignment", addFPRaw, 0x60b45678u, 0xa0765432u, 0x60b45677u, 0x0},
    {"add opposite signs across 191-bit alignment", addFPRaw, 0x7fa22222u, 0xa0111111u, 0x7fa22221u, 0x0},
    {"add adjacent significands renormalizes", addFPRaw, 0x20923457u, 0xa0923456u, 0x15000000u, 0x0},
    {"add adjacent exponent-129 significands", addFPRaw, 0x40ffffffu, 0xc0fffffeu, 0x35000000u, 0x0},
    {"add minimum adjacent values underflow", addFPRaw, 0x00800001u, 0x80800000u, 0x00000000u, 0x2},
    {"add negative exponent-zero operands", addFPRaw, 0x80000001u, 0x807fffffu, 0x80000000u, 0x0},
    {"add exponent-255 values overflow", addFPRaw, 0x7f800000u, 0x7f800000u, 0x7fffffffu, 0x1},
    {"sub borrow across 64-bit alignment", subFPRaw, 0x20a34567u, 0x00f65432u, 0x20a34566u, 0x0},
    {"sub borrow across 128-bit alignment", subFPRaw, 0x40c56789u, 0x00923456u, 0x40c56788u, 0x0},
    {"sub borrow across 192-bit alignment", subFPRaw, 0x60d6789au, 0x00a34567u, 0x60d67899u, 0x0},
    {"sub borrow from exponent 255", subFPRaw, 0x7fb45678u, 0x00b45678u, 0x7fb45677u, 0x0},
    {"sub adjacent exponent-65 significands", subFPRaw, 0x20923457u, 0x20923456u, 0x15000000u, 0x0},
    {"sub adjacent exponent-129 significands", subFPRaw, 0x40f00001u, 0x40f00000u, 0x35000000u, 0x0},
    {"sub exact exponent-255 cancellation", subFPRaw, 0x7fffffffu, 0x7fffffffu, 0x00000000u, 0x0},
    {"sub negative zero from positive zero", subFPRaw, 0x80000001u, 0x00000001u, 0x80000000u, 0x0},
    {"sub negative operand becomes addition", subFPRaw, 0x3f923456u, 0xbf654321u, 0x40026af3u, 0x0},
    {"sub larger value produces negative result", subFPRaw, 0x3f111111u, 0x3fa22222u, 0xbf333333u, 0x0},
    {"sub adjacent minimum values underflow", subFPRaw, 0x80800001u, 0x80800000u, 0x80000000u, 0x2},
    {"sub negative maximum overflows", subFPRaw, 0xff800000u, 0x7f800000u, 0xffffffffu, 0x1},
    {"mul low-half truncation", mulFPRaw, 0x3f800001u, 0x3fc00000u, 0x3fc00001u, 0x0},
    {"mul high product normalization", mulFPRaw, 0x3fffffffu, 0x3ffffffeu, 0x407ffffdu, 0x0},
    {"mul negative truncation", mulFPRaw, 0xbf923457u, 0x40654321u, 0xc082ef28u, 0x0},
    {"mul exponent-255 by small finite", mulFPRaw, 0x7fc00000u, 0x00a00000u, 0x40f00000u, 0x0},
    {"mul exponent-255 remains finite", mulFPRaw, 0x7f923456u, 0x3f800000u, 0x7f923456u, 0x0},
    {"mul maximum by one", mulFPRaw, 0x7fffffffu, 0x3f800000u, 0x7fffffffu, 0x0},
    {"mul maximum overflow", mulFPRaw, 0x7fffffffu, 0x40000000u, 0x7fffffffu, 0x1},
    {"mul negative overflow", mulFPRaw, 0xffc00000u, 0x40400000u, 0xffffffffu, 0x1},
    {"mul minimum normalized by one", mulFPRaw, 0x00800000u, 0x3f800000u, 0x00800000u, 0x0},
    {"mul minimum normalized underflows", mulFPRaw, 0x00800000u, 0x3f000000u, 0x00000000u, 0x2},
    {"mul negative underflow", mulFPRaw, 0x80c00000u, 0x3f000000u, 0x80000000u, 0x2},
    {"mul exponent-zero uses signed zero", mulFPRaw, 0x807fffffu, 0xffffffffu, 0x00000000u, 0x0}
  };

  for (const ArithmeticVector &vector : vectors)
  {
    CAPTURE(vector.name);
    const VUFloatResult result =
      vector.operation(vector.d1Bits, vector.d2Bits);

    REQUIRE(result.bits == vector.resultBits);
    REQUIRE(result.flags == vector.resultFlags);
  }
}

TEST_CASE("VU raw values expose their arithmetic classification")
{
  SECTION("Exponent-zero encodings are classified as signed zero")
  {
    VUFloatDecomposition value = decomposeVUFloat(0x807fffffu);

    REQUIRE(value.negative);
    REQUIRE(value.encodedExponent == 0);
    REQUIRE(value.unbiasedExponent == -127);
    REQUIRE(value.mantissa == 0x7fffffu);
    REQUIRE(value.classification == VUFloatClassification::Zero);
  }

  SECTION("Ordinary finite values expose their raw components")
  {
    VUFloatDecomposition value = decomposeVUFloat(0x3fc12345u);

    REQUIRE_FALSE(value.negative);
    REQUIRE(value.encodedExponent == 127);
    REQUIRE(value.unbiasedExponent == 0);
    REQUIRE(value.mantissa == 0x412345u);
    REQUIRE(value.classification == VUFloatClassification::Finite);
  }

  SECTION("Exponent-255 encodings are extended finite VU values")
  {
    VUFloatDecomposition value = decomposeVUFloat(0xffc00000u);

    REQUIRE(value.negative);
    REQUIRE(value.encodedExponent == 255);
    REQUIRE(value.unbiasedExponent == 128);
    REQUIRE(value.mantissa == 0x400000u);
    REQUIRE(value.classification ==
            VUFloatClassification::ExtendedFinite);
  }
}
