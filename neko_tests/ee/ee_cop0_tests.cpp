#include <cstdint>
#include <string>

#include "catch.hpp"
#include "ee_core.hpp"
#include "ee_test_utils.hpp"
#include "ee_instruction.hpp"
#include "neko_system.hpp"
#include "save_state_mutation.hpp"

namespace
{
  constexpr std::uint32_t READ_ONLY_COP0_INSTRUCTIONS[] = {
    UINT32_C(0x4003b800),
    UINT32_C(0x4003c000),
    UINT32_C(0x4003c007),
    UINT32_C(0x4003c800),
    UINT32_C(0x4003c801),
    UINT32_C(0x4003c803)
  };

  std::uint32_t cop0TransferInstruction(
    std::uint8_t source,
    std::uint8_t target,
    EECOP0Register cop0Register)
  {
    return
      (UINT32_C(0x10) << 26) |
      (static_cast<std::uint32_t>(source) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      (static_cast<std::uint32_t>(cop0Register) << 11);
  }

  std::uint32_t cop0OperationInstruction(
    std::uint8_t function)
  {
    return
      (UINT32_C(0x10) << 26) |
      (UINT32_C(0x10) << 21) |
      function;
  }

  std::uint32_t immediateInstruction(
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

  std::uint32_t cacheInstruction(
    std::uint8_t source,
    std::uint8_t operation,
    std::uint16_t immediate)
  {
    return immediateInstruction(
      0x2f,
      source,
      operation,
      immediate);
  }

  std::uint32_t prefetchInstruction(
    std::uint8_t source,
    std::uint8_t hint,
    std::uint16_t immediate)
  {
    return immediateInstruction(
      0x33,
      source,
      hint,
      immediate);
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

TEST_CASE("EE EI DI decode canonical controls and reject nonzero fixed fields",
  "[ei-di]")
{
  for (const std::uint32_t raw : {0x42000038u, 0x42000039u})
  {
    const auto instruction = decodeEEInstruction(raw);
    REQUIRE(eeInstructionRouting(instruction.operation).category ==
      EEInstructionCategory::COP0);
    for (unsigned bit = 6; bit < 21; ++bit)
    {
      REQUIRE_THROWS_WITH(decodeEEInstruction(raw | (1u << bit)),
        "Reserved EE instruction encoding.");
    }
  }
  REQUIRE(decodeEEInstruction(0x42000038).operation !=
    decodeEEInstruction(0x42000039).operation);
}

TEST_CASE("EE EI DI preserves the following Status read before disabling",
  "[ei-di]")
{
  NekoSystem system;
  auto &core = system.eeCore();
  const auto status = EECOP0Status::COP0_USABLE |
    EECOP0Status::MASTER_INTERRUPT_ENABLE | EECOP0Status::INTERRUPT_ENABLE;
  core.setCOP0Register(EECOP0Register::Status, status);
  mapLowKusegForTest(&core);
  system.eeBus().write32(0, 0x42000039);
  system.eeBus().write32(4, cop0TransferInstruction(0, 1, EECOP0Register::Status));
  system.eeBus().write32(8, immediateInstruction(0x0d, 1, 1, 0x13));
  system.eeBus().write32(12, cop0TransferInstruction(4, 1, EECOP0Register::Status));
  core.startExecution(0);
  system.clockMasterCycle();
  REQUIRE(core.programCounter() == 4);
  REQUIRE(core.cop0Register(EECOP0Register::Status) == status);
  system.clockMasterCycle();
  REQUIRE(core.generalRegister(1).low == status);
  REQUIRE(core.cop0Register(EECOP0Register::Status) ==
    (status & ~EECOP0Status::MASTER_INTERRUPT_ENABLE));
  system.runMasterCycles(2);
  REQUIRE(core.cop0Register(EECOP0Register::Status) == (status | 0x13));
  REQUIRE(core.pendingException() == EEException::None);
}

TEST_CASE("EE EI DI gates effects by effective kernel or EDI without CU0 faults",
  "[ei-di]")
{
  for (const std::uint32_t mode : {0u, 8u, 16u, 24u})
  {
    for (const std::uint32_t permission : {0u, 2u, 4u, 0x20000u, 0x10000000u})
    {
      for (const bool enable : {false, true})
      {
        CAPTURE(mode);
        CAPTURE(permission);
        CAPTURE(enable);
        NekoSystem system;
        auto &core = system.eeCore();
        mapLowKusegForTest(&core);
        const auto initial = mode | permission | EECOP0Status::INTERRUPT_ENABLE |
          (enable ? 0u : EECOP0Status::MASTER_INTERRUPT_ENABLE);
        core.setCOP0Register(EECOP0Register::Status, initial);
        const std::uint32_t raw = enable ? 0x42000038 : 0x42000039;
        system.eeBus().write32(0, raw);
        system.eeBus().write32(4, raw);
        system.eeBus().write32(8, raw);
        core.startExecution(0);
        system.runMasterCycles(enable ? 1 : 2);
        REQUIRE(core.pendingException() == EEException::None);
        REQUIRE(core.stopReason() == EEStopReason::None);
        const bool permitted = mode == 0 || (permission & 0x20006) != 0;
        const auto expected = !permitted ? initial :
          enable ? initial | EECOP0Status::MASTER_INTERRUPT_ENABLE :
            initial & ~EECOP0Status::MASTER_INTERRUPT_ENABLE;
        REQUIRE(core.cop0Register(EECOP0Register::Status) == expected);
      }
    }
  }
}

TEST_CASE("EE EI DI applies delay slot disabling before the branch target",
  "[ei-di]")
{
  NekoSystem system;
  auto &core = system.eeCore();
  mapLowKusegForTest(&core);
  core.setCOP0Register(EECOP0Register::Status,
    EECOP0Status::COP0_USABLE | EECOP0Status::MASTER_INTERRUPT_ENABLE);
  system.eeBus().write32(0, 0x10000003);
  system.eeBus().write32(4, 0x42000039);
  system.eeBus().write32(16, cop0TransferInstruction(0, 1, EECOP0Register::Status));
  core.startExecution(0);
  system.runMasterCycles(2);
  REQUIRE(core.pendingException() == EEException::None);
  REQUIRE(core.generalRegister(1).low == EECOP0Status::COP0_USABLE);
  REQUIRE(core.cop0Register(EECOP0Register::Status) == EECOP0Status::COP0_USABLE);
}

TEST_CASE("EE EI DI orders deferred disabling after subsequent control writes",
  "[ei-di]")
{
  for (const std::uint32_t following : {
    0x42000038u, 0x42000039u, cop0TransferInstruction(4, 1, EECOP0Register::Status)})
  {
    CAPTURE(following);
    NekoSystem system;
    auto &core = system.eeCore();
    mapLowKusegForTest(&core);
    const auto status = EECOP0Status::COP0_USABLE |
      EECOP0Status::MASTER_INTERRUPT_ENABLE | EECOP0Status::INTERRUPT_ENABLE;
    core.setCOP0Register(EECOP0Register::Status, status);
    core.setGeneralRegister(1, {status, 0});
    system.eeBus().write32(0, 0x42000039);
    system.eeBus().write32(4, following);
    system.eeBus().write32(8, cop0TransferInstruction(0, 2, EECOP0Register::Status));
    core.startExecution(0);
    system.runMasterCycles(2);
    REQUIRE(core.cop0Register(EECOP0Register::Status) ==
      (status & ~EECOP0Status::MASTER_INTERRUPT_ENABLE));
    system.clockMasterCycle();
    REQUIRE(core.generalRegister(2).low ==
      (status & ~EECOP0Status::MASTER_INTERRUPT_ENABLE));
    REQUIRE(core.pendingException() == EEException::None);
  }
}

TEST_CASE("EE EI DI pending effects survive pause restore and reject malformed state",
  "[ei-di][save-state]")
{
  NekoSystem original;
  auto &core = original.eeCore();
  mapLowKusegForTest(&core);
  core.setCOP0Register(EECOP0Register::Status,
    EECOP0Status::COP0_USABLE | EECOP0Status::MASTER_INTERRUPT_ENABLE);
  original.eeBus().write32(0, 0x42000039);
  original.eeBus().write32(4, cop0TransferInstruction(0, 1, EECOP0Register::Status));
  core.startExecution(0);
  original.clockMasterCycle();
  REQUIRE(core.stopReason() == EEStopReason::None);
  core.haltExecution();
  const auto paused = original.saveState();
  SaveStateMutation mutation(paused);
  REQUIRE(mutation.readByte("system.eeCore.deferredDisableProgramOrder") == 1);
  NekoSystem restored;
  restored.loadState(paused);
  REQUIRE(restored.saveState() == paused);
  REQUIRE(restored.eeStateHash() == original.eeStateHash());
  {
    SaveStateMutation withoutPending(paused);
    withoutPending.writeScalar("system.eeCore.deferredDisableProgramOrder", 0);
    withoutPending.refreshChecksum();
    NekoSystem cleared;
    cleared.loadState(withoutPending.bytes());
    REQUIRE(cleared.eeStateHash() != original.eeStateHash());
  }
  for (const std::uint64_t invalid : {UINT64_C(2), UINT64_MAX})
  {
    mutation.writeScalar("system.eeCore.deferredDisableProgramOrder", invalid);
    mutation.refreshChecksum();
    REQUIRE_THROWS_AS(restored.loadState(mutation.bytes()), std::invalid_argument);
    REQUIRE(restored.saveState() == paused);
  }
  for (const auto *field : {
    "system.eeCore.lastInstruction", "system.eeCore.nextProgramOrder"})
  {
    SaveStateMutation inconsistent(paused);
    inconsistent.writeScalar(field,
      std::string(field) == "system.eeCore.nextProgramOrder" ? 3 : 0);
    inconsistent.refreshChecksum();
    REQUIRE_THROWS_AS(restored.loadState(inconsistent.bytes()), std::invalid_argument);
    REQUIRE(restored.saveState() == paused);
  }
  for (auto *system : {&original, &restored})
  {
    system->eeCore().startExecution(4);
    system->clockMasterCycle();
    REQUIRE(system->eeCore().generalRegister(1).low ==
      (EECOP0Status::COP0_USABLE | EECOP0Status::MASTER_INTERRUPT_ENABLE));
    REQUIRE(system->eeCore().cop0Register(EECOP0Register::Status) ==
      EECOP0Status::COP0_USABLE);
  }
  REQUIRE(restored.saveState() == original.saveState());
  restored.loadState(paused);
  restored.eeBus().write32(8, 0x42000038);
  restored.eeBus().write32(12, 0x42000038);
  restored.eeCore().startExecution(8);
  NekoSystem restarted;
  REQUIRE_NOTHROW(restarted.loadState(restored.saveState()));
  restored.runMasterCycles(2);
  REQUIRE((restored.eeCore().cop0Register(EECOP0Register::Status) &
    EECOP0Status::MASTER_INTERRUPT_ENABLE) != 0);
  restored.reset();
  NekoSystem fresh;
  REQUIRE(restored.saveState() == fresh.saveState());
}

TEST_CASE("EE EI DI exposes pending interrupts only at existing boundaries",
  "[ei-di]")
{
  for (const bool enable : {false, true})
  {
    CAPTURE(enable);
    NekoSystem system;
    auto &core = system.eeCore();
    mapLowKusegForTest(&core);
    const auto status = EECOP0Status::INTERRUPT_ENABLE |
      EECOP0Status::INTC_MASK |
      (enable ? 0u : EECOP0Status::MASTER_INTERRUPT_ENABLE);
    core.setCOP0Register(EECOP0Register::Status, status);
    system.eeBus().write32(0, enable ? 0x42000038 : 0x42000039);
    system.eeBus().write32(4, cop0TransferInstruction(0, 1, EECOP0Register::Status));
    core.startExecution(0);
    system.clockMasterCycle();
    system.interruptController().setSource(EEInterruptSource::VIF0, true);
    system.interruptController().toggleMask(
      EEInterruptSource::mask(EEInterruptSource::VIF0));
    system.clockMasterCycle();
    REQUIRE(core.pendingException() == EEException::Interrupt);
    REQUIRE(core.cop0Register(EECOP0Register::EPC) == 4);
    REQUIRE((core.cop0Register(EECOP0Register::Status) &
      EECOP0Status::MASTER_INTERRUPT_ENABLE) ==
      (enable ? EECOP0Status::MASTER_INTERRUPT_ENABLE : 0u));
  }
}

TEST_CASE("EE EI DI completes older effects before younger faults and host rejections",
  "[ei-di]")
{
  for (const std::uint32_t following : {0x0000000cu, 0x42000019u, 0x41000000u})
  {
    CAPTURE(following);
    NekoSystem system;
    auto &core = system.eeCore();
    mapLowKusegForTest(&core);
    core.setCOP0Register(EECOP0Register::Status,
      EECOP0Status::MASTER_INTERRUPT_ENABLE);
    system.eeBus().write32(0, 0x42000039);
    system.eeBus().write32(4, following);
    core.startExecution(0);
    system.runMasterCycles(2);
    REQUIRE((core.cop0Register(EECOP0Register::Status) &
      EECOP0Status::MASTER_INTERRUPT_ENABLE) == 0);
    REQUIRE(core.pendingException() ==
      (following == 0xc ? EEException::SystemCall :
       following == 0x42000019 ? EEException::ReservedInstruction : EEException::None));
    if (following == 0x41000000)
    {
      REQUIRE(core.stopReason() == EEStopReason::UnsupportedInstruction);
    }
    SaveStateMutation saved(system.saveState());
    REQUIRE(saved.readByte("system.eeCore.deferredDisableProgramOrder") == 0);
  }
}

TEST_CASE("EE EI DI evaluates deferred permission against the resulting Status",
  "[ei-di]")
{
  for (const bool initiallyKernel : {false, true})
  {
    NekoSystem system;
    auto &core = system.eeCore();
    mapLowKusegForTest(&core);
    const auto enabled = EECOP0Status::COP0_USABLE |
      EECOP0Status::MASTER_INTERRUPT_ENABLE;
    core.setCOP0Register(EECOP0Register::Status,
      enabled | (initiallyKernel ? 0u : EECOP0Status::USER_MODE));
    const auto followingStatus =
      enabled | (initiallyKernel ? EECOP0Status::USER_MODE : 0u);
    core.setGeneralRegister(1, {followingStatus, 0});
    system.eeBus().write32(0, 0x42000039);
    system.eeBus().write32(4,
      cop0TransferInstruction(4, 1, EECOP0Register::Status));
    core.startExecution(0);
    system.runMasterCycles(2);
    REQUIRE(core.pendingException() == EEException::None);
    REQUIRE(core.cop0Register(EECOP0Register::Status) ==
      (initiallyKernel ? followingStatus :
        followingStatus & ~EECOP0Status::MASTER_INTERRUPT_ENABLE));
  }
}

TEST_CASE("EE EI DI retains its pending effect while the following instruction stalls",
  "[ei-di]")
{
  NekoSystem system;
  auto &core = system.eeCore();
  mapLowKusegForTest(&core);
  core.setCOP0Register(EECOP0Register::Status,
    EECOP0Status::MASTER_INTERRUPT_ENABLE);
  core.setGeneralRegister(1, {84, 0});
  core.setGeneralRegister(2, {7, 0});
  system.eeBus().write32(0, 0x0022001b);
  system.eeBus().write32(4, 0x42000039);
  system.eeBus().write32(8, 0x00001812);
  core.startExecution(0);
  system.clockMasterCycle();
  REQUIRE(core.programCounter() == 8);
  system.clockMasterCycle();
  REQUIRE(core.programCounter() == 8);
  REQUIRE(core.cop0Register(EECOP0Register::Status) ==
    EECOP0Status::MASTER_INTERRUPT_ENABLE);
  system.runMasterCycles(40);
  REQUIRE(core.generalRegister(3).low == 12);
  REQUIRE(core.cop0Register(EECOP0Register::Status) == 0);
  REQUIRE(core.pendingException() == EEException::None);
}

TEST_CASE("EE EI DI does not escape an older COP1 access fault",
  "[ei-di]")
{
  NekoSystem system;
  auto &core = system.eeCore();
  mapLowKusegForTest(&core);
  const auto status = EECOP0Status::COP1_USABLE |
    EECOP0Status::MASTER_INTERRUPT_ENABLE;
  core.setCOP0Register(EECOP0Register::Status, status);
  core.setGeneralRegister(1, {0x00400000, 0});
  system.eeBus().write32(0, immediateInstruction(0x31, 1, 3, 0));
  system.eeBus().write32(4, 0x42000039);
  core.startExecution(0);
  system.clockMasterCycle();
  REQUIRE(core.programCounter() == 4);
  SaveStateMutation pending(system.saveState());
  REQUIRE(pending.readByte("system.eeCore.deferredDisableProgramOrder") == 0);
  system.runMasterCycles(2);
  REQUIRE(core.pendingException() == EEException::TLBRefillLoadOrFetch);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Status) ==
    (status | EECOP0Status::EXCEPTION_LEVEL));
  SaveStateMutation faulted(system.saveState());
  REQUIRE(faulted.readByte("system.eeCore.deferredDisableProgramOrder") == 0);
}

TEST_CASE("EE EI DI completes before a following COP1 access fault",
  "[ei-di]")
{
  NekoSystem system;
  auto &core = system.eeCore();
  mapLowKusegForTest(&core);
  core.setCOP0Register(EECOP0Register::Status,
    EECOP0Status::COP1_USABLE | EECOP0Status::MASTER_INTERRUPT_ENABLE);
  core.setGeneralRegister(1, {0x00400000, 0});
  system.eeBus().write32(0, 0x42000039);
  system.eeBus().write32(4, immediateInstruction(0x31, 1, 3, 0));
  core.startExecution(0);
  system.runMasterCycles(4);
  REQUIRE(core.pendingException() == EEException::TLBRefillLoadOrFetch);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 4);
  REQUIRE(core.cop0Register(EECOP0Register::Status) ==
    (EECOP0Status::COP1_USABLE | EECOP0Status::EXCEPTION_LEVEL));
}

TEST_CASE("EE EI DI tracing does not change control effects or canonical state",
  "[ei-di][trace]")
{
  NekoSystem traced;
  NekoSystem plain;
  for (auto *system : {&traced, &plain})
  {
    auto &core = system->eeCore();
    mapLowKusegForTest(&core);
    core.setCOP0Register(EECOP0Register::Status,
      EECOP0Status::COP0_USABLE | EECOP0Status::MASTER_INTERRUPT_ENABLE);
    system->eeBus().write32(0, 0x42000039);
    system->eeBus().write32(4,
      cop0TransferInstruction(0, 1, EECOP0Register::Status));
    system->eeBus().write32(8, 0x42000038);
    system->eeBus().write32(12,
      cop0TransferInstruction(0, 2, EECOP0Register::Status));
    core.startExecution(0);
  }
  traced.startTrace();
  traced.runMasterCycles(4);
  plain.runMasterCycles(4);
  REQUIRE(traced.eeCore().generalRegister(1).low ==
    (EECOP0Status::COP0_USABLE | EECOP0Status::MASTER_INTERRUPT_ENABLE));
  REQUIRE(traced.eeCore().generalRegister(2).low ==
    (EECOP0Status::COP0_USABLE | EECOP0Status::MASTER_INTERRUPT_ENABLE));
  REQUIRE(traced.eeCore().pendingException() == EEException::None);
  REQUIRE_FALSE(traced.trace().empty());
  REQUIRE(plain.trace().empty());
  REQUIRE(traced.eeStateHash() == plain.eeStateHash());
  REQUIRE(traced.saveState() == plain.saveState());
}

TEST_CASE("EE TLB management operations decode canonically")
{
  const struct
  {
    std::uint8_t function;
    EEOperation operation;
  } contracts[] = {
    {0x01, EEOperation::ReadIndexedTLBEntry},
    {0x02, EEOperation::WriteIndexedTLBEntry},
    {0x06, EEOperation::WriteRandomTLBEntry},
    {0x08, EEOperation::ProbeTLB}
  };

  for (const auto &contract : contracts)
  {
    REQUIRE(
      decodeEEInstruction(
        cop0OperationInstruction(contract.function)).operation ==
      contract.operation);
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(
        cop0OperationInstruction(contract.function) |
        (UINT32_C(1) << 6)),
      "Reserved EE instruction encoding.");
  }
}

