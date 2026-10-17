#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <vector>

#include "catch.hpp"
#include "iop_bus.hpp"
#include "iop_core.hpp"
#include "iop_instruction.hpp"
#include "iop_interrupt_controller.hpp"
#include "iop_timers.hpp"

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

  constexpr std::uint32_t jumpInstruction(
    std::uint8_t opcode,
    std::uint32_t target)
  {
    return
      (static_cast<std::uint32_t>(opcode) << 26) |
      (target & UINT32_C(0x03ffffff));
  }

  constexpr std::uint32_t cop0TransferInstruction(
    std::uint8_t selector,
    std::uint8_t target,
    std::uint8_t cop0Register)
  {
    return immediateInstruction(
      0x10, selector, target,
      static_cast<std::uint16_t>(cop0Register) << 11);
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
    core->directInterruptInputs = IOPCOP0Cause::HARDWARE_INTERRUPT_PENDING;
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
    core->enteredException = {
      IOPException::DataBusError,
      UINT32_C(0x2468ace0),
      true,
      2
    };
    core->rejectedStore.width = IOPStoreWidth::Word;
    core->rejectedStore.value = UINT32_C(0x89abcdef);
    core->rejectedStore.access = {
      IOPAccessOutcome::ReadOnly,
      IOPException::None,
      UINT32_C(0xbfc00000),
      UINT32_C(0x1fc00000),
      IOPCacheRoute::Uncached
    };
    core->rejectedStore.mergeByteEnable = 7;
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

  static void setCounters(
    IOPCore *core,
    IOPCycleCount cycles,
    std::uint64_t instructions)
  {
    core->cycles = cycles;
    core->retiredInstructionTotal = instructions;
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

  static IOPInstructionEffects instructionEffects(
    IOPCore &core,
    IOPWord raw)
  {
    return core.instructionEffects(decodeIOPInstruction(raw));
  }

  static void setDelayedResultContinuation(IOPCore *core)
  {
    core->delayedResult = {
      IOPDelayedResultSource::Load,
      1,
      UINT32_C(0x12345678)
    };
  }

  static void setCOP0State(IOPCore *core, const IOPCOP0State &value)
  {
    core->cop0 = value;
    for (std::uint8_t input = 0; input < 6; ++input)
    {
      core->setInterruptInput(
        static_cast<IOPInterruptInput>(input),
        (value.cause & (IOPWord(1) << (10 + input))) != 0);
    }
  }
};

TEST_CASE("IOP MFC0 reads only the approved common register fields with a delay",
  "[iop][execution][cop0]")
{
  struct ReadCase
  {
    std::uint8_t index;
    IOPWord expected;
  };
  for (const ReadCase test : {
         ReadCase{8, 0x12345679},
         ReadCase{12, 0xf27fff3f},
         ReadCase{13, 0xb000ff7c},
         ReadCase{14, 0x89abcdef},
         ReadCase{15, 0x1f}})
  {
    INFO("COP0 register " << static_cast<unsigned>(test.index));
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setCOP0State(
      &core, {0x12345679,
        test.index == 13 ? UINT32_MAX & ~IOPWord(1) : UINT32_MAX,
        test.index == 13 ? UINT32_MAX : UINT32_MAX & ~IOPWord(0xff00),
        0x89abcdef});
    IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xaabbccdd);
    // Scratchpad fetch permits testing every Status bit independently of SwC.
    const IOPAddress code = IOPBus::SCRATCHPAD_BASE;
    loadInstruction(&bus, code, cop0TransferInstruction(0, 2, test.index));
    loadInstruction(&bus, code + 4, registerInstruction(0x21, 2, 0, 3));
    loadInstruction(&bus, code + 8, registerInstruction(0x21, 2, 0, 4));
    core.startExecution(code);
    core.stepInstruction();
    REQUIRE(core.retiredInstructions() == 1);
    REQUIRE(core.generalRegister(2) == 0xaabbccdd);
    REQUIRE(core.delayedResultContinuation().source ==
      IOPDelayedResultSource::COP0Transfer);
    REQUIRE(core.delayedResultContinuation().destination == 2);
    REQUIRE(core.delayedResultContinuation().value == test.expected);
    core.stepInstruction();
    REQUIRE(core.generalRegister(3) == 0xaabbccdd);
    REQUIRE(core.generalRegister(2) == test.expected);
    REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
    core.stepInstruction();
    REQUIRE(core.generalRegister(4) == test.expected);
  }
}

TEST_CASE("IOP MTC0 status preserves fixed and read only bits and clears parity on one",
  "[iop][execution][cop0]")
{
  for (unsigned bit = 0; bit < 34; ++bit)
  {
    const IOPWord value = bit < 32
      ? IOPWord(1) << bit
      : (bit == 32 ? UINT32_MAX : 0);
    for (const IOPWord previous : {IOPWord(0x00380000), IOPWord(0xf27fff3f)})
    {
      INFO("write " << value << " over " << previous);
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      IOPCoreTestAccess::setCOP0State(&core, {0, previous, 0, 0});
      IOPCoreTestAccess::setGeneralRegister(&core, 2, value);
      loadInstruction(&bus, IOPBus::SCRATCHPAD_BASE,
        cop0TransferInstruction(4, 2, 12));
      core.startExecution(IOPBus::SCRATCHPAD_BASE);
      core.stepInstruction();
      const IOPWord expected =
        (value & 0xf247ff3f) |
        (previous & 0x00080000) |
        (previous & ~value & 0x00100000) |
        0x00200000;
      REQUIRE(core.status() == expected);
      REQUIRE(core.retiredInstructions() == 1);
      REQUIRE(core.exception().kind == IOPException::None);
    }
  }
}

TEST_CASE("IOP MTC0 cause changes only software pending interrupts",
  "[iop][execution][cop0]")
{
  for (unsigned bit = 0; bit < 34; ++bit)
  {
    const IOPWord value = bit < 32
      ? IOPWord(1) << bit
      : (bit == 32 ? UINT32_MAX : 0);
    for (const IOPWord previous : {IOPWord(0), IOPWord(0xb000ff7c)})
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      IOPCoreTestAccess::setCOP0State(&core, {0, 0x00600000, previous, 0});
      IOPCoreTestAccess::setGeneralRegister(&core, 2, value);
      loadInstruction(&bus, 0, cop0TransferInstruction(4, 2, 13));
      core.startExecution(0);
      core.stepInstruction();
      REQUIRE(core.cause() == ((previous & ~IOPWord(0x300)) | (value & 0x300)));
      REQUIRE(core.retiredInstructions() == 1);
    }
  }
}

TEST_CASE("IOP MTC0 EPC accepts all bits while BadVAddr and PRId ignore writes",
  "[iop][execution][cop0]")
{
  for (const std::uint8_t index : {8, 14, 15})
  {
    for (const IOPWord value : {IOPWord(0), IOPWord(0x89abcdef), UINT32_MAX})
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      IOPCoreTestAccess::setCOP0State(&core, {0x12345679, 0x00600000, 0, 0x10203041});
      IOPCoreTestAccess::setGeneralRegister(&core, 2, value);
      loadInstruction(&bus, 0, cop0TransferInstruction(4, 2, index));
      core.startExecution(0);
      core.stepInstruction();
      REQUIRE(core.retiredInstructions() == 1);
      REQUIRE(core.badVirtualAddress() == 0x12345679);
      REQUIRE(core.processorRevision() == 0x1f);
      REQUIRE(core.exceptionProgramCounter() == (index == 14 ? value : 0x10203041));
    }
  }
}

TEST_CASE("IOP COP0 transfers require CU0 only in user mode",
  "[iop][execution][cop0]")
{
  for (const bool user : {false, true})
  {
    for (const bool usable : {false, true})
    {
      for (const std::uint8_t selector : {0, 4})
      {
        for (const std::uint8_t index : {8, 12, 13, 14, 15, 7})
        {
          IOPBus bus;
          IOPCore core;
          core.attachBus(&bus);
          const IOPWord status = 0x00600000 |
            (user ? 2 : 0) | (usable ? 0x10000000 : 0);
          IOPCoreTestAccess::setCOP0State(&core, {0, status, 0, 0});
          IOPCoreTestAccess::setGeneralRegister(&core, 2, 0x12345678);
          loadInstruction(&bus, 0, cop0TransferInstruction(selector, 2, index));
          core.startExecution(0);
          core.stepInstruction();
          const IOPException expected = user && !usable
            ? IOPException::CoprocessorUnusable
            : (index == 7 ? IOPException::ReservedInstruction : IOPException::None);
          REQUIRE(core.exception().kind == expected);
          REQUIRE(core.exception().coprocessor == 0);
          REQUIRE(core.retiredInstructions() == (expected == IOPException::None ? 1 : 0));
          if (expected != IOPException::None)
          {
            REQUIRE(core.status() ==
              ((status & ~IOPWord(0x3f)) | ((status & 0xf) << 2)));
            REQUIRE(core.generalRegister(2) == 0x12345678);
            REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
          }
        }
      }
    }
  }
}

TEST_CASE("IOP unsupported COP0 register transfers raise RI even with register zero",
  "[iop][execution][cop0]")
{
  for (std::uint8_t index = 0; index < 32; ++index)
  {
    if (index == 8 || index == 12 || index == 13 || index == 14 || index == 15)
    {
      continue;
    }
    for (const std::uint8_t selector : {0, 4})
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      loadInstruction(&bus, 0, cop0TransferInstruction(selector, 0, index));
      core.startExecution(0);
      core.stepInstruction();
      REQUIRE(core.exception().kind == IOPException::ReservedInstruction);
      REQUIRE(core.stopReason() == IOPStopReason::None);
      REQUIRE(core.programCounter() == 0xbfc00180);
      REQUIRE(core.exceptionProgramCounter() == 0);
      REQUIRE(core.retiredInstructions() == 0);
    }
  }
}

TEST_CASE("IOP MTC0 captures the old GPR before a preceding delayed load writes back",
  "[iop][execution][cop0]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  REQUIRE(bus.write32(0x100, 0x89abcdef).status == IOPBusStatus::Completed);
  IOPCoreTestAccess::setGeneralRegister(&core, 2, 0x12345679);
  loadInstruction(&bus, 0, immediateInstruction(0x23, 0, 2, 0x100));
  loadInstruction(&bus, 4, cop0TransferInstruction(4, 2, 14));
  loadInstruction(&bus, 8, cop0TransferInstruction(0, 3, 14));
  loadInstruction(&bus, 12, 0);
  core.startExecution(0);
  core.stepInstruction();
  core.stepInstruction();
  REQUIRE(core.exceptionProgramCounter() == 0x12345679);
  REQUIRE(core.generalRegister(2) == 0x89abcdef);
  core.stepInstruction();
  core.stepInstruction();
  REQUIRE(core.generalRegister(3) == 0x12345679);
}

TEST_CASE("IOP MFC0 zero and overlapping destinations follow delayed result ordering",
  "[iop][execution][cop0]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  loadInstruction(&bus, 0, cop0TransferInstruction(0, 0, 15));
  loadInstruction(&bus, 4, cop0TransferInstruction(0, 2, 15));
  loadInstruction(&bus, 8, immediateInstruction(0x09, 0, 2, 7));
  loadInstruction(&bus, 12, cop0TransferInstruction(0, 2, 15));
  loadInstruction(&bus, 16, cop0TransferInstruction(0, 2, 12));
  loadInstruction(&bus, 20, 0);
  core.startExecution(0);
  core.stepInstruction();
  REQUIRE(core.generalRegister(0) == 0);
  REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
  core.stepInstruction();
  core.stepInstruction();
  REQUIRE(core.generalRegister(2) == 7);
  core.stepInstruction();
  core.stepInstruction();
  REQUIRE(core.generalRegister(2) == 0x1f);
  REQUIRE(core.delayedResultContinuation().value == 0x00600000);
  core.stepInstruction();
  REQUIRE(core.generalRegister(2) == 0x00600000);
}

TEST_CASE("IOP MFC0 survives host pause and completes before a younger fault",
  "[iop][execution][cop0]")
{
  for (const bool fetchFault : {false, true})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    const IOPAddress start = fetchFault ? 0x007ffffc : 0;
    loadInstruction(&bus, start, cop0TransferInstruction(0, 2, 15));
    if (!fetchFault)
    {
      loadInstruction(&bus, 4, UINT32_MAX);
    }
    core.startExecution(start);
    core.stepInstruction();
    core.haltExecution();
    core.resumeExecution();
    REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::COP0Transfer);
    core.stepInstruction();
    REQUIRE(core.generalRegister(2) == 0x1f);
    REQUIRE(core.retiredInstructions() == 1);
    REQUIRE(core.exception().kind ==
      (fetchFault ? IOPException::InstructionBusError : IOPException::ReservedInstruction));
    REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
  }
}

TEST_CASE("IOP COP0 transfers retire normally in branch delay slots",
  "[iop][execution][cop0]")
{
  for (const std::uint8_t selector : {0, 4})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setGeneralRegister(&core, 2, 0x12345679);
    loadInstruction(&bus, 0, immediateInstruction(4, 0, 0, 3));
    loadInstruction(&bus, 4, cop0TransferInstruction(selector, 2, selector == 0 ? 15 : 14));
    loadInstruction(&bus, 16, registerInstruction(0x21, 2, 0, 3));
    core.startExecution(0);
    core.stepInstruction();
    core.stepInstruction();
    REQUIRE(core.programCounter() == 16);
    REQUIRE_FALSE(core.branchContinuation().active);
    REQUIRE(core.retiredInstructions() == 2);
    core.stepInstruction();
    REQUIRE(core.generalRegister(3) == 0x12345679);
    REQUIRE(core.generalRegister(2) == (selector == 0 ? 0x1f : 0x12345679));
    REQUIRE(core.exceptionProgramCounter() == (selector == 4 ? 0x12345679 : 0));
  }
}

TEST_CASE("IOP MTC0 status privilege changes apply at the next instruction boundary",
  "[iop][execution][cop0]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setGeneralRegister(&core, 2, 0x00600002);
  loadInstruction(&bus, 0, cop0TransferInstruction(4, 2, 12));
  loadInstruction(&bus, 4, cop0TransferInstruction(0, 3, 15));
  core.startExecution(0);
  core.stepInstruction();
  REQUIRE(core.status() == 0x00600002);
  core.stepInstruction();
  REQUIRE(core.exception().kind == IOPException::CoprocessorUnusable);
  REQUIRE(core.retiredInstructions() == 1);
}

TEST_CASE("IOP COP0 effect construction is pure and exception commit suppresses writes",
  "[iop][execution][cop0]")
{
  IOPCore core;
  IOPCoreTestAccess::setGeneralRegister(&core, 2, 0x89abcdef);
  const IOPInstructionEffects read = IOPCoreTestAccess::instructionEffects(
    core, cop0TransferInstruction(0, 2, 15));
  REQUIRE(read.delayedResult.source == IOPDelayedResultSource::COP0Transfer);
  REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
  IOPInstructionEffects write = IOPCoreTestAccess::instructionEffects(
    core, cop0TransferInstruction(4, 2, 14));
  REQUIRE(write.completion == IOPInstructionCompletion::Retired);
  REQUIRE(core.exceptionProgramCounter() == 0);
  write.exception.kind = IOPException::CoprocessorUnusable;
  IOPCoreTestAccess::commitInstructionEffects(&core, 0, write);
  REQUIRE(core.exceptionProgramCounter() == 0);
  REQUIRE(core.retiredInstructions() == 0);
}

TEST_CASE("IOP MFC0 captures CP0 before a following MTC0 and does not forward address operands",
  "[iop][execution][cop0]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setCOP0State(&core, {0, 0x00600000, 0, 0x200});
  IOPCoreTestAccess::setGeneralRegister(&core, 2, 0x100);
  IOPCoreTestAccess::setGeneralRegister(&core, 3, 0x300);
  REQUIRE(bus.write32(0x100, 0x12345678).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x200, 0x89abcdef).status == IOPBusStatus::Completed);
  loadInstruction(&bus, 0, cop0TransferInstruction(0, 2, 14));
  loadInstruction(&bus, 4, immediateInstruction(0x23, 2, 4, 0));
  loadInstruction(&bus, 8, cop0TransferInstruction(0, 5, 14));
  loadInstruction(&bus, 12, cop0TransferInstruction(4, 3, 14));
  loadInstruction(&bus, 16, cop0TransferInstruction(0, 6, 14));
  loadInstruction(&bus, 20, 0);
  core.startExecution(0);
  core.stepInstruction();
  core.stepInstruction();
  REQUIRE(core.generalRegister(2) == 0x200);
  REQUIRE(core.delayedResultContinuation().value == 0x12345678);
  core.stepInstruction();
  REQUIRE(core.generalRegister(4) == 0x12345678);
  core.stepInstruction();
  REQUIRE(core.generalRegister(5) == 0x200);
  REQUIRE(core.exceptionProgramCounter() == 0x300);
  core.stepInstruction();
  core.stepInstruction();
  REQUIRE(core.generalRegister(6) == 0x300);
}

TEST_CASE("IOP denied COP0 transfer in a delay slot preserves branch provenance",
  "[iop][execution][cop0]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setGeneralRegister(&core, 2, 0x12345678);
  IOPCoreTestAccess::setCOP0State(&core, {0, 0x00600002, 0, 0});
  loadInstruction(&bus, 0, immediateInstruction(4, 0, 0, 3));
  loadInstruction(&bus, 4, cop0TransferInstruction(4, 2, 14));
  core.startExecution(0);
  core.stepInstruction();
  core.stepInstruction();
  REQUIRE(core.exception().kind == IOPException::CoprocessorUnusable);
  REQUIRE(core.exception().inBranchDelaySlot);
  REQUIRE_FALSE(core.branchContinuation().active);
  REQUIRE(core.branchContinuation().instructionAddress == 0);
  REQUIRE(core.branchContinuation().targetAddress == 0);
  REQUIRE(core.cause() == 0x8000002c);
  REQUIRE(core.exceptionProgramCounter() == 0);
  REQUIRE(core.generalRegister(2) == 0x12345678);
  REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
  REQUIRE(core.retiredInstructions() == 1);
}

TEST_CASE("IOP MTC0 register zero and user transition affect the following fetch",
  "[iop][execution][cop0]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setGeneralRegister(&core, 2, 2);
  loadInstruction(&bus, 0, cop0TransferInstruction(4, 0, 12));
  loadInstruction(&bus, 4, cop0TransferInstruction(4, 0, 14));
  loadInstruction(&bus, 8, cop0TransferInstruction(4, 2, 12));
  core.startExecution(0x80000000);
  core.stepInstruction();
  REQUIRE(core.status() == 0x00200000);
  core.stepInstruction();
  REQUIRE(core.exceptionProgramCounter() == 0);
  core.stepInstruction();
  REQUIRE(core.status() == 0x00200002);
  core.stepInstruction();
  REQUIRE(core.exception().kind == IOPException::AddressErrorLoadOrFetch);
  REQUIRE(core.exception().badVirtualAddress == 0x8000000c);
  REQUIRE(core.retiredInstructions() == 3);
}

TEST_CASE("IOP reset and explicit restart discard pending MFC0 results",
  "[iop][execution][cop0]")
{
  for (const bool reset : {false, true})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    loadInstruction(&bus, 0, cop0TransferInstruction(0, 2, 15));
    loadInstruction(&bus, 4, 0);
    core.startExecution(0);
    core.stepInstruction();
    if (reset)
    {
      core.reset();
    }
    core.startExecution(4);
    core.stepInstruction();
    REQUIRE(core.generalRegister(2) == 0);
    REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
    REQUIRE(core.status() == 0x00600000);
    REQUIRE(core.cause() == 0);
    REQUIRE(core.badVirtualAddress() == 0);
    REQUIRE(core.exceptionProgramCounter() == 0);
    REQUIRE(core.processorRevision() == 0x1f);
  }
}

TEST_CASE("IOP synchronous entry maps exception codes and updates only owned CP0 fields",
  "[iop][execution][exception-entry]")
{
  struct EntryCase
  {
    IOPException kind;
    IOPWord code;
  };
  for (const EntryCase test : {
         EntryCase{IOPException::AddressErrorLoadOrFetch, 4},
         EntryCase{IOPException::AddressErrorStore, 5},
         EntryCase{IOPException::InstructionBusError, 6},
         EntryCase{IOPException::DataBusError, 7},
         EntryCase{IOPException::SystemCall, 8},
         EntryCase{IOPException::Breakpoint, 9},
         EntryCase{IOPException::ReservedInstruction, 10},
         EntryCase{IOPException::CoprocessorUnusable, 11},
         EntryCase{IOPException::ArithmeticOverflow, 12}})
  {
    for (const bool bootstrap : {false, true})
    {
      INFO("exception code " << test.code << ", BEV " << bootstrap);
      IOPCore core;
      const IOPWord previousStatus = 0xf237ff3f |
        (bootstrap ? 0x00400000 : 0);
      IOPCoreTestAccess::setCOP0State(
        &core, {0x12345679, previousStatus, 0xb000a57c, 0xabcdef00});
      core.startExecution(0x1004);
      IOPInstructionEffects effects;
      effects.exception = {test.kind, 0x89abcdef, false, 2};
      IOPCoreTestAccess::commitInstructionEffects(&core, 0x1004, effects);
      REQUIRE(core.programCounter() == (bootstrap ? 0xbfc00180 : 0x80000080));
      REQUIRE(core.exceptionProgramCounter() == 0x1004);
      REQUIRE(core.status() == ((previousStatus & ~IOPWord(0x3f)) | 0x3c));
      REQUIRE(core.cause() ==
        (0x0000a500 | (test.code << 2) |
          (test.kind == IOPException::CoprocessorUnusable ? 0x20000000 : 0)));
      const bool addressError =
        test.kind == IOPException::AddressErrorLoadOrFetch ||
        test.kind == IOPException::AddressErrorStore;
      REQUIRE(core.badVirtualAddress() == (addressError ? 0x89abcdef : 0x12345679));
      REQUIRE(core.exception().kind == test.kind);
      REQUIRE_FALSE(core.exception().inBranchDelaySlot);
      REQUIRE(core.executionState() == IOPExecutionState::Running);
      REQUIRE(core.stopReason() == IOPStopReason::None);
      REQUIRE(core.elapsedCycles() == 1);
      REQUIRE(core.retiredInstructions() == 0);
    }
  }
}

