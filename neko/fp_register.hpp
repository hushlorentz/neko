#ifndef FPREGISTER_HPP
#define FPREGISTER_HPP

#include <cstdint>

#define FP_REGISTER_X_FIELD 1
#define FP_REGISTER_Y_FIELD 2
#define FP_REGISTER_Z_FIELD 4
#define FP_REGISTER_W_FIELD 8
#define FP_REGISTER_ALL_FIELDS 15

#define FP_REGISTER_FIELD_IS_NEGATIVE(x) (x & (1l << 63))

class FPRegister
{
  public:
    FPRegister();
    FPRegister(double x, double y, double z, double w);
    void load(double x, double y, double z, double w);
    void copyFrom(FPRegister * srcReg);
    union
    {
      double x;
      long xInt;
    };
    union
    {
      double y;
      long yInt;
    };
    union
    {
      double z;
      long zInt;
    };
    union
    {
      double w;
      long wInt;
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
    void storeAddDouble(FPRegister * r1, double value, uint8_t fieldMask, uint16_t * resultFlags);
    void storeMulDouble(FPRegister * r1, double value, uint8_t fieldMask, uint16_t * resultFlags);
    void toInt0(FPRegister * source, uint8_t fieldMask);
    void toInt4(FPRegister * source, uint8_t fieldMask);
    void toInt12(FPRegister * source, uint8_t fieldMask);
    void toInt15(FPRegister * source, uint8_t fieldMask);
    void toDouble0(FPRegister * source, uint8_t fieldMask);
    void toDouble4(FPRegister * source, uint8_t fieldMask);
    void toDouble12(FPRegister * source, uint8_t fieldMask);
    void toDouble15(FPRegister * source, uint8_t fieldMask);

  private:
    void toInt(FPRegister * source, uint8_t fieldMask, long (*convertFunc)(double));
    void toDouble(FPRegister * source, uint8_t fieldMask, double (*convertFunc)(long));
    void clearFlags();
};

#endif
