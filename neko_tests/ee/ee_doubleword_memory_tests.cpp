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

TEST_CASE("EE aligned doubleword loads preserve the upper GPR")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  setRegister(&core, 1, UINT64_C(0x1234000000000108));
  setRegister(&core, 2, 0);
  for (std::uint8_t index = 0; index < 8; ++index)
  {
    REQUIRE(
      system.eeBus().writeData8(
        0x100 + index,
        static_cast<std::uint8_t>(0x11 * (index + 1))));
  }

  runInstruction(
    &system,
    memoryInstruction(0x37, 1, 2, 0xfff8));

  REQUIRE(
    core.generalRegister(2).low ==
    UINT64_C(0x8877665544332211));
  REQUIRE(
    core.generalRegister(2).high ==
    UINT64_C(0xfeedfacecafebeef));
}

TEST_CASE("EE aligned doubleword stores use GPR bits 63 through 0")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  setRegister(&core, 1, 0xa0000100);
  setRegister(&core, 2, UINT64_C(0x0123456789abcdef));

  runInstruction(
    &system,
    memoryInstruction(0x3f, 1, 2, 8));

  const std::uint8_t expected[8] = {
    0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01
  };
  for (std::uint8_t index = 0; index < 8; ++index)
  {
    std::uint8_t value = 0;
    REQUIRE(system.eeBus().readData8(0x108 + index, &value));
    REQUIRE(value == expected[index]);
  }
}

TEST_CASE("EE aligned doubleword faults are precise")
{
  SECTION("A misaligned load preserves its destination")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, 0x104);
    setRegister(&core, 2, UINT64_C(0x1122334455667788));

    runInstruction(
      &system,
      memoryInstruction(0x37, 1, 2, 0));

    REQUIRE(
      core.pendingException() ==
      EEException::AddressErrorLoadOrFetch);
    REQUIRE(core.exceptionAddress() == 0x104);
    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0x1122334455667788));
  }

  SECTION("A misaligned store performs no partial write")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, 0x104);
    setRegister(&core, 2, UINT64_MAX);
    REQUIRE(
      system.eeBus().writeData64(
        0x100,
        UINT64_C(0x1122334455667788)));

    runInstruction(
      &system,
      memoryInstruction(0x3f, 1, 2, 0));

    REQUIRE(core.pendingException() == EEException::AddressErrorStore);
    std::uint64_t value = 0;
    REQUIRE(system.eeBus().readData64(0x100, &value));
    REQUIRE(value == UINT64_C(0x1122334455667788));
  }

  SECTION("An aligned access beyond RAM reports a bus fault")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, EEMemoryMap::MAIN_MEMORY_SIZE);

    runInstruction(
      &system,
      memoryInstruction(0x37, 1, 2, 0));

    REQUIRE(core.pendingException() == EEException::DataBusErrorLoad);
    REQUIRE(
      core.exceptionAddress() ==
      EEMemoryMap::MAIN_MEMORY_SIZE);
  }

  SECTION("An aligned store beyond RAM reports a bus fault")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, EEMemoryMap::MAIN_MEMORY_SIZE);
    setRegister(&core, 2, UINT64_MAX);

    runInstruction(
      &system,
      memoryInstruction(0x3f, 1, 2, 0));

    REQUIRE(core.pendingException() == EEException::DataBusErrorStore);
    REQUIRE(
      core.exceptionAddress() ==
      EEMemoryMap::MAIN_MEMORY_SIZE);
  }
}

TEST_CASE("EE aligned doubleword access reaches the RAM boundary")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  const std::uint32_t address =
    EEMemoryMap::MAIN_MEMORY_SIZE - 8;
  setRegister(&core, 1, address);
  setRegister(&core, 2, UINT64_C(0x76543210fedcba98));

  runInstruction(
    &system,
    memoryInstruction(0x3f, 1, 2, 0));

  std::uint64_t value = 0;
  REQUIRE(system.eeBus().readData64(address, &value));
  REQUIRE(value == UINT64_C(0x76543210fedcba98));
}

TEST_CASE("EE aligned doubleword loads execute in branch delay slots")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  setRegister(&core, 1, 0x100);
  REQUIRE(
    system.eeBus().writeData64(
      0x100,
      UINT64_C(0x123456789abcdef0)));
  system.eeBus().write32(
    0,
    memoryInstruction(0x04, 0, 0, 2));
  system.eeBus().write32(
    4,
    memoryInstruction(0x37, 1, 2, 0));
  system.eeBus().write32(
    12,
    memoryInstruction(0x0d, 0, 3, 1));
  core.startExecution(0);
  system.runMasterCycles(3);

  REQUIRE(
    core.generalRegister(2).low ==
    UINT64_C(0x123456789abcdef0));
  REQUIRE(core.generalRegister(3).low == 1);
}
