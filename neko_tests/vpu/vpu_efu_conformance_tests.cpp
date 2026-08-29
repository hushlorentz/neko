#include <array>
#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"

namespace
{
  struct EFUVector
  {
    const char *name;
    std::array<std::uint32_t, 4> source;
    std::uint32_t expected;
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
