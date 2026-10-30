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

TEST_CASE("EE SH translates DEV9 initialization aliases and captures the low halfword",
  "[ee][dev9-init]")
{
  for (std::uint32_t address : {0x9f801470u, 0xbf801470u, 0x9f801472u, 0xbf801472u})
  {
    for (std::uint64_t value : {UINT64_C(1), UINT64_C(0x12340001),
         UINT64_C(0), UINT64_C(0x12340000), UINT64_C(2)})
    {
      CAPTURE(address);
      CAPTURE(value);
      NekoSystem system;
      EECore &core = system.eeCore();
      setRegister(&core, 1, address);
      setRegister(&core, 2, value);
      runInstruction(&system, memoryInstruction(0x29, 1, 2, 0));
      const bool accepted = static_cast<std::uint16_t>(value) <=
        ((address & 2u) != 0 ? 1 : 0);
      REQUIRE(core.pendingException() ==
        (accepted ? EEException::None : EEException::DataBusErrorStore));
      if (accepted)
      {
        REQUIRE(core.programCounter() == 4);
      }
      REQUIRE(core.generalRegister(2).low == value);
      REQUIRE(core.generalRegister(2).high == UINT64_C(0xfeedfacecafebeef));
    }
  }
}

TEST_CASE("EE SH translates BA0 halfword aliases and captures the low halfword",
  "[ee][guest][trace][ba0-init][ba0-control][ba0-staging][ba0-command][ba0-read][ba0-read7f]")
{
  std::uint32_t physical = 0x1a000006;
  std::uint16_t selected = 3;
  std::uint8_t base = 2;
  std::uint16_t control = 0x42;
  std::uint16_t selector = 0x7e;
  bool staging = false;
  SECTION("status acknowledgement") {}
  SECTION("selector command")
  {
    physical = 0x1a000000;
    selected = 0x81;
  }
  SECTION("control selection")
  {
    physical = 0x1a000002;
    selected = 0x42;
    base = 3;
  }
  SECTION("read control selection")
  {
    physical = 0x1a000002;
    selected = 0x43;
    base = 3;
  }
  SECTION("device read command")
  {
    physical = 0x1a000000;
    selected = 0x81;
    control = 0x43;
  }
  SECTION("zero device read command")
  {
    physical = 0x1a000000;
    selected = 0x81;
    control = 0x43;
    selector = 0x7f;
  }
  SECTION("first staging halfword")
  {
    physical = 0x1a000010;
    selected = 0x7e;
    staging = true;
  }
  SECTION("last staging halfword")
  {
    physical = 0x1a00001e;
    selected = 0x7e;
    staging = true;
  }
  for (const std::uint32_t address :
       {physical | EEMemoryMap::KSEG0_BASE, physical | EEMemoryMap::KSEG1_BASE})
  {
    for (const std::uint64_t value :
         {std::uint64_t(selected), UINT64_C(0x12340000) | selected,
         UINT64_C(2)})
    {
      CAPTURE(address);
      CAPTURE(value);
      NekoSystem traced;
      NekoSystem plain;
      const auto half = static_cast<std::uint16_t>(value);
      const bool accepted = staging || half == selected;
      for (auto *system : {&traced, &plain})
      {
        auto &core = system->eeCore();
        if (physical == EEMemoryMap::BA0_COMMAND_00)
        {
          REQUIRE(system->eeBus().writeData16(EEMemoryMap::BA0_CONTROL_02, 0x42));
          REQUIRE(system->eeBus().writeData16(EEMemoryMap::BA0_TRANSFER_BEGIN, selector));
          if (control == 0x43)
          {
            REQUIRE(system->eeBus().writeData16(EEMemoryMap::BA0_COMMAND_00, 0x81));
            REQUIRE(system->eeBus().writeData16(EEMemoryMap::BA0_STATUS_06, 3));
            REQUIRE(system->eeBus().writeData16(EEMemoryMap::BA0_CONTROL_02, control));
          }
        }
        system->eeBus().write32(0, memoryInstruction(0x29, base, 4, 0));
        setRegister(&core, base, address);
        setRegister(&core, 4, value);
        core.setCOP0Register(EECOP0Register::Status, 0);
        core.startExecution(EEMemoryMap::KSEG1_BASE);
        if (system == &traced)
        {
          system->startTrace(100);
        }
        const auto result = system->stepEEInstruction(16);
        REQUIRE(result.pendingException ==
          (accepted ? EEException::None : EEException::DataBusErrorStore));
        REQUIRE_FALSE(result.cycleLimitReached);
        REQUIRE(core.programCounter() ==
          (accepted ? EEMemoryMap::KSEG1_BASE + 4
                    : EEExceptionVector::GENERAL));
        REQUIRE(core.generalRegister(4).low == value);
        REQUIRE(core.generalRegister(4).high == UINT64_C(0xfeedfacecafebeef));
        if (physical == EEMemoryMap::BA0_COMMAND_00)
        {
          std::uint16_t status = 0xffff;
          REQUIRE(system->eeBus().readData16(EEMemoryMap::BA0_STATUS_06, &status));
          REQUIRE(status == (accepted ? 2 : 0));
          if (control == 0x43)
          {
            std::uint16_t result = 0;
            REQUIRE(system->eeBus().readData16(EEMemoryMap::BA0_TRANSFER_BEGIN, &result));
            const std::uint16_t response = selector == 0x7e ? 0x1c : 0;
            REQUIRE(result == (accepted ? response : selector));
          }
        }
        if (staging)
        {
          std::uint16_t retained = 0;
          REQUIRE(system->eeBus().readData16(physical, &retained));
          REQUIRE(retained == half);
        }
      }
      std::size_t stores = 0;
      for (const auto &event : traced.trace())
      {
        if (event.subsystem == NekoTraceSubsystem::EE &&
            event.type == NekoTraceEventType::MemoryAccess &&
            event.value0 == address)
        {
          ++stores;
          REQUIRE(event.value1 == half);
          REQUIRE(event.value2 == 0);
          const bool cached = address < EEMemoryMap::KSEG1_BASE;
          REQUIRE(event.value3 == nekoPackEEMemoryTraceMetadata({
            physical, 2, NekoEETraceMemory::AccessKind::DataStore,
            NekoEETraceMemory::TranslationOutcome::Translated,
            cached ? NekoEETraceMemory::CacheRoute::CachedNoncoherent
                   : NekoEETraceMemory::CacheRoute::Uncached,
            static_cast<std::uint8_t>(cached ? 3 : 2), false, true,
            NekoEETraceMemory::CacheAccess::Bypassed,
            accepted ? NekoEETraceMemory::TransferOutcome::Completed
                     : NekoEETraceMemory::TransferOutcome::PhysicalBusError,
            accepted ? NekoEETraceMemory::FailurePhase::None
                     : NekoEETraceMemory::FailurePhase::Direct}));
        }
      }
      REQUIRE(stores == 1);
      REQUIRE_FALSE(traced.traceTruncated());
      REQUIRE(traced.eeStateHash() == plain.eeStateHash());
    }
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

  SECTION("LHU zero extends through a kernel alias")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    setRegister(&core, 1, 0x80000100);
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
  system.runMasterCycles(1);

  REQUIRE(
    core.pendingException() ==
    EEException::AddressErrorLoadOrFetch);
  REQUIRE(core.exceptionAddress() == 0x101);
  REQUIRE(
    core.programCounter() ==
    EEExceptionVector::BOOTSTRAP_GENERAL);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Cause) &
      EECOP0Cause::BRANCH_DELAY) != 0);
}
