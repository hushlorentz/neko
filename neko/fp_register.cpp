#include "bit_ops.hpp"
#include "floating_point_ops.hpp"
#include "fp_register.hpp"

FPRegister::FPRegister() : x(0), y(0), z(0), w(0)
{
}

FPRegister::FPRegister(float x, float y, float z, float w) : x(x), y(y), z(z), w(w)
{
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
  if (hasFlag(fieldMask, FP_REGISTER_X_FIELD))
  {
    dest->x = abs(source->x);
  }
  if (hasFlag(fieldMask, FP_REGISTER_Y_FIELD))
  {
    dest->y = abs(source->y);
  }
  if (hasFlag(fieldMask, FP_REGISTER_Z_FIELD))
  {
    dest->z = abs(source->z);
  }
  if (hasFlag(fieldMask, FP_REGISTER_W_FIELD))
  {
    dest->w = abs(source->w);
  }
}
