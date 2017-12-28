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

#include <cmath>

#include "fp_register.hpp"

typedef union {
  double d;
  struct {
    unsigned long mantissa : 52;
    unsigned long exponent : 11; 
    unsigned long sign : 1;
  };
} Double;

double addFP(double d1, double d2, uint8_t * resultFlags);
double mulFP(double d1, double d2, uint8_t * resultFlags);
double divFP(double d1, double d2, uint8_t * resultFlags);
double subFP(double d1, double d2, uint8_t * resultFlags);
long doubleToInteger0(double d);
long doubleToInteger4(double d);
long doubleToInteger12(double d);
long doubleToInteger15(double d);
double integer0ToDouble(long i);
double integer4ToDouble(long i);
double integer12ToDouble(long i);
double integer15ToDouble(long i);

#endif