TEST_CASE("IOP synchronous entry pushes all KU IE stack patterns including nested faults",
  "[iop][execution][exception-entry]")
{
  for (IOPWord stack = 0; stack < 64; ++stack)
  {
    IOPCore core;
    IOPWord status = 0x0265a500 | stack;
    IOPCoreTestAccess::setCOP0State(&core, {0, status, 0, 0});
    core.startExecution(0x100);
    for (const IOPAddress address : {0x100u, 0xbfc00180u, 0xbfc00184u})
    {
      IOPInstructionEffects effects;
      effects.exception.kind = IOPException::SystemCall;
      IOPCoreTestAccess::commitInstructionEffects(&core, address, effects);
      status = (status & ~IOPWord(0x3f)) | ((status & 0xf) << 2);
      REQUIRE(core.status() == status);
      REQUIRE(core.exceptionProgramCounter() == address);
      REQUIRE(core.programCounter() == 0xbfc00180);
      REQUIRE(core.cause() == 0x20);
    }
  }
}

TEST_CASE("IOP executing synchronous traps records delay slot EPC BD and cancels the branch",
  "[iop][execution][exception-entry]")
{
  struct TrapCase
  {
    IOPWord instruction;
    IOPWord code;
    std::uint8_t coprocessor;
  };
  for (const TrapCase test : {
         TrapCase{0x0000000c, 8, 0},
         TrapCase{0x03ffffcc, 8, 0},
         TrapCase{0x0000000d, 9, 0},
         TrapCase{0x03ffffcd, 9, 0},
         TrapCase{0xffffffff, 10, 0},
         TrapCase{immediateInstruction(0x08, 1, 2, 1), 12, 0},
         TrapCase{0x44000000, 11, 1},
         TrapCase{0x48000000, 11, 2},
         TrapCase{immediateInstruction(0x31, 1, 2, 0), 11, 1},
         TrapCase{immediateInstruction(0x32, 1, 2, 0), 11, 2},
         TrapCase{immediateInstruction(0x39, 1, 2, 0), 11, 1},
         TrapCase{immediateInstruction(0x3a, 1, 2, 0), 11, 2}})
  {
    for (const bool delaySlot : {false, true})
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      IOPCoreTestAccess::setGeneralRegister(&core, 1, 0x7fffffff);
      IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xaabbccdd);
      loadInstruction(&bus, 0x100, immediateInstruction(4, 0, 0, 7));
      loadInstruction(&bus, 0x104, test.instruction);
      core.startExecution(delaySlot ? 0x100 : 0x104);
      if (delaySlot)
      {
        core.stepInstruction();
      }
      core.stepInstruction();
      REQUIRE(core.programCounter() == 0xbfc00180);
      REQUIRE(core.exceptionProgramCounter() == (delaySlot ? 0x100 : 0x104));
      REQUIRE(core.cause() ==
        ((test.code << 2) | (delaySlot ? 0x80000000 : 0) |
          (IOPWord(test.coprocessor) << 28)));
      REQUIRE(core.exception().inBranchDelaySlot == delaySlot);
      REQUIRE(core.exception().coprocessor == test.coprocessor);
      REQUIRE_FALSE(core.branchContinuation().active);
      REQUIRE(core.branchContinuation().instructionAddress == 0);
      REQUIRE(core.branchContinuation().targetAddress == 0);
      REQUIRE(core.generalRegister(2) == 0xaabbccdd);
      REQUIRE(core.retiredInstructions() == (delaySlot ? 1 : 0));
      REQUIRE(core.executionState() == IOPExecutionState::Running);
    }
  }
}

TEST_CASE("IOP exception handler starts with older delayed results already committed",
  "[iop][execution][exception-entry]")
{
  for (const bool cop0Read : {false, true})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    REQUIRE(bus.write32(0x200, 0x12345678).status == IOPBusStatus::Completed);
    loadInstruction(&bus, 0x100, cop0Read
      ? cop0TransferInstruction(0, 2, 15)
      : immediateInstruction(0x23, 0, 2, 0x200));
    loadInstruction(&bus, 0x104, immediateInstruction(0x08, 1, 2, 1));
    loadInstruction(&bus, 0x80, registerInstruction(0x21, 2, 0, 3));
    IOPCoreTestAccess::setCOP0State(&core, {0, 0x00200000, 0, 0});
    IOPCoreTestAccess::setGeneralRegister(&core, 1, 0x7fffffff);
    IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xaabbccdd);
    core.startExecution(0x100);
    core.stepInstruction();
    core.stepInstruction();
    const IOPWord expected = cop0Read ? 0x1f : 0x12345678;
    REQUIRE(core.generalRegister(2) == expected);
    REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
    REQUIRE(core.exceptionProgramCounter() == 0x104);
    REQUIRE(core.cause() == 0x30);
    core.stepInstruction();
    REQUIRE(core.generalRegister(3) == expected);
    REQUIRE(core.programCounter() == 0x80000084);
    REQUIRE(core.exception().kind == IOPException::None);
    REQUIRE(core.retiredInstructions() == 2);
  }
}

TEST_CASE("IOP fetch and data exceptions enter before any younger side effect",
  "[iop][execution][exception-entry]")
{
  struct FaultCase
  {
    IOPAddress entry;
    IOPWord instruction;
    IOPAddress base;
    IOPWord code;
    IOPAddress faultAddress;
  };
  for (const FaultCase test : {
         FaultCase{0x102, 0, 0, 4, 0x102},
         FaultCase{0x800000, 0, 0, 6, 0x800000},
         FaultCase{0x100, immediateInstruction(0x23, 1, 2, 0), 3, 4, 3},
         FaultCase{0x100, immediateInstruction(0x23, 1, 2, 0), 0x800000, 7, 0x800000},
         FaultCase{0x100, immediateInstruction(0x2b, 1, 2, 0), 3, 5, 3}})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    loadInstruction(&bus, 0x100, test.instruction);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, test.base);
    IOPCoreTestAccess::setGeneralRegister(&core, 2, 0x12345678);
    IOPCoreTestAccess::setCOP0State(&core, {0xaabbccdd, 0x00200000, 0, 0});
    core.startExecution(test.entry);
    core.stepInstruction();
    REQUIRE(core.programCounter() == 0x80000080);
    REQUIRE(core.exceptionProgramCounter() == test.entry);
    REQUIRE(core.cause() == (test.code << 2));
    REQUIRE(core.exception().badVirtualAddress == test.faultAddress);
    REQUIRE(core.badVirtualAddress() ==
      (test.code == 4 || test.code == 5 ? test.faultAddress : 0xaabbccdd));
    REQUIRE(core.generalRegister(2) == 0x12345678);
    REQUIRE(core.retiredInstructions() == 0);
  }
}

TEST_CASE("IOP exception handlers can pause resume and take a new nested exception",
  "[iop][execution][exception-entry]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  loadInstruction(&bus, 0x100, 0x0000000c);
  loadInstruction(&bus, 0x80, 0x0000000d);
  IOPCoreTestAccess::setCOP0State(&core, {0, 0x00200003, 0, 0});
  core.startExecution(0x100);
  core.stepInstruction();
  REQUIRE(core.status() == 0x0020000c);
  REQUIRE(core.cause() == 0x20);
  core.haltExecution();
  REQUIRE_NOTHROW(core.resumeExecution());
  core.stepInstruction();
  REQUIRE(core.exceptionProgramCounter() == 0x80000080);
  REQUIRE(core.status() == 0x00200030);
  REQUIRE(core.cause() == 0x24);
  REQUIRE(core.exception().kind == IOPException::Breakpoint);
  REQUIRE(core.programCounter() == 0x80000080);
}

TEST_CASE("IOP delay slot fetch faults and untaken branches retain architectural provenance",
  "[iop][execution][exception-entry]")
{
  for (const bool fetchFault : {false, true})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    const IOPAddress entry = fetchFault ? 0x7ffffc : 0x100;
    loadInstruction(&bus, entry, immediateInstruction(5, 0, 0, 7));
    if (!fetchFault)
    {
      loadInstruction(&bus, entry + 4, 0x0000000c);
    }
    core.startExecution(entry);
    core.stepInstruction();
    core.stepInstruction();
    REQUIRE(core.exceptionProgramCounter() == entry);
    REQUIRE(core.exception().inBranchDelaySlot);
    REQUIRE(core.cause() == (fetchFault ? 0x80000018 : 0x80000020));
    REQUIRE(core.programCounter() == 0xbfc00180);
    REQUIRE_FALSE(core.branchContinuation().active);
    REQUIRE(core.retiredInstructions() == 1);
  }
}

TEST_CASE("IOP coprocessor admission traps denied operations but preserves enabled deferred stops",
  "[iop][execution][exception-entry]")
{
  for (const IOPWord instruction : {
         IOPWord(0x44000000), IOPWord(0x48000000)})
  {
    for (const bool usable : {false, true})
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      const std::uint8_t coprocessor = (instruction >> 26) & 3;
      const IOPWord status = 0x00600002 |
        (usable ? IOPWord(1) << (28 + coprocessor) : 0);
      IOPCoreTestAccess::setCOP0State(&core, {0, status, 0, 0});
      loadInstruction(&bus, 0x100, instruction);
      core.startExecution(0x100);
      core.stepInstruction();
      REQUIRE(core.retiredInstructions() == 0);
      REQUIRE(core.exception().kind ==
        (usable ? IOPException::None : IOPException::CoprocessorUnusable));
      REQUIRE(core.programCounter() == (usable ? 0x100 : 0xbfc00180));
      REQUIRE(core.executionState() ==
        (usable ? IOPExecutionState::Halted : IOPExecutionState::Running));
      REQUIRE(core.cause() ==
        (usable ? 0 : 0x2c | (IOPWord(coprocessor) << 28)));
    }
  }
}

TEST_CASE("IOP RFE pops every KU IE stack pattern without restoring EPC",
  "[iop][execution][rfe][interrupt]")
{
  for (IOPWord stack = 0; stack < 64; ++stack)
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    const IOPWord originalStatus = 0xf27f0000 | stack;
    IOPCoreTestAccess::setCOP0State(
      &core, {0x12345679, originalStatus, 0x90000030, 0x80000300});
    const IOPAddress code = IOPBus::SCRATCHPAD_BASE;
    loadInstruction(&bus, code, 0x42000010);
    core.startExecution(code);
    const auto effects = IOPCoreTestAccess::instructionEffects(core, 0x42000010);
    REQUIRE(core.status() == originalStatus);
    REQUIRE(core.programCounter() == code);
    REQUIRE(effects.exception.kind == IOPException::None);
    core.stepInstruction();
    REQUIRE(core.status() ==
      ((originalStatus & ~IOPWord(0xf)) | ((stack >> 2) & 0xf)));
    REQUIRE(core.cause() == 0x90000030);
    REQUIRE(core.exceptionProgramCounter() == 0x80000300);
    REQUIRE(core.badVirtualAddress() == 0x12345679);
    REQUIRE(core.programCounter() == code + 4);
    REQUIRE(core.retiredInstructions() == 1);
    REQUIRE(core.elapsedCycles() == 1);
    REQUIRE(core.exception().kind == IOPException::None);
    REQUIRE(core.executionState() == IOPExecutionState::Running);
  }
}

TEST_CASE("IOP RFE uses common CU0 admission and suppresses denied stack changes",
  "[iop][execution][rfe][interrupt]")
{
  for (const bool user : {false, true})
  {
    for (const bool usable : {false, true})
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      const IOPWord originalStatus =
        0x0060003c | (user ? 2 : 0) | (usable ? 0x10000000 : 0);
      IOPCoreTestAccess::setCOP0State(&core, {0, originalStatus, 0, 0x300});
      loadInstruction(&bus, 0x100, 0x42000010);
      core.startExecution(0x100);
      core.stepInstruction();
      const bool denied = user && !usable;
      REQUIRE(core.exception().kind ==
        (denied ? IOPException::CoprocessorUnusable : IOPException::None));
      REQUIRE(core.status() == (denied
        ? (originalStatus & ~IOPWord(0x3f)) | ((originalStatus & 0xf) << 2)
        : (originalStatus & ~IOPWord(0xf)) | ((originalStatus >> 2) & 0xf)));
      REQUIRE(core.programCounter() == (denied ? 0xbfc00180 : 0x104));
      REQUIRE(core.exceptionProgramCounter() == (denied ? 0x100 : 0x300));
      REQUIRE(core.retiredInstructions() == (denied ? 0 : 1));
    }
  }
}

TEST_CASE("IOP software interrupt admission requires matching IM IP and IEc",
  "[iop][execution][interrupt]")
{
  for (IOPWord pending = 0; pending < 4; ++pending)
  {
    for (IOPWord mask = 0; mask < 4; ++mask)
    {
      for (const bool enabled : {false, true})
      {
        for (const bool bootstrap : {false, true})
        {
          IOPBus bus;
          IOPCore core;
          core.attachBus(&bus);
          const IOPWord originalStatus = 0x00200002 |
            (bootstrap ? 0x00400000 : 0) | (mask << 8) | (enabled ? 1 : 0);
          IOPCoreTestAccess::setCOP0State(
            &core, {0x12345679, originalStatus, pending << 8, 0x300});
          loadInstruction(&bus, 0x100, immediateInstruction(9, 0, 2, 7));
          core.startExecution(0x100);
          core.stepInstruction();
          const bool admitted = enabled && (pending & mask) != 0;
          REQUIRE(core.exception().kind ==
            (admitted ? IOPException::Interrupt : IOPException::None));
          REQUIRE(core.generalRegister(2) == (admitted ? 0 : 7));
          REQUIRE(core.retiredInstructions() == (admitted ? 0 : 1));
          REQUIRE(core.elapsedCycles() == 1);
          REQUIRE(core.programCounter() ==
            (admitted ? (bootstrap ? 0xbfc00180 : 0x80000080) : 0x104));
          REQUIRE(core.exceptionProgramCounter() == (admitted ? 0x100 : 0x300));
          REQUIRE(core.cause() == (pending << 8));
          REQUIRE(core.badVirtualAddress() == 0x12345679);
          REQUIRE(core.status() == (admitted
            ? (originalStatus & ~IOPWord(0x3f)) | ((originalStatus & 0xf) << 2)
            : originalStatus));
          REQUIRE(core.executionState() == IOPExecutionState::Running);
          REQUIRE(core.stopReason() == IOPStopReason::None);
        }
      }
    }
  }
}

TEST_CASE("IOP interrupt entry completes older delayed values without executing the next instruction",
  "[iop][execution][interrupt]")
{
  for (const bool cop0Read : {false, true})
  {
    for (const IOPWord follower : {
           immediateInstruction(9, 0, 2, 7),
           immediateInstruction(0x2b, 0, 2, 0x200),
           cop0TransferInstruction(4, 2, 14),
           registerInstruction(0x11, 2, 0, 0),
           IOPWord(0x42000010), IOPWord(0xffffffff)})
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      REQUIRE(bus.write32(0x200, 0x12345678).status == IOPBusStatus::Completed);
      loadInstruction(&bus, 0x100, cop0Read
        ? cop0TransferInstruction(0, 2, 15)
        : immediateInstruction(0x23, 0, 2, 0x200));
      loadInstruction(&bus, 0x104, follower);
      IOPCoreTestAccess::setCOP0State(&core, {0, 0x00200101, 0, 0});
      core.startExecution(0x100);
      core.stepInstruction();
      IOPCoreTestAccess::setCOP0State(&core, {0, 0x00200101, 0x100, 0});
      core.stepInstruction();
      REQUIRE(core.exception().kind == IOPException::Interrupt);
      REQUIRE(core.generalRegister(2) == (cop0Read ? 0x1f : 0x12345678));
      REQUIRE(core.hi() == 0);
      REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
      REQUIRE(bus.read32(0x200).value == 0x12345678);
      REQUIRE(core.exceptionProgramCounter() == 0x104);
      REQUIRE(core.programCounter() == 0x80000080);
      REQUIRE(core.retiredInstructions() == 1);
      REQUIRE(core.elapsedCycles() == 2);
    }
  }
}

TEST_CASE("IOP JR RFE restores user mode before the target fetch and interrupt boundary",
  "[iop][execution][rfe][interrupt]")
{
  for (const bool pending : {false, true})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setCOP0State(&core, {0, 0x0020010c, pending ? 0x100u : 0, 0x200});
    IOPCoreTestAccess::setGeneralRegister(&core, 26, 0x200);
    loadInstruction(&bus, 0x80, registerInstruction(8, 26, 0, 0));
    loadInstruction(&bus, 0x84, 0x42000010);
    loadInstruction(&bus, 0x200, immediateInstruction(9, 0, 2, 7));
    core.startExecution(0x80000080);
    core.stepInstruction();
    core.stepInstruction();
    REQUIRE(core.status() == 0x00200103);
    REQUIRE(core.programCounter() == 0x200);
    REQUIRE_FALSE(core.branchContinuation().active);
    REQUIRE(core.exception().kind == IOPException::None);
    REQUIRE(core.retiredInstructions() == 2);
    core.stepInstruction();
    REQUIRE(core.exception().kind ==
      (pending ? IOPException::Interrupt : IOPException::None));
    REQUIRE(core.generalRegister(2) == (pending ? 0 : 7));
    REQUIRE(core.programCounter() == (pending ? 0x80000080 : 0x204));
    REQUIRE(core.exceptionProgramCounter() == 0x200);
    REQUIRE((core.cause() & 0x80000000) == 0);
  }
}

TEST_CASE("IOP INTC aggregate uses only Hardware0 and preserves every masking layer",
  "[iop][execution][interrupt][intc]")
{
  for (const bool sourceEnabled : {false, true})
  {
    for (const bool masterEnabled : {false, true})
    {
      for (const bool cpuEnabled : {false, true})
      {
        for (const bool inputEnabled : {false, true})
        {
          CAPTURE(sourceEnabled);
          CAPTURE(masterEnabled);
          CAPTURE(cpuEnabled);
          CAPTURE(inputEnabled);
          IOPBus bus;
          IOPInterruptController interrupts;
          bus.attachInterruptController(&interrupts);
          IOPCore core;
          core.attachBus(&bus);
          IOPCoreTestAccess::setCOP0State(&core,
            {0, 0x00200000 | (cpuEnabled ? 1u : 0) |
              (inputEnabled ? 0x400u : 0), 0xb0008230, 0x200});
          interrupts.assertSource(IOPInterruptSource::Timer0);
          interrupts.writeMask(sourceEnabled ? 0x10 : 0);
          interrupts.writeControl(masterEnabled ? 1 : 0);
          loadInstruction(&bus, 0x100, immediateInstruction(9, 0, 2, 7));
          core.startExecution(0x100);
          core.stepInstruction();
          const bool pending = sourceEnabled && masterEnabled;
          const bool admitted = pending && cpuEnabled && inputEnabled;
          REQUIRE(core.exception().kind ==
            (admitted ? IOPException::Interrupt : IOPException::None));
          REQUIRE(core.cause() ==
            ((admitted ? 0x8200u : 0xb0008230u) | (pending ? 0x400u : 0)));
          REQUIRE(core.generalRegister(2) == (admitted ? 0 : 7));
          REQUIRE(core.retiredInstructions() == (admitted ? 0 : 1));
          REQUIRE(core.elapsedCycles() == 1);
          REQUIRE(core.exceptionProgramCounter() == (admitted ? 0x100 : 0x200));
          REQUIRE(interrupts.status() == 0x10);
          REQUIRE(interrupts.control() == (masterEnabled ? 1 : 0));
        }
      }
    }
  }
}

TEST_CASE("IOP INTC deassertion is resampled without consuming controller state",
  "[iop][execution][interrupt][intc]")
{
  for (const IOPAddress address :
       {IOPBus::INTC_STAT, IOPBus::INTC_MASK, IOPBus::INTC_CTRL})
  {
    CAPTURE(address);
    IOPBus bus;
    IOPInterruptController interrupts;
    bus.attachInterruptController(&interrupts);
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setCOP0State(&core, {0, 0x00200400, 0, 0});
    interrupts.assertSource(IOPInterruptSource::Timer0);
    interrupts.writeMask(0x10);
    interrupts.writeControl(1);
    loadInstruction(&bus, 0x100, 0);
    loadInstruction(&bus, 0x104, 0);
    core.startExecution(0x100);
    core.stepInstruction();
    REQUIRE(core.cause() == 0x400);
    REQUIRE(interrupts.status() == 0x10);
    REQUIRE(interrupts.control() == 1);
    REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Completed);
    core.stepInstruction();
    REQUIRE(core.cause() == 0);
    REQUIRE(core.exception().kind == IOPException::None);
    REQUIRE(core.retiredInstructions() == 2);
  }
}

