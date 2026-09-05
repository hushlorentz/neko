#include "floating_point_ops.hpp"
#include <array>
#include <cfloat>
#include <cstring>

namespace
{
  constexpr int EE_MIN_EXPONENT = -126;
  constexpr int EE_VALUE_SCALE_EXPONENT = -149;
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

  EEFloatResult encodeNormalizedResult(
    bool negative,
    int exponent,
    std::uint32_t significand)
  {
    if (exponent > FP_MAX_EXPONENT)
    {
      return maximumResult(negative);
    }
    if (exponent < EE_MIN_EXPONENT)
    {
      return signedZero(negative, FP_FLAG_UNDERFLOW);
    }

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
    if (highestBit < 0)
    {
      return signedZero(negative);
    }
    const int exponent = highestBit + EE_VALUE_SCALE_EXPONENT;
    if (exponent > FP_MAX_EXPONENT)
    {
      return maximumResult(negative);
    }
    if (exponent < EE_MIN_EXPONENT)
    {
      return signedZero(negative, FP_FLAG_UNDERFLOW);
    }

    const std::uint32_t significand =
      magnitude.shiftedToUint32(static_cast<unsigned>(highestBit - 23));
    return encodeNormalizedResult(
      negative,
      exponent,
      significand);
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
      static_cast<unsigned>(d1.exponent - EE_MIN_EXPONENT));
    const WideMagnitude d2Magnitude = WideMagnitude::shifted(
      d2.significand,
      static_cast<unsigned>(d2.exponent - EE_MIN_EXPONENT));

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
    return normalizeEEFloat(
      negative,
      product,
      static_cast<std::int16_t>(
        d1.exponent + d2.exponent - 46));
  }

  VUFloatResult divideRaw(
    std::uint32_t numeratorBits,
    std::uint32_t denominatorBits)
  {
    const DecodedOperand numerator = decodeOperand(numeratorBits);
    const DecodedOperand denominator = decodeOperand(denominatorBits);
    const bool negative = numerator.negative != denominator.negative;

    if (denominator.zero)
    {
      return {
        (negative ? FP_SIGN_BIT : 0) | 0x7fffffffu,
        static_cast<std::uint8_t>(
          numerator.zero ? FP_FLAG_I_BIT : FP_FLAG_D_BIT)
      };
    }
    if (numerator.zero)
    {
      return signedZero(negative);
    }

    int exponent = numerator.exponent - denominator.exponent;
    std::uint64_t scaledNumerator;
    if (numerator.significand < denominator.significand)
    {
      exponent--;
      scaledNumerator =
        static_cast<std::uint64_t>(numerator.significand) << 24;
    }
    else
    {
      scaledNumerator =
        static_cast<std::uint64_t>(numerator.significand) << 23;
    }

    if (exponent > FP_MAX_EXPONENT)
    {
      return {
        (negative ? FP_SIGN_BIT : 0) | 0x7fffffffu,
        0
      };
    }
    if (exponent < EE_MIN_EXPONENT)
    {
      return signedZero(negative);
    }

    const std::uint32_t significand = static_cast<std::uint32_t>(
      scaledNumerator / denominator.significand);
    return encodeNormalizedResult(negative, exponent, significand);
  }

  int compareRaw(
    const DecodedOperand &left,
    std::uint32_t leftBits,
    const DecodedOperand &right,
    std::uint32_t rightBits)
  {
    if (left.zero && right.zero)
    {
      return 0;
    }
    if (left.zero)
    {
      return right.negative ? 1 : -1;
    }
    if (right.zero)
    {
      return left.negative ? -1 : 1;
    }
    if (left.negative != right.negative)
    {
      return left.negative ? -1 : 1;
    }

    const std::uint32_t leftMagnitude = leftBits & ~FP_SIGN_BIT;
    const std::uint32_t rightMagnitude = rightBits & ~FP_SIGN_BIT;
    if (leftMagnitude == rightMagnitude)
    {
      return 0;
    }
    const bool leftMagnitudeLess = leftMagnitude < rightMagnitude;
    if (left.negative)
    {
      return leftMagnitudeLess ? 1 : -1;
    }
    return leftMagnitudeLess ? -1 : 1;
  }

  VUFloatResult selectRaw(
    std::uint32_t leftBits,
    std::uint32_t rightBits,
    bool maximum)
  {
    const DecodedOperand left = decodeOperand(leftBits);
    const DecodedOperand right = decodeOperand(rightBits);
    if (left.zero && right.zero)
    {
      const bool negative =
        maximum
          ? left.negative && right.negative
          : left.negative || right.negative;
      return signedZero(negative);
    }

    const int comparison =
      compareRaw(left, leftBits, right, rightBits);
    const bool selectLeft =
      maximum ? comparison >= 0 : comparison <= 0;
    const DecodedOperand &selected = selectLeft ? left : right;
    const std::uint32_t selectedBits =
      selectLeft ? leftBits : rightBits;
    return selected.zero
      ? signedZero(selected.negative)
      : VUFloatResult{selectedBits, 0};
  }

  EEFloatResult selectEERaw(
    std::uint32_t fsBits,
    std::uint32_t ftBits,
    bool maximum)
  {
    const DecodedOperand fs = decodeOperand(fsBits);
    const DecodedOperand ft = decodeOperand(ftBits);
    if (fs.zero && ft.zero)
    {
      const bool negative =
        maximum
          ? fs.negative && ft.negative
          : fs.negative || ft.negative;
      return signedZero(negative);
    }

    const int comparison =
      compareRaw(fs, fsBits, ft, ftBits);
    const bool selectFS =
      maximum ? comparison >= 0 : comparison <= 0;
    const DecodedOperand &selected = selectFS ? fs : ft;
    const std::uint32_t selectedBits =
      selectFS ? fsBits : ftBits;
    return selected.zero
      ? signedZero(selected.negative)
      : EEFloatResult{selectedBits, 0};
  }

  std::uint32_t integerSquareRoot(std::uint64_t value)
  {
    std::uint64_t result = 0;
    std::uint64_t bit = std::uint64_t{1} << 62;
    while (bit > value)
    {
      bit >>= 2;
    }
    while (bit != 0)
    {
      if (value >= result + bit)
      {
        value -= result + bit;
        result = (result >> 1) + bit;
      }
      else
      {
        result >>= 1;
      }
      bit >>= 2;
    }
    return static_cast<std::uint32_t>(result);
  }

  VUFloatResult squareRootRaw(
    std::uint32_t bits,
    bool preserveZeroSign)
  {
    const DecodedOperand operand = decodeOperand(bits);
    if (operand.zero)
    {
      return signedZero(preserveZeroSign && operand.negative);
    }

    int exponent = operand.exponent / 2;
    if (operand.exponent < 0 && (operand.exponent % 2) != 0)
    {
      exponent--;
    }
    const int exponentRemainder = operand.exponent - exponent * 2;
    const std::uint64_t radicand =
      static_cast<std::uint64_t>(operand.significand) <<
      (23 + exponentRemainder);
    const std::uint32_t significand = integerSquareRoot(radicand);
    VUFloatResult result =
      encodeNormalizedResult(false, exponent, significand);
    if (operand.negative)
    {
      result.flags = FP_FLAG_I_BIT;
    }
    return result;
  }

  double maxVUValue()
  {
    return std::ldexp(2.0 - std::ldexp(1.0, -23), FP_MAX_EXPONENT);
  }
}

