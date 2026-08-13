#ifndef FPREGISTER_HPP
#define FPREGISTER_HPP

#include <cstdint>

#define FP_REGISTER_NO_FIELDS 0
#define FP_REGISTER_X_FIELD 1
#define FP_REGISTER_Y_FIELD 2
#define FP_REGISTER_Z_FIELD 4
#define FP_REGISTER_W_FIELD 8
#define FP_REGISTER_ALL_FIELDS 15

#define FP_REGISTER_FIELD_IS_NEGATIVE(x) (x & (std::int64_t(1) << 63))

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
      std::int64_t xInt;
    };
    union
    {
      double y;
      std::int64_t yInt;
    };
    union
    {
      double z;
      std::int64_t zInt;
    };
    union
    {
      double w;
      std::int64_t wInt;
    };
    uint8_t xResultFlags;
    uint8_t yResultFlags;
    uint8_t zResultFlags;
    uint8_t wResultFlags;

    void storeAbs(FPRegister * source, uint8_t fieldMask);
    void storeAdd(FPRegister * r1, FPRegister * r2, uint8_t fieldMask);
    void storeSub(FPRegister * r1, FPRegister * r2, uint8_t fieldMask);
    void storeMul(FPRegister * r1, FPRegister * r2, uint8_t fieldMask);
    void storeDiv(FPRegister * r1, FPRegister * r2, uint8_t fieldMask);
    void storeAddDouble(FPRegister * r1, double value, uint8_t fieldMask);
    void storeMulDouble(FPRegister * r1, double value, uint8_t fieldMask);
    void storeSubDouble(FPRegister * r1, double value, uint8_t fieldMask);
    void storeMax(FPRegister * r1, FPRegister * r2, uint8_t fieldMask);
    void storeMaxDouble(FPRegister * r1, double d, uint8_t fieldMask);
    void storeMin(FPRegister * r1, FPRegister * r2, uint8_t fieldMask);
    void storeMinDouble(FPRegister * r1, double d, uint8_t fieldMask);
    void storeOuterProduct(FPRegister * r1, FPRegister * f2);
    void toInt0(FPRegister * source, uint8_t fieldMask);
    void toInt4(FPRegister * source, uint8_t fieldMask);
    void toInt12(FPRegister * source, uint8_t fieldMask);
    void toInt15(FPRegister * source, uint8_t fieldMask);
    void toDouble0(FPRegister * source, uint8_t fieldMask);
    void toDouble4(FPRegister * source, uint8_t fieldMask);
    void toDouble12(FPRegister * source, uint8_t fieldMask);
    void toDouble15(FPRegister * source, uint8_t fieldMask);

  private:
    void toInt(FPRegister * source, uint8_t fieldMask, std::int64_t (*convertFunc)(double));
    void toDouble(FPRegister * source, uint8_t fieldMask, double (*convertFunc)(std::int64_t));
    void clearFlags();
};

#endif