TEST_CASE("IOP timer MMIO decoded stores and loads preserve mode and load delay semantics",
  "[iop][execution][timer]")
{
  for (std::size_t index = 0; index < 6; ++index)
  {
    const IOPAddress physical = index < 3 ?
      0x1f801100 + static_cast<IOPAddress>(index * 16) :
      0x1f801480 + static_cast<IOPAddress>((index - 3) * 16);
    for (IOPAddress alias : {0u, 0x80000000u, 0xa0000000u})
    {
      CAPTURE(index);
      CAPTURE(alias);
      IOPBus bus;
      IOPTimers timers;
      IOPInterruptController interrupts;
      bus.attachTimers(&timers);
      bus.attachInterruptController(&interrupts);
      IOPCore core;
      core.attachBus(&bus);
      IOPCoreTestAccess::setGeneralRegister(&core, 1, physical | alias);
      IOPCoreTestAccess::setGeneralRegister(&core, 2, 0x89abcdef);
      IOPCoreTestAccess::setGeneralRegister(&core, 3, 0x89abcdee);
      IOPCoreTestAccess::setGeneralRegister(&core, 4, 0x10);
      const IOPWord instructions[] = {
        immediateInstruction(0x29, 1, 4, 4), // SH mode
        immediateInstruction(0x2b, 1, 2, 8), // SW target
        immediateInstruction(0x2b, 1, 3, 0), // SW count
        immediateInstruction(0x23, 1, 5, 4), // LW mode
        0,
        immediateInstruction(0x25, 1, 6, 0), // LHU count
        0,
        immediateInstruction(0x23, 1, 7, 8), // LW target
        0
      };
      for (std::size_t instruction = 0; instruction < 9; ++instruction)
      {
        loadInstruction(&bus, static_cast<IOPAddress>(instruction * 4),
          instructions[instruction]);
      }
      core.startExecution(0);
      for (unsigned int instruction = 0; instruction < 3; ++instruction)
      {
        core.stepInstruction();
      }
      REQUIRE(core.retiredInstructions() == 3);
      const auto requests = timers.advance({true, false, {}});
      REQUIRE(requests.count == 1);
      REQUIRE(requests.sources[0] == IOPTimers::capabilities(index).interruptSource);
      interrupts.assertSource(requests.sources[0]);
      const IOPWord pending = interrupts.status();
      core.stepInstruction();
      REQUIRE(core.generalRegister(5) == 0);
      REQUIRE(timers.counter(index).mode == 0x0410);
      REQUIRE_FALSE(timers.counter(index).oneShotConsumed);
      REQUIRE_FALSE(timers.counter(index).pulseReleasePending);
      core.stepInstruction();
      REQUIRE(core.generalRegister(5) == 0x0810);
      core.stepInstruction();
      REQUIRE(core.generalRegister(6) == 0);
      core.stepInstruction();
      REQUIRE(core.generalRegister(6) == 0xcdef);
      core.stepInstruction();
      REQUIRE(core.generalRegister(7) == 0);
      core.stepInstruction();
      REQUIRE(core.generalRegister(7) == (index < 3 ? 0xcdef : 0x89abcdef));
      REQUIRE(core.retiredInstructions() == 9);
      REQUIRE(core.exception().kind == IOPException::None);
      REQUIRE(interrupts.status() == pending);
    }
  }
}

TEST_CASE("IOP timer MMIO decoded merge stores never read a rejected mode register",
  "[iop][execution][merge][timer]")
{
  for (IOPAddress physical : {0x1f801104u, 0x1f801484u})
  {
    for (IOPAddress alias : {0u, 0x80000000u, 0xa0000000u})
    {
      for (std::uint8_t opcode : {std::uint8_t{0x2a}, std::uint8_t{0x2e}})
      {
        for (std::uint16_t offset = 0; offset < 4; ++offset)
        {
          CAPTURE(physical);
          CAPTURE(alias);
          CAPTURE(opcode);
          CAPTURE(offset);
          const std::size_t index = physical == 0x1f801104 ? 0 : 3;
          IOPBus bus;
          IOPTimers timers;
          bus.attachTimers(&timers);
          timers.writeMode(index, 0x10);
          timers.writeTarget(index, 1);
          timers.advance({true, false, {}});
          const auto before = timers.counter(index);
          IOPCore core;
          core.attachBus(&bus);
          IOPCoreTestAccess::setGeneralRegister(&core, 1, physical | alias);
          IOPCoreTestAccess::setGeneralRegister(&core, 2, 0x89abcdef);
          loadInstruction(&bus, 0, immediateInstruction(opcode, 1, 2, offset));
          core.startExecution(0);
          core.stepInstruction();
          const bool fullMask = opcode == 0x2a ? offset == 3 : offset == 0;
          if (fullMask)
          {
            REQUIRE(core.retiredInstructions() == 1);
            REQUIRE(timers.counter(index).mode == ((0x89abcdef & 0x63ff) | 0x0400));
            REQUIRE(timers.counter(index).count == 0);
          }
          else
          {
            REQUIRE(core.stopReason() == IOPStopReason::StoreFailure);
            REQUIRE(core.retiredInstructions() == 0);
            REQUIRE(core.exception().kind == IOPException::None);
            REQUIRE(core.storeFailure().access.outcome == IOPAccessOutcome::Unmapped);
            REQUIRE(core.storeFailure().access.virtualAddress ==
              ((physical | alias) + offset));
            REQUIRE(core.storeFailure().mergeByteEnable ==
              (opcode == 0x2a ? 0xf >> (3 - offset) : (0xf << offset) & 0xf));
            REQUIRE(timers.counter(index).count == before.count);
            REQUIRE(timers.counter(index).mode == before.mode);
            REQUIRE(timers.counter(index).oneShotConsumed == before.oneShotConsumed);
            REQUIRE(timers.counter(index).pulseReleasePending == before.pulseReleasePending);
          }
        }
      }
    }
  }
}

TEST_CASE("IOP guest INTC enabling stores become eligible at the following boundary",
  "[iop][execution][interrupt][intc]")
{
  for (const IOPAddress address : {IOPBus::INTC_MASK, IOPBus::INTC_CTRL})
  {
    CAPTURE(address);
    IOPBus bus;
    IOPInterruptController interrupts;
    bus.attachInterruptController(&interrupts);
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setCOP0State(&core, {0, 0x00200401, 0, 0});
    IOPCoreTestAccess::setGeneralRegister(&core, 1, address);
    IOPCoreTestAccess::setGeneralRegister(
      &core, 2, address == IOPBus::INTC_MASK ? 0x10 : 1);
    interrupts.assertSource(IOPInterruptSource::Timer0);
    interrupts.writeMask(address == IOPBus::INTC_MASK ? 0 : 0x10);
    interrupts.writeControl(address == IOPBus::INTC_CTRL ? 0 : 1);
    loadInstruction(&bus, 0x100, immediateInstruction(0x2b, 1, 2, 0));
    loadInstruction(&bus, 0x104, immediateInstruction(9, 0, 3, 7));
    core.startExecution(0x100);
    core.stepInstruction();
    REQUIRE(core.exception().kind == IOPException::None);
    REQUIRE(core.retiredInstructions() == 1);
    REQUIRE(core.cause() == 0);
    REQUIRE(interrupts.interruptPending());
    core.stepInstruction();
    REQUIRE(core.exception().kind == IOPException::Interrupt);
    REQUIRE(core.exceptionProgramCounter() == 0x104);
    REQUIRE(core.cause() == 0x400);
    REQUIRE(core.generalRegister(3) == 0);
    REQUIRE(core.retiredInstructions() == 1);
    REQUIRE(core.elapsedCycles() == 2);
  }
}

TEST_CASE("IOP guest INTC control read clears the sampled line before CPU reenable",
  "[iop][execution][interrupt][intc]")
{
  IOPBus bus;
  IOPInterruptController interrupts;
  bus.attachInterruptController(&interrupts);
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setCOP0State(&core, {0, 0x00200400, 0, 0});
  IOPCoreTestAccess::setGeneralRegister(&core, 1, IOPBus::INTC_CTRL);
  IOPCoreTestAccess::setGeneralRegister(&core, 3, 0x00200401);
  interrupts.assertSource(IOPInterruptSource::Timer0);
  interrupts.writeMask(0x10);
  interrupts.writeControl(1);
  loadInstruction(&bus, 0x100, immediateInstruction(0x23, 1, 2, 0));
  loadInstruction(&bus, 0x104, cop0TransferInstruction(4, 3, 12));
  loadInstruction(&bus, 0x108, immediateInstruction(9, 0, 4, 7));
  core.startExecution(0x100);
  core.stepInstruction();
  REQUIRE(core.cause() == 0x400);
  REQUIRE(interrupts.control() == 0);
  REQUIRE(core.generalRegister(2) == 0);
  core.stepInstruction();
  REQUIRE(core.cause() == 0);
  REQUIRE(core.generalRegister(2) == 1);
  REQUIRE(core.status() == 0x00200401);
  core.stepInstruction();
  REQUIRE(core.exception().kind == IOPException::None);
  REQUIRE(core.generalRegister(4) == 7);
  REQUIRE(core.retiredInstructions() == 3);
  REQUIRE(interrupts.status() == 0x10);
}

TEST_CASE("IOP INTC admission precedes fetch faults and side effecting MMIO fetch",
  "[iop][execution][interrupt][intc]")
{
  for (const IOPAddress entry :
       {0x102u, 0x800000u, 0x80000100u, 0xbf801078u})
  {
    CAPTURE(entry);
    IOPBus bus;
    IOPInterruptController interrupts;
    bus.attachInterruptController(&interrupts);
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setCOP0State(&core,
      {0x12345679, entry == 0x80000100 ? 0x00200403u : 0x00200401u, 0, 0x200});
    interrupts.assertSource(IOPInterruptSource::Timer0);
    interrupts.writeMask(0x10);
    interrupts.writeControl(1);
    core.startExecution(entry);
    core.stepInstruction();
    REQUIRE(core.exception().kind == IOPException::Interrupt);
    REQUIRE(core.exceptionProgramCounter() == entry);
    REQUIRE(core.cause() == 0x400);
    REQUIRE(core.badVirtualAddress() == 0x12345679);
    REQUIRE(core.programCounter() == 0x80000080);
    REQUIRE(core.elapsedCycles() == 1);
    REQUIRE(core.retiredInstructions() == 0);
    REQUIRE(interrupts.status() == 0x10);
    REQUIRE(interrupts.control() == 1);
  }
}

TEST_CASE("IOP INTC admission preserves delayed writeback and branch provenance",
  "[iop][execution][interrupt][intc]")
{
  for (const IOPOperation operation : {
       IOPOperation::LoadWord, IOPOperation::MoveFromCOP0,
       IOPOperation::BranchEqual, IOPOperation::BranchNotEqual})
  {
    CAPTURE(static_cast<unsigned>(operation));
    const bool branch = operation == IOPOperation::BranchEqual ||
      operation == IOPOperation::BranchNotEqual;
    IOPBus bus;
    IOPInterruptController interrupts;
    bus.attachInterruptController(&interrupts);
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setCOP0State(&core, {0, 0x00200401, 0, 0});
    IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xaabbccdd);
    REQUIRE(bus.write32(0x200, 0x12345678).status == IOPBusStatus::Completed);
    loadInstruction(&bus, 0x100, branch
      ? immediateInstruction(operation == IOPOperation::BranchEqual ? 4 : 5, 0, 0, 7)
      : operation == IOPOperation::LoadWord
        ? immediateInstruction(0x23, 0, 2, 0x200)
        : cop0TransferInstruction(0, 2, 15));
    loadInstruction(&bus, 0x104, 0x0000000c);
    core.startExecution(0x100);
    core.stepInstruction();
    REQUIRE(core.branchContinuation().active == branch);
    REQUIRE((core.delayedResultContinuation().source != IOPDelayedResultSource::None) ==
      !branch);
    core.haltExecution();
    interrupts.assertSource(IOPInterruptSource::Timer0);
    interrupts.writeMask(0x10);
    interrupts.writeControl(1);
    REQUIRE_THROWS_AS(core.stepInstruction(), std::logic_error);
    REQUIRE(core.cause() == 0);
    REQUIRE(core.elapsedCycles() == 1);
    REQUIRE(core.retiredInstructions() == 1);
    REQUIRE(core.branchContinuation().active == branch);
    REQUIRE((core.delayedResultContinuation().source != IOPDelayedResultSource::None) ==
      !branch);
    core.resumeExecution();
    REQUIRE(core.cause() == 0);
    core.stepInstruction();
    REQUIRE(core.exception().kind == IOPException::Interrupt);
    REQUIRE(core.exception().inBranchDelaySlot == branch);
    REQUIRE(core.exceptionProgramCounter() == (branch ? 0x100 : 0x104));
    REQUIRE(core.cause() == (branch ? 0x80000400 : 0x400));
    REQUIRE(core.generalRegister(2) == (branch ? 0xaabbccdd :
      operation == IOPOperation::LoadWord ? 0x12345678 : 0x1f));
    REQUIRE_FALSE(core.branchContinuation().active);
    REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
    REQUIRE(core.retiredInstructions() == 1);
    REQUIRE(core.elapsedCycles() == 2);
    REQUIRE(interrupts.interruptPending());
  }
}

TEST_CASE("IOP direct pin levels survive INTC attachment changes without stale aggregates",
  "[iop][execution][interrupt][intc]")
{
  for (const bool direct : {false, true})
  {
    CAPTURE(direct);
    IOPBus bus;
    IOPBus otherBus;
    IOPInterruptController interrupts;
    IOPInterruptController replacement;
    IOPCore core;
    core.attachBus(&bus);
    core.setInterruptInput(IOPInterruptInput::Hardware0, direct);
    core.setInterruptInput(IOPInterruptInput::Hardware5, true);
    interrupts.assertSource(IOPInterruptSource::Timer0);
    interrupts.writeMask(0x10);
    interrupts.writeControl(1);
    for (IOPAddress address = 0x100; address <= 0x114; address += 4)
    {
      loadInstruction(&bus, address, 0);
      loadInstruction(&otherBus, address, 0);
    }
    bus.attachInterruptController(&interrupts);
    core.startExecution(0x100);
    core.stepInstruction();
    REQUIRE(core.cause() == 0x8400);
    core.setInterruptInput(IOPInterruptInput::Hardware0, false);
    REQUIRE(core.cause() == 0x8400);
    core.setInterruptInput(IOPInterruptInput::Hardware0, direct);
    bus.attachInterruptController(nullptr);
    core.stepInstruction();
    const IOPWord directPending = 0x8000 | (direct ? 0x400 : 0);
    REQUIRE(core.cause() == directPending);
    bus.attachInterruptController(&interrupts);
    core.stepInstruction();
    REQUIRE(core.cause() == 0x8400);
    bus.attachInterruptController(&replacement);
    core.stepInstruction();
    REQUIRE(core.cause() == directPending);
    bus.attachInterruptController(&interrupts);
    core.stepInstruction();
    REQUIRE(core.cause() == 0x8400);
    core.attachBus(&otherBus);
    core.stepInstruction();
    REQUIRE(core.cause() == directPending);
    REQUIRE(interrupts.status() == 0x10);
    REQUIRE(interrupts.control() == 1);
  }
}

TEST_CASE("IOP reset clears direct pins but resamples an independently owned INTC",
  "[iop][execution][interrupt][intc]")
{
  for (const bool fresh : {false, true})
  {
    CAPTURE(fresh);
    IOPBus bus;
    IOPInterruptController interrupts;
    bus.attachInterruptController(&interrupts);
    IOPCore core;
    core.attachBus(&bus);
    interrupts.assertSource(IOPInterruptSource::Timer0);
    interrupts.writeMask(0x10);
    interrupts.writeControl(1);
    core.setInterruptInput(IOPInterruptInput::Hardware5, true);
    loadInstruction(&bus, 0x100, 0);
    core.startExecution(0x100);
    core.stepInstruction();
    REQUIRE(core.cause() == 0x8400);
    if (fresh)
    {
      core.prepareFreshExecution(0x100, 0x200, 0x300);
    }
    else
    {
      core.reset();
    }
    REQUIRE(core.cause() == 0);
    REQUIRE(interrupts.interruptPending());
    core.startExecution(0x100);
    core.stepInstruction();
    REQUIRE(core.cause() == 0x400);
    REQUIRE(core.exception().kind == IOPException::None);
    REQUIRE(core.retiredInstructions() == 1);
    REQUIRE(interrupts.control() == 1);
  }
}

TEST_CASE("IOP guest handler independently masks and acknowledges simultaneous INTC causes",
  "[iop][execution][interrupt][intc][conformance]")
{
  constexpr IOPWord first = 0x10;
  constexpr IOPWord second = 0x02000000;
  constexpr IOPWord third = 0x10000;
  IOPBus bus;
  IOPInterruptController interrupts;
  bus.attachInterruptController(&interrupts);
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setGeneralRegister(&core, 1, IOPBus::INTC_STAT);
  IOPCoreTestAccess::setGeneralRegister(&core, 2, first);
  IOPCoreTestAccess::setGeneralRegister(&core, 3, 1);
  IOPCoreTestAccess::setGeneralRegister(&core, 6, 0x00200401);
  IOPCoreTestAccess::setGeneralRegister(&core, 12, second);
  IOPCoreTestAccess::setGeneralRegister(&core, 14, third);
  const IOPWord program[] = {
    immediateInstruction(0x2b, 1, 2, 4),
    immediateInstruction(0x2b, 1, 3, 8),
    immediateInstruction(0x23, 1, 4, 0),
    immediateInstruction(0x23, 1, 5, 4),
    0,
    cop0TransferInstruction(4, 6, 12),
    immediateInstruction(9, 0, 7, 7),
    immediateInstruction(0x2b, 1, 12, 4),
    immediateInstruction(9, 0, 13, 9),
    immediateInstruction(0x2b, 1, 14, 4),
    immediateInstruction(9, 0, 15, 11),
    immediateInstruction(0x2b, 1, 2, 4),
    immediateInstruction(9, 0, 16, 13)
  };
  const IOPWord handler[] = {
    immediateInstruction(0x23, 1, 8, 0),
    immediateInstruction(0x23, 1, 9, 4),
    0,
    registerInstruction(0x24, 8, 9, 10),
    registerInstruction(0x27, 10, 0, 11),
    immediateInstruction(0x2b, 1, 11, 0),
    cop0TransferInstruction(0, 26, 14),
    0,
    registerInstruction(8, 26, 0, 0),
    0x42000010
  };
  for (std::size_t index = 0; index < sizeof(program) / sizeof(program[0]); ++index)
  {
    loadInstruction(&bus, 0x100 + 4 * index, program[index]);
  }
  for (std::size_t index = 0; index < sizeof(handler) / sizeof(handler[0]); ++index)
  {
    loadInstruction(&bus, 0x80 + 4 * index, handler[index]);
  }
  interrupts.assertSource(IOPInterruptSource::Timer0);
  interrupts.assertSource(IOPInterruptSource::Timer0);
  interrupts.assertSource(IOPInterruptSource::FireWireDMA);
  interrupts.assertSource(IOPInterruptSource::Timer5);
  core.startExecution(0x100);
  for (unsigned step = 0; step < 6; ++step)
  {
    core.stepInstruction();
    REQUIRE(core.exception().kind == IOPException::None);
  }
  REQUIRE(core.generalRegister(4) == (first | second | third));
  REQUIRE(core.generalRegister(5) == first);
  const auto service = [&](IOPAddress restart, IOPWord pending, IOPWord mask)
  {
    CAPTURE(restart);
    CAPTURE(pending);
    CAPTURE(mask);
    core.stepInstruction();
    REQUIRE(core.exception().kind == IOPException::Interrupt);
    REQUIRE(core.exceptionProgramCounter() == restart);
    REQUIRE(core.cause() == 0x400);
    REQUIRE(interrupts.status() == pending);
    for (unsigned step = 0; step < 10; ++step)
    {
      core.stepInstruction();
      REQUIRE(core.exception().kind == IOPException::None);
    }
    REQUIRE(core.generalRegister(8) == pending);
    REQUIRE(core.generalRegister(9) == mask);
    REQUIRE(core.generalRegister(10) == (pending & mask));
    REQUIRE(interrupts.status() == (pending & ~mask));
    REQUIRE(interrupts.mask() == mask);
    REQUIRE(interrupts.control() == 1);
    REQUIRE(core.cause() == 0);
    REQUIRE(core.status() == 0x00200401);
    REQUIRE(core.programCounter() == restart);
    core.stepInstruction();
    REQUIRE(core.exception().kind == IOPException::None);
  };
  service(0x118, first | second | third, first);
  REQUIRE(core.generalRegister(7) == 7);
  core.stepInstruction();
  REQUIRE(core.exception().kind == IOPException::None);
  REQUIRE(interrupts.mask() == second);
  interrupts.assertSource(IOPInterruptSource::Timer0);
  REQUIRE(interrupts.status() == (first | second | third));
  service(0x120, first | second | third, second);
  REQUIRE(core.generalRegister(13) == 9);
  core.stepInstruction();
  service(0x128, first | third, third);
  REQUIRE(core.generalRegister(15) == 11);
  core.stepInstruction();
  service(0x130, first, first);
  REQUIRE(core.generalRegister(16) == 13);
  REQUIRE(interrupts.status() == 0);
  REQUIRE(core.retiredInstructions() == 53);
  REQUIRE(core.elapsedCycles() == 57);
}

