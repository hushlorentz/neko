#include <array>
#include <cstddef>
#include <cstdint>

#include "catch.hpp"
#include "iop_timers.hpp"
#include "neko_system.hpp"
#include "sif_interrupt_router.hpp"

namespace
{
  constexpr std::uint32_t SBUS = 2;
  constexpr std::uint32_t EE_CTRL = EEMemoryMap::SIF_BASE + 0x40;
  constexpr std::uint32_t REQUEST_IOP = 1u << 18;
  constexpr std::uint32_t EE_INTERRUPT_STATUS =
    EECOP0Status::INTERRUPT_ENABLE |
    EECOP0Status::MASTER_INTERRUPT_ENABLE |
    EECOP0Status::INTC_MASK;

  struct Fixture
  {
    SIF sif;
    NekoSystem system;
    SIFInterruptRouter router{
      system.interruptController(), system.iopInterruptController()};

    Fixture()
    {
      system.eeBus().attachSIF(&sif, &router);
      system.iopBus().attachSIF(&sif, &router);
    }
  };

  void requestIOP(Fixture &fixture)
  {
    REQUIRE(fixture.system.eeBus().writeData32(EE_CTRL, REQUEST_IOP));
  }

  void requestEE(Fixture &fixture)
  {
    REQUIRE(fixture.system.iopBus().write32(IOPBus::SIF_DOORBELL, 2).status ==
      IOPBusStatus::Completed);
  }

  void iopProgram(NekoSystem &system, const std::array<IOPWord, 5> &program)
  {
    for (std::size_t index = 0; index < program.size(); ++index)
    {
      REQUIRE(system.iopBus().write32(
        static_cast<IOPAddress>(4 * index), program[index]).status ==
        IOPBusStatus::Completed);
    }
    system.iopCore().startExecution(0xa0000000);
  }
}

TEST_CASE("SIF SBUS requests latch the destination with both CPUs halted",
  "[sif][interrupt]")
{
  Fixture fixture;
  auto &system = fixture.system;
  REQUIRE(EEInterruptSource::SBUS == 1);
  requestIOP(fixture);
  REQUIRE(system.iopInterruptController().status() == SBUS);
  REQUIRE(system.iopInterruptController().mask() == 0);
  REQUIRE(system.iopInterruptController().control() == 0);
  REQUIRE_FALSE(system.iopInterruptController().interruptPending());
  REQUIRE(system.interruptController().status() == 0);
  requestEE(fixture);
  REQUIRE(system.interruptController().status() == SBUS);
  REQUIRE(system.interruptController().mask() == 0);
  REQUIRE_FALSE(system.interruptController().interruptPending());
  REQUIRE(system.masterClockScheduler().currentCycle() == 0);
  REQUIRE(system.eeCore().elapsedCycles() == 0);
  REQUIRE(system.iopCore().elapsedCycles() == 0);
  REQUIRE(system.eeCore().pendingException() == EEException::None);
  REQUIRE(system.iopCore().exception().kind == IOPException::None);
  REQUIRE(system.eeCore().executionState() == EEExecutionState::Halted);
  REQUIRE(system.iopCore().executionState() == IOPExecutionState::Halted);
  REQUIRE(fixture.sif.control() == 0);
  REQUIRE(fixture.sif.iopDoorbell() == 2);
}

TEST_CASE("SIF SBUS latching is independent of INTC mask and control gates",
  "[sif][interrupt]")
{
  for (bool masked : {false, true})
  {
    for (std::uint32_t control : {0u, 1u})
    {
      CAPTURE(masked);
      CAPTURE(control);
      Fixture fixture;
      auto &ee = fixture.system.interruptController();
      auto &iop = fixture.system.iopInterruptController();
      ee.toggleMask(masked ? 0 : SBUS);
      iop.writeMask(masked ? 0 : SBUS);
      iop.writeControl(control);
      requestIOP(fixture);
      requestEE(fixture);
      REQUIRE(ee.status() == SBUS);
      REQUIRE(iop.status() == SBUS);
      REQUIRE(ee.interruptPending() == !masked);
      REQUIRE(iop.interruptPending() == (!masked && control != 0));
      REQUIRE(iop.control() == control);
      REQUIRE(ee.mask() == (masked ? 0 : SBUS));
      REQUIRE(iop.mask() == (masked ? 0 : SBUS));
      REQUIRE(fixture.system.eeCore().pendingException() == EEException::None);
      REQUIRE(fixture.system.iopCore().exception().kind == IOPException::None);
    }
  }
}

