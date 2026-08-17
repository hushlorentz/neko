#ifndef FPREGISTER_HPP
#define FPREGISTER_HPP

#include <cstdint>

#define FP_REGISTER_NO_FIELDS 0
#define FP_REGISTER_X_FIELD 1
#define FP_REGISTER_Y_FIELD 2
#define FP_REGISTER_Z_FIELD 4
#define FP_REGISTER_W_FIELD 8
#define FP_REGISTER_ALL_FIELDS 15

class VUFloat
{
  public:
    VUFloat();
    explicit VUFloat(double value);

    VUFloat &operator=(double value);
    operator double() const;

    std::uint32_t bits() const;
    void setBits(std::uint32_t value);
    std::int32_t signedValue() const;
    void setSignedValue(std::int32_t value);
    bool isNegative() const;
    void toggleSign();

  private:
    std::uint32_t rawBits;
};

static_assert(sizeof(VUFloat) == sizeof(std::uint32_t), "VU floating-point lanes must be 32 bits");

class FPRegister
{
  public:
    FPRegister();
    FPRegister(double x, double y, double z, double w);
    void load(double x, double y, double z, double w);
    void copyFrom(FPRegister * srcReg);
    void copyFieldsFrom(FPRegister * srcReg, uint8_t fieldMask);
    VUFloat x;
    VUFloat y;
    VUFloat z;
    VUFloat w;
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
    void toInt(FPRegister * source, uint8_t fieldMask, std::int32_t (*convertFunc)(double));
    void toDouble(FPRegister * source, uint8_t fieldMask, double (*convertFunc)(std::int32_t));
    void clearFlags();
};

#endif
