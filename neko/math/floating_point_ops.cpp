#include "floating_point_ops.hpp"
#include <cfloat>

float convertFromIEEE(float value, uint8_t * resultFlags)
{
  //deal with sign bit for returns
  num_32bits num;
  num.float_representation = value;

  if (num.components.exponent == 0xff)
  {
    num.components.exponent = 0x80;
    num.components.mantissa = 0x2;
    *resultFlags |= FP_FLAG_OVERFLOW;

    return num.float_representation;
  }
  if (num.components.exponent == 0 && num.components.mantissa > 0)
  {
    *resultFlags |= FP_FLAG_UNDERFLOW;
    return 0;
  }

  return value;
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
  //deal with 0/0 and x/0 | x != 0
  return convertFromIEEE(f1 / f2, resultFlags);
}

float subFP(float f1, float f2, uint8_t * resultFlags)
{
  return convertFromIEEE(f1 - f2, resultFlags);
}

float adjustForOverflow(double value, uint8_t * resultFlags)
{
  double adjustedValue = value;

  if (adjustedValue > FLT_MAX)
  {
    adjustedValue = FLT_MAX;
    *resultFlags |= FP_FLAG_OVERFLOW;
  }

  return (float)adjustedValue;
}

float adjustForUnderflow(double value, uint8_t * resultFlags)
{
  double adjustedValue = value;

  if (adjustedValue < FLT_MIN)
  {
    adjustedValue = 0;
    *resultFlags |= FP_FLAG_UNDERFLOW;
  }

  return (float)adjustedValue;
}
