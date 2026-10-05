#include <algorithm>
#include <cstdint>

#include "catch.hpp"
#include "ee_core.hpp"
#include "neko_system.hpp"

namespace
{
  constexpr std::uint32_t SCRATCHPAD_VIRTUAL_BASE =
    UINT32_C(0x50000000);
  constexpr std::uint32_t SECOND_SCRATCHPAD_ALIAS =
    UINT32_C(0x60000000);

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

  void mapScratchpad(
    EECore *core,
    std::size_t index = 0,
    std::uint32_t virtualBase = SCRATCHPAD_VIRTUAL_BASE)
  {
    core->setCOP0Register(EECOP0Register::Status, 0);
    core->setTLBEntry(
      index,
      {
        EECOP0PageMask::SIZE_16_KIB,
        virtualBase,
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
          (event.value3 & NekoEETraceMemory::SCRATCHPAD_ROUTE) != 0 &&
          (event.value3 &
            NekoEETraceMemory::PHYSICAL_ADDRESS_VALID) != 0 &&
          static_cast<std::uint32_t>(
            event.value3 >>
            NekoEETraceMemory::PHYSICAL_ADDRESS_SHIFT) ==
            (address & UINT32_C(0x3fff)) &&
          (event.value3 & NekoEETraceMemory::LEGACY_MASK) ==
            flags;
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

TEST_CASE("EE GPR quadword accesses route through scratchpad storage")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  mapScratchpad(&core);
  const EERegister128 expected = {
    UINT64_C(0x7766554433221100),
    UINT64_C(0xffeeddccbbaa9988)
  };
  const EEQuadword busSentinel = {
    UINT64_C(0x0123456789abcdef),
    UINT64_C(0xfedcba9876543210)
  };
  REQUIRE(system.eeBus().writeData128(0x300, busSentinel));

  core.setGeneralRegister(
    1,
    {SCRATCHPAD_VIRTUAL_BASE + 0x30f, 0});
  core.setGeneralRegister(2, expected);
  runMemoryInstruction(&system, 0x1f);

  EEQuadword busValue = {};
  REQUIRE(system.eeBus().readData128(0x300, &busValue));
  REQUIRE(busValue.low == busSentinel.low);
  REQUIRE(busValue.high == busSentinel.high);

  core.setGeneralRegister(2, {});
  runMemoryInstruction(&system, 0x1e);
  REQUIRE(core.generalRegister(2) == expected);
}

TEST_CASE("EE COP2 quadword accesses route through scratchpad storage")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  mapScratchpad(&core);
  const EEQuadword busSentinel = {
    UINT64_C(0x0123456789abcdef),
    UINT64_C(0xfedcba9876543210)
  };
  REQUIRE(system.eeBus().writeData128(0x320, busSentinel));

  core.setGeneralRegister(
    1,
    {SCRATCHPAD_VIRTUAL_BASE + 0x320, 0});
  system.vu0().loadFPRegisterBits(
    3,
    UINT32_C(0x33221100),
    UINT32_C(0x77665544),
    UINT32_C(0xbbaa9988),
    UINT32_C(0xffeeddcc));
  runInstruction(
    &system,
    memoryInstruction(0x3e, 1, 3, 0));

  EEQuadword busValue = {};
  REQUIRE(system.eeBus().readData128(0x320, &busValue));
  REQUIRE(busValue.low == busSentinel.low);
  REQUIRE(busValue.high == busSentinel.high);

  system.vu0().loadFPRegisterBits(4, 0, 0, 0, 0);
  runInstruction(
    &system,
    memoryInstruction(0x36, 1, 4, 0));
  const FPRegister *loaded = system.vu0().fpRegisterValue(4);
  REQUIRE(loaded->x.bits() == UINT32_C(0x33221100));
  REQUIRE(loaded->y.bits() == UINT32_C(0x77665544));
  REQUIRE(loaded->z.bits() == UINT32_C(0xbbaa9988));
  REQUIRE(loaded->w.bits() == UINT32_C(0xffeeddcc));
}

TEST_CASE("EE delayed COP1 memory accesses route through scratchpad storage")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  mapScratchpad(&core);
  core.setCOP0Register(
    EECOP0Register::Status,
    EECOP0Status::COP1_USABLE);
  REQUIRE(
    system.eeBus().writeData64(
      0x340,
      UINT64_C(0x01234567deadbeef)));

  setRegister(&core, 1, SCRATCHPAD_VIRTUAL_BASE + 0x340);
  setRegister(&core, 2, UINT32_C(0x89abcdef));
  runMemoryInstruction(&system, 0x2b);

  core.setFloatingPointRegister(3, 0);
  runInstruction(
    &system,
    memoryInstruction(0x31, 1, 3, 0));
  REQUIRE(core.floatingPointRegister(3) == 0);
  system.runMasterCycles(3);
  REQUIRE(
    core.floatingPointRegister(3) ==
    UINT32_C(0x89abcdef));

  core.setFloatingPointRegister(4, UINT32_C(0x76543210));
  runInstruction(
    &system,
    memoryInstruction(0x39, 1, 4, 4));
  system.runMasterCycles(3);
  setRegister(&core, 2, 0);
  runMemoryInstruction(&system, 0x27, 4);
  REQUIRE(
    core.generalRegister(2).low ==
    UINT32_C(0x76543210));

  std::uint64_t busValue = 0;
  REQUIRE(system.eeBus().readData64(0x340, &busValue));
  REQUIRE(busValue == UINT64_C(0x01234567deadbeef));
}

TEST_CASE("EE scratchpad aliases and SPR DMA share overlapping storage")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  EEBus &bus = system.eeBus();
  mapScratchpad(&core);
  mapScratchpad(&core, 1, SECOND_SCRATCHPAD_ALIAS);
  REQUIRE(
    bus.writeData64(
      0x180,
      UINT64_C(0xfedcba9876543210)));

