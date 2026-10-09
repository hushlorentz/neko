#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#include "catch.hpp"
#include "iop_bus.hpp"
#include "iop_core.hpp"

namespace
{
  constexpr std::uint32_t registerInstruction(
    std::uint8_t function,
    std::uint8_t source,
    std::uint8_t target,
    std::uint8_t destination,
    std::uint8_t shiftAmount = 0)
  {
    return
      (static_cast<std::uint32_t>(source) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      (static_cast<std::uint32_t>(destination) << 11) |
      (static_cast<std::uint32_t>(shiftAmount) << 6) |
      function;
  }

  constexpr std::uint32_t immediateInstruction(
    std::uint8_t opcode,
    std::uint8_t source,
    std::uint8_t target,
    std::uint16_t immediate)
  {
    return
      (static_cast<std::uint32_t>(opcode) << 26) |
      (static_cast<std::uint32_t>(source) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      immediate;
  }

  void loadInstruction(
    IOPBus *bus,
    IOPAddress address,
    std::uint32_t instruction)
  {
    const std::uint8_t bytes[] = {
      static_cast<std::uint8_t>(instruction),
      static_cast<std::uint8_t>(instruction >> 8),
      static_cast<std::uint8_t>(instruction >> 16),
      static_cast<std::uint8_t>(instruction >> 24)
    };
    REQUIRE(
      bus->loadPhysical(address, bytes, sizeof(bytes)) ==
      IOPBusStatus::Completed);
  }
}

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
    core->hiLoState = {2, 2, true, true};
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
    core->retiredInstructionTotal =
      UINT64_C(0x0fedcba987654321);
  }

  static void setGeneralRegister(
    IOPCore *core,
    std::size_t index,
    IOPWord value)
  {
    core->setGeneralRegister(index, value);
  }

  static void setHI(IOPCore *core, IOPWord value)
  {
    core->hiRegister = value;
  }

  static void setLO(IOPCore *core, IOPWord value)
  {
    core->loRegister = value;
  }

  static void setHILOState(IOPCore *core)
  {
    core->hiLoState = {2, 2, true, true};
  }

  static bool hiLoStateIsClear(const IOPCore &core)
  {
    return core.hiLoState.hiWriteHazardInstructions == 0 &&
      core.hiLoState.loWriteHazardInstructions == 0 &&
      !core.hiLoState.unreadMultiplyDivideHI &&
      !core.hiLoState.unreadMultiplyDivideLO;
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

  static void commitInstructionEffects(
    IOPCore *core,
    IOPAddress instructionAddress,
    const IOPInstructionEffects &effects)
  {
    core->commitInstructionEffects(
      instructionAddress,
      effects);
  }

  static void setPendingContinuations(IOPCore *core)
  {
    core->branch = {
      true,
      UINT32_C(0x100),
      UINT32_C(0x200)
    };
    core->delayedResult = {
      IOPDelayedResultSource::Load,
      1,
      UINT32_C(0x12345678)
    };
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
  REQUIRE(IOPCoreTestAccess::hiLoStateIsClear(core));
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
  REQUIRE(core.retiredInstructions() == 0);
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
    BootROMImage::create(std::vector<std::uint8_t>(
      IOPBus::ROM_SIZE,
      static_cast<std::uint8_t>(0)))));

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

TEST_CASE("IOP instruction boundary starts and retires canonical NOP",
  "[iop][execution]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setHILOState(&core);
  const std::uint8_t nop[] = {0, 0, 0, 0};
  REQUIRE(
    bus.loadPhysical(0x100, nop, sizeof(nop)) ==
    IOPBusStatus::Completed);

  core.startExecution(0x80000100);
  REQUIRE(core.executionState() == IOPExecutionState::Running);
  REQUIRE(core.stopReason() == IOPStopReason::None);
  REQUIRE(IOPCoreTestAccess::hiLoStateIsClear(core));

  core.stepInstruction();

  REQUIRE(core.programCounter() == 0x80000104);
  REQUIRE(core.executionState() == IOPExecutionState::Running);
  REQUIRE(core.stopReason() == IOPStopReason::None);
  REQUIRE(core.elapsedCycles() == 1);
  REQUIRE(core.retiredInstructions() == 1);
  REQUIRE(core.exception().kind == IOPException::None);
}

TEST_CASE("IOP instruction boundary rejects stepping while halted",
  "[iop][execution]")
{
  IOPCore core;

  REQUIRE_THROWS_WITH(
    core.stepInstruction(),
    "IOP Core cannot step while halted.");
  REQUIRE(core.elapsedCycles() == 0);
  REQUIRE(core.retiredInstructions() == 0);
}

TEST_CASE("IOP instruction boundary records fetch and decode failures",
  "[iop][execution]")
{
  SECTION("Fetch failure")
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    core.startExecution(2);

    core.stepInstruction();

    REQUIRE(core.programCounter() == 2);
    REQUIRE(core.executionState() == IOPExecutionState::Halted);
    REQUIRE(core.stopReason() == IOPStopReason::FetchFailure);
    REQUIRE(core.elapsedCycles() == 1);
    REQUIRE(core.retiredInstructions() == 0);
    REQUIRE(
      core.exception().kind ==
      IOPException::AddressErrorLoadOrFetch);
    REQUIRE(core.exception().badVirtualAddress == 2);
  }

