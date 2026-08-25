#include "floating_point_ops.hpp"
#include <array>
#include <cfloat>

namespace
{
  using CompatibilityOperation = double (*)(double, double, std::uint8_t *);

  constexpr int VU_MIN_EXPONENT = -126;
  constexpr int VU_VALUE_SCALE_EXPONENT = -149;
  constexpr std::size_t WIDE_WORD_COUNT = 5;

  struct DecodedOperand
  {
    bool negative;
    bool zero;
    int exponent;
    std::uint32_t significand;
  };

  struct WideMagnitude
  {
    std::array<std::uint64_t, WIDE_WORD_COUNT> words{};

    static WideMagnitude shifted(std::uint32_t value, unsigned shift)
    {
      WideMagnitude result;
      const std::size_t word = shift / 64;
      const unsigned offset = shift % 64;

      result.words[word] = static_cast<std::uint64_t>(value) << offset;
      if (offset > 40)
      {
        result.words[word + 1] =
          static_cast<std::uint64_t>(value) >> (64 - offset);
      }
      return result;
    }

    int compare(const WideMagnitude &other) const
    {
      for (std::size_t i = WIDE_WORD_COUNT; i-- > 0;)
      {
        if (words[i] != other.words[i])
        {
          return words[i] < other.words[i] ? -1 : 1;
        }
      }
      return 0;
    }

    WideMagnitude add(const WideMagnitude &other) const
    {
      WideMagnitude result;
      bool carry = false;
      for (std::size_t i = 0; i < WIDE_WORD_COUNT; i++)
      {
        const std::uint64_t partial = words[i] + other.words[i];
        const bool partialCarry = partial < words[i];
        result.words[i] = partial + static_cast<std::uint64_t>(carry);
        carry = partialCarry || (carry && result.words[i] == 0);
      }
      return result;
    }

    WideMagnitude subtract(const WideMagnitude &other) const
    {
      WideMagnitude result;
      bool borrow = false;
      for (std::size_t i = 0; i < WIDE_WORD_COUNT; i++)
      {
        const std::uint64_t subtrahend =
          other.words[i] + static_cast<std::uint64_t>(borrow);
        const bool subtrahendOverflow = subtrahend < other.words[i];
        result.words[i] = words[i] - subtrahend;
        borrow = subtrahendOverflow || words[i] < subtrahend;
      }
      return result;
    }

    int highestBit() const
    {
      for (std::size_t i = WIDE_WORD_COUNT; i-- > 0;)
      {
        std::uint64_t word = words[i];
        if (word == 0)
        {
          continue;
        }

        int bit = 0;
        while ((word >>= 1) != 0)
        {
          bit++;
        }
        return static_cast<int>(i * 64) + bit;
      }
      return -1;
    }

    std::uint32_t shiftedToUint32(unsigned shift) const
    {
      const std::size_t word = shift / 64;
      const unsigned offset = shift % 64;
      std::uint64_t result = words[word] >> offset;
      if (offset != 0 && word + 1 < WIDE_WORD_COUNT)
      {
        result |= words[word + 1] << (64 - offset);
      }
      return static_cast<std::uint32_t>(result);
    }
  };

  DecodedOperand decodeOperand(std::uint32_t bits)
  {
    const std::uint32_t encodedExponent = (bits >> 23) & 0xff;
    return {
      (bits & FP_SIGN_BIT) != 0,
      encodedExponent == 0,
      static_cast<int>(encodedExponent) - 127,
      0x800000u | (bits & FP_MAX_MANTISSA)
    };
  }

  VUFloatResult signedZero(bool negative, std::uint8_t flags = 0)
  {
    return {negative ? FP_SIGN_BIT : 0, flags};
  }

  VUFloatResult maximumResult(bool negative)
  {
    return {
      (negative ? FP_SIGN_BIT : 0) | 0x7fffffffu,
      FP_FLAG_OVERFLOW
    };
  }

  VUFloatResult encodeResult(bool negative,
                             int exponent,
                             std::uint32_t significand)
  {
    return {
      (negative ? FP_SIGN_BIT : 0) |
        (static_cast<std::uint32_t>(exponent + 127) << 23) |
        (significand & FP_MAX_MANTISSA),
      0
    };
  }

