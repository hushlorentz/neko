#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#include "catch.hpp"
#include "iop_bus.hpp"
#include "iop_core.hpp"

static_assert(
  std::is_final<IOPCore>::value,
  "IOP instruction execution must remain concrete and non-overridable.");
static_assert(
  sizeof(IOPWord) == 4,
  "IOP architectural words must remain exactly 32 bits.");
static_assert(
  sizeof(IOPAddress) == 4,
  "IOP architectural addresses must remain exactly 32 bits.");

struct IOPCoreTestAccess
{
  static void fillState(IOPCore *core)
  {
    core->pc = UINT32_C(0x12345678);
    for (std::size_t index = 0;
         index < core->writableGeneralRegisters.size();
         ++index)
    {
      core->writableGeneralRegisters[index] =
        static_cast<IOPWord>(index + 1);
    }
    core->hiRegister = UINT32_C(0x89abcdef);
    core->loRegister = UINT32_C(0xfedcba98);
    core->cop0.badVirtualAddress = UINT32_C(0x10203040);
    core->cop0.status = UINT32_C(0xffffffff);
    core->cop0.cause = UINT32_C(0xffffffff);
    core->cop0.exceptionProgramCounter =
      UINT32_C(0x50607080);
    core->branch = {
      true,
      UINT32_C(0x1000),
      UINT32_C(0x2000)
    };
    core->delayedResult = {
      IOPDelayedResultSource::Load,
      7,
      UINT32_C(0x13579bdf)
    };
    core->pendingException = {
      IOPException::DataBusError,
      UINT32_C(0x2468ace0),
      true,
      2
    };
    core->state = IOPExecutionState::Running;
    core->haltReason = IOPStopReason::ExecutionException;
    core->cycles = UINT64_C(0x123456789abcdef0);
  }

  static void setGeneralRegister(
    IOPCore *core,
    std::size_t index,
    IOPWord value)
  {
    core->setGeneralRegister(index, value);
  }

  static void setProgramCounter(
    IOPCore *core,
    IOPAddress address)
  {
    core->pc = address;
  }

  static void setUserMode(IOPCore *core, bool enabled)
  {
    if (enabled)
    {
      core->cop0.status |= IOPCOP0Status::CURRENT_USER_MODE;
    }
    else
    {
      core->cop0.status &= ~IOPCOP0Status::CURRENT_USER_MODE;
    }
  }
};

TEST_CASE("IOP core reset establishes deterministic architectural state")
{
  IOPCore core;
  IOPCoreTestAccess::fillState(&core);

  core.reset();

  REQUIRE(core.programCounter() == IOPReset::VECTOR);
  for (std::size_t index = 0;
       index < IOPCore::GENERAL_REGISTER_COUNT;
       ++index)
  {
    REQUIRE(core.generalRegister(index) == 0);
  }
  REQUIRE(core.hi() == 0);
  REQUIRE(core.lo() == 0);
  REQUIRE(core.badVirtualAddress() == 0);
  REQUIRE(
    core.status() ==
    (IOPCOP0Status::BOOT_EXCEPTION_VECTORS |
     IOPCOP0Status::TLB_SHUTDOWN));
  REQUIRE(core.cause() == 0);
  REQUIRE(core.exceptionProgramCounter() == 0);
  REQUIRE(core.processorRevision() == IOPCOP0::PROCESSOR_REVISION);

  REQUIRE_FALSE(core.branchContinuation().active);
  REQUIRE(core.branchContinuation().instructionAddress == 0);
  REQUIRE(core.branchContinuation().targetAddress == 0);
  REQUIRE(
    core.delayedResultContinuation().source ==
    IOPDelayedResultSource::None);
  REQUIRE(core.delayedResultContinuation().destination == 0);
  REQUIRE(core.delayedResultContinuation().value == 0);
  REQUIRE(core.exception().kind == IOPException::None);
  REQUIRE(core.exception().badVirtualAddress == 0);
  REQUIRE_FALSE(core.exception().inBranchDelaySlot);
  REQUIRE(core.exception().coprocessor == 0);

  REQUIRE(core.executionState() == IOPExecutionState::Halted);
  REQUIRE(core.stopReason() == IOPStopReason::None);
  REQUIRE(core.elapsedCycles() == 0);
}

TEST_CASE("IOP core inspection rejects an invalid register index")
{
  const IOPCore core;

  REQUIRE(core.generalRegister(0) == 0);
  REQUIRE(core.generalRegister(31) == 0);
  REQUIRE_THROWS_WITH(
    core.generalRegister(IOPCore::GENERAL_REGISTER_COUNT),
    "IOP general register index is out of range.");
}

TEST_CASE("IOP core structurally protects general register zero")
{
  IOPCore core;

  IOPCoreTestAccess::setGeneralRegister(
    &core,
    0,
    UINT32_C(0xffffffff));
  IOPCoreTestAccess::setGeneralRegister(
    &core,
    31,
    UINT32_C(0x12345678));

  REQUIRE(core.generalRegister(0) == 0);
  REQUIRE(
    core.generalRegister(31) ==
    UINT32_C(0x12345678));
}

