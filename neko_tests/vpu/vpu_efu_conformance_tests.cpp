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
}