TEST_CASE("EE TLB indexed writes and reads use canonical entries")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  const std::uint32_t pageMask = EECOP0PageMask::SIZE_16_KIB;
  const std::uint32_t entryHi = UINT32_C(0x12347e5a);
  const std::uint32_t entryLo0 = UINT32_C(0x801234ff);
  const std::uint32_t entryLo1 = UINT32_C(0x001abcff);
  core.setCOP0Register(EECOP0Register::Index, 5);
  core.setCOP0Register(EECOP0Register::PageMask, pageMask);
  core.setCOP0Register(EECOP0Register::EntryHi, entryHi);
  core.setCOP0Register(EECOP0Register::EntryLo0, entryLo0);
  core.setCOP0Register(EECOP0Register::EntryLo1, entryLo1);

  runInstruction(
    &system,
    cop0OperationInstruction(0x02));

  const EETLBEntry &entry = core.tlbEntry(5);
  REQUIRE(entry.pageMask == pageMask);
  REQUIRE(
    entry.entryHi ==
    (entryHi & EECOP0EntryHi::IMPLEMENTED_MASK & ~pageMask));
  REQUIRE(
    entry.evenPage.value ==
    (entryLo0 &
     EECOP0EntryLo::ENTRY_LO_0_IMPLEMENTED_MASK &
     ~(pageMask >> 7)));
  REQUIRE(
    entry.oddPage.value ==
    (entryLo1 &
     EECOP0EntryLo::ENTRY_LO_1_IMPLEMENTED_MASK &
     ~(pageMask >> 7)));
  REQUIRE(entry.global());
  REQUIRE(entry.evenPage.scratchpad());
  REQUIRE_FALSE(entry.oddPage.scratchpad());

  core.setCOP0Register(EECOP0Register::PageMask, 0);
  core.setCOP0Register(EECOP0Register::EntryHi, 0);
  core.setCOP0Register(EECOP0Register::EntryLo0, 0);
  core.setCOP0Register(EECOP0Register::EntryLo1, 0);
  runInstruction(
    &system,
    cop0OperationInstruction(0x01));

  REQUIRE(
    core.cop0Register(EECOP0Register::PageMask) ==
    entry.pageMask);
  REQUIRE(
    core.cop0Register(EECOP0Register::EntryHi) ==
    entry.entryHi);
  REQUIRE(
    core.cop0Register(EECOP0Register::EntryLo0) ==
    entry.evenPage.value);
  REQUIRE(
    core.cop0Register(EECOP0Register::EntryLo1) ==
    entry.oddPage.value);
}

TEST_CASE("EE decoded indexed TLB operations address every entry")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  for (std::size_t index = 0;
       index < EEMemorySystem::TLB_ENTRY_COUNT;
       ++index)
  {
    CAPTURE(index);
    const std::uint32_t entryHi =
      UINT32_C(0x10000000) |
      (static_cast<std::uint32_t>(index) << 13) |
      static_cast<std::uint32_t>(index);
    const std::uint32_t entryLo0 =
      (static_cast<std::uint32_t>(index + 1) << 6) |
      EECOP0EntryLo::GLOBAL |
      EECOP0EntryLo::VALID |
      EECOP0EntryLo::DIRTY;
    const std::uint32_t entryLo1 =
      (static_cast<std::uint32_t>(index + 49) << 6) |
      EECOP0EntryLo::GLOBAL |
      EECOP0EntryLo::VALID;

    core.setCOP0Register(
      EECOP0Register::Index,
      static_cast<std::uint32_t>(index));
    core.setCOP0Register(EECOP0Register::PageMask, 0);
    core.setCOP0Register(EECOP0Register::EntryHi, entryHi);
    core.setCOP0Register(EECOP0Register::EntryLo0, entryLo0);
    core.setCOP0Register(EECOP0Register::EntryLo1, entryLo1);
    runInstruction(
      &system,
      cop0OperationInstruction(0x02));

    core.setCOP0Register(EECOP0Register::PageMask, UINT32_MAX);
    core.setCOP0Register(EECOP0Register::EntryHi, UINT32_MAX);
    core.setCOP0Register(EECOP0Register::EntryLo0, UINT32_MAX);
    core.setCOP0Register(EECOP0Register::EntryLo1, UINT32_MAX);
    runInstruction(
      &system,
      cop0OperationInstruction(0x01));

    REQUIRE(core.cop0Register(EECOP0Register::PageMask) == 0);
    REQUIRE(
      core.cop0Register(EECOP0Register::EntryHi) ==
      (entryHi & EECOP0EntryHi::IMPLEMENTED_MASK));
    REQUIRE(
      core.cop0Register(EECOP0Register::EntryLo0) ==
      (entryLo0 &
       EECOP0EntryLo::ENTRY_LO_0_IMPLEMENTED_MASK));
    REQUIRE(
      core.cop0Register(EECOP0Register::EntryLo1) ==
      (entryLo1 &
       EECOP0EntryLo::ENTRY_LO_1_IMPLEMENTED_MASK));
  }
}

TEST_CASE("EE ordinary TLB entries select pages using their page mask")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Index, 3);
  core.setCOP0Register(
    EECOP0Register::PageMask,
    EECOP0PageMask::SIZE_16_KIB);
  core.setCOP0Register(
    EECOP0Register::EntryHi,
    UINT32_C(0x1234002a));
  core.setCOP0Register(
    EECOP0Register::EntryLo0,
    UINT32_C(0x00010007));
  core.setCOP0Register(
    EECOP0Register::EntryLo1,
    UINT32_C(0x00020007));
  runInstruction(
    &system,
    cop0OperationInstruction(0x02));

  const EETLBEntry &entry = core.tlbEntry(3);
  REQUIRE(entry.matches(UINT32_C(0x12340000), 0xff));
  REQUIRE(
    &entry.pageForAddress(UINT32_C(0x12340000)) ==
    &entry.evenPage);
  REQUIRE(
    &entry.pageForAddress(UINT32_C(0x12344000)) ==
    &entry.oddPage);
  REQUIRE_FALSE(
    entry.pageForAddress(UINT32_C(0x12340000)).
      scratchpad());
  REQUIRE_FALSE(
    entry.pageForAddress(UINT32_C(0x12344000)).
      scratchpad());

  const struct
  {
    std::uint32_t pageMask;
    std::uint32_t oddPageBit;
  } pageSizes[] = {
    {EECOP0PageMask::SIZE_4_KIB, UINT32_C(0x00001000)},
    {EECOP0PageMask::SIZE_16_KIB, UINT32_C(0x00004000)},
    {EECOP0PageMask::SIZE_64_KIB, UINT32_C(0x00010000)},
    {EECOP0PageMask::SIZE_256_KIB, UINT32_C(0x00040000)},
    {EECOP0PageMask::SIZE_1_MIB, UINT32_C(0x00100000)},
    {EECOP0PageMask::SIZE_4_MIB, UINT32_C(0x00400000)},
    {EECOP0PageMask::SIZE_16_MIB, UINT32_C(0x01000000)}
  };
  for (const auto &pageSize : pageSizes)
  {
    const EETLBEntry sizedEntry = {
      pageSize.pageMask,
      0,
      {1},
      {2}
    };
    REQUIRE(&sizedEntry.pageForAddress(0) == &sizedEntry.evenPage);
    REQUIRE(
      &sizedEntry.pageForAddress(pageSize.oddPageBit) ==
      &sizedEntry.oddPage);
  }
}

