#include <cstdint>

#include "catch.hpp"
#include "ee_test_utils.hpp"
#include "gif_dmac_channel.hpp"
#include "neko_system.hpp"

namespace
{
  constexpr std::uint32_t TIMER_PENDING = EECOP0Cause::TIMER_PENDING;
  constexpr std::uint32_t TIMER_ENABLED_STATUS =
    EECOP0Status::INTERRUPT_ENABLE |
    EECOP0Status::MASTER_INTERRUPT_ENABLE |
    EECOP0Status::TIMER_MASK;

  std::uint32_t cop0Transfer(
    std::uint8_t operation,
    std::uint8_t target,
    EECOP0Register index)
  {
    return
      (UINT32_C(0x10) << 26) |
      (static_cast<std::uint32_t>(operation) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      (static_cast<std::uint32_t>(index) << 11);
  }

  void prepareTimer(NekoSystem *system)
  {
    EECore &core = system->eeCore();
    mapLowKusegForTest(&core);
    core.setCOP0Register(EECOP0Register::Status, 0);
    core.startExecution(0);
  }

  std::uint32_t timerPending(const EECore &core)
  {
    return core.cop0Register(EECOP0Register::Cause) & TIMER_PENDING;
  }
}

TEST_CASE("EE CP0 timer advances only on active clocks", "[cp0-timer]")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  system.runMasterCycles(3);
  REQUIRE(core.cop0Register(EECOP0Register::Count) == 0);
  REQUIRE(timerPending(core) == 0);

  core.setCOP0Register(EECOP0Register::Count, 17);
  core.setCOP0Register(EECOP0Register::Compare, 100);
  const std::uint64_t hash = core.stateHash();
  for (unsigned index = 0; index < 3; ++index)
  {
    REQUIRE(core.cop0Register(EECOP0Register::Count) == 17);
    REQUIRE(core.cop0Register(EECOP0Register::Compare) == 100);
    REQUIRE(core.stateHash() == hash);
  }

  prepareTimer(&system);
  system.runMasterCycles(3);
  REQUIRE(core.elapsedCycles() == 3);
  REQUIRE(core.cop0Register(EECOP0Register::Count) == 20);
  REQUIRE(core.cop0Register(EECOP0Register::Compare) == 100);

  core.haltExecution();
  system.runMasterCycles(2);
  REQUIRE(core.cop0Register(EECOP0Register::Count) == 20);
  REQUIRE(core.elapsedCycles() == 3);
  core.startExecution(core.programCounter());
  system.clockMasterCycle();
  REQUIRE(core.cop0Register(EECOP0Register::Count) == 21);

  core.reset();
  REQUIRE(core.cop0Register(EECOP0Register::Count) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Compare) == 0);
  REQUIRE(timerPending(core) == 0);
}

TEST_CASE("EE CP0 timer ignores issue width and MAC stalls", "[cp0-timer]")
{
  NekoSystem system;
  prepareTimer(&system);
  EECore &core = system.eeCore();
  core.setGeneralRegister(1, {3, 0});
  core.setGeneralRegister(2, {4, 0});
  core.setGeneralRegister(4, {5, 0});
  core.setGeneralRegister(5, {6, 0});
  system.eeBus().write32(0, UINT32_C(0x00221818));
  system.eeBus().write32(4, UINT32_C(0x70853018));
  system.eeBus().write32(8, UINT32_C(0x00003812));

  system.clockMasterCycle();
  REQUIRE(core.acceptanceRecordsThisCycle().size() == 2);
  REQUIRE(core.cop0Register(EECOP0Register::Count) == 1);
  for (std::uint32_t count = 2; count <= 3; ++count)
  {
    system.clockMasterCycle();
    REQUIRE(core.acceptanceRecordsThisCycle().size() == 0);
    REQUIRE(core.cop0Register(EECOP0Register::Count) == count);
  }
}

TEST_CASE(
  "EE CP0 timer waits for a younger A stage before entry",
  "[cp0-timer]")
{
  NekoSystem system;
  prepareTimer(&system);
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Status, TIMER_ENABLED_STATUS);
  core.setCOP0Register(EECOP0Register::Compare, 2);
  system.eeBus().write32(0, UINT32_C(0x70221c89));
  system.eeBus().write32(4, UINT32_C(0x24040001));

  system.clockMasterCycle();
  REQUIRE(
    core.lastIssueSelection().continuation ==
    EEIssueContinuation::YoungerAStageOneCycle);
  REQUIRE(core.generalRegister(4).low == 0);
  system.clockMasterCycle();
  REQUIRE(core.cop0Register(EECOP0Register::Count) == 2);
  REQUIRE(timerPending(core) == TIMER_PENDING);
  REQUIRE(core.pendingException() == EEException::None);
  REQUIRE(core.generalRegister(4).low == 1);

  system.clockMasterCycle();
  REQUIRE(core.cop0Register(EECOP0Register::Count) == 3);
  REQUIRE(core.pendingException() == EEException::Interrupt);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 8);
}

