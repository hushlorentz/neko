#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "catch.hpp"
#include "neko_system.hpp"

struct IOPDeviceObservationTestAccess
{
  static void enable(IOPTimers &timers, NekoObservationOrder *order = nullptr)
  {
    timers.enableObservations(order);
  }
  static void enable(IOPInterruptController &interrupts,
    NekoObservationOrder *order = nullptr)
  {
    interrupts.enableObservations(order);
  }
  static std::size_t count(const IOPTimers &timers)
  {
    return timers.observationCount;
  }
  static std::size_t count(const IOPInterruptController &interrupts)
  {
    return interrupts.observationCount;
  }
  static bool armed(const IOPTimers &timers)
  {
    return timers.observationsEnabled;
  }
  static bool armed(const IOPInterruptController &interrupts)
  {
    return interrupts.observationsEnabled;
  }
  static NekoObservationOrder *order(const IOPTimers &timers)
  {
    return timers.observationOrder;
  }
  static NekoObservationOrder *order(const IOPInterruptController &interrupts)
  {
    return interrupts.observationOrder;
  }
  static std::size_t timerCapacity() { return IOPTimers::OBSERVATION_CAPACITY; }
  static std::size_t interruptCapacity()
  {
    return IOPInterruptController::OBSERVATION_CAPACITY;
  }
};

namespace
{
  using Subsystem = NekoTraceSubsystem;
  using Type = NekoTraceEventType;
  namespace Timer = NekoIOPTraceTimer;

  std::vector<NekoTraceEvent> iopTrace(const NekoSystem &system)
  {
    std::vector<NekoTraceEvent> events;
    for (const auto &event : system.trace())
    {
      if (event.subsystem == Subsystem::IOP ||
          event.subsystem == Subsystem::IOPTimers ||
          event.subsystem == Subsystem::IOPInterruptController)
      {
        events.push_back(event);
      }
    }
    return events;
  }

  void instruction(NekoSystem &system, IOPAddress address, IOPWord word)
  {
    REQUIRE(system.iopBus().write32(address, word).status ==
      IOPBusStatus::Completed);
  }

  void prepareAccess(NekoSystem &system, std::uint16_t base, IOPWord access)
  {
    instruction(system, 0, 0x3c01bf80); // lui r1, 0xbf80
    instruction(system, 4, 0x34210000 | base); // ori r1, r1, base
    instruction(system, 8, access);
    system.iopCore().startExecution(0xa0000000);
    system.runMasterCycles(16);
    REQUIRE(system.iopCore().programCounter() == 0xa0000008);
  }

  void armTimer(NekoSystem &system, IOPWord mode = 0x10)
  {
    system.iopTimers().writeTarget(0, 1);
    system.iopTimers().writeMode(0, mode);
    system.iopInterruptController().writeMask(0x10);
    system.iopInterruptController().writeControl(1);
  }

  void requireIdentity(const NekoTraceEvent &event, Subsystem subsystem,
    Type type, std::uint64_t cycle)
  {
    REQUIRE(event.subsystem == subsystem);
    REQUIRE(event.type == type);
    REQUIRE(event.masterCycle == cycle);
  }
}

TEST_CASE("IOP system publishes fetch issue and retirement only on CPU clocks",
  "[iop][trace]")
{
  NekoSystem system;
  instruction(system, 0, 0);
  system.iopCore().startExecution(0xa0000000);
  system.startTrace();
  system.runMasterCycles(8);
  const auto events = iopTrace(system);
  REQUIRE(events.size() == 3);
  requireIdentity(events[0], Subsystem::IOP, Type::MemoryAccess, 1);
  REQUIRE(events[0].value0 == 0xa0000000);
  REQUIRE(events[0].value1 == 0);
  REQUIRE(events[0].value2 == 0);
  REQUIRE(events[0].value3 == 0x30204);
  requireIdentity(events[1], Subsystem::IOP, Type::InstructionIssued, 1);
  REQUIRE(events[1].value0 == 0xa0000000);
  REQUIRE(events[1].value1 == 0);
  REQUIRE(events[1].value2 == 0);
  REQUIRE(events[1].value3 == 0);
  requireIdentity(events[2], Subsystem::IOP, Type::InstructionRetired, 1);
  REQUIRE(events[2].value0 == 0xa0000000);
  REQUIRE(events[2].value1 == 0xa0000004);
  REQUIRE(events[2].value2 == 1);
  REQUIRE(events[2].value3 == 1);
}

