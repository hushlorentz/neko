#include <array>
#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "floating_point_ops.hpp"
#include "vpu.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"

namespace
{
  constexpr std::uint32_t VU_FLOAT_POSITIVE_ZERO_BITS = 0x00000000;
  constexpr std::uint32_t VU_FLOAT_NEGATIVE_ZERO_BITS = 0x80000000;
  constexpr std::uint32_t VU_FLOAT_POSITIVE_DENORMAL_BITS = 0x007fffff;
  constexpr std::uint32_t VU_FLOAT_NEGATIVE_DENORMAL_BITS = 0x807fffff;
  constexpr std::uint32_t VU_FLOAT_MAX_BITS = 0x7fffffff;
  constexpr std::uint32_t VU_FLOAT_NEGATIVE_MAX_BITS = 0xffffffff;
  constexpr std::uint32_t PI_OVER_TWO_BITS = 0x3fc90fdb;
  constexpr std::uint32_t NEGATIVE_PI_OVER_TWO_BITS = 0xbfc90fdb;

  struct EFUVector
  {
    const char *name;
    std::array<std::uint32_t, 4> source;
    std::uint32_t expected;
  };

  struct ScalarEFUSelection
  {
    const char *name;
    std::uint32_t encoding;
    std::array<std::uint32_t, 4> source;
    std::array<std::uint32_t, 4> expected;
  };

  struct EFUSourceHazard
  {
    const char *name;
    std::uint32_t instruction;
    std::uint32_t usedLane;
    std::uint32_t unusedLane;
  };

  void appendWord(
    std::vector<std::uint8_t> *bytes,
    std::uint32_t word)
  {
    bytes->push_back(word & 0xff);
    bytes->push_back((word >> 8) & 0xff);
    bytes->push_back((word >> 16) & 0xff);
    bytes->push_back((word >> 24) & 0xff);
  }

  void appendPair(
    std::vector<std::uint8_t> *instructions,
    std::uint32_t upper,
    std::uint32_t lower)
  {
    appendWord(instructions, lower);
    appendWord(instructions, upper);
  }

  std::uint32_t vectorEFU(
    std::uint32_t encoding,
    std::uint8_t source)
  {
    return
      encoding |
      (static_cast<std::uint32_t>(source) << VPU_FS_REG_SHIFT);
  }

  std::uint32_t scalarEFU(
    std::uint32_t encoding,
    std::uint8_t source,
    std::uint8_t field)
  {
    return
      encoding |
      (static_cast<std::uint32_t>(field) << 21) |
      (static_cast<std::uint32_t>(source) << VPU_FS_REG_SHIFT);
  }

  std::uint32_t runEFU(
    std::uint32_t instruction,
    const std::array<std::uint32_t, 4> &source)
  {
    VPU vpu(VPUType::VU1);
    std::vector<std::uint8_t> instructions;
    vpu.loadFPRegisterBits(
      VPU_REGISTER_VF01,
      source[0],
      source[1],
      source[2],
      source[3]);
    appendPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      instruction);
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();
    return vpu.pRegisterBits();
  }

  std::uint32_t upperAddToVF01(std::uint32_t destinationLane)
  {
    return
      destinationLane |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF02) <<
       VPU_FT_REG_SHIFT) |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF03) <<
       VPU_FS_REG_SHIFT) |
      (static_cast<std::uint32_t>(VPU_REGISTER_VF01) <<
       VPU_FD_REG_SHIFT) |
      VPU_ADD;
  }

  bool sourceWriteStallsEFU(
    std::uint32_t instruction,
    std::uint32_t destinationLane)
  {
    VPU vpu(VPUType::VU1);
    std::vector<std::uint8_t> instructions;
    std::vector<VPUTraceEvent> events;
    vpu.loadFPRegister(VPU_REGISTER_VF01, 0.5, 0.5, 0.5, 0.5);
    vpu.loadFPRegister(VPU_REGISTER_VF02, 0.25, 0.25, 0.25, 0.25);
    vpu.loadFPRegister(VPU_REGISTER_VF03, 0.25, 0.25, 0.25, 0.25);
    appendPair(
      &instructions,
      upperAddToVF01(destinationLane),
      VPU_LOWER_NOP);
    appendPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      instruction);
    appendPair(&instructions, VPU_NOP, VPU_LOWER_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });
    vpu.initMicroMode();

    for (const VPUTraceEvent &event : events)
    {
      if (event.type == VPUTraceEventType::PipelineStall)
      {
        return true;
      }
    }
    return false;
  }
}