  SECTION("Reserved instruction")
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    const std::uint8_t reserved[] = {0, 0, 0, 0xfc};
    REQUIRE(
      bus.loadPhysical(0, reserved, sizeof(reserved)) ==
      IOPBusStatus::Completed);
    core.startExecution(0);

    core.stepInstruction();

    REQUIRE(core.programCounter() == 0);
    REQUIRE(core.executionState() == IOPExecutionState::Halted);
    REQUIRE(
      core.stopReason() ==
      IOPStopReason::ReservedInstruction);
    REQUIRE(core.elapsedCycles() == 1);
    REQUIRE(core.retiredInstructions() == 0);
    REQUIRE(
      core.exception().kind ==
      IOPException::ReservedInstruction);
  }
}

TEST_CASE("IOP instruction boundary does not retire unavailable semantics",
  "[iop][execution]")
{
  const std::uint8_t unavailableInstructions[][4] = {
    {0x08, 0x00, 0x20, 0x00},
    {0, 0, 0, 0x44}
  };
  for (const auto &instruction : unavailableInstructions)
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    REQUIRE(
      bus.loadPhysical(0, instruction, sizeof(instruction)) ==
      IOPBusStatus::Completed);
    core.startExecution(0);

    core.stepInstruction();

    REQUIRE(core.programCounter() == 0);
    REQUIRE(core.executionState() == IOPExecutionState::Halted);
    REQUIRE(core.stopReason() == IOPStopReason::ExecutionException);
    REQUIRE(core.elapsedCycles() == 1);
    REQUIRE(core.retiredInstructions() == 0);
    REQUIRE(core.exception().kind == IOPException::None);
  }
}

TEST_CASE("IOP effect commit owns architectural writes and fault suppression",
  "[iop][execution]")
{
  IOPCore core;
  core.startExecution(0x100);

  IOPInstructionEffects effects;
  effects.destination = {
    IOPWriteEffect::Write,
    0,
    UINT32_C(0xffffffff)
  };
  effects.hi = {
    IOPWriteEffect::Write,
    UINT32_C(0x12345678)
  };
  effects.lo = {
    IOPWriteEffect::Write,
    UINT32_C(0x9abcdef0)
  };
  effects.controlFlow = {
    IOPControlFlowEffect::Sequential,
    0
  };
  effects.completion =
    IOPInstructionCompletion::Retired;

  IOPCoreTestAccess::commitInstructionEffects(
    &core,
    0x100,
    effects);

  REQUIRE(core.generalRegister(0) == 0);
  REQUIRE(core.hi() == UINT32_C(0x12345678));
  REQUIRE(core.lo() == UINT32_C(0x9abcdef0));
  REQUIRE(core.programCounter() == 0x104);
  REQUIRE(core.retiredInstructions() == 1);

  effects.destination.destination = 1;
  effects.destination.value = UINT32_C(0x87654321);
  effects.hi.value = UINT32_C(0x11111111);
  effects.lo.value = UINT32_C(0x22222222);
  effects.exception = {
    IOPException::ArithmeticOverflow,
    UINT32_C(0x200),
    false,
    0
  };
  effects.stopReason = IOPStopReason::ExecutionException;

  IOPCoreTestAccess::commitInstructionEffects(
    &core,
    0x104,
    effects);

  REQUIRE(core.generalRegister(1) == 0);
  REQUIRE(core.hi() == UINT32_C(0x12345678));
  REQUIRE(core.lo() == UINT32_C(0x9abcdef0));
  REQUIRE(core.programCounter() == 0x104);
  REQUIRE(core.retiredInstructions() == 1);
  REQUIRE(core.elapsedCycles() == 2);
  REQUIRE(
    core.exception().kind ==
    IOPException::ArithmeticOverflow);
}

