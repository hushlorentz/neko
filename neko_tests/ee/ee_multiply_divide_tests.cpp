#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "ee_core.hpp"
#include "neko_system.hpp"

namespace
{
  std::uint32_t registerInstruction(
    std::uint8_t function,
    std::uint8_t rs,
    std::uint8_t rt,
    std::uint8_t rd,
    std::uint8_t shiftAmount = 0)
  {
    return
      (static_cast<std::uint32_t>(rs) << 21) |
      (static_cast<std::uint32_t>(rt) << 16) |
      (static_cast<std::uint32_t>(rd) << 11) |
      (static_cast<std::uint32_t>(shiftAmount) << 6) |
      function;
  }

  std::uint32_t mmiInstruction(
    std::uint8_t function,
    std::uint8_t rs,
    std::uint8_t rt,
    std::uint8_t rd,
    std::uint8_t nestedFunction = 0)
  {
    return
      UINT32_C(0x70000000) |
      registerInstruction(
        function,
        rs,
        rt,
        rd,
        nestedFunction);
  }

  std::uint32_t regimmInstruction(
    std::uint8_t rt,
    std::uint8_t rs,
    std::uint16_t immediate)
  {
    return
      UINT32_C(0x04000000) |
      (static_cast<std::uint32_t>(rs) << 21) |
      (static_cast<std::uint32_t>(rt) << 16) |
      immediate;
  }

  void setWord(
    EECore *core,
    std::uint8_t index,
    std::uint32_t value)
  {
    const std::uint64_t extended =
      (value & UINT32_C(0x80000000)) != 0
        ? UINT64_C(0xffffffff00000000) | value
        : value;
    core->setGeneralRegister(
      index,
      {extended, UINT64_C(0xfeedfacecafebeef)});
  }

  void runToCompletion(
    NekoSystem *system,
    std::uint32_t instruction,
    std::uint64_t latency)
  {
    system->eeBus().write32(0, instruction);
    system->eeCore().startExecution(0);
    system->clockMasterCycle();
    system->runMasterCycles(latency);
  }
}

TEST_CASE("EE multiply and multiply-add execution")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  SECTION("Signed multiply writes rd and HI LO after four cycles")
  {
    setWord(&core, 1, UINT32_C(0xfffffffe));
    setWord(&core, 2, 3);
    core.setHI(0x1111);
    core.setLO(0x2222);
    core.setGeneralRegister(
      3,
      {0x3333, UINT64_C(0xfeedfacecafebeef)});
    system.eeBus().write32(
      0,
      registerInstruction(0x18, 1, 2, 3));
    system.eeBus().write32(
      4,
      registerInstruction(0x12, 0, 0, 4));
    core.startExecution(0);

    system.clockMasterCycle();
    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.hi() == 0x1111);
    REQUIRE(core.lo() == 0x2222);
    REQUIRE(core.generalRegister(3).low == 0x3333);

    system.runMasterCycles(3);
    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.lo() == 0x2222);

    system.clockMasterCycle();
    REQUIRE(core.programCounter() == 8);
    REQUIRE(core.hi() == UINT64_MAX);
    REQUIRE(core.lo() == UINT64_C(0xfffffffffffffffa));
    REQUIRE(
      core.generalRegister(3).low ==
      UINT64_C(0xfffffffffffffffa));
    REQUIRE(
      core.generalRegister(3).high ==
      UINT64_C(0xfeedfacecafebeef));
    REQUIRE(
      core.generalRegister(4).low ==
      UINT64_C(0xfffffffffffffffa));
  }

  SECTION("Unsigned multiply sign extends each result word")
  {
    setWord(&core, 1, UINT32_MAX);
    setWord(&core, 2, 2);

    runToCompletion(
      &system,
      registerInstruction(0x19, 1, 2, 3),
      4);

    REQUIRE(core.hi() == 1);
    REQUIRE(core.lo() == UINT64_C(0xfffffffffffffffe));
    REQUIRE(
      core.generalRegister(3).low ==
      UINT64_C(0xfffffffffffffffe));
  }

  SECTION("Multiply-add accumulates the low words of HI and LO")
  {
    setWord(&core, 1, 2);
    setWord(&core, 2, 3);
    core.setHI(UINT64_C(0xffffffff00000000));
    core.setLO(UINT64_C(0x1234567800000005));

    runToCompletion(
      &system,
      mmiInstruction(0x00, 1, 2, 3),
      4);

    REQUIRE(core.hi() == 0);
    REQUIRE(core.lo() == 11);
    REQUIRE(core.generalRegister(3).low == 11);
  }

  SECTION("Pipeline 1 has independent HI1 and LO1 state")
  {
    setWord(&core, 1, 4);
    setWord(&core, 2, 5);
    core.setHI(0x1111);
    core.setLO(0x2222);

    runToCompletion(
      &system,
      mmiInstruction(0x18, 1, 2, 3),
      4);

    REQUIRE(core.hi() == 0x1111);
    REQUIRE(core.lo() == 0x2222);
    REQUIRE(core.hi1() == 0);
    REQUIRE(core.lo1() == 20);
    REQUIRE(core.generalRegister(3).low == 20);
  }
}