TEST_CASE("IOP guest master acknowledgement gates but does not acknowledge pending sources",
  "[iop][execution][interrupt][intc][conformance]")
{
  IOPBus bus;
  IOPInterruptController interrupts;
  bus.attachInterruptController(&interrupts);
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setCOP0State(&core, {0, 0x00200401, 0, 0});
  IOPCoreTestAccess::setGeneralRegister(&core, 1, IOPBus::INTC_STAT);
  IOPCoreTestAccess::setGeneralRegister(&core, 2, 1);
  IOPCoreTestAccess::setGeneralRegister(&core, 3, 0x10010);
  const IOPWord program[] = {
    immediateInstruction(0x2b, 1, 3, 4),
    immediateInstruction(0x2b, 1, 2, 8),
    immediateInstruction(9, 0, 7, 7),
    immediateInstruction(0x2b, 1, 2, 8),
    immediateInstruction(9, 0, 8, 9),
    immediateInstruction(0x2b, 1, 0, 0),
    immediateInstruction(0x2b, 1, 2, 8),
    immediateInstruction(9, 0, 9, 11)
  };
  const IOPWord handler[] = {
    cop0TransferInstruction(4, 0, 13),
    immediateInstruction(0x23, 1, 4, 8),
    immediateInstruction(0x23, 1, 5, 8),
    immediateInstruction(0x23, 1, 6, 0),
    0,
    cop0TransferInstruction(0, 26, 14),
    0,
    registerInstruction(8, 26, 0, 0),
    0x42000010
  };
  for (std::size_t index = 0; index < sizeof(program) / sizeof(program[0]); ++index)
  {
    loadInstruction(&bus, 0x100 + 4 * index, program[index]);
  }
  for (std::size_t index = 0; index < sizeof(handler) / sizeof(handler[0]); ++index)
  {
    loadInstruction(&bus, 0x80 + 4 * index, handler[index]);
  }
  interrupts.assertSource(IOPInterruptSource::Timer0);
  interrupts.assertSource(IOPInterruptSource::Timer5);
  core.startExecution(0x100);
  core.stepInstruction();
  core.stepInstruction();
  for (const IOPAddress restart : {0x108u, 0x110u})
  {
    CAPTURE(restart);
    const IOPWord pending = restart == 0x108 ? 0x10010 : 0x02010010;
    core.stepInstruction();
    REQUIRE(core.exception().kind == IOPException::Interrupt);
    REQUIRE(core.exceptionProgramCounter() == restart);
    core.stepInstruction();
    REQUIRE(core.cause() == 0x400);
    REQUIRE(interrupts.status() == pending);
    core.stepInstruction();
    REQUIRE(interrupts.control() == 0);
    REQUIRE(core.cause() == 0x400);
    for (unsigned step = 0; step < 7; ++step)
    {
      core.stepInstruction();
      REQUIRE(core.exception().kind == IOPException::None);
    }
    REQUIRE(core.generalRegister(4) == 1);
    REQUIRE(core.generalRegister(5) == 0);
    REQUIRE(core.generalRegister(6) == pending);
    REQUIRE(interrupts.status() == pending);
    REQUIRE(interrupts.mask() == 0x10010);
    REQUIRE(core.cause() == 0);
    REQUIRE(core.programCounter() == restart);
    REQUIRE(core.status() == 0x00200401);
    core.stepInstruction();
    if (restart == 0x108)
    {
      REQUIRE(core.generalRegister(7) == 7);
      interrupts.assertSource(IOPInterruptSource::Timer0);
      interrupts.assertSource(IOPInterruptSource::FireWireDMA);
      core.stepInstruction();
    }
  }
  REQUIRE(core.generalRegister(8) == 9);
  core.stepInstruction();
  REQUIRE(interrupts.status() == 0);
  core.stepInstruction();
  REQUIRE(interrupts.control() == 1);
  core.stepInstruction();
  REQUIRE(core.exception().kind == IOPException::None);
  REQUIRE(core.generalRegister(9) == 11);
  REQUIRE(core.retiredInstructions() == 26);
  REQUIRE(core.elapsedCycles() == 28);
}

TEST_CASE("IOP guest RFE retriggers an unacknowledged INTC level until selective status clear",
  "[iop][execution][interrupt][intc][conformance]")
{
  IOPBus bus;
  IOPInterruptController interrupts;
  bus.attachInterruptController(&interrupts);
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setGeneralRegister(&core, 1, IOPBus::INTC_STAT);
  IOPCoreTestAccess::setGeneralRegister(&core, 3, 1);
  IOPCoreTestAccess::setGeneralRegister(&core, 4, 1);
  IOPCoreTestAccess::setGeneralRegister(&core, 5, 0x00200401);
  loadInstruction(&bus, 0x100, immediateInstruction(0x2b, 1, 3, 4));
  loadInstruction(&bus, 0x104, immediateInstruction(0x2b, 1, 4, 8));
  loadInstruction(&bus, 0x108, cop0TransferInstruction(4, 5, 12));
  loadInstruction(&bus, 0x10c, immediateInstruction(9, 0, 6, 7));
  loadInstruction(&bus, 0x80, immediateInstruction(9, 2, 2, 0xffff));
  loadInstruction(&bus, 0x84, immediateInstruction(0x2b, 1, 2, 0));
  loadInstruction(&bus, 0x88, cop0TransferInstruction(0, 26, 14));
  loadInstruction(&bus, 0x8c, 0);
  loadInstruction(&bus, 0x90, registerInstruction(8, 26, 0, 0));
  loadInstruction(&bus, 0x94, 0x42000010);
  interrupts.assertSource(IOPInterruptSource::VBlankStart);
  interrupts.assertSource(IOPInterruptSource::Timer5);
  core.startExecution(0x100);
  for (unsigned step = 0; step < 3; ++step)
  {
    core.stepInstruction();
  }
  for (const bool acknowledge : {false, true})
  {
    CAPTURE(acknowledge);
    core.stepInstruction();
    REQUIRE(core.exception().kind == IOPException::Interrupt);
    REQUIRE(core.exceptionProgramCounter() == 0x10c);
    REQUIRE(core.generalRegister(6) == 0);
    for (unsigned step = 0; step < 6; ++step)
    {
      core.stepInstruction();
      REQUIRE(core.exception().kind == IOPException::None);
    }
    REQUIRE(core.programCounter() == 0x10c);
    REQUIRE(core.status() == 0x00200401);
    REQUIRE(interrupts.status() == (acknowledge ? 0x10000 : 0x10001));
    REQUIRE(interrupts.mask() == 1);
    REQUIRE(interrupts.control() == 1);
    REQUIRE(core.cause() == (acknowledge ? 0 : 0x400));
  }
  core.stepInstruction();
  REQUIRE(core.exception().kind == IOPException::None);
  REQUIRE(core.generalRegister(6) == 7);
  REQUIRE(core.retiredInstructions() == 16);
  REQUIRE(core.elapsedCycles() == 18);
}

TEST_CASE("IOP complete reset clears active INTC execution in every owner reset order",
  "[iop][execution][interrupt][intc][conformance]")
{
  enum class Owner { Core, Bus, Controller };
  const Owner orders[][3] = {
    {Owner::Core, Owner::Bus, Owner::Controller},
    {Owner::Core, Owner::Controller, Owner::Bus},
    {Owner::Bus, Owner::Core, Owner::Controller},
    {Owner::Bus, Owner::Controller, Owner::Core},
    {Owner::Controller, Owner::Core, Owner::Bus},
    {Owner::Controller, Owner::Bus, Owner::Core}
  };
  for (const auto &order : orders)
  {
    for (const bool branch : {false, true})
    {
      CAPTURE(static_cast<unsigned>(order[0]));
      CAPTURE(static_cast<unsigned>(order[1]));
      CAPTURE(branch);
      IOPBus bus;
      IOPInterruptController interrupts;
      bus.attachInterruptController(&interrupts);
      IOPCore core;
      core.attachBus(&bus);
      IOPCoreTestAccess::setGeneralRegister(&core, 1, IOPBus::INTC_CTRL);
      interrupts.assertSource(IOPInterruptSource::Timer0);
      interrupts.writeMask(0x10);
      interrupts.writeControl(1);
      loadInstruction(&bus, 0x100, branch
        ? immediateInstruction(4, 0, 0, 7)
        : immediateInstruction(0x23, 1, 2, 0));
      REQUIRE(bus.write32(0x200, 0x12345678).status == IOPBusStatus::Completed);
      core.startExecution(0x100);
      core.stepInstruction();
      REQUIRE(core.branchContinuation().active == branch);
      REQUIRE(core.delayedResultContinuation().source ==
        (branch ? IOPDelayedResultSource::None : IOPDelayedResultSource::Load));
      core.haltExecution();
      interrupts.assertSource(IOPInterruptSource::Timer5);
      interrupts.writeControl(1);
      core.setInterruptInput(IOPInterruptInput::Hardware5, true);
      REQUIRE(core.cause() == 0x8400);
      for (Owner owner : order)
      {
        switch (owner)
        {
          case Owner::Core: core.reset(); break;
          case Owner::Bus: bus.reset(); break;
          case Owner::Controller: interrupts.reset(); break;
        }
      }
      REQUIRE(core.executionState() == IOPExecutionState::Halted);
      REQUIRE(core.programCounter() == IOPReset::VECTOR);
      REQUIRE(core.cause() == 0);
      REQUIRE(core.elapsedCycles() == 0);
      REQUIRE(core.retiredInstructions() == 0);
      REQUIRE_FALSE(core.branchContinuation().active);
      REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
      REQUIRE(bus.read32(0x100).value == 0);
      REQUIRE(bus.read32(0x200).value == 0);
      for (const IOPAddress address :
           {IOPBus::INTC_STAT, IOPBus::INTC_MASK, IOPBus::INTC_CTRL})
      {
        const auto read = bus.read32(address);
        REQUIRE(read.status == IOPBusStatus::Completed);
        REQUIRE(read.value == 0);
      }
      IOPCoreTestAccess::setGeneralRegister(&core, 1, IOPBus::INTC_STAT);
      IOPCoreTestAccess::setGeneralRegister(&core, 2, 0x10);
      IOPCoreTestAccess::setGeneralRegister(&core, 3, 1);
      IOPCoreTestAccess::setGeneralRegister(&core, 4, 0x00200401);
      loadInstruction(&bus, 0x100, immediateInstruction(0x2b, 1, 2, 4));
      loadInstruction(&bus, 0x104, immediateInstruction(0x2b, 1, 3, 8));
      loadInstruction(&bus, 0x108, cop0TransferInstruction(4, 4, 12));
      loadInstruction(&bus, 0x10c, immediateInstruction(9, 0, 5, 7));
      core.startExecution(0x100);
      for (unsigned step = 0; step < 4; ++step)
      {
        core.stepInstruction();
        REQUIRE(core.exception().kind == IOPException::None);
        REQUIRE(core.cause() == 0);
      }
      REQUIRE(core.generalRegister(5) == 7);
      interrupts.assertSource(IOPInterruptSource::Timer0);
      core.stepInstruction();
      REQUIRE(core.exception().kind == IOPException::Interrupt);
      REQUIRE(core.exceptionProgramCounter() == 0x110);
      REQUIRE(core.cause() == 0x400);
      REQUIRE(interrupts.status() == 0x10);
    }
  }
}

TEST_CASE("IOP hardware interrupt inputs preserve unrelated Cause fields and mask independently",
  "[iop][execution][interrupt]")
{
  const IOPInterruptInput inputs[] = {
    IOPInterruptInput::Hardware0, IOPInterruptInput::Hardware1,
    IOPInterruptInput::Hardware2, IOPInterruptInput::Hardware3,
    IOPInterruptInput::Hardware4, IOPInterruptInput::Hardware5
  };
  for (std::uint8_t pending = 0; pending < 6; ++pending)
  {
    for (std::uint8_t enabled = 0; enabled < 6; ++enabled)
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      const IOPWord pendingBit = IOPWord(1) << (10 + pending);
      const IOPWord enabledBit = IOPWord(1) << (10 + enabled);
      IOPCoreTestAccess::setCOP0State(
        &core, {0, 0x00200001 | enabledBit, 0xb0000330, 0x200});
      core.setInterruptInput(inputs[pending], true);
      REQUIRE(core.cause() == (0xb0000330 | pendingBit));
      REQUIRE(core.executionState() == IOPExecutionState::Halted);
      REQUIRE(core.exception().kind == IOPException::None);
      core.setInterruptInput(inputs[pending], true);
      REQUIRE(core.cause() == (0xb0000330 | pendingBit));
      loadInstruction(&bus, 0x100, 0);
      core.startExecution(0x100);
      core.stepInstruction();
      const bool admitted = pending == enabled;
      REQUIRE(core.exception().kind ==
        (admitted ? IOPException::Interrupt : IOPException::None));
      REQUIRE(core.retiredInstructions() == (admitted ? 0 : 1));
      REQUIRE(core.cause() == ((admitted ? 0x300 : 0xb0000330) | pendingBit));
      core.setInterruptInput(inputs[pending], false);
      REQUIRE(core.cause() == (admitted ? 0x300 : 0xb0000330));
    }
  }
}

TEST_CASE("IOP live hardware levels survive guest Cause writes and host pauses",
  "[iop][execution][interrupt]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  core.setInterruptInput(IOPInterruptInput::Hardware0, true);
  core.setInterruptInput(IOPInterruptInput::Hardware5, true);
  REQUIRE(core.cause() == 0x8400);
  REQUIRE_THROWS_AS(
    core.setInterruptInput(static_cast<IOPInterruptInput>(6), true),
    std::out_of_range);
  REQUIRE(core.cause() == 0x8400);
  loadInstruction(&bus, 0x100, cop0TransferInstruction(4, 2, 13));
  loadInstruction(&bus, 0x104, cop0TransferInstruction(0, 3, 13));
  loadInstruction(&bus, 0x108, 0);
  IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xffffffff);
  core.startExecution(0x100);
  core.stepInstruction();
  REQUIRE(core.cause() == 0x8700);
  core.haltExecution();
  core.setInterruptInput(IOPInterruptInput::Hardware0, false);
  REQUIRE(core.cause() == 0x8300);
  REQUIRE_NOTHROW(core.resumeExecution());
  core.stepInstruction();
  core.setInterruptInput(IOPInterruptInput::Hardware5, false);
  REQUIRE(core.cause() == 0x300);
  core.stepInstruction();
  REQUIRE(core.generalRegister(3) == 0x8300);
  core.setInterruptInput(IOPInterruptInput::Hardware3, true);
  core.reset();
  REQUIRE(core.cause() == 0);
  REQUIRE(core.exception().kind == IOPException::None);
  core.setInterruptInput(IOPInterruptInput::Hardware1, true);
  core.prepareFreshExecution(0x100, 0x200, 0x300);
  REQUIRE(core.cause() == 0);
}

TEST_CASE("IOP MTC0 changes interrupt eligibility at the following boundary",
  "[iop][execution][interrupt]")
{
  struct WriteCase
  {
    std::uint8_t index;
    IOPWord status;
    IOPWord cause;
    IOPWord value;
  };
  for (const WriteCase test : {
         WriteCase{12, 0x00200100, 0x100, 0x00200101},
         WriteCase{12, 0x00200001, 0x100, 0x00200101},
         WriteCase{13, 0x00200101, 0, 0x100}})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setCOP0State(&core, {0, test.status, test.cause, 0});
    IOPCoreTestAccess::setGeneralRegister(&core, 2, test.value);
    loadInstruction(&bus, 0x100, cop0TransferInstruction(4, 2, test.index));
    loadInstruction(&bus, 0x104, immediateInstruction(9, 0, 3, 7));
    core.startExecution(0x100);
    core.stepInstruction();
    REQUIRE(core.exception().kind == IOPException::None);
    REQUIRE(core.programCounter() == 0x104);
    REQUIRE(core.retiredInstructions() == 1);
    core.stepInstruction();
    REQUIRE(core.exception().kind == IOPException::Interrupt);
    REQUIRE(core.exceptionProgramCounter() == 0x104);
    REQUIRE(core.generalRegister(3) == 0);
    REQUIRE(core.retiredInstructions() == 1);
  }
}

TEST_CASE("IOP interrupts cancel taken and untaken delay slots with branch EPC BD",
  "[iop][execution][interrupt]")
{
  for (const bool taken : {false, true})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    loadInstruction(&bus, 0x100, immediateInstruction(taken ? 4 : 5, 0, 0, 7));
    loadInstruction(&bus, 0x104, immediateInstruction(9, 0, 2, 7));
    IOPCoreTestAccess::setCOP0State(&core, {0, 0x00200401, 0, 0});
    core.startExecution(0x100);
    core.stepInstruction();
    core.setInterruptInput(IOPInterruptInput::Hardware0, true);
    core.stepInstruction();
    REQUIRE(core.exception().kind == IOPException::Interrupt);
    REQUIRE(core.exception().inBranchDelaySlot);
    REQUIRE(core.cause() == 0x80000400);
    REQUIRE(core.exceptionProgramCounter() == 0x100);
    REQUIRE_FALSE(core.branchContinuation().active);
    REQUIRE(core.branchContinuation().targetAddress == 0);
    REQUIRE(core.generalRegister(2) == 0);
    REQUIRE(core.retiredInstructions() == 1);
    core.haltExecution();
    core.resumeExecution();
    REQUIRE(core.exception().kind == IOPException::Interrupt);
    core.stepInstruction();
    REQUIRE(core.exception().kind == IOPException::None);
    REQUIRE(core.programCounter() == 0x80000084);
    REQUIRE(core.retiredInstructions() == 2);
  }
}

TEST_CASE("IOP accepted boundary interrupts precede unattempted fetch faults",
  "[iop][execution][interrupt]")
{
  for (const IOPAddress entry : {0x102u, 0x800000u, 0x80000100u})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setCOP0State(&core, {0x12345679, 0x00200103, 0x100, 0});
    core.startExecution(entry);
    core.stepInstruction();
    REQUIRE(core.exception().kind == IOPException::Interrupt);
    REQUIRE(core.exceptionProgramCounter() == entry);
    REQUIRE(core.cause() == 0x100);
    REQUIRE(core.badVirtualAddress() == 0x12345679);
    REQUIRE(core.programCounter() == 0x80000080);
    REQUIRE(core.elapsedCycles() == 1);
    REQUIRE(core.retiredInstructions() == 0);
  }
}

TEST_CASE("IOP RFE preserves completed prior accesses and older delayed writeback",
  "[iop][execution][rfe][interrupt]")
{
  for (const std::uint8_t opcode : {0x23, 0x2b, 0x10})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setCOP0State(&core, {0, 0x0020000c, 0, 0x300});
    IOPCoreTestAccess::setGeneralRegister(&core, 1, 0x80000200);
    IOPCoreTestAccess::setGeneralRegister(&core, 2, 0x12345678);
    REQUIRE(bus.write32(0x200, 0xabcdef01).status == IOPBusStatus::Completed);
    loadInstruction(&bus, 0x100, opcode == 0x10
      ? cop0TransferInstruction(0, 2, 15)
      : immediateInstruction(opcode, 1, 2, 0));
    loadInstruction(&bus, 0x104, 0x42000010);
    core.startExecution(0x80000100);
    core.stepInstruction();
    core.stepInstruction();
    REQUIRE(core.status() == 0x00200003);
    REQUIRE(core.generalRegister(2) ==
      (opcode == 0x23 ? 0xabcdef01 : opcode == 0x10 ? 0x1f : 0x12345678));
    REQUIRE(bus.read32(0x200).value == (opcode == 0x2b ? 0x12345678 : 0xabcdef01));
    REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
    REQUIRE(core.programCounter() == 0x80000108);
    REQUIRE(core.retiredInstructions() == 2);
    core.stepInstruction();
    REQUIRE(core.exception().kind == IOPException::AddressErrorLoadOrFetch);
    REQUIRE(core.exceptionProgramCounter() == 0x80000108);
  }
}

TEST_CASE("IOP nested exception stack restores only through successive RFE instructions",
  "[iop][execution][rfe][interrupt]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setCOP0State(&core, {0, 0x00200003, 0, 0});
  loadInstruction(&bus, 0x100, 0x0000000c);
  loadInstruction(&bus, 0x80, 0x0000000d);
  core.startExecution(0x100);
  core.stepInstruction();
  REQUIRE(core.status() == 0x0020000c);
  core.stepInstruction();
  REQUIRE(core.status() == 0x00200030);
  REQUIRE(core.exceptionProgramCounter() == 0x80000080);
  loadInstruction(&bus, 0x80, 0x42000010);
  loadInstruction(&bus, 0x84, 0x42000010);
  // The host patch does not snoop the cached breakpoint handler.
  IOPCoreTestAccess::setProgramCounter(&core, 0xa0000080);
  core.stepInstruction();
  REQUIRE(core.status() == 0x0020003c);
  core.stepInstruction();
  REQUIRE(core.status() == 0x0020003f);
  REQUIRE(core.exceptionProgramCounter() == 0x80000080);
  REQUIRE(core.cause() == 0x24);
  REQUIRE(core.exception().kind == IOPException::None);
  REQUIRE(core.retiredInstructions() == 2);
}

