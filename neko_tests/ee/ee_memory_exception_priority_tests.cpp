#include <cstdint>

#include "catch.hpp"
#include "ee_test_utils.hpp"
#include "neko_system.hpp"

namespace
{
  constexpr std::uint32_t DATA_VIRTUAL_BASE = UINT32_C(0x00400100);
  constexpr std::uint32_t DATA_VPN2 = UINT32_C(0x00400000);
  constexpr std::uint32_t DATA_PHYSICAL_BASE = UINT32_C(0x00000100);
  constexpr std::uint32_t INITIAL_CONTEXT = UINT32_C(0xabd23450);
  constexpr std::uint32_t INITIAL_ENTRY_HI = UINT32_C(0x89abc05a);
  constexpr std::uint32_t INITIAL_BAD_VADDR = UINT32_C(0x76543210);
  constexpr EERegister128 INITIAL_TARGET = {
    UINT64_C(0x1122334455667788),
    UINT64_C(0x99aabbccddeeff00)
  };

  struct MemoryAccessContract
  {
    const char *name;
    std::uint8_t loadOpcode;
    std::uint8_t storeOpcode;
    std::uint32_t effectiveOffset;
    std::uint32_t alignmentMask;
    bool masksToQuadword;
  };

  constexpr MemoryAccessContract ACCESS_CONTRACTS[] = {
    {"byte", 0x20, 0x28, 0, 0, false},
    {"halfword", 0x21, 0x29, 0, 1, false},
    {"word", 0x23, 0x2b, 0, 3, false},
    {"doubleword", 0x37, 0x3f, 0, 7, false},
    {"quadword", 0x1e, 0x1f, 7, 0, true},
    {"word merge", 0x22, 0x2e, 1, 0, false},
    {"doubleword merge", 0x1a, 0x2d, 3, 0, false}
  };

  std::uint32_t memoryInstruction(
    std::uint8_t opcode,
    std::uint8_t base,
    std::uint8_t target)
  {
    return
      (static_cast<std::uint32_t>(opcode) << 26) |
      (static_cast<std::uint32_t>(base) << 21) |
      (static_cast<std::uint32_t>(target) << 16);
  }

  std::uint32_t pageValue(
    std::uint32_t physicalBase,
    std::uint8_t cacheAttribute,
    bool valid,
    bool dirty)
  {
    return
      (physicalBase >> 6) |
      (static_cast<std::uint32_t>(cacheAttribute) << 3) |
      EECOP0EntryLo::GLOBAL |
      (valid ? EECOP0EntryLo::VALID : 0) |
      (dirty ? EECOP0EntryLo::DIRTY : 0);
  }

  void setExceptionSentinels(EECore *core)
  {
    core->setCOP0Register(EECOP0Register::Context, INITIAL_CONTEXT);
    core->setCOP0Register(EECOP0Register::EntryHi, INITIAL_ENTRY_HI);
    core->setCOP0Register(EECOP0Register::BadVAddr, INITIAL_BAD_VADDR);
  }

  void requireMemorySystemSentinels(const EECore &core)
  {
    REQUIRE(
      core.cop0Register(EECOP0Register::Context) ==
      INITIAL_CONTEXT);
    REQUIRE(
      core.cop0Register(EECOP0Register::EntryHi) ==
      INITIAL_ENTRY_HI);
  }

  void initializeMemory(EEBus *bus)
  {
    for (std::uint32_t offset = 0; offset < 16; ++offset)
    {
      REQUIRE(
        bus->writeData8(
          DATA_PHYSICAL_BASE + offset,
          static_cast<std::uint8_t>(0xa0 + offset)));
    }
  }

  void requireMemoryUnchanged(const EEBus &bus)
  {
    for (std::uint32_t offset = 0; offset < 16; ++offset)
    {
      std::uint8_t value = 0;
      REQUIRE(
        bus.readData8(
          DATA_PHYSICAL_BASE + offset,
          &value));
      REQUIRE(value == static_cast<std::uint8_t>(0xa0 + offset));
    }
  }

  void setDataTLBPage(
    EECore *core,
    std::uint32_t value)
  {
    core->setTLBEntry(
      0,
      {
        EECOP0PageMask::SIZE_4_KIB,
        DATA_VPN2 |
          (INITIAL_ENTRY_HI & EECOP0EntryHi::ASID_MASK),
        {value},
        {}
      });
  }

  void runDataAccess(
    NekoSystem *system,
    const MemoryAccessContract &contract,
    bool store,
    std::uint32_t virtualAddress,
    std::uint32_t status,
    bool mappedInstructionFetch)
  {
    EECore &core = system->eeCore();
    core.setCOP0Register(EECOP0Register::Status, status);
    if (mappedInstructionFetch)
    {
      mapLowKusegForTest(&core);
    }
    core.setGeneralRegister(1, {virtualAddress, 0});
    core.setGeneralRegister(2, INITIAL_TARGET);
    system->eeBus().write32(
      0,
      memoryInstruction(
        store ? contract.storeOpcode : contract.loadOpcode,
        1,
        2));
    core.startExecution(
      mappedInstructionFetch
        ? 0
        : EEMemoryMap::KSEG0_BASE);
    system->clockMasterCycle();
  }

