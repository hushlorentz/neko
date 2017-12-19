#ifndef FLOATING_POINT_OPS
#define FLOATING_POINT_OPS

#define FP_FLAG_OVERFLOW 0x1
#define FP_FLAG_UNDERFLOW 0x2
#define FP_FLAG_I_BIT 0x4
#define FP_FLAG_D_BIT 0x8

#define FP_MAX_MANTISSA 0
#define FP_MAX_EXPONENT 0xff

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
int floatToInteger0(float f);
int floatToInteger4(float f);
int floatToInteger12(float f);
int floatToInteger15(float f);
float integer0ToFloat(int i);
float integer4ToFloat(int i);
float integer12ToFloat(int i);
float integer15ToFloat(int i);

#endif