TEST_CASE("EE guest zero-mask S entry supports an upper scratchpad stack")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  const std::uint32_t program[] = {
    cop0TransferInstruction(0x04, 0, EECOP0Register::Index),
    cop0TransferInstruction(0x04, 0, EECOP0Register::PageMask),
    immediateInstruction(0x0f, 0, 8, 0x7000),
    cop0TransferInstruction(0x04, 8, EECOP0Register::EntryHi),
    immediateInstruction(0x0f, 0, 9, 0x8000),
    immediateInstruction(0x0d, 9, 9, 7),
    cop0TransferInstruction(0x04, 9, EECOP0Register::EntryLo0),
    immediateInstruction(0x09, 0, 10, 7),
    cop0TransferInstruction(0x04, 10, EECOP0Register::EntryLo1),
    cop0OperationInstruction(0x02),
    cop0TransferInstruction(0x04, 0, EECOP0Register::Status),
    immediateInstruction(0x0f, 0, 29, 0x7000),
    immediateInstruction(0x0d, 29, 29, 0x3ff0),
    immediateInstruction(0x0f, 0, 31, 0x1234),
    immediateInstruction(0x0d, 31, 31, 0x5678),
    immediateInstruction(0x19, 29, 29, 0xffa0),
    immediateInstruction(0x3f, 29, 31, 0x50),
    immediateInstruction(0x37, 29, 11, 0x50),
    immediateInstruction(0x0d, 8, 8, 0x2000),
    cop0TransferInstruction(0x04, 8, EECOP0Register::EntryHi),
    cop0OperationInstruction(0x08),
    cop0OperationInstruction(0x01)
  };
  std::uint32_t address = 0x1000;
  for (const std::uint32_t instruction : program)
  {
    system.eeBus().write32(address, instruction);
    address += 4;
  }
  system.eeBus().write32(address, UINT32_C(0x1000ffff));
  system.eeBus().write32(address + 4, 0);
  core.startExecution(UINT32_C(0xa0001000));
  system.runMasterCycles(64);

  REQUIRE(
    (core.cop0Register(EECOP0Register::Status) &
     (EECOP0Status::EXCEPTION_LEVEL | EECOP0Status::ERROR_LEVEL)) == 0);
  REQUIRE(core.generalRegister(29).low == UINT32_C(0x70003f90));
  REQUIRE(core.generalRegister(11).low == UINT32_C(0x12345678));
  std::uint64_t savedReturnAddress = 0;
  REQUIRE(system.eeMemorySystem().readScratchpad64(0x3fe0, &savedReturnAddress));
  REQUIRE(savedReturnAddress == UINT32_C(0x12345678));
  REQUIRE(core.cop0Register(EECOP0Register::Index) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::PageMask) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::EntryHi) == UINT32_C(0x70000000));
  REQUIRE(core.cop0Register(EECOP0Register::EntryLo0) == UINT32_C(0x80000007));
  REQUIRE(core.cop0Register(EECOP0Register::EntryLo1) == 7);
  REQUIRE(
    core.tlbEntry(0) ==
    EETLBEntry{0, UINT32_C(0x70000000), {UINT32_C(0x80000007)}, {7}});
  REQUIRE(core.programCounter() >= UINT32_C(0xa0001000) + address - 0x1000);
  REQUIRE(core.programCounter() <= UINT32_C(0xa0001000) + address - 0x1000 + 4);
}

TEST_CASE("EE TLB probes honor ASIDs and combined global state")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  const auto writeEntry =
    [&system, &core](
      std::uint32_t index,
      std::uint32_t asid,
      bool global)
    {
      core.setCOP0Register(EECOP0Register::Index, index);
      core.setCOP0Register(EECOP0Register::PageMask, 0);
      core.setCOP0Register(
        EECOP0Register::EntryHi,
        UINT32_C(0x23456000) | asid);
      core.setCOP0Register(
        EECOP0Register::EntryLo0,
        UINT32_C(0x00010006) |
        static_cast<std::uint32_t>(global));
      core.setCOP0Register(
        EECOP0Register::EntryLo1,
        UINT32_C(0x00020006) |
        static_cast<std::uint32_t>(global));
      runInstruction(
        &system,
        cop0OperationInstruction(0x02));
    };

  writeEntry(7, 0x2a, false);
  core.setCOP0Register(
    EECOP0Register::EntryHi,
    UINT32_C(0x2345602b));
  runInstruction(
    &system,
    cop0OperationInstruction(0x08));
  REQUIRE(
    core.cop0Register(EECOP0Register::Index) ==
    EECOP0Index::PROBE_FAILURE);

  writeEntry(7, 0x2a, true);
  writeEntry(2, 0x3c, true);
  core.setCOP0Register(
    EECOP0Register::EntryHi,
    UINT32_C(0x234560ff));
  runInstruction(
    &system,
    cop0OperationInstruction(0x08));
  REQUIRE(core.cop0Register(EECOP0Register::Index) == 2);
}

TEST_CASE("EE TLB writes use indexed and random selectors")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Index, 63);
  core.setCOP0Register(EECOP0Register::PageMask, 0);
  core.setCOP0Register(
    EECOP0Register::EntryHi,
    UINT32_C(0x11110001));
  core.setCOP0Register(
    EECOP0Register::EntryLo0,
    UINT32_C(0x00010006));
  core.setCOP0Register(
    EECOP0Register::EntryLo1,
    UINT32_C(0x00020006));
  runInstruction(
    &system,
    cop0OperationInstruction(0x02));
  REQUIRE(core.tlbEntry(47) == EETLBEntry{});

  core.setCOP0Register(EECOP0Register::Random, 11);
  runInstruction(
    &system,
    cop0OperationInstruction(0x06));
  REQUIRE(
    core.tlbEntry(11).entryHi ==
    UINT32_C(0x11110001));
  REQUIRE(core.cop0Register(EECOP0Register::Random) == 10);

  core.setCOP0Register(EECOP0Register::Index, 63);
  core.setCOP0Register(
    EECOP0Register::EntryHi,
    UINT32_C(0x22222002));
  runInstruction(
    &system,
    cop0OperationInstruction(0x01));
  REQUIRE(
    core.cop0Register(EECOP0Register::EntryHi) ==
    UINT32_C(0x22222002));
}

TEST_CASE("EE TLB operations require COP0 usability")
{
  for (const std::uint8_t function :
       {UINT8_C(0x01), UINT8_C(0x02),
        UINT8_C(0x06), UINT8_C(0x08)})
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setTLBEntry(
      4,
      {
        EECOP0PageMask::SIZE_4_KIB,
        UINT32_C(0x45678033),
        {UINT32_C(0x00030007)},
        {UINT32_C(0x00040007)}
      });
    core.setTLBEntry(
      EECOP0Random::RESET,
      {
        EECOP0PageMask::SIZE_16_KIB,
        UINT32_C(0x56780044),
        {UINT32_C(0x00050007)},
        {UINT32_C(0x00060007)}
      });
    core.setCOP0Register(EECOP0Register::Index, 4);
    core.setCOP0Register(
      EECOP0Register::PageMask,
      EECOP0PageMask::SIZE_64_KIB);
    core.setCOP0Register(
      EECOP0Register::EntryHi,
      UINT32_C(0x12340022));
    core.setCOP0Register(
      EECOP0Register::EntryLo0,
      UINT32_C(0x00010007));
    core.setCOP0Register(
      EECOP0Register::EntryLo1,
      UINT32_C(0x00020007));
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::USER_MODE);
    mapLowKusegForTest(&core);
    const EETLBEntry indexedBefore = core.tlbEntry(4);
    const EETLBEntry randomBefore =
      core.tlbEntry(EECOP0Random::RESET);
    const std::uint32_t indexBefore =
      core.cop0Register(EECOP0Register::Index);
    const std::uint32_t randomBeforeValue =
      core.cop0Register(EECOP0Register::Random);
    const std::uint32_t pageMaskBefore =
      core.cop0Register(EECOP0Register::PageMask);
    const std::uint32_t entryHiBefore =
      core.cop0Register(EECOP0Register::EntryHi);
    const std::uint32_t entryLo0Before =
      core.cop0Register(EECOP0Register::EntryLo0);
    const std::uint32_t entryLo1Before =
      core.cop0Register(EECOP0Register::EntryLo1);

    runInstruction(
      &system,
      cop0OperationInstruction(function));

    REQUIRE(
      core.pendingException() ==
      EEException::CoprocessorUnusable);
    REQUIRE(core.tlbEntry(4) == indexedBefore);
    REQUIRE(
      core.tlbEntry(EECOP0Random::RESET) ==
      randomBefore);
    REQUIRE(
      core.cop0Register(EECOP0Register::Index) ==
      indexBefore);
    REQUIRE(
      core.cop0Register(EECOP0Register::Random) ==
      randomBeforeValue);
    REQUIRE(
      core.cop0Register(EECOP0Register::PageMask) ==
      pageMaskBefore);
    REQUIRE(
      core.cop0Register(EECOP0Register::EntryHi) ==
      entryHiBefore);
    REQUIRE(
      core.cop0Register(EECOP0Register::EntryLo0) ==
      entryLo0Before);
    REQUIRE(
      core.cop0Register(EECOP0Register::EntryLo1) ==
      entryLo1Before);
  }
}

TEST_CASE("EE CACHE requires COP0 usability before dispatch")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(
    EECOP0Register::Status,
    EECOP0Status::USER_MODE |
      EECOP0Status::CACHE_HIT);
  mapLowKusegForTest(&core);
  core.setGeneralRegister(2, {UINT32_C(0x12340000), 0});

  runInstruction(
    &system,
    cacheInstruction(2, 0x00, 0x0040));

  REQUIRE(
    core.pendingException() ==
    EEException::CoprocessorUnusable);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Status) &
      EECOP0Status::CACHE_HIT) != 0);
}

TEST_CASE("EE BFH retires with unused addresses and preserves COP0 state")
{
  for (const bool user : {false, true})
  {
    for (const bool cacheHit : {false, true})
    {
      NekoSystem system;
      EECore &core = system.eeCore();
      core.setCOP0Register(
        EECOP0Register::Status,
        (user ? EECOP0Status::USER_MODE | EECOP0Status::COP0_USABLE :
                UINT32_C(0)) |
          (cacheHit ? EECOP0Status::CACHE_HIT : 0));
      mapLowKusegForTest(&core);
      core.setCOP0Register(
        EECOP0Register::TagLo, UINT32_C(0xabcdef01));
      core.setCOP0Register(
        EECOP0Register::TagHi, UINT32_C(0x12345678));
      for (const std::uint32_t address : {
             UINT32_C(0), UINT32_C(0x12345677),
             UINT32_C(0xb000f130)})
      {
        core.setGeneralRegister(2, {address, 0});
        runInstruction(&system, cacheInstruction(2, 0x0c, 0));

        REQUIRE(core.stopReason() == EEStopReason::None);
        REQUIRE_FALSE(core.exceptionPending());
        REQUIRE(core.programCounter() == 4);
        REQUIRE(core.lastInstruction().operation ==
                EEOperation::CacheMaintenance);
        REQUIRE(((core.cop0Register(EECOP0Register::Status) &
                  EECOP0Status::CACHE_HIT) != 0) == cacheHit);
        REQUIRE(core.cop0Register(EECOP0Register::TagLo) ==
                UINT32_C(0xabcdef01));
        REQUIRE(core.cop0Register(EECOP0Register::TagHi) ==
                UINT32_C(0x12345678));
        REQUIRE(core.generalRegister(2).low == address);
      }
    }
  }
}

TEST_CASE("EE BFH requires COP0 usability before ignoring its address")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(
    EECOP0Register::Status,
    EECOP0Status::USER_MODE | EECOP0Status::CACHE_HIT);
  mapLowKusegForTest(&core);
  core.setGeneralRegister(2, {UINT32_C(0xb000f130), 0});

  runInstruction(&system, cacheInstruction(2, 0x0c, 0));

  REQUIRE(core.pendingException() == EEException::CoprocessorUnusable);
  REQUIRE(core.stopReason() == EEStopReason::None);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
  REQUIRE((core.cop0Register(EECOP0Register::Status) &
           EECOP0Status::CACHE_HIT) != 0);
}

TEST_CASE("EE CACHE index operations preserve Status CH")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(
    EECOP0Register::Status,
    core.cop0Register(EECOP0Register::Status) |
      EECOP0Status::CACHE_HIT);
  core.setGeneralRegister(2, {UINT32_C(0x80001000), 0});

  runInstruction(
    &system,
    cacheInstruction(2, 0x00, 0x0040));

  REQUIRE(
    core.stopReason() ==
    EEStopReason::None);
  REQUIRE_FALSE(core.exceptionPending());
  REQUIRE(
    (core.cop0Register(EECOP0Register::Status) &
      EECOP0Status::CACHE_HIT) != 0);

  core.setCOP0Register(
    EECOP0Register::TagLo,
    UINT32_C(0x12345000) |
      EECOP0TagLo::DIRTY |
      EECOP0TagLo::VALID |
      EECOP0TagLo::LEAST_RECENTLY_FILLED |
      EECOP0TagLo::LOCK);
  core.setCOP0Register(
    EECOP0Register::TagHi,
    UINT32_C(0x89abcdef));
  runInstruction(
    &system,
    cacheInstruction(2, 0x12, 0x0001));

  REQUIRE_FALSE(core.exceptionPending());
  REQUIRE(
    (core.cop0Register(EECOP0Register::Status) &
      EECOP0Status::CACHE_HIT) != 0);
  REQUIRE(
    core.cop0Register(EECOP0Register::TagHi) ==
    UINT32_C(0x89abcdef));
  const std::size_t set =
    (UINT32_C(0x80001001) >> 6) &
    (EEMemorySystem::DATA_CACHE_SET_COUNT - 1);
  const EECacheLine &line =
    system.eeMemorySystem().dataCacheLine(set, 1);
  REQUIRE(line.physicalTag == UINT32_C(0x12345000));
  REQUIRE(line.valid);
  REQUIRE(line.dirty);
  REQUIRE(line.leastRecentlyFilled);
  REQUIRE(line.locked);
}

