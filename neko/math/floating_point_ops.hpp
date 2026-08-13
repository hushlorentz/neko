#ifndef FLOATING_POINT_OPS
#define FLOATING_POINT_OPS

#define FP_FLAG_OVERFLOW 0x1
#define FP_FLAG_UNDERFLOW 0x2
#define FP_FLAG_I_BIT 0x4
#define FP_FLAG_D_BIT 0x8

#define FP_MAX_MANTISSA 0x7fffff
#define FP_MAX_EXPONENT 128
#define FP_EXP_BIAS 1023
#define FP_MAX_EXPONENT_WITH_BIAS FP_EXP_BIAS + FP_MAX_EXPONENT
#define FP_SIGN_POS 0
#define FP_SIGN_NEG 1
#define FP_SIGN_BIT 0x8000000000000000

#include <cstdint>
#include <cmath>

#include "fp_register.hpp"

union Double {
  double d;
  struct {
    std::uint64_t mantissa : 52;
    std::uint64_t exponent : 11;
    std::uint64_t sign : 1;
  };
  Double() : d(0.0) {}
};

double addFP(double d1, double d2, uint8_t * resultFlags);
double mulFP(double d1, double d2, uint8_t * resultFlags);
double divFP(double d1, double d2, uint8_t * resultFlags);
double subFP(double d1, double d2, uint8_t * resultFlags);
std::int64_t doubleToInteger0(double d);
std::int64_t doubleToInteger4(double d);
std::int64_t doubleToInteger12(double d);
std::int64_t doubleToInteger15(double d);
double integer0ToDouble(std::int64_t i);
double integer4ToDouble(std::int64_t i);
double integer12ToDouble(std::int64_t i);
double integer15ToDouble(std::int64_t i);

#endif
