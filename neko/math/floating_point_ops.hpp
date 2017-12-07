#ifndef FLOATING_POINT_OPS
#define FLOATING_POINT_OPS

#define FP_FLAG_OVERFLOW 0x1
#define FP_FLAG_UNDERFLOW 0x2

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
float adjustForOverflow(double value, uint8_t * resultFlags);
float adjustForUnderflow(double value, uint8_t * resultFlags);

#endif