TEST_CASE("EE packed word multiply execution")
{
  SECTION("PMULTW commits signed lane products atomically")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      1,
      {UINT64_C(0xfffffffffffffffe),
       UINT64_C(0xffffffff80000000)});
    core.setGeneralRegister(
      2,
      {3, UINT64_MAX});
    core.setGeneralRegister(
      3,
      {UINT64_C(0x1111222233334444),
       UINT64_C(0x5555666677778888)});
    core.setHI(UINT64_C(0x1111));
    core.setLO(UINT64_C(0x2222));
    core.setHI1(UINT64_C(0x3333));
    core.setLO1(UINT64_C(0x4444));
    system.eeBus().write32(
      0,
      mmiInstruction(0x09, 1, 2, 3, 0x0c));
    core.startExecution(0);

    system.clockMasterCycle();
    system.runMasterCycles(3);
    REQUIRE(core.hi() == UINT64_C(0x1111));
    REQUIRE(core.lo() == UINT64_C(0x2222));
    REQUIRE(core.hi1() == UINT64_C(0x3333));
    REQUIRE(core.lo1() == UINT64_C(0x4444));
    REQUIRE(
      core.generalRegister(3) ==
      EERegister128{UINT64_C(0x1111222233334444),
                    UINT64_C(0x5555666677778888)});

    system.clockMasterCycle();
    REQUIRE(core.hi() == UINT64_MAX);
    REQUIRE(core.lo() == UINT64_C(0xfffffffffffffffa));
    REQUIRE(core.hi1() == 0);
    REQUIRE(core.lo1() == UINT64_C(0xffffffff80000000));
    REQUIRE(
      core.generalRegister(3) ==
      EERegister128{UINT64_C(0xfffffffffffffffa),
                    UINT64_C(0x0000000080000000)});
  }

  SECTION("PMULTUW sign extends HI LO words but preserves full products")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      1,
      {UINT64_MAX, UINT64_C(0xffffffff80000000)});
    core.setGeneralRegister(2, {2, 3});
    system.eeBus().write32(
      0,
      mmiInstruction(0x29, 1, 2, 4, 0x0c));
    core.startExecution(0);

    system.clockMasterCycle();
    system.runMasterCycles(4);

    REQUIRE(core.hi() == 1);
    REQUIRE(core.lo() == UINT64_C(0xfffffffffffffffe));
    REQUIRE(core.hi1() == 1);
    REQUIRE(core.lo1() == UINT64_C(0xffffffff80000000));
    REQUIRE(
      core.generalRegister(4) ==
      EERegister128{UINT64_C(0x00000001fffffffe),
                    UINT64_C(0x0000000180000000)});
  }

  SECTION("A zero destination still commits both HI LO products")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, {4, 5});
    core.setGeneralRegister(2, {6, 7});
    system.eeBus().write32(
      0,
      mmiInstruction(0x09, 1, 2, 0, 0x0c));
    core.startExecution(0);

    system.clockMasterCycle();
    system.runMasterCycles(4);

    REQUIRE(core.lo() == 24);
    REQUIRE(core.lo1() == 35);
    REQUIRE(core.generalRegister(0) == EERegister128{});
  }

  SECTION("Two packed multiplies overlap at the two-cycle interval")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, {2, 3});
    core.setGeneralRegister(2, {4, 5});
    core.setGeneralRegister(5, {6, 7});
    core.setGeneralRegister(6, {8, 9});
    system.eeBus().write32(
      0,
      mmiInstruction(0x09, 1, 2, 3, 0x0c));
    system.eeBus().write32(
      4,
      mmiInstruction(0x29, 5, 6, 4, 0x0c));
    core.startExecution(0);

    system.clockMasterCycle();
    system.clockMasterCycle();
    REQUIRE(core.programCounter() == 4);
    system.clockMasterCycle();
    REQUIRE(core.programCounter() >= 8);

    system.runMasterCycles(2);
    REQUIRE(core.generalRegister(3) == EERegister128{8, 15});
    REQUIRE(core.generalRegister(4) == EERegister128{});

    system.runMasterCycles(2);
    REQUIRE(core.generalRegister(4) == EERegister128{48, 63});
    REQUIRE(core.lo() == 48);
    REQUIRE(core.lo1() == 63);
  }

  SECTION("Packed destinations interlock consumers and writers")
  {
    SECTION("A consumer waits for the full packed result")
    {
      NekoSystem system;
      EECore &core = system.eeCore();
      core.setGeneralRegister(1, {2, 3});
      core.setGeneralRegister(2, {4, 5});
      system.eeBus().write32(
        0,
        mmiInstruction(0x09, 1, 2, 3, 0x0c));
      system.eeBus().write32(
        4,
        registerInstruction(0x21, 3, 0, 7));
      core.startExecution(0);

      system.clockMasterCycle();
      system.runMasterCycles(3);

      REQUIRE(core.programCounter() == 4);
      REQUIRE(core.generalRegister(3) == EERegister128{});
      REQUIRE(core.generalRegister(7) == EERegister128{});

      system.clockMasterCycle();

      REQUIRE(core.generalRegister(3) == EERegister128{8, 15});
      REQUIRE(core.generalRegister(7).low == 8);
    }

    SECTION("A writer waits and follows packed retirement")
    {
      NekoSystem system;
      EECore &core = system.eeCore();
      core.setGeneralRegister(1, {2, 3});
      core.setGeneralRegister(2, {4, 5});
      setWord(&core, 5, 7);
      setWord(&core, 6, 8);
      system.eeBus().write32(
        0,
        mmiInstruction(0x09, 1, 2, 3, 0x0c));
      system.eeBus().write32(
        4,
        registerInstruction(0x21, 5, 6, 3));
      core.startExecution(0);

      system.clockMasterCycle();
      system.runMasterCycles(3);

      REQUIRE(core.programCounter() == 4);
      REQUIRE(core.generalRegister(3) == EERegister128{});

      system.clockMasterCycle();

      REQUIRE(core.generalRegister(3) == EERegister128{15, 15});
    }
  }

  SECTION("Packed HI LO hazards interlock full-width transfers")
  {
    SECTION("A full-width read waits for packed retirement")
    {
      NekoSystem system;
      EECore &core = system.eeCore();
      core.setGeneralRegister(1, {2, 3});
      core.setGeneralRegister(2, {4, 5});
      system.eeBus().write32(
        0,
        mmiInstruction(0x09, 1, 2, 3, 0x0c));
      system.eeBus().write32(
        4,
        mmiInstruction(0x09, 0, 0, 7, 0x09));
      core.startExecution(0);

      system.clockMasterCycle();
      system.runMasterCycles(3);

      REQUIRE(core.programCounter() == 4);
      REQUIRE(core.generalRegister(7) == EERegister128{});

      system.clockMasterCycle();

      REQUIRE(core.generalRegister(7) == EERegister128{8, 15});
    }

    SECTION("A full-width write follows packed retirement")
    {
      NekoSystem system;
      EECore &core = system.eeCore();
      core.setGeneralRegister(1, {2, 3});
      core.setGeneralRegister(2, {4, 5});
      core.setGeneralRegister(5, {11, 12});
      system.eeBus().write32(
        0,
        mmiInstruction(0x09, 1, 2, 3, 0x0c));
      system.eeBus().write32(
        4,
        mmiInstruction(0x29, 5, 0, 0, 0x09));
      core.startExecution(0);

      system.clockMasterCycle();
      system.runMasterCycles(3);

      REQUIRE(core.programCounter() == 4);
      REQUIRE(core.lo() == 0);
      REQUIRE(core.lo1() == 0);

      system.clockMasterCycle();

      REQUIRE(core.generalRegister(3) == EERegister128{8, 15});
      REQUIRE(core.lo() == 11);
      REQUIRE(core.lo1() == 12);
    }
  }

  SECTION("Packed and scalar MAC0 starts retry in either order")
  {
    const auto prepareRegisters =
      [](EECore *core)
      {
        core->setGeneralRegister(1, {2, 3});
        core->setGeneralRegister(2, {4, 5});
        setWord(core, 5, 6);
        setWord(core, 6, 7);
      };

    SECTION("Packed older blocks scalar MAC0")
    {
      NekoSystem system;
      EECore &core = system.eeCore();
      prepareRegisters(&core);
      system.eeBus().write32(
        0,
        mmiInstruction(0x09, 1, 2, 3, 0x0c));
      system.eeBus().write32(
        4,
        registerInstruction(0x18, 5, 6, 7));
      core.startExecution(0);

      system.clockMasterCycle();
      system.runMasterCycles(4);

      REQUIRE(core.generalRegister(3) == EERegister128{8, 15});
      REQUIRE(core.lo() == 8);
      REQUIRE(core.generalRegister(7).low == 0);

      system.runMasterCycles(4);

      REQUIRE(core.generalRegister(7).low == 42);
      REQUIRE(core.lo() == 42);
    }

    SECTION("Scalar MAC0 older blocks packed work")
    {
      NekoSystem system;
      EECore &core = system.eeCore();
      prepareRegisters(&core);
      system.eeBus().write32(
        0,
        registerInstruction(0x18, 5, 6, 7));
      system.eeBus().write32(
        4,
        mmiInstruction(0x09, 1, 2, 3, 0x0c));
      core.startExecution(0);

      system.clockMasterCycle();
      system.runMasterCycles(4);

      REQUIRE(core.generalRegister(7).low == 42);
      REQUIRE(core.lo() == 42);
      REQUIRE(core.generalRegister(3) == EERegister128{});

      system.runMasterCycles(4);

      REQUIRE(core.generalRegister(3) == EERegister128{8, 15});
      REQUIRE(core.lo() == 8);
      REQUIRE(core.lo1() == 15);
    }
  }

  SECTION("Packed and scalar MAC1 starts retry in either order")
  {
    const auto prepareRegisters =
      [](EECore *core)
      {
        core->setGeneralRegister(1, {2, 3});
        core->setGeneralRegister(2, {4, 5});
        setWord(core, 5, 6);
        setWord(core, 6, 7);
      };

    SECTION("Packed older blocks the younger MAC1 continuation")
    {
      NekoSystem system;
      EECore &core = system.eeCore();
      prepareRegisters(&core);
      system.eeBus().write32(
        0,
        mmiInstruction(0x09, 1, 2, 3, 0x0c));
      system.eeBus().write32(
        4,
        mmiInstruction(0x18, 5, 6, 7));
      core.startExecution(0);

      system.clockMasterCycle();
      REQUIRE(core.programCounter() == 4);
      system.clockMasterCycle();
      REQUIRE(core.programCounter() == 4);
      system.clockMasterCycle();
      REQUIRE(core.programCounter() == 4);
      system.clockMasterCycle();
      REQUIRE(core.programCounter() == 4);
      system.clockMasterCycle();
      REQUIRE(core.generalRegister(3) == EERegister128{8, 15});
      REQUIRE(core.lo1() == 15);
      system.runMasterCycles(4);
      REQUIRE(core.lo1() == 42);
    }

    SECTION("Scalar MAC1 older makes the younger packed work retry")
    {
      NekoSystem system;
      EECore &core = system.eeCore();
      prepareRegisters(&core);
      system.eeBus().write32(
        0,
        mmiInstruction(0x18, 5, 6, 7));
      system.eeBus().write32(
        4,
        mmiInstruction(0x09, 1, 2, 3, 0x0c));
      core.startExecution(0);

      system.clockMasterCycle();
      REQUIRE(core.programCounter() == 4);
      system.runMasterCycles(4);
      REQUIRE(core.lo1() == 42);
      REQUIRE(core.generalRegister(3) == EERegister128{});
      system.runMasterCycles(4);
      REQUIRE(core.generalRegister(3) == EERegister128{8, 15});
    }
  }
}