EEFloatDecomposition decomposeEEFloat(std::uint32_t bits)
{
  const std::uint8_t encodedExponent =
    static_cast<std::uint8_t>((bits >> 23) & 0xff);
  const std::uint32_t mantissa = bits & FP_MAX_MANTISSA;
  EEFloatClassification classification = EEFloatClassification::Normal;
  IEEEFloatEncoding ieeeEncoding = IEEEFloatEncoding::Normal;
  if (encodedExponent == 0)
  {
    classification = EEFloatClassification::Zero;
    ieeeEncoding =
      mantissa == 0
        ? IEEEFloatEncoding::Zero
        : IEEEFloatEncoding::Subnormal;
  }
  else if (encodedExponent == 0xff)
  {
    classification = EEFloatClassification::ExtendedFinite;
    ieeeEncoding =
      mantissa == 0
        ? IEEEFloatEncoding::Infinity
        : IEEEFloatEncoding::NaN;
  }

  return {
    (bits & FP_SIGN_BIT) != 0,
    encodedExponent,
    static_cast<std::int16_t>(
      static_cast<std::int32_t>(encodedExponent) - 127),
    mantissa,
    classification,
    ieeeEncoding
  };
}

VUFloatDecomposition decomposeVUFloat(std::uint32_t bits)
{
  return decomposeEEFloat(bits);
}

EEFloatResult normalizeEEFloat(
  bool negative,
  std::uint64_t magnitude,
  std::int16_t leastSignificantBitExponent)
{
  if (magnitude == 0)
  {
    return signedZero(negative);
  }

  const int magnitudeHighestBit = highestBit(magnitude);
  std::uint32_t significand = 0;
  if (magnitudeHighestBit > 23)
  {
    significand = static_cast<std::uint32_t>(
      magnitude >> (magnitudeHighestBit - 23));
  }
  else
  {
    significand = static_cast<std::uint32_t>(
      magnitude << (23 - magnitudeHighestBit));
  }

  return encodeNormalizedResult(
    negative,
    magnitudeHighestBit + leastSignificantBitExponent,
    significand);
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
  return divideRaw(d1Bits, d2Bits);
}

VUFloatResult maxFPRaw(std::uint32_t d1Bits, std::uint32_t d2Bits)
{
  return selectRaw(d1Bits, d2Bits, true);
}

VUFloatResult minFPRaw(std::uint32_t d1Bits, std::uint32_t d2Bits)
{
  return selectRaw(d1Bits, d2Bits, false);
}

EEFloatResult maxEEFloatRaw(
  std::uint32_t fsBits,
  std::uint32_t ftBits)
{
  return selectEERaw(fsBits, ftBits, true);
}