TEST_CASE("IOP sequential commit wraps the 32-bit program counter",
  "[iop][execution]")
{
  IOPCore core;
  core.startExecution(UINT32_C(0xfffffffc));

  IOPInstructionEffects effects;
  effects.controlFlow.kind = IOPControlFlowEffect::Sequential;
  effects.completion = IOPInstructionCompletion::Retired;
  effects.stopReason = IOPStopReason::None;
  IOPCoreTestAccess::commitInstructionEffects(
    &core,
    UINT32_C(0xfffffffc),
    effects);

  REQUIRE(core.programCounter() == 0);
  REQUIRE(core.retiredInstructions() == 1);
}

TEST_CASE("IOP effect commit rejects incomplete retirement atomically",
  "[iop][execution]")
{
  IOPCore core;
  core.startExecution(0x100);

  IOPInstructionEffects effects;
  effects.destination = {
    IOPWriteEffect::Write,
    1,
    UINT32_C(0x12345678)
  };
  effects.completion = IOPInstructionCompletion::Retired;
  effects.stopReason = IOPStopReason::None;

  REQUIRE_THROWS_WITH(
    IOPCoreTestAccess::commitInstructionEffects(
      &core,
      0x100,
      effects),
    "Retired IOP instruction has no control-flow effect.");
  REQUIRE(core.generalRegister(1) == 0);
  REQUIRE(core.programCounter() == 0x100);
  REQUIRE(core.elapsedCycles() == 0);
  REQUIRE(core.retiredInstructions() == 0);
}

TEST_CASE("IOP instruction boundary rejects unimplemented continuations",
  "[iop][execution]")
{
  IOPCore core;
  core.startExecution(0x100);
  IOPCoreTestAccess::setPendingContinuations(&core);

  IOPInstructionEffects effects;
  effects.controlFlow.kind = IOPControlFlowEffect::Sequential;
  effects.completion = IOPInstructionCompletion::Retired;
  effects.stopReason = IOPStopReason::None;

  REQUIRE_THROWS_WITH(
    IOPCoreTestAccess::commitInstructionEffects(
      &core,
      0x100,
      effects),
    "IOP continuation commit is not implemented.");
  REQUIRE(core.programCounter() == 0x100);
  REQUIRE(core.branchContinuation().active);
  REQUIRE(
    core.delayedResultContinuation().source ==
    IOPDelayedResultSource::Load);
  REQUIRE(core.elapsedCycles() == 0);
  REQUIRE(core.retiredInstructions() == 0);
}

TEST_CASE("IOP executes fixed and variable logical shifts",
  "[iop][execution][integer]")
{
  struct ShiftCase
  {
    std::uint32_t instruction;
    IOPWord source;
    IOPWord shift;
    IOPWord expected;
  };
  const ShiftCase cases[] = {
    {
      registerInstruction(0x00, 0, 1, 3, 4),
      UINT32_C(0x12345678),
      0,
      UINT32_C(0x23456780)
    },
    {
      registerInstruction(0x02, 0, 1, 3, 4),
      UINT32_C(0x92345678),
      0,
      UINT32_C(0x09234567)
    },
    {
      registerInstruction(0x03, 0, 1, 3, 4),
      UINT32_C(0x92345678),
      0,
      UINT32_C(0xf9234567)
    },
    {
      registerInstruction(0x04, 2, 1, 3),
      UINT32_C(0x12345678),
      36,
      UINT32_C(0x23456780)
    },
    {
      registerInstruction(0x06, 2, 1, 3),
      UINT32_C(0x92345678),
      36,
      UINT32_C(0x09234567)
    },
    {
      registerInstruction(0x07, 2, 1, 3),
      UINT32_C(0x92345678),
      36,
      UINT32_C(0xf9234567)
    },
    {
      registerInstruction(0x03, 0, 1, 3, 0),
      UINT32_C(0x80000000),
      0,
      UINT32_C(0x80000000)
    },
    {
      registerInstruction(0x00, 0, 1, 3, 31),
      1,
      0,
      UINT32_C(0x80000000)
    },
    {
      registerInstruction(0x02, 0, 1, 3, 31),
      UINT32_C(0x80000000),
      0,
      1
    },
    {
      registerInstruction(0x03, 0, 1, 3, 31),
      UINT32_C(0x80000000),
      0,
      UINT32_C(0xffffffff)
    }
  };

  for (const ShiftCase &test : cases)
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, test.source);
    IOPCoreTestAccess::setGeneralRegister(&core, 2, test.shift);
    loadInstruction(&bus, 0, test.instruction);
    core.startExecution(0);

    core.stepInstruction();

    REQUIRE(core.generalRegister(3) == test.expected);
    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.retiredInstructions() == 1);
  }
}