TEST_CASE("IOP trace retains timer assertion and same-cycle mode acknowledgement",
  "[iop][trace]")
{
  NekoSystem system;
  prepareAccess(system, 0x1100, 0x8c220004); // lw r2, Mode(r1)
  armTimer(system);
  system.startTrace();
  system.clockMasterCycle();
  const auto events = iopTrace(system);
  REQUIRE(events.size() == 7);
  requireIdentity(events[0], Subsystem::IOPTimers, Type::TimerEvent, 17);
  REQUIRE(events[0].value0 ==
    (Timer::SYSTEM_PULSE | Timer::TARGET_REACHED | Timer::INTERRUPT_REQUESTED));
  REQUIRE(events[0].value1 == UINT64_C(0x41000000000));
  REQUIRE(events[0].value2 ==
    (UINT64_C(0x81000000001) | Timer::ONE_SHOT_CONSUMED |
      Timer::PULSE_RELEASE_PENDING));
  REQUIRE(events[0].value3 == UINT64_C(0x100000001));
  requireIdentity(events[1], Subsystem::IOPInterruptController,
    Type::InterruptChanged, 17);
  REQUIRE(events[1].value0 == 0x10);
  REQUIRE(events[1].value1 == 0x10);
  REQUIRE(events[1].value2 == 1);
  REQUIRE(events[1].value3 == 1);
  requireIdentity(events[2], Subsystem::IOP, Type::MemoryAccess, 17);
  requireIdentity(events[3], Subsystem::IOP, Type::InstructionIssued, 17);
  requireIdentity(events[4], Subsystem::IOPTimers, Type::TimerEvent, 17);
  REQUIRE(events[4].value0 == (Timer::MODE_ACKNOWLEDGED |
    (std::uint64_t(Timer::Register::Mode) << Timer::REGISTER_SHIFT)));
  REQUIRE(events[4].value1 == events[0].value2);
  REQUIRE(events[4].value2 == UINT64_C(0x41000000001));
  REQUIRE(events[4].value3 == UINT64_C(0x100000001));
  requireIdentity(events[5], Subsystem::IOP, Type::MemoryAccess, 17);
  REQUIRE(events[5].value0 == 0xbf801104);
  REQUIRE(events[5].value1 == 0x810);
  REQUIRE(events[5].value3 == UINT64_C(0x1f80110400030604));
  requireIdentity(events[6], Subsystem::IOP, Type::InstructionRetired, 17);
  REQUIRE(system.iopTimers().counter(0).mode == 0x410);
  REQUIRE(system.iopInterruptController().status() == 0x10);
}

TEST_CASE("IOP trace retains INTC assertion and CPU acknowledgement in one cycle",
  "[iop][trace]")
{
  NekoSystem system;
  prepareAccess(system, 0x1070, 0xac200000); // sw r0, I_STAT(r1)
  armTimer(system);
  system.startTrace();
  system.clockMasterCycle();
  const auto events = iopTrace(system);
  REQUIRE(events.size() == 7);
  requireIdentity(events[0], Subsystem::IOPTimers, Type::TimerEvent, 17);
  requireIdentity(events[1], Subsystem::IOPInterruptController,
    Type::InterruptChanged, 17);
  REQUIRE(events[1].value0 == 0x10);
  REQUIRE(events[1].value3 == 1);
  requireIdentity(events[2], Subsystem::IOP, Type::MemoryAccess, 17);
  requireIdentity(events[3], Subsystem::IOP, Type::InstructionIssued, 17);
  requireIdentity(events[4], Subsystem::IOPInterruptController,
    Type::InterruptChanged, 17);
  REQUIRE(events[4].value0 == 0);
  REQUIRE(events[4].value1 == 0x10);
  REQUIRE(events[4].value2 == 1);
  REQUIRE(events[4].value3 == 0);
  requireIdentity(events[5], Subsystem::IOP, Type::MemoryAccess, 17);
  REQUIRE(events[5].value0 == 0xbf801070);
  REQUIRE(events[5].value1 == 0);
  REQUIRE(events[5].value3 == UINT64_C(0x1f801070003f0b04));
  requireIdentity(events[6], Subsystem::IOP, Type::InstructionRetired, 17);
  REQUIRE(system.iopInterruptController().status() == 0);
}

