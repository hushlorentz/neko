#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "ee_core.hpp"
#include "ee_instruction.hpp"
#include "floating_point_ops.hpp"
#include "neko_system.hpp"

namespace
{
  constexpr std::uint8_t COP1_DIV_SQRT_LATENCY = 8;
  constexpr std::uint8_t COP1_RSQRT_LATENCY = 14;

  std::uint32_t cop1TransferInstruction(
    std::uint8_t source,
    std::uint8_t target,
    std::uint8_t floatingPointRegister)
  {
    return
      (UINT32_C(0x11) << 26) |
      (static_cast<std::uint32_t>(source) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      (static_cast<std::uint32_t>(floatingPointRegister) << 11);
  }

  std::uint32_t cop1MemoryInstruction(
    std::uint8_t opcode,
    std::uint8_t base,
    std::uint8_t floatingPointRegister,
    std::uint16_t offset)
  {
    return
      (static_cast<std::uint32_t>(opcode) << 26) |
      (static_cast<std::uint32_t>(base) << 21) |
      (static_cast<std::uint32_t>(
        floatingPointRegister) << 16) |
      offset;
  }

  std::uint32_t cop1SingleInstruction(
    std::uint8_t function,
    std::uint8_t source,
    std::uint8_t destination,
    std::uint8_t target = 0)
  {
    return
      (UINT32_C(0x11) << 26) |
      (UINT32_C(0x10) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      (static_cast<std::uint32_t>(source) << 11) |
      (static_cast<std::uint32_t>(destination) << 6) |
      function;
  }

  std::uint32_t cop1WordInstruction(
    std::uint8_t function,
    std::uint8_t source,
    std::uint8_t destination,
    std::uint8_t target = 0)
  {
    return
      (UINT32_C(0x11) << 26) |
      (UINT32_C(0x14) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      (static_cast<std::uint32_t>(source) << 11) |
      (static_cast<std::uint32_t>(destination) << 6) |
      function;
  }

  std::uint32_t cop1BranchInstruction(
    std::uint8_t condition,
    std::uint16_t offset)
  {
    return
      (UINT32_C(0x11) << 26) |
      (UINT32_C(0x08) << 21) |
      (static_cast<std::uint32_t>(condition) << 16) |
      offset;
  }

  void runInstruction(
    NekoSystem *system,
    std::uint32_t instruction)
  {
    system->eeBus().write32(0, instruction);
    system->eeCore().startExecution(0);
    system->clockMasterCycle();
  }
}

TEST_CASE("EE COP1 raw values expose EE and IEEE classifications")
{
  struct ClassificationVector
  {
    std::uint32_t bits;
    bool negative;
    std::uint8_t encodedExponent;
    std::int16_t unbiasedExponent;
    std::uint32_t mantissa;
    EEFloatClassification classification;
    IEEEFloatEncoding ieeeEncoding;
  };

  const ClassificationVector vectors[] = {
    {
      UINT32_C(0x00000000),
      false,
      0,
      -127,
      0,
      EEFloatClassification::Zero,
      IEEEFloatEncoding::Zero
    },
    {
      UINT32_C(0x80000000),
      true,
      0,
      -127,
      0,
      EEFloatClassification::Zero,
      IEEEFloatEncoding::Zero
    },
    {
      UINT32_C(0x807fffff),
      true,
      0,
      -127,
      UINT32_C(0x7fffff),
      EEFloatClassification::Zero,
      IEEEFloatEncoding::Subnormal
    },
    {
      UINT32_C(0x3fc12345),
      false,
      127,
      0,
      UINT32_C(0x412345),
      EEFloatClassification::Normal,
      IEEEFloatEncoding::Normal
    },
    {
      UINT32_C(0x7f800000),
      false,
      255,
      128,
      0,
      EEFloatClassification::ExtendedFinite,
      IEEEFloatEncoding::Infinity
    },
    {
      UINT32_C(0xffc00000),
      true,
      255,
      128,
      UINT32_C(0x400000),
      EEFloatClassification::ExtendedFinite,
      IEEEFloatEncoding::NaN
    }
  };

  for (const ClassificationVector &vector : vectors)
  {
    const EEFloatDecomposition value =
      decomposeEEFloat(vector.bits);

    REQUIRE(value.negative == vector.negative);
    REQUIRE(value.encodedExponent == vector.encodedExponent);
    REQUIRE(value.unbiasedExponent == vector.unbiasedExponent);
    REQUIRE(value.mantissa == vector.mantissa);
    REQUIRE(value.classification == vector.classification);
    REQUIRE(value.ieeeEncoding == vector.ieeeEncoding);
  }
}

TEST_CASE("EE COP1 normalization uses exact truncating binary arithmetic")
{
  SECTION("Exact magnitudes normalize left without changing their value")
  {
    const EEFloatResult result =
      normalizeEEFloat(false, UINT64_C(1), 0);

    REQUIRE(result.bits == UINT32_C(0x3f800000));
    REQUIRE(result.flags == 0);
  }

  SECTION("Wide magnitudes normalize right and discard low bits")
  {
    const EEFloatResult result =
      normalizeEEFloat(
        false,
        UINT64_C(0x1000001),
        -24);

    REQUIRE(result.bits == UINT32_C(0x3f800000));
    REQUIRE(result.flags == 0);
  }

  SECTION("Negative magnitudes truncate toward zero symmetrically")
  {
    const EEFloatResult result =
      normalizeEEFloat(
        true,
        UINT64_C(0x1000001),
        -24);

    REQUIRE(result.bits == UINT32_C(0xbf800000));
    REQUIRE(result.flags == 0);
  }

  SECTION("Normalization renormalizes after leading-bit cancellation")
  {
    const EEFloatResult result =
      normalizeEEFloat(
        false,
        UINT64_C(0x7fffff),
        -23);

    REQUIRE(result.bits == UINT32_C(0x3f7ffffe));
    REQUIRE(result.flags == 0);
  }

  SECTION("Exponent 255 remains a finite EE encoding")
  {
    const EEFloatResult result =
      normalizeEEFloat(
        false,
        UINT64_C(0xffffff),
        105);

    REQUIRE(result.bits == UINT32_C(0x7fffffff));
    REQUIRE(result.flags == 0);
  }
}

TEST_CASE("EE COP1 normalization saturates and flushes exponent ranges")
{
  struct RangeVector
  {
    bool negative;
    std::uint64_t magnitude;
    std::int16_t leastSignificantBitExponent;
    std::uint32_t expectedBits;
    std::uint8_t expectedFlags;
  };

  const RangeVector vectors[] = {
    {
      false,
      UINT64_C(1),
      -126,
      UINT32_C(0x00800000),
      0
    },
    {
      true,
      UINT64_C(1),
      -126,
      UINT32_C(0x80800000),
      0
    },
    {
      false,
      UINT64_C(0xffffff),
      105,
      UINT32_C(0x7fffffff),
      0
    },
    {
      true,
      UINT64_C(0xffffff),
      105,
      UINT32_C(0xffffffff),
      0
    },
    {
      false,
      UINT64_C(1),
      129,
      UINT32_C(0x7fffffff),
      FP_FLAG_OVERFLOW
    },
    {
      true,
      UINT64_C(1),
      129,
      UINT32_C(0xffffffff),
      FP_FLAG_OVERFLOW
    },
    {
      false,
      UINT64_C(1),
      -127,
      0,
      FP_FLAG_UNDERFLOW
    },
    {
      true,
      UINT64_C(1),
      -127,
      FP_SIGN_BIT,
      FP_FLAG_UNDERFLOW
    },
    {
      false,
      0,
      129,
      0,
      0
    },
    {
      true,
      0,
      -127,
      FP_SIGN_BIT,
      0
    }
  };

  for (const RangeVector &vector : vectors)
  {
    const EEFloatResult result =
      normalizeEEFloat(
        vector.negative,
        vector.magnitude,
        vector.leastSignificantBitExponent);

    REQUIRE(result.bits == vector.expectedBits);
    REQUIRE(result.flags == vector.expectedFlags);
  }
}

TEST_CASE("EE COP1 arithmetic follows the documented signed-zero table")
{
  struct BinaryVector
  {
    EEFloatResult (*operation)(std::uint32_t, std::uint32_t);
    std::uint32_t left;
    std::uint32_t right;
    std::uint32_t expectedBits;
    std::uint8_t expectedFlags;
  };

  const BinaryVector vectors[] = {
    {addFPRaw, 0, 0, 0, 0},
    {addFPRaw, 0, FP_SIGN_BIT, 0, 0},
    {addFPRaw, FP_SIGN_BIT, 0, 0, 0},
    {addFPRaw, FP_SIGN_BIT, FP_SIGN_BIT, FP_SIGN_BIT, 0},
    {subFPRaw, 0, 0, 0, 0},
    {subFPRaw, 0, FP_SIGN_BIT, 0, 0},
    {subFPRaw, FP_SIGN_BIT, 0, FP_SIGN_BIT, 0},
    {subFPRaw, FP_SIGN_BIT, FP_SIGN_BIT, 0, 0},
    {mulFPRaw, 0, 0, 0, 0},
    {mulFPRaw, 0, FP_SIGN_BIT, FP_SIGN_BIT, 0},
    {mulFPRaw, FP_SIGN_BIT, 0, FP_SIGN_BIT, 0},
    {mulFPRaw, FP_SIGN_BIT, FP_SIGN_BIT, 0, 0},
    {divFPRaw, 0, 0, UINT32_C(0x7fffffff), FP_FLAG_I_BIT},
    {
      divFPRaw,
      0,
      FP_SIGN_BIT,
      UINT32_C(0xffffffff),
      FP_FLAG_I_BIT
    },
    {
      divFPRaw,
      FP_SIGN_BIT,
      0,
      UINT32_C(0xffffffff),
      FP_FLAG_I_BIT
    },
    {
      divFPRaw,
      FP_SIGN_BIT,
      FP_SIGN_BIT,
      UINT32_C(0x7fffffff),
      FP_FLAG_I_BIT
    },
    {maxFPRaw, 0, 0, 0, 0},
    {maxFPRaw, 0, FP_SIGN_BIT, 0, 0},
    {maxFPRaw, FP_SIGN_BIT, 0, 0, 0},
    {maxFPRaw, FP_SIGN_BIT, FP_SIGN_BIT, FP_SIGN_BIT, 0},
    {minFPRaw, 0, 0, 0, 0},
    {minFPRaw, 0, FP_SIGN_BIT, FP_SIGN_BIT, 0},
    {minFPRaw, FP_SIGN_BIT, 0, FP_SIGN_BIT, 0},
    {minFPRaw, FP_SIGN_BIT, FP_SIGN_BIT, FP_SIGN_BIT, 0}
  };

  for (const BinaryVector &vector : vectors)
  {
    const EEFloatResult result =
      vector.operation(vector.left, vector.right);

    REQUIRE(result.bits == vector.expectedBits);
    REQUIRE(result.flags == vector.expectedFlags);
  }
}

TEST_CASE("EE COP1 square root operations preserve signed-zero behavior")
{
  REQUIRE(sqrtEEFloatRaw(0).bits == 0);
  REQUIRE(sqrtEEFloatRaw(FP_SIGN_BIT).bits == FP_SIGN_BIT);
  REQUIRE(sqrtEEFloatRaw(UINT32_C(0x807fffff)).bits == FP_SIGN_BIT);

  struct ReciprocalSquareRootVector
  {
    std::uint32_t numerator;
    std::uint32_t radicand;
    std::uint32_t expectedBits;
    std::uint8_t expectedFlags;
  };

  const ReciprocalSquareRootVector vectors[] = {
    {0, 0, UINT32_C(0x7fffffff), FP_FLAG_D_BIT},
    {0, FP_SIGN_BIT, UINT32_C(0xffffffff), FP_FLAG_D_BIT},
    {FP_SIGN_BIT, 0, UINT32_C(0xffffffff), FP_FLAG_D_BIT},
    {
      FP_SIGN_BIT,
      FP_SIGN_BIT,
      UINT32_C(0x7fffffff),
      FP_FLAG_D_BIT
    },
    {
      UINT32_C(0x3f800000),
      0,
      UINT32_C(0x7fffffff),
      FP_FLAG_D_BIT
    },
    {
      UINT32_C(0x3f800000),
      FP_SIGN_BIT,
      UINT32_C(0xffffffff),
      FP_FLAG_D_BIT
    },
    {
      UINT32_C(0xbf800000),
      0,
      UINT32_C(0xffffffff),
      FP_FLAG_D_BIT
    },
    {
      UINT32_C(0xbf800000),
      FP_SIGN_BIT,
      UINT32_C(0x7fffffff),
      FP_FLAG_D_BIT
    }
  };

  for (const ReciprocalSquareRootVector &vector : vectors)
  {
    const EEFloatResult result =
      rsqrtEEFloatRaw(vector.numerator, vector.radicand);

    REQUIRE(result.bits == vector.expectedBits);
    REQUIRE(result.flags == vector.expectedFlags);
  }
}

TEST_CASE("EE COP1 min and max compare raw finite values without host floats")
{
  REQUIRE(
    maxFPRaw(
      UINT32_C(0x7fc00000),
      UINT32_C(0x7f800000)).bits ==
    UINT32_C(0x7fc00000));
  REQUIRE(
    minFPRaw(
      UINT32_C(0xffc00000),
      UINT32_C(0xff800000)).bits ==
    UINT32_C(0xffc00000));
  REQUIRE(
    maxFPRaw(
      UINT32_C(0x807fffff),
      UINT32_C(0xbf800000)).bits ==
    FP_SIGN_BIT);
  REQUIRE(
    minFPRaw(
      UINT32_C(0x007fffff),
      UINT32_C(0x3f800000)).bits ==
    0);
}

TEST_CASE("EE COP1 manual result tables have fixed raw-bit vectors")
{
  struct ReferenceVector
  {
    const char *description;
    EEFloatResult result;
    std::uint32_t expectedBits;
    std::uint8_t affectedFlags;
    std::uint8_t expectedFlags;
  };

  const ReferenceVector vectors[] = {
    {
      "zero divided by zero saturates and raises invalid",
      divFPRaw(0, 0),
      UINT32_C(0x7fffffff),
      FP_FLAG_I_BIT | FP_FLAG_D_BIT,
      FP_FLAG_I_BIT
    },
    {
      "negative divided by zero saturates and raises division by zero",
      divFPRaw(UINT32_C(0xbf800000), 0),
      UINT32_C(0xffffffff),
      FP_FLAG_I_BIT | FP_FLAG_D_BIT,
      FP_FLAG_D_BIT
    },
    {
      "negative square root uses the absolute value and raises invalid",
      sqrtEEFloatRaw(UINT32_C(0xc0800000)),
      UINT32_C(0x40000000),
      FP_FLAG_I_BIT | FP_FLAG_D_BIT,
      FP_FLAG_I_BIT
    },
    {
      "exponent overflow saturates to positive maximum",
      mulFPRaw(UINT32_C(0x7fffffff), UINT32_C(0x40000000)),
      UINT32_C(0x7fffffff),
      FP_FLAG_OVERFLOW | FP_FLAG_UNDERFLOW,
      FP_FLAG_OVERFLOW
    },
    {
      "exponent underflow flushes to negative zero",
      mulFPRaw(UINT32_C(0x80800000), UINT32_C(0x3f000000)),
      FP_SIGN_BIT,
      FP_FLAG_OVERFLOW | FP_FLAG_UNDERFLOW,
      FP_FLAG_UNDERFLOW
    },
    {
      "rounding discards product bits below the EE significand",
      mulFPRaw(UINT32_C(0x3f800001), UINT32_C(0x3fc00000)),
      UINT32_C(0x3fc00001),
      FP_FLAG_OVERFLOW | FP_FLAG_UNDERFLOW,
      0
    },
    {
      "IEEE infinity encodings participate as finite EE values",
      addFPRaw(UINT32_C(0x7f800000), UINT32_C(0xff800000)),
      0,
      FP_FLAG_OVERFLOW | FP_FLAG_UNDERFLOW,
      0
    }
  };

  for (const ReferenceVector &vector : vectors)
  {
    CAPTURE(vector.description);
    REQUIRE(vector.result.bits == vector.expectedBits);
    REQUIRE(vector.result.flags == vector.expectedFlags);

    NekoSystem system;
    system.eeCore().updateCOP1ArithmeticFlags(
      vector.affectedFlags,
      vector.result.flags);
    const std::uint32_t status =
      system.eeCore().cop1ControlRegister(31);

    if ((vector.expectedFlags & FP_FLAG_I_BIT) != 0)
    {
      REQUIRE(
        (status & EECOP1Control::CAUSE_INVALID) != 0);
      REQUIRE(
        (status & EECOP1Control::STICKY_INVALID) != 0);
    }
    if ((vector.expectedFlags & FP_FLAG_D_BIT) != 0)
    {
      REQUIRE(
        (status &
         EECOP1Control::CAUSE_DIVISION_BY_ZERO) != 0);
      REQUIRE(
        (status &
         EECOP1Control::STICKY_DIVISION_BY_ZERO) != 0);
    }
    if ((vector.expectedFlags & FP_FLAG_OVERFLOW) != 0)
    {
      REQUIRE(
        (status & EECOP1Control::CAUSE_OVERFLOW) != 0);
      REQUIRE(
        (status & EECOP1Control::STICKY_OVERFLOW) != 0);
    }
    if ((vector.expectedFlags & FP_FLAG_UNDERFLOW) != 0)
    {
      REQUIRE(
        (status & EECOP1Control::CAUSE_UNDERFLOW) != 0);
      REQUIRE(
        (status & EECOP1Control::STICKY_UNDERFLOW) != 0);
    }
    REQUIRE(
      (status & EECOP1Control::CAUSE_MASK) ==
      (status & EECOP1Control::STICKY_MASK) << 11);
  }
}

TEST_CASE("EE COP1 memory transfer instructions decode canonically")
{
  REQUIRE(
    decodeEEInstruction(
      cop1MemoryInstruction(
        0x31,
        1,
        2,
        0x3456)).operation ==
    EEOperation::LoadWordToCOP1);
  REQUIRE(
    decodeEEInstruction(
      cop1MemoryInstruction(
        0x39,
        1,
        2,
        0x3456)).operation ==
    EEOperation::StoreWordFromCOP1);
}

TEST_CASE("EE COP1 word transfer instructions decode canonically")
{
  REQUIRE(
    decodeEEInstruction(
      cop1TransferInstruction(0x00, 2, 3)).operation ==
    EEOperation::MoveWordFromCOP1);
  REQUIRE(
    decodeEEInstruction(
      cop1TransferInstruction(0x04, 2, 3)).operation ==
    EEOperation::MoveWordToCOP1);
  REQUIRE(
    decodeEEInstruction(
      cop1TransferInstruction(0x02, 2, 0)).operation ==
    EEOperation::MoveControlWordFromCOP1);
  REQUIRE(
    decodeEEInstruction(
      cop1TransferInstruction(0x06, 2, 31)).operation ==
    EEOperation::MoveControlWordToCOP1);

  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop1TransferInstruction(0x00, 2, 3) | 1),
    "Reserved EE instruction encoding.");
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop1TransferInstruction(0x04, 2, 3) | 1),
    "Reserved EE instruction encoding.");
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop1TransferInstruction(0x02, 2, 0) | 1),
    "Reserved EE instruction encoding.");
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop1TransferInstruction(0x06, 2, 31) | 1),
    "Reserved EE instruction encoding.");
}

