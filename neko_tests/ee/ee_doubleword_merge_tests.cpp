#include <array>
#include <cstdint>

#include "catch.hpp"
#include "ee_core.hpp"
#include "neko_system.hpp"

namespace
{
  std::uint32_t memoryInstruction(
    std::uint8_t opcode,
    std::uint8_t base,
    std::uint8_t target,
    std::uint16_t offset)
  {
    return
      (static_cast<std::uint32_t>(opcode) << 26) |
      (static_cast<std::uint32_t>(base) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      offset;
  }

  void setRegister(
    EECore *core,
    std::uint8_t index,
    std::uint64_t low)
  {
    core->setGeneralRegister(
      index,
      {low, UINT64_C(0xfeedfacecafebeef)});
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

TEST_CASE("EE LDL follows every little-endian byte position")
{
  const std::array<std::uint64_t, 8> expected = {{
    UINT64_C(0x10223344a1b2c3d4),
    UINT64_C(0x20103344a1b2c3d4),
    UINT64_C(0x30201044a1b2c3d4),
    UINT64_C(0x40302010a1b2c3d4),
    UINT64_C(0x5040302010b2c3d4),
    UINT64_C(0x605040302010c3d4),
    UINT64_C(0x70605040302010d4),
    UINT64_C(0x8070605040302010)
  }};

  for (std::uint8_t offset = 0; offset < 8; ++offset)
  {
    NekoSystem system;
    setRegister(&system.eeCore(), 1, 0x100);
    setRegister(
      &system.eeCore(),
      2,
      UINT64_C(0x11223344a1b2c3d4));
    REQUIRE(
      system.eeBus().writeData64(
        0x100,
        UINT64_C(0x8070605040302010)));

    runInstruction(
      &system,
      memoryInstruction(0x1a, 1, 2, offset));

    REQUIRE(system.eeCore().generalRegister(2).low == expected[offset]);
    REQUIRE(
      system.eeCore().generalRegister(2).high ==
      UINT64_C(0xfeedfacecafebeef));
  }
}

TEST_CASE("EE LDR follows every little-endian byte position")
{
  const std::array<std::uint64_t, 8> expected = {{
    UINT64_C(0x8070605040302010),
    UINT64_C(0x1180706050403020),
    UINT64_C(0x1122807060504030),
    UINT64_C(0x1122338070605040),
    UINT64_C(0x1122334480706050),
    UINT64_C(0x11223344a1807060),
    UINT64_C(0x11223344a1b28070),
    UINT64_C(0x11223344a1b2c380)
  }};

  for (std::uint8_t offset = 0; offset < 8; ++offset)
  {
    NekoSystem system;
    setRegister(&system.eeCore(), 1, 0x100);
    setRegister(
      &system.eeCore(),
      2,
      UINT64_C(0x11223344a1b2c3d4));
    REQUIRE(
      system.eeBus().writeData64(
        0x100,
        UINT64_C(0x8070605040302010)));

    runInstruction(
      &system,
      memoryInstruction(0x1b, 1, 2, offset));

    REQUIRE(system.eeCore().generalRegister(2).low == expected[offset]);
    REQUIRE(
      system.eeCore().generalRegister(2).high ==
      UINT64_C(0xfeedfacecafebeef));
  }
}

TEST_CASE("EE SDL follows every little-endian byte position")
{
  const std::array<std::uint64_t, 8> expected = {{
    UINT64_C(0x77665544ccbbaa11),
    UINT64_C(0x77665544ccbb1122),
    UINT64_C(0x77665544cc112233),
    UINT64_C(0x7766554411223344),
    UINT64_C(0x77665511223344a1),
    UINT64_C(0x776611223344a1b2),
    UINT64_C(0x7711223344a1b2c3),
    UINT64_C(0x11223344a1b2c3d4)
  }};

  for (std::uint8_t offset = 0; offset < 8; ++offset)
  {
    NekoSystem system;
    setRegister(&system.eeCore(), 1, 0x100);
    setRegister(
      &system.eeCore(),
      2,
      UINT64_C(0x11223344a1b2c3d4));
    REQUIRE(
      system.eeBus().writeData64(
        0x100,
        UINT64_C(0x77665544ccbbaa99)));

    runInstruction(
      &system,
      memoryInstruction(0x2c, 1, 2, offset));

    std::uint64_t memory = 0;
    REQUIRE(system.eeBus().readData64(0x100, &memory));
    REQUIRE(memory == expected[offset]);
  }
}

TEST_CASE("EE SDR follows every little-endian byte position")
{
  const std::array<std::uint64_t, 8> expected = {{
    UINT64_C(0x11223344a1b2c3d4),
    UINT64_C(0x223344a1b2c3d499),
    UINT64_C(0x3344a1b2c3d4aa99),
    UINT64_C(0x44a1b2c3d4bbaa99),
    UINT64_C(0xa1b2c3d4ccbbaa99),
    UINT64_C(0xb2c3d444ccbbaa99),
    UINT64_C(0xc3d45544ccbbaa99),
    UINT64_C(0xd4665544ccbbaa99)
  }};

  for (std::uint8_t offset = 0; offset < 8; ++offset)
  {
    NekoSystem system;
    setRegister(&system.eeCore(), 1, 0x100);
    setRegister(
      &system.eeCore(),
      2,
      UINT64_C(0x11223344a1b2c3d4));
    REQUIRE(
      system.eeBus().writeData64(
        0x100,
        UINT64_C(0x77665544ccbbaa99)));

    runInstruction(
      &system,
      memoryInstruction(0x2d, 1, 2, offset));

    std::uint64_t memory = 0;
    REQUIRE(system.eeBus().readData64(0x100, &memory));
    REQUIRE(memory == expected[offset]);
  }
}

TEST_CASE("EE paired doubleword merges transfer unaligned data")
{
  SECTION("LDL and LDR assemble eight unaligned bytes")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, UINT64_C(0x1234000000000101));
    setRegister(&core, 2, 0);
    REQUIRE(
      system.eeBus().writeData64(
        0x100,
        UINT64_C(0x7060504030201000)));
    REQUIRE(
      system.eeBus().writeData64(
        0x108,
        UINT64_C(0xf0e0d0c0b0a09080)));
    system.eeBus().write32(
      0,
      memoryInstruction(0x1a, 1, 2, 7));
    system.eeBus().write32(
      4,
      memoryInstruction(0x1b, 1, 2, 0));
    core.startExecution(0);
    system.runMasterCycles(2);

    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0x8070605040302010));
  }

  SECTION("SDL and SDR distribute eight unaligned bytes")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, 0x101);
    setRegister(&core, 2, UINT64_C(0x8877665544332211));
    REQUIRE(
      system.eeBus().writeData64(
        0x100,
        UINT64_C(0x0706050403020100)));
    REQUIRE(
      system.eeBus().writeData64(
        0x108,
        UINT64_C(0x0f0e0d0c0b0a0908)));
    system.eeBus().write32(
      0,
      memoryInstruction(0x2c, 1, 2, 7));
    system.eeBus().write32(
      4,
      memoryInstruction(0x2d, 1, 2, 0));
    core.startExecution(0);
    system.runMasterCycles(2);

    std::uint64_t first = 0;
    std::uint64_t second = 0;
    REQUIRE(system.eeBus().readData64(0x100, &first));
    REQUIRE(system.eeBus().readData64(0x108, &second));
    REQUIRE(first == UINT64_C(0x7766554433221100));
    REQUIRE(second == UINT64_C(0x0f0e0d0c0b0a0988));
  }
}