TEST_CASE("IOP RFE effect does not pop the stack when a fault suppresses retirement",
  "[iop][execution][rfe][interrupt]")
{
  IOPCore core;
  IOPCoreTestAccess::setCOP0State(&core, {0, 0x0020000c, 0, 0});
  core.startExecution(0x100);
  auto effects = IOPCoreTestAccess::instructionEffects(core, 0x42000010);
  effects.exception.kind = IOPException::ReservedInstruction;
  IOPCoreTestAccess::commitInstructionEffects(&core, 0x100, effects);
  REQUIRE(core.status() == 0x00200030);
  REQUIRE(core.exception().kind == IOPException::ReservedInstruction);
  REQUIRE(core.retiredInstructions() == 0);
}

TEST_CASE("IOP asserted interrupt levels retrigger after return until deasserted",
  "[iop][execution][rfe][interrupt]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setCOP0State(&core, {0, 0x00200403, 0, 0});
  IOPCoreTestAccess::setGeneralRegister(&core, 26, 0x100);
  loadInstruction(&bus, 0x80, registerInstruction(8, 26, 0, 0));
  loadInstruction(&bus, 0x84, 0x42000010);
  loadInstruction(&bus, 0x100, immediateInstruction(9, 0, 2, 7));
  core.setInterruptInput(IOPInterruptInput::Hardware0, true);
  core.startExecution(0x100);
  for (const bool deassert : {false, true})
  {
    core.stepInstruction();
    REQUIRE(core.exception().kind == IOPException::Interrupt);
    REQUIRE(core.exceptionProgramCounter() == 0x100);
    REQUIRE(core.cause() == 0x400);
    if (deassert)
    {
      core.setInterruptInput(IOPInterruptInput::Hardware0, false);
    }
    core.stepInstruction();
    core.stepInstruction();
    REQUIRE(core.programCounter() == 0x100);
    REQUIRE(core.status() == 0x00200403);
    REQUIRE(core.generalRegister(2) == 0);
  }
  core.stepInstruction();
  REQUIRE(core.exception().kind == IOPException::None);
  REQUIRE(core.generalRegister(2) == 7);
  REQUIRE(core.retiredInstructions() == 5);
  REQUIRE(core.elapsedCycles() == 7);
}

TEST_CASE("IOP syscall handler returns through delayed EPC read JR and RFE",
  "[iop][execution][rfe][interrupt]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setCOP0State(&core, {0, 0x00200003, 0, 0});
  loadInstruction(&bus, 0x100, 0x0000000c);
  loadInstruction(&bus, 0x104, immediateInstruction(9, 0, 2, 7));
  loadInstruction(&bus, 0x80, cop0TransferInstruction(0, 26, 14));
  loadInstruction(&bus, 0x84, 0);
  loadInstruction(&bus, 0x88, immediateInstruction(9, 26, 26, 4));
  loadInstruction(&bus, 0x8c, registerInstruction(8, 26, 0, 0));
  loadInstruction(&bus, 0x90, 0x42000010);
  core.startExecution(0x100);
  core.stepInstruction();
  REQUIRE(core.exception().kind == IOPException::SystemCall);
  for (int instruction = 0; instruction < 5; ++instruction)
  {
    core.stepInstruction();
  }
  REQUIRE(core.programCounter() == 0x104);
  REQUIRE(core.status() == 0x00200003);
  REQUIRE(core.exceptionProgramCounter() == 0x100);
  REQUIRE(core.exception().kind == IOPException::None);
  REQUIRE_FALSE(core.branchContinuation().active);
  core.stepInstruction();
  REQUIRE(core.generalRegister(2) == 7);
  REQUIRE(core.retiredInstructions() == 6);
  REQUIRE(core.elapsedCycles() == 7);
}

TEST_CASE("IOP masked pending interrupts preserve address fault priority over physical rejection",
  "[iop][execution][conformance]")
{
  for (const bool globallyEnabled : {false, true})
  {
    for (const bool user : {false, true})
    {
      for (const std::uint8_t opcode : {0x21, 0x25, 0x23, 0x29, 0x2b})
      {
        IOPBus bus;
        IOPCore core;
        core.attachBus(&bus);
        const IOPAddress address = user ? 0x80800001 : 0x00800001;
        const IOPWord originalStatus = 0x00200000 | (user ? 2 : 0) |
          (globallyEnabled ? 1 : 0x500);
        IOPCoreTestAccess::setCOP0State(
          &core, {0x12345679, originalStatus, 0x100, 0x300});
        core.setInterruptInput(IOPInterruptInput::Hardware0, true);
        IOPCoreTestAccess::setGeneralRegister(&core, 1, address);
        IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xaabbccdd);
        loadInstruction(&bus, 0x100, immediateInstruction(opcode, 1, 2, 0));
        core.startExecution(0x100);
        const bool store = opcode == 0x29 || opcode == 0x2b;
        const IOPAccessOutcome expectedOutcome = user
          ? IOPAccessOutcome::ProtectionFailure
          : IOPAccessOutcome::Misaligned;
        if (store)
        {
          REQUIRE(core.writeData32(address, 0).outcome == expectedOutcome);
        }
        else
        {
          REQUIRE(core.readData32(address).outcome == expectedOutcome);
        }
        core.stepInstruction();
        REQUIRE(core.exception().kind == (store
          ? IOPException::AddressErrorStore : IOPException::AddressErrorLoadOrFetch));
        REQUIRE(core.exception().badVirtualAddress == address);
        REQUIRE(core.badVirtualAddress() == address);
        REQUIRE(core.cause() == (store ? 0x514 : 0x510));
        REQUIRE(core.exceptionProgramCounter() == 0x100);
        REQUIRE(core.generalRegister(2) == 0xaabbccdd);
        REQUIRE(core.storeFailure().width == IOPStoreWidth::None);
        REQUIRE(core.retiredInstructions() == 0);
        REQUIRE(core.elapsedCycles() == 1);
      }
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      IOPCoreTestAccess::setCOP0State(
        &core, {0, 0x00200000 | (user ? 2u : 0) |
          (globallyEnabled ? 1u : 0x500), 0x100, 0});
      core.setInterruptInput(IOPInterruptInput::Hardware0, true);
      const IOPAddress entry = user ? 0x80800002 : 0x00800002;
      core.startExecution(entry);
      REQUIRE(core.fetchInstruction().outcome == (user
        ? IOPAccessOutcome::ProtectionFailure : IOPAccessOutcome::Misaligned));
      core.stepInstruction();
      REQUIRE(core.exception().kind == IOPException::AddressErrorLoadOrFetch);
      REQUIRE(core.badVirtualAddress() == entry);
      REQUIRE(core.exceptionProgramCounter() == entry);
      REQUIRE(core.cause() == 0x510);
    }
  }
}

TEST_CASE("IOP interrupts arriving during host pauses preserve pending work until resume",
  "[iop][execution][conformance]")
{
  for (const IOPOperation operation : {
         IOPOperation::LoadWord, IOPOperation::MoveFromCOP0, IOPOperation::BranchEqual})
  {
    for (const bool deassert : {false, true})
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      REQUIRE(bus.write32(0x200, 0x12345678).status == IOPBusStatus::Completed);
      IOPCoreTestAccess::setCOP0State(&core, {0, 0x00200401, 0, 0x300});
      const bool branch = operation == IOPOperation::BranchEqual;
      loadInstruction(&bus, 0x100, branch
        ? immediateInstruction(4, 0, 0, 7)
        : operation == IOPOperation::LoadWord
          ? immediateInstruction(0x23, 0, 2, 0x200)
          : cop0TransferInstruction(0, 2, 15));
      loadInstruction(&bus, 0x104, immediateInstruction(9, 0, 3, 7));
      core.startExecution(0x100);
      core.stepInstruction();
      core.haltExecution();
      core.setInterruptInput(IOPInterruptInput::Hardware0, true);
      if (deassert)
      {
        core.setInterruptInput(IOPInterruptInput::Hardware0, false);
      }
      REQUIRE_THROWS_AS(core.stepInstruction(), std::logic_error);
      REQUIRE(core.elapsedCycles() == 1);
      REQUIRE(core.retiredInstructions() == 1);
      REQUIRE(core.exception().kind == IOPException::None);
      REQUIRE(core.exceptionProgramCounter() == 0x300);
      REQUIRE(core.generalRegister(2) == 0);
      REQUIRE(core.branchContinuation().active == branch);
      REQUIRE((core.delayedResultContinuation().source != IOPDelayedResultSource::None) ==
        !branch);
      core.resumeExecution();
      REQUIRE(core.exception().kind == IOPException::None);
      REQUIRE(core.programCounter() == 0x104);
      core.stepInstruction();
      REQUIRE(core.exception().kind == (deassert ? IOPException::None : IOPException::Interrupt));
      REQUIRE(core.generalRegister(2) == (branch ? 0
        : operation == IOPOperation::LoadWord ? 0x12345678 : 0x1f));
      REQUIRE(core.generalRegister(3) == (deassert ? 7 : 0));
      REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
      REQUIRE_FALSE(core.branchContinuation().active);
      REQUIRE(core.retiredInstructions() == (deassert ? 2 : 1));
      REQUIRE(core.elapsedCycles() == 2);
      REQUIRE(core.programCounter() == (deassert
        ? (branch ? 0x120 : 0x108) : 0x80000080));
      if (!deassert)
      {
        REQUIRE(core.exception().inBranchDelaySlot == branch);
        REQUIRE(core.exceptionProgramCounter() == (branch ? 0x100 : 0x104));
        REQUIRE(core.cause() == (branch ? 0x80000400 : 0x400));
      }
    }
  }
}

TEST_CASE("IOP restart reset and fresh preparation clear actual exception entry events",
  "[iop][execution][conformance]")
{
  enum class Restart
  {
    Execution,
    Reset,
    Fresh
  };
  for (const IOPException exception : {
         IOPException::AddressErrorLoadOrFetch, IOPException::CoprocessorUnusable,
         IOPException::SystemCall, IOPException::Interrupt})
  {
    for (const Restart restart : {Restart::Execution, Restart::Reset, Restart::Fresh})
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      IOPCoreTestAccess::setCOP0State(&core, {0x12345679, 0x00200403, 0, 0x300});
      IOPCoreTestAccess::setGeneralRegister(&core, 1, 3);
      IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xaabbccdd);
      loadInstruction(&bus, 0x100, immediateInstruction(4, 0, 0, 7));
      loadInstruction(&bus, 0x104,
        exception == IOPException::AddressErrorLoadOrFetch
          ? immediateInstruction(0x23, 1, 2, 0)
          : exception == IOPException::CoprocessorUnusable ? 0x48000000 : 0x0000000c);
      loadInstruction(&bus, 0x200, 0);
      core.startExecution(0x100);
      core.stepInstruction();
      if (exception == IOPException::Interrupt)
      {
        core.setInterruptInput(IOPInterruptInput::Hardware0, true);
      }
      core.stepInstruction();
      REQUIRE(core.exception().kind == exception);
      REQUIRE(core.exception().inBranchDelaySlot);
      core.setInterruptInput(IOPInterruptInput::Hardware0, true);
      const IOPWord savedStatus = core.status();
      const IOPWord savedCause = core.cause();
      const IOPAddress savedAddress = core.badVirtualAddress();
      switch (restart)
      {
        case Restart::Execution:
          core.startExecution(0x200);
          break;
        case Restart::Reset:
          core.reset();
          REQUIRE(core.programCounter() == IOPReset::VECTOR);
          break;
        case Restart::Fresh:
          core.prepareFreshExecution(0x200, 0x1230, 0x4560);
          REQUIRE(core.programCounter() == 0x200);
          REQUIRE(core.generalRegister(29) == 0x1230);
          REQUIRE(core.generalRegister(31) == 0x4560);
          break;
      }
      REQUIRE(core.hasAttachedBus());
      REQUIRE(core.exception().kind == IOPException::None);
      REQUIRE(core.exception().badVirtualAddress == 0);
      REQUIRE_FALSE(core.exception().inBranchDelaySlot);
      REQUIRE(core.exception().coprocessor == 0);
      REQUIRE_FALSE(core.branchContinuation().active);
      REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
      REQUIRE(core.storeFailure().width == IOPStoreWidth::None);
      const bool preservesState = restart == Restart::Execution;
      REQUIRE(core.status() == (preservesState ? savedStatus : 0x00600000));
      REQUIRE(core.cause() == (preservesState ? savedCause : 0));
      REQUIRE(core.badVirtualAddress() == (preservesState ? savedAddress : 0));
      REQUIRE(core.exceptionProgramCounter() == (preservesState ? 0x100 : 0));
      REQUIRE(core.generalRegister(2) == (preservesState ? 0xaabbccdd : 0));
      REQUIRE(core.elapsedCycles() == (preservesState ? 2 : 0));
      REQUIRE(core.retiredInstructions() == (preservesState ? 1 : 0));
      REQUIRE(core.stopReason() == IOPStopReason::None);
      REQUIRE(core.executionState() ==
        (preservesState ? IOPExecutionState::Running : IOPExecutionState::Halted));
      if (!preservesState)
      {
        core.startExecution(0x200);
      }
      core.stepInstruction();
      REQUIRE(core.exception().kind == IOPException::None);
      REQUIRE(core.programCounter() == 0x204);
      REQUIRE(bus.read32(0x100).value == immediateInstruction(4, 0, 0, 7));
    }
  }
}

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
  REQUIRE(core.storeFailure().width == IOPStoreWidth::None);
  REQUIRE(core.storeFailure().value == 0);
  REQUIRE(core.storeFailure().mergeByteEnable == 0);
  REQUIRE(core.storeFailure().access.virtualAddress == 0);
  REQUIRE(core.storeFailure().access.physicalAddress == 0);
  REQUIRE(core.storeFailure().access.cacheRoute == IOPCacheRoute::None);
  REQUIRE(core.storeFailure().access.exception == IOPException::None);
  REQUIRE(core.storeFailure().access.outcome == IOPAccessOutcome::Unmapped);

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

  // Observe physical store completion independently of cached visibility.
  const auto byteStore = core.writeData8(0x201, 0x5a);
  REQUIRE(byteStore.outcome == IOPAccessOutcome::Completed);
  REQUIRE(core.readData8(0xa0000201).value == 0x5a);

  const auto halfwordStore = core.writeData16(0x202, 0x1357);
  REQUIRE(halfwordStore.outcome == IOPAccessOutcome::Completed);
  REQUIRE(core.readData16(0xa0000202).value == 0x1357);

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
  REQUIRE(readOnlyStore.exception == IOPException::None);
  const auto unmappedStore = core.writeData8(0x00800000, 0);
  REQUIRE(unmappedStore.outcome == IOPAccessOutcome::Unmapped);
  REQUIRE(unmappedStore.exception == IOPException::None);
  REQUIRE(core.storeFailure().width == IOPStoreWidth::None);
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

    REQUIRE(core.programCounter() == 0xbfc00180);
    REQUIRE(core.exceptionProgramCounter() == 2);
    REQUIRE(core.executionState() == IOPExecutionState::Running);
    REQUIRE(core.stopReason() == IOPStopReason::None);
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

    REQUIRE(core.programCounter() == 0xbfc00180);
    REQUIRE(core.exceptionProgramCounter() == 0);
    REQUIRE(core.executionState() == IOPExecutionState::Running);
    REQUIRE(
      core.stopReason() ==
      IOPStopReason::None);
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
    {0, 0, 0, 0x48},
    {0, 0, 0, 0x44}
  };
  for (const auto &instruction : unavailableInstructions)
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setCOP0State(&core, {0, 0x60600000, 0, 0});
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
  REQUIRE(core.programCounter() == 0xbfc00180);
  REQUIRE(core.exceptionProgramCounter() == 0x104);
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

TEST_CASE("IOP instruction boundary publishes an older delayed result",
  "[iop][execution][load]")
{
  IOPCore core;
  core.startExecution(0x100);
  IOPCoreTestAccess::setDelayedResultContinuation(&core);

  IOPInstructionEffects effects;
  effects.controlFlow.kind = IOPControlFlowEffect::Sequential;
  effects.completion = IOPInstructionCompletion::Retired;
  effects.stopReason = IOPStopReason::None;

  IOPCoreTestAccess::commitInstructionEffects(
    &core,
    0x100,
    effects);
  REQUIRE(core.programCounter() == 0x104);
  REQUIRE(core.generalRegister(1) == UINT32_C(0x12345678));
  REQUIRE_FALSE(core.branchContinuation().active);
  REQUIRE(
    core.delayedResultContinuation().source ==
    IOPDelayedResultSource::None);
  REQUIRE(core.elapsedCycles() == 1);
  REQUIRE(core.retiredInstructions() == 1);
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
    REQUIRE(core.programCounter() == 0xbfc00180);
    REQUIRE(core.exceptionProgramCounter() == 0);
    REQUIRE(core.executionState() == IOPExecutionState::Running);
    REQUIRE(core.stopReason() == IOPStopReason::None);
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

TEST_CASE("IOP conditional branches always execute one delay slot",
  "[iop][execution][branch]")
{
  struct BranchCase
  {
    IOPWord instruction;
    IOPWord source;
    IOPWord target;
    bool taken;
    bool links;
  };
  const BranchCase cases[] = {
    {immediateInstruction(0x04, 1, 2, 2), 5, 5, true, false},
    {immediateInstruction(0x04, 1, 2, 2), 5, 6, false, false},
    {immediateInstruction(0x05, 1, 2, 2), 5, 6, true, false},
    {immediateInstruction(0x05, 1, 2, 2), 5, 5, false, false},
    {immediateInstruction(0x06, 1, 0, 2), 0, 0, true, false},
    {immediateInstruction(0x06, 1, 0, 2), 1, 0, false, false},
    {immediateInstruction(0x07, 1, 0, 2), 1, 0, true, false},
    {immediateInstruction(0x07, 1, 0, 2), 0, 0, false, false},
    {immediateInstruction(0x01, 1, 0x00, 2),
      UINT32_C(0xffffffff), 0, true, false},
    {immediateInstruction(0x01, 1, 0x00, 2),
      0, 0, false, false},
    {immediateInstruction(0x01, 1, 0x01, 2),
      0, 0, true, false},
    {immediateInstruction(0x01, 1, 0x01, 2),
      UINT32_C(0xffffffff), 0, false, false},
    {immediateInstruction(0x01, 1, 0x10, 2),
      UINT32_C(0x80000000), 0, true, true},
    {immediateInstruction(0x01, 1, 0x10, 2),
      0, 0, false, true},
    {immediateInstruction(0x01, 1, 0x11, 2),
      0, 0, true, true},
    {immediateInstruction(0x01, 1, 0x11, 2),
      UINT32_C(0x80000000), 0, false, true}
  };

  for (const BranchCase &test : cases)
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, test.source);
    IOPCoreTestAccess::setGeneralRegister(&core, 2, test.target);
    IOPCoreTestAccess::setGeneralRegister(
      &core,
      31,
      UINT32_C(0xaaaaaaaa));
    loadInstruction(&bus, 0, test.instruction);
    loadInstruction(
      &bus,
      4,
      immediateInstruction(0x09, 0, 3, 7));
    core.startExecution(0);

    core.stepInstruction();

    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.branchContinuation().active);
    REQUIRE(core.branchContinuation().instructionAddress == 0);
    REQUIRE(
      core.branchContinuation().targetAddress ==
      (test.taken ? 12 : 8));
    REQUIRE(
      core.generalRegister(31) ==
      (test.links ? 8 : UINT32_C(0xaaaaaaaa)));

    core.stepInstruction();

    REQUIRE(core.generalRegister(3) == 7);
    REQUIRE(
      core.programCounter() ==
      (test.taken ? 12 : 8));
    REQUIRE_FALSE(core.branchContinuation().active);
    REQUIRE(core.retiredInstructions() == 2);
  }
}

TEST_CASE("IOP relative branches sign extend their shifted displacement",
  "[iop][execution][branch]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setGeneralRegister(&core, 1, 1);
  loadInstruction(
    &bus,
    4,
    immediateInstruction(0x04, 1, 1, UINT16_C(0xfffe)));
  core.startExecution(4);

  core.stepInstruction();

  REQUIRE(core.programCounter() == 8);
  REQUIRE(core.branchContinuation().active);
  REQUIRE(core.branchContinuation().instructionAddress == 4);
  REQUIRE(core.branchContinuation().targetAddress == 0);
}

TEST_CASE("IOP absolute jumps link and retire through their delay slot",
  "[iop][execution][branch]")
{
  struct JumpCase
  {
    std::uint8_t opcode;
    bool links;
  };
  const JumpCase cases[] = {
    {0x02, false},
    {0x03, true}
  };

  for (const JumpCase &test : cases)
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setGeneralRegister(
      &core,
      31,
      UINT32_C(0xaaaaaaaa));
    loadInstruction(&bus, 0, jumpInstruction(test.opcode, 3));
    loadInstruction(
      &bus,
      4,
      immediateInstruction(0x09, 0, 2, 9));
    core.startExecution(0);

    core.stepInstruction();
    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.branchContinuation().targetAddress == 12);
    REQUIRE(
      core.generalRegister(31) ==
      (test.links ? 8 : UINT32_C(0xaaaaaaaa)));

    core.stepInstruction();
    REQUIRE(core.generalRegister(2) == 9);
    REQUIRE(core.programCounter() == 12);
    REQUIRE_FALSE(core.branchContinuation().active);
  }
}