TEST_CASE("EE COP1 single movement instructions decode canonically")
{
  REQUIRE(
    decodeEEInstruction(
      cop1SingleInstruction(0x05, 2, 3)).operation ==
    EEOperation::AbsoluteSingleCOP1);
  REQUIRE(
    decodeEEInstruction(
      cop1SingleInstruction(0x06, 2, 3)).operation ==
    EEOperation::MoveSingleCOP1);
  REQUIRE(
    decodeEEInstruction(
      cop1SingleInstruction(0x07, 2, 3)).operation ==
    EEOperation::NegateSingleCOP1);

  for (const std::uint8_t function : {0x05, 0x06, 0x07})
  {
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(
        cop1SingleInstruction(function, 2, 3, 1)),
      "Reserved EE instruction encoding.");
  }
}

TEST_CASE("EE COP1 add and subtract instructions decode canonically")
{
  REQUIRE(
    decodeEEInstruction(
      cop1SingleInstruction(0x00, 2, 4, 3)).operation ==
    EEOperation::AddSingleCOP1);
  REQUIRE(
    decodeEEInstruction(
      cop1SingleInstruction(0x01, 2, 4, 3)).operation ==
    EEOperation::SubtractSingleCOP1);
}

TEST_CASE("EE COP1 multiply instructions decode canonically")
{
  REQUIRE(
    decodeEEInstruction(
      cop1SingleInstruction(0x02, 2, 4, 3)).operation ==
    EEOperation::MultiplySingleCOP1);
  REQUIRE(
    decodeEEInstruction(
      cop1SingleInstruction(0x1a, 2, 0, 3)).operation ==
    EEOperation::MultiplySingleToAccumulatorCOP1);

  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop1SingleInstruction(0x1a, 2, 1, 3)),
    "Reserved EE instruction encoding.");
}

TEST_CASE("EE COP1 divide decodes canonically")
{
  REQUIRE(
    decodeEEInstruction(
      cop1SingleInstruction(0x03, 2, 4, 3)).operation ==
    EEOperation::DivideSingleCOP1);
}

TEST_CASE("EE COP1 square root decodes only with fs zero")
{
  REQUIRE(
    decodeEEInstruction(
      cop1SingleInstruction(0x04, 0, 4, 3)).operation ==
    EEOperation::SquareRootSingleCOP1);

  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop1SingleInstruction(0x04, 2, 4, 3)),
    "Reserved EE instruction encoding.");
}

TEST_CASE("EE COP1 reciprocal square root decodes canonically")
{
  REQUIRE(
    decodeEEInstruction(
      cop1SingleInstruction(0x16, 2, 4, 3)).operation ==
    EEOperation::ReciprocalSquareRootSingleCOP1);
}

TEST_CASE("EE COP1 multiply-add instructions decode canonically")
{
  REQUIRE(
    decodeEEInstruction(
      cop1SingleInstruction(0x1c, 2, 4, 3)).operation ==
    EEOperation::MultiplyAddSingleCOP1);
  REQUIRE(
    decodeEEInstruction(
      cop1SingleInstruction(0x1e, 2, 0, 3)).operation ==
    EEOperation::MultiplyAddSingleToAccumulatorCOP1);

  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop1SingleInstruction(0x1e, 2, 1, 3)),
    "Reserved EE instruction encoding.");
}

TEST_CASE("EE COP1 multiply-subtract instructions decode canonically")
{
  REQUIRE(
    decodeEEInstruction(
      cop1SingleInstruction(0x1d, 2, 4, 3)).operation ==
    EEOperation::MultiplySubtractSingleCOP1);
  REQUIRE(
    decodeEEInstruction(
      cop1SingleInstruction(0x1f, 2, 0, 3)).operation ==
    EEOperation::MultiplySubtractSingleToAccumulatorCOP1);

  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop1SingleInstruction(0x1f, 2, 1, 3)),
    "Reserved EE instruction encoding.");
}

TEST_CASE(
  "EE COP1 accumulator add and subtract decode only with fd zero")
{
  REQUIRE(
    decodeEEInstruction(
      cop1SingleInstruction(0x18, 2, 0, 3)).operation ==
    EEOperation::AddSingleToAccumulatorCOP1);
  REQUIRE(
    decodeEEInstruction(
      cop1SingleInstruction(0x19, 2, 0, 3)).operation ==
    EEOperation::SubtractSingleToAccumulatorCOP1);

  for (const std::uint8_t function : {0x18, 0x19})
  {
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(
        cop1SingleInstruction(function, 2, 1, 3)),
      "Reserved EE instruction encoding.");
  }
}

TEST_CASE("EE COP1 multiply produces exact raw results")
{
  struct ArithmeticVector
  {
    std::uint32_t fs;
    std::uint32_t ft;
    std::uint32_t expected;
  };
  const ArithmeticVector vectors[] = {
    {UINT32_C(0xc0000000), UINT32_C(0x40400000),
     UINT32_C(0xc0c00000)},
    {UINT32_C(0x3f800001), UINT32_C(0x3fc00000),
     UINT32_C(0x3fc00001)},
    {UINT32_C(0x00000001), UINT32_C(0xc0000000),
     FP_SIGN_BIT},
    {UINT32_C(0x807fffff), UINT32_C(0xc0000000), 0},
    {UINT32_C(0x7fc00000), UINT32_C(0x3f000000),
     UINT32_C(0x7f400000)}
  };

  for (const ArithmeticVector &vector : vectors)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, vector.fs);
    core.setFloatingPointRegister(3, vector.ft);

    runInstruction(
      &system,
      cop1SingleInstruction(0x02, 2, 4, 3));

    REQUIRE(core.floatingPointRegister(2) == vector.fs);
    REQUIRE(core.floatingPointRegister(3) == vector.ft);
    REQUIRE(
      core.floatingPointRegister(4) ==
      vector.expected);
  }
}

TEST_CASE("EE COP1 divide produces exact raw results")
{
  struct ArithmeticVector
  {
    std::uint32_t fs;
    std::uint32_t ft;
    std::uint32_t expected;
  };
  const ArithmeticVector vectors[] = {
    {UINT32_C(0x40c00000), UINT32_C(0x40000000),
     UINT32_C(0x40400000)},
    {UINT32_C(0xc0c00000), UINT32_C(0x40000000),
     UINT32_C(0xc0400000)},
    {UINT32_C(0x40c00000), UINT32_C(0xc0000000),
     UINT32_C(0xc0400000)},
    {UINT32_C(0x00000001), UINT32_C(0x3f800000), 0},
    {UINT32_C(0x80000001), UINT32_C(0x3f800000),
     FP_SIGN_BIT},
    {UINT32_C(0x7fffffff), UINT32_C(0x00800000),
     UINT32_C(0x7fffffff)}
  };

  for (const ArithmeticVector &vector : vectors)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, vector.fs);
    core.setFloatingPointRegister(3, vector.ft);

    runInstruction(
      &system,
      cop1SingleInstruction(0x03, 2, 4, 3));
    system.runMasterCycles(COP1_DIV_SQRT_LATENCY);

    REQUIRE(core.floatingPointRegister(2) == vector.fs);
    REQUIRE(core.floatingPointRegister(3) == vector.ft);
    REQUIRE(
      core.floatingPointRegister(4) ==
      vector.expected);
  }
}

TEST_CASE("EE COP1 divide supports in-place writes")
{
  SECTION("The destination may alias fs")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x40c00000));
    core.setFloatingPointRegister(3, UINT32_C(0x40000000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x03, 2, 2, 3));
    system.runMasterCycles(COP1_DIV_SQRT_LATENCY);

    REQUIRE(
      core.floatingPointRegister(2) ==
      UINT32_C(0x40400000));
  }

  SECTION("The destination may alias ft")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x40c00000));
    core.setFloatingPointRegister(3, UINT32_C(0x40000000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x03, 2, 3, 3));
    system.runMasterCycles(COP1_DIV_SQRT_LATENCY);

    REQUIRE(
      core.floatingPointRegister(3) ==
      UINT32_C(0x40400000));
  }
}

TEST_CASE("EE COP1 divide updates only invalid and division flags")
{
  constexpr std::uint32_t INITIAL_STATUS =
    EECOP1Control::CAUSE_MASK |
    EECOP1Control::STICKY_MASK;

  SECTION("An ordinary quotient clears current I and D only")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP1ControlRegister(31, INITIAL_STATUS);
    core.setFloatingPointRegister(2, UINT32_C(0x40c00000));
    core.setFloatingPointRegister(3, UINT32_C(0x40000000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x03, 2, 4, 3));
    system.runMasterCycles(COP1_DIV_SQRT_LATENCY);

    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_OVERFLOW |
       EECOP1Control::CAUSE_UNDERFLOW |
       EECOP1Control::STICKY_MASK));
  }

  SECTION("Zero divided by zero raises invalid with the quotient sign")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, FP_SIGN_BIT);
    core.setFloatingPointRegister(3, 0);

    runInstruction(
      &system,
      cop1SingleInstruction(0x03, 2, 4, 3));
    system.runMasterCycles(COP1_DIV_SQRT_LATENCY);

    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0xffffffff));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_INVALID |
       EECOP1Control::STICKY_INVALID));
  }

  SECTION("A nonzero numerator divided by zero raises division by zero")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0xbf800000));
    core.setFloatingPointRegister(3, FP_SIGN_BIT);

    runInstruction(
      &system,
      cop1SingleInstruction(0x03, 2, 4, 3));
    system.runMasterCycles(COP1_DIV_SQRT_LATENCY);

    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x7fffffff));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_DIVISION_BY_ZERO |
       EECOP1Control::STICKY_DIVISION_BY_ZERO));
  }
}

TEST_CASE("EE COP1 square root produces exact raw results")
{
  struct ArithmeticVector
  {
    std::uint32_t ft;
    std::uint32_t expected;
  };
  const ArithmeticVector vectors[] = {
    {UINT32_C(0x41100000), UINT32_C(0x40400000)},
    {UINT32_C(0x41200000), UINT32_C(0x404a62c1)},
    {UINT32_C(0xc1100000), UINT32_C(0x40400000)},
    {0, 0},
    {FP_SIGN_BIT, FP_SIGN_BIT},
    {UINT32_C(0x807fffff), FP_SIGN_BIT}
  };

  for (const ArithmeticVector &vector : vectors)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(3, vector.ft);

    runInstruction(
      &system,
      cop1SingleInstruction(0x04, 0, 4, 3));
    system.runMasterCycles(COP1_DIV_SQRT_LATENCY);

    REQUIRE(core.floatingPointRegister(3) == vector.ft);
    REQUIRE(
      core.floatingPointRegister(4) ==
      vector.expected);
  }
}

TEST_CASE("EE COP1 square root supports an in-place ft destination")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setFloatingPointRegister(3, UINT32_C(0x41100000));

  runInstruction(
    &system,
    cop1SingleInstruction(0x04, 0, 3, 3));
  system.runMasterCycles(COP1_DIV_SQRT_LATENCY);

  REQUIRE(
    core.floatingPointRegister(3) ==
    UINT32_C(0x40400000));
}

TEST_CASE("EE COP1 square root updates only invalid and division flags")
{
  constexpr std::uint32_t INITIAL_STATUS =
    EECOP1Control::CAUSE_MASK |
    EECOP1Control::STICKY_MASK;

  SECTION("A nonnegative operand clears current I and D only")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP1ControlRegister(31, INITIAL_STATUS);
    core.setFloatingPointRegister(3, UINT32_C(0x41100000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x04, 0, 4, 3));
    system.runMasterCycles(COP1_DIV_SQRT_LATENCY);

    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_OVERFLOW |
       EECOP1Control::CAUSE_UNDERFLOW |
       EECOP1Control::STICKY_MASK));
  }

  SECTION("A negative operand raises invalid and clears current D")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP1ControlRegister(
      31,
      EECOP1Control::CAUSE_DIVISION_BY_ZERO |
      EECOP1Control::STICKY_DIVISION_BY_ZERO);
    core.setFloatingPointRegister(3, UINT32_C(0xc1100000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x04, 0, 4, 3));
    system.runMasterCycles(COP1_DIV_SQRT_LATENCY);

    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x40400000));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_INVALID |
       EECOP1Control::STICKY_INVALID |
       EECOP1Control::STICKY_DIVISION_BY_ZERO));
  }
}

TEST_CASE("EE COP1 reciprocal square root produces exact raw results")
{
  struct ArithmeticVector
  {
    std::uint32_t fs;
    std::uint32_t ft;
    std::uint32_t expected;
  };
  const ArithmeticVector vectors[] = {
    {UINT32_C(0x40c00000), UINT32_C(0x40800000),
     UINT32_C(0x40400000)},
    {UINT32_C(0xc0c00000), UINT32_C(0x40800000),
     UINT32_C(0xc0400000)},
    {UINT32_C(0x40c00000), UINT32_C(0xc1100000),
     UINT32_C(0x40000000)},
    {UINT32_C(0x00000001), UINT32_C(0x3f800000), 0},
    {UINT32_C(0x80000001), UINT32_C(0x3f800000),
     FP_SIGN_BIT},
    {UINT32_C(0x7fffffff), UINT32_C(0x00800000),
     UINT32_C(0x7fffffff)}
  };

  for (const ArithmeticVector &vector : vectors)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, vector.fs);
    core.setFloatingPointRegister(3, vector.ft);

    runInstruction(
      &system,
      cop1SingleInstruction(0x16, 2, 4, 3));
    system.runMasterCycles(COP1_RSQRT_LATENCY);

    REQUIRE(core.floatingPointRegister(2) == vector.fs);
    REQUIRE(core.floatingPointRegister(3) == vector.ft);
    REQUIRE(
      core.floatingPointRegister(4) ==
      vector.expected);
  }
}

TEST_CASE("EE COP1 reciprocal square root supports in-place writes")
{
  SECTION("The destination may alias fs")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x40c00000));
    core.setFloatingPointRegister(3, UINT32_C(0x40800000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x16, 2, 2, 3));
    system.runMasterCycles(COP1_RSQRT_LATENCY);

    REQUIRE(
      core.floatingPointRegister(2) ==
      UINT32_C(0x40400000));
  }

  SECTION("The destination may alias ft")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x40c00000));
    core.setFloatingPointRegister(3, UINT32_C(0x40800000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x16, 2, 3, 3));
    system.runMasterCycles(COP1_RSQRT_LATENCY);

    REQUIRE(
      core.floatingPointRegister(3) ==
      UINT32_C(0x40400000));
  }
}

TEST_CASE(
  "EE COP1 reciprocal square root updates only invalid and division flags")
{
  constexpr std::uint32_t INITIAL_STATUS =
    EECOP1Control::CAUSE_MASK |
    EECOP1Control::STICKY_MASK;

  SECTION("An ordinary result clears current I and D only")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP1ControlRegister(31, INITIAL_STATUS);
    core.setFloatingPointRegister(2, UINT32_C(0x40c00000));
    core.setFloatingPointRegister(3, UINT32_C(0x40800000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x16, 2, 4, 3));
    system.runMasterCycles(COP1_RSQRT_LATENCY);

    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_OVERFLOW |
       EECOP1Control::CAUSE_UNDERFLOW |
       EECOP1Control::STICKY_MASK));
  }

  SECTION("A zero radicand raises division by zero even for zero over zero")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP1ControlRegister(
      31,
      EECOP1Control::CAUSE_INVALID |
      EECOP1Control::STICKY_INVALID);
    core.setFloatingPointRegister(2, 0);
    core.setFloatingPointRegister(3, FP_SIGN_BIT);

    runInstruction(
      &system,
      cop1SingleInstruction(0x16, 2, 4, 3));
    system.runMasterCycles(COP1_RSQRT_LATENCY);

    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0xffffffff));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_DIVISION_BY_ZERO |
       EECOP1Control::STICKY_INVALID |
       EECOP1Control::STICKY_DIVISION_BY_ZERO));
  }

  SECTION("A negative nonzero radicand raises invalid")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0xbf800000));
    core.setFloatingPointRegister(3, UINT32_C(0xc1100000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x16, 2, 4, 3));
    system.runMasterCycles(COP1_RSQRT_LATENCY);

    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0xbeaaaaaa));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_INVALID |
       EECOP1Control::STICKY_INVALID));
  }
}

TEST_CASE("EE COP1 accumulator multiply writes only ACC")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setFloatingPointRegister(0, UINT32_C(0x11111111));
  core.setFloatingPointRegister(2, UINT32_C(0xc0000000));
  core.setFloatingPointRegister(3, UINT32_C(0x40400000));
  core.setFloatingPointAccumulator(UINT32_C(0x22222222));

  runInstruction(
    &system,
    cop1SingleInstruction(0x1a, 2, 0, 3));

  REQUIRE(core.floatingPointRegister(0) == UINT32_C(0x11111111));
  REQUIRE(core.floatingPointRegister(2) == UINT32_C(0xc0000000));
  REQUIRE(core.floatingPointRegister(3) == UINT32_C(0x40400000));
  REQUIRE(
    core.floatingPointAccumulator() ==
    UINT32_C(0xc0c00000));
}

TEST_CASE("EE COP1 multiply supports in-place writes")
{
  SECTION("The destination may alias fs")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0xc0000000));
    core.setFloatingPointRegister(3, UINT32_C(0x40400000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x02, 2, 2, 3));

    REQUIRE(
      core.floatingPointRegister(2) ==
      UINT32_C(0xc0c00000));
  }

  SECTION("The destination may alias ft")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0xc0000000));
    core.setFloatingPointRegister(3, UINT32_C(0x40400000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x02, 2, 3, 3));

    REQUIRE(
      core.floatingPointRegister(3) ==
      UINT32_C(0xc0c00000));
  }
}