  VUFloatResult normalizeWideResult(const WideMagnitude &magnitude,
                                    bool negative)
  {
    const int highestBit = magnitude.highestBit();
    const int exponent = highestBit + VU_VALUE_SCALE_EXPONENT;
    if (exponent > FP_MAX_EXPONENT)
    {
      return maximumResult(negative);
    }
    if (exponent < VU_MIN_EXPONENT)
    {
      return signedZero(negative, FP_FLAG_UNDERFLOW);
    }

    const std::uint32_t significand =
      magnitude.shiftedToUint32(static_cast<unsigned>(highestBit - 23));
    return encodeResult(negative, exponent, significand);
  }

  VUFloatResult addRaw(std::uint32_t d1Bits, std::uint32_t d2Bits)
  {
    const DecodedOperand d1 = decodeOperand(d1Bits);
    const DecodedOperand d2 = decodeOperand(d2Bits);

    if (d1.zero && d2.zero)
    {
      return signedZero(d1.negative && d2.negative);
    }
    if (d1.zero)
    {
      return {d2Bits, 0};
    }
    if (d2.zero)
    {
      return {d1Bits, 0};
    }

    const WideMagnitude d1Magnitude = WideMagnitude::shifted(
      d1.significand,
      static_cast<unsigned>(d1.exponent - VU_MIN_EXPONENT));
    const WideMagnitude d2Magnitude = WideMagnitude::shifted(
      d2.significand,
      static_cast<unsigned>(d2.exponent - VU_MIN_EXPONENT));

    if (d1.negative == d2.negative)
    {
      return normalizeWideResult(
        d1Magnitude.add(d2Magnitude),
        d1.negative);
    }

    const int comparison = d1Magnitude.compare(d2Magnitude);
    if (comparison == 0)
    {
      return signedZero(false);
    }
    if (comparison > 0)
    {
      return normalizeWideResult(
        d1Magnitude.subtract(d2Magnitude),
        d1.negative);
    }
    return normalizeWideResult(
      d2Magnitude.subtract(d1Magnitude),
      d2.negative);
  }

  int highestBit(std::uint64_t value)
  {
    int bit = 0;
    while ((value >>= 1) != 0)
    {
      bit++;
    }
    return bit;
  }

  VUFloatResult multiplyRaw(std::uint32_t d1Bits, std::uint32_t d2Bits)
  {
    const DecodedOperand d1 = decodeOperand(d1Bits);
    const DecodedOperand d2 = decodeOperand(d2Bits);
    const bool negative = d1.negative != d2.negative;

    if (d1.zero || d2.zero)
    {
      return signedZero(negative);
    }

    const std::uint64_t product =
      static_cast<std::uint64_t>(d1.significand) * d2.significand;
    const int productHighestBit = highestBit(product);
    const int exponent =
      d1.exponent + d2.exponent + productHighestBit - 46;
    if (exponent > FP_MAX_EXPONENT)
    {
      return maximumResult(negative);
    }
    if (exponent < VU_MIN_EXPONENT)
    {
      return signedZero(negative, FP_FLAG_UNDERFLOW);
    }

    const std::uint32_t significand = static_cast<std::uint32_t>(
      product >> (productHighestBit - 23));
    return encodeResult(negative, exponent, significand);
  }

  double compatibilityOperand(std::uint32_t bits)
  {
    if ((bits & 0x7f800000u) == 0)
    {
      return std::copysign(0.0, (bits & FP_SIGN_BIT) != 0 ? -1.0 : 1.0);
    }

    VUFloat value;
    value.setBits(bits);
    return value;
  }

  double maxVUValue()
  {
    return std::ldexp(2.0 - std::ldexp(1.0, -23), FP_MAX_EXPONENT);
  }

  VUFloatResult runCompatibilityOperation(
    std::uint32_t d1Bits,
    std::uint32_t d2Bits,
    CompatibilityOperation operation)
  {
    std::uint8_t flags = 0;
    VUFloat result(operation(compatibilityOperand(d1Bits),
                             compatibilityOperand(d2Bits),
                             &flags));

    return {result.bits(), flags};
  }
}