TEST_CASE("IOP absolute jumps use the delay slot region and wrapping link",
  "[iop][execution][branch]")
{
  SECTION("delay slot selects the target region")
  {
    IOPCore core;
    IOPCoreTestAccess::setProgramCounter(
      &core,
      UINT32_C(0x0ffffffc));

    const IOPInstructionEffects effects =
      IOPCoreTestAccess::instructionEffects(
        core,
        jumpInstruction(0x02, 1));

    REQUIRE(
      effects.controlFlow.kind ==
      IOPControlFlowEffect::ScheduleBranch);
    REQUIRE(
      effects.controlFlow.target ==
      UINT32_C(0x10000004));
  }

  SECTION("link arithmetic wraps to 32 bits")
  {
    IOPCore core;
    IOPCoreTestAccess::setProgramCounter(
      &core,
      UINT32_C(0xfffffffc));

    const IOPInstructionEffects effects =
      IOPCoreTestAccess::instructionEffects(
        core,
        jumpInstruction(0x03, 1));

    REQUIRE(effects.destination.kind == IOPWriteEffect::Write);
    REQUIRE(effects.destination.destination == 31);
    REQUIRE(effects.destination.value == 4);
    REQUIRE(effects.controlFlow.target == 4);
  }
}

TEST_CASE("IOP register jumps capture their source before link writes",
  "[iop][execution][branch]")
{
  SECTION("JR")
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, 16);
    loadInstruction(&bus, 0, registerInstruction(0x08, 1, 0, 0));
    loadInstruction(&bus, 4, 0);
    core.startExecution(0);

    core.stepInstruction();
    REQUIRE(core.branchContinuation().targetAddress == 16);
    core.stepInstruction();
    REQUIRE(core.programCounter() == 16);
  }

  SECTION("JALR source destination alias")
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setGeneralRegister(&core, 5, 16);
    loadInstruction(&bus, 0, registerInstruction(0x09, 5, 0, 5));
    loadInstruction(&bus, 4, 0);
    core.startExecution(0);

    core.stepInstruction();
    REQUIRE(core.generalRegister(5) == 8);
    REQUIRE(core.branchContinuation().targetAddress == 16);
    core.stepInstruction();
    REQUIRE(core.programCounter() == 16);
  }
}

TEST_CASE("IOP conditional link aliases use the pre-link source value",
  "[iop][execution][branch]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setGeneralRegister(
    &core,
    31,
    UINT32_C(0x80000000));
  loadInstruction(
    &bus,
    0,
    immediateInstruction(0x01, 31, 0x11, 2));
  core.startExecution(0);

  core.stepInstruction();

  REQUIRE(core.generalRegister(31) == 8);
  REQUIRE(core.branchContinuation().active);
  REQUIRE(core.branchContinuation().targetAddress == 8);
}

TEST_CASE("IOP JALR rejects an invalid target before its delay slot",
  "[iop][execution][branch]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setGeneralRegister(&core, 1, 3);
  IOPCoreTestAccess::setGeneralRegister(
    &core,
    5,
    UINT32_C(0xaaaaaaaa));
  loadInstruction(&bus, 0, registerInstruction(0x09, 1, 0, 5));
  core.startExecution(0);

  core.stepInstruction();

  REQUIRE(core.programCounter() == 0xbfc00180);
  REQUIRE(core.exceptionProgramCounter() == 0);
  REQUIRE(core.generalRegister(5) == UINT32_C(0xaaaaaaaa));
  REQUIRE_FALSE(core.branchContinuation().active);
  REQUIRE(core.executionState() == IOPExecutionState::Running);
  REQUIRE(core.stopReason() == IOPStopReason::None);
  REQUIRE(
    core.exception().kind ==
    IOPException::AddressErrorLoadOrFetch);
  REQUIRE(core.exception().badVirtualAddress == 3);
  REQUIRE_FALSE(core.exception().inBranchDelaySlot);
  REQUIRE(core.retiredInstructions() == 0);
}

TEST_CASE("IOP JR misalignment faults when the target is fetched",
  "[iop][execution][branch]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setGeneralRegister(&core, 1, 3);
  loadInstruction(&bus, 0, registerInstruction(0x08, 1, 0, 0));
  loadInstruction(&bus, 4, 0);
  core.startExecution(0);

  core.stepInstruction();
  core.stepInstruction();
  REQUIRE(core.programCounter() == 3);
  REQUIRE_FALSE(core.branchContinuation().active);

  core.stepInstruction();

  REQUIRE(core.programCounter() == 0xbfc00180);
  REQUIRE(core.exceptionProgramCounter() == 3);
  REQUIRE(core.executionState() == IOPExecutionState::Running);
  REQUIRE(core.stopReason() == IOPStopReason::None);
  REQUIRE(
    core.exception().kind ==
    IOPException::AddressErrorLoadOrFetch);
  REQUIRE(core.exception().badVirtualAddress == 3);
  REQUIRE_FALSE(core.exception().inBranchDelaySlot);
  REQUIRE(core.retiredInstructions() == 2);
}

TEST_CASE("IOP rejects a control transfer in a branch delay slot",
  "[iop][execution][branch]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setGeneralRegister(
    &core,
    31,
    UINT32_C(0xaaaaaaaa));
  loadInstruction(&bus, 0, jumpInstruction(0x02, 3));
  loadInstruction(&bus, 4, jumpInstruction(0x03, 4));
  core.startExecution(0);

  core.stepInstruction();
  core.stepInstruction();

  REQUIRE(core.programCounter() == 0xbfc00180);
  REQUIRE(core.exceptionProgramCounter() == 0);
  REQUIRE(core.generalRegister(31) == UINT32_C(0xaaaaaaaa));
  REQUIRE_FALSE(core.branchContinuation().active);
  REQUIRE(core.branchContinuation().instructionAddress == 0);
  REQUIRE(core.branchContinuation().targetAddress == 0);
  REQUIRE(core.executionState() == IOPExecutionState::Running);
  REQUIRE(core.stopReason() == IOPStopReason::None);
  REQUIRE(core.exception().kind == IOPException::ReservedInstruction);
  REQUIRE(core.exception().inBranchDelaySlot);
  REQUIRE(core.retiredInstructions() == 1);
}

TEST_CASE("IOP exceptions in a delay slot preserve branch provenance",
  "[iop][execution][branch]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setGeneralRegister(
    &core,
    1,
    UINT32_C(0x7fffffff));
  IOPCoreTestAccess::setGeneralRegister(&core, 2, 1);
  IOPCoreTestAccess::setGeneralRegister(
    &core,
    3,
    UINT32_C(0xaaaaaaaa));
  loadInstruction(&bus, 0, jumpInstruction(0x02, 3));
  loadInstruction(&bus, 4, registerInstruction(0x20, 1, 2, 3));
  core.startExecution(0);

  core.stepInstruction();
  core.stepInstruction();

  REQUIRE(core.programCounter() == 0xbfc00180);
  REQUIRE(core.exceptionProgramCounter() == 0);
  REQUIRE(core.generalRegister(3) == UINT32_C(0xaaaaaaaa));
  REQUIRE_FALSE(core.branchContinuation().active);
  REQUIRE(core.branchContinuation().instructionAddress == 0);
  REQUIRE(core.exception().kind == IOPException::ArithmeticOverflow);
  REQUIRE(core.exception().inBranchDelaySlot);
  REQUIRE(core.retiredInstructions() == 1);
}

TEST_CASE("IOP aligned loads extend little endian data after one instruction",
  "[iop][execution][load]")
{
  struct LoadCase
  {
    std::uint8_t opcode;
    std::uint16_t offset;
    IOPWord expected;
  };
  const LoadCase cases[] = {
    {0x20, 0, 0},
    {0x20, 1, 0x7f},
    {0x20, 2, UINT32_C(0xffffff80)},
    {0x20, 3, UINT32_C(0xffffffff)},
    {0x24, 0, 0},
    {0x24, 1, 0x7f},
    {0x24, 2, 0x80},
    {0x24, 3, 0xff},
    {0x21, 0, 0x7f00},
    {0x21, 2, UINT32_C(0xffffff80)},
    {0x25, 0, 0x7f00},
    {0x25, 2, 0xff80},
    {0x23, 0, UINT32_C(0xff807f00)}
  };
  for (const auto &test : cases)
  {
    INFO("opcode " << unsigned(test.opcode) << " offset " << test.offset);
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    const std::uint8_t data[] = {0, 0x7f, 0x80, 0xff};
    REQUIRE(bus.loadPhysical(0x100, data, sizeof(data)) ==
      IOPBusStatus::Completed);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, 0x100);
    IOPCoreTestAccess::setGeneralRegister(&core, 2, 0x1234);
    loadInstruction(&bus, 0,
      immediateInstruction(test.opcode, 1, 2, test.offset));
    loadInstruction(&bus, 4, registerInstruction(0x21, 2, 0, 3));
    loadInstruction(&bus, 8, registerInstruction(0x21, 2, 0, 4));
    core.startExecution(0);

    core.stepInstruction();
    REQUIRE(core.generalRegister(2) == 0x1234);
    REQUIRE(core.delayedResultContinuation().source ==
      IOPDelayedResultSource::Load);
    REQUIRE(core.delayedResultContinuation().destination == 2);
    REQUIRE(core.delayedResultContinuation().value == test.expected);
    core.stepInstruction();
    REQUIRE(core.generalRegister(3) == 0x1234);
    REQUIRE(core.generalRegister(2) == test.expected);
    REQUIRE(core.delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
    REQUIRE(core.delayedResultContinuation().destination == 0);
    REQUIRE(core.delayedResultContinuation().value == 0);
    core.stepInstruction();
    REQUIRE(core.generalRegister(4) == test.expected);
    REQUIRE(core.programCounter() == 12);
    REQUIRE(core.retiredInstructions() == 3);
    REQUIRE(core.elapsedCycles() == 3);
    REQUIRE(core.exception().kind == IOPException::None);
  }
}

TEST_CASE("IOP loads sign extend offsets and wrap effective addresses",
  "[iop][execution][load]")
{
  struct AddressCase
  {
    IOPAddress base;
    std::uint16_t offset;
    IOPAddress physical;
  };
  const AddressCase cases[] = {
    {0x104, 0xfffc, 0x100},
    {UINT32_C(0xfffffff0), 0x110, 0x100},
    {0x807ffc, 0x8000, 0x7ffffc},
    {0x80000104, 0xfffc, 0x100},
    {0xa0200104, 0xfffc, 0x100},
    {0x9f800004, 0xfffc, 0x1f800000}
  };
  for (const auto &test : cases)
  {
    INFO("base " << test.base);
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    REQUIRE(bus.write32(test.physical, 0x89abcdef).status ==
      IOPBusStatus::Completed);
    loadInstruction(&bus, 0,
      immediateInstruction(0x23, 1, 1, test.offset));
    loadInstruction(&bus, 4, 0);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, test.base);
    core.startExecution(0);
    core.stepInstruction();
    REQUIRE(core.generalRegister(1) == test.base);
    core.stepInstruction();
    REQUIRE(core.generalRegister(1) == 0x89abcdef);
  }
}

TEST_CASE("IOP consecutive loads retain separate delayed destinations",
  "[iop][execution][load]")
{
  for (const std::uint8_t destination : {std::uint8_t(2), std::uint8_t(3)})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    REQUIRE(bus.write32(0x100, 0x11111111).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write32(0x104, 0x22222222).status ==
      IOPBusStatus::Completed);
    loadInstruction(&bus, 0, immediateInstruction(0x23, 0, 2, 0x100));
    loadInstruction(&bus, 4,
      immediateInstruction(0x23, 0, destination, 0x104));
    loadInstruction(&bus, 8, registerInstruction(0x21, 2, 0, 4));
    core.startExecution(0);
    core.stepInstruction();
    core.stepInstruction();
    REQUIRE(core.generalRegister(2) == 0x11111111);
    REQUIRE(core.delayedResultContinuation().destination == destination);
    core.stepInstruction();
    REQUIRE(core.generalRegister(4) == 0x11111111);
    REQUIRE(core.generalRegister(destination) == 0x22222222);
  }
}

TEST_CASE("IOP younger destination writes win over older delayed loads",
  "[iop][execution][load]")
{
  for (const IOPWord instruction : {
    immediateInstruction(0x09, 2, 2, 1),
    jumpInstruction(0x03, 3)})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    const std::uint8_t destination =
      instruction == jumpInstruction(0x03, 3) ? 31 : 2;
    IOPCoreTestAccess::setGeneralRegister(&core, destination, 0x40);
    REQUIRE(bus.write32(0x100, 0x12345678).status ==
      IOPBusStatus::Completed);
    loadInstruction(&bus, 0,
      immediateInstruction(0x23, 0, destination, 0x100));
    loadInstruction(&bus, 4, instruction);
    core.startExecution(0);
    core.stepInstruction();
    core.stepInstruction();
    REQUIRE(core.generalRegister(destination) ==
      (destination == 31 ? 12 : 0x41));
    REQUIRE(core.delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
  }
}

TEST_CASE("IOP load faults suppress the result and preserve original address",
  "[iop][execution][load]")
{
  struct FaultCase
  {
    std::uint8_t opcode;
    IOPAddress address;
    IOPException exception;
    bool userMode;
  };
  const FaultCase cases[] = {
    {0x21, 0x101, IOPException::AddressErrorLoadOrFetch, false},
    {0x25, 0x101, IOPException::AddressErrorLoadOrFetch, false},
    {0x23, 0x102, IOPException::AddressErrorLoadOrFetch, false},
    {0x23, 0x103, IOPException::AddressErrorLoadOrFetch, false},
    {0x20, 0x800000, IOPException::DataBusError, false},
    {0x24, 0x800000, IOPException::DataBusError, false},
    {0x21, 0x800000, IOPException::DataBusError, false},
    {0x25, 0x800000, IOPException::DataBusError, false},
    {0x23, 0x800000, IOPException::DataBusError, false},
    {0x23, 0x80000100, IOPException::AddressErrorLoadOrFetch, true}
  };
  for (const auto &test : cases)
  {
    for (const std::uint8_t destination : {std::uint8_t(0), std::uint8_t(2)})
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      IOPCoreTestAccess::setGeneralRegister(&core, 1, test.address);
      IOPCoreTestAccess::setGeneralRegister(&core, 2, 0x1234);
      IOPCoreTestAccess::setUserMode(&core, test.userMode);
      loadInstruction(&bus, 0,
        immediateInstruction(test.opcode, 1, destination, 0));
      core.startExecution(0);
      core.stepInstruction();
      REQUIRE(core.generalRegister(2) == 0x1234);
      REQUIRE(core.generalRegister(0) == 0);
      REQUIRE(core.delayedResultContinuation().source ==
        IOPDelayedResultSource::None);
      REQUIRE(core.exception().kind == test.exception);
      REQUIRE(core.exception().badVirtualAddress == test.address);
      REQUIRE(core.stopReason() == IOPStopReason::None);
      REQUIRE(core.executionState() == IOPExecutionState::Running);
      REQUIRE(core.programCounter() == 0xbfc00180);
      REQUIRE(core.exceptionProgramCounter() == 0);
      REQUIRE(core.retiredInstructions() == 0);
    }
  }
}

TEST_CASE("IOP older loads complete before younger execution or fetch faults",
  "[iop][execution][load]")
{
  for (const IOPWord follower : {
    immediateInstruction(0x23, 0, 2, 3),
    registerInstruction(0x20, 3, 4, 2),
    UINT32_C(0xffffffff)})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    REQUIRE(bus.write32(0x100, 0x12345678).status ==
      IOPBusStatus::Completed);
    IOPCoreTestAccess::setGeneralRegister(&core, 3, 0x7fffffff);
    IOPCoreTestAccess::setGeneralRegister(&core, 4, 1);
    loadInstruction(&bus, 0, immediateInstruction(0x23, 0, 2, 0x100));
    loadInstruction(&bus, 4, follower);
    core.startExecution(0);
    core.stepInstruction();
    core.stepInstruction();
    REQUIRE(core.generalRegister(2) == 0x12345678);
    REQUIRE(core.delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
    REQUIRE(core.exception().kind != IOPException::None);
    REQUIRE(core.retiredInstructions() == 1);
    REQUIRE(core.programCounter() == 0xbfc00180);
    REQUIRE(core.exceptionProgramCounter() == 4);
  }

  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  REQUIRE(bus.write32(0x100, 0xabcdef01).status == IOPBusStatus::Completed);
  loadInstruction(&bus, 0x7ffffc,
    immediateInstruction(0x23, 0, 2, 0x100));
  core.startExecution(0x7ffffc);
  core.stepInstruction();
  core.stepInstruction();
  REQUIRE(core.generalRegister(2) == 0xabcdef01);
  REQUIRE(core.exception().kind == IOPException::InstructionBusError);
  REQUIRE(core.programCounter() == 0xbfc00180);
  REQUIRE(core.exceptionProgramCounter() == 0x800000);
  REQUIRE(core.retiredInstructions() == 1);
}

TEST_CASE("IOP load delay composes with branch capture and target execution",
  "[iop][execution][load]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  REQUIRE(bus.write32(0x100, 1).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x104, 0xabcdef01).status == IOPBusStatus::Completed);
  loadInstruction(&bus, 0, immediateInstruction(0x23, 0, 2, 0x100));
  loadInstruction(&bus, 4, immediateInstruction(0x04, 2, 0, 2));
  loadInstruction(&bus, 8, immediateInstruction(0x23, 0, 3, 0x104));
  loadInstruction(&bus, 16, registerInstruction(0x21, 3, 0, 4));
  loadInstruction(&bus, 20, registerInstruction(0x21, 3, 0, 5));
  core.startExecution(0);
  core.stepInstruction();
  core.stepInstruction();
  REQUIRE(core.generalRegister(2) == 1);
  REQUIRE(core.branchContinuation().targetAddress == 16);
  core.stepInstruction();
  REQUIRE(core.programCounter() == 16);
  REQUIRE_FALSE(core.branchContinuation().active);
  REQUIRE(core.generalRegister(3) == 0);
  core.stepInstruction();
  REQUIRE(core.generalRegister(4) == 0);
  REQUIRE(core.generalRegister(3) == 0xabcdef01);
  core.stepInstruction();
  REQUIRE(core.generalRegister(5) == 0xabcdef01);
}

TEST_CASE("IOP faulting delay slot loads preserve branch provenance",
  "[iop][execution][load]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  loadInstruction(&bus, 0, jumpInstruction(0x02, 4));
  loadInstruction(&bus, 4, immediateInstruction(0x23, 0, 2, 3));
  core.startExecution(0);
  core.stepInstruction();
  core.stepInstruction();
  REQUIRE(core.exception().kind == IOPException::AddressErrorLoadOrFetch);
  REQUIRE(core.exception().badVirtualAddress == 3);
  REQUIRE(core.exception().inBranchDelaySlot);
  REQUIRE(core.branchContinuation().instructionAddress == 0);
  REQUIRE_FALSE(core.branchContinuation().active);
  REQUIRE(core.programCounter() == 0xbfc00180);
  REQUIRE(core.exceptionProgramCounter() == 0);
  REQUIRE(core.retiredInstructions() == 1);
}

TEST_CASE("IOP successful loads to register zero cannot change it",
  "[iop][execution][load]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  REQUIRE(bus.write32(0x100, 0xffffffff).status == IOPBusStatus::Completed);
  loadInstruction(&bus, 0, immediateInstruction(0x23, 0, 0, 0x100));
  loadInstruction(&bus, 4, immediateInstruction(0x09, 0, 1, 1));
  core.startExecution(0);
  core.stepInstruction();
  REQUIRE(core.generalRegister(0) == 0);
  core.stepInstruction();
  REQUIRE(core.generalRegister(0) == 0);
  REQUIRE(core.generalRegister(1) == 1);
  REQUIRE(core.retiredInstructions() == 2);
}

TEST_CASE("IOP delayed loads preserve the next load's old base",
  "[iop][execution][load]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  REQUIRE(bus.write32(0x100, 0x200).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x104, 0x12345678).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x204, 0xabcdef01).status == IOPBusStatus::Completed);
  IOPCoreTestAccess::setGeneralRegister(&core, 1, 0x100);
  loadInstruction(&bus, 0, immediateInstruction(0x23, 1, 1, 0));
  loadInstruction(&bus, 4, immediateInstruction(0x23, 1, 2, 4));
  loadInstruction(&bus, 8, 0);
  core.startExecution(0);
  core.stepInstruction();
  core.stepInstruction();
  REQUIRE(core.generalRegister(1) == 0x200);
  core.stepInstruction();
  REQUIRE(core.generalRegister(2) == 0x12345678);
}

TEST_CASE("IOP host resume preserves delayed loads and branch continuation",
  "[iop][execution][load]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  REQUIRE(bus.write32(0x100, 0xabcdef01).status == IOPBusStatus::Completed);
  loadInstruction(&bus, 0, immediateInstruction(0x23, 0, 2, 0x100));
  loadInstruction(&bus, 4, jumpInstruction(0x02, 4));
  loadInstruction(&bus, 8, immediateInstruction(0x23, 0, 3, 0x100));
  loadInstruction(&bus, 16, registerInstruction(0x21, 3, 0, 4));
  core.startExecution(0);
  core.stepInstruction();
  core.haltExecution();
  REQUIRE(core.generalRegister(2) == 0);
  REQUIRE(core.delayedResultContinuation().value == 0xabcdef01);
  core.resumeExecution();
  REQUIRE(core.elapsedCycles() == 1);
  core.stepInstruction();
  REQUIRE(core.generalRegister(2) == 0xabcdef01);
  core.haltExecution();
  core.resumeExecution();
  REQUIRE(core.branchContinuation().active);
  REQUIRE(core.branchContinuation().instructionAddress == 4);
  core.stepInstruction();
  REQUIRE(core.programCounter() == 16);
  core.haltExecution();
  core.resumeExecution();
  core.stepInstruction();
  REQUIRE(core.generalRegister(4) == 0);
  REQUIRE(core.generalRegister(3) == 0xabcdef01);
  REQUIRE(core.elapsedCycles() == 4);
  REQUIRE(core.retiredInstructions() == 4);
}

