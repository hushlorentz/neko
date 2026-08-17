#include "floating_point_ops.hpp"
#include <cfloat>

namespace
{
  double maxVUValue()
  {
    return std::ldexp(2.0 - std::ldexp(1.0, -23), FP_MAX_EXPONENT);
  }
}

double convertFromIEEE(double value, uint8_t * resultFlags)
{
  if (std::abs(value) >= std::ldexp(1.0, FP_MAX_EXPONENT))
  {
    *resultFlags |= FP_FLAG_OVERFLOW;

    return std::copysign(maxVUValue(), value);
  }
  if (value != 0 && std::abs(value) < std::ldexp(1.0, -126))
  {
    *resultFlags |= FP_FLAG_UNDERFLOW;

    return std::copysign(0.0, value);
  }

  return value;
}

uint8_t getSignFromNumXORNum(double d1, double d2)
{
  return std::signbit(d1) ^ std::signbit(d2);
}

double processZeroDivZero(double d1, double d2)
{
  return std::copysign(0.0, getSignFromNumXORNum(d1, d2) ? -1.0 : 1.0);
}

double processNumDivZero(double d1, double d2)
{
  return std::copysign(maxVUValue(), getSignFromNumXORNum(d1, d2) ? -1.0 : 1.0);
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

std::int32_t doubleToInteger0(double d)
{
  return static_cast<std::int32_t>(d);
}

std::int32_t doubleToInteger4(double d)
{
  return static_cast<std::int32_t>(d * 16);
}

std::int32_t doubleToInteger12(double d)
{
  return static_cast<std::int32_t>(d * 4096);
}

std::int32_t doubleToInteger15(double d)
{
  return static_cast<std::int32_t>(d * 32768);
}

double integer0ToDouble(std::int32_t i)
{
  return (double)i;
}

double integer4ToDouble(std::int32_t i)
{
  return i / 16.0f;
}

double integer12ToDouble(std::int32_t i)
{
  return i / 4096.0f;
}

double integer15ToDouble(std::int32_t i)
{
  return i / 32768.0f;
}
