#ifndef FPREGISTER_HPP
#define FPREGISTER_HPP

#include <cstdint>

#define FP_REGISTER_X_FIELD 1
#define FP_REGISTER_Y_FIELD 2
#define FP_REGISTER_Z_FIELD 4
#define FP_REGISTER_W_FIELD 8
#define FP_REGISTER_ALL_FIELDS 15

class FPRegister
{
  public:
    FPRegister();
    FPRegister(float x, float y, float z, float w);
    float x;
    float y;
    float z;
    float w;
};

void addFPRegisters(FPRegister * r1, FPRegister * r2, FPRegister * r3, uint8_t fieldMask, uint16_t * resultFlags);
void subFPRegisters(FPRegister * r1, FPRegister * r2, FPRegister * r3, uint8_t fieldMask, uint16_t * resultFlags);
void mulFPRegisters(FPRegister * r1, FPRegister * r2, FPRegister * r3, uint8_t fieldMask, uint16_t * resultFlags);
void divFPRegisters(FPRegister * r1, FPRegister * r2, FPRegister * r3, uint8_t fieldMask, uint16_t * resultFlags);
void absFPRegisters(FPRegister * source, FPRegister * dest, uint8_t fieldMask);

#endif