TEST_CASE(
  "EE COP1 multiply updates overflow and underflow flags")
{
  constexpr std::uint32_t INITIAL_STATUS =
    EECOP1Control::CAUSE_MASK |
    EECOP1Control::STICKY_MASK;

  SECTION("An ordinary result clears current O and U only")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP1ControlRegister(31, INITIAL_STATUS);
    core.setFloatingPointRegister(2, UINT32_C(0x3f800000));
    core.setFloatingPointRegister(3, UINT32_C(0x40000000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x02, 2, 4, 3));

    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_INVALID |
       EECOP1Control::CAUSE_DIVISION_BY_ZERO |
       EECOP1Control::STICKY_MASK));
  }

  SECTION("Overflow saturates and sets current and sticky O")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x7f800000));
    core.setFloatingPointRegister(3, UINT32_C(0x40000000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x02, 2, 4, 3));

    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x7fffffff));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_OVERFLOW |
       EECOP1Control::STICKY_OVERFLOW));
  }

  SECTION("Accumulator underflow preserves the sign and sets U")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x80800000));
    core.setFloatingPointRegister(3, UINT32_C(0x3f000000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x1a, 2, 0, 3));

    REQUIRE(core.floatingPointAccumulator() == FP_SIGN_BIT);
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_UNDERFLOW |
       EECOP1Control::STICKY_UNDERFLOW));
  }
}

TEST_CASE("EE COP1 multiply-add uses ACC and selects its destination")
{
  SECTION("MADD.S writes an FPR without changing ACC")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointAccumulator(UINT32_C(0x41200000));
    core.setFloatingPointRegister(2, UINT32_C(0x40000000));
    core.setFloatingPointRegister(3, UINT32_C(0x40400000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x1c, 2, 4, 3));

    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x41800000));
    REQUIRE(
      core.floatingPointAccumulator() ==
      UINT32_C(0x41200000));
  }

  SECTION("MADDA.S writes ACC without changing FPR zero")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointAccumulator(UINT32_C(0x41200000));
    core.setFloatingPointRegister(0, UINT32_C(0x11111111));
    core.setFloatingPointRegister(2, UINT32_C(0x40000000));
    core.setFloatingPointRegister(3, UINT32_C(0x40400000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x1e, 2, 0, 3));

    REQUIRE(
      core.floatingPointAccumulator() ==
      UINT32_C(0x41800000));
    REQUIRE(core.floatingPointRegister(0) == UINT32_C(0x11111111));
  }
}

TEST_CASE("EE COP1 multiply-add preserves intermediate flag rules")
{
  SECTION("Product underflow sets sticky U but leaves current U clear")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointAccumulator(UINT32_C(0x40000000));
    core.setFloatingPointRegister(2, UINT32_C(0x80800000));
    core.setFloatingPointRegister(3, UINT32_C(0x3f000000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x1c, 2, 4, 3));

    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x40000000));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::STICKY_UNDERFLOW));
  }

  SECTION("Product overflow bypasses ACC and raises O")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointAccumulator(UINT32_C(0xff7fffff));
    core.setFloatingPointRegister(2, UINT32_C(0x7f800000));
    core.setFloatingPointRegister(3, UINT32_C(0x40000000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x1c, 2, 4, 3));

    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x7fffffff));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_OVERFLOW |
       EECOP1Control::STICKY_OVERFLOW));
  }

  SECTION("An exponent-255 product is a compound-operation overflow")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointAccumulator(UINT32_C(0xbf800000));
    core.setFloatingPointRegister(2, UINT32_C(0x7f000000));
    core.setFloatingPointRegister(3, UINT32_C(0x40000000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x1c, 2, 4, 3));

    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x7fffffff));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_OVERFLOW |
       EECOP1Control::STICKY_OVERFLOW));
  }

  SECTION("An overflow-state ACC is preserved and raises O")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointAccumulator(UINT32_C(0xffc00000));
    core.setFloatingPointRegister(2, UINT32_C(0x3f800000));
    core.setFloatingPointRegister(3, UINT32_C(0x3f800000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x1e, 2, 0, 3));

    REQUIRE(
      core.floatingPointAccumulator() ==
      UINT32_C(0xffc00000));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_OVERFLOW |
       EECOP1Control::STICKY_OVERFLOW));
  }

  SECTION("A final exponent-255 sum saturates and raises O")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointAccumulator(UINT32_C(0x7f000000));
    core.setFloatingPointRegister(2, UINT32_C(0x7e800000));
    core.setFloatingPointRegister(3, UINT32_C(0x40000000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x1e, 2, 0, 3));

    REQUIRE(
      core.floatingPointAccumulator() ==
      UINT32_C(0x7fffffff));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_OVERFLOW |
       EECOP1Control::STICKY_OVERFLOW));
  }

  SECTION("Final cancellation underflow sets current and sticky U")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointAccumulator(UINT32_C(0x00800001));
    core.setFloatingPointRegister(2, UINT32_C(0x80800000));
    core.setFloatingPointRegister(3, UINT32_C(0x3f800000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x1e, 2, 0, 3));

    REQUIRE(core.floatingPointAccumulator() == 0);
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_UNDERFLOW |
       EECOP1Control::STICKY_UNDERFLOW));
  }
}

TEST_CASE("EE COP1 multiply-subtract uses ACC and selects its destination")
{
  SECTION("MSUB.S writes an FPR without changing ACC")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointAccumulator(UINT32_C(0x41200000));
    core.setFloatingPointRegister(2, UINT32_C(0x40000000));
    core.setFloatingPointRegister(3, UINT32_C(0x40400000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x1d, 2, 4, 3));

    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x40800000));
    REQUIRE(
      core.floatingPointAccumulator() ==
      UINT32_C(0x41200000));
  }

  SECTION("MSUBA.S writes ACC without changing FPR zero")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointAccumulator(UINT32_C(0x41200000));
    core.setFloatingPointRegister(0, UINT32_C(0x11111111));
    core.setFloatingPointRegister(2, UINT32_C(0x40000000));
    core.setFloatingPointRegister(3, UINT32_C(0x40400000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x1f, 2, 0, 3));

    REQUIRE(
      core.floatingPointAccumulator() ==
      UINT32_C(0x40800000));
    REQUIRE(core.floatingPointRegister(0) == UINT32_C(0x11111111));
  }
}

TEST_CASE("EE COP1 multiply-subtract preserves intermediate flag rules")
{
  SECTION("Product underflow sets sticky U but leaves current U clear")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointAccumulator(UINT32_C(0x40000000));
    core.setFloatingPointRegister(2, UINT32_C(0x80800000));
    core.setFloatingPointRegister(3, UINT32_C(0x3f000000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x1d, 2, 4, 3));

    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x40000000));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::STICKY_UNDERFLOW));
  }

  SECTION("Positive product overflow produces negative maximum")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointAccumulator(UINT32_C(0x3f800000));
    core.setFloatingPointRegister(2, UINT32_C(0x7f800000));
    core.setFloatingPointRegister(3, UINT32_C(0x40000000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x1d, 2, 4, 3));

    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0xffffffff));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_OVERFLOW |
       EECOP1Control::STICKY_OVERFLOW));
  }

  SECTION("Negative exponent-255 product produces positive maximum")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointAccumulator(UINT32_C(0x3f800000));
    core.setFloatingPointRegister(2, UINT32_C(0xff000000));
    core.setFloatingPointRegister(3, UINT32_C(0x40000000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x1f, 2, 0, 3));

    REQUIRE(
      core.floatingPointAccumulator() ==
      UINT32_C(0x7fffffff));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_OVERFLOW |
       EECOP1Control::STICKY_OVERFLOW));
  }

  SECTION("An overflow-state ACC is preserved and raises O")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointAccumulator(UINT32_C(0xffc00000));
    core.setFloatingPointRegister(2, UINT32_C(0x3f800000));
    core.setFloatingPointRegister(3, UINT32_C(0x3f800000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x1d, 2, 4, 3));

    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0xffc00000));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_OVERFLOW |
       EECOP1Control::STICKY_OVERFLOW));
  }

  SECTION("A final exponent-255 difference saturates and raises O")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointAccumulator(UINT32_C(0x7f000000));
    core.setFloatingPointRegister(2, UINT32_C(0xfe800000));
    core.setFloatingPointRegister(3, UINT32_C(0x40000000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x1f, 2, 0, 3));

    REQUIRE(
      core.floatingPointAccumulator() ==
      UINT32_C(0x7fffffff));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_OVERFLOW |
       EECOP1Control::STICKY_OVERFLOW));
  }

  SECTION("Final cancellation underflow sets current and sticky U")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointAccumulator(UINT32_C(0x00800001));
    core.setFloatingPointRegister(2, UINT32_C(0x00800000));
    core.setFloatingPointRegister(3, UINT32_C(0x3f800000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x1f, 2, 0, 3));

    REQUIRE(core.floatingPointAccumulator() == 0);
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_UNDERFLOW |
       EECOP1Control::STICKY_UNDERFLOW));
  }
}

TEST_CASE("EE COP1 compound operations preserve raw intermediate rules")
{
  SECTION("MADD truncates the product before adding ACC")
  {
    const EEFloatResult product =
      mulFPRaw(UINT32_C(0x3e000001), UINT32_C(0x3e000001));
    const EECompoundFloatResult result =
      maddEEFloatRaw(
        UINT32_C(0xbc800001),
        UINT32_C(0x3e000001),
        UINT32_C(0x3e000001));

    REQUIRE(product.bits == UINT32_C(0x3c800002));
    REQUIRE(product.flags == 0);
    REQUIRE(result.bits == UINT32_C(0x31000000));
    REQUIRE(result.flags == 0);
    REQUIRE(result.stickyFlags == 0);
  }

  SECTION("MSUB truncates the product before subtracting from ACC")
  {
    const EEFloatResult product =
      mulFPRaw(UINT32_C(0x3e000001), UINT32_C(0x3e000001));
    const EECompoundFloatResult result =
      msubEEFloatRaw(
        UINT32_C(0x3c800001),
        UINT32_C(0x3e000001),
        UINT32_C(0x3e000001));

    REQUIRE(product.bits == UINT32_C(0x3c800002));
    REQUIRE(product.flags == 0);
    REQUIRE(result.bits == UINT32_C(0xb1000000));
    REQUIRE(result.flags == 0);
    REQUIRE(result.stickyFlags == 0);
  }

  SECTION("An exact zero product preserves ACC without underflow")
  {
    const EECompoundFloatResult result =
      maddEEFloatRaw(
        UINT32_C(0xc0400000),
        FP_SIGN_BIT,
        UINT32_C(0x7f000000));

    REQUIRE(result.bits == UINT32_C(0xc0400000));
    REQUIRE(result.flags == 0);
    REQUIRE(result.stickyFlags == 0);
  }

  SECTION("An underflowed product preserves ACC but records sticky U")
  {
    const EECompoundFloatResult result =
      maddEEFloatRaw(
        UINT32_C(0x40000000),
        UINT32_C(0x80800000),
        UINT32_C(0x3f000000));

    REQUIRE(result.bits == UINT32_C(0x40000000));
    REQUIRE(result.flags == 0);
    REQUIRE(result.stickyFlags == FP_FLAG_UNDERFLOW);
  }

  SECTION("Final magnitude underflow records current and sticky U")
  {
    const EECompoundFloatResult result =
      maddEEFloatRaw(
        UINT32_C(0x00800001),
        UINT32_C(0x80800000),
        UINT32_C(0x3f800000));

    REQUIRE(result.bits == 0);
    REQUIRE(result.flags == FP_FLAG_UNDERFLOW);
    REQUIRE(result.stickyFlags == FP_FLAG_UNDERFLOW);
  }

  SECTION("Exact MADD cancellation records current and sticky U")
  {
    const EECompoundFloatResult result =
      maddEEFloatRaw(
        UINT32_C(0x3f800000),
        UINT32_C(0xbf800000),
        UINT32_C(0x3f800000));

    REQUIRE(result.bits == 0);
    REQUIRE(result.flags == FP_FLAG_UNDERFLOW);
    REQUIRE(result.stickyFlags == FP_FLAG_UNDERFLOW);
  }

  SECTION("Exact MSUB cancellation records current and sticky U")
  {
    const EECompoundFloatResult result =
      msubEEFloatRaw(
        UINT32_C(0x3f800000),
        UINT32_C(0x3f800000),
        UINT32_C(0x3f800000));

    REQUIRE(result.bits == 0);
    REQUIRE(result.flags == FP_FLAG_UNDERFLOW);
    REQUIRE(result.stickyFlags == FP_FLAG_UNDERFLOW);
  }

  SECTION("ACC overflow and product underflow preserve both sticky causes")
  {
    const EECompoundFloatResult result =
      maddEEFloatRaw(
        UINT32_C(0xffc00000),
        UINT32_C(0x80800000),
        UINT32_C(0x3f000000));

    REQUIRE(result.bits == UINT32_C(0xffc00000));
    REQUIRE(result.flags == FP_FLAG_OVERFLOW);
    REQUIRE(
      result.stickyFlags ==
      (FP_FLAG_OVERFLOW | FP_FLAG_UNDERFLOW));
  }

  SECTION("MADD product overflow takes the product sign over ACC")
  {
    const EECompoundFloatResult result =
      maddEEFloatRaw(
        UINT32_C(0xffffffff),
        UINT32_C(0x7f800000),
        UINT32_C(0x40000000));

    REQUIRE(result.bits == UINT32_C(0x7fffffff));
    REQUIRE(result.flags == FP_FLAG_OVERFLOW);
    REQUIRE(result.stickyFlags == FP_FLAG_OVERFLOW);
  }

  SECTION("A final exponent-255 sum saturates and raises overflow")
  {
    const EECompoundFloatResult result =
      maddEEFloatRaw(
        UINT32_C(0x7f000000),
        UINT32_C(0x7e800000),
        UINT32_C(0x40000000));

    REQUIRE(result.bits == UINT32_C(0x7fffffff));
    REQUIRE(result.flags == FP_FLAG_OVERFLOW);
    REQUIRE(result.stickyFlags == FP_FLAG_OVERFLOW);
  }

  SECTION("MSUB product overflow inverts the product sign")
  {
    const EECompoundFloatResult result =
      msubEEFloatRaw(
        UINT32_C(0x7fffffff),
        UINT32_C(0xff800000),
        UINT32_C(0x40000000));

    REQUIRE(result.bits == UINT32_C(0x7fffffff));
    REQUIRE(result.flags == FP_FLAG_OVERFLOW);
    REQUIRE(result.stickyFlags == FP_FLAG_OVERFLOW);
  }
}

TEST_CASE("EE COP1 add and subtract produce exact raw results")
{
  struct ArithmeticVector
  {
    std::uint8_t function;
    std::uint32_t fs;
    std::uint32_t ft;
    std::uint32_t expected;
  };
  const ArithmeticVector vectors[] = {
    {0x00, UINT32_C(0x3fc00000), UINT32_C(0x40100000),
     UINT32_C(0x40700000)},
    {0x01, UINT32_C(0x40b00000), UINT32_C(0x3fc00000),
     UINT32_C(0x40800000)},
    {0x00, 0, FP_SIGN_BIT, 0},
    {0x00, FP_SIGN_BIT, FP_SIGN_BIT, FP_SIGN_BIT},
    {0x01, FP_SIGN_BIT, 0, FP_SIGN_BIT},
    {0x01, FP_SIGN_BIT, FP_SIGN_BIT, 0},
    {0x00, UINT32_C(0x7f800000), UINT32_C(0x7f800000),
     UINT32_C(0x7fffffff)},
    {0x01, UINT32_C(0x80800001), UINT32_C(0x80800000),
     FP_SIGN_BIT}
  };

  for (const ArithmeticVector &vector : vectors)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, vector.fs);
    core.setFloatingPointRegister(3, vector.ft);

    runInstruction(
      &system,
      cop1SingleInstruction(
        vector.function,
        2,
        4,
        3));

    REQUIRE(core.floatingPointRegister(2) == vector.fs);
    REQUIRE(core.floatingPointRegister(3) == vector.ft);
    REQUIRE(
      core.floatingPointRegister(4) ==
      vector.expected);
  }
}

TEST_CASE("EE COP1 accumulator add and subtract write only ACC")
{
  struct ArithmeticVector
  {
    std::uint8_t function;
    std::uint32_t fs;
    std::uint32_t ft;
    std::uint32_t expected;
  };
  const ArithmeticVector vectors[] = {
    {0x18, UINT32_C(0x3fc00000), UINT32_C(0x40100000),
     UINT32_C(0x40700000)},
    {0x19, UINT32_C(0x40b00000), UINT32_C(0x3fc00000),
     UINT32_C(0x40800000)},
    {0x18, 0, FP_SIGN_BIT, 0},
    {0x18, FP_SIGN_BIT, FP_SIGN_BIT, FP_SIGN_BIT},
    {0x19, FP_SIGN_BIT, 0, FP_SIGN_BIT},
    {0x19, FP_SIGN_BIT, FP_SIGN_BIT, 0},
    {0x18, UINT32_C(0x7f800000), UINT32_C(0x7f800000),
     UINT32_C(0x7fffffff)},
    {0x19, UINT32_C(0x80800001), UINT32_C(0x80800000),
     FP_SIGN_BIT}
  };

  for (const ArithmeticVector &vector : vectors)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(0, UINT32_C(0x11111111));
    core.setFloatingPointRegister(2, vector.fs);
    core.setFloatingPointRegister(3, vector.ft);
    core.setFloatingPointAccumulator(UINT32_C(0x22222222));

    runInstruction(
      &system,
      cop1SingleInstruction(
        vector.function,
        2,
        0,
        3));

    REQUIRE(core.floatingPointRegister(0) == UINT32_C(0x11111111));
    REQUIRE(core.floatingPointRegister(2) == vector.fs);
    REQUIRE(core.floatingPointRegister(3) == vector.ft);
    REQUIRE(
      core.floatingPointAccumulator() ==
      vector.expected);
  }
}

TEST_CASE("EE COP1 add and subtract support in-place writes")
{
  SECTION("The destination may alias fs")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x3fc00000));
    core.setFloatingPointRegister(3, UINT32_C(0x40100000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x00, 2, 2, 3));

    REQUIRE(
      core.floatingPointRegister(2) ==
      UINT32_C(0x40700000));
  }

  SECTION("The destination may alias ft")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x40b00000));
    core.setFloatingPointRegister(3, UINT32_C(0x3fc00000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x01, 2, 3, 3));

    REQUIRE(
      core.floatingPointRegister(3) ==
      UINT32_C(0x40800000));
  }
}

TEST_CASE(
  "EE COP1 accumulator add and subtract update overflow and underflow flags")
{
  constexpr std::uint32_t INITIAL_STATUS =
    EECOP1Control::CAUSE_MASK |
    EECOP1Control::STICKY_MASK;

  SECTION("An ordinary result clears current O and U only")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP1ControlRegister(31, INITIAL_STATUS);
    core.setFloatingPointRegister(2, UINT32_C(0x3f800000));
    core.setFloatingPointRegister(3, UINT32_C(0x40000000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x18, 2, 0, 3));

    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_INVALID |
       EECOP1Control::CAUSE_DIVISION_BY_ZERO |
       EECOP1Control::STICKY_MASK));
  }

  SECTION("Overflow sets current and sticky O")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x7f800000));
    core.setFloatingPointRegister(3, UINT32_C(0x7f800000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x18, 2, 0, 3));

    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_OVERFLOW |
       EECOP1Control::STICKY_OVERFLOW));
  }

  SECTION("Underflow sets current and sticky U")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x00800001));
    core.setFloatingPointRegister(3, UINT32_C(0x00800000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x19, 2, 0, 3));

    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_UNDERFLOW |
       EECOP1Control::STICKY_UNDERFLOW));
  }
}