TEST_CASE("IOP trace records INTC read-to-clear without reading twice",
  "[iop][trace]")
{
  NekoSystem system;
  prepareAccess(system, 0x1070, 0x8c220008); // lw r2, I_CTRL(r1)
  system.iopInterruptController().assertSource(IOPInterruptSource::VBlankStart);
  system.iopInterruptController().writeMask(1);
  system.iopInterruptController().writeControl(1);
  system.startTrace();
  system.clockMasterCycle();
  const auto events = iopTrace(system);
  REQUIRE(events.size() == 5);
  requireIdentity(events[2], Subsystem::IOPInterruptController,
    Type::InterruptChanged, 17);
  REQUIRE(events[2].value0 == 1);
  REQUIRE(events[2].value1 == 1);
  REQUIRE(events[2].value2 == 0);
  REQUIRE(events[2].value3 == 0);
  requireIdentity(events[3], Subsystem::IOP, Type::MemoryAccess, 17);
  REQUIRE(events[3].value1 == 1);
  REQUIRE(system.iopCore().delayedResultContinuation().value == 1);
  REQUIRE(system.iopInterruptController().control() == 0);
}

TEST_CASE("IOP trace places instruction-fetch MMIO side effects before the fetch",
  "[iop][trace]")
{
  NekoSystem system;
  system.iopInterruptController().writeControl(1);
  system.iopCore().startExecution(0xbf801078);
  system.startTrace();
  system.clockMasterCycle();
  const auto events = iopTrace(system);
  REQUIRE(events.size() == 4);
  requireIdentity(events[0], Subsystem::IOPInterruptController,
    Type::InterruptChanged, 1);
  REQUIRE(events[0].value2 == 0);
  requireIdentity(events[1], Subsystem::IOP, Type::MemoryAccess, 1);
  REQUIRE(events[1].value0 == 0xbf801078);
  REQUIRE(events[1].value1 == 1);
  requireIdentity(events[2], Subsystem::IOP, Type::InstructionIssued, 1);
  requireIdentity(events[3], Subsystem::IOP, Type::ExceptionEntered, 1);
  REQUIRE(events[3].value0 == 10);
}

TEST_CASE("IOP trace identifies all guest timer register writes even if unchanged",
  "[iop][trace]")
{
  for (const auto offset : {0u, 4u, 8u})
  {
    CAPTURE(offset);
    NekoSystem system;
    prepareAccess(system, 0x1100, 0xac200000 | offset);
    system.startTrace();
    system.clockMasterCycle();
    const auto events = iopTrace(system);
    REQUIRE(events.size() == 5);
    requireIdentity(events[2], Subsystem::IOPTimers, Type::TimerEvent, 17);
    const auto reg = offset == 0 ? Timer::Register::Count :
      offset == 4 ? Timer::Register::Mode : Timer::Register::Target;
    REQUIRE(events[2].value0 == (Timer::REGISTER_WRITTEN |
      (std::uint64_t(reg) << Timer::REGISTER_SHIFT)));
    requireIdentity(events[3], Subsystem::IOP, Type::MemoryAccess, 17);
    requireIdentity(events[4], Subsystem::IOP, Type::InstructionRetired, 17);
  }
}

TEST_CASE("IOP trace publishes timer and INTC before admitted interrupt without fetch",
  "[iop][trace]")
{
  NekoSystem system;
  instruction(system, 0, 0x34010401); // ori r1, r0, IEc | IM2
  instruction(system, 4, 0x40816000); // mtc0 r1, Status
  system.iopCore().startExecution(0xa0000000);
  system.runMasterCycles(16);
  armTimer(system);
  system.startTrace();
  system.clockMasterCycle();
  const auto events = iopTrace(system);
  REQUIRE(events.size() == 4);
  requireIdentity(events[0], Subsystem::IOPTimers, Type::TimerEvent, 17);
  requireIdentity(events[1], Subsystem::IOPInterruptController,
    Type::InterruptChanged, 17);
  requireIdentity(events[2], Subsystem::IOP, Type::InterruptDelivered, 17);
  REQUIRE(events[2].value0 == 0xa0000008);
  REQUIRE(events[2].value1 == (IOPCOP0Status::TLB_SHUTDOWN | 0x401));
  REQUIRE(events[2].value2 == 0x400);
  REQUIRE(events[2].value3 == 0x80000080);
  requireIdentity(events[3], Subsystem::IOP, Type::ExceptionEntered, 17);
  REQUIRE(events[3].value0 == 0);
  REQUIRE(events[3].value1 == 0xa0000008);
  REQUIRE(events[3].value2 == 0x80000080);
  REQUIRE(events[3].value3 == 0x400);
}

