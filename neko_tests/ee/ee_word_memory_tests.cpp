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

TEST_CASE("EE signed and unsigned aligned word loads")
{
  SECTION("LW sign extends a little-endian word")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, UINT64_C(0x1234000000000104));
    setRegister(&core, 2, 0);
    REQUIRE(
      system.eeBus().writeData32(
        0x100,
        UINT32_C(0x80000001)));

    runInstruction(
      &system,
      memoryInstruction(0x23, 1, 2, 0xfffc));

    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0xffffffff80000001));
    REQUIRE(
      core.generalRegister(2).high ==
      UINT64_C(0xfeedfacecafebeef));
  }

  SECTION("LWU zero extends through a RAM alias")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, 0x30000100);
    setRegister(&core, 2, UINT64_MAX);
    REQUIRE(
      system.eeBus().writeData32(
        0x100,
        UINT32_C(0xfedcba98)));

    runInstruction(
      &system,
      memoryInstruction(0x27, 1, 2, 0));

    REQUIRE(core.generalRegister(2).low == UINT32_C(0xfedcba98));
    REQUIRE(
      core.generalRegister(2).high ==
      UINT64_C(0xfeedfacecafebeef));
  }
}

TEST_CASE("EE aligned word stores use the low 32 bits")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  setRegister(&core, 1, 0x80000100);
  setRegister(&core, 2, UINT64_C(0x1122334489abcdef));

  runInstruction(
    &system,
    memoryInstruction(0x2b, 1, 2, 4));

  std::uint32_t value = 0;
  REQUIRE(system.eeBus().readData32(0x104, &value));
  REQUIRE(value == UINT32_C(0x89abcdef));
}

TEST_CASE("EE aligned word faults are precise")
{
  SECTION("A misaligned word load preserves its destination")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, 0x102);
    setRegister(&core, 2, 0x1234);

    runInstruction(
      &system,
      memoryInstruction(0x23, 1, 2, 0));

    REQUIRE(
      core.pendingException() ==
      EEException::AddressErrorLoadOrFetch);
    REQUIRE(core.exceptionAddress() == 0x102);
    REQUIRE(core.generalRegister(2).low == 0x1234);
  }

  SECTION("A misaligned word store performs no partial write")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, 0x102);
    setRegister(&core, 2, UINT32_C(0xaabbccdd));
    REQUIRE(
      system.eeBus().writeData32(
        0x100,
        UINT32_C(0x11223344)));

    runInstruction(
      &system,
      memoryInstruction(0x2b, 1, 2, 0));

    REQUIRE(core.pendingException() == EEException::AddressErrorStore);
    std::uint32_t value = 0;
    REQUIRE(system.eeBus().readData32(0x100, &value));
    REQUIRE(value == UINT32_C(0x11223344));
  }

  SECTION("An aligned access beyond RAM reports a data bus fault")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, EEMemoryMap::MAIN_MEMORY_SIZE);

    runInstruction(
      &system,
      memoryInstruction(0x27, 1, 2, 0));

    REQUIRE(core.pendingException() == EEException::DataBusErrorLoad);
    REQUIRE(
      core.exceptionAddress() ==
      EEMemoryMap::MAIN_MEMORY_SIZE);
  }
}

TEST_CASE("EE aligned word access reaches the RAM boundary")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  const std::uint32_t address =
    EEMemoryMap::MAIN_MEMORY_SIZE - 4;
  setRegister(&core, 1, address);
  setRegister(&core, 2, UINT32_C(0x76543210));

  runInstruction(
    &system,
    memoryInstruction(0x2b, 1, 2, 0));

  std::uint32_t value = 0;
  REQUIRE(system.eeBus().readData32(address, &value));
  REQUIRE(value == UINT32_C(0x76543210));
}

TEST_CASE("EE aligned word loads execute in branch delay slots")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  setRegister(&core, 1, 0x100);
  REQUIRE(
    system.eeBus().writeData32(
      0x100,
      UINT32_C(0x12345678)));
  system.eeBus().write32(
    0,
    memoryInstruction(0x04, 0, 0, 2));
  system.eeBus().write32(
    4,
    memoryInstruction(0x23, 1, 2, 0));
  system.eeBus().write32(
    12,
    memoryInstruction(0x0d, 0, 3, 1));
  core.startExecution(0);
  system.runMasterCycles(3);

  REQUIRE(core.generalRegister(2).low == 0x12345678);
  REQUIRE(core.generalRegister(3).low == 1);
}