TEST_CASE("EE doubleword merge faults preserve architectural state")
{
  SECTION("An unmapped merge load preserves its destination")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(
      &core,
      1,
      EEMemoryMap::MAIN_MEMORY_SIZE + 3);
    setRegister(&core, 2, UINT64_C(0x1122334455667788));

    runInstruction(
      &system,
      memoryInstruction(0x1a, 1, 2, 0));

    REQUIRE(core.pendingException() == EEException::DataBusErrorLoad);
    REQUIRE(
      core.exceptionAddress() ==
      EEMemoryMap::MAIN_MEMORY_SIZE + 3);
    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0x1122334455667788));
  }

  SECTION("An unmapped merge store reports the effective address")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(
      &core,
      1,
      EEMemoryMap::MAIN_MEMORY_SIZE + 5);
    setRegister(&core, 2, UINT64_MAX);

    runInstruction(
      &system,
      memoryInstruction(0x2d, 1, 2, 0));

    REQUIRE(core.pendingException() == EEException::DataBusErrorStore);
    REQUIRE(
      core.exceptionAddress() ==
      EEMemoryMap::MAIN_MEMORY_SIZE + 5);
  }
}

TEST_CASE("EE doubleword merge loads execute in branch delay slots")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  setRegister(&core, 1, 0x107);
  setRegister(&core, 2, UINT64_C(0x1122334455667788));
  REQUIRE(
    system.eeBus().writeData64(
      0x100,
      UINT64_C(0x8070605040302010)));
  system.eeBus().write32(
    0,
    memoryInstruction(0x04, 0, 0, 2));
  system.eeBus().write32(
    4,
    memoryInstruction(0x1b, 1, 2, 0));
  system.eeBus().write32(
    12,
    memoryInstruction(0x0d, 0, 3, 1));
  core.startExecution(0);
  system.runMasterCycles(3);

  REQUIRE(
    core.generalRegister(2).low ==
    UINT64_C(0x1122334455667780));
  REQUIRE(core.generalRegister(3).low == 1);
}