TEST_CASE("IOP executes register logical and comparison operations",
  "[iop][execution][integer]")
{
  struct RegisterCase
  {
    std::uint8_t function;
    IOPWord source;
    IOPWord target;
    IOPWord expected;
  };
  const RegisterCase cases[] = {
    {0x24, UINT32_C(0xff00ff00), UINT32_C(0x0f0f0f0f),
      UINT32_C(0x0f000f00)},
    {0x25, UINT32_C(0xff00ff00), UINT32_C(0x0f0f0f0f),
      UINT32_C(0xff0fff0f)},
    {0x26, UINT32_C(0xff00ff00), UINT32_C(0x0f0f0f0f),
      UINT32_C(0xf00ff00f)},
    {0x27, UINT32_C(0xff00ff00), UINT32_C(0x0f0f0f0f),
      UINT32_C(0x00f000f0)},
    {0x2a, UINT32_C(0xffffffff), 0, 1},
    {0x2a, UINT32_C(0x80000000), UINT32_C(0x7fffffff), 1},
    {0x2a, UINT32_C(0x7fffffff), UINT32_C(0x80000000), 0},
    {0x2b, UINT32_C(0xffffffff), 0, 0},
    {0x2b, 0, UINT32_C(0xffffffff), 1}
  };

  for (const RegisterCase &test : cases)
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, test.source);
    IOPCoreTestAccess::setGeneralRegister(&core, 2, test.target);
    loadInstruction(
      &bus,
      0,
      registerInstruction(test.function, 1, 2, 3));
    core.startExecution(0);

    core.stepInstruction();

    REQUIRE(core.generalRegister(3) == test.expected);
    REQUIRE(core.retiredInstructions() == 1);
  }
}

TEST_CASE("IOP executes logical comparison and upper immediates",
  "[iop][execution][integer]")
{
  struct ImmediateCase
  {
    std::uint8_t opcode;
    IOPWord source;
    std::uint16_t immediate;
    IOPWord expected;
  };
  const ImmediateCase cases[] = {
    {0x0a, 0, UINT16_C(0xffff), 0},
    {0x0a, UINT32_C(0xffffffff), 1, 1},
    {0x0b, 0, UINT16_C(0xffff), 1},
    {0x0b, UINT32_C(0xffffffff), UINT16_C(0xffff), 0},
    {0x0c, UINT32_C(0xffff00ff), UINT16_C(0x0ff0),
      UINT32_C(0x000000f0)},
    {0x0d, UINT32_C(0xffff0000), UINT16_C(0x00ff),
      UINT32_C(0xffff00ff)},
    {0x0e, UINT32_C(0xffff0000), UINT16_C(0xffff),
      UINT32_C(0xffffffff)},
    {0x0f, UINT32_C(0xffffffff), UINT16_C(0x89ab),
      UINT32_C(0x89ab0000)}
  };

  for (const ImmediateCase &test : cases)
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, test.source);
    const std::uint8_t sourceRegister =
      test.opcode == 0x0f ? 0 : 1;
    loadInstruction(
      &bus,
      0,
      immediateInstruction(
        test.opcode,
        sourceRegister,
        3,
        test.immediate));
    core.startExecution(0);

    core.stepInstruction();

    REQUIRE(core.generalRegister(3) == test.expected);
    REQUIRE(core.retiredInstructions() == 1);
  }
}