TEST_CASE("VU1 EFU source selection and hazard conformance")
{
  SECTION("Every scalar operation reads each encoded source lane")
  {
    const std::array<ScalarEFUSelection, 6> operations{{
      {
        "ESQRT",
        VPU_ESQRT_ENCODING,
        {0x40000000, 0x41200000, 0x3f000001, 0x41100000},
        {0x3fb504f3, 0x404a62c1, 0x3f3504f3, 0x40400000}
      },
      {
        "ERSQRT",
        VPU_ERSQRT_ENCODING,
        {0x40000000, 0x41200000, 0x3f000001, 0x41800000},
        {0x3f3504f3, 0x3ea1e89b, 0x3fb504f3, 0x3e800000}
      },
      {
        "ERCPR",
        VPU_ERCPR_ENCODING,
        {0x40400000, 0xc1200000, 0x3f000001, 0x40000000},
        {0x3eaaaaaa, 0xbdcccccc, 0x3ffffffe, 0x3f000000}
      },
      {
        "ESIN",
        VPU_ESIN_ENCODING,
        {0x3e800000, 0xbf400000, 0x3fa00000, 0x3f000000},
        {0x3e7d5776, 0xbf2e7fdf, 0x3f72f0a7, 0x3ef57742}
      },
      {
        "EEXP",
        VPU_EEXP_ENCODING,
        {0x3e800000, 0x3fc00000, 0x40800000, 0x3f800000},
        {0x3f475f84, 0x3e647c55, 0x3c960b33, 0x3ebc5abf}
      },
      {
        "EATAN",
        VPU_EATAN_ENCODING,
        {0x3e800000, 0x3f000000, 0x3f400000, 0x3f800000},
        {0x3e7adbbf, 0x3eed633c, 0x3f24bc7e, 0x3f490fdb}
      }
    }};

    for (const ScalarEFUSelection &operation : operations)
    {
      for (std::uint8_t field = 0; field < 4; field++)
      {
        CAPTURE(operation.name);
        CAPTURE(field);
        REQUIRE(
          runEFU(
            scalarEFU(
              operation.encoding,
              VPU_REGISTER_VF01,
              field),
            operation.source) ==
          operation.expected[field]);
      }
    }
  }

  SECTION("Every operation stalls only for lanes in its source mask")
  {
    const std::array<EFUSourceHazard, 13> operations{{
      {
        "ESUM",
        vectorEFU(VPU_ESUM_ENCODING, VPU_REGISTER_VF01),
        VPU_DEST_W_BIT,
        0
      },
      {
        "ESADD",
        vectorEFU(VPU_ESADD_ENCODING, VPU_REGISTER_VF01),
        VPU_DEST_X_BIT,
        VPU_DEST_W_BIT
      },
      {
        "ELENG",
        vectorEFU(VPU_ELENG_ENCODING, VPU_REGISTER_VF01),
        VPU_DEST_Y_BIT,
        VPU_DEST_W_BIT
      },
      {
        "ERSADD",
        vectorEFU(VPU_ERSADD_ENCODING, VPU_REGISTER_VF01),
        VPU_DEST_Z_BIT,
        VPU_DEST_W_BIT
      },
      {
        "ERLENG",
        vectorEFU(VPU_ERLENG_ENCODING, VPU_REGISTER_VF01),
        VPU_DEST_X_BIT,
        VPU_DEST_W_BIT
      },
      {
        "EATANxy",
        vectorEFU(VPU_EATANXY_ENCODING, VPU_REGISTER_VF01),
        VPU_DEST_Y_BIT,
        VPU_DEST_Z_BIT
      },
      {
        "EATANxz",
        vectorEFU(VPU_EATANXZ_ENCODING, VPU_REGISTER_VF01),
        VPU_DEST_Z_BIT,
        VPU_DEST_Y_BIT
      },
      {
        "ESQRT",
        scalarEFU(VPU_ESQRT_ENCODING, VPU_REGISTER_VF01, 0),
        VPU_DEST_X_BIT,
        VPU_DEST_Y_BIT
      },
      {
        "ERSQRT",
        scalarEFU(VPU_ERSQRT_ENCODING, VPU_REGISTER_VF01, 1),
        VPU_DEST_Y_BIT,
        VPU_DEST_X_BIT
      },
      {
        "ERCPR",
        scalarEFU(VPU_ERCPR_ENCODING, VPU_REGISTER_VF01, 2),
        VPU_DEST_Z_BIT,
        VPU_DEST_W_BIT
      },
      {
        "ESIN",
        scalarEFU(VPU_ESIN_ENCODING, VPU_REGISTER_VF01, 3),
        VPU_DEST_W_BIT,
        VPU_DEST_X_BIT
      },
      {
        "EEXP",
        scalarEFU(VPU_EEXP_ENCODING, VPU_REGISTER_VF01, 0),
        VPU_DEST_X_BIT,
        VPU_DEST_Y_BIT
      },
      {
        "EATAN",
        scalarEFU(VPU_EATAN_ENCODING, VPU_REGISTER_VF01, 1),
        VPU_DEST_Y_BIT,
        VPU_DEST_Z_BIT
      }
    }};

    for (const EFUSourceHazard &operation : operations)
    {
      CAPTURE(operation.name);
      REQUIRE(
        sourceWriteStallsEFU(
          operation.instruction,
          operation.usedLane));
      if (operation.unusedLane != 0)
      {
        REQUIRE_FALSE(
          sourceWriteStallsEFU(
            operation.instruction,
            operation.unusedLane));
      }
    }
  }
}