TEST_CASE("EE packed word multiply accumulate execution")
{
  SECTION("PMADDW adds signed products to both accumulators")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      1,
      {UINT64_C(0xfffffffffffffffe), 2});
    core.setGeneralRegister(2, {3, 3});
    core.setHI(0);
    core.setLO(10);
    core.setHI1(1);
    core.setLO1(0);
    system.eeBus().write32(
      0,
      mmiInstruction(0x09, 1, 2, 3, 0x00));
    core.startExecution(0);

    system.clockMasterCycle();
    system.runMasterCycles(4);

    REQUIRE(core.hi() == 0);
    REQUIRE(core.lo() == 4);
    REQUIRE(core.hi1() == 1);
    REQUIRE(core.lo1() == 6);
    REQUIRE(
      core.generalRegister(3) ==
      EERegister128{4, UINT64_C(0x0000000100000006)});
  }

  SECTION("PMADDUW wraps unsigned accumulation modulo 64 bits")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, {1, 4});
    core.setGeneralRegister(2, {3, 5});
    core.setHI(UINT64_MAX);
    core.setLO(UINT64_C(0xfffffffffffffffe));
    core.setHI1(0);
    core.setLO1(5);
    system.eeBus().write32(
      0,
      mmiInstruction(0x29, 1, 2, 3, 0x00));
    core.startExecution(0);

    system.clockMasterCycle();
    system.runMasterCycles(4);

    REQUIRE(core.hi() == 0);
    REQUIRE(core.lo() == 1);
    REQUIRE(core.hi1() == 0);
    REQUIRE(core.lo1() == 25);
    REQUIRE(core.generalRegister(3) == EERegister128{1, 25});
  }

  SECTION("PMSUBW subtracts signed products modulo 64 bits")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      1,
      {2, UINT64_C(0xfffffffffffffffe)});
    core.setGeneralRegister(2, {4, 3});
    core.setHI(0);
    core.setLO(5);
    core.setHI1(0);
    core.setLO1(0);
    system.eeBus().write32(
      0,
      mmiInstruction(0x09, 1, 2, 3, 0x04));
    core.startExecution(0);

    system.clockMasterCycle();
    system.runMasterCycles(4);

    REQUIRE(core.hi() == UINT64_MAX);
    REQUIRE(core.lo() == UINT64_C(0xfffffffffffffffd));
    REQUIRE(core.hi1() == 0);
    REQUIRE(core.lo1() == 6);
    REQUIRE(
      core.generalRegister(3) ==
      EERegister128{UINT64_C(0xfffffffffffffffd), 6});
  }

  SECTION("Overlapping accumulate forwards the newest packed result")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, {2, 3});
    core.setGeneralRegister(2, {4, 5});
    core.setGeneralRegister(5, {1, 1});
    core.setGeneralRegister(6, {2, 2});
    system.eeBus().write32(
      0,
      mmiInstruction(0x09, 1, 2, 3, 0x0c));
    system.eeBus().write32(
      4,
      mmiInstruction(0x09, 5, 6, 4, 0x00));
    core.startExecution(0);

    system.clockMasterCycle();
    system.clockMasterCycle();
    REQUIRE(core.programCounter() == 4);
    system.clockMasterCycle();
    REQUIRE(core.programCounter() >= 8);

    system.runMasterCycles(2);
    REQUIRE(core.generalRegister(3) == EERegister128{8, 15});
    REQUIRE(core.generalRegister(4) == EERegister128{});

    system.runMasterCycles(2);
    REQUIRE(core.generalRegister(4) == EERegister128{10, 17});
    REQUIRE(core.lo() == 10);
    REQUIRE(core.lo1() == 17);
  }
}

