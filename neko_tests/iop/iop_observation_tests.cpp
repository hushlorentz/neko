#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <vector>

#include "catch.hpp"
#include "iop_bus.hpp"
#include "iop_core.hpp"
#include "neko_system.hpp"

struct IOPObservationTestAccess
{
  using Kind = IOPCore::StepEventKind;
  using MemoryKind = IOPCore::MemoryAccessKind;

  static void enable(IOPCore &core) { core.enableObservations(); }
  static void disable(IOPCore &core) { core.disableObservations(); }
  static bool enabled(const IOPCore &core) { return core.observationsEnabled; }
  static std::size_t count(const IOPCore &core) { return core.stepEventCount; }
  static const IOPCore::StepEvent &event(const IOPCore &core, std::size_t index)
  {
    return core.stepEvents.at(index);
  }
  static void setRegister(IOPCore &core, std::size_t index, IOPWord value)
  {
    core.setGeneralRegister(index, value);
  }
  static void setStatus(IOPCore &core, IOPWord status) { core.cop0.status = status; }
  static void setDelayed(IOPCore &core, std::uint8_t destination, IOPWord value)
  {
    core.delayedResult = {IOPDelayedResultSource::Load, destination, value};
  }
  static void fillEvents(IOPCore &core)
  {
    core.clearObservations();
    for (std::size_t index = 0; index < IOPCore::STEP_EVENT_CAPACITY; ++index)
    {
      core.recordStepEvent(IOPCore::InstructionIssuedEvent{});
    }
  }
  static void appendEvent(IOPCore &core)
  {
    core.recordStepEvent(IOPCore::InstructionIssuedEvent{});
  }
  static std::size_t capacity() { return IOPCore::STEP_EVENT_CAPACITY; }
};

namespace
{
  using Access = IOPObservationTestAccess;
  using Kind = Access::Kind;
  using MemoryKind = Access::MemoryKind;
  constexpr IOPAddress CODE = UINT32_C(0xa0000000);

  IOPWord immediate(std::uint8_t opcode, std::uint8_t base,
    std::uint8_t target, std::uint16_t offset)
  {
    return (IOPWord(opcode) << 26) | (IOPWord(base) << 21) |
      (IOPWord(target) << 16) | offset;
  }

  void install(IOPBus &bus, IOPAddress address, IOPWord word)
  {
    REQUIRE(bus.write32(address, word).status == IOPBusStatus::Completed);
  }

  struct Fixture
  {
    IOPBus bus;
    IOPCore core;

    Fixture()
    {
      core.attachBus(&bus);
      core.startExecution(CODE);
      Access::enable(core);
    }
  };

  void requireKinds(const IOPCore &core, std::initializer_list<Kind> kinds)
  {
    REQUIRE(Access::count(core) == kinds.size());
    std::size_t index = 0;
    for (const Kind kind : kinds)
    {
      REQUIRE(Access::event(core, index++).kind == kind);
    }
  }