TEST_CASE("SIF SBUS requests coalesce and identical writes reassert after acknowledgement",
  "[sif][interrupt]")
{
  Fixture fixture;
  auto &system = fixture.system;
  for (unsigned int repeat = 0; repeat < 3; ++repeat)
  {
    requestIOP(fixture);
    requestIOP(fixture);
    requestEE(fixture);
    requestEE(fixture);
    REQUIRE(system.interruptController().status() == SBUS);
    REQUIRE(system.iopInterruptController().status() == SBUS);
    REQUIRE(system.eeBus().writeData32(EEMemoryMap::INTC_STAT, SBUS));
    REQUIRE(system.iopBus().write32(IOPBus::INTC_STAT, ~SBUS).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.interruptController().status() == 0);
    REQUIRE(system.iopInterruptController().status() == 0);
    system.runMasterCycles(8);
    REQUIRE(system.interruptController().status() == 0);
    REQUIRE(system.iopInterruptController().status() == 0);
    REQUIRE(system.eeBus().read32(EE_CTRL) == 0xf0000102);
    REQUIRE(system.iopBus().read32(IOPBus::SIF_DOORBELL).value == 2);
  }
}

TEST_CASE("SIF SBUS acknowledgement belongs only to the destination INTC",
  "[sif][interrupt]")
{
  Fixture fixture;
  auto &system = fixture.system;
  REQUIRE(system.eeBus().writeData32(EEMemoryMap::SIF_BASE + 0x20, 0xfedcba98));
  REQUIRE(system.iopBus().write32(IOPBus::SIF_BASE + 0x30, 0x87654321).status ==
    IOPBusStatus::Completed);
  requestIOP(fixture);
  requestEE(fixture);
  REQUIRE(system.eeBus().writeData32(EEMemoryMap::SIF_BASE + 0x30, 0x21));
  REQUIRE(system.iopBus().write32(IOPBus::SIF_BASE + 0x20, 0x98).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.eeBus().writeData32(EE_CTRL, 0));
  REQUIRE(system.iopBus().write32(IOPBus::SIF_DOORBELL, 0).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.interruptController().status() == SBUS);
  REQUIRE(system.iopInterruptController().status() == SBUS);
  system.interruptController().setSource(EEInterruptSource::VIF0, true);
  system.iopInterruptController().assertSource(IOPInterruptSource::DMAC);
  REQUIRE(system.eeBus().writeData32(EEMemoryMap::INTC_STAT, SBUS));
  REQUIRE(system.iopBus().write32(IOPBus::INTC_STAT, ~SBUS).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.interruptController().status() ==
    EEInterruptSource::mask(EEInterruptSource::VIF0));
  REQUIRE(system.iopInterruptController().status() == 8);
  REQUIRE(fixture.sif.readEE(SIFRegister::MSFLAG) == 0xfedcba00);
  REQUIRE(fixture.sif.readIOP(SIFRegister::SMFLAG) == 0x87654300);
}

TEST_CASE("SIF mailbox flag and empty effects cannot request SBUS",
  "[sif][interrupt]")
{
  Fixture fixture;
  auto &system = fixture.system;
  for (std::uint32_t offset : {0u, 0x10u, 0x20u, 0x30u})
  {
    REQUIRE(system.eeBus().writeData32(EEMemoryMap::SIF_BASE + offset, 0xffffffff));
    REQUIRE(system.iopBus().write32(IOPBus::SIF_BASE + offset, 0xffffffff).status ==
      IOPBusStatus::Completed);
  }
  REQUIRE(system.eeBus().writeData32(EE_CTRL, 0x100));
  REQUIRE(system.iopBus().write32(IOPBus::SIF_BASE + 0x40, 0xf0).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.iopBus().write32(IOPBus::SIF_DOORBELL, 0xfffffffd).status ==
    IOPBusStatus::Completed);
  fixture.router.requestSIFSignal(SIFSignal::None);
  REQUIRE(system.interruptController().status() == 0);
  REQUIRE(system.iopInterruptController().status() == 0);
}