TEST_CASE("EE packed halfword multiply execution")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(
    1,
    {UINT64_C(0x0004fffd0002ffff),
     UINT64_C(0x80007fff0001fffe)});
  core.setGeneralRegister(
    2,
    {UINT64_C(0xfffb00040003fffe),
     UINT64_C(0x0002ffff80000003)});
  system.eeBus().write32(
    0,
    mmiInstruction(0x09, 1, 2, 3, 0x1c));
  core.startExecution(0);

  system.clockMasterCycle();
  system.runMasterCycles(4);

  REQUIRE(core.lo() == UINT64_C(0x0000000600000002));
  REQUIRE(core.hi() == UINT64_C(0xffffffecfffffff4));
  REQUIRE(core.lo1() == UINT64_C(0xffff8000fffffffa));
  REQUIRE(core.hi1() == UINT64_C(0xffff0000ffff8001));
  REQUIRE(
    core.generalRegister(3) ==
    EERegister128{UINT64_C(0xfffffff400000002),
                  UINT64_C(0xffff8001fffffffa)});
}

TEST_CASE("EE packed halfword multiply accumulate execution")
{
  SECTION("PMADDH adds products modulo 32 bits")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      1,
      {UINT64_C(0x0004000300020001),
       UINT64_C(0x0008000700060005)});
    core.setGeneralRegister(
      2,
      {UINT64_C(0x0001000100010001),
       UINT64_C(0x0001000100010001)});
    core.setLO(UINT64_C(0xffffffff00000010));
    core.setHI(UINT64_C(0x00000020fffffffe));
    core.setLO1(UINT64_C(0xfffffff000000030));
    core.setHI1(UINT64_C(0x00000040fffffffc));
    system.eeBus().write32(
      0,
      mmiInstruction(0x09, 1, 2, 3, 0x10));
    core.startExecution(0);

    system.clockMasterCycle();
    system.runMasterCycles(4);

    REQUIRE(core.lo() == UINT64_C(0x0000000100000011));
    REQUIRE(core.hi() == UINT64_C(0x0000002400000001));
    REQUIRE(core.lo1() == UINT64_C(0xfffffff600000035));
    REQUIRE(core.hi1() == UINT64_C(0x0000004800000003));
    REQUIRE(
      core.generalRegister(3) ==
      EERegister128{UINT64_C(0x0000000100000011),
                    UINT64_C(0x0000000300000035)});
  }

  SECTION("PMSUBH subtracts products modulo 32 bits")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      1,
      {UINT64_C(0x0004000300020001),
       UINT64_C(0x0008000700060005)});
    core.setGeneralRegister(
      2,
      {UINT64_C(0x0001000100010001),
       UINT64_C(0x0001000100010001)});
    core.setLO(UINT64_C(0x0000000100000010));
    core.setHI(UINT64_C(0x00000020fffffffe));
    core.setLO1(UINT64_C(0x0000001000000030));
    core.setHI1(UINT64_C(0x00000040fffffffc));
    system.eeBus().write32(
      0,
      mmiInstruction(0x09, 1, 2, 3, 0x14));
    core.startExecution(0);

    system.clockMasterCycle();
    system.runMasterCycles(4);

    REQUIRE(core.lo() == UINT64_C(0xffffffff0000000f));
    REQUIRE(core.hi() == UINT64_C(0x0000001cfffffffb));
    REQUIRE(core.lo1() == UINT64_C(0x0000000a0000002b));
    REQUIRE(core.hi1() == UINT64_C(0x00000038fffffff5));
    REQUIRE(
      core.generalRegister(3) ==
      EERegister128{UINT64_C(0xfffffffb0000000f),
                    UINT64_C(0xfffffff50000002b)});
  }
}

TEST_CASE("EE packed MAC forwards halfword state into word accumulation")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(
    1,
    {UINT64_C(0x0004000300020001),
     UINT64_C(0x0008000700060005)});
  core.setGeneralRegister(
    2,
    {UINT64_C(0x0001000100010001),
     UINT64_C(0x0001000100010001)});
  core.setGeneralRegister(5, {2, 3});
  core.setGeneralRegister(6, {4, 5});
  system.eeBus().write32(
    0,
    mmiInstruction(0x09, 1, 2, 3, 0x1c));
  system.eeBus().write32(
    4,
    mmiInstruction(0x09, 5, 6, 4, 0x00));
  core.startExecution(0);

  system.runMasterCycles(7);

  REQUIRE(
    core.generalRegister(3) ==
    EERegister128{UINT64_C(0x0000000300000001),
                  UINT64_C(0x0000000700000005)});
  REQUIRE(
    core.generalRegister(4) ==
    EERegister128{UINT64_C(0x0000000300000009),
                  UINT64_C(0x0000000700000014)});
  REQUIRE(core.hi() == 3);
  REQUIRE(core.lo() == 9);
  REQUIRE(core.hi1() == 7);
  REQUIRE(core.lo1() == 20);
}

TEST_CASE("EE packed horizontal halfword MAC execution")
{
  const EERegister128 source = {
    UINT64_C(0x0004000300020001),
    UINT64_C(0x0008000700060005)
  };
  const EERegister128 target = {
    UINT64_C(0x0005000400030002),
    UINT64_C(0x0009000800070006)
  };

  SECTION("PHMADH stores sums and the observed odd products")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, target);
    system.eeBus().write32(
      0,
      mmiInstruction(0x09, 1, 2, 3, 0x11));
    core.startExecution(0);

    system.clockMasterCycle();
    system.runMasterCycles(4);

    REQUIRE(core.lo() == UINT64_C(0x0000000600000008));
    REQUIRE(core.hi() == UINT64_C(0x0000001400000020));
    REQUIRE(core.lo1() == UINT64_C(0x0000002a00000048));
    REQUIRE(core.hi1() == UINT64_C(0x0000004800000080));
    REQUIRE(
      core.generalRegister(3) ==
      EERegister128{UINT64_C(0x0000002000000008),
                    UINT64_C(0x0000008000000048)});
  }

  SECTION("PHMSBH stores differences and complemented odd products")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(1, source);
    core.setGeneralRegister(2, target);
    system.eeBus().write32(
      0,
      mmiInstruction(0x09, 1, 2, 3, 0x15));
    core.startExecution(0);

    system.clockMasterCycle();
    system.runMasterCycles(4);

    REQUIRE(core.lo() == UINT64_C(0xfffffff900000004));
    REQUIRE(core.hi() == UINT64_C(0xffffffeb00000008));
    REQUIRE(core.lo1() == UINT64_C(0xffffffd50000000c));
    REQUIRE(core.hi1() == UINT64_C(0xffffffb700000010));
    REQUIRE(
      core.generalRegister(3) ==
      EERegister128{UINT64_C(0x0000000800000004),
                    UINT64_C(0x000000100000000c)});
  }
}