TEST_CASE("IOP core classifies the selected non-TLB virtual segments")
{
  IOPCore core;

  const auto lowStart = core.classifyAddress(0x00000000);
  REQUIRE(lowStart.outcome == IOPAddressOutcome::Translated);
  REQUIRE(lowStart.physicalAddress == 0x00000000);
  REQUIRE(lowStart.cacheRoute == IOPCacheRoute::Cached);

  const auto lowEnd = core.classifyAddress(0x7fffffff);
  REQUIRE(lowEnd.outcome == IOPAddressOutcome::Translated);
  REQUIRE(lowEnd.physicalAddress == 0x7fffffff);
  REQUIRE(lowEnd.cacheRoute == IOPCacheRoute::Cached);

  const auto cachedStart = core.classifyAddress(0x80000000);
  REQUIRE(cachedStart.outcome == IOPAddressOutcome::Translated);
  REQUIRE(cachedStart.physicalAddress == 0x00000000);
  REQUIRE(cachedStart.cacheRoute == IOPCacheRoute::Cached);

  const auto cachedEnd = core.classifyAddress(0x9fffffff);
  REQUIRE(cachedEnd.outcome == IOPAddressOutcome::Translated);
  REQUIRE(cachedEnd.physicalAddress == 0x1fffffff);
  REQUIRE(cachedEnd.cacheRoute == IOPCacheRoute::Cached);

  const auto uncachedStart = core.classifyAddress(0xa0000000);
  REQUIRE(uncachedStart.outcome == IOPAddressOutcome::Translated);
  REQUIRE(uncachedStart.physicalAddress == 0x00000000);
  REQUIRE(uncachedStart.cacheRoute == IOPCacheRoute::Uncached);

  const auto uncachedEnd = core.classifyAddress(0xbfffffff);
  REQUIRE(uncachedEnd.outcome == IOPAddressOutcome::Translated);
  REQUIRE(uncachedEnd.physicalAddress == 0x1fffffff);
  REQUIRE(uncachedEnd.cacheRoute == IOPCacheRoute::Uncached);

  const auto highStart = core.classifyAddress(0xc0000000);
  REQUIRE(highStart.outcome == IOPAddressOutcome::Translated);
  REQUIRE(highStart.physicalAddress == 0xc0000000);
  REQUIRE(highStart.cacheRoute == IOPCacheRoute::Uncached);

  const auto highEnd = core.classifyAddress(0xffffffff);
  REQUIRE(highEnd.outcome == IOPAddressOutcome::Translated);
  REQUIRE(highEnd.physicalAddress == 0xffffffff);
  REQUIRE(highEnd.cacheRoute == IOPCacheRoute::Uncached);
}

TEST_CASE("IOP core protects kernel virtual segments from user mode")
{
  IOPCore core;
  IOPCoreTestAccess::setUserMode(&core, true);

  REQUIRE(
    core.classifyAddress(0x7fffffff).outcome ==
    IOPAddressOutcome::Translated);

  const IOPAddress protectedAddresses[] = {
    0x80000000,
    0xa0000000,
    0xc0000000,
    0xffffffff
  };
  for (IOPAddress address : protectedAddresses)
  {
    const auto classification = core.classifyAddress(address);
    REQUIRE(
      classification.outcome ==
      IOPAddressOutcome::ProtectionFailure);
    REQUIRE(classification.virtualAddress == address);
    REQUIRE(classification.physicalAddress == 0);
    REQUIRE(classification.cacheRoute == IOPCacheRoute::None);
  }
}

TEST_CASE("IOP core fetches only through virtual translation and the bus")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);

  const std::uint8_t instruction[] = {0x78, 0x56, 0x34, 0x12};
  REQUIRE(
    bus.loadPhysical(0x100, instruction, sizeof(instruction)) ==
    IOPBusStatus::Completed);

  IOPCoreTestAccess::setProgramCounter(&core, 0x80000100);
  const auto cached = core.fetchInstruction();
  REQUIRE(cached.outcome == IOPAccessOutcome::Completed);
  REQUIRE(cached.exception == IOPException::None);
  REQUIRE(cached.virtualAddress == 0x80000100);
  REQUIRE(cached.physicalAddress == 0x100);
  REQUIRE(cached.cacheRoute == IOPCacheRoute::Cached);
  REQUIRE(cached.value == UINT32_C(0x12345678));

  IOPCoreTestAccess::setProgramCounter(&core, 0xa0000100);
  const auto uncached = core.fetchInstruction();
  REQUIRE(uncached.outcome == IOPAccessOutcome::Completed);
  REQUIRE(uncached.physicalAddress == 0x100);
  REQUIRE(uncached.cacheRoute == IOPCacheRoute::Uncached);
  REQUIRE(uncached.value == UINT32_C(0x12345678));
}