TEST_CASE("SIF SBUS rejection cannot publish either interrupt",
  "[sif][interrupt]")
{
  Fixture fixture;
  auto &system = fixture.system;
  REQUIRE(system.eeBus().writeData32(EE_CTRL, 0x100));
  REQUIRE_FALSE(system.eeBus().writeData32(EE_CTRL, 0xc0000));
  REQUIRE_FALSE(system.eeBus().writeData64(EE_CTRL, REQUEST_IOP));
  REQUIRE_FALSE(system.eeBus().writeData16(EE_CTRL, 0xffff));
  REQUIRE(system.iopBus().write16(IOPBus::SIF_DOORBELL, 2).status ==
    IOPBusStatus::Unmapped);
  REQUIRE(system.iopBus().writeMasked32(IOPBus::SIF_DOORBELL, 2, 1).status ==
    IOPBusStatus::Unmapped);
  REQUIRE(fixture.sif.control() == 0x100);
  REQUIRE(fixture.sif.iopDoorbell() == 0);
  REQUIRE(system.interruptController().status() == 0);
  REQUIRE(system.iopInterruptController().status() == 0);
}

TEST_CASE("SIF SBUS and DMA completion have independent status and acknowledgement",
  "[sif][interrupt]")
{
  Fixture fixture;
  auto &system = fixture.system;
  auto &dmac = system.dmacController();
  dmac.signalChannelCompletion(DMACStatus::CHANNEL_2);
  dmac.writeStatus(DMACStatus::CHANNEL_2_MASK);
  const auto dmaStatus = dmac.status();
  requestIOP(fixture);
  requestEE(fixture);
  REQUIRE(dmac.status() == dmaStatus);
  REQUIRE(dmac.interruptPending());
  REQUIRE(system.eeBus().writeData32(EEMemoryMap::INTC_STAT, SBUS));
  REQUIRE(system.iopBus().write32(IOPBus::INTC_STAT, ~SBUS).status ==
    IOPBusStatus::Completed);
  REQUIRE(dmac.status() == dmaStatus);
  requestIOP(fixture);
  requestEE(fixture);
  dmac.writeStatus(DMACStatus::CHANNEL_2);
  REQUIRE_FALSE(dmac.interruptPending());
  REQUIRE(system.interruptController().status() == SBUS);
  REQUIRE(system.iopInterruptController().status() == SBUS);
}

TEST_CASE("SIF SBUS reaches EE admission without bypassing CPU enable gates",
  "[sif][interrupt]")
{
  Fixture fixture;
  auto &system = fixture.system;
  system.interruptController().toggleMask(SBUS);
  system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
  system.eeCore().startExecution(0xa0000100);
  requestEE(fixture);
  system.clockMasterCycle();
  REQUIRE(system.eeCore().pendingException() == EEException::None);
  REQUIRE(system.eeCore().programCounter() == 0xa0000104);
  REQUIRE((system.eeCore().cop0Register(EECOP0Register::Cause) &
    EECOP0Cause::INTC_PENDING) != 0);
  system.eeCore().setCOP0Register(EECOP0Register::Status, EE_INTERRUPT_STATUS);
  REQUIRE(system.eeCore().pendingException() == EEException::None);
  system.clockMasterCycle();
  REQUIRE(system.eeCore().pendingException() == EEException::Interrupt);
  REQUIRE(system.eeCore().cop0Register(EECOP0Register::EPC) == 0xa0000104);
  REQUIRE(system.interruptController().status() == SBUS);
}

TEST_CASE("SIF SBUS reaches IOP admission without bypassing CPU enable gates",
  "[sif][interrupt]")
{
  Fixture fixture;
  auto &system = fixture.system;
  system.iopInterruptController().writeMask(SBUS);
  system.iopInterruptController().writeControl(1);
  iopProgram(system, {{0x34010401, 0x40816000, 0x24420001, 0, 0}});
  requestIOP(fixture);
  system.runMasterCycles(9);
  REQUIRE(system.iopCore().exception().kind == IOPException::None);
  REQUIRE(system.iopCore().retiredInstructions() == 2);
  system.runMasterCycles(7);
  REQUIRE(system.iopCore().exception().kind == IOPException::None);
  system.clockMasterCycle();
  REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
  REQUIRE(system.iopCore().exceptionProgramCounter() == 0xa0000008);
  REQUIRE(system.iopCore().retiredInstructions() == 2);
  REQUIRE(system.iopCore().generalRegister(2) == 0);
  REQUIRE(system.iopInterruptController().status() == SBUS);
}