TEST_CASE("EE packed word MAC operations require word-valued lanes")
{
  struct OperationEncoding
  {
    std::uint8_t function;
    std::uint8_t nestedFunction;
  };
  const OperationEncoding operations[] = {
    {0x09, 0x0c},
    {0x29, 0x0c},
    {0x09, 0x00},
    {0x29, 0x00},
    {0x09, 0x04}
  };
  struct InvalidOperands
  {
    EERegister128 source;
    EERegister128 target;
  };
  const EERegister128 validSource = {
    UINT64_C(0xfffffffffffffffe), 3
  };
  const EERegister128 validTarget = {
    4, UINT64_C(0xfffffffffffffffb)
  };
  const InvalidOperands invalidOperands[] = {
    {
      {UINT64_C(0x0000000080000000), 3},
      validTarget
    },
    {
      {UINT64_C(0xfffffffffffffffe),
       UINT64_C(0x0000000080000000)},
      validTarget
    },
    {
      validSource,
      {UINT64_C(0x0000000080000000),
       UINT64_C(0xfffffffffffffffb)}
    },
    {
      validSource,
      {4, UINT64_C(0x0000000080000000)}
    }
  };

  for (const OperationEncoding operation : operations)
  {
    for (const InvalidOperands &operands : invalidOperands)
    {
      NekoSystem system;
      EECore &core = system.eeCore();
      const std::uint32_t instruction = mmiInstruction(
        operation.function,
        1,
        2,
        3,
        operation.nestedFunction);
      core.setGeneralRegister(1, operands.source);
      core.setGeneralRegister(2, operands.target);
      core.setGeneralRegister(
        3,
        {UINT64_C(0x1111222233334444),
         UINT64_C(0x5555666677778888)});
      core.setHI(UINT64_C(0x1111));
      core.setLO(UINT64_C(0x2222));
      core.setHI1(UINT64_C(0x3333));
      core.setLO1(UINT64_C(0x4444));
      system.eeBus().write32(0, instruction);
      core.startExecution(0);

      system.clockMasterCycle();

      CAPTURE(operation.function);
      CAPTURE(operation.nestedFunction);
      CAPTURE(operands.source.low);
      CAPTURE(operands.source.high);
      CAPTURE(operands.target.low);
      CAPTURE(operands.target.high);
      REQUIRE(
        core.executionState() == EEExecutionState::Halted);
      REQUIRE(
        core.stopReason() ==
        EEStopReason::UndefinedOperation);
      REQUIRE(core.programCounter() == 0);
      REQUIRE(core.rejectedInstruction() == instruction);
      REQUIRE(core.hi() == UINT64_C(0x1111));
      REQUIRE(core.lo() == UINT64_C(0x2222));
      REQUIRE(core.hi1() == UINT64_C(0x3333));
      REQUIRE(core.lo1() == UINT64_C(0x4444));
      REQUIRE(
        core.generalRegister(3) ==
        EERegister128{UINT64_C(0x1111222233334444),
                      UINT64_C(0x5555666677778888)});
    }
  }
}

TEST_CASE("EE packed word MAC lifecycle is explicit")
{
  const auto preparePackedMultiply =
    [](NekoSystem *system)
    {
      EECore &core = system->eeCore();
      core.setGeneralRegister(1, {2, 3});
      core.setGeneralRegister(2, {4, 5});
      system->eeBus().write32(
        0,
        mmiInstruction(0x09, 1, 2, 3, 0x0c));
    };

  SECTION("Accepted work crosses synchronous exception entry")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    preparePackedMultiply(&system);
    system.eeBus().write32(4, UINT32_C(0x0000000c));
    for (std::uint32_t address = EEExceptionVector::GENERAL;
         address < EEExceptionVector::GENERAL + 16;
         address += 4)
    {
      system.eeBus().write32(address, 0);
    }
    core.startExecution(0);

    system.runMasterCycles(2);

    REQUIRE(core.pendingException() == EEException::SystemCall);
    REQUIRE(core.generalRegister(3) == EERegister128{});

    system.runMasterCycles(3);

    REQUIRE(core.generalRegister(3) == EERegister128{8, 15});
    REQUIRE(core.lo() == 8);
    REQUIRE(core.lo1() == 15);
  }

  SECTION("Accepted work crosses interrupt entry")
  {
    constexpr std::uint32_t INTC_ENABLED_STATUS =
      EECOP0Status::INTERRUPT_ENABLE |
      EECOP0Status::MASTER_INTERRUPT_ENABLE |
      EECOP0Status::INTC_MASK;
    NekoSystem system;
    EECore &core = system.eeCore();
    preparePackedMultiply(&system);
    core.startExecution(0);
    system.clockMasterCycle();
    system.interruptController().setSource(
      EEInterruptSource::VIF0,
      true);
    system.interruptController().toggleMask(
      EEInterruptSource::mask(EEInterruptSource::VIF0));
    core.setCOP0Register(
      EECOP0Register::Status,
      INTC_ENABLED_STATUS);

    system.clockMasterCycle();

    REQUIRE(core.pendingException() == EEException::Interrupt);
    REQUIRE(core.generalRegister(3) == EERegister128{});

    system.interruptController().acknowledge(
      EEInterruptSource::mask(EEInterruptSource::VIF0));
    system.runMasterCycles(4);

    REQUIRE(core.generalRegister(3) == EERegister128{8, 15});
    REQUIRE(core.lo() == 8);
    REQUIRE(core.lo1() == 15);
  }

  SECTION("Host halt freezes and exact resume preserves accepted work")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    preparePackedMultiply(&system);
    core.startExecution(0);
    system.clockMasterCycle();

    core.haltExecution();
    system.runMasterCycles(6);

    REQUIRE(core.generalRegister(3) == EERegister128{});

    core.startExecution(core.programCounter());
    system.runMasterCycles(4);

    REQUIRE(core.generalRegister(3) == EERegister128{8, 15});
  }

  SECTION("Fresh execution restart discards accepted work")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    preparePackedMultiply(&system);
    core.startExecution(0);
    system.clockMasterCycle();
    core.haltExecution();
    for (std::uint32_t address = 0x100;
         address < 0x118;
         address += 4)
    {
      system.eeBus().write32(address, 0);
    }

    core.startExecution(0x100);
    system.runMasterCycles(5);

    REQUIRE(core.generalRegister(3) == EERegister128{});
    REQUIRE(core.hi() == 0);
    REQUIRE(core.lo() == 0);
    REQUIRE(core.hi1() == 0);
    REQUIRE(core.lo1() == 0);
  }

  SECTION("External PC mutation preserves accepted work")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    preparePackedMultiply(&system);
    core.startExecution(0);
    system.clockMasterCycle();
    for (std::uint32_t address = 0x100;
         address < 0x118;
         address += 4)
    {
      system.eeBus().write32(address, 0);
    }

    core.setProgramCounter(0x100);
    system.runMasterCycles(4);

    REQUIRE(core.generalRegister(3) == EERegister128{8, 15});
    REQUIRE(core.programCounter() >= 0x110);
  }
}