  void requireSameEvents(const IOPCore &left, const IOPCore &right)
  {
    REQUIRE(Access::count(left) == Access::count(right));
    for (std::size_t index = 0; index < Access::count(left); ++index)
    {
      const auto &a = Access::event(left, index);
      const auto &b = Access::event(right, index);
      REQUIRE(a.kind == b.kind);
      switch (a.kind)
      {
        case Kind::InstructionIssued:
          REQUIRE(a.payload.instructionIssued.address == b.payload.instructionIssued.address);
          REQUIRE(a.payload.instructionIssued.instruction == b.payload.instructionIssued.instruction);
          REQUIRE(a.payload.instructionIssued.inBranchDelaySlot == b.payload.instructionIssued.inBranchDelaySlot);
          break;
        case Kind::InstructionRetired:
          REQUIRE(a.payload.instructionRetired.address == b.payload.instructionRetired.address);
          REQUIRE(a.payload.instructionRetired.nextAddress == b.payload.instructionRetired.nextAddress);
          REQUIRE(a.payload.instructionRetired.instructions == b.payload.instructionRetired.instructions);
          REQUIRE(a.payload.instructionRetired.cycles == b.payload.instructionRetired.cycles);
          break;
        case Kind::BranchScheduled:
          REQUIRE(a.payload.branchScheduled.address == b.payload.branchScheduled.address);
          REQUIRE(a.payload.branchScheduled.target == b.payload.branchScheduled.target);
          REQUIRE(a.payload.branchScheduled.outcome == b.payload.branchScheduled.outcome);
          break;
        case Kind::MemoryAccess:
          REQUIRE(a.payload.memoryAccess.kind == b.payload.memoryAccess.kind);
          REQUIRE(a.payload.memoryAccess.virtualAddress == b.payload.memoryAccess.virtualAddress);
          REQUIRE(a.payload.memoryAccess.physicalAddress == b.payload.memoryAccess.physicalAddress);
          REQUIRE(a.payload.memoryAccess.value == b.payload.memoryAccess.value);
          REQUIRE(a.payload.memoryAccess.outcome == b.payload.memoryAccess.outcome);
          REQUIRE(a.payload.memoryAccess.cacheRoute == b.payload.memoryAccess.cacheRoute);
          REQUIRE(a.payload.memoryAccess.width == b.payload.memoryAccess.width);
          REQUIRE(a.payload.memoryAccess.byteEnable == b.payload.memoryAccess.byteEnable);
          REQUIRE(a.payload.memoryAccess.physicalAddressValid == b.payload.memoryAccess.physicalAddressValid);
          break;
        case Kind::ExceptionEntered:
          REQUIRE(a.payload.exceptionEntered.epc == b.payload.exceptionEntered.epc);
          REQUIRE(a.payload.exceptionEntered.vector == b.payload.exceptionEntered.vector);
          REQUIRE(a.payload.exceptionEntered.cause == b.payload.exceptionEntered.cause);
          break;
        case Kind::InterruptDelivered:
          REQUIRE(a.payload.interruptDelivered.address == b.payload.interruptDelivered.address);
          REQUIRE(a.payload.interruptDelivered.status == b.payload.interruptDelivered.status);
          REQUIRE(a.payload.interruptDelivered.cause == b.payload.interruptDelivered.cause);
          REQUIRE(a.payload.interruptDelivered.vector == b.payload.interruptDelivered.vector);
          break;
        case Kind::StateChanged:
          REQUIRE(a.payload.stateChanged.before == b.payload.stateChanged.before);
          REQUIRE(a.payload.stateChanged.after == b.payload.stateChanged.after);
          REQUIRE(a.payload.stateChanged.reason == b.payload.stateChanged.reason);
          REQUIRE(a.payload.stateChanged.address == b.payload.stateChanged.address);
          break;
      }
    }
  }
}

TEST_CASE("IOP observations distinguish fetch attempt and retirement",
  "[iop][observation]")
{
  Fixture fixture;
  install(fixture.bus, 0, 0);
  fixture.core.stepInstruction();
  requireKinds(fixture.core,
    {Kind::MemoryAccess, Kind::InstructionIssued, Kind::InstructionRetired});
  const auto &fetch = Access::event(fixture.core, 0).payload.memoryAccess;
  REQUIRE(fetch.kind == MemoryKind::InstructionFetch);
  REQUIRE(fetch.virtualAddress == CODE);
  REQUIRE(fetch.physicalAddress == 0);
  REQUIRE(fetch.physicalAddressValid);
  REQUIRE(fetch.cacheRoute == IOPCacheRoute::Uncached);
  REQUIRE(fetch.outcome == IOPAccessOutcome::Completed);
  REQUIRE(fetch.width == 4);
  REQUIRE(fetch.value == 0);
  REQUIRE(fetch.byteEnable == 0);
  const auto &issued = Access::event(fixture.core, 1).payload.instructionIssued;
  REQUIRE(issued.address == CODE);
  REQUIRE(issued.instruction == 0);
  REQUIRE_FALSE(issued.inBranchDelaySlot);
  const auto &retired = Access::event(fixture.core, 2).payload.instructionRetired;
  REQUIRE(retired.address == CODE);
  REQUIRE(retired.nextAddress == CODE + 4);
  REQUIRE(retired.instructions == 1);
  REQUIRE(retired.cycles == 1);
  fixture.core.stepInstruction();
  REQUIRE(Access::count(fixture.core) == 3);
  REQUIRE(Access::event(fixture.core, 2).payload.instructionRetired.instructions == 2);
}

TEST_CASE("IOP branch observations retain decisions even at the fallthrough target",
  "[iop][observation]")
{
  for (const bool equal : {false, true})
  {
    Fixture fixture;
    install(fixture.bus, 0, immediate(4, 1, 0, 1));
    Access::setRegister(fixture.core, 1, equal ? 0 : 1);
    // The older load changes the source at retirement, not at branch evaluation.
    Access::setDelayed(fixture.core, 1, equal ? 1 : 0);
    fixture.core.stepInstruction();
    requireKinds(fixture.core, {Kind::MemoryAccess, Kind::InstructionIssued,
      Kind::BranchScheduled, Kind::InstructionRetired});
    const auto &branch = Access::event(fixture.core, 2).payload.branchScheduled;
    REQUIRE(branch.address == CODE);
    REQUIRE(branch.target == CODE + 8);
    REQUIRE(branch.outcome ==
      (equal ? IOPBranchOutcome::Taken : IOPBranchOutcome::NotTaken));
    REQUIRE(fixture.core.generalRegister(1) == (equal ? 1 : 0));
    fixture.core.stepInstruction();
    REQUIRE(Access::event(fixture.core, 1).payload.instructionIssued.inBranchDelaySlot);
    REQUIRE(Access::event(fixture.core, 2).payload.instructionRetired.nextAddress ==
      CODE + 8);
  }
}