TEST_CASE("IOP resume requires a host halt and permits an entered exception",
  "[iop][execution][load]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  REQUIRE_THROWS_AS(core.resumeExecution(), std::logic_error);
  core.startExecution(0);
  REQUIRE_THROWS_AS(core.resumeExecution(), std::logic_error);
  loadInstruction(&bus, 0, 0xffffffff);
  core.stepInstruction();
  REQUIRE_THROWS_AS(core.resumeExecution(), std::logic_error);
  core.haltExecution();
  REQUIRE_NOTHROW(core.resumeExecution());
  REQUIRE(core.exception().kind == IOPException::ReservedInstruction);
  REQUIRE(core.programCounter() == 0xbfc00180);
  REQUIRE(core.retiredInstructions() == 0);
}

TEST_CASE("IOP reset and explicit restart discard delayed load state",
  "[iop][execution][load]")
{
  for (const bool reset : {false, true})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    REQUIRE(bus.write32(0x100, 0xabcdef01).status == IOPBusStatus::Completed);
    loadInstruction(&bus, 0, immediateInstruction(0x23, 0, 2, 0x100));
    loadInstruction(&bus, 4, 0);
    core.startExecution(0);
    core.stepInstruction();
    if (reset)
    {
      core.reset();
    }
    core.startExecution(4);
    core.stepInstruction();
    REQUIRE(core.generalRegister(2) == 0);
    REQUIRE(core.delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
    REQUIRE(core.delayedResultContinuation().destination == 0);
    REQUIRE(core.delayedResultContinuation().value == 0);
  }
}

TEST_CASE("IOP deferred semantics preserve a pending load for inspection",
  "[iop][execution][load]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setCOP0State(&core, {0, 0x20600000, 0, 0});
  REQUIRE(bus.write32(0x100, 0xabcdef01).status == IOPBusStatus::Completed);
  loadInstruction(&bus, 0, immediateInstruction(0x23, 0, 2, 0x100));
  loadInstruction(&bus, 4, 0x44000000);
  core.startExecution(0);
  core.stepInstruction();
  core.stepInstruction();
  REQUIRE(core.generalRegister(2) == 0);
  REQUIRE(core.delayedResultContinuation().source ==
    IOPDelayedResultSource::Load);
  REQUIRE(core.delayedResultContinuation().value == 0xabcdef01);
  REQUIRE(core.exception().kind == IOPException::None);
  REQUIRE(core.executionState() == IOPExecutionState::Halted);
  REQUIRE(core.retiredInstructions() == 1);
  REQUIRE(core.programCounter() == 4);
}

TEST_CASE("IOP aligned stores truncate to little endian byte lanes",
  "[iop][execution][store]")
{
  struct StoreCase
  {
    std::uint8_t opcode;
    std::uint16_t offset;
    IOPWord expected;
  };
  const StoreCase cases[] = {
    {0x28, 0, 0x443322ef},
    {0x28, 1, 0x4433ef11},
    {0x28, 2, 0x44ef2211},
    {0x28, 3, 0xef332211},
    {0x29, 0, 0x4433cdef},
    {0x29, 2, 0xcdef2211},
    {0x2b, 0, 0x89abcdef}
  };
  for (const auto &test : cases)
  {
    INFO("opcode " << unsigned(test.opcode) << " offset " << test.offset);
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    REQUIRE(bus.write32(0xfc, 0xaaaaaaaa).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(0x100, 0x44332211).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(0x104, 0xbbbbbbbb).status == IOPBusStatus::Completed);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, 0x100);
    IOPCoreTestAccess::setGeneralRegister(&core, 2, 0x89abcdef);
    loadInstruction(&bus, 0,
      immediateInstruction(test.opcode, 1, 2, test.offset));
    core.startExecution(0);
    core.stepInstruction();
    REQUIRE(bus.read32(0x100).value == test.expected);
    REQUIRE(bus.read32(0xfc).value == 0xaaaaaaaa);
    REQUIRE(bus.read32(0x104).value == 0xbbbbbbbb);
    REQUIRE(core.generalRegister(1) == 0x100);
    REQUIRE(core.generalRegister(2) == 0x89abcdef);
    REQUIRE(core.programCounter() == 4);
    REQUIRE(core.elapsedCycles() == 1);
    REQUIRE(core.retiredInstructions() == 1);
    REQUIRE(core.executionState() == IOPExecutionState::Running);
    REQUIRE(core.stopReason() == IOPStopReason::None);
    REQUIRE(core.exception().kind == IOPException::None);
  }
}

TEST_CASE("IOP stores sign extend offsets and wrap effective addresses",
  "[iop][execution][store]")
{
  struct AddressCase
  {
    IOPAddress base;
    std::uint16_t offset;
    IOPAddress physical;
  };
  const AddressCase cases[] = {
    {0x104, 0xfffc, 0x100},
    {0xfffffff0, 0x110, 0x100},
    {0x807ffc, 0x8000, 0x7ffffc},
    {0x80000104, 0xfffc, 0x100},
    {0xa0200104, 0xfffc, 0x100},
    {0x9f800400, 0xfffc, 0x1f8003fc}
  };
  for (const auto &test : cases)
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, test.base);
    loadInstruction(&bus, 0,
      immediateInstruction(0x2b, 1, 1, test.offset));
    core.startExecution(0);
    core.stepInstruction();
    REQUIRE(bus.read32(test.physical).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(test.physical).value == test.base);
    REQUIRE(core.generalRegister(1) == test.base);
    REQUIRE(core.retiredInstructions() == 1);
  }
}

TEST_CASE("IOP stores complete in order before cached and uncached reads",
  "[iop][execution][store]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setGeneralRegister(&core, 1, 0x80000100);
  IOPCoreTestAccess::setGeneralRegister(&core, 2, 0x11223344);
  IOPCoreTestAccess::setGeneralRegister(&core, 3, 0xabcdefaa);
  IOPCoreTestAccess::setGeneralRegister(&core, 4, 0xa0200100);
  loadInstruction(&bus, 0, immediateInstruction(0x2b, 1, 2, 0));
  loadInstruction(&bus, 4, immediateInstruction(0x28, 1, 3, 1));
  loadInstruction(&bus, 8, immediateInstruction(0x29, 1, 3, 2));
  loadInstruction(&bus, 12, immediateInstruction(0x23, 4, 5, 0));
  loadInstruction(&bus, 16, 0);
  loadInstruction(&bus, 20, immediateInstruction(0x23, 1, 6, 0));
  loadInstruction(&bus, 24, 0);
  core.startExecution(0);
  core.stepInstruction();
  REQUIRE(bus.read32(0x100).value == 0x11223344);
  core.stepInstruction();
  REQUIRE(bus.read32(0x100).value == 0x1122aa44);
  core.stepInstruction();
  REQUIRE(bus.read32(0x100).value == 0xefaaaa44);
  for (int index = 0; index < 4; ++index)
  {
    core.stepInstruction();
  }
  REQUIRE(core.generalRegister(5) == 0xefaaaa44);
  REQUIRE(core.generalRegister(6) == 0xefaaaa44);
  REQUIRE(core.retiredInstructions() == 7);
}

TEST_CASE("IOP stores capture old load delay base and data operands",
  "[iop][execution][store]")
{
  for (const std::uint8_t destination : {std::uint8_t(1), std::uint8_t(2)})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    REQUIRE(bus.write32(0x200, 0x300).status == IOPBusStatus::Completed);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, 0x100);
    IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xabcdef01);
    loadInstruction(&bus, 0,
      immediateInstruction(0x23, 0, destination, 0x200));
    loadInstruction(&bus, 4, immediateInstruction(0x2b, 1, 2, 0));
    core.startExecution(0);
    core.stepInstruction();
    core.stepInstruction();
    REQUIRE(bus.read32(0x100).value == 0xabcdef01);
    REQUIRE(bus.read32(0x300).value == 0);
    REQUIRE(core.generalRegister(destination) == 0x300);
    REQUIRE(core.delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
    REQUIRE(core.retiredInstructions() == 2);
  }
}

TEST_CASE("IOP stores from register zero still access memory",
  "[iop][execution][store]")
{
  for (const std::uint8_t opcode : {
    std::uint8_t(0x28), std::uint8_t(0x29), std::uint8_t(0x2b)})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    REQUIRE(bus.write32(0x100, 0xffffffff).status == IOPBusStatus::Completed);
    loadInstruction(&bus, 0, immediateInstruction(opcode, 0, 0, 0x100));
    core.startExecution(0);
    core.stepInstruction();
    const IOPWord expected = opcode == 0x28
      ? 0xffffff00 : opcode == 0x29 ? 0xffff0000 : 0;
    REQUIRE(bus.read32(0x100).value == expected);
    REQUIRE(core.generalRegister(0) == 0);
    REQUIRE(core.retiredInstructions() == 1);
  }
}

TEST_CASE("IOP store address faults preserve memory and complete older loads",
  "[iop][execution][store]")
{
  struct FaultCase
  {
    std::uint8_t opcode;
    IOPAddress address;
    bool userMode;
  };
  const FaultCase cases[] = {
    {0x29, 0x101, false},
    {0x2b, 0x101, false},
    {0x2b, 0x102, false},
    {0x2b, 0x103, false},
    {0x28, 0x80000100, true},
    {0x29, 0xa0000100, true},
    {0x2b, 0x80000102, true}
  };
  for (const auto &test : cases)
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    REQUIRE(bus.write32(0x100, 0x12345678).status == IOPBusStatus::Completed);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, test.address);
    IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xffffffff);
    IOPCoreTestAccess::setUserMode(&core, test.userMode);
    loadInstruction(&bus, 0, immediateInstruction(test.opcode, 1, 2, 0));
    core.startExecution(0);
    IOPCoreTestAccess::setDelayedResultContinuation(&core);
    core.stepInstruction();
    REQUIRE(bus.read32(0x100).value == 0x12345678);
    REQUIRE(core.generalRegister(1) == 0x12345678);
    REQUIRE(core.generalRegister(2) == 0xffffffff);
    REQUIRE(core.exception().kind == IOPException::AddressErrorStore);
    REQUIRE(core.exception().badVirtualAddress == test.address);
    REQUIRE(core.executionState() == IOPExecutionState::Running);
    REQUIRE(core.stopReason() == IOPStopReason::None);
    REQUIRE(core.programCounter() == 0xbfc00180);
    REQUIRE(core.exceptionProgramCounter() == 0);
    REQUIRE(core.retiredInstructions() == 0);
    REQUIRE(core.delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
  }
}

TEST_CASE("IOP stores in branch delay slots commit or preserve fault provenance",
  "[iop][execution][store]")
{
  for (const std::uint16_t offset : {
    std::uint16_t(0x100), std::uint16_t(0x102)})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xabcdef01);
    loadInstruction(&bus, 0, jumpInstruction(0x02, 4));
    loadInstruction(&bus, 4, immediateInstruction(0x2b, 0, 2, offset));
    core.startExecution(0);
    core.stepInstruction();
    core.stepInstruction();
    if (offset == 0x100)
    {
      REQUIRE(bus.read32(0x100).value == 0xabcdef01);
      REQUIRE(core.programCounter() == 16);
      REQUIRE_FALSE(core.branchContinuation().active);
      REQUIRE(core.retiredInstructions() == 2);
    }
    else
    {
      REQUIRE(bus.read32(0x100).value == 0);
      REQUIRE(core.programCounter() == 0xbfc00180);
      REQUIRE(core.exceptionProgramCounter() == 0);
      REQUIRE(core.exception().kind == IOPException::AddressErrorStore);
      REQUIRE(core.exception().inBranchDelaySlot);
      REQUIRE(core.branchContinuation().instructionAddress == 0);
      REQUIRE(core.retiredInstructions() == 1);
    }
  }
}

TEST_CASE("IOP store effects do not mutate memory before commit admission",
  "[iop][execution][store]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setGeneralRegister(&core, 1, 0x12345678);
  const auto effects = IOPCoreTestAccess::instructionEffects(
    core, immediateInstruction(0x2b, 0, 1, 0x100));
  REQUIRE(effects.store.operation == IOPStoreOperation::Word);
  REQUIRE(effects.store.virtualAddress == 0x100);
  REQUIRE(effects.store.value == 0x12345678);
  REQUIRE(bus.read32(0x100).value == 0);
  REQUIRE(core.elapsedCycles() == 0);
  REQUIRE(core.retiredInstructions() == 0);

  IOPInstructionEffects invalid = effects;
  invalid.completion = IOPInstructionCompletion::Retired;
  invalid.controlFlow.kind = IOPControlFlowEffect::Hold;
  REQUIRE_THROWS_AS(
    IOPCoreTestAccess::commitInstructionEffects(&core, 0, invalid),
    std::logic_error);
  REQUIRE(bus.read32(0x100).value == 0);

  IOPInstructionEffects fault = effects;
  fault.exception.kind = IOPException::AddressErrorStore;
  IOPCoreTestAccess::commitInstructionEffects(&core, 0, fault);
  REQUIRE(bus.read32(0x100).value == 0);
  REQUIRE(core.exception().kind == IOPException::AddressErrorStore);
}

TEST_CASE("IOP rejected physical stores stop with typed nonexception diagnostics",
  "[iop][execution][store]")
{
  struct WidthCase
  {
    std::uint8_t opcode;
    IOPStoreWidth width;
    IOPWord value;
  };
  const WidthCase widths[] = {
    {0x28, IOPStoreWidth::Byte, 0xef},
    {0x29, IOPStoreWidth::Halfword, 0xcdef},
    {0x2b, IOPStoreWidth::Word, 0x89abcdef}
  };
  for (const auto &test : widths)
  {
    for (const bool installRom : {false, true})
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      if (installRom)
      {
        REQUIRE(bus.installRom(BootROMImage::create(
          std::vector<std::uint8_t>(IOPBus::ROM_SIZE, 0x5a))));
      }
      IOPCoreTestAccess::setGeneralRegister(&core, 1, 0xbfc00000);
      IOPCoreTestAccess::setGeneralRegister(&core, 2, 0x89abcdef);
      loadInstruction(&bus, 0, immediateInstruction(test.opcode, 1, 2, 0));
      core.startExecution(0);
      IOPCoreTestAccess::setDelayedResultContinuation(&core);
      const IOPWord originalStatus = core.status();
      core.stepInstruction();

      const IOPStoreFailure &failure = core.storeFailure();
      REQUIRE(failure.width == test.width);
      REQUIRE(failure.value == test.value);
      REQUIRE(failure.access.virtualAddress == 0xbfc00000);
      REQUIRE(failure.access.physicalAddress == 0x1fc00000);
      REQUIRE(failure.access.cacheRoute == IOPCacheRoute::Uncached);
      REQUIRE(failure.access.outcome ==
        (installRom ? IOPAccessOutcome::ReadOnly : IOPAccessOutcome::Unmapped));
      REQUIRE(failure.access.exception == IOPException::None);
      REQUIRE(core.executionState() == IOPExecutionState::Halted);
      REQUIRE(core.stopReason() == IOPStopReason::StoreFailure);
      REQUIRE(core.programCounter() == 0);
      REQUIRE(core.retiredInstructions() == 0);
      REQUIRE(core.elapsedCycles() == 1);
      REQUIRE(core.exception().kind == IOPException::None);
      REQUIRE(core.badVirtualAddress() == 0);
      REQUIRE(core.status() == originalStatus);
      REQUIRE(core.cause() == 0);
      REQUIRE(core.exceptionProgramCounter() == 0);
      REQUIRE(core.generalRegister(1) == 0xbfc00000);
      REQUIRE(core.delayedResultContinuation().source ==
        IOPDelayedResultSource::Load);
      REQUIRE(core.delayedResultContinuation().value == 0x12345678);
      if (installRom)
      {
        REQUIRE(bus.read32(IOPBus::ROM_BASE).value == 0x5a5a5a5a);
      }
      REQUIRE_THROWS_AS(core.resumeExecution(), std::logic_error);
      core.haltExecution();
      REQUIRE_THROWS_AS(core.resumeExecution(), std::logic_error);
    }
  }
}

TEST_CASE("IOP unmapped store boundaries never wrap into writable storage",
  "[iop][execution][store]")
{
  for (const IOPAddress address : {
    UINT32_C(0xa0800000), UINT32_C(0x9f800400), UINT32_C(0xfffffffc)})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    REQUIRE(bus.write32(0x100, 0x12345678).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(0x1ffffc, 0x44332211).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(0x1f8003fc, 0x88776655).status == IOPBusStatus::Completed);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, address);
    loadInstruction(&bus, 0, immediateInstruction(0x2b, 1, 0, 0));
    core.startExecution(0);
    core.stepInstruction();
    REQUIRE(core.stopReason() == IOPStopReason::StoreFailure);
    REQUIRE(core.exception().kind == IOPException::None);
    REQUIRE(core.storeFailure().access.virtualAddress == address);
    REQUIRE(core.storeFailure().access.outcome == IOPAccessOutcome::Unmapped);
    REQUIRE(bus.read32(0x100).value == 0x12345678);
    REQUIRE(bus.read32(0x1ffffc).value == 0x44332211);
    REQUIRE(bus.read32(0x1f8003fc).value == 0x88776655);
  }
}

TEST_CASE("IOP rejected stores retain branch continuation for inspection",
  "[iop][execution][store]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCoreTestAccess::setGeneralRegister(&core, 1, 0xa0800000);
  loadInstruction(&bus, 0, jumpInstruction(0x02, 4));
  loadInstruction(&bus, 4, immediateInstruction(0x2b, 1, 0, 0));
  core.startExecution(0);
  core.stepInstruction();
  core.stepInstruction();
  REQUIRE(core.stopReason() == IOPStopReason::StoreFailure);
  REQUIRE(core.exception().kind == IOPException::None);
  REQUIRE(core.programCounter() == 4);
  REQUIRE(core.retiredInstructions() == 1);
  REQUIRE(core.branchContinuation().active);
  REQUIRE(core.branchContinuation().instructionAddress == 0);
  REQUIRE(core.branchContinuation().targetAddress == 16);
}

TEST_CASE("IOP reset and fresh starts clear rejected store diagnostics",
  "[iop][execution][store]")
{
  for (const int restart : {0, 1, 2})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, 0xa0800000);
    loadInstruction(&bus, 0, immediateInstruction(0x2b, 1, 0, 0));
    core.startExecution(0);
    core.stepInstruction();
    REQUIRE(core.storeFailure().width == IOPStoreWidth::Word);
    switch (restart)
    {
      case 0:
        core.reset();
        break;
      case 1:
        core.startExecution(4);
        break;
      case 2:
        core.prepareFreshExecution(4, 0x100, 0x200);
        break;
    }
    REQUIRE(core.storeFailure().width == IOPStoreWidth::None);
    REQUIRE(core.storeFailure().value == 0);
    REQUIRE(core.storeFailure().access.virtualAddress == 0);
    REQUIRE(core.storeFailure().access.physicalAddress == 0);
    REQUIRE(core.storeFailure().access.cacheRoute == IOPCacheRoute::None);
    REQUIRE(core.storeFailure().access.exception == IOPException::None);
  }
}

TEST_CASE("IOP store and subsequent fetch are ordered in the uncached model",
  "[iop][execution][store]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  const IOPWord replacement = immediateInstruction(0x09, 0, 2, 7);
  IOPCoreTestAccess::setGeneralRegister(&core, 1, replacement);
  loadInstruction(&bus, 0, immediateInstruction(0x2b, 0, 1, 4));
  loadInstruction(&bus, 4, 0xffffffff);
  core.startExecution(0xa0000000);
  core.stepInstruction();
  core.stepInstruction();
  REQUIRE(core.generalRegister(2) == 7);
  REQUIRE(core.exception().kind == IOPException::None);
  REQUIRE(core.retiredInstructions() == 2);
}

TEST_CASE("IOP counter overflow rejects store commit before memory mutation",
  "[iop][execution][store]")
{
  for (const bool cycleOverflow : {false, true})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, 0x12345678);
    loadInstruction(&bus, 0, immediateInstruction(0x2b, 0, 1, 0x100));
    core.startExecution(0);
    const auto cycles = cycleOverflow
      ? std::numeric_limits<IOPCycleCount>::max() : 0;
    const auto instructions = cycleOverflow
      ? 0 : std::numeric_limits<std::uint64_t>::max();
    IOPCoreTestAccess::setCounters(&core, cycles, instructions);
    REQUIRE_THROWS_AS(core.stepInstruction(), std::overflow_error);
    REQUIRE(bus.read32(0x100).value == 0);
    REQUIRE(core.programCounter() == 0);
    REQUIRE(core.elapsedCycles() == cycles);
    REQUIRE(core.retiredInstructions() == instructions);
  }
}