TEST_CASE("IOP integer results cannot modify general register zero",
  "[iop][execution][integer]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setGeneralRegister(
    &core,
    1,
    UINT32_C(0xffffffff));
  loadInstruction(
    &bus,
    0,
    immediateInstruction(0x0e, 1, 0, UINT16_C(0xffff)));
  core.startExecution(0);

  core.stepInstruction();

  REQUIRE(core.generalRegister(0) == 0);
  REQUIRE(core.retiredInstructions() == 1);
}

TEST_CASE("IOP executes wrapping register and immediate arithmetic",
  "[iop][execution][integer][arithmetic]")
{
  struct ArithmeticCase
  {
    std::uint32_t instruction;
    IOPWord source;
    IOPWord target;
    IOPWord expected;
  };
  const ArithmeticCase cases[] = {
    {
      registerInstruction(0x21, 1, 2, 3),
      UINT32_C(0xffffffff),
      1,
      0
    },
    {
      registerInstruction(0x23, 1, 2, 3),
      0,
      1,
      UINT32_C(0xffffffff)
    },
    {
      immediateInstruction(0x09, 1, 3, UINT16_C(0xffff)),
      0,
      0,
      UINT32_C(0xffffffff)
    },
    {
      immediateInstruction(0x09, 1, 3, 1),
      UINT32_C(0x7fffffff),
      0,
      UINT32_C(0x80000000)
    }
  };

  for (const ArithmeticCase &test : cases)
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, test.source);
    IOPCoreTestAccess::setGeneralRegister(&core, 2, test.target);
    loadInstruction(&bus, 0, test.instruction);
    core.startExecution(0);

    core.stepInstruction();

    REQUIRE(core.generalRegister(3) == test.expected);
    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.executionState() == IOPExecutionState::Running);
    REQUIRE(core.retiredInstructions() == 1);
    REQUIRE(core.exception().kind == IOPException::None);
  }
}

TEST_CASE("IOP executes non-overflowing trapping arithmetic",
  "[iop][execution][integer][arithmetic]")
{
  struct ArithmeticCase
  {
    std::uint32_t instruction;
    IOPWord source;
    IOPWord target;
    IOPWord expected;
  };
  const ArithmeticCase cases[] = {
    {
      registerInstruction(0x20, 1, 2, 3),
      UINT32_C(0x7ffffffe),
      1,
      UINT32_C(0x7fffffff)
    },
    {
      registerInstruction(0x22, 1, 2, 3),
      UINT32_C(0x80000001),
      1,
      UINT32_C(0x80000000)
    },
    {
      immediateInstruction(0x08, 1, 3, UINT16_C(0xffff)),
      0,
      0,
      UINT32_C(0xffffffff)
    }
  };

  for (const ArithmeticCase &test : cases)
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, test.source);
    IOPCoreTestAccess::setGeneralRegister(&core, 2, test.target);
    loadInstruction(&bus, 0, test.instruction);
    core.startExecution(0);

    core.stepInstruction();

    REQUIRE(core.generalRegister(3) == test.expected);
    REQUIRE(core.executionState() == IOPExecutionState::Running);
    REQUIRE(core.retiredInstructions() == 1);
    REQUIRE(core.exception().kind == IOPException::None);
  }
}