TEST_CASE("IOP observations identify every decoded branch family",
  "[iop][observation]")
{
  const IOPWord branches[] = {
    0x00200008, 0x00201009, 0x08000004, 0x0c000004,
    0x04200003, 0x04210003, 0x04300003, 0x04310003,
    0x10200003, 0x14200003, 0x18200003, 0x1c200003
  };
  for (std::size_t index = 0; index < 12; ++index)
  {
    Fixture fixture;
    install(fixture.bus, 0, branches[index]);
    Access::setRegister(fixture.core, 1,
      index < 2 ? CODE + 16 : UINT32_C(0xffffffff));
    fixture.core.stepInstruction();
    requireKinds(fixture.core, {Kind::MemoryAccess, Kind::InstructionIssued,
      Kind::BranchScheduled, Kind::InstructionRetired});
    const auto &branch = Access::event(fixture.core, 2).payload.branchScheduled;
    const bool taken = index != 5 && index != 7 && index != 8 && index != 11;
    REQUIRE(branch.outcome ==
      (taken ? IOPBranchOutcome::Taken : IOPBranchOutcome::NotTaken));
    REQUIRE(branch.target == CODE + (taken ? 16 : 8));
  }
}

TEST_CASE("IOP load observations retain raw access values and containing words",
  "[iop][observation]")
{
  struct LoadCase
  {
    std::uint8_t opcode;
    std::uint16_t offset;
    std::uint8_t width;
    IOPWord raw;
  };
  for (const auto test : {
       LoadCase{0x20, 1, 1, 0xcd}, LoadCase{0x24, 1, 1, 0xcd},
       LoadCase{0x21, 2, 2, 0x89ab}, LoadCase{0x25, 2, 2, 0x89ab},
       LoadCase{0x23, 0, 4, 0x89abcdef},
       LoadCase{0x22, 1, 4, 0x89abcdef}, LoadCase{0x26, 3, 4, 0x89abcdef}})
  {
    for (const IOPAddress base : {IOPAddress(0x100), CODE + 0x100})
    {
      Fixture fixture;
      install(fixture.bus, 0, immediate(test.opcode, 1, 2, test.offset));
      install(fixture.bus, 0x100, 0x89abcdef);
      Access::setRegister(fixture.core, 1, base);
      fixture.core.stepInstruction();
      requireKinds(fixture.core, {Kind::MemoryAccess, Kind::InstructionIssued,
        Kind::MemoryAccess, Kind::InstructionRetired});
      const auto &memory = Access::event(fixture.core, 2).payload.memoryAccess;
      const bool merge = test.opcode == 0x22 || test.opcode == 0x26;
      REQUIRE(memory.kind == MemoryKind::DataLoad);
      REQUIRE(memory.virtualAddress == base + (merge ? 0 : test.offset));
      REQUIRE(memory.physicalAddress == 0x100 + (merge ? 0 : test.offset));
      REQUIRE(memory.physicalAddressValid);
      REQUIRE(memory.value == test.raw);
      REQUIRE(memory.width == test.width);
      REQUIRE(memory.byteEnable == 0);
      REQUIRE(memory.outcome == IOPAccessOutcome::Completed);
      REQUIRE(memory.cacheRoute ==
        (base == 0x100 ? IOPCacheRoute::Cached : IOPCacheRoute::Uncached));
      REQUIRE(fixture.core.generalRegister(2) == 0);
      REQUIRE(fixture.core.delayedResultContinuation().source ==
        IOPDelayedResultSource::Load);
    }
  }
}