TEST_CASE("EE CACHE index store data accepts a full guest word")
{
  constexpr std::uint32_t value = UINT32_C(0xdeadbeef);
  constexpr std::uint32_t address = UINT32_C(0x8000012d);
  constexpr std::size_t set = 4;
  constexpr std::size_t way = 1;
  constexpr std::size_t wordOffset = 0x2c;

  SECTION("Instruction cache")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(2, {value, 0});
    runInstruction(
      &system,
      cop0TransferInstruction(
        0x04,
        2,
        EECOP0Register::TagLo));
    core.setGeneralRegister(3, {address, 0});
    runInstruction(
      &system,
      cacheInstruction(3, 0x05, 0));

    const EECacheLine &line =
      system.eeMemorySystem().instructionCacheLine(set, way);
    REQUIRE(line.data[wordOffset] == 0xef);
    REQUIRE(line.data[wordOffset + 1] == 0xbe);
    REQUIRE(line.data[wordOffset + 2] == 0xad);
    REQUIRE(line.data[wordOffset + 3] == 0xde);
  }

  SECTION("Data cache")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setGeneralRegister(2, {value, 0});
    runInstruction(
      &system,
      cop0TransferInstruction(
        0x04,
        2,
        EECOP0Register::TagLo));
    core.setGeneralRegister(3, {address, 0});
    runInstruction(
      &system,
      cacheInstruction(3, 0x13, 0));

    const EECacheLine &line =
      system.eeMemorySystem().dataCacheLine(set, way);
    REQUIRE(line.data[wordOffset] == 0xef);
    REQUIRE(line.data[wordOffset + 1] == 0xbe);
    REQUIRE(line.data[wordOffset + 2] == 0xad);
    REQUIRE(line.data[wordOffset + 3] == 0xde);
  }
}

TEST_CASE("EE CACHE instruction fill and hit invalidate update CH precisely")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  constexpr std::uint32_t target = UINT32_C(0x80001000);
  constexpr std::size_t set =
    (target >> 6) &
    (EEMemorySystem::INSTRUCTION_CACHE_SET_COUNT - 1);
  core.setCOP0Register(
    EECOP0Register::Status,
    core.cop0Register(EECOP0Register::Status) |
      EECOP0Status::CACHE_HIT);
  core.setCOP0Register(
    EECOP0Register::TagHi,
    UINT32_C(0x12345678));
  core.setGeneralRegister(2, {target, 0});
  system.eeBus().write32(
    UINT32_C(0x1000),
    UINT32_C(0x24020007));

  runInstruction(
    &system,
    cacheInstruction(2, 0x0e, 0));

  REQUIRE_FALSE(core.exceptionPending());
  REQUIRE(
    (core.cop0Register(EECOP0Register::Status) &
      EECOP0Status::CACHE_HIT) != 0);
  REQUIRE(
    core.cop0Register(EECOP0Register::TagHi) ==
    UINT32_C(0x12345678));
  REQUIRE(
    system.eeMemorySystem().instructionCacheLine(set, 0).valid);

  core.setCOP0Register(
    EECOP0Register::Status,
    core.cop0Register(EECOP0Register::Status) &
      ~EECOP0Status::CACHE_HIT);
  runInstruction(
    &system,
    cacheInstruction(2, 0x0b, 0));

  REQUIRE_FALSE(core.exceptionPending());
  REQUIRE(
    (core.cop0Register(EECOP0Register::Status) &
      EECOP0Status::CACHE_HIT) != 0);

  runInstruction(
    &system,
    cacheInstruction(2, 0x0b, 0));

  REQUIRE_FALSE(core.exceptionPending());
  REQUIRE(
    (core.cop0Register(EECOP0Register::Status) &
      EECOP0Status::CACHE_HIT) == 0);
}

TEST_CASE("EE CACHE data hit operations update CH precisely")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  constexpr std::uint32_t target = UINT32_C(0x80003210);
  constexpr std::size_t set =
    (target >> 6) &
    (EEMemorySystem::DATA_CACHE_SET_COUNT - 1);
  core.setGeneralRegister(2, {target, 0});
  core.setCOP0Register(
    EECOP0Register::TagLo,
    UINT32_C(0x3000) |
      EECOP0TagLo::VALID |
      EECOP0TagLo::DIRTY |
      EECOP0TagLo::LOCK);
  runInstruction(
    &system,
    cacheInstruction(2, 0x12, 0));

  core.setCOP0Register(
    EECOP0Register::Status,
    core.cop0Register(EECOP0Register::Status) &
      ~EECOP0Status::CACHE_HIT);
  runInstruction(
    &system,
    cacheInstruction(2, 0x1a, 0));

  REQUIRE_FALSE(core.exceptionPending());
  REQUIRE(
    (core.cop0Register(EECOP0Register::Status) &
      EECOP0Status::CACHE_HIT) != 0);
  REQUIRE_FALSE(
    system.eeMemorySystem().dataCacheLine(set, 0).valid);

  runInstruction(
    &system,
    cacheInstruction(2, 0x1a, 0));

  REQUIRE_FALSE(core.exceptionPending());
  REQUIRE(
    (core.cop0Register(EECOP0Register::Status) &
      EECOP0Status::CACHE_HIT) == 0);
}

TEST_CASE("EE CACHE addressed operations fault before CH update")
{
  for (const std::uint8_t operation : {
         0x0b, 0x0e, 0x18, 0x1a, 0x1c})
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::USER_MODE |
        EECOP0Status::COP0_USABLE |
        EECOP0Status::CACHE_HIT);
    mapLowKusegForTest(&core);
    core.setGeneralRegister(2, {UINT32_C(0x00010000), 0});

    runInstruction(
      &system,
      cacheInstruction(2, operation, 0));

    REQUIRE(
      core.pendingException() ==
      EEException::TLBRefillLoadOrFetch);
    REQUIRE(
      core.cop0Register(EECOP0Register::BadVAddr) ==
      UINT32_C(0x00010000));
    REQUIRE(
      (core.cop0Register(EECOP0Register::Status) &
        EECOP0Status::CACHE_HIT) != 0);
  }
}

TEST_CASE("EE CACHE instruction fill reports a load bus error")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  constexpr std::uint32_t target =
    EEMemoryMap::KSEG0_BASE +
    EEMemoryMap::MAIN_MEMORY_SIZE;
  core.setCOP0Register(
    EECOP0Register::Status,
    core.cop0Register(EECOP0Register::Status) |
      EECOP0Status::CACHE_HIT);
  core.setGeneralRegister(2, {target, 0});

  runInstruction(
    &system,
    cacheInstruction(2, 0x0e, 0));

  REQUIRE(
    core.pendingException() ==
    EEException::DataBusErrorLoad);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Status) &
      EECOP0Status::CACHE_HIT) != 0);
}

TEST_CASE("EE CACHE data hit writeback faults before CH or line mutation")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  constexpr std::uint32_t target =
    EEMemoryMap::KSEG0_BASE +
    EEMemoryMap::MAIN_MEMORY_SIZE;
  constexpr std::size_t set =
    (target >> 6) &
    (EEMemorySystem::DATA_CACHE_SET_COUNT - 1);
  core.setGeneralRegister(2, {target, 0});
  core.setCOP0Register(
    EECOP0Register::TagLo,
    EEMemoryMap::MAIN_MEMORY_SIZE |
      EECOP0TagLo::DIRTY |
      EECOP0TagLo::VALID |
      EECOP0TagLo::LEAST_RECENTLY_FILLED |
      EECOP0TagLo::LOCK);
  runInstruction(
    &system,
    cacheInstruction(2, 0x12, 0));

  core.setCOP0Register(
    EECOP0Register::Status,
    core.cop0Register(EECOP0Register::Status) &
      ~EECOP0Status::CACHE_HIT);
  runInstruction(
    &system,
    cacheInstruction(2, 0x18, 0));

  REQUIRE(
    core.pendingException() ==
    EEException::DataBusErrorLoad);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Status) &
      EECOP0Status::CACHE_HIT) == 0);
  const EECacheLine &preserved =
    system.eeMemorySystem().dataCacheLine(set, 0);
  REQUIRE(
    preserved.physicalTag ==
    EEMemoryMap::MAIN_MEMORY_SIZE);
  REQUIRE(preserved.valid);
  REQUIRE(preserved.dirty);
  REQUIRE(preserved.leastRecentlyFilled);
  REQUIRE(preserved.locked);
}

TEST_CASE("EE PREF suppresses faults and does not require COP0 usability")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  core.setCOP0Register(
    EECOP0Register::Status,
    (core.cop0Register(EECOP0Register::Status) &
       ~EECOP0Status::COP0_USABLE) |
      EECOP0Status::CACHE_HIT);
  core.setGeneralRegister(
    2,
    {UINT32_C(0x80000140), 0});
  system.eeBus().writeData8(0x100, 0x5a);

  runInstruction(
    &system,
    prefetchInstruction(2, 31, 0xffc0));

  REQUIRE_FALSE(core.exceptionPending());
  REQUIRE(core.stopReason() == EEStopReason::None);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Status) &
      EECOP0Status::CACHE_HIT) != 0);
  const EECacheLine &line =
    system.eeMemorySystem().dataCacheLine(4, 0);
  REQUIRE(line.valid);
  REQUIRE_FALSE(line.dirty);
  REQUIRE(line.data[0] == 0x5a);

  mapLowKusegForTest(&core);
  core.setCOP0Register(
    EECOP0Register::Status,
    EECOP0Status::USER_MODE |
      EECOP0Status::CACHE_HIT);
  core.setGeneralRegister(2, {UINT32_C(0x00010000), 0});
  runInstruction(
    &system,
    prefetchInstruction(2, 0, 0));

  REQUIRE_FALSE(core.exceptionPending());
  REQUIRE(core.stopReason() == EEStopReason::None);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Status) &
      EECOP0Status::CACHE_HIT) != 0);
}

TEST_CASE("EE CACHE makes self-modifying code visible explicitly")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  constexpr std::uint32_t target = UINT32_C(0x80001000);
  constexpr std::uint32_t original =
    (UINT32_C(0x09) << 26) |
    (UINT32_C(2) << 16) |
    UINT32_C(1);
  constexpr std::uint32_t replacement =
    (UINT32_C(0x09) << 26) |
    (UINT32_C(2) << 16) |
    UINT32_C(2);
  const auto executeTarget = [&system, &core]()
  {
    core.setGeneralRegister(2, {});
    core.startExecution(target);
    system.clockMasterCycle();
    REQUIRE_FALSE(core.exceptionPending());
    return core.generalRegister(2).low;
  };

  core.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::INSTRUCTION_CACHE_ENABLE |
      EECOP0Config::DATA_CACHE_ENABLE);
  system.eeBus().write32(0x1000, original);
  REQUIRE(executeTarget() == 1);

  core.setGeneralRegister(3, {target, 0});
  core.setGeneralRegister(4, {replacement, 0});
  runInstruction(
    &system,
    immediateInstruction(0x2b, 3, 4, 0));
  REQUIRE(system.eeBus().read32(0x1000) == original);
  REQUIRE(executeTarget() == 1);

  runInstruction(
    &system,
    cacheInstruction(3, 0x18, 0));
  REQUIRE(system.eeBus().read32(0x1000) == replacement);
  REQUIRE(executeTarget() == 1);

  core.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  REQUIRE(executeTarget() == 2);
  core.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::INSTRUCTION_CACHE_ENABLE |
      EECOP0Config::DATA_CACHE_ENABLE);
  REQUIRE(executeTarget() == 1);

  runInstruction(
    &system,
    cacheInstruction(3, 0x0b, 0));
  REQUIRE(executeTarget() == 2);
}

TEST_CASE("EE CACHE coordinates explicit DMA coherence workflows")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  constexpr std::uint32_t target = UINT32_C(0x80000200);
  constexpr std::uint32_t cachedValue = UINT32_C(0x11223344);
  constexpr std::uint32_t dmaValue = UINT32_C(0x55667788);
  core.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  core.setGeneralRegister(3, {target, 0});
  core.setGeneralRegister(4, {cachedValue, 0});

  runInstruction(
    &system,
    immediateInstruction(0x2b, 3, 4, 0));
  EEQuadword transferred = {};
  REQUIRE(system.eeBus().readDMAC128(0x200, &transferred));
  REQUIRE(transferred.low == 0);

  runInstruction(
    &system,
    cacheInstruction(3, 0x1c, 0));
  REQUIRE(system.eeBus().readDMAC128(0x200, &transferred));
  REQUIRE(
    static_cast<std::uint32_t>(transferred.low) ==
    cachedValue);

  REQUIRE(
    system.eeBus().writeDMAC128(
      0x200,
      {dmaValue, 0}));
  runInstruction(
    &system,
    immediateInstruction(0x23, 3, 5, 0));
  REQUIRE(core.generalRegister(5).low == cachedValue);

  core.setCOP0Register(
    EECOP0Register::Config,
    0);
  runInstruction(
    &system,
    immediateInstruction(0x23, 3, 6, 0));
  REQUIRE(core.generalRegister(6).low == dmaValue);
  core.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  runInstruction(
    &system,
    immediateInstruction(0x23, 3, 7, 0));
  REQUIRE(core.generalRegister(7).low == cachedValue);

  runInstruction(
    &system,
    cacheInstruction(3, 0x1a, 0));
  runInstruction(
    &system,
    immediateInstruction(0x23, 3, 8, 0));
  REQUIRE(core.generalRegister(8).low == dmaValue);
}