TEST_CASE("EE CP0 timer matches exactly across unsigned wrap", "[cp0-timer]")
{
  for (std::uint32_t compare : {UINT32_MAX, UINT32_C(0)})
  {
    NekoSystem system;
    prepareTimer(&system);
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Count, UINT32_MAX - 1);
    core.setCOP0Register(EECOP0Register::Compare, compare);
    system.clockMasterCycle();
    REQUIRE(core.cop0Register(EECOP0Register::Count) == UINT32_MAX);
    REQUIRE(timerPending(core) == (compare == 0 ? 0 : TIMER_PENDING));
    system.clockMasterCycle();
    REQUIRE(core.cop0Register(EECOP0Register::Count) == 0);
    REQUIRE(timerPending(core) == TIMER_PENDING);
    system.clockMasterCycle();
    REQUIRE(core.cop0Register(EECOP0Register::Count) == 1);
    REQUIRE(timerPending(core) == TIMER_PENDING);
  }
}

TEST_CASE(
  "EE CP0 timer guest reads observe the cycle entry tick",
  "[cp0-timer]")
{
  NekoSystem system;
  prepareTimer(&system);
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Count, UINT32_C(0x7fffffff));
  core.setCOP0Register(EECOP0Register::Compare, UINT32_C(0x81234567));
  system.eeBus().write32(0, cop0Transfer(0, 2, EECOP0Register::Count));
  system.eeBus().write32(4, cop0Transfer(0, 3, EECOP0Register::Count));
  system.eeBus().write32(8, cop0Transfer(0, 4, EECOP0Register::Compare));

  system.runMasterCycles(3);
  REQUIRE(core.generalRegister(2).low == UINT64_C(0xffffffff80000000));
  REQUIRE(core.generalRegister(3).low == UINT64_C(0xffffffff80000001));
  REQUIRE(core.generalRegister(4).low == UINT64_C(0xffffffff81234567));
  REQUIRE(core.cop0Register(EECOP0Register::Count) == UINT32_C(0x80000002));
}

TEST_CASE(
  "EE CP0 timer Count writes replace ticks without acknowledgment",
  "[cp0-timer]")
{
  for (bool pending : {false, true})
  {
    NekoSystem system;
    prepareTimer(&system);
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Count, 4);
    core.setCOP0Register(EECOP0Register::Compare, 17);
    core.setCOP0Register(EECOP0Register::Cause, pending ? TIMER_PENDING : 0);
    core.setGeneralRegister(1, {17, 0});
    system.eeBus().write32(0, cop0Transfer(4, 1, EECOP0Register::Count));

    system.clockMasterCycle();
    REQUIRE(core.cop0Register(EECOP0Register::Count) == 17);
    REQUIRE(timerPending(core) == (pending ? TIMER_PENDING : 0));
    system.clockMasterCycle();
    REQUIRE(core.cop0Register(EECOP0Register::Count) == 18);
    REQUIRE(timerPending(core) == (pending ? TIMER_PENDING : 0));
  }

  NekoSystem system;
  prepareTimer(&system);
  system.eeCore().setCOP0Register(EECOP0Register::Compare, 1);
  system.eeCore().setGeneralRegister(1, {100, 0});
  system.eeBus().write32(0, cop0Transfer(4, 1, EECOP0Register::Count));
  system.clockMasterCycle();
  REQUIRE(system.eeCore().cop0Register(EECOP0Register::Count) == 100);
  REQUIRE(timerPending(system.eeCore()) == TIMER_PENDING);
}

TEST_CASE(
  "EE CP0 timer Compare writes clear only pending at equal values",
  "[cp0-timer]")
{
  constexpr std::uint32_t otherCause =
    EECOP0Cause::BRANCH_DELAY |
    EECOP0Cause::COPROCESSOR_1 |
    (UINT32_C(9) << 2);
  for (std::uint32_t compare : {UINT32_C(1), UINT32_C(100)})
  {
    NekoSystem system;
    prepareTimer(&system);
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Compare, compare);
    core.setCOP0Register(EECOP0Register::Cause, otherCause);
    core.setGeneralRegister(1, {1, 0});
    system.eeBus().write32(0, cop0Transfer(4, 1, EECOP0Register::Compare));

    system.clockMasterCycle();
    REQUIRE(core.cop0Register(EECOP0Register::Count) == 1);
    REQUIRE(core.cop0Register(EECOP0Register::Compare) == 1);
    REQUIRE(core.cop0Register(EECOP0Register::Cause) == otherCause);
    system.clockMasterCycle();
    REQUIRE(core.cop0Register(EECOP0Register::Cause) == otherCause);
  }
}

