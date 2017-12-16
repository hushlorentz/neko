#ifndef FLOATING_POINT_OPS
#define FLOATING_POINT_OPS

#define FP_FLAG_OVERFLOW 0x1
#define FP_FLAG_UNDERFLOW 0x2
#define FP_FLAG_I_BIT 0x4
#define FP_FLAG_D_BIT 0x8

#define FP_MAX_MANTISSA 0x2
#define FP_MAX_EXPONENT 0x80

#include <cmath>

#include "fp_register.hpp"

typedef union {
  float float_representation;
  struct {
    unsigned int mantissa : 23;
    unsigned int exponent : 8;
    unsigned int sign : 1;
  } components;
} num_32bits;

float addFP(float f1, float f2, uint8_t * resultFlags);
float mulFP(float f1, float f2, uint8_t * resultFlags);
float divFP(float f1, float f2, uint8_t * resultFlags);
float subFP(float f1, float f2, uint8_t * resultFlags);
uint32_t floatToInteger0(float f);
uint32_t floatToInteger4(float f);
uint32_t floatToInteger12(float f);
uint32_t floatToInteger15(float f);

#endif