TEST_CASE("IOP trapping arithmetic preserves its destination on overflow",
  "[iop][execution][integer][arithmetic]")
{
  struct OverflowCase
  {
    std::uint32_t instruction;
    IOPWord source;
    IOPWord target;
  };
  const OverflowCase cases[] = {
    {
      registerInstruction(0x20, 1, 2, 3),
      UINT32_C(0x7fffffff),
      1
    },
    {
      registerInstruction(0x20, 1, 2, 3),
      UINT32_C(0x80000000),
      UINT32_C(0xffffffff)
    },
    {
      registerInstruction(0x22, 1, 2, 3),
      UINT32_C(0x80000000),
      1
    },
    {
      registerInstruction(0x22, 1, 2, 3),
      UINT32_C(0x7fffffff),
      UINT32_C(0xffffffff)
    },
    {
      immediateInstruction(0x08, 1, 3, 1),
      UINT32_C(0x7fffffff),
      0
    },
    {
      immediateInstruction(0x08, 1, 3, UINT16_C(0xffff)),
      UINT32_C(0x80000000),
      0
    }
  };

  for (const OverflowCase &test : cases)
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, test.source);
    IOPCoreTestAccess::setGeneralRegister(&core, 2, test.target);
    IOPCoreTestAccess::setGeneralRegister(
      &core,
      3,
      UINT32_C(0x12345678));
    loadInstruction(&bus, 0, test.instruction);
    core.startExecution(0);

    core.stepInstruction();

    REQUIRE(
      core.generalRegister(3) ==
      UINT32_C(0x12345678));
    REQUIRE(core.programCounter() == 0);
    REQUIRE(core.executionState() == IOPExecutionState::Halted);
    REQUIRE(core.stopReason() == IOPStopReason::ExecutionException);
    REQUIRE(core.retiredInstructions() == 0);
    REQUIRE(core.elapsedCycles() == 1);
    REQUIRE(
      core.exception().kind ==
      IOPException::ArithmeticOverflow);
  }
}

TEST_CASE("IOP overflow is raised even when the destination is register zero",
  "[iop][execution][integer][arithmetic]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setGeneralRegister(
    &core,
    1,
    UINT32_C(0x7fffffff));
  IOPCoreTestAccess::setGeneralRegister(&core, 2, 1);
  loadInstruction(
    &bus,
    0,
    registerInstruction(0x20, 1, 2, 0));
  core.startExecution(0);

  core.stepInstruction();

  REQUIRE(core.generalRegister(0) == 0);
  REQUIRE(core.retiredInstructions() == 0);
  REQUIRE(
    core.exception().kind ==
    IOPException::ArithmeticOverflow);
}

TEST_CASE("IOP transfers values through HI and LO",
  "[iop][execution][hilo]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setHI(&core, UINT32_C(0xaaaaaaaa));
  IOPCoreTestAccess::setLO(&core, UINT32_C(0xbbbbbbbb));
  IOPCoreTestAccess::setGeneralRegister(
    &core,
    1,
    UINT32_C(0x12345678));
  IOPCoreTestAccess::setGeneralRegister(
    &core,
    2,
    UINT32_C(0x9abcdef0));
  loadInstruction(&bus, 0, registerInstruction(0x11, 1, 0, 0));
  loadInstruction(&bus, 4, registerInstruction(0x10, 0, 0, 3));
  loadInstruction(&bus, 8, 0);
  loadInstruction(&bus, 12, 0);
  loadInstruction(&bus, 16, registerInstruction(0x13, 2, 0, 0));
  loadInstruction(&bus, 20, registerInstruction(0x12, 0, 0, 4));
  core.startExecution(0);

  core.stepInstruction();
  REQUIRE(core.hi() == UINT32_C(0x12345678));
  REQUIRE(core.lo() == UINT32_C(0xbbbbbbbb));

  core.stepInstruction();
  REQUIRE(core.generalRegister(3) == UINT32_C(0x12345678));

  core.stepInstruction();
  core.stepInstruction();
  core.stepInstruction();
  REQUIRE(core.hi() == UINT32_C(0x12345678));
  REQUIRE(core.lo() == UINT32_C(0x9abcdef0));

  core.stepInstruction();
  REQUIRE(core.generalRegister(4) == UINT32_C(0x9abcdef0));
  REQUIRE(core.retiredInstructions() == 6);
}

TEST_CASE("IOP signed and unsigned multiplication populate HI and LO",
  "[iop][execution][hilo]")
{
  struct MultiplyCase
  {
    std::uint8_t function;
    IOPWord source;
    IOPWord target;
    IOPWord expectedHI;
    IOPWord expectedLO;
  };
  const MultiplyCase cases[] = {
    {
      0x18,
      UINT32_C(0xfffffffe),
      3,
      UINT32_C(0xffffffff),
      UINT32_C(0xfffffffa)
    },
    {
      0x18,
      UINT32_C(0x80000000),
      UINT32_C(0xffffffff),
      0,
      UINT32_C(0x80000000)
    },
    {
      0x19,
      UINT32_C(0xffffffff),
      2,
      1,
      UINT32_C(0xfffffffe)
    }
  };

  for (const MultiplyCase &test : cases)
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, test.source);
    IOPCoreTestAccess::setGeneralRegister(&core, 2, test.target);
    loadInstruction(
      &bus,
      0,
      registerInstruction(test.function, 1, 2, 0));
    core.startExecution(0);

    core.stepInstruction();

    REQUIRE(core.hi() == test.expectedHI);
    REQUIRE(core.lo() == test.expectedLO);
    REQUIRE(core.retiredInstructions() == 1);
  }
}

