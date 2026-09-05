#ifndef FLOATING_POINT_OPS
#define FLOATING_POINT_OPS

#define FP_FLAG_OVERFLOW 0x1
#define FP_FLAG_UNDERFLOW 0x2
#define FP_FLAG_I_BIT 0x4
#define FP_FLAG_D_BIT 0x8

#define FP_MAX_MANTISSA 0x7fffff
#define FP_MAX_EXPONENT 128
#define FP_SIGN_BIT 0x80000000u
#define VU_FLOAT_ONE_BITS 0x3f800000u

#include <cstdint>
#include <cmath>

#include "fp_register.hpp"

struct EEFloatResult
{
  std::uint32_t bits;
  std::uint8_t flags;
};

using VUFloatResult = EEFloatResult;

enum class EEFloatClassification : std::uint8_t
{
  Zero,
  Normal,
  Finite = Normal,
  ExtendedFinite
};

enum class IEEEFloatEncoding : std::uint8_t
{
  Zero,
  Subnormal,
  Normal,
  Infinity,
  NaN
};

struct EEFloatDecomposition
{
  bool negative;
  std::uint8_t encodedExponent;
  std::int16_t unbiasedExponent;
  std::uint32_t mantissa;
  EEFloatClassification classification;
  IEEEFloatEncoding ieeeEncoding;
};

using VUFloatClassification = EEFloatClassification;
using VUFloatDecomposition = EEFloatDecomposition;

EEFloatDecomposition decomposeEEFloat(std::uint32_t bits);
VUFloatDecomposition decomposeVUFloat(std::uint32_t bits);
EEFloatResult normalizeEEFloat(
  bool negative,
  std::uint64_t magnitude,
  std::int16_t leastSignificantBitExponent);
VUFloatResult addFPRaw(std::uint32_t d1Bits, std::uint32_t d2Bits);
VUFloatResult mulFPRaw(std::uint32_t d1Bits, std::uint32_t d2Bits);
VUFloatResult divFPRaw(std::uint32_t d1Bits, std::uint32_t d2Bits);
VUFloatResult maxFPRaw(std::uint32_t d1Bits, std::uint32_t d2Bits);
VUFloatResult minFPRaw(std::uint32_t d1Bits, std::uint32_t d2Bits);
EEFloatResult maxEEFloatRaw(
  std::uint32_t fsBits,
  std::uint32_t ftBits);
EEFloatResult minEEFloatRaw(
  std::uint32_t fsBits,
  std::uint32_t ftBits);
EEFloatResult sqrtEEFloatRaw(std::uint32_t bits);
EEFloatResult rsqrtEEFloatRaw(
  std::uint32_t numeratorBits,
  std::uint32_t radicandBits);
EEFloatResult convertEEFloatToWordRaw(std::uint32_t bits);
VUFloatResult sqrtFPRaw(std::uint32_t bits);
VUFloatResult rsqrtFPRaw(
  std::uint32_t numeratorBits,
  std::uint32_t radicandBits);
VUFloatResult subFPRaw(std::uint32_t d1Bits, std::uint32_t d2Bits);
std::uint32_t floatToFixedRaw(
  std::uint32_t bits,
  std::uint8_t fractionalBits);
std::uint32_t fixedToFloatRaw(
  std::uint32_t bits,
  std::uint8_t fractionalBits);

double addFP(double d1, double d2, uint8_t * resultFlags);
double mulFP(double d1, double d2, uint8_t * resultFlags);
double divFP(double d1, double d2, uint8_t * resultFlags);
double subFP(double d1, double d2, uint8_t * resultFlags);
std::int32_t doubleToInteger0(double d);
std::int32_t doubleToInteger4(double d);
std::int32_t doubleToInteger12(double d);
std::int32_t doubleToInteger15(double d);
double integer0ToDouble(std::int32_t i);
double integer4ToDouble(std::int32_t i);
double integer12ToDouble(std::int32_t i);
double integer15ToDouble(std::int32_t i);

#endif