  void requireNoDataMutation(
    const NekoSystem &system,
    bool store)
  {
    if (store)
    {
      requireMemoryUnchanged(system.eeBus());
    }
    else
    {
      REQUIRE(system.eeCore().generalRegister(2) == INITIAL_TARGET);
    }
  }

  enum class DataFaultStage
  {
    SegmentProtection,
    TLBRefill,
    TLBInvalid,
    TLBModified,
    UnsupportedCache,
    PhysicalBus
  };

  const char *stageName(DataFaultStage stage)
  {
    switch (stage)
    {
      case DataFaultStage::SegmentProtection:
        return "segment protection";
      case DataFaultStage::TLBRefill:
        return "TLB refill";
      case DataFaultStage::TLBInvalid:
        return "TLB invalid";
      case DataFaultStage::TLBModified:
        return "TLB modified";
      case DataFaultStage::UnsupportedCache:
        return "unsupported cache";
      case DataFaultStage::PhysicalBus:
        return "physical bus";
    }
    return "unknown";
  }

  EEException expectedException(
    DataFaultStage stage,
    bool store)
  {
    switch (stage)
    {
      case DataFaultStage::SegmentProtection:
        return store
          ? EEException::AddressErrorStore
          : EEException::AddressErrorLoadOrFetch;
      case DataFaultStage::TLBRefill:
        return store
          ? EEException::TLBRefillStore
          : EEException::TLBRefillLoadOrFetch;
      case DataFaultStage::TLBInvalid:
        return store
          ? EEException::TLBInvalidStore
          : EEException::TLBInvalidLoadOrFetch;
      case DataFaultStage::TLBModified:
        return EEException::TLBModified;
      case DataFaultStage::UnsupportedCache:
      case DataFaultStage::PhysicalBus:
        return store
          ? EEException::DataBusErrorStore
          : EEException::DataBusErrorLoad;
    }
    return EEException::None;
  }

  void requireDataFaultPriority(
    const MemoryAccessContract &contract,
    bool store,
    DataFaultStage stage)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    initializeMemory(&system.eeBus());
    setExceptionSentinels(&core);
    std::uint32_t virtualAddress =
      DATA_VIRTUAL_BASE + contract.effectiveOffset;
    std::uint32_t status = 0;
    bool mappedInstructionFetch = false;

    switch (stage)
    {
      case DataFaultStage::SegmentProtection:
        virtualAddress =
          EEMemoryMap::KSEG0_BASE + contract.effectiveOffset;
        status = EECOP0Status::USER_MODE;
        mappedInstructionFetch = true;
        break;
      case DataFaultStage::TLBRefill:
        break;
      case DataFaultStage::TLBInvalid:
        setDataTLBPage(
          &core,
          pageValue(
            EEMemoryMap::MAIN_MEMORY_SIZE,
            0,
            false,
            false));
        break;
      case DataFaultStage::TLBModified:
        setDataTLBPage(
          &core,
          pageValue(0, 0, true, false));
        break;
      case DataFaultStage::UnsupportedCache:
        setDataTLBPage(
          &core,
          pageValue(0, 0, true, true));
        break;
      case DataFaultStage::PhysicalBus:
        setDataTLBPage(
          &core,
          pageValue(
            EEMemoryMap::MAIN_MEMORY_SIZE,
            2,
            true,
            true));
        break;
    }

    runDataAccess(
      &system,
      contract,
      store,
      virtualAddress,
      status,
      mappedInstructionFetch);

    const std::uint32_t expectedFaultAddress =
      contract.masksToQuadword
        ? virtualAddress & ~UINT32_C(0x0f)
        : virtualAddress;
    REQUIRE(
      core.pendingException() ==
      expectedException(stage, store));
    REQUIRE(core.exceptionAddress() == expectedFaultAddress);
    if (stage == DataFaultStage::SegmentProtection ||
        stage == DataFaultStage::UnsupportedCache ||
        stage == DataFaultStage::PhysicalBus)
    {
      requireMemorySystemSentinels(core);
    }
    if (stage == DataFaultStage::UnsupportedCache ||
        stage == DataFaultStage::PhysicalBus)
    {
      REQUIRE(
        core.cop0Register(EECOP0Register::BadVAddr) ==
        INITIAL_BAD_VADDR);
    }
    else
    {
      REQUIRE(
        core.cop0Register(EECOP0Register::BadVAddr) ==
        expectedFaultAddress);
    }
    requireNoDataMutation(system, store);
  }
}

