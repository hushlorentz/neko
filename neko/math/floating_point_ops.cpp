#include "floating_point_ops.hpp"
#include <cfloat>

double convertFromIEEE(double value, uint8_t * resultFlags)
{
  Double num;
  num.d = value;

  if (static_cast<int>(num.exponent) - FP_EXP_BIAS >= FP_MAX_EXPONENT)
  {
    num.exponent = FP_EXP_BIAS + FP_MAX_EXPONENT;
    num.mantissa = FP_MAX_MANTISSA;
    *resultFlags |= FP_FLAG_OVERFLOW;

    return num.d;
  }
  if (num.exponent == 0 && num.mantissa > 0)
  {
    num.exponent = 0;
    num.mantissa = 0;
    *resultFlags |= FP_FLAG_UNDERFLOW;

    return num.d;
  }

  return value;
}

uint8_t getSignFromNumXORNum(double d1, double d2)
{
  Double f1Num;
  Double f2Num;

  f1Num.d = d1;
  f2Num.d = d2;

  return f1Num.sign ^ f2Num.sign;
}

double processZeroDivZero(double d1, double d2)
{
  Double result;

  result.d = 0;
  result.sign = getSignFromNumXORNum(d1, d2);

  return result.d;
}

double processNumDivZero(double d1, double d2)
{
  Double result;

  result.mantissa = FP_MAX_MANTISSA;
  result.exponent = FP_EXP_BIAS + FP_MAX_EXPONENT;
  result.sign = getSignFromNumXORNum(d1, d2);

  return result.d;
}

double addFP(double d1, double d2, uint8_t * resultFlags)
{
  return convertFromIEEE(d1 + d2, resultFlags);
}

double mulFP(double d1, double d2, uint8_t * resultFlags)
{
  return convertFromIEEE(d1 * d2, resultFlags);
}

double divFP(double d1, double d2, uint8_t * resultFlags)
{
  if (d2 == 0.0f)
  {
    if (d1 == 0.0f)
    {
      *resultFlags |= FP_FLAG_I_BIT;
      return processZeroDivZero(d1, d2);
    }
    else
    {
      *resultFlags |= FP_FLAG_D_BIT;
      return processNumDivZero(d1, d2);
    }
  }

  return convertFromIEEE(d1 / d2, resultFlags);
}

double subFP(double d1, double d2, uint8_t * resultFlags)
{
  return convertFromIEEE(d1 - d2, resultFlags);
}

std::int64_t doubleToInteger0(double d)
{
  return static_cast<std::int64_t>(d);
}

std::int64_t doubleToInteger4(double d)
{
  return static_cast<std::int64_t>(d * 16);
}

std::int64_t doubleToInteger12(double d)
{
  return static_cast<std::int64_t>(d * 4096);
}

std::int64_t doubleToInteger15(double d)
{
  return static_cast<std::int64_t>(d * 32768);
}

double integer0ToDouble(std::int64_t i)
{
  return (double)i;
}

double integer4ToDouble(std::int64_t i)
{
  return i / 16.0f;
}

double integer12ToDouble(std::int64_t i)
{
  return i / 4096.0f;
}

double integer15ToDouble(std::int64_t i)
{
  return i / 32768.0f;
}