TEST_CASE("IOP store observations preserve widths values and byte lanes",
  "[iop][observation]")
{
  for (const std::uint8_t opcode : {0x28, 0x29, 0x2b, 0x2a, 0x2e})
  {
    for (std::uint16_t offset = 0; offset < 4; ++offset)
    {
      const bool merge = opcode == 0x2a || opcode == 0x2e;
      const std::uint8_t width = opcode == 0x28 ? 1 : opcode == 0x29 ? 2 : 4;
      if (!merge && offset % width != 0)
      {
        continue;
      }
      Fixture fixture;
      install(fixture.bus, 0, immediate(opcode, 1, 2, offset));
      Access::setRegister(fixture.core, 1, CODE + 0x100);
      Access::setRegister(fixture.core, 2, 0x89abcdef);
      fixture.core.stepInstruction();
      requireKinds(fixture.core, {Kind::MemoryAccess, Kind::InstructionIssued,
        Kind::MemoryAccess, Kind::InstructionRetired});
      const auto &memory = Access::event(fixture.core, 2).payload.memoryAccess;
      const IOPWord value = opcode == 0x2a ? IOPWord(0x89abcdef) >> (8 * (3 - offset))
        : opcode == 0x2e ? IOPWord(0x89abcdef) << (8 * offset)
        : IOPWord(0x89abcdef) & (UINT32_MAX >> ((4 - width) * 8));
      const std::uint8_t lanes = opcode == 0x2a ? 0xf >> (3 - offset)
        : opcode == 0x2e ? (0xf << offset) & 0xf
        : ((1u << width) - 1) << offset;
      REQUIRE(memory.kind == MemoryKind::DataStore);
      REQUIRE(memory.virtualAddress == CODE + 0x100 + (merge ? 0 : offset));
      REQUIRE(memory.physicalAddress == 0x100 + (merge ? 0 : offset));
      REQUIRE(memory.physicalAddressValid);
      REQUIRE(memory.width == width);
      REQUIRE(memory.value == value);
      REQUIRE(memory.byteEnable == lanes);
      REQUIRE(memory.outcome == IOPAccessOutcome::Completed);
    }
  }
}

TEST_CASE("IOP failed fetch observations precede exception entry without issue",
  "[iop][observation]")
{
  struct FetchCase
  {
    IOPAddress address;
    IOPAccessOutcome outcome;
    IOPWord code;
    bool userMode;
  };
  for (const auto test : {
       FetchCase{CODE + 1, IOPAccessOutcome::Misaligned, 4, false},
       FetchCase{0xbe000000, IOPAccessOutcome::Unmapped, 6, false},
       FetchCase{CODE, IOPAccessOutcome::ProtectionFailure, 4, true}})
  {
    Fixture fixture;
    fixture.core.startExecution(test.address);
    if (test.userMode)
    {
      Access::setStatus(fixture.core, IOPCOP0Status::CURRENT_USER_MODE |
        IOPCOP0Status::BOOT_EXCEPTION_VECTORS);
    }
    fixture.core.stepInstruction();
    requireKinds(fixture.core, {Kind::MemoryAccess, Kind::ExceptionEntered});
    const auto &fetch = Access::event(fixture.core, 0).payload.memoryAccess;
    REQUIRE(fetch.kind == MemoryKind::InstructionFetch);
    REQUIRE(fetch.virtualAddress == test.address);
    REQUIRE(fetch.width == 4);
    REQUIRE(fetch.value == 0);
    REQUIRE(fetch.outcome == test.outcome);
    REQUIRE(fetch.physicalAddressValid == !test.userMode);
    const auto &exception = Access::event(fixture.core, 1).payload.exceptionEntered;
    REQUIRE(((exception.cause >> 2) & 0x1f) == test.code);
    REQUIRE(exception.epc == test.address);
    REQUIRE(exception.vector == IOPExceptionVector::BOOT_GENERAL);
    REQUIRE(fixture.core.retiredInstructions() == 0);
  }
}

