#include <cstdint>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "gif_dmac_channel.hpp"
#include "neko_system.hpp"

namespace
{
  constexpr std::uint32_t INTC_ENABLED_STATUS =
    EECOP0Status::INTERRUPT_ENABLE |
    EECOP0Status::MASTER_INTERRUPT_ENABLE |
    EECOP0Status::INTC_MASK;
  constexpr std::uint32_t DMAC_ENABLED_STATUS =
    EECOP0Status::INTERRUPT_ENABLE |
    EECOP0Status::MASTER_INTERRUPT_ENABLE |
    EECOP0Status::DMAC_MASK;

  void assertINTCLine(NekoSystem *system)
  {
    system->interruptController().setSource(
      EEInterruptSource::VIF0,
      true);
    system->interruptController().toggleMask(
      EEInterruptSource::mask(EEInterruptSource::VIF0));
  }
}

TEST_CASE("EE INTC pending state is visible while the core is halted")
{
  NekoSystem system;
  assertINTCLine(&system);

  system.clockMasterCycle();

  REQUIRE(
    (system.eeCore().cop0Register(EECOP0Register::Cause) &
      EECOP0Cause::INTC_PENDING) != 0);
  REQUIRE(
    system.eeCore().executionState() ==
    EEExecutionState::Halted);
  REQUIRE(system.eeCore().pendingException() == EEException::None);
}

TEST_CASE("EE INTC delivery obeys all COP0 enable gates")
{
  const std::uint32_t blockedStatuses[] = {
    INTC_ENABLED_STATUS &
      ~EECOP0Status::INTERRUPT_ENABLE,
    INTC_ENABLED_STATUS &
      ~EECOP0Status::MASTER_INTERRUPT_ENABLE,
    INTC_ENABLED_STATUS &
      ~EECOP0Status::INTC_MASK,
    INTC_ENABLED_STATUS |
      EECOP0Status::EXCEPTION_LEVEL,
    INTC_ENABLED_STATUS |
      EECOP0Status::ERROR_LEVEL
  };

  for (std::uint32_t status : blockedStatuses)
  {
    NekoSystem system;
    assertINTCLine(&system);
    system.eeCore().setCOP0Register(
      EECOP0Register::Status,
      status);
    system.eeBus().write32(0, 0);
    system.eeCore().startExecution(0);

    system.clockMasterCycle();

    REQUIRE(system.eeCore().pendingException() == EEException::None);
    REQUIRE(system.eeCore().programCounter() == 4);
    REQUIRE(
      (system.eeCore().cop0Register(EECOP0Register::Cause) &
        EECOP0Cause::INTC_PENDING) != 0);
  }
}

TEST_CASE("EE takes an enabled INTC line before the next instruction")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  assertINTCLine(&system);
  core.setCOP0Register(
    EECOP0Register::Status,
    INTC_ENABLED_STATUS);
  system.eeBus().write32(0, 0);
  core.startExecution(0);

  const EEExecutionResult entry = system.stepEEInstruction(1);

  REQUIRE(entry.instructions == 0);
  REQUIRE_FALSE(entry.cycleLimitReached);
  REQUIRE(core.pendingException() == EEException::Interrupt);
  REQUIRE(core.programCounter() == EEExceptionVector::INTERRUPT);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Cause) &
      EECOP0Cause::INTC_PENDING) != 0);

  system.interruptController().acknowledge(
    EEInterruptSource::mask(EEInterruptSource::VIF0));
  system.eeBus().write32(EEExceptionVector::INTERRUPT, 0);
  system.clockMasterCycle();

  REQUIRE(
    (core.cop0Register(EECOP0Register::Cause) &
      EECOP0Cause::INTC_PENDING) == 0);
  REQUIRE(
    core.programCounter() ==
    EEExceptionVector::INTERRUPT + 4);
}

TEST_CASE("EE interrupt delivery preserves a pending branch restart")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  core.setCOP0Register(
    EECOP0Register::Status,
    INTC_ENABLED_STATUS);
  system.eeBus().write32(
    0,
    (UINT32_C(0x04) << 26) | 2);
  system.eeBus().write32(
    4,
    (UINT32_C(0x0d) << 26) |
      (UINT32_C(1) << 16) |
      1);
  core.startExecution(0);
  system.clockMasterCycle();
  assertINTCLine(&system);

  system.clockMasterCycle();

  REQUIRE(core.pendingException() == EEException::Interrupt);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 0);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Cause) &
      EECOP0Cause::BRANCH_DELAY) != 0);
  REQUIRE(core.generalRegister(1).low == 0);
}

TEST_CASE("EE observes DMAC completion at the next instruction boundary")
{
  NekoSystem system;
  EECore &core = system.eeCore();
  EEBus &bus = system.eeBus();
  core.setCOP0Register(
    EECOP0Register::Status,
    DMAC_ENABLED_STATUS);
  bus.write32(
    EEMemoryMap::D_STAT,
    GIFDMACStatus::CHANNEL_2_MASK);
  bus.write32(
    EEMemoryMap::D_CTRL,
    GIFDMACControl::DMA_ENABLE);
  bus.write32(EEMemoryMap::D2_QWC, 0);
  bus.write32(
    EEMemoryMap::D2_CHCR,
    GIFDMACChannelControl::START);
  bus.write32(0, 0);
  bus.write32(4, 0);
  core.startExecution(0);

  system.clockMasterCycle();

  REQUIRE(core.pendingException() == EEException::None);
  REQUIRE(core.programCounter() == 4);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Cause) &
      EECOP0Cause::DMAC_PENDING) != 0);

  system.clockMasterCycle();

  REQUIRE(core.pendingException() == EEException::Interrupt);
  REQUIRE(core.cop0Register(EECOP0Register::EPC) == 4);
  REQUIRE(core.programCounter() == EEExceptionVector::INTERRUPT);
  REQUIRE(
    (core.cop0Register(EECOP0Register::Cause) &
      EECOP0Cause::DMAC_PENDING) != 0);
  REQUIRE(core.lastInstructionAddress() == 0);
}

TEST_CASE("EE interrupt lines survive save-state restoration")
{
  NekoSystem original;
  assertINTCLine(&original);
  original.eeCore().setCOP0Register(
    EECOP0Register::Status,
    INTC_ENABLED_STATUS);
  original.eeBus().write32(0, 0);
  original.eeCore().startExecution(0);

  NekoSystem restored;
  restored.loadState(original.saveState());
  restored.clockMasterCycle();

  REQUIRE(
    restored.eeCore().pendingException() ==
    EEException::Interrupt);
  REQUIRE(
    restored.eeCore().programCounter() ==
    EEExceptionVector::INTERRUPT);
  REQUIRE(restored.eeCore().cop0Register(EECOP0Register::EPC) == 0);
}