TEST_CASE("EE COP1 add and subtract update overflow and underflow flags")
{
  constexpr std::uint32_t INITIAL_STATUS =
    EECOP1Control::CAUSE_MASK |
    EECOP1Control::STICKY_MASK;

  SECTION("An ordinary result clears current O and U only")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP1ControlRegister(31, INITIAL_STATUS);
    core.setFloatingPointRegister(2, UINT32_C(0x3f800000));
    core.setFloatingPointRegister(3, UINT32_C(0x40000000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x00, 2, 4, 3));

    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_INVALID |
       EECOP1Control::CAUSE_DIVISION_BY_ZERO |
       EECOP1Control::STICKY_MASK));
  }

  SECTION("Overflow sets current and sticky O and clears current U")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP1ControlRegister(
      31,
      EECOP1Control::CAUSE_UNDERFLOW);
    core.setFloatingPointRegister(2, UINT32_C(0x7f800000));
    core.setFloatingPointRegister(3, UINT32_C(0x7f800000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x00, 2, 4, 3));

    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_OVERFLOW |
       EECOP1Control::STICKY_OVERFLOW));
  }

  SECTION("Underflow sets current and sticky U and clears current O")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP1ControlRegister(
      31,
      EECOP1Control::CAUSE_OVERFLOW);
    core.setFloatingPointRegister(2, UINT32_C(0x00800001));
    core.setFloatingPointRegister(3, UINT32_C(0x00800000));

    runInstruction(
      &system,
      cop1SingleInstruction(0x01, 2, 4, 3));

    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_UNDERFLOW |
       EECOP1Control::STICKY_UNDERFLOW));
  }
}

TEST_CASE("EE COP1 single movement instructions transform raw bits")
{
  struct MovementVector
  {
    std::uint8_t function;
    std::uint32_t source;
    std::uint32_t expected;
  };
  const MovementVector vectors[] = {
    {0x05, UINT32_C(0xffc12345), UINT32_C(0x7fc12345)},
    {0x05, UINT32_C(0x80000000), UINT32_C(0x00000000)},
    {0x06, UINT32_C(0xffc12345), UINT32_C(0xffc12345)},
    {0x06, UINT32_C(0x807fffff), UINT32_C(0x807fffff)},
    {0x07, UINT32_C(0x7fc12345), UINT32_C(0xffc12345)},
    {0x07, UINT32_C(0x80000000), UINT32_C(0x00000000)}
  };

  for (const MovementVector &vector : vectors)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, vector.source);

    runInstruction(
      &system,
      cop1SingleInstruction(vector.function, 2, 3));

    REQUIRE(core.floatingPointRegister(2) == vector.source);
    REQUIRE(
      core.floatingPointRegister(3) == vector.expected);
  }
}

TEST_CASE("EE COP1 single movement instructions support in-place writes")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setFloatingPointRegister(2, UINT32_C(0xffc12345));

  runInstruction(
    &system,
    cop1SingleInstruction(0x05, 2, 2));

  REQUIRE(
    core.floatingPointRegister(2) ==
    UINT32_C(0x7fc12345));
}

TEST_CASE("EE COP1 movement instructions apply documented flags")
{
  constexpr std::uint32_t INITIAL_STATUS =
    EECOP1Control::CAUSE_MASK |
    EECOP1Control::STICKY_MASK;

  SECTION("MOV.S leaves every arithmetic flag unchanged")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP1ControlRegister(31, INITIAL_STATUS);
    core.setFloatingPointRegister(2, UINT32_C(0x12345678));

    runInstruction(
      &system,
      cop1SingleInstruction(0x06, 2, 3));

    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED | INITIAL_STATUS));
  }

  SECTION("ABS.S and NEG.S clear current O and U only")
  {
    for (const std::uint8_t function : {0x05, 0x07})
    {
      NekoSystem system;
      EECore &core = system.eeCore();
      core.setCOP1ControlRegister(31, INITIAL_STATUS);
      core.setFloatingPointRegister(2, UINT32_C(0x12345678));

      runInstruction(
        &system,
        cop1SingleInstruction(function, 2, 3));

      REQUIRE(
        core.cop1ControlRegister(31) ==
        (EECOP1Control::STATUS_FIXED |
         EECOP1Control::CAUSE_INVALID |
         EECOP1Control::CAUSE_DIVISION_BY_ZERO |
         EECOP1Control::STICKY_MASK));
    }
  }
}

TEST_CASE("EE COP1 min and max instructions decode canonically")
{
  REQUIRE(
    decodeEEInstruction(
      cop1SingleInstruction(0x28, 2, 4, 3)).operation ==
    EEOperation::MaximumSingleCOP1);
  REQUIRE(
    decodeEEInstruction(
      cop1SingleInstruction(0x29, 2, 4, 3)).operation ==
    EEOperation::MinimumSingleCOP1);
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop1SingleInstruction(0x30, 2, 4, 3)),
    "Reserved EE instruction encoding.");
}

TEST_CASE("EE COP1 min and max select exact source encodings")
{
  struct SelectionVector
  {
    std::uint8_t function;
    std::uint32_t fs;
    std::uint32_t ft;
    std::uint32_t expected;
  };
  const SelectionVector vectors[] = {
    {0x28, UINT32_C(0x40000000), UINT32_C(0x3f800000),
     UINT32_C(0x40000000)},
    {0x28, UINT32_C(0xbf800000), UINT32_C(0xc0000000),
     UINT32_C(0xbf800000)},
    {0x29, UINT32_C(0x40000000), UINT32_C(0x3f800000),
     UINT32_C(0x3f800000)},
    {0x29, UINT32_C(0xbf800000), UINT32_C(0xc0000000),
     UINT32_C(0xc0000000)},
    {0x28, UINT32_C(0x7fc12345), UINT32_C(0x7f800000),
     UINT32_C(0x7fc12345)},
    {0x29, UINT32_C(0xffc12345), UINT32_C(0xff800000),
     UINT32_C(0xffc12345)}
  };

  for (const SelectionVector &vector : vectors)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, vector.fs);
    core.setFloatingPointRegister(3, vector.ft);

    runInstruction(
      &system,
      cop1SingleInstruction(
        vector.function,
        2,
        4,
        3));

    REQUIRE(
      core.floatingPointRegister(4) ==
      vector.expected);
  }
}

TEST_CASE("EE COP1 min and max produce documented signed-zero results")
{
  struct SignedZeroVector
  {
    std::uint8_t function;
    std::uint32_t fs;
    std::uint32_t ft;
    std::uint32_t expected;
  };
  const SignedZeroVector vectors[] = {
    {0x28, 0, 0, 0},
    {0x28, 0, FP_SIGN_BIT, 0},
    {0x28, FP_SIGN_BIT, 0, 0},
    {0x28, FP_SIGN_BIT, FP_SIGN_BIT, FP_SIGN_BIT},
    {0x29, 0, 0, 0},
    {0x29, 0, FP_SIGN_BIT, FP_SIGN_BIT},
    {0x29, FP_SIGN_BIT, 0, FP_SIGN_BIT},
    {0x29, FP_SIGN_BIT, FP_SIGN_BIT, FP_SIGN_BIT}
  };

  for (const SignedZeroVector &vector : vectors)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, vector.fs);
    core.setFloatingPointRegister(3, vector.ft);

    runInstruction(
      &system,
      cop1SingleInstruction(
        vector.function,
        2,
        4,
        3));

    REQUIRE(
      core.floatingPointRegister(4) ==
      vector.expected);
  }
}

TEST_CASE("EE COP1 min and max flush selected denormals to signed zero")
{
  REQUIRE(
    maxEEFloatRaw(
      UINT32_C(0x007fffff),
      UINT32_C(0x80000000)).bits ==
    0);
  REQUIRE(
    minEEFloatRaw(
      UINT32_C(0x807fffff),
      UINT32_C(0x00000000)).bits ==
    FP_SIGN_BIT);
}

TEST_CASE("EE COP1 min and max clear current O and U")
{
  for (const std::uint8_t function : {0x28, 0x29})
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP1ControlRegister(
      31,
      EECOP1Control::CAUSE_MASK |
        EECOP1Control::STICKY_MASK);
    core.setFloatingPointRegister(2, UINT32_C(0x3f800000));
    core.setFloatingPointRegister(3, UINT32_C(0x40000000));

    runInstruction(
      &system,
      cop1SingleInstruction(function, 2, 4, 3));

    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_INVALID |
       EECOP1Control::CAUSE_DIVISION_BY_ZERO |
       EECOP1Control::STICKY_MASK));
  }
}

TEST_CASE("EE COP1 comparisons decode only canonical encodings")
{
  struct DecodeVector
  {
    std::uint8_t function;
    EEOperation operation;
  };
  const DecodeVector vectors[] = {
    {0x30, EEOperation::CompareFalseSingleCOP1},
    {0x32, EEOperation::CompareEqualSingleCOP1},
    {0x34, EEOperation::CompareLessThanSingleCOP1},
    {0x36, EEOperation::CompareLessThanOrEqualSingleCOP1}
  };

  for (const DecodeVector &vector : vectors)
  {
    REQUIRE(
      decodeEEInstruction(
        cop1SingleInstruction(
          vector.function,
          2,
          0,
          3)).operation ==
      vector.operation);
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(
        cop1SingleInstruction(
          vector.function,
          2,
          1,
          3)),
      "Reserved EE instruction encoding.");
  }
}

TEST_CASE("EE COP1 branches decode canonical condition forms")
{
  const EEOperation operations[] = {
    EEOperation::BranchCOP1False,
    EEOperation::BranchCOP1True,
    EEOperation::BranchCOP1FalseLikely,
    EEOperation::BranchCOP1TrueLikely
  };
  for (std::uint8_t condition = 0; condition < 4; ++condition)
  {
    REQUIRE(
      decodeEEInstruction(
        cop1BranchInstruction(condition, 0x1234)).operation ==
      operations[condition]);
  }

  REQUIRE_THROWS_WITH(
    decodeEEInstruction(cop1BranchInstruction(4, 0)),
    "Reserved EE instruction encoding.");
}

TEST_CASE("EE COP1 branches use FCR31 condition and likely annulment")
{
  struct Contract
  {
    std::uint8_t condition;
    bool conditionBit;
    bool taken;
    bool likely;
  };
  const Contract contracts[] = {
    {0, false, true, false},
    {0, true, false, false},
    {1, false, false, false},
    {1, true, true, false},
    {2, false, true, true},
    {2, true, false, true},
    {3, false, false, true},
    {3, true, true, true}
  };

  for (const Contract &contract : contracts)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP1ControlRegister(
      31,
      contract.conditionBit
        ? EECOP1Control::CONDITION
        : 0);
    const std::uint32_t program[] = {
      cop1BranchInstruction(contract.condition, 2),
      UINT32_C(0x34020001),
      UINT32_C(0x34030002),
      UINT32_C(0x34040003)
    };
    for (std::size_t index = 0; index < 4; ++index)
    {
      system.eeBus().write32(index * 4, program[index]);
    }
    core.startExecution(0);
    system.runMasterCycles(contract.taken ? 3 : 2);

    REQUIRE(
      core.generalRegister(2).low ==
      (contract.likely && !contract.taken ? 0 : 1));
    REQUIRE(
      core.programCounter() ==
      (contract.taken
        ? 16
        : contract.likely
          ? 12
          : 8));
  }
}

TEST_CASE("EE COP1 branches consume the preceding comparison condition")
{
  struct Contract
  {
    std::uint32_t ft;
    std::uint8_t branchCondition;
  };
  const Contract contracts[] = {
    {UINT32_C(0x3f800000), 1},
    {UINT32_C(0x40000000), 0}
  };

  for (const Contract &contract : contracts)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x3f800000));
    core.setFloatingPointRegister(3, contract.ft);
    const std::uint32_t program[] = {
      cop1SingleInstruction(0x32, 2, 0, 3),
      cop1BranchInstruction(contract.branchCondition, 2),
      UINT32_C(0x34020001),
      UINT32_C(0x34030002),
      UINT32_C(0x34040003)
    };
    for (std::size_t index = 0; index < 5; ++index)
    {
      system.eeBus().write32(index * 4, program[index]);
    }
    core.startExecution(0);

    system.runMasterCycles(3);

    REQUIRE(core.elapsedCycles() == 3);
    REQUIRE(core.programCounter() == 16);
    REQUIRE(core.generalRegister(2).low == 1);
    REQUIRE(core.generalRegister(3).low == 0);

    system.clockMasterCycle();
    REQUIRE(core.generalRegister(4).low == 3);
  }
}

TEST_CASE("EE COP1 branch delay-slot exceptions preserve branch ownership")
{
  struct Contract
  {
    std::uint8_t branchCondition;
    bool conditionBit;
    bool delaySlotExecutes;
  };
  const Contract contracts[] = {
    {0, false, true},
    {0, true, true},
    {2, false, true},
    {2, true, false},
    {3, true, true},
    {3, false, false}
  };

  for (const Contract &contract : contracts)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::COP1_USABLE);
    core.setCOP1ControlRegister(
      31,
      contract.conditionBit
        ? EECOP1Control::CONDITION
        : 0);
    system.eeBus().write32(
      0,
      cop1BranchInstruction(contract.branchCondition, 2));
    system.eeBus().write32(4, UINT32_C(0x0000000c));
    system.eeBus().write32(8, UINT32_C(0x34020001));
    core.startExecution(0);

    system.runMasterCycles(2);

    if (contract.delaySlotExecutes)
    {
      REQUIRE(core.pendingException() == EEException::SystemCall);
      REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
      REQUIRE(
        (core.cop0Register(EECOP0Register::Cause) &
          EECOP0Cause::BRANCH_DELAY) != 0);
    }
    else
    {
      REQUIRE(core.pendingException() == EEException::None);
      REQUIRE(core.generalRegister(2).low == 1);
      REQUIRE(core.programCounter() == 12);
    }
  }
}

TEST_CASE("EE COP1 branch continuations survive save-state restore")
{
  struct Contract
  {
    std::uint8_t branchCondition;
    bool conditionBit;
    bool taken;
    bool likely;
  };
  const Contract contracts[] = {
    {0, false, true, false},
    {0, true, false, false},
    {1, true, true, false},
    {1, false, false, false},
    {2, false, true, true},
    {2, true, false, true},
    {3, true, true, true},
    {3, false, false, true}
  };

  for (const Contract &contract : contracts)
  {
    NekoSystem original;
    EECore &originalCore = original.eeCore();
    originalCore.setCOP1ControlRegister(
      31,
      contract.conditionBit
        ? EECOP1Control::CONDITION
        : 0);
    const std::uint32_t program[] = {
      cop1BranchInstruction(contract.branchCondition, 2),
      UINT32_C(0x34020001),
      UINT32_C(0x34030002),
      UINT32_C(0x34040003)
    };
    for (std::size_t index = 0; index < 4; ++index)
    {
      original.eeBus().write32(index * 4, program[index]);
    }
    originalCore.startExecution(0);
    original.clockMasterCycle();

    NekoSystem restored;
    restored.loadState(original.saveState());

    REQUIRE(original.saveState() == restored.saveState());
    REQUIRE(originalCore.stateHash() == restored.eeCore().stateHash());

    original.clockMasterCycle();
    restored.clockMasterCycle();

    REQUIRE(original.saveState() == restored.saveState());
    REQUIRE(originalCore.stateHash() == restored.eeCore().stateHash());
    REQUIRE(
      restored.eeCore().generalRegister(2).low ==
      (contract.likely && !contract.taken ? 0 : 1));
    REQUIRE(
      restored.eeCore().programCounter() ==
      (contract.taken
        ? 12
        : contract.likely
          ? 12
          : 8));
  }
}

TEST_CASE("EE COP1 comparisons update the condition bit exactly")
{
  struct ComparisonVector
  {
    std::uint8_t function;
    std::uint32_t fs;
    std::uint32_t ft;
    bool expected;
  };
  const ComparisonVector vectors[] = {
    {0x30, UINT32_C(0x3f800000), UINT32_C(0x3f800000), false},
    {0x32, UINT32_C(0x3f800000), UINT32_C(0x3f800000), true},
    {0x32, UINT32_C(0x00000000), UINT32_C(0x80000000), true},
    {0x32, UINT32_C(0x7fc12345), UINT32_C(0x7fc12345), true},
    {0x32, UINT32_C(0x7fc12345), UINT32_C(0x7fc12346), false},
    {0x34, UINT32_C(0xbf800000), UINT32_C(0x00000000), true},
    {0x34, UINT32_C(0x80000000), UINT32_C(0x00000000), false},
    {0x34, UINT32_C(0x7f800000), UINT32_C(0x7fc00000), true},
    {0x36, UINT32_C(0x80000000), UINT32_C(0x00000000), true},
    {0x36, UINT32_C(0xc0000000), UINT32_C(0xbf800000), true},
    {0x36, UINT32_C(0x40000000), UINT32_C(0x3f800000), false}
  };

  for (const ComparisonVector &vector : vectors)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP1ControlRegister(
      31,
      EECOP1Control::CONDITION |
        EECOP1Control::CAUSE_MASK |
        EECOP1Control::STICKY_MASK);
    core.setFloatingPointRegister(2, vector.fs);
    core.setFloatingPointRegister(3, vector.ft);

    runInstruction(
      &system,
      cop1SingleInstruction(
        vector.function,
        2,
        0,
        3));

    const std::uint32_t status =
      core.cop1ControlRegister(31);
    REQUIRE(
      core.cop1Condition() == vector.expected);
    REQUIRE(
      (status &
        (EECOP1Control::CAUSE_MASK |
         EECOP1Control::STICKY_MASK)) ==
      (EECOP1Control::CAUSE_MASK |
       EECOP1Control::STICKY_MASK));
  }
}

TEST_CASE("EE COP1 comparison condition is visible at the 2S boundary")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setFloatingPointRegister(2, UINT32_C(0x3f800000));
  core.setFloatingPointRegister(3, UINT32_C(0x3f800000));
  system.eeBus().write32(
    0,
    cop1SingleInstruction(0x32, 2, 0, 3));
  system.eeBus().write32(
    4,
    cop1TransferInstruction(0x02, 5, 31));
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(core.elapsedCycles() == 1);
  REQUIRE(core.programCounter() == 4);
  REQUIRE(core.cop1Condition());
  REQUIRE(core.generalRegister(5) == EERegister128{});

  system.clockMasterCycle();

  REQUIRE(core.elapsedCycles() == 2);
  REQUIRE(core.programCounter() == 4);
  REQUIRE(core.generalRegister(5) == EERegister128{});

  system.clockMasterCycle();

  REQUIRE(core.elapsedCycles() == 3);
  REQUIRE(core.programCounter() == 8);
  REQUIRE(
    core.generalRegister(5).low ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::CONDITION));
}