TEST_CASE("IOP merge loads select little endian byte lanes and delay writeback",
  "[iop][execution][merge]")
{
  const IOPWord left[] = {
    0x11bbccdd, 0x2211ccdd, 0x332211dd, 0x44332211
  };
  const IOPWord right[] = {
    0x44332211, 0xaa443322, 0xaabb4433, 0xaabbcc44
  };
  for (const std::uint8_t opcode : {std::uint8_t(0x22), std::uint8_t(0x26)})
  {
    for (std::uint16_t offset = 0; offset < 4; ++offset)
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      REQUIRE(bus.write32(0x100, 0x44332211).status == IOPBusStatus::Completed);
      IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xaabbccdd);
      loadInstruction(&bus, 0,
        immediateInstruction(opcode, 0, 2, 0x100 + offset));
      loadInstruction(&bus, 4, registerInstruction(0x21, 2, 0, 3));
      loadInstruction(&bus, 8, registerInstruction(0x21, 2, 0, 4));
      core.startExecution(0);
      core.stepInstruction();
      const IOPWord expected = opcode == 0x22 ? left[offset] : right[offset];
      REQUIRE(core.generalRegister(2) == 0xaabbccdd);
      REQUIRE(core.delayedResultContinuation().value == expected);
      core.stepInstruction();
      REQUIRE(core.generalRegister(3) == 0xaabbccdd);
      REQUIRE(core.generalRegister(2) == expected);
      core.stepInstruction();
      REQUIRE(core.generalRegister(4) == expected);
      REQUIRE(core.retiredInstructions() == 3);
    }
  }
}

TEST_CASE("IOP paired merge loads bypass in either order across word boundaries",
  "[iop][execution][merge]")
{
  const std::uint8_t data[] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};
  for (std::uint16_t offset = 0; offset < 4; ++offset)
  {
    for (const bool leftFirst : {false, true})
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      REQUIRE(bus.loadPhysical(0x100, data, sizeof(data)) == IOPBusStatus::Completed);
      IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xaabbccdd);
      const IOPWord left = immediateInstruction(0x22, 0, 2, 0x103 + offset);
      const IOPWord right = immediateInstruction(0x26, 0, 2, 0x100 + offset);
      loadInstruction(&bus, 0, leftFirst ? left : right);
      loadInstruction(&bus, 4, leftFirst ? right : left);
      loadInstruction(&bus, 8, 0);
      core.startExecution(0);
      core.stepInstruction();
      const IOPWord partial = core.delayedResultContinuation().value;
      core.stepInstruction();
      REQUIRE(core.generalRegister(2) == partial);
      IOPWord expected = 0;
      for (std::uint8_t byte = 0; byte < 4; ++byte)
      {
        expected |= static_cast<IOPWord>(data[offset + byte]) << (8 * byte);
      }
      REQUIRE(core.delayedResultContinuation().value == expected);
      core.stepInstruction();
      REQUIRE(core.generalRegister(2) == expected);
      REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
    }
  }
}

TEST_CASE("IOP merge load bypass accepts preceding scalar loads only for matching destinations",
  "[iop][execution][merge]")
{
  for (const std::uint8_t destination : {std::uint8_t(1), std::uint8_t(2)})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    REQUIRE(bus.write32(0x100, 0x44332211).status == IOPBusStatus::Completed);
    loadInstruction(&bus, 0, immediateInstruction(0x23, 0, destination, 0x100));
    loadInstruction(&bus, 4, immediateInstruction(0x22, 0, 2, 0x100));
    loadInstruction(&bus, 8, 0);
    IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xaabbccdd);
    core.startExecution(0);
    core.stepInstruction();
    core.stepInstruction();
    core.stepInstruction();
    REQUIRE(core.generalRegister(2) ==
      (destination == 2 ? 0x11332211 : 0x11bbccdd));
  }
}

TEST_CASE("IOP merge load destination bypass never forwards the address base",
  "[iop][execution][merge]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  REQUIRE(bus.write32(0x100, 0x44332211).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x200, 0x300).status == IOPBusStatus::Completed);
  IOPCoreTestAccess::setGeneralRegister(&core, 1, 0x100);
  loadInstruction(&bus, 0, immediateInstruction(0x23, 0, 1, 0x200));
  loadInstruction(&bus, 4, immediateInstruction(0x22, 1, 1, 0));
  loadInstruction(&bus, 8, 0);
  core.startExecution(0);
  core.stepInstruction();
  core.stepInstruction();
  REQUIRE(core.generalRegister(1) == 0x300);
  core.stepInstruction();
  REQUIRE(core.generalRegister(1) == 0x11000300);
}

TEST_CASE("IOP merge stores write exactly the selected little endian lanes",
  "[iop][execution][merge]")
{
  const IOPWord left[] = {
    0x443322aa, 0x4433aabb, 0x44aabbcc, 0xaabbccdd
  };
  const IOPWord right[] = {
    0xaabbccdd, 0xbbccdd11, 0xccdd2211, 0xdd332211
  };
  for (const std::uint8_t opcode : {std::uint8_t(0x2a), std::uint8_t(0x2e)})
  {
    for (std::uint16_t offset = 0; offset < 4; ++offset)
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      REQUIRE(bus.write32(0xfc, 0xdeadbeef).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(0x100, 0x44332211).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(0x104, 0x12345678).status == IOPBusStatus::Completed);
      IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xaabbccdd);
      loadInstruction(&bus, 0,
        immediateInstruction(opcode, 0, 2, 0x100 + offset));
      core.startExecution(0);
      core.stepInstruction();
      REQUIRE(bus.read32(0x100).value ==
        (opcode == 0x2a ? left[offset] : right[offset]));
      REQUIRE(bus.read32(0xfc).value == 0xdeadbeef);
      REQUIRE(bus.read32(0x104).value == 0x12345678);
      REQUIRE(core.retiredInstructions() == 1);
      REQUIRE(core.exception().kind == IOPException::None);
    }
  }
}

TEST_CASE("IOP paired merge stores preserve surrounding bytes in either order",
  "[iop][execution][merge]")
{
  for (std::uint16_t offset = 0; offset < 4; ++offset)
  {
    for (const bool leftFirst : {false, true})
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      const std::uint8_t sentinels[8] = {1, 2, 3, 4, 5, 6, 7, 8};
      REQUIRE(bus.loadPhysical(0x100, sentinels, sizeof(sentinels)) == IOPBusStatus::Completed);
      IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xaabbccdd);
      const auto left = immediateInstruction(0x2a, 0, 2, 0x103 + offset);
      const auto right = immediateInstruction(0x2e, 0, 2, 0x100 + offset);
      loadInstruction(&bus, 0, leftFirst ? left : right);
      loadInstruction(&bus, 4, leftFirst ? right : left);
      core.startExecution(0);
      core.stepInstruction();
      core.stepInstruction();
      const std::uint8_t source[] = {0xdd, 0xcc, 0xbb, 0xaa};
      for (std::uint16_t byte = 0; byte < 8; ++byte)
      {
        const auto expected = byte >= offset && byte < offset + 4
          ? source[byte - offset] : sentinels[byte];
        REQUIRE(bus.read8(0x100 + byte).value == expected);
      }
      REQUIRE(core.retiredInstructions() == 2);
    }
  }
}

TEST_CASE(
  "IOP merge bypass handles scalar loads and overlapping merge sequences",
  "[iop][execution][merge]")
{
  struct LoadCase
  {
    std::uint8_t opcode;
    std::uint16_t address;
    IOPWord value;
  };
  const LoadCase loads[] = {{0x20, 0x104, 0xffffffef},
    {0x24, 0x104, 0xef},
    {0x21, 0x104, 0xffffcdef},
    {0x25, 0x104, 0xcdef},
    {0x23, 0x104, 0x89abcdef},
    {0x22, 0x105, 0xcdefccdd},
    {0x26, 0x106, 0xaabb89ab}};
  for (const auto &load : loads)
  {
    for (const std::uint8_t opcode : {std::uint8_t(0x22), std::uint8_t(0x26)})
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      REQUIRE(bus.write32(0x100, 0x44332211).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(0x104, 0x89abcdef).status == IOPBusStatus::Completed);
      IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xaabbccdd);
      loadInstruction(
        &bus, 0, immediateInstruction(load.opcode, 0, 2, load.address));
      loadInstruction(&bus,
        4,
        immediateInstruction(opcode, 0, 2, opcode == 0x22 ? 0x101 : 0x102));
      loadInstruction(&bus, 8, 0);
      core.startExecution(0);
      core.stepInstruction();
      REQUIRE(core.delayedResultContinuation().value == load.value);
      core.stepInstruction();
      REQUIRE(core.generalRegister(2) == load.value);
      const IOPWord expected = opcode == 0x22
                                 ? (load.value & 0xffff) | 0x22110000
                                 : (load.value & 0xffff0000) | 0x4433;
      core.stepInstruction();
      REQUIRE(core.generalRegister(2) == expected);
    }
  }
}

TEST_CASE("IOP merge accesses wrap signed offsets through physical aliases",
  "[iop][execution][merge]")
{
  struct AddressCase
  {
    IOPAddress base;
    std::uint16_t offset;
    IOPAddress physical;
  };
  const AddressCase addresses[] = {{0x104, 0xfffc, 0x100},
    {0xfffffff0, 0x110, 0x100},
    {0xa0200100, 0, 0x100},
    {0x807ffc, 0x8000, 0x7ffffc},
    {0x9f800400, 0xfffc, 0x1f8003fc}};
  const IOPWord leftLoads[] = {0x11bbccdd, 0x2211ccdd, 0x332211dd, 0x44332211};
  const IOPWord rightLoads[] = {0x44332211, 0xaa443322, 0xaabb4433, 0xaabbcc44};
  const IOPWord leftStores[] = {0x443322aa, 0x4433aabb, 0x44aabbcc, 0xaabbccdd};
  const IOPWord rightStores[] = {
    0xaabbccdd, 0xbbccdd11, 0xccdd2211, 0xdd332211};
  for (const auto &address : addresses)
  {
    for (std::uint16_t offset = 0; offset < 4; ++offset)
    {
      for (const std::uint8_t opcode : {std::uint8_t(0x22),
             std::uint8_t(0x26),
             std::uint8_t(0x2a),
             std::uint8_t(0x2e)})
      {
        IOPBus bus;
        IOPCore core;
        core.attachBus(&bus);
        REQUIRE(bus.write32(address.physical, 0x44332211).status ==
                IOPBusStatus::Completed);
        IOPCoreTestAccess::setGeneralRegister(&core, 1, address.base);
        IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xaabbccdd);
        loadInstruction(
          &bus, 0, immediateInstruction(opcode, 1, 2, address.offset + offset));
        loadInstruction(&bus, 4, 0);
        core.startExecution(0);
        core.stepInstruction();
        core.stepInstruction();
        if (opcode == 0x22 || opcode == 0x26)
        {
          REQUIRE(core.generalRegister(2) ==
                  (opcode == 0x22 ? leftLoads[offset] : rightLoads[offset]));
        }
        else
        {
          REQUIRE(bus.read32(address.physical).value ==
                  (opcode == 0x2a ? leftStores[offset] : rightStores[offset]));
        }
        REQUIRE(core.retiredInstructions() == 2);
        REQUIRE(core.exception().kind == IOPException::None);
      }
    }
  }
}

TEST_CASE(
  "IOP merge faults retain original addresses and precise branch ownership",
  "[iop][execution][merge]")
{
  for (const bool userMode : {false, true})
  {
    for (std::uint16_t offset = 0; offset < 4; ++offset)
    {
      for (const std::uint8_t opcode : {std::uint8_t(0x22),
             std::uint8_t(0x26),
             std::uint8_t(0x2a),
             std::uint8_t(0x2e)})
      {
        IOPBus bus;
        IOPCore core;
        core.attachBus(&bus);
        const IOPAddress base = userMode ? 0x80000100 : 0xa0800000;
        IOPCoreTestAccess::setGeneralRegister(&core, 3, base);
        IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xaabbccdd);
        IOPCoreTestAccess::setUserMode(&core, userMode);
        loadInstruction(&bus, 0, jumpInstruction(0x02, 4));
        loadInstruction(&bus, 4, immediateInstruction(opcode, 3, 2, offset));
        core.startExecution(0);
        core.stepInstruction();
        IOPCoreTestAccess::setDelayedResultContinuation(&core);
        core.stepInstruction();
        REQUIRE(core.retiredInstructions() == 1);
        REQUIRE(core.branchContinuation().instructionAddress == 0);
        REQUIRE(core.generalRegister(2) == 0xaabbccdd);
        const bool store = opcode == 0x2a || opcode == 0x2e;
        if (!userMode && store)
        {
          REQUIRE(core.programCounter() == 4);
          REQUIRE(core.branchContinuation().active);
          const auto &failure = core.storeFailure();
          REQUIRE(core.stopReason() == IOPStopReason::StoreFailure);
          REQUIRE(core.exception().kind == IOPException::None);
          REQUIRE(core.generalRegister(1) == 0);
          REQUIRE(core.delayedResultContinuation().source ==
                  IOPDelayedResultSource::Load);
          REQUIRE(failure.width == IOPStoreWidth::Word);
          REQUIRE(
            failure.mergeByteEnable ==
            (opcode == 0x2a ? 0xf >> (3 - offset) : (0xf << offset) & 0xf));
          REQUIRE(failure.value == (opcode == 0x2a
                                       ? 0xaabbccdd >> (8 * (3 - offset))
                                       : IOPWord(0xaabbccdd) << (8 * offset)));
          REQUIRE(failure.access.virtualAddress == base + offset);
          REQUIRE(failure.access.physicalAddress == 0x800000 + offset);
          REQUIRE(failure.access.outcome == IOPAccessOutcome::Unmapped);
        }
        else
        {
          REQUIRE(core.programCounter() == 0xbfc00180);
          REQUIRE(core.exceptionProgramCounter() == 0);
          REQUIRE_FALSE(core.branchContinuation().active);
          REQUIRE(core.exception().kind ==
                  (userMode ? (store ? IOPException::AddressErrorStore
                                     : IOPException::AddressErrorLoadOrFetch)
                            : IOPException::DataBusError));
          REQUIRE(core.exception().badVirtualAddress == base + offset);
          REQUIRE(core.exception().inBranchDelaySlot);
          REQUIRE(core.generalRegister(1) == 0x12345678);
          REQUIRE(core.delayedResultContinuation().source ==
                  IOPDelayedResultSource::None);
          REQUIRE(core.storeFailure().width == IOPStoreWidth::None);
        }
      }
    }
  }
}

TEST_CASE(
  "IOP merge pairs keep an older successful partial access on a later fault",
  "[iop][execution][merge]")
{
  for (const bool store : {false, true})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    REQUIRE(
      bus.write32(0x7ffffc, 0x44332211).status == IOPBusStatus::Completed);
    IOPCoreTestAccess::setGeneralRegister(&core, 1, 0x7ffffe);
    IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xaabbccdd);
    loadInstruction(
      &bus, 0, immediateInstruction(store ? 0x2e : 0x26, 1, 2, 0));
    loadInstruction(
      &bus, 4, immediateInstruction(store ? 0x2a : 0x22, 1, 2, 3));
    core.startExecution(0);
    core.stepInstruction();
    core.stepInstruction();
    REQUIRE(core.retiredInstructions() == 1);
    if (store)
    {
      REQUIRE(bus.read32(0x7ffffc).value == 0xccdd2211);
      REQUIRE(core.stopReason() == IOPStopReason::StoreFailure);
      REQUIRE(core.storeFailure().access.virtualAddress == 0x800001);
    }
    else
    {
      REQUIRE(core.generalRegister(2) == 0xaabb4433);
      REQUIRE(core.exception().kind == IOPException::DataBusError);
      REQUIRE(core.exception().badVirtualAddress == 0x800001);
      REQUIRE(core.delayedResultContinuation().source ==
              IOPDelayedResultSource::None);
    }
  }
}

TEST_CASE("IOP merge stores use old delayed data and address operands",
  "[iop][execution][merge]")
{
  for (const std::uint8_t destination : {std::uint8_t(1), std::uint8_t(2)})
  {
    for (const std::uint8_t opcode : {std::uint8_t(0x2a), std::uint8_t(0x2e)})
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      REQUIRE(bus.write32(0x200, 0x300).status == IOPBusStatus::Completed);
      IOPCoreTestAccess::setGeneralRegister(&core, 1, 0x101);
      IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xaabbccdd);
      loadInstruction(
        &bus, 0, immediateInstruction(0x23, 0, destination, 0x200));
      loadInstruction(&bus, 4, immediateInstruction(opcode, 1, 2, 0));
      core.startExecution(0);
      core.stepInstruction();
      core.stepInstruction();
      REQUIRE(
        bus.read32(0x100).value == (opcode == 0x2a ? 0xaabb : 0xbbccdd00));
      REQUIRE(bus.read32(0x300).value == 0);
      REQUIRE(core.generalRegister(destination) == 0x300);
    }
  }
}

TEST_CASE("IOP merge operations protect register zero without skipping faults",
  "[iop][execution][merge]")
{
  for (const std::uint8_t opcode : {std::uint8_t(0x22),
         std::uint8_t(0x26),
         std::uint8_t(0x2a),
         std::uint8_t(0x2e)})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    REQUIRE(bus.write32(0x100, 0x44332211).status == IOPBusStatus::Completed);
    loadInstruction(&bus, 0, immediateInstruction(opcode, 0, 0, 0x101));
    loadInstruction(&bus, 4, 0);
    loadInstruction(&bus, 8, immediateInstruction(opcode, 1, 0, 0));
    core.startExecution(0);
    core.stepInstruction();
    core.stepInstruction();
    REQUIRE(core.generalRegister(0) == 0);
    REQUIRE(
      core.delayedResultContinuation().source == IOPDelayedResultSource::None);
    REQUIRE(bus.read32(0x100).value == (opcode == 0x2a    ? 0x44330000
                                         : opcode == 0x2e ? 0x11
                                                          : 0x44332211));
    IOPCoreTestAccess::setGeneralRegister(&core, 1, 0x800001);
    core.stepInstruction();
    const bool store = opcode == 0x2a || opcode == 0x2e;
    REQUIRE(core.executionState() ==
      (store ? IOPExecutionState::Halted : IOPExecutionState::Running));
    REQUIRE(core.programCounter() == (store ? 8 : 0xbfc00180));
    REQUIRE(core.exception().kind ==
      (store ? IOPException::None : IOPException::DataBusError));
    REQUIRE(core.retiredInstructions() == 2);
  }
}

TEST_CASE("IOP paused merge pairs retain bypass and branch state",
  "[iop][execution][merge]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  REQUIRE(bus.write32(0x100, 0x44332211).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x104, 0x88776655).status == IOPBusStatus::Completed);
  loadInstruction(&bus, 0, jumpInstruction(0x02, 4));
  loadInstruction(&bus, 4, immediateInstruction(0x22, 0, 2, 0x104));
  loadInstruction(&bus, 16, immediateInstruction(0x26, 0, 2, 0x101));
  loadInstruction(&bus, 20, 0);
  core.startExecution(0);
  core.stepInstruction();
  core.haltExecution();
  core.resumeExecution();
  core.stepInstruction();
  core.haltExecution();
  core.resumeExecution();
  core.stepInstruction();
  REQUIRE(core.delayedResultContinuation().value == 0x55443322);
  core.stepInstruction();
  REQUIRE(core.generalRegister(2) == 0x55443322);
  REQUIRE_FALSE(core.branchContinuation().active);
}

TEST_CASE(
  "IOP merge accesses stay within the last ROM word", "[iop][execution][merge]")
{
  const auto rom =
    BootROMImage::create(std::vector<std::uint8_t>(IOPBus::ROM_SIZE, 0x5a));
  const IOPWord left[] = {0x5abbccdd, 0x5a5accdd, 0x5a5a5add, 0x5a5a5a5a};
  const IOPWord right[] = {0x5a5a5a5a, 0xaa5a5a5a, 0xaabb5a5a, 0xaabbcc5a};
  for (const std::uint8_t opcode : {std::uint8_t(0x22),
         std::uint8_t(0x26),
         std::uint8_t(0x2a),
         std::uint8_t(0x2e)})
  {
    for (std::uint16_t offset = 0; offset < 4; ++offset)
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      REQUIRE(bus.installRom(rom));
      IOPCoreTestAccess::setGeneralRegister(&core, 1, 0xbffffffc);
      IOPCoreTestAccess::setGeneralRegister(&core, 2, 0xaabbccdd);
      loadInstruction(&bus, 0, immediateInstruction(opcode, 1, 2, offset));
      loadInstruction(&bus, 4, 0);
      core.startExecution(0);
      core.stepInstruction();
      if (opcode == 0x22 || opcode == 0x26)
      {
        core.stepInstruction();
        REQUIRE(core.generalRegister(2) ==
                (opcode == 0x22 ? left[offset] : right[offset]));
      }
      else
      {
        REQUIRE(core.stopReason() == IOPStopReason::StoreFailure);
        REQUIRE(
          core.storeFailure().access.outcome == IOPAccessOutcome::ReadOnly);
        REQUIRE(
          core.storeFailure().access.virtualAddress == 0xbffffffc + offset);
        REQUIRE(
          core.storeFailure().access.physicalAddress == 0x1ffffffc + offset);
        core.startExecution(4);
        REQUIRE(core.storeFailure().mergeByteEnable == 0);
      }
      REQUIRE(bus.read32(0x1ffffffc).value == 0x5a5a5a5a);
    }
  }
}