EEFloatResult minEEFloatRaw(
  std::uint32_t fsBits,
  std::uint32_t ftBits)
{
  return selectEERaw(fsBits, ftBits, false);
}

EEFloatResult sqrtEEFloatRaw(std::uint32_t bits)
{
  return squareRootRaw(bits, true);
}

EEFloatResult rsqrtEEFloatRaw(
  std::uint32_t numeratorBits,
  std::uint32_t radicandBits)
{
  const EEFloatResult root =
    squareRootRaw(radicandBits, true);
  EEFloatResult result = divideRaw(numeratorBits, root.bits);
  result.flags |= root.flags;
  return result;
}

VUFloatResult sqrtFPRaw(std::uint32_t bits)
{
  return squareRootRaw(bits, false);
}

VUFloatResult rsqrtFPRaw(
  std::uint32_t numeratorBits,
  std::uint32_t radicandBits)
{
  const VUFloatResult root =
    squareRootRaw(radicandBits, false);
  const std::uint32_t rootBits =
    (root.bits & ~FP_SIGN_BIT) == 0
      ? root.bits | (radicandBits & FP_SIGN_BIT)
      : root.bits;
  VUFloatResult result = divideRaw(numeratorBits, rootBits);
  result.flags |= root.flags;
  return result;
}

VUFloatResult subFPRaw(std::uint32_t d1Bits, std::uint32_t d2Bits)
{
  return addRaw(d1Bits, d2Bits ^ FP_SIGN_BIT);
}

EEFloatResult convertEEFloatToWordRaw(std::uint32_t bits)
{
  const std::uint32_t encodedExponent = (bits >> 23) & 0xff;
  if (encodedExponent > 0x9d)
  {
    return {
      (bits & FP_SIGN_BIT) != 0
        ? UINT32_C(0x80000000)
        : UINT32_C(0x7fffffff),
      FP_FLAG_I_BIT
    };
  }
  return {floatToFixedRaw(bits, 0), 0};
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

std::uint32_t floatToFixedRaw(
  std::uint32_t bits,
  std::uint8_t fractionalBits)
{
  const bool negative = (bits & FP_SIGN_BIT) != 0;
  const std::uint32_t encodedExponent = (bits >> 23) & 0xff;
  if (encodedExponent == 0)
  {
    return 0;
  }

  const std::uint32_t significand =
    0x800000u | (bits & FP_MAX_MANTISSA);
  const int shift =
    static_cast<int>(encodedExponent) - 127 - 23 + fractionalBits;
  const std::uint64_t limit =
    negative ? 0x80000000ull : 0x7fffffffull;
  std::uint64_t magnitude;

  if (shift > 8)
  {
    magnitude = limit + 1;
  }
  else if (shift >= 0)
  {
    magnitude = static_cast<std::uint64_t>(significand) << shift;
  }
  else if (shift <= -24)
  {
    magnitude = 0;
  }
  else
  {
    magnitude = significand >> -shift;
  }

  if (magnitude > limit)
  {
    return negative ? 0x80000000u : 0x7fffffffu;
  }
  if (negative && magnitude != 0)
  {
    return 0u - static_cast<std::uint32_t>(magnitude);
  }
  return static_cast<std::uint32_t>(magnitude);
}

std::uint32_t fixedToFloatRaw(
  std::uint32_t bits,
  std::uint8_t fractionalBits)
{
  if (bits == 0)
  {
    return 0;
  }

  const bool negative = (bits & FP_SIGN_BIT) != 0;
  const std::uint32_t magnitude = negative ? 0u - bits : bits;
  return normalizeEEFloat(
    negative,
    magnitude,
    -static_cast<std::int16_t>(fractionalBits)).bits;
}

namespace
{
  std::int32_t doubleToInteger(double value, std::uint8_t fractionalBits)
  {
    const VUFloat source(value);
    const std::uint32_t resultBits =
      floatToFixedRaw(source.bits(), fractionalBits);
    std::int32_t result;
    std::memcpy(&result, &resultBits, sizeof(result));
    return result;
  }

  double integerToDouble(std::int32_t value, std::uint8_t fractionalBits)
  {
    std::uint32_t sourceBits;
    std::memcpy(&sourceBits, &value, sizeof(sourceBits));
    VUFloat result;
    result.setBits(fixedToFloatRaw(sourceBits, fractionalBits));
    return result;
  }
}

std::int32_t doubleToInteger0(double d)
{
  return doubleToInteger(d, 0);
}

std::int32_t doubleToInteger4(double d)
{
  return doubleToInteger(d, 4);
}

std::int32_t doubleToInteger12(double d)
{
  return doubleToInteger(d, 12);
}

std::int32_t doubleToInteger15(double d)
{
  return doubleToInteger(d, 15);
}

double integer0ToDouble(std::int32_t i)
{
  return integerToDouble(i, 0);
}

double integer4ToDouble(std::int32_t i)
{
  return integerToDouble(i, 4);
}

double integer12ToDouble(std::int32_t i)
{
  return integerToDouble(i, 12);
}

double integer15ToDouble(std::int32_t i)
{
  return integerToDouble(i, 15);
}
