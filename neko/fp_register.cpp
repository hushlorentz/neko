#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <limits>

#include "bit_ops.hpp"
#include "floating_point_ops.hpp"
#include "fp_register.hpp"

using namespace std;

namespace
{
  std::uint32_t floatBits(float value)
  {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
  }

  double hostValue(std::uint32_t bits)
  {
    const bool negative = (bits & FP_SIGN_BIT) != 0;
    const std::uint32_t exponent = (bits >> 23) & 0xff;
    const std::uint32_t mantissa = bits & FP_MAX_MANTISSA;

    if (exponent == 0)
    {
      if (mantissa == 0)
      {
        return std::copysign(0.0, negative ? -1.0 : 1.0);
      }

      return std::copysign(std::numeric_limits<double>::min(), negative ? -1.0 : 1.0);
    }

    const double significand = 1.0 + static_cast<double>(mantissa) / (1u << 23);
    return std::copysign(std::ldexp(significand, static_cast<int>(exponent) - 127), negative ? -1.0 : 1.0);
  }

  std::uint32_t vuBits(double value)
  {
    const std::uint32_t sign = std::signbit(value) ? FP_SIGN_BIT : 0;
    const double magnitude = std::abs(value);

    if (!std::isfinite(value))
    {
      return floatBits(static_cast<float>(value));
    }

    if (magnitude == 0)
    {
      return sign;
    }

    if (magnitude <= FLT_MAX)
    {
      const std::uint32_t bits = floatBits(static_cast<float>(value));
      return (bits & ~FP_SIGN_BIT) == 0 ? sign | 1u : bits;
    }

    int exponent;
    const double fraction = std::frexp(magnitude, &exponent) * 2.0;
    const int biasedExponent = exponent - 1 + 127;
    if (biasedExponent > 255)
    {
      return sign | 0x7fffffffu;
    }

    const double scaledMantissa = (fraction - 1.0) * (1u << 23);
    const std::uint32_t mantissa = std::min<std::uint32_t>(
      static_cast<std::uint32_t>(scaledMantissa),
      FP_MAX_MANTISSA);
    return sign | (static_cast<std::uint32_t>(biasedExponent) << 23) | mantissa;
  }
}

VUFloat::VUFloat() : rawBits(0)
{
}

VUFloat::VUFloat(double value) : rawBits(vuBits(value))
{
}

VUFloat &VUFloat::operator=(double value)
{
  rawBits = vuBits(value);
  return *this;
}

VUFloat::operator double() const
{
  return hostValue(rawBits);
}

std::uint32_t VUFloat::bits() const
{
  return rawBits;
}

void VUFloat::setBits(std::uint32_t value)
{
  rawBits = value;
}

std::int32_t VUFloat::signedValue() const
{
  std::int32_t value;
  std::memcpy(&value, &rawBits, sizeof(value));
  return value;
}

void VUFloat::setSignedValue(std::int32_t value)
{
  std::memcpy(&rawBits, &value, sizeof(rawBits));
}

bool VUFloat::isNegative() const
{
  return (rawBits & FP_SIGN_BIT) != 0;
}

void VUFloat::toggleSign()
{
  rawBits ^= FP_SIGN_BIT;
}

FPRegister::FPRegister() : x(0), y(0), z(0), w(0), xResultFlags(0), yResultFlags(0), zResultFlags(0), wResultFlags(0)
{
}

FPRegister::FPRegister(double x, double y, double z, double w) : x(x), y(y), z(z), w(w), xResultFlags(0), yResultFlags(0), zResultFlags(0), wResultFlags(0)
{
}

void FPRegister::load(double newX, double newY, double newZ, double newW)
{
  x = newX;
  y = newY;
  z = newZ;
  w = newW;
}

void FPRegister::copyFrom(FPRegister * srcReg)
{
  x = srcReg->x;
  y = srcReg->y;
  z = srcReg->z;
  w = srcReg->w;
  xResultFlags = srcReg->xResultFlags;
  yResultFlags = srcReg->yResultFlags;
  zResultFlags = srcReg->zResultFlags;
  wResultFlags = srcReg->wResultFlags;
}