TEST_CASE("IOP trace separates old pulse release from renewed same-sample request",
  "[iop][trace]")
{
  NekoSystem system;
  armTimer(system, 0x58); // Reset on target, repeat pulse interrupt.
  system.clockMasterCycle();
  system.startTrace();
  system.runMasterCycles(8);
  const auto events = iopTrace(system);
  REQUIRE(events.size() == 2);
  requireIdentity(events[0], Subsystem::IOPTimers, Type::TimerEvent, 9);
  REQUIRE(events[0].value0 == (Timer::SYSTEM_PULSE | Timer::PULSE_RELEASED));
  requireIdentity(events[1], Subsystem::IOPTimers, Type::TimerEvent, 9);
  REQUIRE(events[1].value0 ==
    (Timer::SYSTEM_PULSE | Timer::TARGET_REACHED | Timer::INTERRUPT_REQUESTED));
  REQUIRE(events[0].value2 == events[1].value1);
  REQUIRE(events[0].value1 == events[1].value2);
}

TEST_CASE("IOP trace observes routed gate edges but omits ordinary counter increments",
  "[iop][trace]")
{
  NekoSystem system;
  system.gsDisplay().configureTiming({3, 5, {3, 7, 2}});
  system.startTrace();
  system.runMasterCycles(3);
  const auto events = iopTrace(system);
  REQUIRE(events.size() == 3);
  requireIdentity(events[0], Subsystem::IOPTimers, Type::TimerEvent, 2);
  REQUIRE(events[0].value0 == (Timer::GATE_CHANGED | Timer::HBLANK_AFTER));
  for (std::size_t index = 1; index <= 2; ++index)
  {
    requireIdentity(events[index], Subsystem::IOPTimers, Type::TimerEvent, 3);
    REQUIRE(events[index].value0 ==
      ((index == 1 ? 1u : 3u) | Timer::GATE_CHANGED |
        Timer::HBLANK_BEFORE | Timer::HBLANK_AFTER | Timer::VBLANK_AFTER));
  }
}

TEST_CASE("IOP publication excludes host setup and preserves machine persistence",
  "[iop][trace]")
{
  NekoSystem traced;
  NekoSystem plain;
  for (auto *system : {&traced, &plain})
  {
    prepareAccess(*system, 0x1070, 0xac200000);
    armTimer(*system);
  }
  traced.startTrace();
  for (unsigned int index = 0; index < 40; ++index)
  {
    traced.iopInterruptController().writeMask(index);
    traced.iopTimers().writeTarget(5, index);
    plain.iopInterruptController().writeMask(index);
    plain.iopTimers().writeTarget(5, index);
  }
  traced.clockMasterCycle();
  plain.clockMasterCycle();
  REQUIRE(traced.iopStateHash() == plain.iopStateHash());
  REQUIRE(traced.saveState() == plain.saveState());
  for (const auto &event : iopTrace(traced))
  {
    if (event.subsystem == Subsystem::IOPTimers)
    {
      REQUIRE((event.value0 & Timer::REGISTER_WRITTEN) == 0);
    }
  }
  traced.clearTrace();
  traced.runMasterCycles(7);
  REQUIRE(iopTrace(traced).empty());
}

TEST_CASE("IOP trace packs branch decisions delay-slot flags and committed traps",
  "[iop][trace]")
{
  for (const bool taken : {false, true})
  {
    NekoSystem system;
    instruction(system, 0, taken ? 0x10000001 : 0x14000001);
    instruction(system, 4, 0x0000000c); // syscall in the delay slot
    system.iopCore().startExecution(0xa0000000);
    system.startTrace();
    system.runMasterCycles(9);
    const auto events = iopTrace(system);
    REQUIRE(events.size() == 7);
    requireIdentity(events[2], Subsystem::IOP, Type::BranchScheduled, 1);
    REQUIRE(events[2].value0 == 0xa0000000);
    REQUIRE(events[2].value1 == 0xa0000008);
    REQUIRE(events[2].value2 == (taken ? 1 : 0));
    REQUIRE(events[2].value3 == 0);
    requireIdentity(events[5], Subsystem::IOP, Type::InstructionIssued, 9);
    REQUIRE(events[5].value3 == NekoIOPTraceInstruction::DELAY_SLOT);
    requireIdentity(events[6], Subsystem::IOP, Type::ExceptionEntered, 9);
    REQUIRE(events[6].value0 == 8);
    REQUIRE(events[6].value1 == 0xa0000000);
    REQUIRE(events[6].value2 == IOPExceptionVector::BOOT_GENERAL);
    REQUIRE(events[6].value3 == 0x80000020);
  }
}