TEST_CASE("VU1 EFU boundary and exceptional conformance")
{
  SECTION("Polynomial operations preserve their documented domain endpoints")
  {
    REQUIRE(
      runEFU(
        scalarEFU(VPU_ESIN_ENCODING, VPU_REGISTER_VF01, 0),
        {PI_OVER_TWO_BITS, 0, 0, 0}) ==
      VU_FLOAT_ONE_BITS);
    REQUIRE(
      runEFU(
        scalarEFU(VPU_ESIN_ENCODING, VPU_REGISTER_VF01, 0),
        {NEGATIVE_PI_OVER_TWO_BITS, 0, 0, 0}) ==
      (FP_SIGN_BIT | VU_FLOAT_ONE_BITS));
    REQUIRE(
      runEFU(
        scalarEFU(VPU_EEXP_ENCODING, VPU_REGISTER_VF01, 0),
        {VU_FLOAT_POSITIVE_ZERO_BITS, 0, 0, 0}) ==
      VU_FLOAT_ONE_BITS);
    REQUIRE(
      runEFU(
        scalarEFU(VPU_EEXP_ENCODING, VPU_REGISTER_VF01, 0),
        {VU_FLOAT_MAX_BITS, 0, 0, 0}) ==
      VU_FLOAT_POSITIVE_ZERO_BITS);
  }

  SECTION("Arctangent operations preserve their documented domain endpoints")
  {
    constexpr std::uint32_t EATAN_ZERO_APPROXIMATION_BITS = 0x34738000;
    constexpr std::uint32_t PI_OVER_FOUR_BITS = 0x3f490fdb;

    REQUIRE(
      runEFU(
        scalarEFU(VPU_EATAN_ENCODING, VPU_REGISTER_VF01, 0),
        {VU_FLOAT_POSITIVE_ZERO_BITS, 0, 0, 0}) ==
      EATAN_ZERO_APPROXIMATION_BITS);
    REQUIRE(
      runEFU(
        scalarEFU(VPU_EATAN_ENCODING, VPU_REGISTER_VF01, 0),
        {VU_FLOAT_ONE_BITS, 0, 0, 0}) ==
      PI_OVER_FOUR_BITS);
    REQUIRE(
      runEFU(
        vectorEFU(VPU_EATANXY_ENCODING, VPU_REGISTER_VF01),
        {VU_FLOAT_ONE_BITS, VU_FLOAT_POSITIVE_ZERO_BITS, 0, 0}) ==
      EATAN_ZERO_APPROXIMATION_BITS);
    REQUIRE(
      runEFU(
        vectorEFU(VPU_EATANXY_ENCODING, VPU_REGISTER_VF01),
        {VU_FLOAT_ONE_BITS, VU_FLOAT_ONE_BITS, 0, 0}) ==
      PI_OVER_FOUR_BITS);
    REQUIRE(
      runEFU(
        vectorEFU(VPU_EATANXZ_ENCODING, VPU_REGISTER_VF01),
        {VU_FLOAT_ONE_BITS, 0, VU_FLOAT_POSITIVE_ZERO_BITS, 0}) ==
      EATAN_ZERO_APPROXIMATION_BITS);
    REQUIRE(
      runEFU(
        vectorEFU(VPU_EATANXZ_ENCODING, VPU_REGISTER_VF01),
        {VU_FLOAT_ONE_BITS, 0, VU_FLOAT_ONE_BITS, 0}) ==
      PI_OVER_FOUR_BITS);
  }

  SECTION("Signed zero and denormals follow VU zero semantics")
  {
    REQUIRE(
      runEFU(
        vectorEFU(VPU_ESUM_ENCODING, VPU_REGISTER_VF01),
        {
          VU_FLOAT_NEGATIVE_ZERO_BITS,
          VU_FLOAT_NEGATIVE_ZERO_BITS,
          VU_FLOAT_NEGATIVE_ZERO_BITS,
          VU_FLOAT_NEGATIVE_ZERO_BITS
        }) ==
      VU_FLOAT_NEGATIVE_ZERO_BITS);
    REQUIRE(
      runEFU(
        vectorEFU(VPU_ESUM_ENCODING, VPU_REGISTER_VF01),
        {
          VU_FLOAT_POSITIVE_ZERO_BITS,
          VU_FLOAT_NEGATIVE_ZERO_BITS,
          VU_FLOAT_POSITIVE_DENORMAL_BITS,
          VU_FLOAT_NEGATIVE_DENORMAL_BITS
        }) ==
      VU_FLOAT_POSITIVE_ZERO_BITS);
    REQUIRE(
      runEFU(
        vectorEFU(VPU_ESADD_ENCODING, VPU_REGISTER_VF01),
        {
          VU_FLOAT_NEGATIVE_ZERO_BITS,
          VU_FLOAT_NEGATIVE_DENORMAL_BITS,
          VU_FLOAT_POSITIVE_DENORMAL_BITS,
          VU_FLOAT_MAX_BITS
        }) ==
      VU_FLOAT_POSITIVE_ZERO_BITS);
    REQUIRE(
      runEFU(
        vectorEFU(VPU_ELENG_ENCODING, VPU_REGISTER_VF01),
        {
          VU_FLOAT_POSITIVE_DENORMAL_BITS,
          VU_FLOAT_NEGATIVE_DENORMAL_BITS,
          VU_FLOAT_NEGATIVE_ZERO_BITS,
          VU_FLOAT_MAX_BITS
        }) ==
      VU_FLOAT_POSITIVE_ZERO_BITS);
    REQUIRE(
      runEFU(
        scalarEFU(VPU_ESQRT_ENCODING, VPU_REGISTER_VF01, 0),
        {VU_FLOAT_NEGATIVE_DENORMAL_BITS, 0, 0, 0}) ==
      VU_FLOAT_POSITIVE_ZERO_BITS);
    REQUIRE(
      runEFU(
        scalarEFU(VPU_ESIN_ENCODING, VPU_REGISTER_VF01, 0),
        {VU_FLOAT_NEGATIVE_DENORMAL_BITS, 0, 0, 0}) ==
      VU_FLOAT_POSITIVE_ZERO_BITS);
    REQUIRE(
      runEFU(
        scalarEFU(VPU_EEXP_ENCODING, VPU_REGISTER_VF01, 0),
        {VU_FLOAT_NEGATIVE_DENORMAL_BITS, 0, 0, 0}) ==
      VU_FLOAT_ONE_BITS);
  }

  SECTION("Reciprocal operations preserve signed divide-by-zero results")
  {
    REQUIRE(
      runEFU(
        scalarEFU(VPU_ERCPR_ENCODING, VPU_REGISTER_VF01, 0),
        {VU_FLOAT_POSITIVE_ZERO_BITS, 0, 0, 0}) ==
      VU_FLOAT_MAX_BITS);
    REQUIRE(
      runEFU(
        scalarEFU(VPU_ERCPR_ENCODING, VPU_REGISTER_VF01, 0),
        {VU_FLOAT_NEGATIVE_DENORMAL_BITS, 0, 0, 0}) ==
      VU_FLOAT_NEGATIVE_MAX_BITS);
    REQUIRE(
      runEFU(
        scalarEFU(VPU_ERSQRT_ENCODING, VPU_REGISTER_VF01, 0),
        {VU_FLOAT_POSITIVE_DENORMAL_BITS, 0, 0, 0}) ==
      VU_FLOAT_MAX_BITS);
    REQUIRE(
      runEFU(
        scalarEFU(VPU_ERSQRT_ENCODING, VPU_REGISTER_VF01, 0),
        {VU_FLOAT_NEGATIVE_ZERO_BITS, 0, 0, 0}) ==
      VU_FLOAT_NEGATIVE_MAX_BITS);
    REQUIRE(
      runEFU(
        vectorEFU(VPU_ERSADD_ENCODING, VPU_REGISTER_VF01),
        {
          VU_FLOAT_POSITIVE_DENORMAL_BITS,
          VU_FLOAT_NEGATIVE_DENORMAL_BITS,
          VU_FLOAT_NEGATIVE_ZERO_BITS,
          VU_FLOAT_ONE_BITS
        }) ==
      VU_FLOAT_MAX_BITS);
    REQUIRE(
      runEFU(
        vectorEFU(VPU_ERLENG_ENCODING, VPU_REGISTER_VF01),
        {
          VU_FLOAT_POSITIVE_DENORMAL_BITS,
          VU_FLOAT_NEGATIVE_DENORMAL_BITS,
          VU_FLOAT_NEGATIVE_ZERO_BITS,
          VU_FLOAT_ONE_BITS
        }) ==
      VU_FLOAT_MAX_BITS);
  }

  SECTION("Intermediate overflow saturates before later EFU operations")
  {
    constexpr std::uint32_t SQUARE_ROOT_OF_VU_MAX_BITS = 0x5fb504f2;
    constexpr std::uint32_t RECIPROCAL_SQUARE_ROOT_OF_VU_MAX_BITS =
      0x1f3504f4;

    REQUIRE(
      runEFU(
        vectorEFU(VPU_ESUM_ENCODING, VPU_REGISTER_VF01),
        {VU_FLOAT_MAX_BITS, VU_FLOAT_MAX_BITS, 0, 0}) ==
      VU_FLOAT_MAX_BITS);
    REQUIRE(
      runEFU(
        vectorEFU(VPU_ESADD_ENCODING, VPU_REGISTER_VF01),
        {VU_FLOAT_MAX_BITS, 0, 0, 0}) ==
      VU_FLOAT_MAX_BITS);
    REQUIRE(
      runEFU(
        vectorEFU(VPU_ELENG_ENCODING, VPU_REGISTER_VF01),
        {VU_FLOAT_MAX_BITS, 0, 0, 0}) ==
      SQUARE_ROOT_OF_VU_MAX_BITS);
    REQUIRE(
      runEFU(
        vectorEFU(VPU_ERSADD_ENCODING, VPU_REGISTER_VF01),
        {VU_FLOAT_MAX_BITS, 0, 0, 0}) ==
      VU_FLOAT_POSITIVE_ZERO_BITS);
    REQUIRE(
      runEFU(
        vectorEFU(VPU_ERLENG_ENCODING, VPU_REGISTER_VF01),
        {VU_FLOAT_MAX_BITS, 0, 0, 0}) ==
      RECIPROCAL_SQUARE_ROOT_OF_VU_MAX_BITS);
  }

  SECTION("Negative roots and excluded arctangent inputs are deterministic")
  {
    constexpr std::uint32_t NEGATIVE_NINE_BITS = 0xc1100000;
    constexpr std::uint32_t POSITIVE_THREE_BITS = 0x40400000;
    constexpr std::uint32_t RECIPROCAL_THREE_BITS = 0x3eaaaaaa;
    constexpr std::uint32_t EATAN_INDETERMINATE_BITS = 0x7fc90fd6;

    REQUIRE(
      runEFU(
        scalarEFU(VPU_ESQRT_ENCODING, VPU_REGISTER_VF01, 0),
        {NEGATIVE_NINE_BITS, 0, 0, 0}) ==
      POSITIVE_THREE_BITS);
    REQUIRE(
      runEFU(
        scalarEFU(VPU_ERSQRT_ENCODING, VPU_REGISTER_VF01, 0),
        {NEGATIVE_NINE_BITS, 0, 0, 0}) ==
      RECIPROCAL_THREE_BITS);
    REQUIRE(
      runEFU(
        vectorEFU(VPU_EATANXY_ENCODING, VPU_REGISTER_VF01),
        {
          VU_FLOAT_POSITIVE_ZERO_BITS,
          VU_FLOAT_POSITIVE_ZERO_BITS,
          0,
          0
        }) ==
      EATAN_INDETERMINATE_BITS);
    REQUIRE(
      runEFU(
        vectorEFU(VPU_EATANXZ_ENCODING, VPU_REGISTER_VF01),
        {
          VU_FLOAT_POSITIVE_ZERO_BITS,
          0,
          VU_FLOAT_POSITIVE_ZERO_BITS,
          0
        }) ==
      EATAN_INDETERMINATE_BITS);
  }
}

