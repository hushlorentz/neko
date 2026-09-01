#include <cstddef>
#include <cstdint>

#include "catch.hpp"
#include "ee_core.hpp"

TEST_CASE("EE Core architectural state")
{
  EECore core;

  SECTION("The EE exposes 32 zero-initialized 128-bit GPRs")
  {
    REQUIRE(EECore::GENERAL_REGISTER_COUNT == 32);
    for (std::size_t index = 0;
         index < EECore::GENERAL_REGISTER_COUNT;
         ++index)
    {
      REQUIRE(core.generalRegister(index).low == 0);
      REQUIRE(core.generalRegister(index).high == 0);
    }
  }

  SECTION("GPRs preserve all 128 bits")
  {
    const EERegister128 value = {
      UINT64_C(0x0123456789abcdef),
      UINT64_C(0xfedcba9876543210)
    };

    core.setGeneralRegister(1, value);

    REQUIRE(core.generalRegister(1) == value);
  }

  SECTION("Writes to GPR zero are ignored")
  {
    core.setGeneralRegister(
      0,
      {
        UINT64_C(0xffffffffffffffff),
        UINT64_C(0xffffffffffffffff)
      });

    REQUIRE(core.generalRegister(0) == EERegister128{});
  }

  SECTION("GPR indices are checked")
  {
    REQUIRE_THROWS(
      core.generalRegister(EECore::GENERAL_REGISTER_COUNT));
    REQUIRE_THROWS(
      core.setGeneralRegister(
        EECore::GENERAL_REGISTER_COUNT,
        {}));
  }

  SECTION("PC and integer special registers are independent")
  {
    core.setProgramCounter(0x81234560);
    core.setHI(UINT64_C(0x1111111122222222));
    core.setLO(UINT64_C(0x3333333344444444));
    core.setHI1(UINT64_C(0x5555555566666666));
    core.setLO1(UINT64_C(0x7777777788888888));
    core.setShiftAmount(0x99);

    REQUIRE(core.programCounter() == 0x81234560);
    REQUIRE(core.hi() == UINT64_C(0x1111111122222222));
    REQUIRE(core.lo() == UINT64_C(0x3333333344444444));
    REQUIRE(core.hi1() == UINT64_C(0x5555555566666666));
    REQUIRE(core.lo1() == UINT64_C(0x7777777788888888));
    REQUIRE(core.shiftAmount() == 0x99);
  }

  SECTION("Reset restores deterministic architectural state")
  {
    core.setGeneralRegister(
      31,
      {UINT64_C(0xaaaaaaaaaaaaaaaa),
       UINT64_C(0xbbbbbbbbbbbbbbbb)});
    core.setProgramCounter(0x12345678);
    core.setHI(1);
    core.setLO(2);
    core.setHI1(3);
    core.setLO1(4);
    core.setShiftAmount(5);

    core.reset();

    for (std::size_t index = 0;
         index < EECore::GENERAL_REGISTER_COUNT;
         ++index)
    {
      REQUIRE(core.generalRegister(index) == EERegister128{});
    }
    REQUIRE(core.programCounter() == 0);
    REQUIRE(core.hi() == 0);
    REQUIRE(core.lo() == 0);
    REQUIRE(core.hi1() == 0);
    REQUIRE(core.lo1() == 0);
    REQUIRE(core.shiftAmount() == 0);
  }
}