TEST_CASE("IOP data faults observe failure without retiring the instruction",
  "[iop][observation]")
{
  struct FaultCase
  {
    std::uint8_t opcode;
    IOPAddress base;
    std::uint16_t offset;
    std::uint8_t width;
    IOPAccessOutcome outcome;
    IOPWord code;
    bool user;
  };
  for (const auto test : {
       FaultCase{0x20, 0xbe000000, 0, 1, IOPAccessOutcome::Unmapped, 7, false},
       FaultCase{0x21, 0xbe000000, 0, 2, IOPAccessOutcome::Unmapped, 7, false},
       FaultCase{0x23, 0xbe000000, 0, 4, IOPAccessOutcome::Unmapped, 7, false},
       FaultCase{0x21, CODE, 1, 2, IOPAccessOutcome::Misaligned, 4, false},
       FaultCase{0x23, CODE, 2, 4, IOPAccessOutcome::Misaligned, 4, false},
       FaultCase{0x29, CODE, 1, 2, IOPAccessOutcome::Misaligned, 5, false},
       FaultCase{0x2b, CODE, 2, 4, IOPAccessOutcome::Misaligned, 5, false},
       FaultCase{0x23, CODE, 0, 4, IOPAccessOutcome::ProtectionFailure, 4, true},
       FaultCase{0x2b, CODE, 0, 4, IOPAccessOutcome::ProtectionFailure, 5, true},
       FaultCase{0x22, 0xbe000000, 3, 4, IOPAccessOutcome::Unmapped, 7, false}})
  {
    Fixture fixture;
    install(fixture.bus, 0, immediate(test.opcode, 1, 2, test.offset));
    if (test.user)
    {
      fixture.core.startExecution(0);
      Access::setStatus(fixture.core, IOPCOP0Status::CURRENT_USER_MODE |
        IOPCOP0Status::BOOT_EXCEPTION_VECTORS);
    }
    Access::setRegister(fixture.core, 1, test.base);
    Access::setRegister(fixture.core, 2, 0x89abcdef);
    fixture.core.stepInstruction();
    requireKinds(fixture.core, {Kind::MemoryAccess, Kind::InstructionIssued,
      Kind::MemoryAccess, Kind::ExceptionEntered});
    const auto &memory = Access::event(fixture.core, 2).payload.memoryAccess;
    const bool store = test.opcode == 0x29 || test.opcode == 0x2b;
    REQUIRE(memory.kind == (store ? MemoryKind::DataStore : MemoryKind::DataLoad));
    REQUIRE(memory.width == test.width);
    REQUIRE(memory.virtualAddress == test.base +
      (test.opcode == 0x22 ? 0 : test.offset));
    REQUIRE(memory.physicalAddressValid == !test.user);
    REQUIRE(memory.outcome == test.outcome);
    REQUIRE(memory.value == (store
      ? test.width == 2 ? 0xcdef : 0x89abcdef : 0));
    REQUIRE(((Access::event(fixture.core, 3).payload.exceptionEntered.cause >> 2) &
      0x1f) == test.code);
    REQUIRE(fixture.core.generalRegister(2) == 0x89abcdef);
    REQUIRE(fixture.core.retiredInstructions() == 0);
  }
}

TEST_CASE("IOP delay-slot faults retain branch provenance and older load writeback",
  "[iop][observation]")
{
  Fixture fixture;
  install(fixture.bus, 0, immediate(4, 0, 0, 3));
  install(fixture.bus, 4, immediate(0x23, 0, 2, 3));
  fixture.core.stepInstruction();
  Access::setDelayed(fixture.core, 5, 0x12345678);
  fixture.core.stepInstruction();
  requireKinds(fixture.core, {Kind::MemoryAccess, Kind::InstructionIssued,
    Kind::MemoryAccess, Kind::ExceptionEntered});
  REQUIRE(Access::event(fixture.core, 1).payload.instructionIssued.inBranchDelaySlot);
  const auto &exception = Access::event(fixture.core, 3).payload.exceptionEntered;
  REQUIRE(exception.epc == CODE);
  REQUIRE(exception.cause == (IOPCOP0Cause::BRANCH_DELAY | 0x10));
  REQUIRE(fixture.core.generalRegister(5) == 0x12345678);
  REQUIRE(fixture.core.delayedResultContinuation().source ==
    IOPDelayedResultSource::None);
  REQUIRE(fixture.core.retiredInstructions() == 1);
}

TEST_CASE("IOP rejected stores observe host failure without consuming continuations",
  "[iop][observation]")
{
  for (const bool readOnly : {false, true})
  {
    Fixture fixture;
    if (readOnly)
    {
      REQUIRE(fixture.bus.installRom(BootROMImage::create(
        std::vector<std::uint8_t>(BootROMImage::SIZE, 0))));
    }
    install(fixture.bus, 0, immediate(4, 0, 0, 3));
    install(fixture.bus, 4, immediate(0x2e, 1, 2, 1));
    Access::setRegister(fixture.core, 1, readOnly ? 0xbfc00000 : 0xbe000000);
    Access::setRegister(fixture.core, 2, 0x89abcdef);
    fixture.core.stepInstruction();
    Access::setDelayed(fixture.core, 5, 0x12345678);
    fixture.core.stepInstruction();
    requireKinds(fixture.core, {Kind::MemoryAccess, Kind::InstructionIssued,
      Kind::MemoryAccess, Kind::StateChanged});
    const auto &memory = Access::event(fixture.core, 2).payload.memoryAccess;
    REQUIRE(memory.outcome ==
      (readOnly ? IOPAccessOutcome::ReadOnly : IOPAccessOutcome::Unmapped));
    REQUIRE(memory.width == 4);
    REQUIRE(memory.byteEnable == 0xe);
    REQUIRE(memory.value == 0xabcdef00);
    REQUIRE(memory.virtualAddress == (readOnly ? 0xbfc00000 : 0xbe000000));
    const auto &state = Access::event(fixture.core, 3).payload.stateChanged;
    REQUIRE(state.before == IOPExecutionState::Running);
    REQUIRE(state.after == IOPExecutionState::Halted);
    REQUIRE(state.reason == IOPStopReason::StoreFailure);
    REQUIRE(state.address == CODE + 4);
    REQUIRE(fixture.core.retiredInstructions() == 1);
    REQUIRE(fixture.core.elapsedCycles() == 2);
    REQUIRE(fixture.core.branchContinuation().active);
    REQUIRE(fixture.core.delayedResultContinuation().source ==
      IOPDelayedResultSource::Load);
    REQUIRE(fixture.core.generalRegister(5) == 0);
    REQUIRE(fixture.core.exception().kind == IOPException::None);
  }
}