  setRegister(&core, 1, SCRATCHPAD_VIRTUAL_BASE + 0x180);
  setRegister(&core, 2, UINT64_C(0x0123456789abcdef));
  runMemoryInstruction(&system, 0x3f);

  setRegister(
    &core,
    1,
    SECOND_SCRATCHPAD_ALIAS + 0x180);
  setRegister(&core, 2, 0);
  runMemoryInstruction(&system, 0x37);
  REQUIRE(
    core.generalRegister(2).low ==
    UINT64_C(0x0123456789abcdef));

  std::uint64_t mainMemory = 0;
  REQUIRE(bus.readData64(0x180, &mainMemory));
  REQUIRE(mainMemory == UINT64_C(0xfedcba9876543210));

  bus.write32(
    EEMemoryMap::D_CTRL,
    DMACControl::DMA_ENABLE);
  bus.write32(EEMemoryMap::D8_MADR, 0x200);
  bus.write32(EEMemoryMap::D8_QWC, 1);
  bus.write32(EEMemoryMap::D8_SADR, 0x180);
  bus.write32(
    EEMemoryMap::D8_CHCR,
    DMACChannelControl::START);
  system.clockMasterCycle();

  EEQuadword transferred = {};
  REQUIRE(bus.readDMAC128(0x200, &transferred));
  REQUIRE(
    transferred.low ==
    UINT64_C(0x0123456789abcdef));

  const EEQuadword replacement = {
    UINT64_C(0x8877665544332211),
    UINT64_C(0xffeeddccbbaa9988)
  };
  REQUIRE(bus.writeDMAC128(0x300, replacement));
  bus.write32(EEMemoryMap::D9_MADR, 0x300);
  bus.write32(EEMemoryMap::D9_QWC, 1);
  bus.write32(EEMemoryMap::D9_SADR, 0x180);
  bus.write32(
    EEMemoryMap::D9_CHCR,
    DMACChannelControl::START);
  system.clockMasterCycle();

