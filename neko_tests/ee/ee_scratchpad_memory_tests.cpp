#include <algorithm>
#include <cstdint>

#include "catch.hpp"
#include "ee_core.hpp"
#include "neko_system.hpp"

namespace
{
  constexpr std::uint32_t SCRATCHPAD_VIRTUAL_BASE =
    UINT32_C(0x50000000);

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

  void mapScratchpad(EECore *core)
  {
    core->setCOP0Register(EECOP0Register::Status, 0);
    core->setTLBEntry(
      0,
      {
        EECOP0PageMask::SIZE_16_KIB,
        SCRATCHPAD_VIRTUAL_BASE,
        {
          EECOP0EntryLo::SCRATCHPAD |
          EECOP0EntryLo::DIRTY |
          EECOP0EntryLo::VALID
        },
        {
          EECOP0EntryLo::SCRATCHPAD |
          EECOP0EntryLo::DIRTY |
          EECOP0EntryLo::VALID
        }
      });
  }

  void runInstruction(
    NekoSystem *system,
    std::uint32_t instruction)
  {
    system->eeBus().write32(0, instruction);
    system->eeCore().startExecution(EEMemoryMap::KSEG0_BASE);
    system->clockMasterCycle();
    REQUIRE(
      system->eeCore().pendingException() ==
      EEException::None);
  }

  void runMemoryInstruction(
    NekoSystem *system,
    std::uint8_t opcode,
    std::uint16_t offset = 0)
  {
    runInstruction(
      system,
      memoryInstruction(opcode, 1, 2, offset));
  }

  bool hasMemoryTrace(
    const NekoSystem &system,
    std::uint32_t address,
    std::uint8_t width,
    bool write,
    std::uint64_t value)
  {
    const std::uint64_t flags =
      width |
      (write ? NekoEETraceMemory::WRITE : 0) |
      NekoEETraceMemory::SUCCEEDED;
    return std::any_of(
      system.trace().begin(),
      system.trace().end(),
      [address, flags, value](const NekoTraceEvent &event)
      {
        return
          event.subsystem == NekoTraceSubsystem::EE &&
          event.type == NekoTraceEventType::MemoryAccess &&
          event.value0 == address &&
          event.value1 == value &&
          event.value3 == flags;
      });
  }
}

TEST_CASE("EE scalar memory accesses route through scratchpad storage")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  mapScratchpad(&core);
  REQUIRE(
    system.eeBus().writeData64(
      0x100,
      UINT64_C(0x0123456789abcdef)));
  REQUIRE(
    system.eeBus().writeData64(
      0x108,
      UINT64_C(0xfedcba9876543210)));
  system.startTrace();

  setRegister(&core, 1, SCRATCHPAD_VIRTUAL_BASE + 0x100);
  setRegister(&core, 2, 0x80);
  runMemoryInstruction(&system, 0x28);
  setRegister(&core, 2, 0x8001);
  runMemoryInstruction(&system, 0x29, 2);
  setRegister(&core, 2, UINT32_C(0x80000001));
  runMemoryInstruction(&system, 0x2b, 4);
  setRegister(&core, 2, UINT64_C(0x8877665580000180));
  runMemoryInstruction(&system, 0x3f, 8);

  std::uint64_t busValue = 0;
  REQUIRE(system.eeBus().readData64(0x100, &busValue));
  REQUIRE(busValue == UINT64_C(0x0123456789abcdef));
  REQUIRE(system.eeBus().readData64(0x108, &busValue));
  REQUIRE(busValue == UINT64_C(0xfedcba9876543210));

  setRegister(&core, 2, 0);
  runMemoryInstruction(&system, 0x20);
  REQUIRE(
    core.generalRegister(2).low ==
    UINT64_C(0xffffffffffffff80));
  runMemoryInstruction(&system, 0x24);
  REQUIRE(core.generalRegister(2).low == 0x80);

  runMemoryInstruction(&system, 0x21, 2);
  REQUIRE(
    core.generalRegister(2).low ==
    UINT64_C(0xffffffffffff8001));
  runMemoryInstruction(&system, 0x25, 2);
  REQUIRE(core.generalRegister(2).low == 0x8001);

  runMemoryInstruction(&system, 0x23, 4);
  REQUIRE(
    core.generalRegister(2).low ==
    UINT64_C(0xffffffff80000001));
  runMemoryInstruction(&system, 0x27, 4);
  REQUIRE(
    core.generalRegister(2).low ==
    UINT32_C(0x80000001));

  runMemoryInstruction(&system, 0x37, 8);
  REQUIRE(
    core.generalRegister(2).low ==
    UINT64_C(0x8877665580000180));
  REQUIRE(
    core.generalRegister(2).high ==
    UINT64_C(0xfeedfacecafebeef));
  REQUIRE(
    hasMemoryTrace(
      system,
      SCRATCHPAD_VIRTUAL_BASE + 0x104,
      4,
      false,
      UINT32_C(0x80000001)));
  REQUIRE(
    hasMemoryTrace(
      system,
      SCRATCHPAD_VIRTUAL_BASE + 0x108,
      8,
      true,
      UINT64_C(0x8877665580000180)));
}

