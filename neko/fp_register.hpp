#ifndef FPREGISTER_HPP
#define FPREGISTER_HPP

#include <cstdint>

#define FP_REGISTER_X_FIELD 1
#define FP_REGISTER_Y_FIELD 2
#define FP_REGISTER_Z_FIELD 4
#define FP_REGISTER_W_FIELD 8
#define FP_REGISTER_ALL_FIELDS 15

#define FP_REGISTER_FIELD_IS_NEGATIVE(x) (x & (1 << 31))

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
    uint8_t xResultFlags;
    uint8_t yResultFlags;
    uint8_t zResultFlags;
    uint8_t wResultFlags;

    void storeAbs(FPRegister * source, uint8_t fieldMask);
    void storeAdd(FPRegister * r1, FPRegister * r2, uint8_t fieldMask, uint16_t * resultFlags);
    void storeSub(FPRegister * r1, FPRegister * r2, uint8_t fieldMask, uint16_t * resultFlags);
    void storeMul(FPRegister * r1, FPRegister * r2, uint8_t fieldMask, uint16_t * resultFlags);
    void storeDiv(FPRegister * r1, FPRegister * r2, uint8_t fieldMask, uint16_t * resultFlags);
    void storeAddFloat(FPRegister * r1, float value, uint8_t fieldMask, uint16_t * resultFlags);
    void toInt0(FPRegister * source, uint8_t fieldMask);
    void toInt4(FPRegister * source, uint8_t fieldMask);
    void toInt12(FPRegister * source, uint8_t fieldMask);
    void toInt15(FPRegister * source, uint8_t fieldMask);
    void toFloat0(FPRegister * source, uint8_t fieldMask);
    void toFloat4(FPRegister * source, uint8_t fieldMask);
    void toFloat12(FPRegister * source, uint8_t fieldMask);
    void toFloat15(FPRegister * source, uint8_t fieldMask);

  private:
    void toInt(FPRegister * source, uint8_t fieldMask, int (*convertFunc)(float));
    void toFloat(FPRegister * source, uint8_t fieldMask, float (*convertFunc)(int));
    void clearFlags();
};

#endif