TEST_CASE(
  "EE CP0 timer host setup does not replay guest effects",
  "[cp0-timer]")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Cause, TIMER_PENDING);
  core.setCOP0Register(EECOP0Register::Compare, 7);
  core.setCOP0Register(EECOP0Register::Count, 7);
  REQUIRE(timerPending(core) == TIMER_PENDING);
  core.setCOP0Register(EECOP0Register::Cause, 0);
  prepareTimer(&system);
  system.clockMasterCycle();
  REQUIRE(core.cop0Register(EECOP0Register::Count) == 8);
  REQUIRE(timerPending(core) == 0);
}

TEST_CASE("EE CP0 timer delivery obeys every interrupt gate", "[cp0-timer]")
{
  const std::uint32_t blockedStatuses[] = {
    TIMER_ENABLED_STATUS & ~EECOP0Status::INTERRUPT_ENABLE,
    TIMER_ENABLED_STATUS & ~EECOP0Status::MASTER_INTERRUPT_ENABLE,
    TIMER_ENABLED_STATUS & ~EECOP0Status::TIMER_MASK,
    TIMER_ENABLED_STATUS | EECOP0Status::EXCEPTION_LEVEL,
    TIMER_ENABLED_STATUS | EECOP0Status::ERROR_LEVEL
  };
  for (std::uint32_t status : blockedStatuses)
  {
    NekoSystem system;
    prepareTimer(&system);
    EECore &core = system.eeCore();
    core.setCOP0Register(EECOP0Register::Status, status);
    core.setCOP0Register(EECOP0Register::Compare, 1);
    system.clockMasterCycle();
    REQUIRE(core.pendingException() == EEException::None);
    REQUIRE(core.programCounter() == 4);
    REQUIRE(timerPending(core) == TIMER_PENDING);
    core.setCOP0Register(EECOP0Register::Status, TIMER_ENABLED_STATUS);
    system.clockMasterCycle();
    REQUIRE(core.pendingException() == EEException::Interrupt);
    REQUIRE(core.programCounter() == EEExceptionVector::INTERRUPT);
    REQUIRE(core.cop0Register(EECOP0Register::EPC) == 4);
    REQUIRE(timerPending(core) == TIMER_PENDING);
  }
}

TEST_CASE("EE CP0 timer can preempt a guest acknowledgment", "[cp0-timer]")
{
  NekoSystem system;
  prepareTimer(&system);
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Status, TIMER_ENABLED_STATUS);
  core.setCOP0Register(EECOP0Register::Compare, 1);
  core.setGeneralRegister(1, {100, 0});
  system.eeBus().write32(0, cop0Transfer(4, 1, EECOP0Register::Compare));
  system.clockMasterCycle();
  REQUIRE(core.pendingException() == EEException::Interrupt);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
  REQUIRE(core.cop0Register(EECOP0Register::Compare) == 1);
  REQUIRE(timerPending(core) == TIMER_PENDING);
}

TEST_CASE("EE CP0 timer reenters after ERET until acknowledged", "[cp0-timer]")
{
  NekoSystem system;
  prepareTimer(&system);
  EECore &core = system.eeCore();
  core.setCOP0Register(
    EECOP0Register::Status,
    TIMER_ENABLED_STATUS | EECOP0Status::EXCEPTION_LEVEL);
  core.setCOP0Register(EECOP0Register::Compare, 1);
  core.setCOP0Register(EECOP0Register::EPC, 0x100);
  system.eeBus().write32(0, UINT32_C(0x42000018));
  system.clockMasterCycle();
  REQUIRE(core.programCounter() == 0x100);
  REQUIRE(core.pendingException() == EEException::None);
  REQUIRE(timerPending(core) == TIMER_PENDING);
  system.clockMasterCycle();
  REQUIRE(core.pendingException() == EEException::Interrupt);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0x100);
}

TEST_CASE("EE CP0 timer enters after a complete branch group", "[cp0-timer]")
{
  NekoSystem system;
  prepareTimer(&system);
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Status, TIMER_ENABLED_STATUS);
  core.setCOP0Register(EECOP0Register::Compare, 2);
  system.eeBus().write32(0, UINT32_C(0x10000002));
  system.eeBus().write32(4, UINT32_C(0x34010001));
  system.clockMasterCycle();
  REQUIRE(core.generalRegister(1).low == 1);
  system.clockMasterCycle();
  REQUIRE(core.pendingException() == EEException::Interrupt);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 12);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Cause) &
      EECOP0Cause::BRANCH_DELAY) == 0);
}

