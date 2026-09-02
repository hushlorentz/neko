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

TEST_CASE("EE signed and unsigned halfword loads")
{
  SECTION("LH sign extends a little-endian halfword")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, UINT64_C(0x1234000000000102));
    setRegister(&core, 2, 0);
    REQUIRE(system.eeBus().writeData16(0x100, 0x8001));

    runInstruction(
      &system,
      memoryInstruction(0x21, 1, 2, 0xfffe));

    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0xffffffffffff8001));
    REQUIRE(
      core.generalRegister(2).high ==
      UINT64_C(0xfeedfacecafebeef));
  }

  SECTION("LHU zero extends through a RAM alias")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, 0x20000100);
    setRegister(&core, 2, UINT64_MAX);
    REQUIRE(system.eeBus().writeData16(0x100, 0xfedc));

    runInstruction(
      &system,
      memoryInstruction(0x25, 1, 2, 0));

    REQUIRE(core.generalRegister(2).low == 0xfedc);
    REQUIRE(
      core.generalRegister(2).high ==
      UINT64_C(0xfeedfacecafebeef));
  }
}

TEST_CASE("EE halfword stores use the low 16 bits")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  setRegister(&core, 1, 0xa0000100);
  setRegister(&core, 2, UINT64_C(0x112233445566abcd));

  runInstruction(
    &system,
    memoryInstruction(0x29, 1, 2, 2));

  std::uint8_t low = 0;
  std::uint8_t high = 0;
  REQUIRE(system.eeBus().readData8(0x102, &low));
  REQUIRE(system.eeBus().readData8(0x103, &high));
  REQUIRE(low == 0xcd);
  REQUIRE(high == 0xab);
}

TEST_CASE("EE halfword alignment faults have no data side effects")
{
  SECTION("A misaligned load preserves its destination")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, 0x101);
    setRegister(&core, 2, 0x1234);

    runInstruction(
      &system,
      memoryInstruction(0x21, 1, 2, 0));

    REQUIRE(
      core.pendingException() ==
      EEException::AddressErrorLoadOrFetch);
    REQUIRE(core.exceptionAddress() == 0x101);
    REQUIRE(
      core.programCounter() ==
      EEExceptionVector::BOOTSTRAP_GENERAL);
    REQUIRE(core.generalRegister(2).low == 0x1234);
  }

  SECTION("A misaligned store performs no partial write")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, 0x101);
    setRegister(&core, 2, 0xabcd);
    REQUIRE(system.eeBus().writeData8(0x101, 0x11));
    REQUIRE(system.eeBus().writeData8(0x102, 0x22));

    runInstruction(
      &system,
      memoryInstruction(0x29, 1, 2, 0));

    REQUIRE(
      core.pendingException() ==
      EEException::AddressErrorStore);
    std::uint8_t first = 0;
    std::uint8_t second = 0;
    REQUIRE(system.eeBus().readData8(0x101, &first));
    REQUIRE(system.eeBus().readData8(0x102, &second));
    REQUIRE(first == 0x11);
    REQUIRE(second == 0x22);
  }
}

TEST_CASE("EE halfword alignment faults restart branch delay slots")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  setRegister(&core, 1, 0x101);
  system.eeBus().write32(
    0,
    memoryInstruction(0x04, 0, 0, 2));
  system.eeBus().write32(
    4,
    memoryInstruction(0x25, 1, 2, 0));
  core.startExecution(0);
  system.runMasterCycles(2);

  REQUIRE(
    core.pendingException() ==
    EEException::AddressErrorLoadOrFetch);
  REQUIRE(core.exceptionAddress() == 0x101);
  REQUIRE(
    core.programCounter() ==
    EEExceptionVector::BOOTSTRAP_GENERAL);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
}
