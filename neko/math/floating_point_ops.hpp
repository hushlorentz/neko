#ifndef FLOATING_POINT_OPS
#define FLOATING_POINT_OPS

#include <cmath>

#include "fp_register.hpp"

typedef union {
  float f;
  struct {
    unsigned int mantissa : 23;
    unsigned int exponent : 8;
    unsigned int sign : 1;
  } parts;
} float_cast;

float addFP(float f1, float f2);
float mulFP(float f1, float f2);
float divFP(float f1, float f2);
float subFP(float f1, float f2);

#endif