TEST_CASE("IOP core reports fetch protection alignment and bus failures")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);

  IOPCoreTestAccess::setUserMode(&core, true);
  IOPCoreTestAccess::setProgramCounter(&core, 0x80000000);
  const auto protection = core.fetchInstruction();
  REQUIRE(protection.outcome == IOPAccessOutcome::ProtectionFailure);
  REQUIRE(
    protection.exception ==
    IOPException::AddressErrorLoadOrFetch);

  IOPCoreTestAccess::setUserMode(&core, false);
  IOPCoreTestAccess::setProgramCounter(&core, 0x00000002);
  const auto misaligned = core.fetchInstruction();
  REQUIRE(misaligned.outcome == IOPAccessOutcome::Misaligned);
  REQUIRE(
    misaligned.exception ==
    IOPException::AddressErrorLoadOrFetch);

  IOPCoreTestAccess::setProgramCounter(&core, 0x00800000);
  const auto unmapped = core.fetchInstruction();
  REQUIRE(unmapped.outcome == IOPAccessOutcome::Unmapped);
  REQUIRE(unmapped.exception == IOPException::InstructionBusError);
}

TEST_CASE("IOP core data access preserves routes and selects typed failures")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  REQUIRE(bus.installRom(
    std::make_shared<std::vector<std::uint8_t>>(
      IOPBus::ROM_SIZE,
      static_cast<std::uint8_t>(0))));

  const auto cachedStore =
    core.writeData32(0x80000200, UINT32_C(0x89abcdef));
  REQUIRE(cachedStore.outcome == IOPAccessOutcome::Completed);
  REQUIRE(cachedStore.exception == IOPException::None);
  REQUIRE(cachedStore.physicalAddress == 0x200);
  REQUIRE(cachedStore.cacheRoute == IOPCacheRoute::Cached);

  const auto uncachedLoad = core.readData32(0xa0000200);
  REQUIRE(uncachedLoad.outcome == IOPAccessOutcome::Completed);
  REQUIRE(uncachedLoad.exception == IOPException::None);
  REQUIRE(uncachedLoad.physicalAddress == 0x200);
  REQUIRE(uncachedLoad.cacheRoute == IOPCacheRoute::Uncached);
  REQUIRE(uncachedLoad.value == UINT32_C(0x89abcdef));

  const auto byteStore = core.writeData8(0x201, 0x5a);
  REQUIRE(byteStore.outcome == IOPAccessOutcome::Completed);
  REQUIRE(core.readData8(0x201).value == 0x5a);

  const auto halfwordStore = core.writeData16(0x202, 0x1357);
  REQUIRE(halfwordStore.outcome == IOPAccessOutcome::Completed);
  REQUIRE(core.readData16(0x202).value == 0x1357);

  const auto misalignedLoad = core.readData32(0x202);
  REQUIRE(misalignedLoad.outcome == IOPAccessOutcome::Misaligned);
  REQUIRE(
    misalignedLoad.exception ==
    IOPException::AddressErrorLoadOrFetch);

  const auto misalignedStore = core.writeData16(0x203, 0);
  REQUIRE(misalignedStore.outcome == IOPAccessOutcome::Misaligned);
  REQUIRE(
    misalignedStore.exception ==
    IOPException::AddressErrorStore);

  const auto unmappedLoad = core.readData8(0x00800000);
  REQUIRE(unmappedLoad.outcome == IOPAccessOutcome::Unmapped);
  REQUIRE(unmappedLoad.exception == IOPException::DataBusError);

  const auto readOnlyStore = core.writeData32(0xbfc00000, 0);
  REQUIRE(readOnlyStore.outcome == IOPAccessOutcome::ReadOnly);
  REQUIRE(readOnlyStore.exception == IOPException::DataBusError);
}

TEST_CASE("IOP core data protection distinguishes load and store exceptions")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setUserMode(&core, true);

  const auto load = core.readData32(0x80000000);
  REQUIRE(load.outcome == IOPAccessOutcome::ProtectionFailure);
  REQUIRE(load.exception == IOPException::AddressErrorLoadOrFetch);

  const auto store = core.writeData32(0x80000000, 0);
  REQUIRE(store.outcome == IOPAccessOutcome::ProtectionFailure);
  REQUIRE(store.exception == IOPException::AddressErrorStore);
}

TEST_CASE("IOP core rejects memory access without an attached bus")
{
  IOPCore core;

  REQUIRE_THROWS_WITH(
    core.fetchInstruction(),
    "IOP Core bus is not attached.");
  REQUIRE_THROWS_WITH(
    core.readData8(0),
    "IOP Core bus is not attached.");
  REQUIRE_THROWS_WITH(
    core.writeData8(0, 0),
    "IOP Core bus is not attached.");
}