TEST_CASE("EE packed MAC continuation survives save-state restore")
{
  NekoSystem original;
  EECore &originalCore = original.eeCore();
  originalCore.setGeneralRegister(1, {2, 3});
  originalCore.setGeneralRegister(2, {4, 5});
  originalCore.setGeneralRegister(5, {1, 1});
  originalCore.setGeneralRegister(6, {2, 2});
  original.eeBus().write32(
    0,
    mmiInstruction(0x09, 1, 2, 3, 0x0c));
  original.eeBus().write32(
    4,
    mmiInstruction(0x09, 5, 6, 4, 0x00));
  originalCore.startExecution(0);
  original.runMasterCycles(3);
  REQUIRE(originalCore.generalRegister(3) == EERegister128{});
  REQUIRE(originalCore.generalRegister(4) == EERegister128{});

  const std::vector<std::uint8_t> state =
    original.saveState();
  const bool repeatedSaveMatches =
    original.saveState() == state;
  REQUIRE(repeatedSaveMatches);

  NekoSystem restored;
  restored.loadState(state);
  const std::vector<std::uint8_t> restoredState =
    restored.saveState();
  const bool restoredSaveMatches =
    restoredState == state;
  REQUIRE(restoredSaveMatches);
  REQUIRE(
    restored.eeCore().stateHash() ==
    originalCore.stateHash());

  original.runMasterCycles(4);
  restored.runMasterCycles(4);

  const bool resumedStatesMatch =
    original.saveState() == restored.saveState();
  REQUIRE(resumedStatesMatch);
  REQUIRE(originalCore.generalRegister(3) == EERegister128{8, 15});
  REQUIRE(originalCore.generalRegister(4) == EERegister128{10, 17});
  REQUIRE(originalCore.lo() == 10);
  REQUIRE(originalCore.lo1() == 17);
}

TEST_CASE("EE packed halfword MAC continuation survives save-state restore")
{
  NekoSystem original;
  EECore &originalCore = original.eeCore();
  originalCore.setGeneralRegister(
    1,
    {UINT64_C(0x0004000300020001),
     UINT64_C(0x0008000700060005)});
  originalCore.setGeneralRegister(
    2,
    {UINT64_C(0x0001000100010001),
     UINT64_C(0x0001000100010001)});
  originalCore.setLO(UINT64_C(0xffffffff00000010));
  originalCore.setHI(UINT64_C(0x00000020fffffffe));
  originalCore.setLO1(UINT64_C(0xfffffff000000030));
  originalCore.setHI1(UINT64_C(0x00000040fffffffc));
  original.eeBus().write32(
    0,
    mmiInstruction(0x09, 1, 2, 3, 0x10));
  originalCore.startExecution(0);
  original.clockMasterCycle();

  const std::vector<std::uint8_t> state =
    original.saveState();
  NekoSystem restored;
  restored.loadState(state);

  REQUIRE(restored.saveState() == state);
  REQUIRE(restored.eeCore().stateHash() == originalCore.stateHash());

  original.runMasterCycles(4);
  restored.runMasterCycles(4);

  REQUIRE(original.saveState() == restored.saveState());
  REQUIRE(
    originalCore.generalRegister(3) ==
    EERegister128{UINT64_C(0x0000000100000011),
                  UINT64_C(0x0000000300000035)});
}

TEST_CASE("EE delayed packed MAC overlap survives save-state restore")
{
  NekoSystem original;
  EECore &core = original.eeCore();
  core.setGeneralRegister(
    1,
    {UINT64_C(0x0004000300020001),
     UINT64_C(0x0008000700060005)});
  core.setGeneralRegister(
    2,
    {UINT64_C(0x0001000100010001),
     UINT64_C(0x0001000100010001)});
  original.eeBus().write32(
    0,
    mmiInstruction(0x09, 1, 2, 3, 0x1c));
  original.eeBus().write32(
    4,
    mmiInstruction(0x09, 1, 2, 7, 0x12));
  original.eeBus().write32(
    8,
    mmiInstruction(0x09, 1, 2, 8, 0x12));
  original.eeBus().write32(
    12,
    mmiInstruction(0x09, 1, 2, 4, 0x10));
  core.startExecution(0);
  original.runMasterCycles(4);

  REQUIRE(core.programCounter() >= 16);
  REQUIRE(core.generalRegister(3) == EERegister128{});
  REQUIRE(core.generalRegister(4) == EERegister128{});

  const std::vector<std::uint8_t> state =
    original.saveState();
  NekoSystem restored;
  REQUIRE_NOTHROW(restored.loadState(state));

  original.runMasterCycles(4);
  restored.runMasterCycles(4);

  REQUIRE(original.saveState() == restored.saveState());
}