TEST_CASE("IOP trace packs failed fetch outcome address validity and cache route",
  "[iop][trace]")
{
  for (const auto address : {IOPAddress(0xa0000001), IOPAddress(0xbe000000)})
  {
    NekoSystem system;
    system.iopCore().startExecution(address);
    system.startTrace();
    system.clockMasterCycle();
    const auto events = iopTrace(system);
    REQUIRE(events.size() == 2);
    requireIdentity(events[0], Subsystem::IOP, Type::MemoryAccess, 1);
    REQUIRE(events[0].value0 == address);
    REQUIRE(events[0].value1 == 0);
    const auto expected = address == 0xa0000001
      ? UINT64_C(0x100032004) : UINT64_C(0x1e00000000034004);
    REQUIRE(events[0].value3 == expected);
    requireIdentity(events[1], Subsystem::IOP, Type::ExceptionEntered, 1);
    REQUIRE(events[1].value0 == (address == 0xa0000001 ? 4 : 6));
  }
}

TEST_CASE("IOP trace packs cache-selected loads and partial containing-word stores",
  "[iop][trace]")
{
  NekoSystem system;
  instruction(system, 0, 0x3c018000); // lui r1, 0x8000
  instruction(system, 4, 0x8c220100); // lw r2, 0x100(r1)
  instruction(system, 8, 0);
  instruction(system, 12, 0xb8220101); // swr r2, 0x101(r1)
  instruction(system, 0x100, 0x89abcdef);
  system.iopCore().startExecution(0xa0000000);
  system.runMasterCycles(8);
  system.startTrace();
  system.runMasterCycles(17);
  const auto events = iopTrace(system);
  REQUIRE(events.size() == 11);
  requireIdentity(events[2], Subsystem::IOP, Type::MemoryAccess, 9);
  REQUIRE(events[2].value0 == 0x80000100);
  REQUIRE(events[2].value1 == 0x89abcdef);
  REQUIRE(events[2].value3 == UINT64_C(0x10000028604));
  requireIdentity(events[9], Subsystem::IOP, Type::MemoryAccess, 25);
  REQUIRE(events[9].value0 == 0x80000100);
  REQUIRE(events[9].value1 == 0xabcdef00);
  REQUIRE(events[9].value3 == UINT64_C(0x100003a8b04));
}

TEST_CASE("IOP trace packs guest halts without synthesizing host transitions",
  "[iop][trace]")
{
  NekoSystem system;
  instruction(system, 0, 0x18); // mult r0, r0
  instruction(system, 4, 0x18); // undefined unread-result overwrite
  system.iopCore().startExecution(0xa0000000);
  system.runMasterCycles(8);
  system.startTrace();
  system.clockMasterCycle();
  const auto events = iopTrace(system);
  REQUIRE(events.size() == 3);
  requireIdentity(events[2], Subsystem::IOP, Type::StateChanged, 9);
  REQUIRE(events[2].value0 ==
    std::uint8_t(NekoIOPTraceState::Execution::Running));
  REQUIRE(events[2].value1 ==
    std::uint8_t(NekoIOPTraceState::Execution::Halted));
  REQUIRE(events[2].value2 ==
    std::uint8_t(NekoIOPTraceState::StopReason::UndefinedOperation));
  REQUIRE(events[2].value3 == 0xa0000004);
  system.clearTrace();
  system.runMasterCycles(8);
  REQUIRE(iopTrace(system).empty());
}

TEST_CASE("IOP trace retains six simultaneous timer requests and their INTC order",
  "[iop][trace]")
{
  NekoSystem system;
  constexpr std::array<IOPWord, 6> causes = {{0x10, 0x20, 0x40, 0x4000, 0x8000, 0x10000}};
  for (std::size_t index = 0; index < 6; ++index)
  {
    system.iopTimers().writeTarget(index, 0);
    system.iopTimers().writeMode(index, 0x30);
    system.iopTimers().writeCount(index, index < 3 ? 0xffff : 0xffffffff);
  }
  system.startTrace();
  system.clockMasterCycle();
  const auto events = iopTrace(system);
  REQUIRE(events.size() == 12);
  IOPWord status = 0;
  for (std::size_t index = 0; index < 6; ++index)
  {
    requireIdentity(events[index], Subsystem::IOPTimers, Type::TimerEvent, 1);
    REQUIRE(events[index].value0 == (index | Timer::TARGET_REACHED |
      Timer::OVERFLOW_REACHED | Timer::INTERRUPT_REQUESTED | Timer::SYSTEM_PULSE));
    status |= causes[index];
    requireIdentity(events[index + 6], Subsystem::IOPInterruptController,
      Type::InterruptChanged, 1);
    REQUIRE(events[index + 6].value0 == status);
    REQUIRE(events[index + 6].value3 == 0);
  }
}