TEST_CASE("EE COP1 comparison condition retires in instruction order")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setFloatingPointRegister(2, UINT32_C(0x3f800000));
  core.setFloatingPointRegister(3, UINT32_C(0x3f800000));
  system.eeBus().write32(
    0,
    cop1SingleInstruction(0x32, 2, 0, 3));
  system.eeBus().write32(
    4,
    cop1SingleInstruction(0x30, 2, 0, 3));
  core.startExecution(0);

  system.clockMasterCycle();
  REQUIRE(core.cop1Condition());

  system.clockMasterCycle();
  REQUIRE_FALSE(core.cop1Condition());
}

TEST_CASE("EE COP1 CVT.S.W decodes only its canonical W form")
{
  REQUIRE(
    decodeEEInstruction(
      cop1WordInstruction(0x20, 2, 3)).operation ==
    EEOperation::ConvertWordToSingleCOP1);
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop1WordInstruction(0x20, 2, 3, 1)),
    "Reserved EE instruction encoding.");
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop1WordInstruction(0x21, 2, 3)),
    "Unsupported EE instruction encoding.");
}

TEST_CASE("EE COP1 CVT.S.W converts signed words with EE truncation")
{
  struct ConversionVector
  {
    std::uint32_t source;
    std::uint32_t expected;
  };
  const ConversionVector vectors[] = {
    {UINT32_C(0x00000000), UINT32_C(0x00000000)},
    {UINT32_C(0x00000001), UINT32_C(0x3f800000)},
    {UINT32_C(0xffffffff), UINT32_C(0xbf800000)},
    {UINT32_C(0x01000000), UINT32_C(0x4b800000)},
    {UINT32_C(0x01000001), UINT32_C(0x4b800000)},
    {UINT32_C(0xfeffffff), UINT32_C(0xcb800000)},
    {UINT32_C(0x7fffffff), UINT32_C(0x4effffff)},
    {UINT32_C(0x80000000), UINT32_C(0xcf000000)}
  };

  for (const ConversionVector &vector : vectors)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, vector.source);

    runInstruction(
      &system,
      cop1WordInstruction(0x20, 2, 3));

    REQUIRE(core.floatingPointRegister(2) == vector.source);
    REQUIRE(
      core.floatingPointRegister(3) ==
      vector.expected);
  }
}

TEST_CASE("EE COP1 CVT.S.W supports in-place conversion")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setFloatingPointRegister(2, UINT32_C(0x01000001));

  runInstruction(
    &system,
    cop1WordInstruction(0x20, 2, 2));

  REQUIRE(
    core.floatingPointRegister(2) ==
    UINT32_C(0x4b800000));
}

TEST_CASE("EE COP1 CVT.S.W leaves arithmetic flags unchanged")
{
  constexpr std::uint32_t INITIAL_STATUS =
    EECOP1Control::CAUSE_MASK |
    EECOP1Control::STICKY_MASK;
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP1ControlRegister(31, INITIAL_STATUS);
  core.setFloatingPointRegister(2, UINT32_C(0x7fffffff));

  runInstruction(
    &system,
    cop1WordInstruction(0x20, 2, 3));

  REQUIRE(
    core.cop1ControlRegister(31) ==
    (EECOP1Control::STATUS_FIXED | INITIAL_STATUS));
}

TEST_CASE("EE COP1 CVT.W.S decodes only its canonical S form")
{
  REQUIRE(
    decodeEEInstruction(
      cop1SingleInstruction(0x24, 2, 3)).operation ==
    EEOperation::ConvertSingleToWordCOP1);
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop1SingleInstruction(0x24, 2, 3, 1)),
    "Reserved EE instruction encoding.");
}

TEST_CASE("EE COP1 CVT.W.S truncates and clamps raw EE values")
{
  struct ConversionVector
  {
    std::uint32_t source;
    std::uint32_t expected;
  };
  const ConversionVector vectors[] = {
    {UINT32_C(0x00000000), UINT32_C(0x00000000)},
    {UINT32_C(0x80000000), UINT32_C(0x00000000)},
    {UINT32_C(0x007fffff), UINT32_C(0x00000000)},
    {UINT32_C(0x3ff33333), UINT32_C(0x00000001)},
    {UINT32_C(0xbff33333), UINT32_C(0xffffffff)},
    {UINT32_C(0x4effffff), UINT32_C(0x7fffff80)},
    {UINT32_C(0x4f000000), UINT32_C(0x7fffffff)},
    {UINT32_C(0xcf000000), UINT32_C(0x80000000)},
    {UINT32_C(0xcf000001), UINT32_C(0x80000000)},
    {UINT32_C(0x7fffffff), UINT32_C(0x7fffffff)},
    {UINT32_C(0xffffffff), UINT32_C(0x80000000)}
  };

  for (const ConversionVector &vector : vectors)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, vector.source);

    runInstruction(
      &system,
      cop1SingleInstruction(0x24, 2, 3));

    REQUIRE(core.floatingPointRegister(2) == vector.source);
    REQUIRE(
      core.floatingPointRegister(3) ==
      vector.expected);
  }
}

TEST_CASE("EE COP1 CVT.W.S supports in-place conversion")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setFloatingPointRegister(2, UINT32_C(0xbff33333));

  runInstruction(
    &system,
    cop1SingleInstruction(0x24, 2, 2));

  REQUIRE(
    core.floatingPointRegister(2) ==
    UINT32_C(0xffffffff));
}

TEST_CASE("EE COP1 CVT.W.S updates conversion overflow flags")
{
  constexpr std::uint32_t INITIAL_STATUS =
    EECOP1Control::CAUSE_MASK |
    EECOP1Control::STICKY_MASK;

  SECTION("In-range conversion clears current I only")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP1ControlRegister(31, INITIAL_STATUS);
    core.setFloatingPointRegister(2, UINT32_C(0x3ff33333));

    runInstruction(
      &system,
      cop1SingleInstruction(0x24, 2, 3));

    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_DIVISION_BY_ZERO |
       EECOP1Control::CAUSE_OVERFLOW |
       EECOP1Control::CAUSE_UNDERFLOW |
       EECOP1Control::STICKY_MASK));
  }

  SECTION("Clamp-path conversion sets current and sticky I")
  {
    for (const std::uint32_t source : {
           UINT32_C(0x4f000000),
           UINT32_C(0xcf000000),
           UINT32_C(0x7fffffff),
           UINT32_C(0xffffffff)})
    {
      NekoSystem system;
      EECore &core = system.eeCore();
      core.setFloatingPointRegister(2, source);

      runInstruction(
        &system,
        cop1SingleInstruction(0x24, 2, 3));

      REQUIRE(
        core.cop1ControlRegister(31) ==
        (EECOP1Control::STATUS_FIXED |
         EECOP1Control::CAUSE_INVALID |
         EECOP1Control::STICKY_INVALID));
    }
  }
}

TEST_CASE(
  "EE COP1 scalar results have ordered visibility")
{
  struct VisibilityVector
  {
    std::uint32_t instruction;
    std::uint32_t fs;
    std::uint32_t ft;
    std::uint32_t expected;
    bool interlocksFollowingMove;
  };
  const VisibilityVector vectors[] = {
    {
      cop1SingleInstruction(0x05, 2, 4),
      UINT32_C(0xffc12345),
      0,
      UINT32_C(0x7fc12345),
      true
    },
    {
      cop1SingleInstruction(0x06, 2, 4),
      UINT32_C(0x89abcdef),
      0,
      UINT32_C(0x89abcdef),
      false
    },
    {
      cop1SingleInstruction(0x07, 2, 4),
      UINT32_C(0x7fc12345),
      0,
      UINT32_C(0xffc12345),
      true
    },
    {
      cop1SingleInstruction(0x28, 2, 4, 3),
      UINT32_C(0x3f800000),
      UINT32_C(0x40000000),
      UINT32_C(0x40000000),
      true
    },
    {
      cop1SingleInstruction(0x29, 2, 4, 3),
      UINT32_C(0x3f800000),
      UINT32_C(0x40000000),
      UINT32_C(0x3f800000),
      true
    },
    {
      cop1SingleInstruction(0x00, 2, 4, 3),
      UINT32_C(0x3fc00000),
      UINT32_C(0x40100000),
      UINT32_C(0x40700000),
      true
    },
    {
      cop1SingleInstruction(0x01, 2, 4, 3),
      UINT32_C(0x40b00000),
      UINT32_C(0x3fc00000),
      UINT32_C(0x40800000),
      true
    },
    {
      cop1WordInstruction(0x20, 2, 4),
      UINT32_C(0x01000001),
      0,
      UINT32_C(0x4b800000),
      true
    },
    {
      cop1SingleInstruction(0x24, 2, 4),
      UINT32_C(0x3ff33333),
      0,
      UINT32_C(0x00000001),
      true
    }
  };

  for (const VisibilityVector &vector : vectors)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, vector.fs);
    core.setFloatingPointRegister(3, vector.ft);
    core.setFloatingPointRegister(6, UINT32_C(0x76543210));
    system.eeBus().write32(0, vector.instruction);
    system.eeBus().write32(
      4,
      cop1SingleInstruction(0x06, 4, 5));
    system.eeBus().write32(
      8,
      cop1SingleInstruction(0x06, 6, 4));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(core.programCounter() == 4);
    REQUIRE(
      core.floatingPointRegister(4) ==
      vector.expected);
    REQUIRE(core.floatingPointRegister(5) == 0);

    system.clockMasterCycle();

    if (vector.interlocksFollowingMove)
    {
      REQUIRE(core.programCounter() == 4);
      REQUIRE(core.floatingPointRegister(5) == 0);
      system.clockMasterCycle();
    }

    REQUIRE(core.programCounter() == 8);
    REQUIRE(
      core.floatingPointRegister(5) ==
      vector.expected);

    system.clockMasterCycle();

    REQUIRE(core.programCounter() == 12);
    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x76543210));
  }
}

TEST_CASE(
  "EE COP1 add and subtract forward back-to-back FPR dependencies")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setFloatingPointRegister(2, UINT32_C(0x3f800000));
  core.setFloatingPointRegister(3, UINT32_C(0x40000000));
  system.eeBus().write32(
    0,
    cop1SingleInstruction(0x00, 2, 4, 3));
  system.eeBus().write32(
    4,
    cop1SingleInstruction(0x01, 4, 5, 2));
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(core.programCounter() == 4);
  REQUIRE(
    core.floatingPointRegister(4) ==
    UINT32_C(0x40400000));
  REQUIRE(core.floatingPointRegister(5) == 0);

  system.clockMasterCycle();

  REQUIRE(core.programCounter() == 8);
  REQUIRE(
    core.floatingPointRegister(5) ==
    UINT32_C(0x40000000));
}

TEST_CASE("EE COP1 multiply chains forward FPR and ACC results")
{
  SECTION("A multiply result feeds the next compound operation")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointAccumulator(UINT32_C(0x3f800000));
    core.setFloatingPointRegister(2, UINT32_C(0x40000000));
    core.setFloatingPointRegister(3, UINT32_C(0x40400000));
    core.setFloatingPointRegister(5, UINT32_C(0x3f000000));
    system.eeBus().write32(
      0,
      cop1SingleInstruction(0x02, 2, 4, 3));
    system.eeBus().write32(
      4,
      cop1SingleInstruction(0x1c, 4, 6, 5));
    core.startExecution(0);

    system.runMasterCycles(2);

    REQUIRE(core.programCounter() == 8);
    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x40c00000));
    REQUIRE(
      core.floatingPointRegister(6) ==
      UINT32_C(0x40800000));
  }

  SECTION("An accumulator write feeds the next compound operation")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x40000000));
    core.setFloatingPointRegister(3, UINT32_C(0x40400000));
    core.setFloatingPointRegister(5, UINT32_C(0x3f000000));
    core.setFloatingPointRegister(6, UINT32_C(0x40000000));
    system.eeBus().write32(
      0,
      cop1SingleInstruction(0x1a, 2, 0, 3));
    system.eeBus().write32(
      4,
      cop1SingleInstruction(0x1d, 5, 4, 6));
    core.startExecution(0);

    system.runMasterCycles(2);

    REQUIRE(core.programCounter() == 8);
    REQUIRE(
      core.floatingPointAccumulator() ==
      UINT32_C(0x40c00000));
    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x40a00000));
  }

  SECTION("A compound ACC write feeds a younger compound ACC write")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointAccumulator(UINT32_C(0x3f800000));
    core.setFloatingPointRegister(2, UINT32_C(0x40000000));
    core.setFloatingPointRegister(3, UINT32_C(0x40400000));
    core.setFloatingPointRegister(5, UINT32_C(0x3f000000));
    core.setFloatingPointRegister(6, UINT32_C(0x40000000));
    system.eeBus().write32(
      0,
      cop1SingleInstruction(0x1e, 2, 0, 3));
    system.eeBus().write32(
      4,
      cop1SingleInstruction(0x1f, 5, 0, 6));
    core.startExecution(0);

    system.runMasterCycles(2);

    REQUIRE(core.programCounter() == 8);
    REQUIRE(
      core.floatingPointAccumulator() ==
      UINT32_C(0x40c00000));
  }
}

TEST_CASE("EE COP1 independent multiply operations retire every cycle")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setFloatingPointAccumulator(UINT32_C(0x3f800000));
  core.setFloatingPointRegister(2, UINT32_C(0x40000000));
  core.setFloatingPointRegister(3, UINT32_C(0x40400000));
  core.setFloatingPointRegister(5, UINT32_C(0x40a00000));
  core.setFloatingPointRegister(6, UINT32_C(0x3f000000));
  system.eeBus().write32(
    0,
    cop1SingleInstruction(0x02, 2, 4, 3));
  system.eeBus().write32(
    4,
    cop1SingleInstruction(0x1c, 5, 7, 6));
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(core.elapsedCycles() == 1);
  REQUIRE(core.programCounter() == 4);
  REQUIRE(
    core.floatingPointRegister(4) ==
    UINT32_C(0x40c00000));
  REQUIRE(core.floatingPointRegister(7) == 0);

  system.clockMasterCycle();

  REQUIRE(core.elapsedCycles() == 2);
  REQUIRE(core.programCounter() == 8);
  REQUIRE(
    core.floatingPointRegister(7) ==
    UINT32_C(0x40600000));
}

TEST_CASE(
  "EE COP1 divider operations retire results and flags after their provisional latency")
{
  struct TimingVector
  {
    std::uint8_t function;
    std::uint8_t sourceRegister;
    std::uint32_t fs;
    std::uint32_t ft;
    std::uint32_t expected;
    std::uint32_t expectedStatus;
    std::uint8_t latency;
  };
  const TimingVector vectors[] = {
    {
      0x03,
      2,
      0,
      0,
      UINT32_C(0x7fffffff),
      EECOP1Control::STATUS_FIXED |
        EECOP1Control::CAUSE_INVALID |
        EECOP1Control::STICKY_INVALID,
      COP1_DIV_SQRT_LATENCY
    },
    {
      0x04,
      0,
      0,
      UINT32_C(0xc1100000),
      UINT32_C(0x40400000),
      EECOP1Control::STATUS_FIXED |
        EECOP1Control::CAUSE_INVALID |
        EECOP1Control::STICKY_INVALID,
      COP1_DIV_SQRT_LATENCY
    },
    {
      0x16,
      2,
      UINT32_C(0x3f800000),
      0,
      UINT32_C(0x7fffffff),
      EECOP1Control::STATUS_FIXED |
        EECOP1Control::CAUSE_DIVISION_BY_ZERO |
        EECOP1Control::STICKY_DIVISION_BY_ZERO,
      COP1_RSQRT_LATENCY
    }
  };

  for (const TimingVector &vector : vectors)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, vector.fs);
    core.setFloatingPointRegister(3, vector.ft);
    core.setFloatingPointRegister(4, UINT32_C(0x76543210));
    for (std::uint32_t address = 4; address <= 64; address += 4)
    {
      system.eeBus().write32(address, 0);
    }
    system.eeBus().write32(
      0,
      cop1SingleInstruction(
        vector.function,
        vector.sourceRegister,
        4,
        3));
    core.startExecution(0);

    system.clockMasterCycle();
    REQUIRE(core.programCounter() == 4);
    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x76543210));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      EECOP1Control::STATUS_FIXED);

    system.runMasterCycles(vector.latency - 1);
    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x76543210));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      EECOP1Control::STATUS_FIXED);

    system.clockMasterCycle();
    REQUIRE(
      core.floatingPointRegister(4) ==
      vector.expected);
    REQUIRE(
      core.cop1ControlRegister(31) ==
      vector.expectedStatus);
  }
}

