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

  void runInstruction(
    NekoSystem *system,
    std::uint32_t instruction)
  {
    system->eeBus().write32(0, instruction);
    system->eeCore().startExecution(0);
    system->clockMasterCycle();
  }
}

TEST_CASE("EE LQ replaces the complete 128-bit GPR")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(
    1,
    {UINT64_C(0x123400000000011f), 0});
  core.setGeneralRegister(2, {UINT64_MAX, UINT64_MAX});
  REQUIRE(
    system.eeBus().writeData128(
      0x110,
      {
        UINT64_C(0x7766554433221100),
        UINT64_C(0xffeeddccbbaa9988)
      }));

  runInstruction(
    &system,
    memoryInstruction(0x1e, 1, 2, 0xfff8));

  REQUIRE(
    core.generalRegister(2).low ==
    UINT64_C(0x7766554433221100));
  REQUIRE(
    core.generalRegister(2).high ==
    UINT64_C(0xffeeddccbbaa9988));
}

TEST_CASE("EE SQ stores the complete 128-bit GPR")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(1, {0xa000011f, 0});
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0x7766554433221100),
      UINT64_C(0xffeeddccbbaa9988)
    });

  runInstruction(
    &system,
    memoryInstruction(0x1f, 1, 2, 0));

  const std::uint8_t expected[16] = {
    0x00, 0x11, 0x22, 0x33,
    0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xaa, 0xbb,
    0xcc, 0xdd, 0xee, 0xff
  };
  for (std::uint8_t index = 0; index < 16; ++index)
  {
    std::uint8_t value = 0;
    REQUIRE(system.eeBus().readData8(0x110 + index, &value));
    REQUIRE(value == expected[index]);
  }
}

TEST_CASE("EE LQ and SQ mask the low effective-address bits")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(1, {0x10f, 0});
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0x0123456789abcdef),
      UINT64_C(0xfedcba9876543210)
    });

  runInstruction(
    &system,
    memoryInstruction(0x1f, 1, 2, 0));

  REQUIRE(core.pendingException() == EEException::None);
  EEQuadword value;
  REQUIRE(system.eeBus().readData128(0x100, &value));
  REQUIRE(value.low == UINT64_C(0x0123456789abcdef));
  REQUIRE(value.high == UINT64_C(0xfedcba9876543210));
}

TEST_CASE("EE quadword faults preserve architectural state")
{
  SECTION("An unmapped LQ preserves its destination")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      1,
      {EEMemoryMap::MAIN_MEMORY_SIZE + 15, 0});
    core.setGeneralRegister(
      2,
      {
        UINT64_C(0x1122334455667788),
        UINT64_C(0x99aabbccddeeff00)
      });

    runInstruction(
      &system,
      memoryInstruction(0x1e, 1, 2, 0));

    REQUIRE(core.pendingException() == EEException::DataBusErrorLoad);
    REQUIRE(
      core.exceptionAddress() ==
      EEMemoryMap::MAIN_MEMORY_SIZE);
    REQUIRE((
      core.generalRegister(2) ==
      EERegister128{
        UINT64_C(0x1122334455667788),
        UINT64_C(0x99aabbccddeeff00)
      }));
  }

  SECTION("An unmapped SQ reports the masked address")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(
      1,
      {EEMemoryMap::MAIN_MEMORY_SIZE + 7, 0});
    core.setGeneralRegister(2, {UINT64_MAX, UINT64_MAX});

    runInstruction(
      &system,
      memoryInstruction(0x1f, 1, 2, 0));

    REQUIRE(core.pendingException() == EEException::DataBusErrorStore);
    REQUIRE(
      core.exceptionAddress() ==
      EEMemoryMap::MAIN_MEMORY_SIZE);
  }
}

TEST_CASE("EE quadword access reaches the exact RAM boundary")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(
    1,
    {EEMemoryMap::MAIN_MEMORY_SIZE - 1, 0});
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0x0123456789abcdef),
      UINT64_C(0xfedcba9876543210)
    });

  runInstruction(
    &system,
    memoryInstruction(0x1f, 1, 2, 0));

  EEQuadword value;
  REQUIRE(
    system.eeBus().readData128(
      EEMemoryMap::MAIN_MEMORY_SIZE - 16,
      &value));
  REQUIRE(value.low == UINT64_C(0x0123456789abcdef));
  REQUIRE(value.high == UINT64_C(0xfedcba9876543210));
}

TEST_CASE("EE LQ keeps register zero immutable")
{
  NekoSystem system;
  REQUIRE(
    system.eeBus().writeData128(
      0x100,
      {UINT64_MAX, UINT64_MAX}));
  system.eeCore().setGeneralRegister(1, {0x100, 0});

  runInstruction(
    &system,
    memoryInstruction(0x1e, 1, 0, 0));

  REQUIRE(
    system.eeCore().generalRegister(0) ==
    EERegister128{});
}

TEST_CASE("EE LQ executes in a branch delay slot")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(1, {0x107, 0});
  REQUIRE(
    system.eeBus().writeData128(
      0x100,
      {
        UINT64_C(0x0123456789abcdef),
        UINT64_C(0xfedcba9876543210)
      }));
  system.eeBus().write32(
    0,
    memoryInstruction(0x04, 0, 0, 2));
  system.eeBus().write32(
    4,
    memoryInstruction(0x1e, 1, 2, 0));
  system.eeBus().write32(
    12,
    memoryInstruction(0x0d, 0, 3, 1));
  core.startExecution(0);
  system.runMasterCycles(3);

  REQUIRE((
    core.generalRegister(2) ==
    EERegister128{
      UINT64_C(0x0123456789abcdef),
      UINT64_C(0xfedcba9876543210)
    }));
  REQUIRE(core.generalRegister(3).low == 1);
}