TEST_CASE("EE CACHE locking repeats deterministically across reset")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  constexpr std::size_t set = 4;
  const auto exercise = [&system, &core]()
  {
    core.setCOP0Register(
      EECOP0Register::Config,
      EECOP0Config::DATA_CACHE_ENABLE);
    core.setCOP0Register(
      EECOP0Register::TagLo,
      EECOP0TagLo::VALID |
        EECOP0TagLo::LOCK);
    core.setGeneralRegister(
      2,
      {UINT32_C(0x80000100), 0});
    runInstruction(
      &system,
      cacheInstruction(2, 0x12, 0));

    system.eeBus().write32(0x1100, UINT32_C(0x11111111));
    core.setGeneralRegister(
      3,
      {UINT32_C(0x80001100), 0});
    runInstruction(
      &system,
      immediateInstruction(0x23, 3, 4, 0));
    system.eeBus().write32(0x2100, UINT32_C(0x22222222));
    core.setGeneralRegister(
      3,
      {UINT32_C(0x80002100), 0});
    runInstruction(
      &system,
      immediateInstruction(0x23, 3, 5, 0));

    const EECacheLine &locked =
      system.eeMemorySystem().dataCacheLine(set, 0);
    const EECacheLine &replaceable =
      system.eeMemorySystem().dataCacheLine(set, 1);
    REQUIRE(locked.valid);
    REQUIRE(locked.locked);
    REQUIRE(locked.physicalTag == 0);
    REQUIRE(replaceable.valid);
    REQUIRE_FALSE(replaceable.locked);
    REQUIRE(
      replaceable.physicalTag ==
      UINT32_C(0x00002000));
    REQUIRE(core.generalRegister(4).low == UINT32_C(0x11111111));
    REQUIRE(core.generalRegister(5).low == UINT32_C(0x22222222));
  };

  exercise();
  system.reset();
  REQUIRE_FALSE(
    system.eeMemorySystem().dataCacheLine(set, 0).valid);
  REQUIRE_FALSE(
    system.eeMemorySystem().dataCacheLine(set, 1).valid);
  exercise();
}

TEST_CASE("EE COP0 faults preserve precise two-wide issue")
{
  const std::uint32_t tlbwi = cop0OperationInstruction(0x02);
  const std::uint32_t add =
    immediateInstruction(0x09, 0, 3, 7);

  SECTION("An older COP0 fault cancels the younger instruction")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::USER_MODE);
    mapLowKusegForTest(&core);
    core.setCOP0Register(EECOP0Register::Index, 4);
    core.setCOP0Register(
      EECOP0Register::EntryHi,
      UINT32_C(0x12345001));
    system.eeBus().write32(0, tlbwi);
    system.eeBus().write32(4, add);
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(
      core.pendingException() ==
      EEException::CoprocessorUnusable);
    REQUIRE(core.exceptionAddress() == 0);
    REQUIRE(core.generalRegister(3).low == 0);
    REQUIRE(core.tlbEntry(4) == EETLBEntry{});
    REQUIRE(
      core.cop0Register(EECOP0Register::Random) ==
      EECOP0Random::RESET);
    REQUIRE(core.acceptanceRecordsThisCycle().size() == 0);
  }

  SECTION("A younger COP0 fault preserves the older instruction")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::USER_MODE);
    mapLowKusegForTest(&core);
    core.setCOP0Register(EECOP0Register::Index, 4);
    core.setCOP0Register(
      EECOP0Register::EntryHi,
      UINT32_C(0x12345001));
    system.eeBus().write32(0, add);
    system.eeBus().write32(4, tlbwi);
    core.startExecution(0);

    system.clockMasterCycle();

    REQUIRE(core.generalRegister(3).low == 7);
    REQUIRE(
      core.pendingException() ==
      EEException::CoprocessorUnusable);
    REQUIRE(core.exceptionAddress() == 4);
    REQUIRE(core.tlbEntry(4) == EETLBEntry{});
    REQUIRE(
      core.cop0Register(EECOP0Register::Random) ==
      EECOP0Random::RESET - 1);
    REQUIRE(core.acceptanceRecordsThisCycle().size() == 1);
    REQUIRE(core.acceptanceRecordsThisCycle()[0].address == 0);
  }
}

TEST_CASE("EE TLB operations execute in branch delay slots")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Index, 6);
  core.setCOP0Register(
    EECOP0Register::EntryHi,
    UINT32_C(0x1234402a));
  core.setCOP0Register(
    EECOP0Register::EntryLo0,
    UINT32_C(0x00010007));
  core.setCOP0Register(
    EECOP0Register::EntryLo1,
    UINT32_C(0x00020007));
  system.eeBus().write32(
    0,
    immediateInstruction(0x04, 0, 0, 1));
  system.eeBus().write32(
    4,
    cop0OperationInstruction(0x02));
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(
    core.tlbEntry(6).entryHi ==
    UINT32_C(0x1234402a));
  REQUIRE(core.programCounter() == 8);
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 2);
  REQUIRE(
    core.acceptanceRecordsThisCycle()[1].mode ==
    EEAcceptanceMode::DelaySlot);
}

TEST_CASE("EE SYNC.P preserves completed COP0 visibility")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  const std::uint32_t value = UINT32_C(0x1234402a);
  core.setGeneralRegister(2, {value, UINT64_MAX});
  core.setGeneralRegister(3, {UINT64_MAX, UINT64_MAX});
  system.eeBus().write32(
    0,
    cop0TransferInstruction(
      0x04,
      2,
      EECOP0Register::EntryHi));
  system.eeBus().write32(4, UINT32_C(0x0000040f));
  system.eeBus().write32(
    8,
    cop0TransferInstruction(
      0x00,
      3,
      EECOP0Register::EntryHi));
  core.startExecution(0);

  system.runMasterCycles(3);

  REQUIRE(
    core.cop0Register(EECOP0Register::EntryHi) ==
    value);
  REQUIRE(core.generalRegister(3).low == value);
  REQUIRE(core.generalRegister(3).high == UINT64_MAX);
  REQUIRE(core.programCounter() == 12);
}

TEST_CASE("EE TLB execution resets and repeats deterministically")
{
  const auto prepareEntry =
    [](EECore *core)
    {
      core->setCOP0Register(EECOP0Register::Index, 9);
      core->setCOP0Register(
        EECOP0Register::PageMask,
        EECOP0PageMask::SIZE_16_KIB);
      core->setCOP0Register(
        EECOP0Register::EntryHi,
        UINT32_C(0x1234402a));
      core->setCOP0Register(
        EECOP0Register::EntryLo0,
        UINT32_C(0x00010007));
      core->setCOP0Register(
        EECOP0Register::EntryLo1,
        UINT32_C(0x00020007));
    };

  NekoSystem system;
  prepareEntry(&system.eeCore());
  runInstruction(
    &system,
    cop0OperationInstruction(0x02));
  const EETLBEntry expected = system.eeCore().tlbEntry(9);
  const std::uint64_t expectedHash =
    system.eeCore().stateHash();

  system.reset();
  REQUIRE(system.eeCore().tlbEntry(9) == EETLBEntry{});
  REQUIRE(
    system.eeCore().cop0Register(EECOP0Register::Random) ==
    EECOP0Random::RESET);

  prepareEntry(&system.eeCore());
  runInstruction(
    &system,
    cop0OperationInstruction(0x02));
  REQUIRE(system.eeCore().tlbEntry(9) == expected);
  REQUIRE(system.eeCore().stateHash() == expectedHash);
}

TEST_CASE("EE TLB entries reset deterministically and affect hashes")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  const std::uint64_t resetHash = core.stateHash();

  for (std::size_t index = 0;
       index < EEMemorySystem::TLB_ENTRY_COUNT;
       ++index)
  {
    REQUIRE(core.tlbEntry(index) == EETLBEntry{});
  }

  core.setTLBEntry(
    17,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x1234402a),
      {UINT32_C(0x00010007)},
      {UINT32_C(0x00020007)}
    });
  REQUIRE(core.stateHash() != resetHash);

  core.reset();
  REQUIRE(core.stateHash() == resetHash);
}

TEST_CASE("EE COP0 register transfers decode canonically")
{
  REQUIRE(
    decodeEEInstruction(
      cop0TransferInstruction(
        0x00,
        2,
        EECOP0Register::EntryHi)).operation ==
    EEOperation::MoveWordFromCOP0);
  REQUIRE(
    decodeEEInstruction(
      cop0TransferInstruction(
        0x04,
        2,
        EECOP0Register::EntryHi)).operation ==
    EEOperation::MoveWordToCOP0);

  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop0TransferInstruction(
        0x00,
        2,
        EECOP0Register::EntryHi) | 1),
    "Reserved EE instruction encoding.");
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      cop0TransferInstruction(
        0x04,
        2,
        EECOP0Register::EntryHi) | 1),
    "Reserved EE instruction encoding.");
}

TEST_CASE("EE MFC0 sign extends the COP0 low word")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(
    EECOP0Register::TagHi,
    UINT32_C(0x80000001));
  core.setGeneralRegister(
    2,
    {UINT64_C(0x1111111122222222),
     UINT64_C(0x3333333344444444)});

  runInstruction(
    &system,
    cop0TransferInstruction(
      0x00,
      2,
      EECOP0Register::TagHi));

  REQUIRE(
    core.generalRegister(2) ==
    EERegister128{
      UINT64_C(0xffffffff80000001),
      UINT64_C(0x3333333344444444)});
}

TEST_CASE("EE reserved COP0 reads require canonical read encodings",
  "[reserved-cop0]")
{
  for (const std::uint8_t index : {
         7, 17, 18, 19, 20, 21, 22, 23, 26, 27, 31})
  {
    CAPTURE(index);
    const auto reg = static_cast<EECOP0Register>(index);
    for (std::uint8_t target = 0; target < 32; ++target)
    {
      const auto instruction =
        decodeEEInstruction(cop0TransferInstruction(0, target, reg));
      REQUIRE(instruction.operation == EEOperation::MoveWordFromCOP0);
      REQUIRE(instruction.targetRegister == target);
      REQUIRE(instruction.destinationRegister == index);
    }
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(cop0TransferInstruction(4, 3, reg)),
      "Unsupported EE instruction encoding.");
    for (unsigned bit = 0; bit < 11; ++bit)
    {
      for (const std::uint8_t source : {0, 4})
      {
        REQUIRE_THROWS_WITH(
          decodeEEInstruction(
            cop0TransferInstruction(source, 3, reg) | (UINT32_C(1) << bit)),
          "Reserved EE instruction encoding.");
      }
    }
  }
  REQUIRE(decodeEEInstruction(UINT32_C(0x4003b800)).destinationRegister == 23);
}

TEST_CASE("EE reserved COP0 reads return zero without writable state",
  "[reserved-cop0]")
{
  for (const std::uint8_t index : {
         7, 17, 18, 19, 20, 21, 22, 23, 26, 27, 31})
  {
    CAPTURE(index);
    NekoSystem system;
    EECore &core = system.eeCore();
    const auto reg = static_cast<EECOP0Register>(index);
    const auto before = core.stateHash();
    REQUIRE(core.cop0Register(reg) == 0);
    REQUIRE(core.stateHash() == before);
    for (const auto value : {UINT32_C(0), UINT32_MAX})
    {
      REQUIRE_THROWS_WITH(
        core.setCOP0Register(reg, value),
        "EE COP0 register is not implemented.");
    }
    REQUIRE(core.stateHash() == before);
    core.setGeneralRegister(3, {UINT64_MAX, UINT64_C(0x123456789abcdef0)});

    runInstruction(&system, cop0TransferInstruction(0, 3, reg));

    REQUIRE(core.stopReason() == EEStopReason::None);
    REQUIRE(core.acceptanceRecordsThisCycle().size() == 1);
    REQUIRE(
      core.generalRegister(3) ==
      EERegister128{0, UINT64_C(0x123456789abcdef0)});
    REQUIRE(core.cop0Register(reg) == 0);

    runInstruction(&system, cop0TransferInstruction(0, 0, reg));

    REQUIRE(core.stopReason() == EEStopReason::None);
    REQUIRE(core.generalRegister(0) == EERegister128{});
    core.reset();
    REQUIRE(core.cop0Register(reg) == 0);
  }
}

TEST_CASE("EE debug COP0 decoder admits only the manual read selectors",
  "[debug-cop0]")
{
  const auto debug = static_cast<EECOP0Register>(24);
  for (std::uint16_t selector = 0; selector < 2048; ++selector)
  {
    CAPTURE(selector);
    const bool valid = selector == 0 || (selector >= 2 && selector <= 7);
    for (const std::uint8_t source : {0, 4})
    {
      const auto raw = cop0TransferInstruction(source, 24, debug) | selector;
      if (!valid)
      {
        REQUIRE_THROWS_WITH(
          decodeEEInstruction(raw), "Reserved EE instruction encoding.");
      }
      else if (source == 4)
      {
        REQUIRE_THROWS_WITH(
          decodeEEInstruction(raw), "Unsupported EE instruction encoding.");
      }
      else
      {
        for (std::uint8_t target = 0; target < 32; ++target)
        {
          const auto instruction =
            decodeEEInstruction(cop0TransferInstruction(0, target, debug) | selector);
          REQUIRE(instruction.operation == EEOperation::MoveWordFromCOP0);
          REQUIRE(instruction.destinationRegister == 24);
          REQUIRE(instruction.targetRegister == target);
          REQUIRE((instruction.raw & UINT32_C(0x7ff)) == selector);
        }
      }
    }
  }
}

TEST_CASE("EE debug COP0 reads project the unprogrammed bank",
  "[debug-cop0]")
{
  const auto debug = static_cast<EECOP0Register>(24);
  for (const std::uint16_t selector : {0, 2, 3, 4, 5, 6, 7})
  {
    CAPTURE(selector);
    for (const std::uint8_t target : {0, 24, 31})
    {
      CAPTURE(target);
      NekoSystem system;
      auto &core = system.eeCore();
      core.setGeneralRegister(target, {UINT64_MAX, UINT64_C(0x123456789abcdef0)});

      runInstruction(&system, cop0TransferInstruction(0, target, debug) | selector);

      REQUIRE(core.stopReason() == EEStopReason::None);
      REQUIRE(core.pendingException() == EEException::None);
      REQUIRE(core.acceptanceRecordsThisCycle().size() == 1);
      REQUIRE(core.lastInstruction().raw ==
        (cop0TransferInstruction(0, target, debug) | selector));
      REQUIRE(core.generalRegister(target) ==
        (target == 0 ? EERegister128{} : EERegister128{0, UINT64_C(0x123456789abcdef0)}));
    }
    for (const auto value : {UINT64_C(0), UINT64_MAX})
    {
      NekoSystem system;
      system.eeCore().setGeneralRegister(24, {value, UINT64_MAX});
      runInstruction(&system, cop0TransferInstruction(4, 24, debug) | selector);
      REQUIRE(system.eeCore().stopReason() == EEStopReason::UnsupportedInstruction);
      REQUIRE(system.eeCore().acceptanceRecordsThisCycle().size() == 0);
      REQUIRE(system.eeCore().pendingException() == EEException::None);
    }
  }
}