VUFloatDecomposition decomposeVUFloat(std::uint32_t bits)
{
  const std::uint8_t encodedExponent =
    static_cast<std::uint8_t>((bits >> 23) & 0xff);
  VUFloatClassification classification = VUFloatClassification::Finite;
  if (encodedExponent == 0)
  {
    classification = VUFloatClassification::Zero;
  }
  else if (encodedExponent == 0xff)
  {
    classification = VUFloatClassification::ExtendedFinite;
  }

  return {
    (bits & FP_SIGN_BIT) != 0,
    encodedExponent,
    static_cast<std::int16_t>(
      static_cast<std::int32_t>(encodedExponent) - 127),
    bits & FP_MAX_MANTISSA,
    classification
  };
}

VUFloatResult addFPRaw(std::uint32_t d1Bits, std::uint32_t d2Bits)
{
  return addRaw(d1Bits, d2Bits);
}

VUFloatResult mulFPRaw(std::uint32_t d1Bits, std::uint32_t d2Bits)
{
  return multiplyRaw(d1Bits, d2Bits);
}

VUFloatResult divFPRaw(std::uint32_t d1Bits, std::uint32_t d2Bits)
{
  return runCompatibilityOperation(d1Bits, d2Bits, divFP);
}

VUFloatResult subFPRaw(std::uint32_t d1Bits, std::uint32_t d2Bits)
{
  return addRaw(d1Bits, d2Bits ^ FP_SIGN_BIT);
}

double convertFromIEEE(double value, uint8_t * resultFlags)
{
  if (std::abs(value) >= std::ldexp(1.0, FP_MAX_EXPONENT + 1))
  {
    *resultFlags |= FP_FLAG_OVERFLOW;

    return std::copysign(maxVUValue(), value);
  }
  if (value != 0 && std::abs(value) < std::ldexp(1.0, -126))
  {
    *resultFlags |= FP_FLAG_UNDERFLOW;

    return std::copysign(0.0, value);
  }

  return value;
}

uint8_t getSignFromNumXORNum(double d1, double d2)
{
  return std::signbit(d1) ^ std::signbit(d2);
}

double processZeroDivZero(double d1, double d2)
{
  return std::copysign(0.0, getSignFromNumXORNum(d1, d2) ? -1.0 : 1.0);
}

double processNumDivZero(double d1, double d2)
{
  return std::copysign(maxVUValue(), getSignFromNumXORNum(d1, d2) ? -1.0 : 1.0);
}

double addFP(double d1, double d2, uint8_t * resultFlags)
{
  return convertFromIEEE(d1 + d2, resultFlags);
}

double mulFP(double d1, double d2, uint8_t * resultFlags)
{
  return convertFromIEEE(d1 * d2, resultFlags);
}

double divFP(double d1, double d2, uint8_t * resultFlags)
{
  if (d2 == 0.0f)
  {
    if (d1 == 0.0f)
    {
      *resultFlags |= FP_FLAG_I_BIT;
      return processZeroDivZero(d1, d2);
    }
    else
    {
      *resultFlags |= FP_FLAG_D_BIT;
      return processNumDivZero(d1, d2);
    }
  }

  return convertFromIEEE(d1 / d2, resultFlags);
}

double subFP(double d1, double d2, uint8_t * resultFlags)
{
  return convertFromIEEE(d1 - d2, resultFlags);
}

std::int32_t doubleToInteger0(double d)
{
  return static_cast<std::int32_t>(d);
}

std::int32_t doubleToInteger4(double d)
{
  return static_cast<std::int32_t>(d * 16);
}

std::int32_t doubleToInteger12(double d)
{
  return static_cast<std::int32_t>(d * 4096);
}

std::int32_t doubleToInteger15(double d)
{
  return static_cast<std::int32_t>(d * 32768);
}

double integer0ToDouble(std::int32_t i)
{
  return (double)i;
}

double integer4ToDouble(std::int32_t i)
{
  return i / 16.0f;
}

double integer12ToDouble(std::int32_t i)
{
  return i / 4096.0f;
}

double integer15ToDouble(std::int32_t i)
{
  return i / 32768.0f;
}