TEST_CASE("EE pending multiply divide interlocks are resource specific")
{
  const auto startMAC0Multiply =
    [](NekoSystem *system)
    {
      EECore &core = system->eeCore();
      setWord(&core, 1, 2);
      setWord(&core, 2, 3);
      system->eeBus().write32(
        0,
        registerInstruction(0x18, 1, 2, 3));
      system->eeBus().write32(
        4,
        registerInstruction(0x29, 0, 0, 0));
      core.startExecution(0);
      system->clockMasterCycle();
      REQUIRE(core.programCounter() == 4);
      REQUIRE(core.lo() == 0);
    };

  SECTION("Unrelated integer work continues while MAC0 is pending")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setWord(&core, 4, 7);
    setWord(&core, 5, 8);
    system.eeBus().write32(
      8,
      registerInstruction(0x21, 4, 5, 6));
    startMAC0Multiply(&system);

    system.clockMasterCycle();

    REQUIRE(core.generalRegister(6).low == 15);
    REQUIRE(core.lo() == 0);
  }

  SECTION("MAC1 can start after an older independent MAC0")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setWord(&core, 4, 4);
    setWord(&core, 5, 5);
    system.eeBus().write32(
      8,
      mmiInstruction(0x18, 4, 5, 6));
    startMAC0Multiply(&system);

    system.clockMasterCycle();
    REQUIRE(core.programCounter() >= 12);

    system.runMasterCycles(3);
    REQUIRE(core.lo() == 6);
    REQUIRE(core.lo1() == 0);

    system.clockMasterCycle();
    REQUIRE(core.lo1() == 20);
  }

  SECTION("MAC0 can start after an older independent MAC1")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setWord(&core, 1, 2);
    setWord(&core, 2, 3);
    setWord(&core, 4, 4);
    setWord(&core, 5, 5);
    system.eeBus().write32(
      0,
      mmiInstruction(0x18, 1, 2, 3));
    system.eeBus().write32(
      4,
      mmiInstruction(0x04, 0, 0, 7));
    system.eeBus().write32(
      8,
      registerInstruction(0x18, 4, 5, 6));
    core.startExecution(0);

    system.clockMasterCycle();
    REQUIRE(core.programCounter() == 4);
    system.clockMasterCycle();
    REQUIRE(core.programCounter() >= 12);

    system.runMasterCycles(3);
    REQUIRE(core.lo1() == 6);
    REQUIRE(core.lo() == 0);

    system.clockMasterCycle();
    REQUIRE(core.lo() == 20);
  }

  SECTION("A scalar MAC0 read waits for MAC0 completion")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    system.eeBus().write32(
      8,
      registerInstruction(0x12, 0, 0, 4));
    startMAC0Multiply(&system);

    system.runMasterCycles(3);
    REQUIRE(core.programCounter() == 8);
    REQUIRE(core.generalRegister(4).low == 0);

    system.clockMasterCycle();
    REQUIRE(core.generalRegister(4).low == 6);
  }

  SECTION("A scalar MAC0 write waits and follows MAC0 completion")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      4,
      {UINT64_C(0x123456789abcdef0), 0});
    system.eeBus().write32(
      8,
      registerInstruction(0x11, 4, 0, 0));
    startMAC0Multiply(&system);

    system.runMasterCycles(3);
    REQUIRE(core.programCounter() == 8);
    REQUIRE(core.hi() == 0);

    system.clockMasterCycle();
    REQUIRE(core.hi() == UINT64_C(0x123456789abcdef0));
  }

  SECTION("An independent scalar MAC1 read does not wait for MAC0")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setLO1(UINT64_C(0x123456789abcdef0));
    system.eeBus().write32(
      8,
      mmiInstruction(0x12, 0, 0, 4));
    startMAC0Multiply(&system);

    system.clockMasterCycle();

    REQUIRE(
      core.generalRegister(4).low ==
      UINT64_C(0x123456789abcdef0));
    REQUIRE(core.lo() == 0);
  }

  SECTION("A packed full-width read waits for either MAC pipeline")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setHI1(UINT64_C(0x123456789abcdef0));
    system.eeBus().write32(
      8,
      mmiInstruction(0x09, 0, 0, 4, 0x08));
    startMAC0Multiply(&system);

    system.runMasterCycles(3);
    REQUIRE(core.programCounter() == 8);
    REQUIRE(core.generalRegister(4) == EERegister128{});

    system.clockMasterCycle();
    REQUIRE(
      core.generalRegister(4) ==
      EERegister128{0, UINT64_C(0x123456789abcdef0)});
  }

  SECTION("A pending multiply destination interlocks GPR consumers")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    system.eeBus().write32(
      8,
      registerInstruction(0x21, 3, 0, 4));
    startMAC0Multiply(&system);

    system.runMasterCycles(3);
    REQUIRE(core.programCounter() == 8);
    REQUIRE(core.generalRegister(4).low == 0);

    system.clockMasterCycle();
    REQUIRE(core.generalRegister(3).low == 6);
    REQUIRE(core.generalRegister(4).low == 6);
  }

  SECTION("A pending multiply destination interlocks GPR writers")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setWord(&core, 4, 7);
    setWord(&core, 5, 8);
    system.eeBus().write32(
      8,
      registerInstruction(0x21, 4, 5, 3));
    startMAC0Multiply(&system);

    system.runMasterCycles(3);
    REQUIRE(core.programCounter() == 8);
    REQUIRE(core.generalRegister(3).low == 0);

    system.clockMasterCycle();
    REQUIRE(core.generalRegister(3).low == 15);
  }
}

TEST_CASE("EE packed HI LO transfers wait for either pending MAC pipeline")
{
  struct Contract
  {
    std::uint32_t instruction;
    EEOperation operation;
  };
  const Contract contracts[] = {
    {
      mmiInstruction(0x09, 0, 0, 8, 0x08),
      EEOperation::ParallelMoveFromHI
    },
    {
      mmiInstruction(0x09, 0, 0, 8, 0x09),
      EEOperation::ParallelMoveFromLO
    },
    {
      mmiInstruction(0x29, 8, 0, 0, 0x08),
      EEOperation::ParallelMoveToHI
    },
    {
      mmiInstruction(0x29, 8, 0, 0, 0x09),
      EEOperation::ParallelMoveToLO
    },
    {
      mmiInstruction(0x30, 0, 0, 8, 0),
      EEOperation::ParallelMoveFromHILOLowerWord
    },
    {
      mmiInstruction(0x30, 0, 0, 8, 1),
      EEOperation::ParallelMoveFromHILOUpperWord
    },
    {
      mmiInstruction(0x30, 0, 0, 8, 2),
      EEOperation::ParallelMoveFromHILOSaturatedWord
    },
    {
      mmiInstruction(0x30, 0, 0, 8, 3),
      EEOperation::ParallelMoveFromHILOHalfword
    },
    {
      mmiInstruction(0x30, 0, 0, 8, 4),
      EEOperation::ParallelMoveFromHILOSaturatedHalfword
    },
    {
      mmiInstruction(0x31, 8, 0, 0),
      EEOperation::ParallelMoveToHILOLowerWord
    }
  };

  for (const bool mac1 : {false, true})
  {
    for (const Contract &contract : contracts)
    {
      NekoSystem system;
      EECore &core = system.eeCore();
      setWord(&core, 1, 2);
      setWord(&core, 2, 3);
      core.setGeneralRegister(
        8,
        {
          UINT64_C(0x1111222233334444),
          UINT64_C(0x5555666677778888)
        });
      system.eeBus().write32(
        0,
        mac1
          ? mmiInstruction(0x18, 1, 2, 3)
          : registerInstruction(0x18, 1, 2, 3));
      system.eeBus().write32(
        4,
        mac1
          ? mmiInstruction(0x04, 0, 0, 7)
          : registerInstruction(0x29, 0, 0, 0));
      system.eeBus().write32(8, contract.instruction);
      core.startExecution(0);

      system.clockMasterCycle();
      REQUIRE(core.programCounter() == 4);
      system.runMasterCycles(3);
      REQUIRE(core.programCounter() == 8);

      system.clockMasterCycle();
      REQUIRE(core.programCounter() >= 12);
      REQUIRE(core.lastInstruction().operation == contract.operation);
    }
  }
}

