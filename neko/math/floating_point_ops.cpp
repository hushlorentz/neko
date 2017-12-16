#include "floating_point_ops.hpp"
#include <cfloat>

float convertFromIEEE(float value, uint8_t * resultFlags)
{
  num_32bits num;
  num.float_representation = value;

  if (num.components.exponent == 0xff)
  {
    num.components.exponent = FP_MAX_EXPONENT;
    num.components.mantissa = FP_MAX_MANTISSA;
    *resultFlags |= FP_FLAG_OVERFLOW;

    return num.float_representation;
  }
  if (num.components.exponent == 0 && num.components.mantissa > 0)
  {
    num.components.exponent = 0;
    num.components.mantissa = 0;
    *resultFlags |= FP_FLAG_UNDERFLOW;

    return num.float_representation;
  }

  return value;
}

uint8_t getSignFromNumXORNum(float f1, float f2)
{
  num_32bits f1Num;
  num_32bits f2Num;

  f1Num.float_representation = f1;
  f2Num.float_representation = f2;

  return f1Num.components.sign ^ f2Num.components.sign;
}

float processZeroDivZero(float f1, float f2)
{
  num_32bits result;

  result.float_representation = 0;
  result.components.sign = getSignFromNumXORNum(f1, f2);

  return result.float_representation;
}

float processNumDivZero(float f1, float f2)
{
  num_32bits result;

  result.components.exponent = FP_MAX_EXPONENT;
  result.components.mantissa = FP_MAX_MANTISSA;
  result.components.sign = getSignFromNumXORNum(f1, f2);

  return result.float_representation;
}

float addFP(float f1, float f2, uint8_t * resultFlags)
{
  return convertFromIEEE(f1 + f2, resultFlags);
}

float mulFP(float f1, float f2, uint8_t * resultFlags)
{
  return convertFromIEEE(f1 * f2, resultFlags);
}

float divFP(float f1, float f2, uint8_t * resultFlags)
{
  if (f2 == 0.0f)
  {
    if (f1 == 0.0f)
    {
      *resultFlags |= FP_FLAG_I_BIT;
      return processZeroDivZero(f1, f2);
    }
    else
    {
      *resultFlags |= FP_FLAG_D_BIT;
      return processNumDivZero(f1, f2);
    }
  }

  return convertFromIEEE(f1 / f2, resultFlags);
}

float subFP(float f1, float f2, uint8_t * resultFlags)
{
  return convertFromIEEE(f1 - f2, resultFlags);
}

uint32_t floatToInteger0(float f)
{
  return (uint32_t)f;
}

uint32_t floatToInteger4(float f)
{
  return (uint32_t)(f * 16);
}

uint32_t floatToInteger12(float f)
{
  return (uint32_t)(f * 4096);
}

uint32_t floatToInteger15(float f)
{
  return (uint32_t)(f * 32768);
}