TEST_CASE("VU1 EFU fixed bit-level conformance")
{
  SECTION("ESUM follows the documented x plus y plus z plus w order")
  {
    const std::array<EFUVector, 3> vectors{{
      {
        "positive integers",
        {0x3f800000, 0x40000000, 0x40400000, 0x40800000},
        0x41200000
      },
      {
        "fractional mixed signs",
        {0x3f800001, 0x3eaaaaab, 0xbf000001, 0x3dcccccd},
        0x3f6eeeee
      },
      {
        "order-sensitive cancellation",
        {0x4b000001, 0x3f800000, 0xcb000000, 0x3f000000},
        0x40200000
      }
    }};

    for (const EFUVector &vector : vectors)
    {
      CAPTURE(vector.name);
      REQUIRE(
        runEFU(
          vectorEFU(VPU_ESUM_ENCODING, VPU_REGISTER_VF01),
          vector.source) ==
        vector.expected);
    }
  }

  SECTION("ESADD follows the documented ordered XYZ square sum")
  {
    const std::array<EFUVector, 3> vectors{{
      {
        "fractional mixed signs and ignored W",
        {0x3f8ccccd, 0xc0133333, 0x3f400000, 0x4479c000},
        0x40e1ffff
      },
      {
        "non-exact squared operands",
        {0x3f000001, 0xbeaaaaab, 0x40000001, 0x00000000},
        0x408b8e3b
      },
      {
        "mixed magnitudes",
        {0x41200000, 0xc0a00000, 0x3e800000, 0xbf800000},
        0x42fa2000
      }
    }};

    for (const EFUVector &vector : vectors)
    {
      CAPTURE(vector.name);
      REQUIRE(
        runEFU(
          vectorEFU(VPU_ESADD_ENCODING, VPU_REGISTER_VF01),
          vector.source) ==
        vector.expected);
    }
  }

  SECTION("ESQRT truncates representative irrational roots")
  {
    const std::array<EFUVector, 3> vectors{{
      {"square root of two", {0x40000000, 0, 0, 0}, 0x3fb504f3},
      {"square root of ten", {0x41200000, 0, 0, 0}, 0x404a62c1},
      {
        "square root just above one half",
        {0x3f000001, 0, 0, 0},
        0x3f3504f3
      }
    }};

    for (const EFUVector &vector : vectors)
    {
      CAPTURE(vector.name);
      REQUIRE(
        runEFU(
          scalarEFU(VPU_ESQRT_ENCODING, VPU_REGISTER_VF01, 0),
          vector.source) ==
        vector.expected);
    }
  }

  SECTION("ERSQRT uses the truncated root as its divisor")
  {
    const std::array<EFUVector, 3> vectors{{
      {
        "reciprocal square root of two",
        {0x40000000, 0, 0, 0},
        0x3f3504f3
      },
      {
        "reciprocal square root of ten",
        {0x41200000, 0, 0, 0},
        0x3ea1e89b
      },
      {
        "reciprocal square root just above one half",
        {0x3f000001, 0, 0, 0},
        0x3fb504f3
      }
    }};

    for (const EFUVector &vector : vectors)
    {
      CAPTURE(vector.name);
      REQUIRE(
        runEFU(
          scalarEFU(VPU_ERSQRT_ENCODING, VPU_REGISTER_VF01, 0),
          vector.source) ==
        vector.expected);
    }
  }

  SECTION("ELENG takes the root of the ordered XYZ square sum")
  {
    const std::array<EFUVector, 3> vectors{{
      {
        "fractional mixed signs and ignored W",
        {0x3f8ccccd, 0xc0133333, 0x3f400000, 0x4479c000},
        0x402a1513
      },
      {
        "non-exact squared operands",
        {0x3f000001, 0xbeaaaaab, 0x40000001, 0x00000000},
        0x4005a728
      },
      {
        "mixed magnitudes",
        {0x41200000, 0xc0a00000, 0x3e800000, 0xbf800000},
        0x4132ee1e
      }
    }};

    for (const EFUVector &vector : vectors)
    {
      CAPTURE(vector.name);
      REQUIRE(
        runEFU(
          vectorEFU(VPU_ELENG_ENCODING, VPU_REGISTER_VF01),
          vector.source) ==
        vector.expected);
    }
  }

  SECTION("ERCPR divides one by the selected lane")
  {
    const std::array<EFUVector, 3> vectors{{
      {"reciprocal of three", {0x40400000, 0, 0, 0}, 0x3eaaaaaa},
      {"reciprocal of negative ten", {0xc1200000, 0, 0, 0}, 0xbdcccccc},
      {
        "reciprocal just above one half",
        {0x3f000001, 0, 0, 0},
        0x3ffffffe
      }
    }};

    for (const EFUVector &vector : vectors)
    {
      CAPTURE(vector.name);
      REQUIRE(
        runEFU(
          scalarEFU(VPU_ERCPR_ENCODING, VPU_REGISTER_VF01, 0),
          vector.source) ==
        vector.expected);
    }
  }

  SECTION("ERSADD reciprocates the ordered XYZ square sum")
  {
    const std::array<EFUVector, 3> vectors{{
      {
        "fractional mixed signs and ignored W",
        {0x3f8ccccd, 0xc0133333, 0x3f400000, 0x4479c000},
        0x3e10fdbc
      },
      {
        "non-exact squared operands",
        {0x3f000001, 0xbeaaaaab, 0x40000001, 0x00000000},
        0x3e6acd70
      },
      {
        "mixed magnitudes",
        {0x41200000, 0xc0a00000, 0x3e800000, 0xbf800000},
        0x3c0301a9
      }
    }};

    for (const EFUVector &vector : vectors)
    {
      CAPTURE(vector.name);
      REQUIRE(
        runEFU(
          vectorEFU(VPU_ERSADD_ENCODING, VPU_REGISTER_VF01),
          vector.source) ==
        vector.expected);
    }
  }

  SECTION("ERLENG reciprocates the truncated XYZ length")
  {
    const std::array<EFUVector, 3> vectors{{
      {
        "fractional mixed signs and ignored W",
        {0x3f8ccccd, 0xc0133333, 0x3f400000, 0x4479c000},
        0x3ec0a8de
      },
      {
        "non-exact squared operands",
        {0x3f000001, 0xbeaaaaab, 0x40000001, 0x00000000},
        0x3ef52c1a
      },
      {
        "mixed magnitudes",
        {0x41200000, 0xc0a00000, 0x3e800000, 0xbf800000},
        0x3db72207
      }
    }};

    for (const EFUVector &vector : vectors)
    {
      CAPTURE(vector.name);
      REQUIRE(
        runEFU(
          vectorEFU(VPU_ERLENG_ENCODING, VPU_REGISTER_VF01),
          vector.source) ==
        vector.expected);
    }
  }

  SECTION("ESIN follows the documented odd polynomial")
  {
    const std::array<EFUVector, 3> vectors{{
      {"positive one quarter", {0x3e800000, 0, 0, 0}, 0x3e7d5776},
      {"negative three quarters", {0xbf400000, 0, 0, 0}, 0xbf2e7fdf},
      {"positive five quarters", {0x3fa00000, 0, 0, 0}, 0x3f72f0a7}
    }};

    for (const EFUVector &vector : vectors)
    {
      CAPTURE(vector.name);
      REQUIRE(
        runEFU(
          scalarEFU(VPU_ESIN_ENCODING, VPU_REGISTER_VF01, 0),
          vector.source) ==
        vector.expected);
    }
  }

  SECTION("EEXP follows the documented exp negative x approximation")
  {
    const std::array<EFUVector, 3> vectors{{
      {"positive one quarter", {0x3e800000, 0, 0, 0}, 0x3f475f84},
      {"positive one and one half", {0x3fc00000, 0, 0, 0}, 0x3e647c55},
      {"positive four", {0x40800000, 0, 0, 0}, 0x3c960b33}
    }};

    for (const EFUVector &vector : vectors)
    {
      CAPTURE(vector.name);
      REQUIRE(
        runEFU(
          scalarEFU(VPU_EEXP_ENCODING, VPU_REGISTER_VF01, 0),
          vector.source) ==
        vector.expected);
    }
  }

  SECTION("EATAN transforms its scalar input before the odd polynomial")
  {
    const std::array<EFUVector, 3> vectors{{
      {"arctangent of one quarter", {0x3e800000, 0, 0, 0}, 0x3e7adbbf},
      {"arctangent of one half", {0x3f000000, 0, 0, 0}, 0x3eed633c},
      {
        "arctangent of three quarters",
        {0x3f400000, 0, 0, 0},
        0x3f24bc7e
      }
    }};

    for (const EFUVector &vector : vectors)
    {
      CAPTURE(vector.name);
      REQUIRE(
        runEFU(
          scalarEFU(VPU_EATAN_ENCODING, VPU_REGISTER_VF01, 0),
          vector.source) ==
        vector.expected);
    }
  }

  SECTION("EATANxy transforms the Y to X ratio and ignores ZW")
  {
    const std::array<EFUVector, 3> vectors{{
      {
        "one over three with populated ZW",
        {0x40400000, 0x3f800000, 0x4479c000, 0xc479c000},
        0x3ea4bc82
      },
      {
        "two and one quarter over five and one half",
        {0x40b00000, 0x40100000, 0xc2c80000, 0x42c80000},
        0x3ec6d1bb
      },
      {
        "seven over ten",
        {0x41200000, 0x40e00000, 0x3e800000, 0xbe800000},
        0x3f1c588a
      }
    }};

    for (const EFUVector &vector : vectors)
    {
      CAPTURE(vector.name);
      REQUIRE(
        runEFU(
          vectorEFU(VPU_EATANXY_ENCODING, VPU_REGISTER_VF01),
          vector.source) ==
        vector.expected);
    }
  }

  SECTION("EATANxz transforms the Z to X ratio and ignores YW")
  {
    const std::array<EFUVector, 3> vectors{{
      {
        "one over three with populated YW",
        {0x40400000, 0x4479c000, 0x3f800000, 0xc479c000},
        0x3ea4bc82
      },
      {
        "two and one quarter over five and one half",
        {0x40b00000, 0xc2c80000, 0x40100000, 0x42c80000},
        0x3ec6d1bb
      },
      {
        "seven over ten",
        {0x41200000, 0x3e800000, 0x40e00000, 0xbe800000},
        0x3f1c588a
      }
    }};

    for (const EFUVector &vector : vectors)
    {
      CAPTURE(vector.name);
      REQUIRE(
        runEFU(
          vectorEFU(VPU_EATANXZ_ENCODING, VPU_REGISTER_VF01),
          vector.source) ==
        vector.expected);
    }
  }
}
