#include "floating_point_ops.hpp"
#include <cfloat>

float convertFromIEEE(float f, uint8_t * resultFlags)
{
  //deal with sign bit for returns
  float_cast fu;
  fu.f = f;

  if (fu.parts.exponent == 0xff)
  {
    fu.parts.exponent = 0x80;
    fu.parts.mantissa = 0x2;
    *resultFlags |= FP_FLAG_OVERFLOW;

    return fu.f;
  }
  if (fu.parts.exponent == 0 && fu.parts.mantissa > 0)
  {
    *resultFlags |= FP_FLAG_UNDERFLOW;
    return 0;
  }

  return f;
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