TEST_CASE("IOP device observation capacities retain every sample and CPU access",
  "[iop][trace]")
{
  using Access = IOPDeviceObservationTestAccess;
  IOPTimers timers;
  IOPInterruptController interrupts;
  for (std::size_t index = 0; index < 6; ++index)
  {
    timers.writeTarget(index, 1);
    timers.writeMode(index, 0x58);
  }
  timers.advance({true, false, {}});
  NekoObservationOrder order;
  Access::enable(timers, &order);
  Access::enable(interrupts, &order);
  const auto requests = timers.advance({true, false, {}});
  REQUIRE(requests.count == 6);
  REQUIRE(Access::count(timers) == 12);
  for (std::size_t index = 0; index < requests.count; ++index)
  {
    interrupts.assertSource(requests.sources[index]);
  }
  timers.readMode(0);
  timers.writeCount(0, 0);
  interrupts.writeMask(0x1c070);
  interrupts.writeControl(1);
  REQUIRE(Access::count(timers) == Access::timerCapacity());
  REQUIRE(Access::count(interrupts) == Access::interruptCapacity());
  REQUIRE(order.next == Access::timerCapacity() + Access::interruptCapacity());
  REQUIRE_THROWS_AS(timers.readMode(0), std::logic_error);
  REQUIRE_THROWS_AS(interrupts.writeControl(0), std::logic_error);
}

TEST_CASE("IOP cycle observation scope disarms device journals on execution failure",
  "[iop][trace]")
{
  using Access = IOPDeviceObservationTestAccess;
  NekoSystem system;
  instruction(system, 0, 0);
  system.iopCore().startExecution(0xa0000000);
  system.iopCore().attachBus(nullptr);
  armTimer(system);
  system.startTrace();
  REQUIRE_THROWS_AS(system.clockMasterCycle(), std::logic_error);
  REQUIRE_FALSE(Access::armed(system.iopTimers()));
  REQUIRE_FALSE(Access::armed(system.iopInterruptController()));
  REQUIRE(Access::order(system.iopTimers()) == nullptr);
  REQUIRE(Access::order(system.iopInterruptController()) == nullptr);
  REQUIRE(Access::count(system.iopTimers()) == 0);
  REQUIRE(Access::count(system.iopInterruptController()) == 0);
  system.iopCore().attachBus(&system.iopBus());
  system.runMasterCycles(8);
  REQUIRE(system.iopCore().retiredInstructions() == 1);
}

TEST_CASE("IOP device observation journals remain transactional nonpersistent state",
  "[iop][trace]")
{
  using Access = IOPDeviceObservationTestAccess;
  NekoSystem system;
  const auto state = system.saveState();
  NekoObservationOrder order;
  Access::enable(system.iopTimers(), &order);
  Access::enable(system.iopInterruptController(), &order);
  system.iopTimers().writeCount(0, 1);
  system.iopInterruptController().writeMask(1);
  const auto hash = system.iopStateHash();
  auto invalid = state;
  invalid.pop_back();
  REQUIRE_THROWS_AS(system.loadState(invalid), std::invalid_argument);
  REQUIRE(system.iopStateHash() == hash);
  REQUIRE(Access::count(system.iopTimers()) == 1);
  REQUIRE(Access::count(system.iopInterruptController()) == 1);
  system.loadState(state);
  REQUIRE(system.saveState() == state);
  REQUIRE(Access::count(system.iopTimers()) == 0);
  REQUIRE(Access::count(system.iopInterruptController()) == 0);
  REQUIRE(Access::armed(system.iopTimers()));
  REQUIRE(Access::armed(system.iopInterruptController()));
  REQUIRE(Access::order(system.iopTimers()) == &order);
  REQUIRE(Access::order(system.iopInterruptController()) == &order);
}