TEST_CASE("EE word merge accesses route through scratchpad storage")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  mapScratchpad(&core);
  REQUIRE(
    system.eeBus().writeData64(
      0x100,
      UINT64_C(0xfedcba9876543210)));

  setRegister(&core, 1, SCRATCHPAD_VIRTUAL_BASE + 0x100);
  setRegister(&core, 2, UINT32_C(0x33221100));
  runMemoryInstruction(&system, 0x2b);
  setRegister(&core, 2, UINT32_C(0x88776684));
  runMemoryInstruction(&system, 0x2b, 4);

  setRegister(&core, 1, SCRATCHPAD_VIRTUAL_BASE + 0x101);
  setRegister(&core, 2, UINT64_C(0x5566778899aabbcc));
  runMemoryInstruction(&system, 0x22, 3);
  runMemoryInstruction(&system, 0x26);
  REQUIRE(
    core.generalRegister(2).low ==
    UINT64_C(0xffffffff84332211));

  setRegister(&core, 2, UINT32_C(0xa1b2c3d4));
  runMemoryInstruction(&system, 0x2a, 3);
  runMemoryInstruction(&system, 0x2e);

  setRegister(&core, 1, SCRATCHPAD_VIRTUAL_BASE + 0x100);
  runMemoryInstruction(&system, 0x27);
  REQUIRE(core.generalRegister(2).low == UINT32_C(0xb2c3d400));
  runMemoryInstruction(&system, 0x27, 4);
  REQUIRE(core.generalRegister(2).low == UINT32_C(0x887766a1));

  std::uint64_t busValue = 0;
  REQUIRE(system.eeBus().readData64(0x100, &busValue));
  REQUIRE(busValue == UINT64_C(0xfedcba9876543210));
}

TEST_CASE("EE doubleword merge accesses route through scratchpad storage")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  mapScratchpad(&core);
  REQUIRE(
    system.eeBus().writeData64(
      0x200,
      UINT64_C(0x1122334455667788)));
  REQUIRE(
    system.eeBus().writeData64(
      0x208,
      UINT64_C(0x8877665544332211)));

  setRegister(&core, 1, SCRATCHPAD_VIRTUAL_BASE + 0x200);
  setRegister(&core, 2, UINT64_C(0x7060504030201000));
  runMemoryInstruction(&system, 0x3f);
  setRegister(&core, 2, UINT64_C(0xf0e0d0c0b0a09080));
  runMemoryInstruction(&system, 0x3f, 8);

  setRegister(&core, 1, SCRATCHPAD_VIRTUAL_BASE + 0x201);
  setRegister(&core, 2, 0);
  runMemoryInstruction(&system, 0x1a, 7);
  runMemoryInstruction(&system, 0x1b);
  REQUIRE(
    core.generalRegister(2).low ==
    UINT64_C(0x8070605040302010));

  setRegister(&core, 2, UINT64_C(0x8877665544332211));
  runMemoryInstruction(&system, 0x2c, 7);
  runMemoryInstruction(&system, 0x2d);

  setRegister(&core, 1, SCRATCHPAD_VIRTUAL_BASE + 0x200);
  runMemoryInstruction(&system, 0x37);
  REQUIRE(
    core.generalRegister(2).low ==
    UINT64_C(0x7766554433221100));
  runMemoryInstruction(&system, 0x37, 8);
  REQUIRE(
    core.generalRegister(2).low ==
    UINT64_C(0xf0e0d0c0b0a09088));

  std::uint64_t busValue = 0;
  REQUIRE(system.eeBus().readData64(0x200, &busValue));
  REQUIRE(busValue == UINT64_C(0x1122334455667788));
  REQUIRE(system.eeBus().readData64(0x208, &busValue));
  REQUIRE(busValue == UINT64_C(0x8877665544332211));
}