void FPRegister::copyFieldsFrom(FPRegister * srcReg, uint8_t fieldMask)
{
  if (hasFlag(fieldMask, FP_REGISTER_X_FIELD))
  {
    x = srcReg->x;
    xResultFlags = srcReg->xResultFlags;
  }
  if (hasFlag(fieldMask, FP_REGISTER_Y_FIELD))
  {
    y = srcReg->y;
    yResultFlags = srcReg->yResultFlags;
  }
  if (hasFlag(fieldMask, FP_REGISTER_Z_FIELD))
  {
    z = srcReg->z;
    zResultFlags = srcReg->zResultFlags;
  }
  if (hasFlag(fieldMask, FP_REGISTER_W_FIELD))
  {
    w = srcReg->w;
    wResultFlags = srcReg->wResultFlags;
  }
}

void FPRegister::storeAdd(FPRegister * r1, FPRegister * r2, uint8_t fieldMask)
{
  clearFlags();
  x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? addFP(r1->x, r2->x, &xResultFlags) : x;
  y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? addFP(r1->y, r2->y, &yResultFlags) : y;
  z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? addFP(r1->z, r2->z, &zResultFlags) : z;
  w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? addFP(r1->w, r2->w, &wResultFlags) : w;
}

void FPRegister::storeSub(FPRegister * r1, FPRegister * r2, uint8_t fieldMask)
{
  clearFlags();
  x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? subFP(r1->x, r2->x, &xResultFlags) : x;
  y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? subFP(r1->y, r2->y, &yResultFlags) : y;
  z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? subFP(r1->z, r2->z, &zResultFlags) : z;
  w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? subFP(r1->w, r2->w, &wResultFlags) : w;
}

void FPRegister::storeMul(FPRegister * r1, FPRegister * r2, uint8_t fieldMask)
{
  clearFlags();
  x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? mulFP(r1->x, r2->x, &xResultFlags) : x;
  y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? mulFP(r1->y, r2->y, &yResultFlags) : y;
  z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? mulFP(r1->z, r2->z, &zResultFlags) : z;
  w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? mulFP(r1->w, r2->w, &wResultFlags) : w;
}

void FPRegister::storeDiv(FPRegister * r1, FPRegister * r2, uint8_t fieldMask)
{
  clearFlags();
  x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? divFP(r1->x, r2->x, &xResultFlags) : x;
  y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? divFP(r1->y, r2->y, &yResultFlags) : y;
  z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? divFP(r1->z, r2->z, &zResultFlags) : z;
  w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? divFP(r1->w, r2->w, &wResultFlags) : w;
}

void FPRegister::storeAbs(FPRegister * source, uint8_t fieldMask)
{
  x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? abs(source->x) : x;
  y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? abs(source->y) : y;
  z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? abs(source->z) : z;
  w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? abs(source->w) : w;
}

void FPRegister::storeAddDouble(FPRegister * r1, double value, uint8_t fieldMask)
{
  clearFlags();
  x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? addFP(r1->x, value, &xResultFlags) : x;
  y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? addFP(r1->y, value, &yResultFlags) : y;
  z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? addFP(r1->z, value, &zResultFlags) : z;
  w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? addFP(r1->w, value, &wResultFlags) : w;
}

void FPRegister::storeMulDouble(FPRegister * r1, double value, uint8_t fieldMask)
{
  clearFlags();
  x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? mulFP(r1->x, value, &xResultFlags) : x;
  y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? mulFP(r1->y, value, &yResultFlags) : y;
  z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? mulFP(r1->z, value, &zResultFlags) : z;
  w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? mulFP(r1->w, value, &wResultFlags) : w;
}

void FPRegister::storeSubDouble(FPRegister * r1, double value, uint8_t fieldMask)
{
  clearFlags();
  x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? subFP(r1->x, value, &xResultFlags) : x;
  y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? subFP(r1->y, value, &yResultFlags) : y;
  z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? subFP(r1->z, value, &zResultFlags) : z;
  w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? subFP(r1->w, value, &wResultFlags) : w;
}