TEST_CASE("EE fetch fault priority follows the architectural pipeline")
{
  SECTION("Alignment precedes TLB lookup")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Status, 0);
    setExceptionSentinels(&core);
    core.startExecution(DATA_VIRTUAL_BASE + 2);

    system.clockMasterCycle();

    REQUIRE(
      core.pendingException() ==
      EEException::AddressErrorLoadOrFetch);
    REQUIRE(
      core.exceptionAddress() ==
      DATA_VIRTUAL_BASE + 2);
    requireMemorySystemSentinels(core);
  }

  SECTION("Segment protection precedes translation")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::USER_MODE);
    setExceptionSentinels(&core);
    core.startExecution(EEMemoryMap::KSEG0_BASE);

    system.clockMasterCycle();

    REQUIRE(
      core.pendingException() ==
      EEException::AddressErrorLoadOrFetch);
    requireMemorySystemSentinels(core);
  }

  SECTION("TLB validity precedes cache routing")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::USER_MODE);
    setExceptionSentinels(&core);
    setDataTLBPage(
      &core,
      pageValue(
        EEMemoryMap::MAIN_MEMORY_SIZE,
        0,
        false,
        false));
    core.startExecution(DATA_VIRTUAL_BASE);

    system.clockMasterCycle();

    REQUIRE(
      core.pendingException() ==
      EEException::TLBInvalidLoadOrFetch);
  }

  SECTION("Unsupported cache routing precedes the physical bus")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::USER_MODE);
    setExceptionSentinels(&core);
    setDataTLBPage(&core, pageValue(0, 0, true, true));
    system.eeBus().write32(DATA_PHYSICAL_BASE, 0);
    core.startExecution(DATA_VIRTUAL_BASE);

    system.clockMasterCycle();

    REQUIRE(
      core.pendingException() ==
      EEException::InstructionBusError);
    requireMemorySystemSentinels(core);
    REQUIRE(
      core.cop0Register(EECOP0Register::BadVAddr) ==
      INITIAL_BAD_VADDR);
  }

  SECTION("A supported route exposes a physical bus failure")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::USER_MODE);
    setExceptionSentinels(&core);
    setDataTLBPage(
      &core,
      pageValue(
        EEMemoryMap::MAIN_MEMORY_SIZE,
        2,
        true,
        true));
    core.startExecution(DATA_VIRTUAL_BASE);

    system.clockMasterCycle();

    REQUIRE(
      core.pendingException() ==
      EEException::InstructionBusError);
    requireMemorySystemSentinels(core);
    REQUIRE(
      core.cop0Register(EECOP0Register::BadVAddr) ==
      INITIAL_BAD_VADDR);
  }
}

TEST_CASE("EE aligned data faults precede translation for every scalar width")
{
  for (const MemoryAccessContract &contract : ACCESS_CONTRACTS)
  {
    if (contract.alignmentMask == 0)
    {
      continue;
    }
    for (const bool store : {false, true})
    {
      INFO(contract.name << (store ? " store" : " load"));
      NekoSystem system;
      EECore &core = system.eeCore();
      initializeMemory(&system.eeBus());
      setExceptionSentinels(&core);
      runDataAccess(
        &system,
        contract,
        store,
        DATA_VIRTUAL_BASE | 1,
        0,
        false);

      REQUIRE(
        core.pendingException() ==
        (store
           ? EEException::AddressErrorStore
           : EEException::AddressErrorLoadOrFetch));
      REQUIRE(
        core.exceptionAddress() ==
        (DATA_VIRTUAL_BASE | 1));
      requireMemorySystemSentinels(core);
      requireNoDataMutation(system, store);
    }
  }
}

TEST_CASE("EE data fault priority is consistent across memory families")
{
  constexpr DataFaultStage STAGES[] = {
    DataFaultStage::SegmentProtection,
    DataFaultStage::TLBRefill,
    DataFaultStage::TLBInvalid,
    DataFaultStage::TLBModified,
    DataFaultStage::UnsupportedCache,
    DataFaultStage::PhysicalBus
  };

  for (const MemoryAccessContract &contract : ACCESS_CONTRACTS)
  {
    for (const bool store : {false, true})
    {
      for (const DataFaultStage stage : STAGES)
      {
        if (stage == DataFaultStage::TLBModified && !store)
        {
          continue;
        }
        INFO(
          contract.name <<
          (store ? " store " : " load ") <<
          stageName(stage));
        requireDataFaultPriority(
          contract,
          store,
          stage);
      }
    }

  }
}

TEST_CASE("EE TLB fault priority covers a global odd page")
{
  constexpr std::uint32_t oddPageAddress = UINT32_C(0x00401100);
  const MemoryAccessContract &byte = ACCESS_CONTRACTS[0];
  NekoSystem system;
  EECore &core = system.eeCore();
  initializeMemory(&system.eeBus());
  setExceptionSentinels(&core);
  core.setTLBEntry(
    0,
    {
      EECOP0PageMask::SIZE_4_KIB,
      DATA_VPN2,
      {EECOP0EntryLo::GLOBAL},
      {
        pageValue(
          EEMemoryMap::MAIN_MEMORY_SIZE,
          0,
          false,
          false)
      }
    });

  runDataAccess(
    &system,
    byte,
    false,
    oddPageAddress,
    0,
    false);

  REQUIRE(
    core.pendingException() ==
    EEException::TLBInvalidLoadOrFetch);
  REQUIRE(core.exceptionAddress() == oddPageAddress);
  REQUIRE(
    core.cop0Register(EECOP0Register::BadVAddr) ==
    oddPageAddress);
  REQUIRE(core.generalRegister(2) == INITIAL_TARGET);
}