TEST_CASE("IOP exception observations distinguish reserved deferred and undefined operations",
  "[iop][observation]")
{
  SECTION("reserved instruction and syscall enter without retirement")
  {
    for (const IOPWord raw : {IOPWord(0xffffffff), IOPWord(0x0000000c)})
    {
      Fixture fixture;
      install(fixture.bus, 0, raw);
      fixture.core.stepInstruction();
      requireKinds(fixture.core,
        {Kind::MemoryAccess, Kind::InstructionIssued, Kind::ExceptionEntered});
      REQUIRE(Access::event(fixture.core, 1).payload.instructionIssued.instruction == raw);
      REQUIRE(((Access::event(fixture.core, 2).payload.exceptionEntered.cause >> 2) &
        0x1f) == (raw == 0xffffffff ? 10 : 8));
      REQUIRE(fixture.core.retiredInstructions() == 0);
    }
  }
  SECTION("deferred operation halts without a guest exception")
  {
    Fixture fixture;
    install(fixture.bus, 0, 0x4a000000);
    Access::setStatus(fixture.core, IOPWord(1) << 30);
    fixture.core.stepInstruction();
    requireKinds(fixture.core,
      {Kind::MemoryAccess, Kind::InstructionIssued, Kind::StateChanged});
    REQUIRE(Access::event(fixture.core, 2).payload.stateChanged.reason ==
      IOPStopReason::ExecutionException);
  }
  SECTION("undefined HI LO overwrite halts without a guest exception")
  {
    Fixture fixture;
    install(fixture.bus, 0, 0x18);
    install(fixture.bus, 4, 0x11);
    fixture.core.stepInstruction();
    fixture.core.stepInstruction();
    requireKinds(fixture.core,
      {Kind::MemoryAccess, Kind::InstructionIssued, Kind::StateChanged});
    REQUIRE(Access::event(fixture.core, 2).payload.stateChanged.reason ==
      IOPStopReason::UndefinedOperation);
    REQUIRE(fixture.core.retiredInstructions() == 1);
  }
}

TEST_CASE("IOP interrupt observations precede an unattempted delay-slot fetch",
  "[iop][observation]")
{
  Fixture fixture;
  install(fixture.bus, 0, immediate(4, 0, 0, 3));
  fixture.core.stepInstruction();
  Access::setDelayed(fixture.core, 5, 0x12345678);
  const IOPWord status = IOPCOP0Status::BOOT_EXCEPTION_VECTORS |
    IOPCOP0Status::INTERRUPT_ENABLE | 0x400;
  Access::setStatus(fixture.core, status);
  fixture.core.setInterruptInput(IOPInterruptInput::Hardware0, true);
  fixture.core.stepInstruction();
  requireKinds(fixture.core, {Kind::InterruptDelivered, Kind::ExceptionEntered});
  const auto &interrupt = Access::event(fixture.core, 0).payload.interruptDelivered;
  REQUIRE(interrupt.address == CODE + 4);
  REQUIRE(interrupt.status == status);
  REQUIRE(interrupt.cause == (IOPCOP0Cause::BRANCH_DELAY | 0x400));
  REQUIRE(interrupt.vector == IOPExceptionVector::BOOT_GENERAL);
  REQUIRE(Access::event(fixture.core, 1).payload.exceptionEntered.epc == CODE);
  REQUIRE(fixture.core.generalRegister(5) == 0x12345678);
  REQUIRE(fixture.core.retiredInstructions() == 1);
  REQUIRE(fixture.core.elapsedCycles() == 2);
}

TEST_CASE("IOP MFC0 retirement observations preserve delayed result semantics",
  "[iop][observation]")
{
  Fixture fixture;
  install(fixture.bus, 0, 0x40027800);
  install(fixture.bus, 4, 0xffffffff);
  fixture.core.stepInstruction();
  requireKinds(fixture.core,
    {Kind::MemoryAccess, Kind::InstructionIssued, Kind::InstructionRetired});
  REQUIRE(fixture.core.generalRegister(2) == 0);
  REQUIRE(fixture.core.delayedResultContinuation().source ==
    IOPDelayedResultSource::COP0Transfer);
  fixture.core.stepInstruction();
  REQUIRE(fixture.core.generalRegister(2) == IOPCOP0::PROCESSOR_REVISION);
  REQUIRE(fixture.core.retiredInstructions() == 1);
}

