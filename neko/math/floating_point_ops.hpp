#include "fp_register.hpp"

typedef union {
  float f;
  struct {
    unsigned int mantissa : 23;
    unsigned int exponent : 8;
    unsigned int sign : 1;
  } parts;
} float_cast;

float convertFromIEEE(float f)
{
  float_cast fu;
  fu.f = f;

  if (fu.parts.exponent == 0xff)
  {
    fu.parts.exponent = 0x80;
    fu.parts.mantissa = 0x2;

    return fu.f;
  }
  if (fu.parts.exponent == 0 && fu.parts.mantissa > 0)
  {
    return 0;
  }

  return f;
}

float addFP(float f1, float f2)
{
  return convertFromIEEE(f1 + f2);
}

float mulFP(float f1, float f2)
{
  return convertFromIEEE(f1 * f2);
}

float divFP(float f1, float f2)
{
  return convertFromIEEE(f1 / f2);
}

float subFP(float f1, float f2)
{
  return convertFromIEEE(f1 - f2);
}

void addFPRegisters(FPRegister * r1, FPRegister * r2, FPRegister * r3)
{
  r3->x = addFP(r1->x, r2->x);
  r3->y = addFP(r1->y, r2->y);
  r3->z = addFP(r1->z, r2->z);
  r3->w = addFP(r1->w, r2->w);
}

void subFPRegisters(FPRegister * r1, FPRegister * r2, FPRegister * r3)
{
  r3->x = subFP(r1->x, r2->x);
  r3->y = subFP(r1->y, r2->y);
  r3->z = subFP(r1->z, r2->z);
  r3->w = subFP(r1->w, r2->w);
}

void mulFPRegisters(FPRegister * r1, FPRegister * r2, FPRegister * r3)
{
  r3->x = mulFP(r1->x, r2->x);
  r3->y = mulFP(r1->y, r2->y);
  r3->z = mulFP(r1->z, r2->z);
  r3->w = mulFP(r1->w, r2->w);
}

void divFPRegisters(FPRegister * r1, FPRegister * r2, FPRegister * r3)
{
  r3->x = divFP(r1->x, r2->x);
  r3->y = divFP(r1->y, r2->y);
  r3->z = divFP(r1->z, r2->z);
  r3->w = divFP(r1->w, r2->w);
}