TEST_CASE("EE debug COP0 inspection rejects invalid selectors without mutation",
  "[debug-cop0]")
{
  NekoSystem system;
  auto &core = system.eeCore();
  const auto before = core.stateHash();
  for (const std::uint16_t selector : {0, 2, 3, 4, 5, 6, 7})
  {
    REQUIRE(core.cop0DebugRegister(static_cast<EECOP0DebugRegister>(selector)) == 0);
    REQUIRE(core.stateHash() == before);
  }
  for (const std::uint16_t selector : {1, 8, 0x40, 0x400, 0x7ff, 0x800, 0xffff})
  {
    REQUIRE_THROWS_WITH(
      core.cop0DebugRegister(static_cast<EECOP0DebugRegister>(selector)),
      "EE COP0 debug register is not implemented.");
    REQUIRE(core.stateHash() == before);
  }
}

TEST_CASE("EE performance COP0 decoder admits bounded manual transfer selectors",
  "[performance-cop0][performance-control]")
{
  const auto performance = static_cast<EECOP0Register>(25);
  for (std::uint16_t selector = 0; selector < 2048; ++selector)
  {
    CAPTURE(selector);
    const bool valid = selector == 0 || selector == 1 || selector == 3;
    for (const std::uint8_t source : {0, 4})
    {
      CAPTURE(source);
      const auto raw = cop0TransferInstruction(source, 25, performance) | selector;
      if (!valid)
      {
        REQUIRE_THROWS_WITH(
          decodeEEInstruction(raw), "Reserved EE instruction encoding.");
      }
      else if (source == 4 && selector != 0)
      {
        REQUIRE_THROWS_WITH(
          decodeEEInstruction(raw), "Unsupported EE instruction encoding.");
      }
      else
      {
        for (std::uint8_t target = 0; target < 32; ++target)
        {
          const auto instruction =
            decodeEEInstruction(cop0TransferInstruction(source, target, performance) | selector);
          REQUIRE(
            instruction.operation ==
            (source == 0 ? EEOperation::MoveWordFromCOP0 : EEOperation::MoveWordToCOP0));
          REQUIRE(instruction.destinationRegister == 25);
          REQUIRE(instruction.targetRegister == target);
          REQUIRE((instruction.raw & UINT32_C(0x7ff)) == selector);
        }
      }
    }
  }
}

TEST_CASE("EE performance COP0 reads project the unprogrammed disabled bank",
  "[performance-cop0]")
{
  const auto performance = static_cast<EECOP0Register>(25);
  for (const std::uint16_t selector : {0, 1, 3})
  {
    CAPTURE(selector);
    for (const std::uint8_t target : {0, 25, 31})
    {
      CAPTURE(target);
      NekoSystem system;
      auto &core = system.eeCore();
      core.setGeneralRegister(target, {UINT64_MAX, UINT64_C(0x123456789abcdef0)});
      const auto read = cop0TransferInstruction(0, target, performance) | selector;

      runInstruction(&system, read);

      REQUIRE(core.stopReason() == EEStopReason::None);
      REQUIRE(core.pendingException() == EEException::None);
      REQUIRE(core.acceptanceRecordsThisCycle().size() == 1);
      REQUIRE(core.lastInstruction().raw == read);
      const auto expected =
        target == 0 ? EERegister128{} : EERegister128{0, UINT64_C(0x123456789abcdef0)};
      REQUIRE(core.generalRegister(target) == expected);
      const auto count = core.cop0Register(EECOP0Register::Count);

      system.runMasterCycles(4);
      runInstruction(&system, read);

      REQUIRE(core.cop0Register(EECOP0Register::Count) != count);
      REQUIRE(core.stopReason() == EEStopReason::None);
      REQUIRE(core.pendingException() == EEException::None);
      REQUIRE(core.generalRegister(target) == expected);
      core.reset();
      runInstruction(&system, read);
      REQUIRE(core.generalRegister(target).low == 0);
    }
    for (const auto value : {UINT64_C(0), UINT64_MAX, UINT64_C(0x80000000)})
    {
      NekoSystem system;
      system.eeCore().setGeneralRegister(25, {value, UINT64_MAX});
      runInstruction(&system, cop0TransferInstruction(4, 25, performance) | selector);
      REQUIRE(
        system.eeCore().stopReason() ==
        (selector != 0 ? EEStopReason::UnsupportedInstruction :
         value == 0 ? EEStopReason::None : EEStopReason::UndefinedOperation));
      REQUIRE(system.eeCore().acceptanceRecordsThisCycle().size() == (selector == 0 && value == 0 ? 1 : 0));
      REQUIRE(system.eeCore().pendingException() == EEException::None);
    }
  }
}

TEST_CASE("EE performance COP0 inspection rejects invalid selectors without mutation",
  "[performance-cop0]")
{
  NekoSystem system;
  auto &core = system.eeCore();
  const auto before = core.stateHash();
  for (const std::uint16_t selector : {0, 1, 3})
  {
    REQUIRE(
      core.cop0PerformanceRegister(static_cast<EECOP0PerformanceRegister>(selector)) == 0);
    REQUIRE(core.stateHash() == before);
  }
  for (const std::uint16_t selector : {2, 4, 0x40, 0x400, 0x7ff, 0x800, 0xffff})
  {
    REQUIRE_THROWS_WITH(
      core.cop0PerformanceRegister(static_cast<EECOP0PerformanceRegister>(selector)),
      "EE COP0 performance register is not implemented.");
    REQUIRE(core.stateHash() == before);
  }
}

TEST_CASE("EE MTPS accepts only a zero low word from every GPR",
  "[performance-control]")
{
  const auto performance = static_cast<EECOP0Register>(25);
  for (std::uint8_t source = 0; source < 32; ++source)
  {
    CAPTURE(source);
    for (const auto value : {UINT64_C(0), UINT64_C(0xffffffff00000000)})
    {
      NekoSystem system;
      auto &core = system.eeCore();
      core.setGeneralRegister(source, {value, UINT64_MAX});
      const auto before = core.generalRegister(source);
      const auto write = cop0TransferInstruction(4, source, performance);

      runInstruction(&system, write);

      REQUIRE(core.stopReason() == EEStopReason::None);
      REQUIRE(core.pendingException() == EEException::None);
      REQUIRE(core.acceptanceRecordsThisCycle().size() == 1);
      REQUIRE(core.lastInstruction().raw == write);
      REQUIRE(core.generalRegister(source) == before);
      REQUIRE(core.cop0PerformanceRegister(EECOP0PerformanceRegister::EventSpecifier) == 0);
      REQUIRE(core.cop0PerformanceRegister(EECOP0PerformanceRegister::Counter0) == 0);
      REQUIRE(core.cop0PerformanceRegister(EECOP0PerformanceRegister::Counter1) == 0);
    }
  }
  for (std::uint32_t bit = 0; bit < 32; ++bit)
  {
    CAPTURE(bit);
    NekoSystem system;
    auto &core = system.eeCore();
    const EERegister128 value{UINT64_C(0xffffffff00000000) | (UINT32_C(1) << bit), UINT64_MAX};
    core.setGeneralRegister(3, value);
    const auto write = cop0TransferInstruction(4, 3, performance);

    runInstruction(&system, write);

    REQUIRE(core.stopReason() == EEStopReason::UndefinedOperation);
    REQUIRE(core.rejectedInstruction() == write);
    REQUIRE(core.pendingException() == EEException::None);
    REQUIRE(core.acceptanceRecordsThisCycle().size() == 0);
    REQUIRE(core.generalRegister(3) == value);
    REQUIRE(core.cop0PerformanceRegister(EECOP0PerformanceRegister::EventSpecifier) == 0);
    REQUIRE(core.cop0PerformanceRegister(EECOP0PerformanceRegister::Counter0) == 0);
    REQUIRE(core.cop0PerformanceRegister(EECOP0PerformanceRegister::Counter1) == 0);
  }
}

TEST_CASE("EE MTPS checks usability before its operand boundary",
  "[performance-control]")
{
  for (const auto mode :
       {UINT32_C(0), EECOP0Status::USER_MODE, EECOP0Status::SUPERVISOR_MODE})
  {
    for (const bool cu0 : {false, true})
    {
      for (const std::uint64_t value : {0, 1})
      {
        CAPTURE(mode);
        CAPTURE(cu0);
        CAPTURE(value);
        NekoSystem system;
        auto &core = system.eeCore();
        core.setCOP0Register(EECOP0Register::Status, mode | (cu0 ? EECOP0Status::COP0_USABLE : 0));
        mapLowKusegForTest(&core);
        core.setGeneralRegister(3, {value, UINT64_MAX});
        const auto write = UINT32_C(0x4083c800);
        const bool usable = mode == 0 || cu0;

        runInstruction(&system, write);

        REQUIRE(
          core.stopReason() ==
          (usable && value != 0 ? EEStopReason::UndefinedOperation : EEStopReason::None));
        REQUIRE(
          core.pendingException() ==
          (usable ? EEException::None : EEException::CoprocessorUnusable));
        REQUIRE(core.acceptanceRecordsThisCycle().size() == (usable && value == 0 ? 1 : 0));
        REQUIRE(core.generalRegister(3) == EERegister128{value, UINT64_MAX});
        REQUIRE(core.cop0PerformanceRegister(EECOP0PerformanceRegister::EventSpecifier) == 0);
        if (!usable)
        {
          REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
          REQUIRE((core.cop0Register(EECOP0Register::Cause) & EECOP0Cause::COPROCESSOR_ERROR_MASK) == 0);
        }
      }
    }
  }

  SECTION("Delay-slot faults preserve the older branch")
  {
    for (const bool usable : {false, true})
    {
      NekoSystem system;
      auto &core = system.eeCore();
      core.setCOP0Register(
        EECOP0Register::Status,
        EECOP0Status::USER_MODE | (usable ? EECOP0Status::COP0_USABLE : 0));
      mapLowKusegForTest(&core);
      core.setGeneralRegister(3, {1, UINT64_MAX});
      system.eeBus().write32(0x100, UINT32_C(0x10000007));
      system.eeBus().write32(0x104, UINT32_C(0x4083c800));
      core.startExecution(0x100);

      system.clockMasterCycle();

      REQUIRE(core.acceptanceRecordsThisCycle().size() == 1);
      REQUIRE(core.acceptanceRecordsThisCycle()[0].instruction.raw == UINT32_C(0x10000007));
      REQUIRE(core.stopReason() == (usable ? EEStopReason::UndefinedOperation : EEStopReason::None));
      REQUIRE(core.pendingException() == (usable ? EEException::None : EEException::CoprocessorUnusable));
      if (usable)
      {
        REQUIRE(core.programCounter() == 0x104);
        REQUIRE(core.rejectedInstruction() == UINT32_C(0x4083c800));
      }
      else
      {
        REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0x100);
        REQUIRE((core.cop0Register(EECOP0Register::Cause) & EECOP0Cause::BRANCH_DELAY) != 0);
      }
    }
  }
}

TEST_CASE("EE MTPS preserves ordered Wide work and source dependencies",
  "[performance-control]")
{
  SECTION("Independent Wide work survives only when older than a rejection")
  {
    for (const bool writeOlder : {false, true})
    {
      for (const std::uint64_t value : {0, 1})
      {
        CAPTURE(writeOlder);
        CAPTURE(value);
        NekoSystem system;
        auto &core = system.eeCore();
        constexpr auto WRITE = UINT32_C(0x4083c800);
        constexpr auto WIDE = UINT32_C(0x70a43488);
        system.eeBus().write32(0, writeOlder ? WRITE : WIDE);
        system.eeBus().write32(4, writeOlder ? WIDE : WRITE);
        core.setGeneralRegister(3, {value, UINT64_MAX});
        core.setGeneralRegister(4, {UINT64_C(0x1111111100000000), 0});
        core.setGeneralRegister(5, {UINT64_C(0x3333333322222222), 0});
        core.startExecution(0);

        system.clockMasterCycle();

        const bool wideRetires = value == 0 || !writeOlder;
        REQUIRE(core.stopReason() == (value == 0 ? EEStopReason::None : EEStopReason::UndefinedOperation));
        REQUIRE(core.pendingException() == EEException::None);
        REQUIRE(core.acceptanceRecordsThisCycle().size() == (value == 0 ? 2 : (writeOlder ? 0 : 1)));
        REQUIRE(
          core.generalRegister(6) ==
          (wideRetires
             ? EERegister128{UINT64_C(0x2222222200000000), UINT64_C(0x3333333311111111)}
             : EERegister128{}));
        REQUIRE(core.generalRegister(3) == EERegister128{value, UINT64_MAX});
        if (value != 0)
        {
          REQUIRE(core.programCounter() == (writeOlder ? 0 : 4));
          REQUIRE(core.rejectedInstruction() == WRITE);
        }
      }
    }
  }

  SECTION("The operand comes from the completed older producer")
  {
    for (const std::uint64_t value : {0, 1})
    {
      NekoSystem system;
      auto &core = system.eeCore();
      system.eeBus().write32(0, UINT32_C(0x24030000) | static_cast<std::uint32_t>(value));
      system.eeBus().write32(4, UINT32_C(0x4083c800));
      core.setGeneralRegister(3, {value == 0 ? 1U : 0U, UINT64_MAX});
      core.startExecution(0);

      system.clockMasterCycle();

      REQUIRE(core.lastIssueSelection().instructionCount == 1);
      REQUIRE(core.programCounter() == 4);
      REQUIRE(core.generalRegister(3).low == value);
      REQUIRE(core.stopReason() == EEStopReason::None);

      system.clockMasterCycle();

      REQUIRE(core.stopReason() == (value == 0 ? EEStopReason::None : EEStopReason::UndefinedOperation));
      REQUIRE(core.cop0PerformanceRegister(EECOP0PerformanceRegister::EventSpecifier) == 0);
    }
  }
}