TEST_CASE("IOP division populates quotient and remainder without host UB",
  "[iop][execution][hilo]")
{
  struct DivideCase
  {
    std::uint8_t function;
    IOPWord dividend;
    IOPWord divisor;
    IOPWord expectedHI;
    IOPWord expectedLO;
  };
  const DivideCase cases[] = {
    {0x1a, UINT32_C(0xfffffff9), 3,
      UINT32_C(0xffffffff), UINT32_C(0xfffffffe)},
    {0x1a, 7, UINT32_C(0xfffffffd), 1,
      UINT32_C(0xfffffffe)},
    {0x1a, UINT32_C(0xfffffff9), UINT32_C(0xfffffffd),
      UINT32_C(0xffffffff), 2},
    {0x1b, UINT32_C(0xffffffff), 2, 1,
      UINT32_C(0x7fffffff)},
    {0x1a, 7, 0, 7, UINT32_C(0xffffffff)},
    {0x1a, UINT32_C(0xfffffff9), 0,
      UINT32_C(0xfffffff9), 1},
    {0x1b, UINT32_C(0x89abcdef), 0,
      UINT32_C(0x89abcdef), UINT32_C(0xffffffff)},
    {0x1a, UINT32_C(0x80000000), UINT32_C(0xffffffff),
      0, UINT32_C(0x80000000)}
  };

  for (const DivideCase &test : cases)
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, test.dividend);
    IOPCoreTestAccess::setGeneralRegister(&core, 2, test.divisor);
    loadInstruction(
      &bus,
      0,
      registerInstruction(test.function, 1, 2, 0));
    core.startExecution(0);

    core.stepInstruction();

    REQUIRE(core.hi() == test.expectedHI);
    REQUIRE(core.lo() == test.expectedLO);
    REQUIRE(core.executionState() == IOPExecutionState::Running);
    REQUIRE(core.retiredInstructions() == 1);
    REQUIRE(core.exception().kind == IOPException::None);
  }
}

TEST_CASE("IOP multiply results are functionally interlocked for immediate reads",
  "[iop][execution][hilo]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setGeneralRegister(&core, 1, 6);
  IOPCoreTestAccess::setGeneralRegister(&core, 2, 7);
  loadInstruction(&bus, 0, registerInstruction(0x18, 1, 2, 0));
  loadInstruction(&bus, 4, registerInstruction(0x12, 0, 0, 3));
  core.startExecution(0);

  core.stepInstruction();
  core.stepInstruction();

  REQUIRE(core.generalRegister(3) == 42);
  REQUIRE(core.retiredInstructions() == 2);
  REQUIRE(core.elapsedCycles() == 2);
}

TEST_CASE("IOP rejects HI and LO writes in the post-read hazard window",
  "[iop][execution][hilo]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setHI(&core, UINT32_C(0x11111111));
  IOPCoreTestAccess::setGeneralRegister(
    &core,
    1,
    UINT32_C(0x22222222));
  loadInstruction(&bus, 0, registerInstruction(0x10, 0, 0, 3));
  loadInstruction(&bus, 4, 0);
  loadInstruction(&bus, 8, registerInstruction(0x11, 1, 0, 0));
  core.startExecution(0);

  core.stepInstruction();
  core.stepInstruction();
  core.stepInstruction();

  REQUIRE(core.hi() == UINT32_C(0x11111111));
  REQUIRE(core.programCounter() == 8);
  REQUIRE(core.executionState() == IOPExecutionState::Halted);
  REQUIRE(core.stopReason() == IOPStopReason::UndefinedOperation);
  REQUIRE(core.retiredInstructions() == 2);
  REQUIRE(core.exception().kind == IOPException::None);
}