TEST_CASE("IOP observations report committed exception cause and vector selection",
  "[iop][observation]")
{
  struct TrapCase
  {
    IOPWord instruction;
    IOPWord cause;
  };
  for (const bool bootstrap : {false, true})
  {
    for (const auto test : {
         TrapCase{0x0000000d, 9 << 2},
         TrapCase{0x20220001, 12 << 2},
         TrapCase{0x4a000000, (2u << 28) | (11 << 2)}})
    {
      Fixture fixture;
      install(fixture.bus, 0, test.instruction);
      Access::setRegister(fixture.core, 1, 0x7fffffff);
      Access::setStatus(fixture.core,
        bootstrap ? IOPCOP0Status::BOOT_EXCEPTION_VECTORS : 0);
      fixture.core.stepInstruction();
      requireKinds(fixture.core,
        {Kind::MemoryAccess, Kind::InstructionIssued, Kind::ExceptionEntered});
      const auto &exception = Access::event(fixture.core, 2).payload.exceptionEntered;
      REQUIRE(exception.epc == CODE);
      REQUIRE(exception.cause == test.cause);
      REQUIRE(exception.vector == (bootstrap
        ? IOPExceptionVector::BOOT_GENERAL : IOPExceptionVector::GENERAL));
      REQUIRE(exception.vector == fixture.core.programCounter());
      REQUIRE(fixture.core.retiredInstructions() == 0);
    }
  }
}

TEST_CASE("IOP observations capture device reads without repeating acknowledgements",
  "[iop][observation]")
{
  NekoSystem system;
  auto &core = system.iopCore();
  install(system.iopBus(), 0, immediate(0x23, 1, 2, 0));
  Access::setRegister(core, 1, IOPBus::INTC_CTRL);
  system.iopInterruptController().writeControl(1);
  core.startExecution(CODE);
  Access::enable(core);
  core.stepInstruction();
  requireKinds(core, {Kind::MemoryAccess, Kind::InstructionIssued,
    Kind::MemoryAccess, Kind::InstructionRetired});
  REQUIRE(Access::event(core, 2).payload.memoryAccess.value == 1);
  REQUIRE(system.iopInterruptController().control() == 0);
  REQUIRE(core.delayedResultContinuation().value == 1);
}

TEST_CASE("IOP cached and isolated operations remain single architectural observations",
  "[iop][observation]")
{
  Fixture fixture;
  install(fixture.bus, 0, immediate(0x23, 1, 2, 0));
  install(fixture.bus, 4, immediate(0x2b, 1, 2, 0));
  install(fixture.bus, 0x100, 0x12345678);
  fixture.core.startExecution(0x80000000);
  Access::setRegister(fixture.core, 1, 0x80000100);
  fixture.core.stepInstruction();
  requireKinds(fixture.core, {Kind::MemoryAccess, Kind::InstructionIssued,
    Kind::MemoryAccess, Kind::InstructionRetired});
  REQUIRE(Access::event(fixture.core, 0).payload.memoryAccess.cacheRoute ==
    IOPCacheRoute::Cached);
  REQUIRE(Access::event(fixture.core, 2).payload.memoryAccess.value == 0x12345678);
  Access::setStatus(fixture.core, IOPCOP0Status::CACHE_ISOLATED);
  Access::setRegister(fixture.core, 2, 0x89abcdef);
  fixture.core.stepInstruction();
  requireKinds(fixture.core, {Kind::MemoryAccess, Kind::InstructionIssued,
    Kind::MemoryAccess, Kind::InstructionRetired});
  REQUIRE(Access::event(fixture.core, 2).payload.memoryAccess.value == 0x89abcdef);
  REQUIRE(fixture.bus.read32(0x100).value == 0x12345678);
}