TEST_CASE("EE divide execution")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  SECTION("Signed divide produces a truncated quotient and remainder")
  {
    setWord(&core, 1, UINT32_C(0xfffffff9));
    setWord(&core, 2, 3);

    runToCompletion(
      &system,
      registerInstruction(0x1a, 1, 2, 0),
      37);

    REQUIRE(core.lo() == UINT64_C(0xfffffffffffffffe));
    REQUIRE(core.hi() == UINT64_MAX);
  }

  SECTION("Signed minimum divided by negative one has defined results")
  {
    setWord(&core, 1, UINT32_C(0x80000000));
    setWord(&core, 2, UINT32_MAX);

    runToCompletion(
      &system,
      mmiInstruction(0x1a, 1, 2, 0),
      37);

    REQUIRE(core.lo1() == UINT64_C(0xffffffff80000000));
    REQUIRE(core.hi1() == 0);
    REQUIRE(core.exceptionPending() == false);
  }

  SECTION("The manual-defined zero-divisor result remains explicit")
  {
    setWord(&core, 1, 7);
    setWord(&core, 2, 0);

    system.eeBus().write32(
      0,
      registerInstruction(0x1b, 1, 2, 0));
    core.startExecution(0);
    system.clockMasterCycle();

    REQUIRE(core.executionState() == EEExecutionState::Halted);
    REQUIRE(core.stopReason() == EEStopReason::UndefinedOperation);
    REQUIRE(core.programCounter() == 0);
  }
}

TEST_CASE("EE HI LO and shift amount transfers")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  core.setGeneralRegister(
    1,
    {UINT64_C(0x123456789abcdef0), 0});
  system.eeBus().write32(
    0,
    registerInstruction(0x11, 1, 0, 0));
  system.eeBus().write32(
    4,
    registerInstruction(0x10, 0, 0, 2));
  system.eeBus().write32(
    8,
    regimmInstruction(0x18, 1, 3));
  system.eeBus().write32(
    12,
    registerInstruction(0x28, 0, 0, 3));
  system.eeBus().write32(
    16,
    0);
  system.eeBus().write32(
    20,
    0);
  system.eeBus().write32(
    24,
    0);
  system.eeBus().write32(
    28,
    regimmInstruction(0x19, 1, 5));
  system.eeBus().write32(
    32,
    registerInstruction(0x28, 0, 0, 4));
  core.startExecution(0);
  system.runMasterCycles(9);

  REQUIRE(core.hi() == UINT64_C(0x123456789abcdef0));
  REQUIRE(
    core.generalRegister(2).low ==
    UINT64_C(0x123456789abcdef0));
  REQUIRE(core.generalRegister(3).low == 24);
  REQUIRE(core.generalRegister(4).low == 80);
}

TEST_CASE("EE shift amount ordering restrictions are explicit")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  system.eeBus().write32(
    0,
    registerInstruction(0x28, 0, 0, 1));
  system.eeBus().write32(
    4,
    regimmInstruction(0x18, 0, 2));
  core.startExecution(0);
  system.runMasterCycles(2);

  REQUIRE(core.executionState() == EEExecutionState::Halted);
  REQUIRE(core.stopReason() == EEStopReason::UndefinedOperation);
  REQUIRE(core.programCounter() == 4);
}

TEST_CASE("EE QFSRV establishes shift amount read restrictions")
{
  const std::uint32_t restrictedFollowers[] = {
    registerInstruction(0x29, 1, 0, 0),
    regimmInstruction(0x18, 1, 0),
    regimmInstruction(0x19, 1, 0)
  };

  for (std::uint32_t follower : restrictedFollowers)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    system.eeBus().write32(
      0,
      mmiInstruction(0x28, 1, 2, 3, 0x1b));
    system.eeBus().write32(4, follower);
    core.startExecution(0);
    system.runMasterCycles(2);

    REQUIRE(core.executionState() == EEExecutionState::Halted);
    REQUIRE(core.stopReason() == EEStopReason::UndefinedOperation);
    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.rejectedInstruction() == follower);
  }
}

TEST_CASE("EE shift amount windows advance per acceptance")
{
  EEShiftAmountOrderingWindow window;
  window.accept(
    EEOperation::QuadwordFunnelShiftRightVariable);
  REQUIRE_FALSE(
    window.permits(EEOperation::MoveToShiftAmount));
  REQUIRE_FALSE(
    window.permits(
      EEOperation::MoveByteCountToShiftAmount));
  REQUIRE_FALSE(
    window.permits(
      EEOperation::MoveHalfwordCountToShiftAmount));

  window.accept(EEOperation::Nop);
  window.accept(EEOperation::Nop);

  REQUIRE_FALSE(
    window.permits(EEOperation::MoveToShiftAmount));
  REQUIRE_FALSE(
    window.permits(
      EEOperation::MoveByteCountToShiftAmount));
  REQUIRE_FALSE(
    window.permits(
      EEOperation::MoveHalfwordCountToShiftAmount));

  window.accept(EEOperation::Nop);

  REQUIRE(
    window.permits(EEOperation::MoveToShiftAmount));
  REQUIRE(
    window.permits(
      EEOperation::MoveByteCountToShiftAmount));
  REQUIRE(
    window.permits(
      EEOperation::MoveHalfwordCountToShiftAmount));
}

TEST_CASE("EE reset clears both pending MAC pipelines")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(1, {3, 0});
  core.setGeneralRegister(2, {4, 0});
  core.setGeneralRegister(4, {5, 0});
  core.setGeneralRegister(5, {6, 0});
  system.eeBus().write32(
    0,
    registerInstruction(0x18, 1, 2, 3));
  system.eeBus().write32(
    4,
    UINT32_C(0x70000000) |
      registerInstruction(0x18, 4, 5, 6));
  core.startExecution(0);

  system.clockMasterCycle();
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 2);

  core.reset();

  EECore baseline;
  REQUIRE(core.stateHash() == baseline.stateHash());
  REQUIRE(core.generalRegister(3).low == 0);
  REQUIRE(core.generalRegister(6).low == 0);
  REQUIRE(core.lo() == 0);
  REQUIRE(core.lo1() == 0);
}