TEST_CASE("EE MTPS delay-slot completion and rejection restore deterministically",
  "[performance-control][save-state]")
{
  for (const std::uint64_t value : {0, 1})
  {
    for (const std::uint64_t checkpointCycle : {0, 1})
    {
      CAPTURE(value);
      CAPTURE(checkpointCycle);
      NekoSystem original;
      original.eeBus().write32(0x100, UINT32_C(0x10000007));
      original.eeBus().write32(0x104, UINT32_C(0x4083c800));
      original.eeBus().write32(0x120, UINT32_C(0x0000040f));
      original.eeBus().write32(0x124, UINT32_C(0x4004c800));
      original.eeCore().setGeneralRegister(3, {value, UINT64_MAX});
      original.eeCore().setGeneralRegister(4, {UINT64_MAX, UINT64_MAX});
      original.eeCore().startExecution(UINT32_C(0x80000100));
      original.runMasterCycles(checkpointCycle);
      const auto checkpoint = original.saveState();
      NekoSystem restored;
      NekoSystem plain;
      restored.loadState(checkpoint);
      plain.loadState(checkpoint);
      REQUIRE(restored.saveState() == checkpoint);
      REQUIRE(plain.saveState() == checkpoint);
      original.startTrace();
      restored.startTrace();
      for (auto *machine : {&original, &restored, &plain})
      {
        machine->runMasterCycles(3 - checkpointCycle);
        const auto &core = machine->eeCore();
        REQUIRE(core.stopReason() == (value == 0 ? EEStopReason::None : EEStopReason::UndefinedOperation));
        REQUIRE(core.pendingException() == EEException::None);
        REQUIRE(core.generalRegister(3) == EERegister128{value, UINT64_MAX});
        REQUIRE(
          core.generalRegister(4) ==
          EERegister128{value == 0 ? 0 : UINT64_MAX, UINT64_MAX});
        REQUIRE(core.cop0PerformanceRegister(EECOP0PerformanceRegister::EventSpecifier) == 0);
        REQUIRE(core.cop0PerformanceRegister(EECOP0PerformanceRegister::Counter0) == 0);
        REQUIRE(core.cop0PerformanceRegister(EECOP0PerformanceRegister::Counter1) == 0);
      }
      REQUIRE(original.saveState() == restored.saveState());
      REQUIRE(original.saveState() == plain.saveState());
      REQUIRE(original.traceHash() == restored.traceHash());
      REQUIRE(original.trace().size() == restored.trace().size());
      REQUIRE(plain.trace().empty());
    }
  }
}

TEST_CASE("EE read-only COP0 reads preserve usability and ordered issue",
  "[reserved-cop0][debug-cop0][performance-cop0]")
{
  SECTION("User and supervisor reads still require CU0")
  {
    for (const auto read : READ_ONLY_COP0_INSTRUCTIONS)
    {
      CAPTURE(read);
      for (const auto mode : {EECOP0Status::USER_MODE, EECOP0Status::SUPERVISOR_MODE})
      {
        for (const bool usable : {false, true})
        {
          CAPTURE(mode);
          CAPTURE(usable);
          NekoSystem system;
          EECore &core = system.eeCore();
          core.setCOP0Register(
            EECOP0Register::Status, mode | (usable ? EECOP0Status::COP0_USABLE : 0));
          mapLowKusegForTest(&core);
          core.setGeneralRegister(3, {UINT64_MAX, UINT64_MAX});

          runInstruction(&system, read);

          REQUIRE(core.stopReason() == EEStopReason::None);
          REQUIRE(
            core.pendingException() ==
            (usable ? EEException::None : EEException::CoprocessorUnusable));
          REQUIRE(
            core.generalRegister(3) ==
            EERegister128{usable ? 0 : UINT64_MAX, UINT64_MAX});
          if (!usable)
          {
            REQUIRE(core.acceptanceRecordsThisCycle().size() == 0);
            REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
            REQUIRE(
              (core.cop0Register(EECOP0Register::Cause) &
               EECOP0Cause::COPROCESSOR_ERROR_MASK) == 0);
          }
        }
      }
    }
  }

  SECTION("An unusable delay-slot read preserves the older branch")
  {
    for (const auto read : READ_ONLY_COP0_INSTRUCTIONS)
    {
      CAPTURE(read);
      NekoSystem system;
      auto &core = system.eeCore();
      core.setCOP0Register(EECOP0Register::Status, EECOP0Status::USER_MODE);
      mapLowKusegForTest(&core);
      core.setGeneralRegister(3, {UINT64_MAX, UINT64_MAX});
      system.eeBus().write32(0x100, UINT32_C(0x10000007));
      system.eeBus().write32(0x104, read);
      core.startExecution(0x100);

      system.clockMasterCycle();

      REQUIRE(core.stopReason() == EEStopReason::None);
      REQUIRE(core.pendingException() == EEException::CoprocessorUnusable);
      REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0x100);
      REQUIRE((core.cop0Register(EECOP0Register::Cause) & EECOP0Cause::BRANCH_DELAY) != 0);
      REQUIRE(core.generalRegister(3) == EERegister128{UINT64_MAX, UINT64_MAX});
      REQUIRE(core.acceptanceRecordsThisCycle().size() == 1);
      REQUIRE(core.acceptanceRecordsThisCycle()[0].instruction.raw == UINT32_C(0x10000007));
    }
  }

  SECTION("Independent Wide work pairs in either order without a stall")
  {
    for (const auto read : READ_ONLY_COP0_INSTRUCTIONS)
    {
      CAPTURE(read);
      for (const bool readOlder : {true, false})
      {
        CAPTURE(readOlder);
        NekoSystem system;
        EECore &core = system.eeCore();
        constexpr std::uint32_t WIDE = UINT32_C(0x70a43488);
        system.eeBus().write32(0, readOlder ? read : WIDE);
        system.eeBus().write32(4, readOlder ? WIDE : read);
        system.eeBus().write32(8, UINT32_C(0x24670001));
        core.setGeneralRegister(3, {UINT64_MAX, UINT64_MAX});
        core.setGeneralRegister(4, {UINT64_C(0x1111111100000000), 0});
        core.setGeneralRegister(5, {UINT64_C(0x3333333322222222), 0});
        core.startExecution(0);

        system.clockMasterCycle();

        REQUIRE(core.lastIssueSelection().instructionCount == 2);
        REQUIRE(core.lastIssueSelection().pairing == EEIssuePairing::Concurrent);
        REQUIRE(core.lastIssueSelection().continuation == EEIssueContinuation::None);
        REQUIRE(core.acceptanceRecordsThisCycle().size() == 2);
        REQUIRE(core.generalRegister(3) == EERegister128{0, UINT64_MAX});
        REQUIRE(core.generalRegister(6).low == UINT64_C(0x2222222200000000));
        REQUIRE(core.generalRegister(6).high == UINT64_C(0x3333333311111111));
        REQUIRE(core.programCounter() == 8);

        system.clockMasterCycle();

        REQUIRE(core.generalRegister(7).low == 1);
        REQUIRE(core.acceptanceRecordsThisCycle().size() == 1);
        REQUIRE(core.acceptanceRecordsThisCycle()[0].programOrder == 3);
      }
    }
  }

  SECTION("A dependent successor waits for the read")
  {
    for (const auto read : READ_ONLY_COP0_INSTRUCTIONS)
    {
      NekoSystem system;
      system.eeBus().write32(0, read);
      system.eeBus().write32(4, UINT32_C(0x24670001));
      system.eeCore().setGeneralRegister(3, {UINT64_MAX, 0});
      system.eeCore().startExecution(0);

      system.clockMasterCycle();

      REQUIRE(system.eeCore().lastIssueSelection().instructionCount == 1);
      REQUIRE(system.eeCore().generalRegister(7).low == 0);
      REQUIRE(system.eeCore().programCounter() == 4);

      system.clockMasterCycle();

      REQUIRE(system.eeCore().generalRegister(7).low == 1);
    }
  }
}

TEST_CASE("EE read-only COP0 delay-slot reads restore deterministic continuation",
  "[reserved-cop0][debug-cop0][performance-cop0][save-state]")
{
  for (const auto read : READ_ONLY_COP0_INSTRUCTIONS)
  {
    CAPTURE(read);
    for (const std::uint64_t checkpointCycle : {0, 1})
    {
      CAPTURE(checkpointCycle);
      NekoSystem original;
      original.eeBus().write32(0x100, UINT32_C(0x10000007));
      original.eeBus().write32(0x104, read);
      original.eeBus().write32(0x120, UINT32_C(0x24640001));
      original.eeCore().setGeneralRegister(3, {UINT64_MAX, UINT64_MAX});
      original.eeCore().startExecution(UINT32_C(0x80000100));
      original.runMasterCycles(checkpointCycle);
      const auto checkpoint = original.saveState();
      NekoSystem restored;
      NekoSystem plain;
      restored.loadState(checkpoint);
      plain.loadState(checkpoint);
      REQUIRE(restored.saveState() == checkpoint);
      REQUIRE(plain.saveState() == checkpoint);
      original.startTrace();
      restored.startTrace();
      for (auto *machine : {&original, &restored, &plain})
      {
        machine->runMasterCycles(2 - checkpointCycle);
        REQUIRE(machine->eeCore().stopReason() == EEStopReason::None);
        REQUIRE(machine->eeCore().generalRegister(3) == EERegister128{0, UINT64_MAX});
        REQUIRE(machine->eeCore().generalRegister(4).low == 1);
        REQUIRE(machine->eeCore().programCounter() == UINT32_C(0x80000124));
      }
      REQUIRE(original.saveState() == restored.saveState());
      REQUIRE(original.saveState() == plain.saveState());
      REQUIRE(original.traceHash() == restored.traceHash());
      REQUIRE(original.trace().size() == restored.trace().size());
      REQUIRE_FALSE(original.trace().empty());
      REQUIRE(plain.trace().empty());
    }
  }
}

TEST_CASE("EE MTC0 consumes only the general-register low word")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setGeneralRegister(
    2,
    {UINT64_C(0xabcdef0112345678), UINT64_MAX});

  runInstruction(
    &system,
    cop0TransferInstruction(
      0x04,
      2,
      EECOP0Register::TagHi));

  REQUIRE(
    core.cop0Register(EECOP0Register::TagHi) ==
    UINT32_C(0x12345678));
}

TEST_CASE("EE COP0 transfers honor general register zero")
{
  SECTION("MFC0 suppresses a write to general register zero")
  {
    NekoSystem system;
    system.eeCore().setCOP0Register(
      EECOP0Register::TagHi,
      UINT32_MAX);

    runInstruction(
      &system,
      cop0TransferInstruction(
        0x00,
        0,
        EECOP0Register::TagHi));

    REQUIRE(system.eeCore().generalRegister(0) == EERegister128{});
  }

  SECTION("MTC0 reads zero from general register zero")
  {
    NekoSystem system;
    system.eeCore().setCOP0Register(
      EECOP0Register::EntryHi,
      UINT32_MAX);

    runInstruction(
      &system,
      cop0TransferInstruction(
        0x04,
        0,
        EECOP0Register::EntryHi));

    REQUIRE(
      system.eeCore().cop0Register(EECOP0Register::EntryHi) ==
      0);
  }
}

TEST_CASE("EE MTC0 applies register-specific write policies")
{
  SECTION("Index preserves probe state")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Index,
      EECOP0Index::PROBE_FAILURE);
    core.setGeneralRegister(2, {42, 0});

    runInstruction(
      &system,
      cop0TransferInstruction(
        0x04,
        2,
        EECOP0Register::Index));

    REQUIRE(
      core.cop0Register(EECOP0Register::Index) ==
      (EECOP0Index::PROBE_FAILURE | 42));
  }

  SECTION("Context preserves exception-owned BadVPN2")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Context,
      UINT32_C(0x00543210));
    core.setGeneralRegister(2, {UINT32_MAX, 0});

    runInstruction(
      &system,
      cop0TransferInstruction(
        0x04,
        2,
        EECOP0Register::Context));

    REQUIRE(
      core.cop0Register(EECOP0Register::Context) ==
      UINT32_C(0xffd43210));
  }

  SECTION("Status preserves cache-hit state")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::CACHE_HIT);
    mapLowKusegForTest(&core);
    core.setGeneralRegister(2, {UINT32_MAX, 0});

    runInstruction(
      &system,
      cop0TransferInstruction(
        0x04,
        2,
        EECOP0Register::Status));

    REQUIRE(
      core.cop0Register(EECOP0Register::Status) ==
      EECOP0Status::IMPLEMENTED_MASK);
  }

  SECTION("Read-only registers ignore guest writes")
  {
    SECTION("Random follows only normal retirement sequencing")
    {
      NekoSystem system;
      EECore &core = system.eeCore();
      core.setCOP0Register(EECOP0Register::Random, 17);
      core.setGeneralRegister(2, {0, 0});
      runInstruction(
        &system,
        cop0TransferInstruction(
          0x04,
          2,
          EECOP0Register::Random));
      REQUIRE(core.cop0Register(EECOP0Register::Random) == 16);
    }

    SECTION("BadVAddr and Cause remain unchanged")
    {
      NekoSystem system;
      EECore &core = system.eeCore();
      core.setCOP0Register(
        EECOP0Register::BadVAddr,
        UINT32_C(0x12345678));
      core.setCOP0Register(
        EECOP0Register::Cause,
        UINT32_C(0x80008030));
      core.setGeneralRegister(2, {0, 0});

      runInstruction(
        &system,
        cop0TransferInstruction(
          0x04,
          2,
          EECOP0Register::BadVAddr));
      runInstruction(
        &system,
        cop0TransferInstruction(
          0x04,
          2,
          EECOP0Register::Cause));

      REQUIRE(
        core.cop0Register(EECOP0Register::BadVAddr) ==
        UINT32_C(0x12345678));
      REQUIRE(
        core.cop0Register(EECOP0Register::Cause) ==
        UINT32_C(0x80008030));
    }
  }
}

