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

TEST_CASE("EE LWL follows every little-endian byte position")
{
  const std::array<std::uint64_t, 4> expected = {{
    UINT64_C(0x0000000010b2c3d4),
    UINT64_C(0x000000002010c3d4),
    UINT64_C(0x00000000302010d4),
    UINT64_C(0xffffffff80302010)
  }};

  for (std::uint8_t offset = 0; offset < 4; ++offset)
  {
    NekoSystem system;
    setRegister(&system.eeCore(), 1, 0x100);
    setRegister(
      &system.eeCore(),
      2,
      UINT64_C(0x11223344a1b2c3d4));
    REQUIRE(
      system.eeBus().writeData32(
        0x100,
        UINT32_C(0x80302010)));

    runInstruction(
      &system,
      memoryInstruction(0x22, 1, 2, offset));

    REQUIRE(
      system.eeCore().generalRegister(2).low ==
      expected[offset]);
    REQUIRE(
      system.eeCore().generalRegister(2).high ==
      UINT64_C(0xfeedfacecafebeef));
  }
}

TEST_CASE("EE LWR preserves or sign extends its upper word")
{
  const std::array<std::uint64_t, 4> expected = {{
    UINT64_C(0xffffffff80302010),
    UINT64_C(0x11223344a1803020),
    UINT64_C(0x11223344a1b28030),
    UINT64_C(0x11223344a1b2c380)
  }};

  for (std::uint8_t offset = 0; offset < 4; ++offset)
  {
    NekoSystem system;
    setRegister(&system.eeCore(), 1, 0x100);
    setRegister(
      &system.eeCore(),
      2,
      UINT64_C(0x11223344a1b2c3d4));
    REQUIRE(
      system.eeBus().writeData32(
        0x100,
        UINT32_C(0x80302010)));

    runInstruction(
      &system,
      memoryInstruction(0x26, 1, 2, offset));

    REQUIRE(
      system.eeCore().generalRegister(2).low ==
      expected[offset]);
  }
}

TEST_CASE("EE SWL follows every little-endian byte position")
{
  const std::array<std::uint32_t, 4> expected = {{
    UINT32_C(0x776655a1),
    UINT32_C(0x7766a1b2),
    UINT32_C(0x77a1b2c3),
    UINT32_C(0xa1b2c3d4)
  }};

  for (std::uint8_t offset = 0; offset < 4; ++offset)
  {
    NekoSystem system;
    setRegister(&system.eeCore(), 1, 0x100);
    setRegister(
      &system.eeCore(),
      2,
      UINT64_C(0x11223344a1b2c3d4));
    REQUIRE(
      system.eeBus().writeData32(
        0x100,
        UINT32_C(0x77665544)));

    runInstruction(
      &system,
      memoryInstruction(0x2a, 1, 2, offset));

    std::uint32_t memory = 0;
    REQUIRE(system.eeBus().readData32(0x100, &memory));
    REQUIRE(memory == expected[offset]);
  }
}

TEST_CASE("EE SWR follows every little-endian byte position")
{
  const std::array<std::uint32_t, 4> expected = {{
    UINT32_C(0xa1b2c3d4),
    UINT32_C(0xb2c3d444),
    UINT32_C(0xc3d45544),
    UINT32_C(0xd4665544)
  }};

  for (std::uint8_t offset = 0; offset < 4; ++offset)
  {
    NekoSystem system;
    setRegister(&system.eeCore(), 1, 0x100);
    setRegister(
      &system.eeCore(),
      2,
      UINT64_C(0x11223344a1b2c3d4));
    REQUIRE(
      system.eeBus().writeData32(
        0x100,
        UINT32_C(0x77665544)));

    runInstruction(
      &system,
      memoryInstruction(0x2e, 1, 2, offset));

    std::uint32_t memory = 0;
    REQUIRE(system.eeBus().readData32(0x100, &memory));
    REQUIRE(memory == expected[offset]);
  }
}

TEST_CASE("EE paired word merges transfer an unaligned word")
{
  SECTION("LWL and LWR assemble four unaligned bytes")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, 0x101);
    setRegister(&core, 2, UINT64_C(0x5566778899aabbcc));
    REQUIRE(
      system.eeBus().writeData32(
        0x100,
        UINT32_C(0x33221100)));
    REQUIRE(
      system.eeBus().writeData32(
        0x104,
        UINT32_C(0x88776684)));
    system.eeBus().write32(
      0,
      memoryInstruction(0x22, 1, 2, 3));
    system.eeBus().write32(
      4,
      memoryInstruction(0x26, 1, 2, 0));
    core.startExecution(0);
    system.runMasterCycles(2);

    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0xffffffff84332211));
  }

  SECTION("SWL and SWR distribute four unaligned bytes")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, 0x101);
    setRegister(&core, 2, UINT32_C(0xa1b2c3d4));
    REQUIRE(
      system.eeBus().writeData32(
        0x100,
        UINT32_C(0x33221100)));
    REQUIRE(
      system.eeBus().writeData32(
        0x104,
        UINT32_C(0x88776655)));
    system.eeBus().write32(
      0,
      memoryInstruction(0x2a, 1, 2, 3));
    system.eeBus().write32(
      4,
      memoryInstruction(0x2e, 1, 2, 0));
    core.startExecution(0);
    system.runMasterCycles(2);

    std::uint32_t first = 0;
    std::uint32_t second = 0;
    REQUIRE(system.eeBus().readData32(0x100, &first));
    REQUIRE(system.eeBus().readData32(0x104, &second));
    REQUIRE(first == UINT32_C(0xb2c3d400));
    REQUIRE(second == UINT32_C(0x887766a1));
  }
}

TEST_CASE("EE word merge faults preserve destination and memory")
{
  SECTION("An unmapped merge load preserves its destination")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(
      &core,
      1,
      EEMemoryMap::MAIN_MEMORY_SIZE + 1);
    setRegister(&core, 2, UINT64_C(0x1122334455667788));

    runInstruction(
      &system,
      memoryInstruction(0x22, 1, 2, 0));

    REQUIRE(core.pendingException() == EEException::DataBusErrorLoad);
    REQUIRE(
      core.exceptionAddress() ==
      EEMemoryMap::MAIN_MEMORY_SIZE + 1);
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
      EEMemoryMap::MAIN_MEMORY_SIZE + 2);
    setRegister(&core, 2, UINT32_C(0xaabbccdd));

    runInstruction(
      &system,
      memoryInstruction(0x2e, 1, 2, 0));

    REQUIRE(core.pendingException() == EEException::DataBusErrorStore);
    REQUIRE(
      core.exceptionAddress() ==
      EEMemoryMap::MAIN_MEMORY_SIZE + 2);
  }
}