TEST_CASE(
  "EE COP1 divider dependencies wait for result retirement")
{
  SECTION("An FPR consumer issues on the completion cycle")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x40c00000));
    core.setFloatingPointRegister(3, UINT32_C(0x40000000));
    core.setFloatingPointRegister(6, UINT32_C(0x3f800000));
    system.eeBus().write32(
      0,
      cop1SingleInstruction(0x03, 2, 4, 3));
    system.eeBus().write32(
      4,
      cop1SingleInstruction(0x00, 4, 5, 6));
    core.startExecution(0);

    system.clockMasterCycle();
    system.runMasterCycles(COP1_DIV_SQRT_LATENCY - 1);

    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.floatingPointRegister(4) == 0);
    REQUIRE(core.floatingPointRegister(5) == 0);

    system.clockMasterCycle();

    REQUIRE(core.programCounter() == 8);
    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x40400000));
    REQUIRE(
      core.floatingPointRegister(5) ==
      UINT32_C(0x40800000));
  }

  SECTION("CFC1 observes flags on the completion cycle")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(3, UINT32_C(0xc1100000));
    system.eeBus().write32(
      0,
      cop1SingleInstruction(0x04, 0, 4, 3));
    system.eeBus().write32(
      4,
      cop1TransferInstruction(0x02, 5, 31));
    core.startExecution(0);

    system.clockMasterCycle();
    system.runMasterCycles(COP1_DIV_SQRT_LATENCY - 1);

    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.generalRegister(5) == EERegister128{});

    system.clockMasterCycle();

    REQUIRE(core.programCounter() == 8);
    REQUIRE(
      core.generalRegister(5).low ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_INVALID |
       EECOP1Control::STICKY_INVALID));
  }

  SECTION("An FPR writer issues on the completion cycle")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(5, {UINT64_C(0x12345678), 0});
    core.setFloatingPointRegister(2, UINT32_C(0x40c00000));
    core.setFloatingPointRegister(3, UINT32_C(0x40000000));
    system.eeBus().write32(
      0,
      cop1SingleInstruction(0x03, 2, 4, 3));
    system.eeBus().write32(
      4,
      cop1TransferInstruction(0x04, 5, 4));
    core.startExecution(0);

    system.clockMasterCycle();
    system.runMasterCycles(COP1_DIV_SQRT_LATENCY - 1);

    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.floatingPointRegister(4) == 0);

    system.clockMasterCycle();

    REQUIRE(core.programCounter() == 8);
    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x12345678));
  }

  SECTION("A dependent divider issues on the completion cycle")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x40c00000));
    core.setFloatingPointRegister(3, UINT32_C(0x40000000));
    core.setFloatingPointRegister(6, UINT32_C(0x3f800000));
    system.eeBus().write32(
      0,
      cop1SingleInstruction(0x03, 2, 4, 3));
    system.eeBus().write32(
      4,
      cop1SingleInstruction(0x03, 4, 5, 6));
    core.startExecution(0);

    system.clockMasterCycle();
    system.runMasterCycles(COP1_DIV_SQRT_LATENCY - 1);

    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.floatingPointRegister(4) == 0);

    system.clockMasterCycle();

    REQUIRE(core.programCounter() == 8);
    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x40400000));
    REQUIRE(core.floatingPointRegister(5) == 0);

    system.runMasterCycles(COP1_DIV_SQRT_LATENCY);
    REQUIRE(
      core.floatingPointRegister(5) ==
      UINT32_C(0x40400000));
  }

  SECTION("CTC1 writes FCR31 on the completion cycle")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      5,
      {EECOP1Control::CAUSE_OVERFLOW, 0});
    core.setFloatingPointRegister(3, UINT32_C(0xc1100000));
    system.eeBus().write32(
      0,
      cop1SingleInstruction(0x04, 0, 4, 3));
    system.eeBus().write32(
      4,
      cop1TransferInstruction(0x06, 5, 31));
    core.startExecution(0);

    system.clockMasterCycle();
    system.runMasterCycles(COP1_DIV_SQRT_LATENCY - 1);

    REQUIRE(core.programCounter() == 4);
    REQUIRE(
      core.cop1ControlRegister(31) ==
      EECOP1Control::STATUS_FIXED);

    system.clockMasterCycle();

    REQUIRE(core.programCounter() == 8);
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_OVERFLOW));
  }
}

TEST_CASE(
  "EE COP1 divider initiation intervals permit overlapping results")
{
  SECTION("DIV.S and SQRT.S use a seven-cycle initiation interval")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x40c00000));
    core.setFloatingPointRegister(3, UINT32_C(0x40000000));
    core.setFloatingPointRegister(6, UINT32_C(0x41100000));
    system.eeBus().write32(
      0,
      cop1SingleInstruction(0x03, 2, 4, 3));
    system.eeBus().write32(
      4,
      cop1SingleInstruction(0x04, 0, 5, 6));
    core.startExecution(0);

    system.clockMasterCycle();
    system.runMasterCycles(6);
    REQUIRE(core.programCounter() == 4);

    system.clockMasterCycle();
    REQUIRE(core.programCounter() == 8);
    REQUIRE(core.floatingPointRegister(4) == 0);
    REQUIRE(core.floatingPointRegister(5) == 0);

    system.clockMasterCycle();
    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x40400000));
    REQUIRE(core.floatingPointRegister(5) == 0);

    system.runMasterCycles(7);
    REQUIRE(
      core.floatingPointRegister(5) ==
      UINT32_C(0x40400000));
  }

  SECTION("RSQRT.S uses a thirteen-cycle initiation interval")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x40c00000));
    core.setFloatingPointRegister(3, UINT32_C(0x40800000));
    core.setFloatingPointRegister(6, UINT32_C(0x41100000));
    system.eeBus().write32(
      0,
      cop1SingleInstruction(0x16, 2, 4, 3));
    system.eeBus().write32(
      4,
      cop1SingleInstruction(0x04, 0, 5, 6));
    core.startExecution(0);

    system.clockMasterCycle();
    system.runMasterCycles(12);
    REQUIRE(core.programCounter() == 4);

    system.clockMasterCycle();
    REQUIRE(core.programCounter() == 8);
    REQUIRE(core.floatingPointRegister(4) == 0);

    system.clockMasterCycle();
    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x40400000));

    system.runMasterCycles(7);
    REQUIRE(
      core.floatingPointRegister(5) ==
      UINT32_C(0x40400000));
  }
}

TEST_CASE("EE COP1 divider overlap supports every operation pairing")
{
  struct Operation
  {
    std::uint8_t function;
    std::uint8_t sourceRegister;
    std::uint8_t targetRegister;
    std::uint8_t destinationRegister;
    std::uint8_t latency;
    std::uint8_t initiationInterval;
    std::uint32_t expected;
    std::uint32_t expectedStatus;
  };
  const Operation firstOperations[] = {
    {
      0x03, 2, 3, 4, 8, 7, UINT32_C(0x40400000),
      EECOP1Control::STATUS_FIXED
    },
    {
      0x04, 0, 6, 4, 8, 7, UINT32_C(0x40400000),
      EECOP1Control::STATUS_FIXED
    },
    {
      0x16, 2, 6, 4, 14, 13, UINT32_C(0x40000000),
      EECOP1Control::STATUS_FIXED
    }
  };
  const Operation secondOperations[] = {
    {
      0x03, 11, 13, 10, 8, 7, UINT32_C(0x7fffffff),
      EECOP1Control::STATUS_FIXED |
        EECOP1Control::CAUSE_INVALID |
        EECOP1Control::STICKY_INVALID
    },
    {
      0x04, 0, 12, 10, 8, 7, UINT32_C(0x40400000),
      EECOP1Control::STATUS_FIXED |
        EECOP1Control::CAUSE_INVALID |
        EECOP1Control::STICKY_INVALID
    },
    {
      0x16, 14, 13, 10, 14, 13, UINT32_C(0x7fffffff),
      EECOP1Control::STATUS_FIXED |
        EECOP1Control::CAUSE_DIVISION_BY_ZERO |
        EECOP1Control::STICKY_DIVISION_BY_ZERO
    }
  };

  for (const Operation &first : firstOperations)
  {
    for (const Operation &second : secondOperations)
    {
      NekoSystem system;
      EECore &core = system.eeCore();
      core.setFloatingPointRegister(2, UINT32_C(0x40c00000));
      core.setFloatingPointRegister(3, UINT32_C(0x40000000));
      core.setFloatingPointRegister(6, UINT32_C(0x41100000));
      core.setFloatingPointRegister(8, UINT32_C(0x41200000));
      core.setFloatingPointRegister(9, UINT32_C(0x40800000));
      core.setFloatingPointRegister(11, 0);
      core.setFloatingPointRegister(12, UINT32_C(0xc1100000));
      core.setFloatingPointRegister(13, 0);
      core.setFloatingPointRegister(14, UINT32_C(0x3f800000));
      system.eeBus().write32(
        0,
        cop1SingleInstruction(
          first.function,
          first.sourceRegister,
          first.destinationRegister,
          first.targetRegister));
      system.eeBus().write32(
        4,
        cop1SingleInstruction(
          second.function,
          second.sourceRegister,
          second.destinationRegister,
          second.targetRegister));
      core.startExecution(0);

      system.clockMasterCycle();
      system.runMasterCycles(first.initiationInterval - 1);
      REQUIRE(core.programCounter() == 4);

      system.clockMasterCycle();
      REQUIRE(core.programCounter() == 8);
      REQUIRE(core.floatingPointRegister(4) == 0);
      REQUIRE(core.floatingPointRegister(10) == 0);

      system.clockMasterCycle();
      REQUIRE(
        core.floatingPointRegister(4) ==
        first.expected);
      REQUIRE(core.floatingPointRegister(10) == 0);
      REQUIRE(
        core.cop1ControlRegister(31) ==
        EECOP1Control::STATUS_FIXED);

      system.runMasterCycles(second.latency - 2);
      REQUIRE(core.floatingPointRegister(10) == 0);
      REQUIRE(
        core.cop1ControlRegister(31) ==
        EECOP1Control::STATUS_FIXED);

      system.clockMasterCycle();
      REQUIRE(
        core.floatingPointRegister(10) ==
        second.expected);
      REQUIRE(
        core.cop1ControlRegister(31) ==
        second.expectedStatus);
    }
  }
}

TEST_CASE(
  "EE COP1 divider state survives halt and save-state restore")
{
  NekoSystem original;
  EECore &originalCore = original.eeCore();
  originalCore.setFloatingPointRegister(2, UINT32_C(0x40c00000));
  originalCore.setFloatingPointRegister(3, UINT32_C(0x40000000));
  originalCore.setFloatingPointRegister(6, UINT32_C(0x41100000));
  original.eeBus().write32(
    0,
    cop1SingleInstruction(0x03, 2, 4, 3));
  original.eeBus().write32(
    4,
    cop1SingleInstruction(0x04, 0, 5, 6));
  originalCore.startExecution(0);
  original.clockMasterCycle();
  original.runMasterCycles(7);
  originalCore.haltExecution();

  NekoSystem restored;
  restored.loadState(original.saveState());

  REQUIRE(original.saveState() == restored.saveState());
  REQUIRE(originalCore.stateHash() == restored.eeCore().stateHash());

  originalCore.startExecution(8);
  restored.eeCore().startExecution(8);
  original.clockMasterCycle();
  restored.clockMasterCycle();

  REQUIRE(
    originalCore.floatingPointRegister(4) ==
    UINT32_C(0x40400000));
  REQUIRE(
    restored.eeCore().floatingPointRegister(4) ==
    UINT32_C(0x40400000));
  REQUIRE(original.saveState() == restored.saveState());
  REQUIRE(originalCore.stateHash() == restored.eeCore().stateHash());

  original.runMasterCycles(7);
  restored.runMasterCycles(7);

  REQUIRE(
    originalCore.floatingPointRegister(5) ==
    UINT32_C(0x40400000));
  REQUIRE(
    restored.eeCore().floatingPointRegister(5) ==
    UINT32_C(0x40400000));
  REQUIRE(original.saveState() == restored.saveState());
  REQUIRE(originalCore.stateHash() == restored.eeCore().stateHash());
}

TEST_CASE("Every EE COP1 divider operation survives save-state restore")
{
  struct SaveVector
  {
    std::uint8_t function;
    std::uint8_t sourceRegister;
    std::uint32_t fs;
    std::uint32_t ft;
    std::uint32_t expected;
    std::uint32_t expectedStatus;
    std::uint8_t latency;
  };
  const SaveVector vectors[] = {
    {
      0x03, 2, 0, 0,
      UINT32_C(0x7fffffff),
      EECOP1Control::STATUS_FIXED |
        EECOP1Control::CAUSE_INVALID |
        EECOP1Control::STICKY_INVALID,
      COP1_DIV_SQRT_LATENCY
    },
    {
      0x04, 0, 0, UINT32_C(0xc1100000),
      UINT32_C(0x40400000),
      EECOP1Control::STATUS_FIXED |
        EECOP1Control::CAUSE_INVALID |
        EECOP1Control::STICKY_INVALID,
      COP1_DIV_SQRT_LATENCY
    },
    {
      0x16, 2, UINT32_C(0x3f800000), 0,
      UINT32_C(0x7fffffff),
      EECOP1Control::STATUS_FIXED |
        EECOP1Control::CAUSE_DIVISION_BY_ZERO |
        EECOP1Control::STICKY_DIVISION_BY_ZERO,
      COP1_RSQRT_LATENCY
    }
  };

  for (const SaveVector &vector : vectors)
  {
    NekoSystem original;
    EECore &originalCore = original.eeCore();
    originalCore.setFloatingPointRegister(2, vector.fs);
    originalCore.setFloatingPointRegister(3, vector.ft);
    originalCore.setFloatingPointRegister(4, UINT32_C(0x76543210));
    original.eeBus().write32(
      0,
      cop1SingleInstruction(
        vector.function,
        vector.sourceRegister,
        4,
        3));
    originalCore.startExecution(0);
    original.runMasterCycles(3);
    originalCore.haltExecution();

    NekoSystem restored;
    restored.loadState(original.saveState());
    REQUIRE(original.saveState() == restored.saveState());
    REQUIRE(originalCore.stateHash() == restored.eeCore().stateHash());

    originalCore.startExecution(originalCore.programCounter());
    restored.eeCore().startExecution(
      restored.eeCore().programCounter());
    original.runMasterCycles(vector.latency - 3);
    restored.runMasterCycles(vector.latency - 3);

    REQUIRE(
      originalCore.floatingPointRegister(4) ==
      UINT32_C(0x76543210));
    REQUIRE(
      restored.eeCore().floatingPointRegister(4) ==
      UINT32_C(0x76543210));
    REQUIRE(
      originalCore.cop1ControlRegister(31) ==
      EECOP1Control::STATUS_FIXED);
    REQUIRE(
      restored.eeCore().cop1ControlRegister(31) ==
      EECOP1Control::STATUS_FIXED);

    original.clockMasterCycle();
    restored.clockMasterCycle();

    REQUIRE(
      originalCore.floatingPointRegister(4) ==
      vector.expected);
    REQUIRE(
      restored.eeCore().floatingPointRegister(4) ==
      vector.expected);
    REQUIRE(
      originalCore.cop1ControlRegister(31) ==
      vector.expectedStatus);
    REQUIRE(
      restored.eeCore().cop1ControlRegister(31) ==
      vector.expectedStatus);
    REQUIRE(original.saveState() == restored.saveState());
    REQUIRE(originalCore.stateHash() == restored.eeCore().stateHash());
  }
}

TEST_CASE("EE COP1 divider pending results participate in state hashes")
{
  NekoSystem first;
  NekoSystem second;
  first.eeCore().setFloatingPointRegister(2, UINT32_C(0x40c00000));
  second.eeCore().setFloatingPointRegister(2, UINT32_C(0x41000000));
  first.eeCore().setFloatingPointRegister(3, UINT32_C(0x40000000));
  second.eeCore().setFloatingPointRegister(3, UINT32_C(0x40000000));
  const std::uint32_t instruction =
    cop1SingleInstruction(0x03, 2, 4, 3);
  first.eeBus().write32(0, instruction);
  second.eeBus().write32(0, instruction);
  first.eeCore().startExecution(0);
  second.eeCore().startExecution(0);
  first.clockMasterCycle();
  second.clockMasterCycle();
  first.eeCore().setFloatingPointRegister(2, 0);
  second.eeCore().setFloatingPointRegister(2, 0);

  REQUIRE(first.eeCore().stateHash() != second.eeCore().stateHash());
}

TEST_CASE("EE COP1 divider work continues through exception entry")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(
    EECOP0Register::Status,
    EECOP0Status::RESET &
      ~EECOP0Status::BOOTSTRAP_EXCEPTION_VECTOR);
  core.setFloatingPointRegister(2, UINT32_C(0x40c00000));
  core.setFloatingPointRegister(3, UINT32_C(0x40000000));
  system.eeBus().write32(
    0,
    cop1SingleInstruction(0x03, 2, 4, 3));
  system.eeBus().write32(4, UINT32_C(0x0000000c));
  for (std::uint32_t address = EEExceptionVector::GENERAL;
       address < EEExceptionVector::GENERAL + 32;
       address += 4)
  {
    system.eeBus().write32(address, 0);
  }
  core.startExecution(0);

  system.runMasterCycles(2);
  REQUIRE(core.pendingException() == EEException::SystemCall);
  REQUIRE(
    core.programCounter() ==
    EEExceptionVector::GENERAL);
  REQUIRE(core.floatingPointRegister(4) == 0);

  system.runMasterCycles(7);

  REQUIRE(
    core.floatingPointRegister(4) ==
    UINT32_C(0x40400000));
}

TEST_CASE("EE reset cancels pending COP1 divider results")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setFloatingPointRegister(2, UINT32_C(0x40c00000));
  core.setFloatingPointRegister(3, UINT32_C(0x40000000));
  system.eeBus().write32(
    0,
    cop1SingleInstruction(0x03, 2, 4, 3));
  core.startExecution(0);
  system.clockMasterCycle();

  core.reset();
  core.startExecution(4);
  system.runMasterCycles(COP1_DIV_SQRT_LATENCY + 1);

  REQUIRE(core.floatingPointRegister(4) == 0);
  REQUIRE(
    core.cop1ControlRegister(31) ==
    EECOP1Control::STATUS_FIXED);
}

TEST_CASE("EE COP1 operate resource interlocks a following move")
{
  SECTION("SQRT.S interlocks a following MFC1")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(3, UINT32_C(0x41100000));
    system.eeBus().write32(
      0,
      cop1SingleInstruction(0x04, 0, 4, 3));
    system.eeBus().write32(
      4,
      cop1TransferInstruction(0x00, 5, 4));
    core.startExecution(0);

    system.clockMasterCycle();
    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.generalRegister(5) == EERegister128{});

    system.runMasterCycles(COP1_DIV_SQRT_LATENCY - 1);
    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.generalRegister(5) == EERegister128{});

    system.clockMasterCycle();
    REQUIRE(core.programCounter() == 8);
    REQUIRE(
      core.generalRegister(5).low ==
      UINT64_C(0x0000000040400000));
  }

  SECTION("A following MFC1 waits one cycle")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x40000000));
    core.setFloatingPointRegister(3, UINT32_C(0x40400000));
    system.eeBus().write32(
      0,
      cop1SingleInstruction(0x02, 2, 4, 3));
    system.eeBus().write32(
      4,
      cop1TransferInstruction(0x00, 5, 4));
    core.startExecution(0);

    system.clockMasterCycle();
    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.generalRegister(5) == EERegister128{});

    system.clockMasterCycle();
    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.generalRegister(5) == EERegister128{});

    system.clockMasterCycle();
    REQUIRE(core.programCounter() == 8);
    REQUIRE(
      core.generalRegister(5).low ==
      UINT64_C(0x0000000040c00000));
  }

  SECTION("A following operate issues without a structural stall")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x40000000));
    core.setFloatingPointRegister(3, UINT32_C(0x40400000));
    core.setFloatingPointRegister(5, UINT32_C(0x3f000000));
    system.eeBus().write32(
      0,
      cop1SingleInstruction(0x02, 2, 4, 3));
    system.eeBus().write32(
      4,
      cop1SingleInstruction(0x00, 4, 6, 5));
    core.startExecution(0);

    system.runMasterCycles(2);

    REQUIRE(core.programCounter() == 8);
    REQUIRE(
      core.floatingPointRegister(6) ==
      UINT32_C(0x40d00000));
  }

  SECTION("An intervening non-move consumes the occupancy window")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x40000000));
    core.setFloatingPointRegister(3, UINT32_C(0x40400000));
    system.eeBus().write32(
      0,
      cop1SingleInstruction(0x02, 2, 4, 3));
    system.eeBus().write32(4, 0);
    system.eeBus().write32(
      8,
      cop1TransferInstruction(0x00, 5, 4));
    core.startExecution(0);

    system.runMasterCycles(3);

    REQUIRE(core.programCounter() == 12);
    REQUIRE(
      core.generalRegister(5).low ==
      UINT64_C(0x0000000040c00000));
  }
}