TEST_CASE(
  "EE CP0 timer acknowledgment preserves external pending lines",
  "[cp0-timer]")
{
  NekoSystem system;
  prepareTimer(&system);
  EECore &core = system.eeCore();
  core.setCOP0Register(EECOP0Register::Compare, 1);
  system.interruptController().setSource(EEInterruptSource::VIF0, true);
  system.interruptController().toggleMask(
    EEInterruptSource::mask(EEInterruptSource::VIF0));
  system.eeBus().write32(EEMemoryMap::D_STAT, DMACStatus::CHANNEL_2_MASK);
  system.eeBus().write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
  system.eeBus().write32(EEMemoryMap::D2_QWC, 0);
  system.eeBus().write32(EEMemoryMap::D2_CHCR, GIFDMACChannelControl::START);
  core.setGeneralRegister(1, {100, 0});
  system.eeBus().write32(8, cop0Transfer(4, 1, EECOP0Register::Compare));
  const std::uint32_t external =
    EECOP0Cause::INTC_PENDING | EECOP0Cause::DMAC_PENDING;
  for (unsigned index = 0; index < 2; ++index)
  {
    system.clockMasterCycle();
    REQUIRE(
      core.cop0Register(EECOP0Register::Cause) ==
      (external | TIMER_PENDING));
  }
  system.clockMasterCycle();
  REQUIRE(core.cop0Register(EECOP0Register::Cause) == external);
}

TEST_CASE("EE CP0 timer preserves older COP1 fault priority", "[cp0-timer]")
{
  for (std::uint32_t opcode : {UINT32_C(0x31), UINT32_C(0x39)})
  {
    NekoSystem system;
    prepareTimer(&system);
    EECore &core = system.eeCore();
    core.setCOP0Register(
      EECOP0Register::Status,
      TIMER_ENABLED_STATUS | EECOP0Status::COP1_USABLE);
    core.setCOP0Register(EECOP0Register::Compare, 2);
    core.setGeneralRegister(
      1,
      {EEMemoryMap::KSEG0_BASE + EEMemoryMap::MAIN_MEMORY_SIZE, 0});
    core.setFloatingPointRegister(3, UINT32_C(0x12345678));
    system.eeBus().write32(
      0,
      (opcode << 26) | (UINT32_C(1) << 21) | (UINT32_C(3) << 16));

    system.runMasterCycles(4);
    REQUIRE(
      core.pendingException() ==
      (opcode == 0x31
        ? EEException::DataBusErrorLoad
        : EEException::DataBusErrorStore));
    REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
    REQUIRE(timerPending(core) == TIMER_PENDING);
    REQUIRE(core.cop0Register(EECOP0Register::Count) == 4);
    REQUIRE(core.floatingPointRegister(3) == UINT32_C(0x12345678));
  }
}

TEST_CASE(
  "EE CP0 timer resumes before and after match and acknowledgment",
  "[cp0-timer]")
{
  for (unsigned checkpointCycle = 0; checkpointCycle <= 2; ++checkpointCycle)
  {
    NekoSystem original;
    prepareTimer(&original);
    EECore &core = original.eeCore();
    core.setCOP0Register(EECOP0Register::Compare, 1);
    core.setGeneralRegister(1, {5, 0});
    original.eeBus().write32(4, cop0Transfer(4, 1, EECOP0Register::Compare));
    original.runMasterCycles(checkpointCycle);
    REQUIRE(timerPending(core) == (checkpointCycle == 1 ? TIMER_PENDING : 0));
    const std::uint64_t hash = core.stateHash();
    const auto checkpoint = original.saveState();
    REQUIRE(core.stateHash() == hash);
    NekoSystem restored;
    restored.loadState(checkpoint);
    REQUIRE(restored.saveState() == checkpoint);
    REQUIRE(restored.eeStateHash() == original.eeStateHash());

    for (unsigned cycle = checkpointCycle + 1; cycle <= 6; ++cycle)
    {
      original.clockMasterCycle();
      restored.clockMasterCycle();
      REQUIRE(restored.eeStateHash() == original.eeStateHash());
      REQUIRE(
        restored.eeCore().cop0Register(EECOP0Register::Count) == cycle);
      REQUIRE(
        timerPending(restored.eeCore()) ==
        (cycle == 1 || cycle >= 5 ? TIMER_PENDING : 0));
    }
    REQUIRE(restored.saveState() == original.saveState());
  }
}
