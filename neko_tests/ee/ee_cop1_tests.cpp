#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "ee_core.hpp"
#include "ee_instruction.hpp"
#include "floating_point_ops.hpp"
#include "neko_system.hpp"

namespace
{
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
    {0, 0, UINT32_C(0x7fffffff), FP_FLAG_I_BIT},
    {0, FP_SIGN_BIT, UINT32_C(0xffffffff), FP_FLAG_I_BIT},
    {FP_SIGN_BIT, 0, UINT32_C(0xffffffff), FP_FLAG_I_BIT},
    {
      FP_SIGN_BIT,
      FP_SIGN_BIT,
      UINT32_C(0x7fffffff),
      FP_FLAG_I_BIT
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
    "Unsupported EE instruction encoding.");
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
  "EE COP1 movement selection and conversion results have ordered visibility")
{
  struct VisibilityVector
  {
    std::uint32_t instruction;
    std::uint32_t fs;
    std::uint32_t ft;
    std::uint32_t expected;
  };
  const VisibilityVector vectors[] = {
    {
      cop1SingleInstruction(0x05, 2, 4),
      UINT32_C(0xffc12345),
      0,
      UINT32_C(0x7fc12345)
    },
    {
      cop1SingleInstruction(0x06, 2, 4),
      UINT32_C(0x89abcdef),
      0,
      UINT32_C(0x89abcdef)
    },
    {
      cop1SingleInstruction(0x07, 2, 4),
      UINT32_C(0x7fc12345),
      0,
      UINT32_C(0xffc12345)
    },
    {
      cop1SingleInstruction(0x28, 2, 4, 3),
      UINT32_C(0x3f800000),
      UINT32_C(0x40000000),
      UINT32_C(0x40000000)
    },
    {
      cop1SingleInstruction(0x29, 2, 4, 3),
      UINT32_C(0x3f800000),
      UINT32_C(0x40000000),
      UINT32_C(0x3f800000)
    },
    {
      cop1WordInstruction(0x20, 2, 4),
      UINT32_C(0x01000001),
      0,
      UINT32_C(0x4b800000)
    },
    {
      cop1SingleInstruction(0x24, 2, 4),
      UINT32_C(0x3ff33333),
      0,
      UINT32_C(0x00000001)
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
  "EE LWC1 interlocks every completed COP1 movement selection and conversion")
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

TEST_CASE("EE COP1 transfers require Status CU1")
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
    cop1SingleInstruction(0x28, 3, 4, 5),
    cop1SingleInstruction(0x29, 3, 4, 5),
    cop1WordInstruction(0x20, 3, 4),
    cop1SingleInstruction(0x24, 3, 4)
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