TEST_CASE("EE COP1 resource occupancy survives halt and save-state restore")
{
  NekoSystem original;
  EECore &originalCore = original.eeCore();
  originalCore.setFloatingPointRegister(2, UINT32_C(0x40000000));
  originalCore.setFloatingPointRegister(3, UINT32_C(0x40400000));
  original.eeBus().write32(
    0,
    cop1SingleInstruction(0x02, 2, 4, 3));
  original.eeBus().write32(
    4,
    cop1TransferInstruction(0x00, 5, 4));
  originalCore.startExecution(0);
  original.clockMasterCycle();
  originalCore.haltExecution();

  NekoSystem restored;
  restored.loadState(original.saveState());
  originalCore.startExecution(4);
  restored.eeCore().startExecution(4);

  original.clockMasterCycle();
  restored.clockMasterCycle();

  REQUIRE(originalCore.programCounter() == 4);
  REQUIRE(restored.eeCore().programCounter() == 4);
  REQUIRE(original.saveState() == restored.saveState());
  REQUIRE(originalCore.stateHash() == restored.eeCore().stateHash());

  original.clockMasterCycle();
  restored.clockMasterCycle();

  REQUIRE(originalCore.programCounter() == 8);
  REQUIRE(restored.eeCore().programCounter() == 8);
  REQUIRE(
    restored.eeCore().generalRegister(5).low ==
    UINT64_C(0x0000000040c00000));
  REQUIRE(original.saveState() == restored.saveState());
  REQUIRE(originalCore.stateHash() == restored.eeCore().stateHash());
}

TEST_CASE(
  "EE COP1 add and subtract preserve FPR write-after-write order")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setFloatingPointRegister(2, UINT32_C(0x3f800000));
  core.setFloatingPointRegister(3, UINT32_C(0x40000000));
  core.setFloatingPointRegister(6, UINT32_C(0x40a00000));
  system.eeBus().write32(
    0,
    cop1SingleInstruction(0x00, 2, 4, 3));
  system.eeBus().write32(
    4,
    cop1SingleInstruction(0x01, 6, 4, 2));
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(
    core.floatingPointRegister(4) ==
    UINT32_C(0x40400000));

  system.clockMasterCycle();

  REQUIRE(core.programCounter() == 8);
  REQUIRE(
    core.floatingPointRegister(4) ==
    UINT32_C(0x40800000));
}

TEST_CASE(
  "EE COP1 accumulator writes preserve write-after-write order")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setFloatingPointRegister(2, UINT32_C(0x3f800000));
  core.setFloatingPointRegister(3, UINT32_C(0x40000000));
  core.setFloatingPointRegister(6, UINT32_C(0x40a00000));
  system.eeBus().write32(
    0,
    cop1SingleInstruction(0x18, 2, 0, 3));
  system.eeBus().write32(
    4,
    cop1SingleInstruction(0x19, 6, 0, 2));
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(
    core.floatingPointAccumulator() ==
    UINT32_C(0x40400000));

  system.clockMasterCycle();

  REQUIRE(core.programCounter() == 8);
  REQUIRE(
    core.floatingPointAccumulator() ==
    UINT32_C(0x40800000));
}

TEST_CASE(
  "EE COP1 FPR and accumulator destinations remain independent")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setFloatingPointRegister(2, UINT32_C(0x3f800000));
  core.setFloatingPointRegister(3, UINT32_C(0x40000000));
  core.setFloatingPointAccumulator(UINT32_C(0x41100000));
  system.eeBus().write32(
    0,
    cop1SingleInstruction(0x00, 2, 4, 3));
  system.eeBus().write32(
    4,
    cop1SingleInstruction(0x19, 3, 0, 2));
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(
    core.floatingPointRegister(4) ==
    UINT32_C(0x40400000));
  REQUIRE(
    core.floatingPointAccumulator() ==
    UINT32_C(0x41100000));

  system.clockMasterCycle();

  REQUIRE(core.programCounter() == 8);
  REQUIRE(
    core.floatingPointRegister(4) ==
    UINT32_C(0x40400000));
  REQUIRE(
    core.floatingPointAccumulator() ==
    UINT32_C(0x3f800000));
}

TEST_CASE(
  "EE COP1 independent addition and subtraction retire every cycle")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setFloatingPointRegister(2, UINT32_C(0x3f800000));
  core.setFloatingPointRegister(3, UINT32_C(0x40000000));
  core.setFloatingPointRegister(6, UINT32_C(0x40a00000));
  core.setFloatingPointRegister(7, UINT32_C(0x3f000000));
  system.eeBus().write32(
    0,
    cop1SingleInstruction(0x00, 2, 4, 3));
  system.eeBus().write32(
    4,
    cop1SingleInstruction(0x01, 6, 5, 7));
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(core.elapsedCycles() == 1);
  REQUIRE(core.programCounter() == 4);
  REQUIRE(
    core.floatingPointRegister(4) ==
    UINT32_C(0x40400000));
  REQUIRE(core.floatingPointRegister(5) == 0);

  system.clockMasterCycle();

  REQUIRE(core.elapsedCycles() == 2);
  REQUIRE(core.programCounter() == 8);
  REQUIRE(
    core.floatingPointRegister(5) ==
    UINT32_C(0x40900000));
}

TEST_CASE(
  "EE COP1 exceptional results are visible at the retirement boundary")
{
  SECTION("An overflowed FPR result is available to the next move")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x7f800000));
    core.setFloatingPointRegister(3, UINT32_C(0x7f800000));
    system.eeBus().write32(
      0,
      cop1SingleInstruction(0x00, 2, 4, 3));
    system.eeBus().write32(
      4,
      cop1SingleInstruction(0x06, 4, 5));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x7fffffff));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_OVERFLOW |
       EECOP1Control::STICKY_OVERFLOW));

    system.clockMasterCycle();

    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.floatingPointRegister(5) == 0);
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_OVERFLOW |
       EECOP1Control::STICKY_OVERFLOW));

    system.clockMasterCycle();

    REQUIRE(core.programCounter() == 8);
    REQUIRE(
      core.floatingPointRegister(5) ==
      UINT32_C(0x7fffffff));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_OVERFLOW |
       EECOP1Control::STICKY_OVERFLOW));
  }

  SECTION("A younger ACC result clears the current flag but keeps history")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setFloatingPointRegister(2, UINT32_C(0x00800001));
    core.setFloatingPointRegister(3, UINT32_C(0x00800000));
    core.setFloatingPointRegister(6, UINT32_C(0x3f800000));
    core.setFloatingPointRegister(7, UINT32_C(0x40000000));
    system.eeBus().write32(
      0,
      cop1SingleInstruction(0x19, 2, 0, 3));
    system.eeBus().write32(
      4,
      cop1SingleInstruction(0x18, 6, 0, 7));
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(core.floatingPointAccumulator() == 0);
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::CAUSE_UNDERFLOW |
       EECOP1Control::STICKY_UNDERFLOW));

    system.clockMasterCycle();

    REQUIRE(core.programCounter() == 8);
    REQUIRE(
      core.floatingPointAccumulator() ==
      UINT32_C(0x40400000));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      (EECOP1Control::STATUS_FIXED |
       EECOP1Control::STICKY_UNDERFLOW));
  }
}

TEST_CASE("EE COP1 exceptional multiply timing preserves flag order")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setFloatingPointAccumulator(UINT32_C(0x3f800000));
  core.setFloatingPointRegister(2, UINT32_C(0x7f800000));
  core.setFloatingPointRegister(3, UINT32_C(0x40000000));
  core.setFloatingPointRegister(5, UINT32_C(0x40000000));
  core.setFloatingPointRegister(6, UINT32_C(0x40400000));
  system.eeBus().write32(
    0,
    cop1SingleInstruction(0x02, 2, 4, 3));
  system.eeBus().write32(
    4,
    cop1SingleInstruction(0x1c, 5, 7, 6));
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(core.programCounter() == 4);
  REQUIRE(
    core.floatingPointRegister(4) ==
    UINT32_C(0x7fffffff));
  REQUIRE(
    core.cop1ControlRegister(31) ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::CAUSE_OVERFLOW |
     EECOP1Control::STICKY_OVERFLOW));

  system.clockMasterCycle();

  REQUIRE(core.programCounter() == 8);
  REQUIRE(
    core.floatingPointRegister(7) ==
    UINT32_C(0x40e00000));
  REQUIRE(
    core.cop1ControlRegister(31) ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::STICKY_OVERFLOW));
}

TEST_CASE(
  "EE COP1 multiply timing survives host halt and save-state restore")
{
  SECTION("A restored compound operation sees a multiplied FPR")
  {
    NekoSystem original;
    EECore &originalCore = original.eeCore();
    originalCore.setFloatingPointAccumulator(
      UINT32_C(0x3f800000));
    originalCore.setFloatingPointRegister(
      2,
      UINT32_C(0x40000000));
    originalCore.setFloatingPointRegister(
      3,
      UINT32_C(0x40400000));
    originalCore.setFloatingPointRegister(
      5,
      UINT32_C(0x3f000000));
    original.eeBus().write32(
      0,
      cop1SingleInstruction(0x02, 2, 4, 3));
    original.eeBus().write32(
      4,
      cop1SingleInstruction(0x1c, 4, 6, 5));
    originalCore.startExecution(0);
    original.clockMasterCycle();
    originalCore.haltExecution();

    NekoSystem restored;
    restored.loadState(original.saveState());
    originalCore.startExecution(4);
    restored.eeCore().startExecution(4);
    original.clockMasterCycle();
    restored.clockMasterCycle();

    REQUIRE(
      originalCore.floatingPointRegister(6) ==
      UINT32_C(0x40800000));
    REQUIRE(
      restored.eeCore().floatingPointRegister(6) ==
      UINT32_C(0x40800000));
    REQUIRE(original.saveState() == restored.saveState());
    REQUIRE(originalCore.stateHash() == restored.eeCore().stateHash());
  }

  SECTION("A restored compound operation sees a multiplied ACC")
  {
    NekoSystem original;
    EECore &originalCore = original.eeCore();
    originalCore.setFloatingPointRegister(
      2,
      UINT32_C(0x40000000));
    originalCore.setFloatingPointRegister(
      3,
      UINT32_C(0x40400000));
    originalCore.setFloatingPointRegister(
      5,
      UINT32_C(0x3f000000));
    originalCore.setFloatingPointRegister(
      6,
      UINT32_C(0x40000000));
    original.eeBus().write32(
      0,
      cop1SingleInstruction(0x1a, 2, 0, 3));
    original.eeBus().write32(
      4,
      cop1SingleInstruction(0x1d, 5, 4, 6));
    originalCore.startExecution(0);
    original.clockMasterCycle();
    originalCore.haltExecution();

    NekoSystem restored;
    restored.loadState(original.saveState());
    originalCore.startExecution(4);
    restored.eeCore().startExecution(4);
    original.clockMasterCycle();
    restored.clockMasterCycle();

    REQUIRE(
      originalCore.floatingPointRegister(4) ==
      UINT32_C(0x40a00000));
    REQUIRE(
      restored.eeCore().floatingPointRegister(4) ==
      UINT32_C(0x40a00000));
    REQUIRE(original.saveState() == restored.saveState());
    REQUIRE(originalCore.stateHash() == restored.eeCore().stateHash());
  }
}

TEST_CASE(
  "EE COP1 addition timing survives host halt and save-state restore")
{
  SECTION("A restored dependent FPR instruction sees the older result")
  {
    NekoSystem original;
    EECore &originalCore = original.eeCore();
    originalCore.setFloatingPointRegister(
      2,
      UINT32_C(0x3f800000));
    originalCore.setFloatingPointRegister(
      3,
      UINT32_C(0x40000000));
    original.eeBus().write32(
      0,
      cop1SingleInstruction(0x00, 2, 4, 3));
    original.eeBus().write32(
      4,
      cop1SingleInstruction(0x01, 4, 5, 2));
    originalCore.startExecution(0);
    original.clockMasterCycle();
    originalCore.haltExecution();

    NekoSystem restored;
    restored.loadState(original.saveState());
    originalCore.startExecution(4);
    restored.eeCore().startExecution(4);
    original.clockMasterCycle();
    restored.clockMasterCycle();

    REQUIRE(
      originalCore.floatingPointRegister(5) ==
      UINT32_C(0x40000000));
    REQUIRE(
      restored.eeCore().floatingPointRegister(5) ==
      UINT32_C(0x40000000));
    REQUIRE(original.saveState() == restored.saveState());
    REQUIRE(originalCore.stateHash() == restored.eeCore().stateHash());
  }

  SECTION("Restored ACC writes preserve younger-write ordering")
  {
    NekoSystem original;
    EECore &originalCore = original.eeCore();
    originalCore.setFloatingPointRegister(
      2,
      UINT32_C(0x3f800000));
    originalCore.setFloatingPointRegister(
      3,
      UINT32_C(0x40000000));
    originalCore.setFloatingPointRegister(
      6,
      UINT32_C(0x40a00000));
    original.eeBus().write32(
      0,
      cop1SingleInstruction(0x18, 2, 0, 3));
    original.eeBus().write32(
      4,
      cop1SingleInstruction(0x19, 6, 0, 2));
    originalCore.startExecution(0);
    original.clockMasterCycle();
    originalCore.haltExecution();

    NekoSystem restored;
    restored.loadState(original.saveState());
    originalCore.startExecution(4);
    restored.eeCore().startExecution(4);
    original.clockMasterCycle();
    restored.clockMasterCycle();

    REQUIRE(
      originalCore.floatingPointAccumulator() ==
      UINT32_C(0x40800000));
    REQUIRE(
      restored.eeCore().floatingPointAccumulator() ==
      UINT32_C(0x40800000));
    REQUIRE(original.saveState() == restored.saveState());
    REQUIRE(originalCore.stateHash() == restored.eeCore().stateHash());
  }
}

TEST_CASE("EE COP1 control transfers reject reserved FCRs")
{
  for (std::uint8_t controlRegister = 1;
       controlRegister < 31;
       ++controlRegister)
  {
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(
        cop1TransferInstruction(
          0x02,
          2,
          controlRegister)),
      "Reserved EE instruction encoding.");
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(
        cop1TransferInstruction(
          0x06,
          2,
          controlRegister)),
      "Reserved EE instruction encoding.");
  }
}

TEST_CASE("EE MFC1 sign-extends raw FPR words into GPRs")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setFloatingPointRegister(3, UINT32_C(0x89abcdef));
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0x1111111122222222),
      UINT64_C(0x3333333344444444)
    });

  runInstruction(
    &system,
    cop1TransferInstruction(0x00, 2, 3));

  REQUIRE(
    core.generalRegister(2) ==
    EERegister128{
      UINT64_C(0xffffffff89abcdef),
      UINT64_C(0x3333333344444444)
    });
}

TEST_CASE("EE MTC1 writes the low GPR word into any FPR")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0x1122334489abcdef),
      UINT64_C(0xfedcba9876543210)
    });

  runInstruction(
    &system,
    cop1TransferInstruction(0x04, 2, 0));

  REQUIRE(
    core.floatingPointRegister(0) ==
    UINT32_C(0x89abcdef));
}

TEST_CASE("EE COP1 transfers preserve GPR zero")
{
  NekoSystem system;
  system.eeCore().setFloatingPointRegister(
    3,
    UINT32_C(0xffffffff));

  runInstruction(
    &system,
    cop1TransferInstruction(0x00, 0, 3));

  REQUIRE(
    system.eeCore().generalRegister(0) ==
    EERegister128{});
}

TEST_CASE("EE CFC1 exposes implemented control-register values")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP1ControlRegister(31, UINT32_MAX);
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0xaaaaaaaaaaaaaaaa),
      UINT64_C(0xbbbbbbbbbbbbbbbb)
    });

  runInstruction(
    &system,
    cop1TransferInstruction(0x02, 2, 0));

  REQUIRE(
    core.generalRegister(2) ==
    EERegister128{
      EECOP1Control::IMPLEMENTATION_REVISION,
      UINT64_C(0xbbbbbbbbbbbbbbbb)
    });

  runInstruction(
    &system,
    cop1TransferInstruction(0x02, 2, 31));

  REQUIRE(
    core.generalRegister(2) ==
    EERegister128{
      EECOP1Control::STATUS_FIXED |
        EECOP1Control::STATUS_WRITABLE_MASK,
      UINT64_C(0xbbbbbbbbbbbbbbbb)
    });
}

TEST_CASE("EE CTC1 applies control-register architectural masks")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0x11223344ffffffff),
      UINT64_C(0xfedcba9876543210)
    });

  runInstruction(
    &system,
    cop1TransferInstruction(0x06, 2, 0));
  REQUIRE(
    core.cop1ControlRegister(0) ==
    EECOP1Control::IMPLEMENTATION_REVISION);

  runInstruction(
    &system,
    cop1TransferInstruction(0x06, 2, 31));
  REQUIRE(
    core.cop1ControlRegister(31) ==
    (EECOP1Control::STATUS_FIXED |
     EECOP1Control::STATUS_WRITABLE_MASK));
}

TEST_CASE("EE LWC1 and SWC1 transfer raw words through memory")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(
    1,
    {
      UINT64_C(0x1234000000000104),
      UINT64_C(0xabcdef0123456789)
    });
  REQUIRE(
    system.eeBus().writeData32(
      0x100,
      UINT32_C(0x89abcdef)));

  system.eeBus().write32(
    0,
    cop1MemoryInstruction(0x31, 1, 3, 0xfffc));
  system.eeBus().write32(4, 0);
  core.startExecution(0);
  system.clockMasterCycle();
  REQUIRE(core.floatingPointRegister(3) == 0);
  system.clockMasterCycle();
  REQUIRE(
    core.floatingPointRegister(3) ==
    UINT32_C(0x89abcdef));

  core.setFloatingPointRegister(4, UINT32_C(0x76543210));
  runInstruction(
    &system,
    cop1MemoryInstruction(0x39, 1, 4, 0));

  std::uint32_t stored = 0;
  REQUIRE(system.eeBus().readData32(0x104, &stored));
  REQUIRE(stored == UINT32_C(0x76543210));
}