TEST_CASE("IOP permits HI and LO writes after two separation instructions",
  "[iop][execution][hilo]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setGeneralRegister(
    &core,
    1,
    UINT32_C(0x12345678));
  loadInstruction(&bus, 0, registerInstruction(0x12, 0, 0, 3));
  loadInstruction(&bus, 4, 0);
  loadInstruction(&bus, 8, 0);
  loadInstruction(&bus, 12, registerInstruction(0x11, 1, 0, 0));
  core.startExecution(0);

  core.stepInstruction();
  core.stepInstruction();
  core.stepInstruction();
  core.stepInstruction();

  REQUIRE(core.hi() == UINT32_C(0x12345678));
  REQUIRE(core.executionState() == IOPExecutionState::Running);
  REQUIRE(core.retiredInstructions() == 4);
}

TEST_CASE("IOP HI and LO read hazards permit cross-register writes",
  "[iop][execution][hilo]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setGeneralRegister(
    &core,
    1,
    UINT32_C(0x12345678));
  IOPCoreTestAccess::setGeneralRegister(
    &core,
    2,
    UINT32_C(0x9abcdef0));
  loadInstruction(&bus, 0, registerInstruction(0x10, 0, 0, 3));
  loadInstruction(&bus, 4, registerInstruction(0x13, 1, 0, 0));
  loadInstruction(&bus, 8, registerInstruction(0x12, 0, 0, 4));
  loadInstruction(&bus, 12, registerInstruction(0x11, 2, 0, 0));
  core.startExecution(0);

  core.stepInstruction();
  core.stepInstruction();
  REQUIRE(core.lo() == UINT32_C(0x12345678));

  core.stepInstruction();
  core.stepInstruction();
  REQUIRE(core.hi() == UINT32_C(0x9abcdef0));
  REQUIRE(core.executionState() == IOPExecutionState::Running);
  REQUIRE(core.retiredInstructions() == 4);
}

TEST_CASE("IOP rejects overwriting an unread multiply divide result",
  "[iop][execution][hilo]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setGeneralRegister(&core, 1, 6);
  IOPCoreTestAccess::setGeneralRegister(&core, 2, 7);
  IOPCoreTestAccess::setGeneralRegister(
    &core,
    3,
    UINT32_C(0x12345678));
  loadInstruction(&bus, 0, registerInstruction(0x18, 1, 2, 0));
  loadInstruction(&bus, 4, registerInstruction(0x11, 3, 0, 0));
  core.startExecution(0);

  core.stepInstruction();
  REQUIRE(core.hi() == 0);
  REQUIRE(core.lo() == 42);

  core.stepInstruction();

  REQUIRE(core.hi() == 0);
  REQUIRE(core.lo() == 42);
  REQUIRE(core.programCounter() == 4);
  REQUIRE(core.executionState() == IOPExecutionState::Halted);
  REQUIRE(core.stopReason() == IOPStopReason::UndefinedOperation);
  REQUIRE(core.retiredInstructions() == 1);
}

TEST_CASE("IOP preserves unread multiply divide halves independently",
  "[iop][execution][hilo]")
{
  struct HalfReadCase
  {
    std::uint8_t readFunction;
    std::uint8_t writeFunction;
  };
  const HalfReadCase cases[] = {
    {0x10, 0x13},
    {0x12, 0x11}
  };

  for (const HalfReadCase &test : cases)
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, 6);
    IOPCoreTestAccess::setGeneralRegister(&core, 2, 7);
    IOPCoreTestAccess::setGeneralRegister(
      &core,
      3,
      UINT32_C(0x12345678));
    loadInstruction(&bus, 0, registerInstruction(0x18, 1, 2, 0));
    loadInstruction(
      &bus,
      4,
      registerInstruction(test.readFunction, 0, 0, 4));
    loadInstruction(
      &bus,
      8,
      registerInstruction(test.writeFunction, 3, 0, 0));
    core.startExecution(0);

    core.stepInstruction();
    core.stepInstruction();
    core.stepInstruction();

    REQUIRE(core.hi() == 0);
    REQUIRE(core.lo() == 42);
    REQUIRE(core.programCounter() == 8);
    REQUIRE(core.executionState() == IOPExecutionState::Halted);
    REQUIRE(core.stopReason() == IOPStopReason::UndefinedOperation);
    REQUIRE(core.retiredInstructions() == 2);
  }
}