void FPRegister::storeMax(FPRegister * r1, FPRegister * r2, uint8_t fieldMask)
{
  clearFlags();
  x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? max(r1->x, r2->x) : x;
  y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? max(r1->y, r2->y) : y;
  z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? max(r1->z, r2->z) : z;
  w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? max(r1->w, r2->w) : w;
}

void FPRegister::storeMaxDouble(FPRegister * r1, double d, uint8_t fieldMask)
{
  clearFlags();
  x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? max(static_cast<double>(r1->x), d) : x;
  y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? max(static_cast<double>(r1->y), d) : y;
  z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? max(static_cast<double>(r1->z), d) : z;
  w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? max(static_cast<double>(r1->w), d) : w;
}

void FPRegister::storeMin(FPRegister * r1, FPRegister * r2, uint8_t fieldMask)
{
  clearFlags();
  x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? min(r1->x, r2->x) : x;
  y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? min(r1->y, r2->y) : y;
  z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? min(r1->z, r2->z) : z;
  w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? min(r1->w, r2->w) : w;
}
void FPRegister::storeMinDouble(FPRegister * r1, double d, uint8_t fieldMask)
{
  clearFlags();
  x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? min(static_cast<double>(r1->x), d) : x;
  y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? min(static_cast<double>(r1->y), d) : y;
  z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? min(static_cast<double>(r1->z), d) : z;
  w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? min(static_cast<double>(r1->w), d) : w;
}

void FPRegister::storeOuterProduct(FPRegister * r1, FPRegister * r2)
{
  clearFlags();
  x = mulFP(r1->y, r2->z, &xResultFlags);
  y = mulFP(r1->z, r2->x, &yResultFlags);
  z = mulFP(r1->x, r2->y, &zResultFlags);
}

void FPRegister::toInt0(FPRegister * source, uint8_t fieldMask)
{
  toInt(source, fieldMask, &doubleToInteger0);
}

void FPRegister::toInt4(FPRegister * source, uint8_t fieldMask)
{
  toInt(source, fieldMask, &doubleToInteger4);
}

void FPRegister::toInt12(FPRegister * source, uint8_t fieldMask)
{
  toInt(source, fieldMask, &doubleToInteger12);
}

void FPRegister::toInt15(FPRegister * source, uint8_t fieldMask)
{
  toInt(source, fieldMask, &doubleToInteger15);
}

void FPRegister::toDouble0(FPRegister * source, uint8_t fieldMask)
{
  toDouble(source, fieldMask, &integer0ToDouble);
}

void FPRegister::toDouble4(FPRegister * source, uint8_t fieldMask)
{
  toDouble(source, fieldMask, &integer4ToDouble);
}

void FPRegister::toDouble12(FPRegister * source, uint8_t fieldMask)
{
  toDouble(source, fieldMask, &integer12ToDouble);
}

void FPRegister::toDouble15(FPRegister * source, uint8_t fieldMask)
{
  toDouble(source, fieldMask, &integer15ToDouble);
}

void FPRegister::toInt(FPRegister * source, uint8_t fieldMask, std::int32_t (*convertFunc)(double))
{
  if (hasFlag(fieldMask, FP_REGISTER_X_FIELD)) x.setSignedValue((*convertFunc)(source->x));
  if (hasFlag(fieldMask, FP_REGISTER_Y_FIELD)) y.setSignedValue((*convertFunc)(source->y));
  if (hasFlag(fieldMask, FP_REGISTER_Z_FIELD)) z.setSignedValue((*convertFunc)(source->z));
  if (hasFlag(fieldMask, FP_REGISTER_W_FIELD)) w.setSignedValue((*convertFunc)(source->w));
}

void FPRegister::toDouble(FPRegister * source, uint8_t fieldMask, double (*convertFunc)(std::int32_t))
{
  x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? (*convertFunc)(source->x.signedValue()) : x;
  y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? (*convertFunc)(source->y.signedValue()) : y;
  z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? (*convertFunc)(source->z.signedValue()) : z;
  w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? (*convertFunc)(source->w.signedValue()) : w;
}

void FPRegister::clearFlags()
{
  xResultFlags = 0;
  yResultFlags = 0;
  zResultFlags = 0;
  wResultFlags = 0;
};