  setRegister(&core, 1, SCRATCHPAD_VIRTUAL_BASE + 0x180);
  setRegister(&core, 2, 0);
  runMemoryInstruction(&system, 0x37);
  REQUIRE(core.generalRegister(2).low == replacement.low);
  REQUIRE(bus.readData64(0x180, &mainMemory));
  REQUIRE(mainMemory == UINT64_C(0xfedcba9876543210));
}

TEST_CASE("EE scratchpad aliases expose every implemented access width")
{
  struct ScalarAccess
  {
    std::uint8_t storeOpcode;
    std::uint8_t loadOpcode;
    std::uint16_t offset;
    std::uint64_t value;
  };
  const ScalarAccess accesses[] = {
    {0x28, 0x24, 0x100, UINT64_C(0x5a)},
    {0x29, 0x25, 0x102, UINT64_C(0xa55a)},
    {0x2b, 0x27, 0x104, UINT64_C(0x89abcdef)},
    {0x3f, 0x37, 0x108, UINT64_C(0x0123456789abcdef)}
  };
  NekoSystem system;
  EECore &core = system.eeCore();
  mapScratchpad(&core);
  mapScratchpad(&core, 1, SECOND_SCRATCHPAD_ALIAS);

  for (const ScalarAccess &access : accesses)
  {
    setRegister(
      &core,
      1,
      SCRATCHPAD_VIRTUAL_BASE + access.offset);
    setRegister(&core, 2, access.value);
    runMemoryInstruction(&system, access.storeOpcode);
    setRegister(
      &core,
      1,
      SECOND_SCRATCHPAD_ALIAS + access.offset);
    setRegister(&core, 2, 0);
    runMemoryInstruction(&system, access.loadOpcode);
    REQUIRE(core.generalRegister(2).low == access.value);
  }

  const EERegister128 quadword = {
    UINT64_C(0x7766554433221100),
    UINT64_C(0xffeeddccbbaa9988)
  };
  core.setGeneralRegister(
    1,
    {SCRATCHPAD_VIRTUAL_BASE + 0x20f, 0});
  core.setGeneralRegister(2, quadword);
  runMemoryInstruction(&system, 0x1f);
  core.setGeneralRegister(
    1,
    {SECOND_SCRATCHPAD_ALIAS + 0x20f, 0});
  core.setGeneralRegister(2, {});
  runMemoryInstruction(&system, 0x1e);
  REQUIRE(core.generalRegister(2) == quadword);
}

TEST_CASE("EE scratchpad CPU and DMA execution repeats after reset")
{
  const auto execute =
    [](NekoSystem *system)
    {
      EECore &core = system->eeCore();
      EEBus &bus = system->eeBus();
      mapScratchpad(&core);
      setRegister(
        &core,
        1,
        SCRATCHPAD_VIRTUAL_BASE + 0x220);
      setRegister(
        &core,
        2,
        UINT64_C(0x0123456789abcdef));
      runMemoryInstruction(system, 0x3f);
      bus.write32(
        EEMemoryMap::D_CTRL,
        DMACControl::DMA_ENABLE);
      bus.write32(EEMemoryMap::D8_MADR, 0x400);
      bus.write32(EEMemoryMap::D8_QWC, 1);
      bus.write32(EEMemoryMap::D8_SADR, 0x220);
      bus.write32(
        EEMemoryMap::D8_CHCR,
        DMACChannelControl::START);
      system->clockMasterCycle();
    };

  NekoSystem system;
  execute(&system);
  const std::vector<std::uint8_t> firstState =
    system.saveState();
  const std::uint64_t firstHash = system.eeStateHash();
  EEQuadword firstDestination = {};
  REQUIRE(
    system.eeBus().readDMAC128(
      0x400,
      &firstDestination));

  system.reset();
  execute(&system);

  EEQuadword secondDestination = {};
  REQUIRE(
    system.eeBus().readDMAC128(
      0x400,
      &secondDestination));
  REQUIRE(secondDestination.low == firstDestination.low);
  REQUIRE(secondDestination.high == firstDestination.high);
  REQUIRE(system.eeStateHash() == firstHash);
  REQUIRE(system.saveState() == firstState);
}