TEST_CASE("SIF EE guest request is visible before same-cycle IOP admission",
  "[sif][interrupt][timing]")
{
  Fixture fixture;
  auto &system = fixture.system;
  system.iopInterruptController().writeMask(SBUS);
  system.iopInterruptController().writeControl(1);
  iopProgram(system, {{0x34010401, 0x40816000, 0x24420001, 0, 0}});
  system.runMasterCycles(9);
  system.eeBus().write32(0, 0xac220000);
  system.eeCore().setGeneralRegister(1, {0xb000f240, 0});
  system.eeCore().setGeneralRegister(2, {REQUEST_IOP, 0});
  system.runMasterCycles(7);
  REQUIRE(system.masterClockScheduler().currentCycle() == 16);
  REQUIRE(system.iopInterruptController().status() == 0);
  REQUIRE(system.iopCore().exception().kind == IOPException::None);
  REQUIRE(system.iopCore().retiredInstructions() == 2);
  system.eeCore().startExecution(0xa0000000);
  system.clockMasterCycle();
  REQUIRE(system.iopInterruptController().status() == SBUS);
  REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
  REQUIRE(system.iopCore().exceptionProgramCounter() == 0xa0000008);
}

TEST_CASE("SIF IOP guest request waits for the next EE admission boundary",
  "[sif][interrupt][timing]")
{
  Fixture fixture;
  auto &system = fixture.system;
  iopProgram(system, {{0x3c01bf80, 0x34211450, 0x34020002, 0, 0xac220000}});
  system.runMasterCycles(25);
  REQUIRE(system.iopCore().retiredInstructions() == 4);
  system.interruptController().toggleMask(SBUS);
  system.eeCore().setCOP0Register(EECOP0Register::Status, EE_INTERRUPT_STATUS);
  system.eeCore().startExecution(0xa0000100);
  system.runMasterCycles(7);
  REQUIRE(system.masterClockScheduler().currentCycle() == 32);
  const auto before = system.eeCore().programCounter();
  system.clockMasterCycle();
  REQUIRE(system.iopCore().retiredInstructions() == 5);
  REQUIRE(system.interruptController().status() == SBUS);
  REQUIRE(system.interruptController().interruptPending());
  REQUIRE(system.eeCore().pendingException() == EEException::None);
  REQUIRE(system.eeCore().programCounter() == before + 4);
  REQUIRE((system.eeCore().cop0Register(EECOP0Register::Cause) &
    EECOP0Cause::INTC_PENDING) == 0);
  system.clockMasterCycle();
  REQUIRE(system.eeCore().pendingException() == EEException::Interrupt);
  REQUIRE(system.eeCore().cop0Register(EECOP0Register::EPC) == before + 4);
}

TEST_CASE("SIF SBUS and all IOP timer requests fit the traced cycle budget",
  "[sif][interrupt][timing][trace]")
{
  Fixture fixture;
  auto &system = fixture.system;
  iopProgram(system, {{0x3c01bf80, 0x34211070, 0x3c02ffff, 0x3442fffd,
    0xac220000}});
  system.runMasterCycles(32);
  REQUIRE(system.iopCore().retiredInstructions() == 4);
  REQUIRE(system.iopInterruptController().status() == 0);
  for (std::size_t index = 0; index < IOPTimers::COUNTER_COUNT; ++index)
  {
    system.iopTimers().writeTarget(index, 1);
    system.iopTimers().writeMode(index, IOPTimerMode::INTERRUPT_ON_TARGET);
  }
  system.eeBus().write32(0, 0xac220000);
  system.eeCore().setGeneralRegister(1, {0xb000f240, 0});
  system.eeCore().setGeneralRegister(2, {REQUEST_IOP, 0});
  system.eeCore().startExecution(0xa0000000);
  system.startTrace();
  system.clockMasterCycle();

  const std::array<std::uint32_t, 8> expected = {{
    2, 0x12, 0x32, 0x72, 0x4072, 0xc072, 0x1c072, 0x1c070}};
  std::size_t interruptEvents = 0;
  for (const auto &event : system.trace())
  {
    if (event.subsystem != NekoTraceSubsystem::IOPInterruptController ||
        event.type != NekoTraceEventType::InterruptChanged)
    {
      continue;
    }
    REQUIRE(interruptEvents < expected.size());
    REQUIRE(event.masterCycle == 33);
    REQUIRE(event.value0 == expected[interruptEvents++]);
    REQUIRE(event.value1 == 0);
    REQUIRE(event.value2 == 0);
    REQUIRE(event.value3 == 0);
  }
  REQUIRE(interruptEvents == expected.size());
  REQUIRE(system.iopInterruptController().status() == expected.back());
  REQUIRE(system.iopCore().retiredInstructions() == 5);
}
