#include "bit_ops.hpp"
#include "floating_point_ops.hpp"
#include "fp_register.hpp"

FPRegister::FPRegister() : x(0), y(0), z(0), w(0)
{
}

FPRegister::FPRegister(float x, float y, float z, float w) : x(x), y(y), z(z), w(w)
{
}

void addFPRegisters(FPRegister * r1, FPRegister * r2, FPRegister * r3, uint8_t fieldMask)
{
  if (hasFlag(fieldMask, FP_REGISTER_X_FIELD))
  {
    r3->x = addFP(r1->x, r2->x);
  }
  if (hasFlag(fieldMask, FP_REGISTER_Y_FIELD))
  {
    r3->y = addFP(r1->y, r2->y);
  }
  if (hasFlag(fieldMask, FP_REGISTER_Z_FIELD))
  {
    r3->z = addFP(r1->z, r2->z);
  }
  if (hasFlag(fieldMask, FP_REGISTER_W_FIELD))
  {
    r3->w = addFP(r1->w, r2->w);
  }
}

void subFPRegisters(FPRegister * r1, FPRegister * r2, FPRegister * r3, uint8_t fieldMask)
{
  if (hasFlag(fieldMask, FP_REGISTER_X_FIELD))
  {
    r3->x = subFP(r1->x, r2->x);
  }
  if (hasFlag(fieldMask, FP_REGISTER_Y_FIELD))
  {
    r3->y = subFP(r1->y, r2->y);
  }
  if (hasFlag(fieldMask, FP_REGISTER_Z_FIELD))
  {
    r3->z = subFP(r1->z, r2->z);
  }
  if (hasFlag(fieldMask, FP_REGISTER_W_FIELD))
  {
    r3->w = subFP(r1->w, r2->w);
  }
}

void mulFPRegisters(FPRegister * r1, FPRegister * r2, FPRegister * r3, uint8_t fieldMask)
{
  if (hasFlag(fieldMask, FP_REGISTER_X_FIELD))
  {
    r3->x = mulFP(r1->x, r2->x);
  }
  if (hasFlag(fieldMask, FP_REGISTER_Y_FIELD))
  {
    r3->y = mulFP(r1->y, r2->y);
  }
  if (hasFlag(fieldMask, FP_REGISTER_Z_FIELD))
  {
    r3->z = mulFP(r1->z, r2->z);
  }
  if (hasFlag(fieldMask, FP_REGISTER_W_FIELD))
  {
    r3->w = mulFP(r1->w, r2->w);
  }
}

void divFPRegisters(FPRegister * r1, FPRegister * r2, FPRegister * r3, uint8_t fieldMask)
{
  if (hasFlag(fieldMask, FP_REGISTER_X_FIELD))
  {
    r3->x = divFP(r1->x, r2->x);
  }
  if (hasFlag(fieldMask, FP_REGISTER_Y_FIELD))
  {
    r3->y = divFP(r1->y, r2->y);
  }
  if (hasFlag(fieldMask, FP_REGISTER_Z_FIELD))
  {
    r3->z = divFP(r1->z, r2->z);
  }
  if (hasFlag(fieldMask, FP_REGISTER_W_FIELD))
  {
    r3->w = divFP(r1->w, r2->w);
  }
}

void absFPRegisters(FPRegister * dest, FPRegister * source, uint8_t fieldMask)
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
