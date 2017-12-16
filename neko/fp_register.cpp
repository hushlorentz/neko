#include "bit_ops.hpp"
#include "floating_point_ops.hpp"
#include "fp_register.hpp"

FPRegister::FPRegister() : x(0), y(0), z(0), w(0), xInt(0), yInt(0), zInt(0), wInt(0)
{
}

FPRegister::FPRegister(float x, float y, float z, float w) : x(x), y(y), z(z), w(w), xInt(0), yInt(0), zInt(0), wInt(0)
{
}

void FPRegister::load(float newX, float newY, float newZ, float newW)
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
  xInt = srcReg->xInt;
  yInt = srcReg->yInt;
  zInt = srcReg->zInt;
  wInt = srcReg->wInt;
}

void addFPRegisters(FPRegister * r1, FPRegister * r2, FPRegister * r3, uint8_t fieldMask, uint16_t * resultFlags)
{
  uint8_t xResultFlags = 0;
  uint8_t yResultFlags = 0;
  uint8_t zResultFlags = 0;
  uint8_t wResultFlags = 0;

  r3->x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? addFP(r1->x, r2->x, &xResultFlags) : r3->x;
  r3->y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? addFP(r1->y, r2->y, &yResultFlags) : r3->y;
  r3->z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? addFP(r1->z, r2->z, &zResultFlags) : r3->z;
  r3->w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? addFP(r1->w, r2->w, &wResultFlags) : r3->w;
}

void subFPRegisters(FPRegister * r1, FPRegister * r2, FPRegister * r3, uint8_t fieldMask, uint16_t * resultFlags)
{
  uint8_t xResultFlags = 0;
  uint8_t yResultFlags = 0;
  uint8_t zResultFlags = 0;
  uint8_t wResultFlags = 0;

  r3->x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? subFP(r1->x, r2->x, &xResultFlags) : r3->x;
  r3->y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? subFP(r1->y, r2->y, &yResultFlags) : r3->y;
  r3->z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? subFP(r1->z, r2->z, &zResultFlags) : r3->z;
  r3->w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? subFP(r1->w, r2->w, &wResultFlags) : r3->w;
}

void mulFPRegisters(FPRegister * r1, FPRegister * r2, FPRegister * r3, uint8_t fieldMask, uint16_t * resultFlags)
{
  uint8_t xResultFlags = 0;
  uint8_t yResultFlags = 0;
  uint8_t zResultFlags = 0;
  uint8_t wResultFlags = 0;

  r3->x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? mulFP(r1->x, r2->x, &xResultFlags) : r3->x;
  r3->y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? mulFP(r1->y, r2->y, &yResultFlags) : r3->y;
  r3->z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? mulFP(r1->z, r2->z, &zResultFlags) : r3->z;
  r3->w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? mulFP(r1->w, r2->w, &wResultFlags) : r3->w;
}

void divFPRegisters(FPRegister * r1, FPRegister * r2, FPRegister * r3, uint8_t fieldMask, uint16_t * resultFlags)
{
  uint8_t xResultFlags = 0;
  uint8_t yResultFlags = 0;
  uint8_t zResultFlags = 0;
  uint8_t wResultFlags = 0;

  r3->x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? divFP(r1->x, r2->x, &xResultFlags) : r3->x;
  r3->y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? divFP(r1->y, r2->y, &yResultFlags) : r3->y;
  r3->z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? divFP(r1->z, r2->z, &zResultFlags) : r3->z;
  r3->w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? divFP(r1->w, r2->w, &wResultFlags) : r3->w;
}

void absFPRegisters(FPRegister * source, FPRegister * dest, uint8_t fieldMask)
{
  dest->x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? abs(source->x) : dest->x;
  dest->y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? abs(source->y) : dest->y;
  dest->z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? abs(source->z) : dest->z;
  dest->w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? abs(source->w) : dest->w;
}

void addFloatToRegister(FPRegister * r1, float value, FPRegister * dest, uint8_t fieldMask, uint16_t * resultFlags)
{
  uint8_t xResultFlags = 0;
  uint8_t yResultFlags = 0;
  uint8_t zResultFlags = 0;
  uint8_t wResultFlags = 0;

  dest->x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? addFP(r1->x, value, &xResultFlags) : dest->x;
  dest->y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? addFP(r1->y, value, &yResultFlags) : dest->y;
  dest->z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? addFP(r1->z, value, &zResultFlags) : dest->z;
  dest->w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? addFP(r1->w, value, &wResultFlags) : dest->w;
}

void convertFPRegisterToInt0(FPRegister * source, FPRegister * dest, uint8_t fieldMask)
{
  convertFPRegisterToInt(source, dest, fieldMask, &floatToInteger0);
}

void convertFPRegisterToInt4(FPRegister * source, FPRegister * dest, uint8_t fieldMask)
{
  convertFPRegisterToInt(source, dest, fieldMask, &floatToInteger4);
}

void convertFPRegisterToInt12(FPRegister * source, FPRegister * dest, uint8_t fieldMask)
{
  convertFPRegisterToInt(source, dest, fieldMask, &floatToInteger12);
}

void convertFPRegisterToInt15(FPRegister * source, FPRegister * dest, uint8_t fieldMask)
{
  convertFPRegisterToInt(source, dest, fieldMask, &floatToInteger15);
}

void convertFPRegisterToInt(FPRegister * source, FPRegister * dest, uint8_t fieldMask, int (*convertFunc)(float))
{
  dest->xInt = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? (*convertFunc)(source->x) : dest->xInt;
  dest->yInt = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? (*convertFunc)(source->y) : dest->yInt;
  dest->zInt = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? (*convertFunc)(source->z) : dest->zInt;
  dest->wInt = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? (*convertFunc)(source->w) : dest->wInt;
}