TEST_CASE("EE LWC1 stalls an immediate dependent FPR use")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(1, {0x100, 0});
  core.setFloatingPointRegister(3, UINT32_C(0x11111111));
  REQUIRE(
    system.eeBus().writeData32(
      0x100,
      UINT32_C(0x89abcdef)));
  system.eeBus().write32(
    0,
    cop1MemoryInstruction(0x31, 1, 3, 0));
  system.eeBus().write32(
    4,
    cop1TransferInstruction(0x00, 2, 3));
  core.startExecution(0);

  system.clockMasterCycle();
  REQUIRE(core.programCounter() == 4);
  REQUIRE(
    core.floatingPointRegister(3) ==
    UINT32_C(0x11111111));

  system.clockMasterCycle();
  REQUIRE(core.programCounter() == 4);
  REQUIRE(core.generalRegister(2) == EERegister128{});
  REQUIRE(
    core.floatingPointRegister(3) ==
    UINT32_C(0x89abcdef));

  system.clockMasterCycle();
  REQUIRE(core.programCounter() == 8);
  REQUIRE(
    core.generalRegister(2).low ==
    UINT64_C(0xffffffff89abcdef));
}

TEST_CASE("EE LWC1 permits independent work during writeback")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(1, {0x100, 0});
  REQUIRE(
    system.eeBus().writeData32(
      0x100,
      UINT32_C(0x12345678)));
  system.eeBus().write32(
    0,
    cop1MemoryInstruction(0x31, 1, 3, 0));
  system.eeBus().write32(
    4,
    (UINT32_C(0x0d) << 26) |
      (UINT32_C(4) << 16) |
      UINT32_C(0x55));
  system.eeBus().write32(
    8,
    cop1TransferInstruction(0x00, 2, 3));
  core.startExecution(0);

  system.runMasterCycles(2);

  REQUIRE(core.programCounter() == 8);
  REQUIRE(core.generalRegister(4).low == 0x55);
  REQUIRE(
    core.floatingPointRegister(3) ==
    UINT32_C(0x12345678));

  system.clockMasterCycle();
  REQUIRE(core.programCounter() == 12);
  REQUIRE(core.generalRegister(2).low == 0x12345678);
}

TEST_CASE("EE LWC1 interlocks younger writes to the same FPR")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(1, {0x100, 0});
  core.setGeneralRegister(
    2,
    {UINT32_C(0x76543210), 0});
  REQUIRE(
    system.eeBus().writeData32(
      0x100,
      UINT32_C(0x12345678)));
  system.eeBus().write32(
    0,
    cop1MemoryInstruction(0x31, 1, 3, 0));
  system.eeBus().write32(
    4,
    cop1TransferInstruction(0x04, 2, 3));
  core.startExecution(0);

  system.runMasterCycles(2);

  REQUIRE(core.programCounter() == 4);
  REQUIRE(
    core.floatingPointRegister(3) ==
    UINT32_C(0x12345678));

  system.clockMasterCycle();
  REQUIRE(core.programCounter() == 8);
  REQUIRE(
    core.floatingPointRegister(3) ==
    UINT32_C(0x76543210));
}

TEST_CASE("EE LWC1 interlocks an immediate SWC1 source")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(1, {0x100, 0});
  REQUIRE(
    system.eeBus().writeData32(
      0x100,
      UINT32_C(0x89abcdef)));
  system.eeBus().write32(
    0,
    cop1MemoryInstruction(0x31, 1, 3, 0));
  system.eeBus().write32(
    4,
    cop1MemoryInstruction(0x39, 1, 3, 4));
  core.startExecution(0);

  system.runMasterCycles(2);

  REQUIRE(core.programCounter() == 4);
  std::uint32_t stored = 0;
  REQUIRE(system.eeBus().readData32(0x104, &stored));
  REQUIRE(stored == 0);

  system.clockMasterCycle();

  REQUIRE(core.programCounter() == 8);
  REQUIRE(system.eeBus().readData32(0x104, &stored));
  REQUIRE(stored == UINT32_C(0x89abcdef));
}

TEST_CASE(
  "EE LWC1 interlocks every completed scalar COP1 operation")
{
  struct DependencyVector
  {
    std::uint32_t instruction;
    std::uint8_t loadedRegister;
  };
  const DependencyVector vectors[] = {
    {cop1SingleInstruction(0x05, 2, 4), 2},
    {cop1SingleInstruction(0x05, 2, 4), 4},
    {cop1SingleInstruction(0x06, 2, 4), 2},
    {cop1SingleInstruction(0x06, 2, 4), 4},
    {cop1SingleInstruction(0x07, 2, 4), 2},
    {cop1SingleInstruction(0x07, 2, 4), 4},
    {cop1SingleInstruction(0x28, 2, 4, 3), 2},
    {cop1SingleInstruction(0x28, 2, 4, 3), 3},
    {cop1SingleInstruction(0x28, 2, 4, 3), 4},
    {cop1SingleInstruction(0x29, 2, 4, 3), 2},
    {cop1SingleInstruction(0x29, 2, 4, 3), 3},
    {cop1SingleInstruction(0x29, 2, 4, 3), 4},
    {cop1SingleInstruction(0x00, 2, 4, 3), 2},
    {cop1SingleInstruction(0x00, 2, 4, 3), 3},
    {cop1SingleInstruction(0x00, 2, 4, 3), 4},
    {cop1SingleInstruction(0x01, 2, 4, 3), 2},
    {cop1SingleInstruction(0x01, 2, 4, 3), 3},
    {cop1SingleInstruction(0x01, 2, 4, 3), 4},
    {cop1SingleInstruction(0x02, 2, 4, 3), 2},
    {cop1SingleInstruction(0x02, 2, 4, 3), 3},
    {cop1SingleInstruction(0x02, 2, 4, 3), 4},
    {cop1SingleInstruction(0x1c, 2, 4, 3), 2},
    {cop1SingleInstruction(0x1c, 2, 4, 3), 3},
    {cop1SingleInstruction(0x1c, 2, 4, 3), 4},
    {cop1SingleInstruction(0x1d, 2, 4, 3), 2},
    {cop1SingleInstruction(0x1d, 2, 4, 3), 3},
    {cop1SingleInstruction(0x1d, 2, 4, 3), 4},
    {cop1SingleInstruction(0x18, 2, 0, 3), 2},
    {cop1SingleInstruction(0x18, 2, 0, 3), 3},
    {cop1SingleInstruction(0x19, 2, 0, 3), 2},
    {cop1SingleInstruction(0x19, 2, 0, 3), 3},
    {cop1SingleInstruction(0x1a, 2, 0, 3), 2},
    {cop1SingleInstruction(0x1a, 2, 0, 3), 3},
    {cop1SingleInstruction(0x1e, 2, 0, 3), 2},
    {cop1SingleInstruction(0x1e, 2, 0, 3), 3},
    {cop1SingleInstruction(0x1f, 2, 0, 3), 2},
    {cop1SingleInstruction(0x1f, 2, 0, 3), 3},
    {cop1WordInstruction(0x20, 2, 4), 2},
    {cop1WordInstruction(0x20, 2, 4), 4},
    {cop1SingleInstruction(0x24, 2, 4), 2},
    {cop1SingleInstruction(0x24, 2, 4), 4}
  };

  for (const DependencyVector &vector : vectors)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, {0x100, 0});
    core.setFloatingPointRegister(2, UINT32_C(0x3f800000));
    core.setFloatingPointRegister(3, UINT32_C(0x40000000));
    REQUIRE(
      system.eeBus().writeData32(
        0x100,
        UINT32_C(0x40400000)));
    system.eeBus().write32(
      0,
      cop1MemoryInstruction(
        0x31,
        1,
        vector.loadedRegister,
        0));
    system.eeBus().write32(4, vector.instruction);
    core.startExecution(0);

    system.runMasterCycles(2);

    REQUIRE(core.programCounter() == 4);

    system.clockMasterCycle();

    REQUIRE(core.programCounter() == 8);
  }
}

TEST_CASE("EE pending COP1 loads survive host halt and save-state restore")
{
  NekoSystem original;
  EECore &originalCore = original.eeCore();
  originalCore.setGeneralRegister(1, {0x100, 0});
  REQUIRE(
    original.eeBus().writeData32(
      0x100,
      UINT32_C(0x89abcdef)));
  original.eeBus().write32(
    0,
    cop1MemoryInstruction(0x31, 1, 3, 0));
  original.eeBus().write32(
    4,
    cop1TransferInstruction(0x00, 2, 3));
  originalCore.startExecution(0);
  original.clockMasterCycle();
  originalCore.haltExecution();

  const std::vector<std::uint8_t> saved = original.saveState();
  NekoSystem restored;
  restored.loadState(saved);

  originalCore.startExecution(4);
  restored.eeCore().startExecution(4);
  original.clockMasterCycle();
  restored.clockMasterCycle();

  REQUIRE(originalCore.programCounter() == 4);
  REQUIRE(restored.eeCore().programCounter() == 4);
  REQUIRE(
    originalCore.floatingPointRegister(3) ==
    UINT32_C(0x89abcdef));
  REQUIRE(
    restored.eeCore().floatingPointRegister(3) ==
    UINT32_C(0x89abcdef));
  REQUIRE(originalCore.stateHash() == restored.eeCore().stateHash());

  original.clockMasterCycle();
  restored.clockMasterCycle();
  REQUIRE(
    originalCore.generalRegister(2) ==
    restored.eeCore().generalRegister(2));
  REQUIRE(originalCore.stateHash() == restored.eeCore().stateHash());
}

TEST_CASE("EE reset cancels pending COP1 loads")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(1, {0x100, 0});
  REQUIRE(
    system.eeBus().writeData32(
      0x100,
      UINT32_C(0x89abcdef)));

  runInstruction(
    &system,
    cop1MemoryInstruction(0x31, 1, 3, 0));
  REQUIRE(core.floatingPointRegister(3) == 0);

  core.reset();
  system.eeBus().write32(0, 0);
  core.startExecution(0);
  system.clockMasterCycle();

  REQUIRE(core.floatingPointRegister(3) == 0);
}

TEST_CASE("EE COP1 word memory accesses use RAM aliases and boundaries")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  const std::uint32_t boundary =
    EEMemoryMap::MAIN_MEMORY_SIZE - 4;
  REQUIRE(
    system.eeBus().writeData32(
      boundary,
      UINT32_C(0x89abcdef)));
  core.setGeneralRegister(
    1,
    {EEMemoryMap::KSEG0_BASE + boundary, 0});
  system.eeBus().write32(
    0,
    cop1MemoryInstruction(0x31, 1, 3, 0));
  system.eeBus().write32(4, 0);
  core.startExecution(0);
  system.runMasterCycles(2);

  REQUIRE(
    core.floatingPointRegister(3) ==
    UINT32_C(0x89abcdef));

  core.setFloatingPointRegister(4, UINT32_C(0x76543210));
  core.setGeneralRegister(
    1,
    {EEMemoryMap::KSEG1_BASE + boundary, 0});
  runInstruction(
    &system,
    cop1MemoryInstruction(0x39, 1, 4, 0));

  std::uint32_t stored = 0;
  REQUIRE(system.eeBus().readData32(boundary, &stored));
  REQUIRE(stored == UINT32_C(0x76543210));
}

TEST_CASE("EE COP1 word memory alignment faults are precise")
{
  SECTION("LWC1 preserves its destination")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, {0x102, 0});
    core.setFloatingPointRegister(2, UINT32_C(0x12345678));

    runInstruction(
      &system,
      cop1MemoryInstruction(0x31, 1, 2, 0));

    REQUIRE(
      core.pendingException() ==
      EEException::AddressErrorLoadOrFetch);
    REQUIRE(core.exceptionAddress() == 0x102);
    REQUIRE(
      core.floatingPointRegister(2) ==
      UINT32_C(0x12345678));
  }

  SECTION("SWC1 performs no partial write")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, {0x102, 0});
    core.setFloatingPointRegister(2, UINT32_C(0xaabbccdd));
    REQUIRE(
      system.eeBus().writeData32(
        0x100,
        UINT32_C(0x11223344)));

    runInstruction(
      &system,
      cop1MemoryInstruction(0x39, 1, 2, 0));

    REQUIRE(
      core.pendingException() ==
      EEException::AddressErrorStore);
    REQUIRE(core.exceptionAddress() == 0x102);
    std::uint32_t stored = 0;
    REQUIRE(system.eeBus().readData32(0x100, &stored));
    REQUIRE(stored == UINT32_C(0x11223344));
  }
}

TEST_CASE("EE COP1 alignment faults identify branch delay slots")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Status, 0);
  core.setGeneralRegister(1, {0x102, 0});
  system.eeBus().write32(
    0,
    (UINT32_C(0x04) << 26) |
      UINT32_C(1));
  system.eeBus().write32(
    4,
    cop1MemoryInstruction(0x31, 1, 2, 0));
  core.startExecution(0);

  system.runMasterCycles(2);

  REQUIRE(
    core.pendingException() ==
    EEException::CoprocessorUnusable);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Cause) &
      EECOP0Cause::BRANCH_DELAY) != 0);

  core.setCOP0Register(
    EECOP0Register::Status,
    EECOP0Status::COP1_USABLE);
  core.clearPendingException();
  core.setProgramCounter(0);
  core.startExecution(0);
  system.runMasterCycles(2);

  REQUIRE(
    core.pendingException() ==
    EEException::AddressErrorLoadOrFetch);
  REQUIRE(core.exceptionAddress() == 0x102);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Cause) &
      EECOP0Cause::BRANCH_DELAY) != 0);
}

TEST_CASE("EE COP1 word memory bus faults preserve architectural state")
{
  SECTION("LWC1 reports a load bus error")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      1,
      {EEMemoryMap::MAIN_MEMORY_SIZE, 0});
    core.setFloatingPointRegister(2, UINT32_C(0x12345678));

    runInstruction(
      &system,
      cop1MemoryInstruction(0x31, 1, 2, 0));

    REQUIRE(
      core.pendingException() ==
      EEException::DataBusErrorLoad);
    REQUIRE(
      core.exceptionAddress() ==
      EEMemoryMap::MAIN_MEMORY_SIZE);
    REQUIRE(
      core.floatingPointRegister(2) ==
      UINT32_C(0x12345678));
  }

  SECTION("SWC1 reports a store bus error")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      1,
      {EEMemoryMap::MAIN_MEMORY_SIZE, 0});
    core.setFloatingPointRegister(2, UINT32_C(0x89abcdef));

    runInstruction(
      &system,
      cop1MemoryInstruction(0x39, 1, 2, 0));

    REQUIRE(
      core.pendingException() ==
      EEException::DataBusErrorStore);
    REQUIRE(
      core.exceptionAddress() ==
      EEMemoryMap::MAIN_MEMORY_SIZE);
  }
}

TEST_CASE("EE COP1 instructions require Status CU1")
{
  const std::uint32_t instructions[] = {
    cop1TransferInstruction(0x00, 2, 3),
    cop1TransferInstruction(0x04, 2, 3),
    cop1TransferInstruction(0x02, 2, 31),
    cop1TransferInstruction(0x06, 2, 31),
    cop1MemoryInstruction(0x31, 1, 3, 2),
    cop1MemoryInstruction(0x39, 1, 3, 2),
    cop1SingleInstruction(0x05, 3, 4),
    cop1SingleInstruction(0x06, 3, 4),
    cop1SingleInstruction(0x07, 3, 4),
    cop1SingleInstruction(0x00, 3, 4, 5),
    cop1SingleInstruction(0x01, 3, 4, 5),
    cop1SingleInstruction(0x02, 3, 4, 5),
    cop1SingleInstruction(0x03, 3, 4, 5),
    cop1SingleInstruction(0x04, 0, 4, 5),
    cop1SingleInstruction(0x16, 3, 4, 5),
    cop1SingleInstruction(0x1c, 3, 4, 5),
    cop1SingleInstruction(0x1d, 3, 4, 5),
    cop1SingleInstruction(0x18, 3, 0, 5),
    cop1SingleInstruction(0x19, 3, 0, 5),
    cop1SingleInstruction(0x1a, 3, 0, 5),
    cop1SingleInstruction(0x1e, 3, 0, 5),
    cop1SingleInstruction(0x1f, 3, 0, 5),
    cop1SingleInstruction(0x28, 3, 4, 5),
    cop1SingleInstruction(0x29, 3, 4, 5),
    cop1WordInstruction(0x20, 3, 4),
    cop1SingleInstruction(0x24, 3, 4),
    cop1SingleInstruction(0x30, 3, 0, 5),
    cop1SingleInstruction(0x32, 3, 0, 5),
    cop1SingleInstruction(0x34, 3, 0, 5),
    cop1SingleInstruction(0x36, 3, 0, 5),
    cop1BranchInstruction(0, 2),
    cop1BranchInstruction(1, 2),
    cop1BranchInstruction(2, 2),
    cop1BranchInstruction(3, 2)
  };

  for (const std::uint32_t instruction : instructions)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Status, 0);
    core.setCOP0Register(
      EECOP0Register::Cause,
      UINT32_C(0xc0000000));
    core.setGeneralRegister(
      2,
      {
        UINT64_C(0x1111111189abcdef),
        UINT64_C(0x2222222233333333)
      });
    core.setFloatingPointRegister(3, UINT32_C(0x76543210));
    core.setFloatingPointRegister(4, UINT32_C(0x12345678));
    core.setFloatingPointRegister(5, UINT32_C(0x11223344));
    REQUIRE(
      system.eeBus().writeData32(
        0,
        UINT32_C(0x11223344)));
    system.eeBus().write32(0, instruction);
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.pendingException() ==
      EEException::CoprocessorUnusable);
    REQUIRE(
      ((core.cop0Register(EECOP0Register::Cause) &
        EECOP0Cause::EXCEPTION_CODE_MASK) >> 2) ==
      EEExceptionCode::COPROCESSOR_UNUSABLE);
    REQUIRE(
      (core.cop0Register(EECOP0Register::Cause) &
        EECOP0Cause::COPROCESSOR_ERROR_MASK) ==
      EECOP0Cause::COPROCESSOR_1);
    REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
    REQUIRE(
      core.programCounter() ==
      EEExceptionVector::GENERAL);
    REQUIRE(
      core.generalRegister(2) ==
      EERegister128{
        UINT64_C(0x1111111189abcdef),
        UINT64_C(0x2222222233333333)
      });
    REQUIRE(
      core.floatingPointRegister(3) ==
      UINT32_C(0x76543210));
    REQUIRE(
      core.floatingPointRegister(4) ==
      UINT32_C(0x12345678));
    REQUIRE(
      core.floatingPointRegister(5) ==
      UINT32_C(0x11223344));
    REQUIRE(
      core.cop1ControlRegister(31) ==
      EECOP1Control::STATUS_FIXED);
    std::uint32_t stored = 0;
    REQUIRE(system.eeBus().readData32(0, &stored));
    REQUIRE(stored == instruction);
  }
}