TEST_CASE("IOP observation lifecycle is bounded and does not invent host memory execution",
  "[iop][observation]")
{
  Fixture fixture;
  REQUIRE(Access::capacity() == 4);
  fixture.core.haltExecution();
  requireKinds(fixture.core, {Kind::StateChanged});
  REQUIRE(Access::event(fixture.core, 0).payload.stateChanged.reason ==
    IOPStopReason::HostHalt);
  fixture.core.resumeExecution();
  requireKinds(fixture.core, {Kind::StateChanged});
  REQUIRE(Access::event(fixture.core, 0).payload.stateChanged.after ==
    IOPExecutionState::Running);
  fixture.core.reset();
  REQUIRE(Access::count(fixture.core) == 0);
  REQUIRE(Access::enabled(fixture.core));
  fixture.core.startExecution(CODE);
  requireKinds(fixture.core, {Kind::StateChanged});
  REQUIRE(fixture.core.readData32(CODE).outcome == IOPAccessOutcome::Completed);
  REQUIRE(fixture.core.writeData8(CODE + 0x100, 1).outcome == IOPAccessOutcome::Completed);
  requireKinds(fixture.core, {Kind::StateChanged});
  fixture.core.stepInstruction();
  REQUIRE(Access::count(fixture.core) == 3);
  Access::disable(fixture.core);
  REQUIRE(Access::count(fixture.core) == 0);
  fixture.core.stepInstruction();
  REQUIRE(Access::count(fixture.core) == 0);
  Access::enable(fixture.core);
  Access::fillEvents(fixture.core);
  REQUIRE_THROWS_AS(Access::appendEvent(fixture.core), std::logic_error);
  REQUIRE(Access::count(fixture.core) == Access::capacity());
}

TEST_CASE("IOP observation state is excluded from hashes and transactional persistence",
  "[iop][observation]")
{
  NekoSystem observed;
  NekoSystem plain;
  for (NekoSystem *system : {&observed, &plain})
  {
    install(system->iopBus(), 0, immediate(0x23, 0, 1, 0x100));
    install(system->iopBus(), 4, immediate(4, 0, 0, 1));
    install(system->iopBus(), 8, 0);
    install(system->iopBus(), 12, 0);
    install(system->iopBus(), 0x100, 0x12345678);
    system->iopCore().startExecution(CODE);
  }
  Access::enable(observed.iopCore());
  for (unsigned step = 0; step < 4; ++step)
  {
    observed.iopCore().stepInstruction();
    plain.iopCore().stepInstruction();
    REQUIRE(Access::count(observed.iopCore()) != 0);
    REQUIRE(Access::count(plain.iopCore()) == 0);
    REQUIRE(observed.iopStateHash() == plain.iopStateHash());
    REQUIRE(observed.saveState() == plain.saveState());
    const auto saved = observed.saveState();
    observed.loadState(saved);
    REQUIRE(Access::enabled(observed.iopCore()));
    REQUIRE(Access::count(observed.iopCore()) == 0);
    plain.loadState(saved);
    REQUIRE_FALSE(Access::enabled(plain.iopCore()));
  }
  observed.iopCore().stepInstruction();
  const auto count = Access::count(observed.iopCore());
  auto damaged = observed.saveState();
  damaged.pop_back();
  REQUIRE_THROWS(observed.loadState(damaged));
  REQUIRE(Access::enabled(observed.iopCore()));
  REQUIRE(Access::count(observed.iopCore()) == count);
}

TEST_CASE("IOP observation sequences repeat across execution and delayed restore",
  "[iop][observation]")
{
  NekoSystem original;
  NekoSystem repeated;
  for (NekoSystem *system : {&original, &repeated})
  {
    install(system->iopBus(), 0, immediate(0x23, 0, 1, 0x100));
    install(system->iopBus(), 4, immediate(4, 0, 0, 1));
    install(system->iopBus(), 8, immediate(0x29, 0, 1, 3));
    install(system->iopBus(), 0x100, 0x12345678);
    system->iopCore().startExecution(CODE);
    Access::enable(system->iopCore());
  }
  for (unsigned step = 0; step < 3; ++step)
  {
    original.iopCore().stepInstruction();
    repeated.iopCore().stepInstruction();
    requireSameEvents(original.iopCore(), repeated.iopCore());
    const auto saved = original.saveState();
    repeated.loadState(saved);
    REQUIRE(Access::count(repeated.iopCore()) == 0);
  }
  REQUIRE(original.iopCore().exception().inBranchDelaySlot);
  original.iopCore().haltExecution();
  repeated.iopCore().haltExecution();
  requireSameEvents(original.iopCore(), repeated.iopCore());
  original.iopCore().resumeExecution();
  repeated.iopCore().resumeExecution();
  Access::setStatus(original.iopCore(),
    IOPCOP0Status::BOOT_EXCEPTION_VECTORS | IOPCOP0Status::INTERRUPT_ENABLE | 0x400);
  Access::setStatus(repeated.iopCore(), original.iopCore().status());
  original.iopCore().setInterruptInput(IOPInterruptInput::Hardware0, true);
  repeated.iopCore().setInterruptInput(IOPInterruptInput::Hardware0, true);
  original.iopCore().stepInstruction();
  repeated.iopCore().stepInstruction();
  requireSameEvents(original.iopCore(), repeated.iopCore());
}