TEST_CASE("EE MTC0 Wired writes reset Random atomically")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Wired, 3);
  core.setCOP0Register(EECOP0Register::Random, 17);
  core.setGeneralRegister(2, {7, 0});

  runInstruction(
    &system,
    cop0TransferInstruction(
      0x04,
      2,
      EECOP0Register::Wired));

  REQUIRE(core.cop0Register(EECOP0Register::Wired) == 7);
  REQUIRE(
    core.cop0Register(EECOP0Register::Random) ==
    EECOP0Random::RESET - 1);
}

TEST_CASE("EE Random advances through the Wired replacement range")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Wired, 5);
  core.setCOP0Register(EECOP0Register::Random, 6);

  runInstruction(&system, 0);
  REQUIRE(core.cop0Register(EECOP0Register::Random) == 5);
  runInstruction(&system, 0);
  REQUIRE(
    core.cop0Register(EECOP0Register::Random) ==
    EECOP0Random::RESET);
  runInstruction(&system, 0);
  REQUIRE(
    core.cop0Register(EECOP0Register::Random) ==
    EECOP0Random::RESET - 1);
}

TEST_CASE("EE MTC0 rejects unsupported values without mutation")
{
  SECTION("Wired and Random remain unchanged")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Wired, 3);
    core.setCOP0Register(EECOP0Register::Random, 17);
    core.setGeneralRegister(2, {48, 0});
    const std::uint32_t instruction =
      cop0TransferInstruction(
        0x04,
        2,
        EECOP0Register::Wired);

    runInstruction(&system, instruction);

    REQUIRE(core.cop0Register(EECOP0Register::Wired) == 3);
    REQUIRE(core.cop0Register(EECOP0Register::Random) == 17);
    REQUIRE(core.executionState() == EEExecutionState::Halted);
    REQUIRE(core.stopReason() == EEStopReason::UndefinedOperation);
    REQUIRE(core.rejectedInstruction() == instruction);
  }

  SECTION("PageMask remains unchanged after reserved-bit removal")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::PageMask,
      EECOP0PageMask::SIZE_16_KIB);
    core.setGeneralRegister(
      2,
      {UINT32_C(0x80002000), 0});
    const std::uint32_t instruction =
      cop0TransferInstruction(
        0x04,
        2,
        EECOP0Register::PageMask);

    runInstruction(&system, instruction);

    REQUIRE(
      core.cop0Register(EECOP0Register::PageMask) ==
      EECOP0PageMask::SIZE_16_KIB);
    REQUIRE(core.executionState() == EEExecutionState::Halted);
    REQUIRE(core.stopReason() == EEStopReason::UndefinedOperation);
    REQUIRE(core.rejectedInstruction() == instruction);
  }
}

TEST_CASE("EE COP0 transfers require kernel mode or CU0")
{
  SECTION("User mode without CU0 raises Coprocessor Unusable")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::USER_MODE);
    mapLowKusegForTest(&core);
    core.setCOP0Register(
      EECOP0Register::TagHi,
      UINT32_C(0x12345678));
    core.setGeneralRegister(2, {UINT64_MAX, UINT64_MAX});

    runInstruction(
      &system,
      cop0TransferInstruction(
        0x00,
        2,
        EECOP0Register::TagHi));

    REQUIRE(
      core.pendingException() ==
      EEException::CoprocessorUnusable);
    REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
    REQUIRE(
      (core.cop0Register(EECOP0Register::Cause) &
       EECOP0Cause::COPROCESSOR_ERROR_MASK) == 0);
    REQUIRE(
      core.generalRegister(2) ==
      EERegister128{UINT64_MAX, UINT64_MAX});
  }

  SECTION("CU0 permits a user-mode transfer")
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      EECOP0Status::USER_MODE |
      EECOP0Status::COP0_USABLE);
    mapLowKusegForTest(&core);
    core.setCOP0Register(
      EECOP0Register::TagHi,
      UINT32_C(0x12345678));

    runInstruction(
      &system,
      cop0TransferInstruction(
        0x00,
        2,
        EECOP0Register::TagHi));

    REQUIRE_FALSE(core.exceptionPending());
    REQUIRE(
      core.generalRegister(2).low ==
      UINT64_C(0x0000000012345678));
  }
}

TEST_CASE("EE COP0 registers have deterministic reset state")
{
  NekoSystem system;
  const EECore &core = system.eeCore();

  REQUIRE(core.cop0Register(EECOP0Register::Index) == 0);
  REQUIRE(
    core.cop0Register(EECOP0Register::Random) ==
    EECOP0Random::RESET);
  REQUIRE(core.cop0Register(EECOP0Register::EntryLo0) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::EntryLo1) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Context) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::PageMask) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Wired) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::EntryHi) == 0);
  REQUIRE(
    core.cop0Register(EECOP0Register::Status) ==
    EECOP0Status::RESET);
  REQUIRE(core.cop0Register(EECOP0Register::Cause) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
  REQUIRE(
    core.cop0Register(EECOP0Register::Config) ==
    EECOP0Config::RESET);
  REQUIRE(core.cop0Register(EECOP0Register::TagLo) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::TagHi) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::ErrorEPC) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::BadVAddr) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Count) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Compare) == 0);
  REQUIRE(core.programCounter() == EEReset::VECTOR);
  REQUIRE(core.executionState() == EEExecutionState::Halted);
}

TEST_CASE("EE COP0 register state can be inspected and restored")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  const struct
  {
    EECOP0Register registerIndex;
    std::uint32_t value;
  } contracts[] = {
    {EECOP0Register::BadVAddr, UINT32_C(0x81234567)},
    {EECOP0Register::Count, UINT32_C(0x12345678)},
    {EECOP0Register::Compare, UINT32_C(0x87654321)},
    {EECOP0Register::Status, UINT32_C(0xf0c79c1f)},
    {EECOP0Register::Cause, UINT32_C(0x80008030)},
    {EECOP0Register::EPC, UINT32_C(0x80001000)},
    {EECOP0Register::ErrorEPC, UINT32_C(0xbfc00000)}
  };

  for (const auto &contract : contracts)
  {
    core.setCOP0Register(
      contract.registerIndex,
      contract.value);
    REQUIRE(
      core.cop0Register(contract.registerIndex) ==
      contract.value);
  }

  core.reset();

  REQUIRE(
    core.cop0Register(EECOP0Register::Status) ==
    EECOP0Status::RESET);
  REQUIRE(core.cop0Register(EECOP0Register::Cause) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::ErrorEPC) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::BadVAddr) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Count) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Compare) == 0);
  REQUIRE(core.programCounter() == EEReset::VECTOR);
}

TEST_CASE("EE COP0 translation and cache registers canonicalize state")
{
  NekoSystem system;
  EECore &core = system.eeCore();

  core.setCOP0Register(EECOP0Register::Index, UINT32_MAX);
  core.setCOP0Register(EECOP0Register::Random, 17);
  core.setCOP0Register(EECOP0Register::EntryLo0, UINT32_MAX);
  core.setCOP0Register(EECOP0Register::EntryLo1, UINT32_MAX);
  core.setCOP0Register(EECOP0Register::Context, UINT32_MAX);
  core.setCOP0Register(
    EECOP0Register::PageMask,
    UINT32_C(0x80000000) | EECOP0PageMask::SIZE_16_KIB);
  core.setCOP0Register(EECOP0Register::Wired, 47);
  core.setCOP0Register(EECOP0Register::EntryHi, UINT32_MAX);
  core.setCOP0Register(EECOP0Register::Config, UINT32_MAX);
  core.setCOP0Register(EECOP0Register::TagLo, UINT32_MAX);
  core.setCOP0Register(EECOP0Register::TagHi, UINT32_MAX);

  REQUIRE(
    core.cop0Register(EECOP0Register::Index) ==
    EECOP0Index::IMPLEMENTED_MASK);
  REQUIRE(core.cop0Register(EECOP0Register::Random) == 17);
  REQUIRE(
    core.cop0Register(EECOP0Register::EntryLo0) ==
    EECOP0EntryLo::ENTRY_LO_0_IMPLEMENTED_MASK);
  REQUIRE(
    core.cop0Register(EECOP0Register::EntryLo1) ==
    EECOP0EntryLo::ENTRY_LO_1_IMPLEMENTED_MASK);
  REQUIRE(
    core.cop0Register(EECOP0Register::Context) ==
    EECOP0Context::IMPLEMENTED_MASK);
  REQUIRE(
    core.cop0Register(EECOP0Register::PageMask) ==
    EECOP0PageMask::SIZE_16_KIB);
  REQUIRE(core.cop0Register(EECOP0Register::Wired) == 47);
  REQUIRE(
    core.cop0Register(EECOP0Register::EntryHi) ==
    EECOP0EntryHi::IMPLEMENTED_MASK);
  REQUIRE(
    core.cop0Register(EECOP0Register::Config) ==
    (EECOP0Config::WRITABLE_MASK | EECOP0Config::FIXED));
  REQUIRE(
    core.cop0Register(EECOP0Register::TagLo) ==
    UINT32_MAX);
  REQUIRE(
    core.cop0Register(EECOP0Register::TagHi) ==
    UINT32_MAX);

  REQUIRE_THROWS_WITH(
    core.setCOP0Register(EECOP0Register::Random, 48),
    "EE COP0 Random index is outside the 48-entry TLB.");
  REQUIRE_THROWS_WITH(
    core.setCOP0Register(EECOP0Register::Wired, 48),
    "EE COP0 Wired index is outside the 48-entry TLB.");
  REQUIRE_THROWS_WITH(
    core.setCOP0Register(
      EECOP0Register::PageMask,
      UINT32_C(0x00002000)),
    "EE COP0 PageMask encoding is unsupported.");
}

TEST_CASE("EE COP0 translation and cache registers affect state hashes")
{
  const struct
  {
    EECOP0Register registerIndex;
    std::uint32_t value;
  } contracts[] = {
    {EECOP0Register::Index, 1},
    {EECOP0Register::Random, 46},
    {EECOP0Register::EntryLo0, 1},
    {EECOP0Register::EntryLo1, 1},
    {EECOP0Register::Context, UINT32_C(0x10)},
    {EECOP0Register::PageMask, EECOP0PageMask::SIZE_16_KIB},
    {EECOP0Register::Wired, 1},
    {EECOP0Register::EntryHi, 1},
    {EECOP0Register::Config, EECOP0Config::DATA_CACHE_ENABLE},
    {EECOP0Register::TagLo, EECOP0TagLo::VALID},
    {EECOP0Register::TagHi, 1}
  };

  for (const auto &contract : contracts)
  {
    NekoSystem system;
    EECore &core = system.eeCore();
    const std::uint64_t resetHash = core.stateHash();

    core.setCOP0Register(
      contract.registerIndex,
      contract.value);

    REQUIRE(core.stateHash() != resetHash);
    core.reset();
    REQUIRE(core.stateHash() == resetHash);
  }
}

TEST_CASE("Neko system reset returns the EE to architectural Reset entry")
{
  NekoSystem system;
  system.eeCore().setProgramCounter(0x80001000);
  system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
  system.eeCore().setCOP0Register(
    EECOP0Register::ErrorEPC,
    UINT32_MAX);
  system.eeCore().startExecution(0);
  system.clockMasterCycle();

  system.reset();

  REQUIRE(system.eeCore().programCounter() == EEReset::VECTOR);
  REQUIRE(
    system.eeCore().cop0Register(EECOP0Register::Status) ==
    EECOP0Status::RESET);
  REQUIRE(
    system.eeCore().cop0Register(EECOP0Register::ErrorEPC) == 0);
  REQUIRE(
    system.eeCore().executionState() ==
    EEExecutionState::Halted);
  REQUIRE(system.eeCore().stopReason() == EEStopReason::None);
  REQUIRE(system.eeCore().elapsedCycles() == 0);
}

TEST_CASE("EE Reset fetch faults enter the bootstrap exception vector")
{
  NekoSystem system;
  system.eeCore().startExecution(
    system.eeCore().programCounter());

  system.clockMasterCycle();

  REQUIRE(
    system.eeCore().programCounter() ==
    EEExceptionVector::BOOTSTRAP_GENERAL);
  REQUIRE(
    system.eeCore().executionState() ==
    EEExecutionState::Running);
  REQUIRE(system.eeCore().stopReason() == EEStopReason::None);
  REQUIRE(
    system.eeCore().pendingException() ==
    EEException::InstructionBusError);
  REQUIRE(
    system.eeCore().exceptionAddress() ==
    EEReset::VECTOR);
  REQUIRE(
    system.eeCore().cop0Register(EECOP0Register::EPC) ==
    EEReset::VECTOR);
  REQUIRE_FALSE(system.eeCore().hasLastInstruction());
}

TEST_CASE("EE COP0 rejects unimplemented register identifiers")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  for (const std::uint8_t index : {24, 25, 32, 255})
  {
    CAPTURE(index);
    const auto unsupported = static_cast<EECOP0Register>(index);
    REQUIRE_THROWS_WITH(
      core.cop0Register(unsupported),
      "EE COP0 register is not implemented.");
    REQUIRE_THROWS_WITH(
      core.setCOP0Register(unsupported, 0),
      "EE COP0 register is not implemented.");
  }
}
