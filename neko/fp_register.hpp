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
    void load(float x, float y, float z, float w);
    void copyFrom(FPRegister * srcReg);
    union
    {
      float x;
      int xInt;
    };
    union
    {
      float y;
      int yInt;
    };
    union
    {
      float z;
      int zInt;
    };
    union
    {
      float w;
      int wInt;
    };
};

void addFPRegisters(FPRegister * r1, FPRegister * r2, FPRegister * r3, uint8_t fieldMask, uint16_t * resultFlags);
void subFPRegisters(FPRegister * r1, FPRegister * r2, FPRegister * r3, uint8_t fieldMask, uint16_t * resultFlags);
void mulFPRegisters(FPRegister * r1, FPRegister * r2, FPRegister * r3, uint8_t fieldMask, uint16_t * resultFlags);
void divFPRegisters(FPRegister * r1, FPRegister * r2, FPRegister * r3, uint8_t fieldMask, uint16_t * resultFlags);
void absFPRegisters(FPRegister * source, FPRegister * dest, uint8_t fieldMask);
void addFloatToRegister(FPRegister * r1, float value, FPRegister * dest, uint8_t fieldMask, uint16_t * resultFlags);
void convertFPRegisterToInt0(FPRegister * source, FPRegister * dest, uint8_t fieldMask);
void convertFPRegisterToInt4(FPRegister * source, FPRegister * dest, uint8_t fieldMask);
void convertFPRegisterToInt12(FPRegister * source, FPRegister * dest, uint8_t fieldMask);
void convertFPRegisterToInt15(FPRegister * source, FPRegister * dest, uint8_t fieldMask);
void convertFPRegisterToFloat0(FPRegister * source, FPRegister * dest, uint8_t fieldMask);
void convertFPRegisterToFloat4(FPRegister * source, FPRegister * dest, uint8_t fieldMask);
void convertFPRegisterToFloat12(FPRegister * source, FPRegister * dest, uint8_t fieldMask);
void convertFPRegisterToFloat15(FPRegister * source, FPRegister * dest, uint8_t fieldMask);
void convertFPRegisterToInt(FPRegister * source, FPRegister * dest, uint8_t fieldMask, int (*convertFunc)(float));
void convertFPRegisterToFloat(FPRegister * source, FPRegister * dest, uint8_t fieldMask, float (*convertFunc)(int));

#endif
