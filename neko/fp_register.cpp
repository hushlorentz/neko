#include "bit_ops.hpp"
#include "floating_point_ops.hpp"
#include "fp_register.hpp"

FPRegister::FPRegister() : x(0), y(0), z(0), w(0), xResultFlags(0), yResultFlags(0), zResultFlags(0), wResultFlags(0)
{
}

FPRegister::FPRegister(float x, float y, float z, float w) : x(x), y(y), z(z), w(w), xResultFlags(0), yResultFlags(0), zResultFlags(0), wResultFlags(0)
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
  xResultFlags = srcReg->xResultFlags;
  yResultFlags = srcReg->yResultFlags;
  zResultFlags = srcReg->zResultFlags;
  wResultFlags = srcReg->wResultFlags;
}

void FPRegister::storeAdd(FPRegister * r1, FPRegister * r2, uint8_t fieldMask, uint16_t * resultFlags)
{
  clearFlags();
  x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? addFP(r1->x, r2->x, &xResultFlags) : x;
  y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? addFP(r1->y, r2->y, &yResultFlags) : y;
  z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? addFP(r1->z, r2->z, &zResultFlags) : z;
  w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? addFP(r1->w, r2->w, &wResultFlags) : w;
}

void FPRegister::storeSub(FPRegister * r1, FPRegister * r2, uint8_t fieldMask, uint16_t * resultFlags)
{
  clearFlags();
  x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? subFP(r1->x, r2->x, &xResultFlags) : x;
  y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? subFP(r1->y, r2->y, &yResultFlags) : y;
  z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? subFP(r1->z, r2->z, &zResultFlags) : z;
  w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? subFP(r1->w, r2->w, &wResultFlags) : w;
}

void FPRegister::storeMul(FPRegister * r1, FPRegister * r2, uint8_t fieldMask, uint16_t * resultFlags)
{
  clearFlags();
  x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? mulFP(r1->x, r2->x, &xResultFlags) : x;
  y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? mulFP(r1->y, r2->y, &yResultFlags) : y;
  z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? mulFP(r1->z, r2->z, &zResultFlags) : z;
  w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? mulFP(r1->w, r2->w, &wResultFlags) : w;
}

void FPRegister::storeDiv(FPRegister * r1, FPRegister * r2, uint8_t fieldMask, uint16_t * resultFlags)
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

void FPRegister::storeAddFloat(FPRegister * r1, float value, uint8_t fieldMask, uint16_t * resultFlags)
{
  clearFlags();
  x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? addFP(r1->x, value, &xResultFlags) : x;
  y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? addFP(r1->y, value, &yResultFlags) : y;
  z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? addFP(r1->z, value, &zResultFlags) : z;
  w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? addFP(r1->w, value, &wResultFlags) : w;
}

void FPRegister::toInt0(FPRegister * source, uint8_t fieldMask)
{
  toInt(source, fieldMask, &floatToInteger0);
}

void FPRegister::toInt4(FPRegister * source, uint8_t fieldMask)
{
  toInt(source, fieldMask, &floatToInteger4);
}

void FPRegister::toInt12(FPRegister * source, uint8_t fieldMask)
{
  toInt(source, fieldMask, &floatToInteger12);
}

void FPRegister::toInt15(FPRegister * source, uint8_t fieldMask)
{
  toInt(source, fieldMask, &floatToInteger15);
}

void FPRegister::toFloat0(FPRegister * source, uint8_t fieldMask)
{
  toFloat(source, fieldMask, &integer0ToFloat);
}

void FPRegister::toFloat4(FPRegister * source, uint8_t fieldMask)
{
  toFloat(source, fieldMask, &integer4ToFloat);
}

void FPRegister::toFloat12(FPRegister * source, uint8_t fieldMask)
{
  toFloat(source, fieldMask, &integer12ToFloat);
}

void FPRegister::toFloat15(FPRegister * source, uint8_t fieldMask)
{
  toFloat(source, fieldMask, &integer15ToFloat);
}

void FPRegister::toInt(FPRegister * source, uint8_t fieldMask, int (*convertFunc)(float))
{
  xInt = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? (*convertFunc)(source->x) : xInt;
  yInt = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? (*convertFunc)(source->y) : yInt;
  zInt = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? (*convertFunc)(source->z) : zInt;
  wInt = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? (*convertFunc)(source->w) : wInt;
}

void FPRegister::toFloat(FPRegister * source, uint8_t fieldMask, float (*convertFunc)(int))
{
  x = hasFlag(fieldMask, FP_REGISTER_X_FIELD) ? (*convertFunc)(source->xInt) : x;
  y = hasFlag(fieldMask, FP_REGISTER_Y_FIELD) ? (*convertFunc)(source->yInt) : y;
  z = hasFlag(fieldMask, FP_REGISTER_Z_FIELD) ? (*convertFunc)(source->zInt) : z;
  w = hasFlag(fieldMask, FP_REGISTER_W_FIELD) ? (*convertFunc)(source->wInt) : w;
}

void FPRegister::clearFlags()
{
  xResultFlags = 0;
  yResultFlags = 0;
  zResultFlags = 0;
  wResultFlags = 0;
};
