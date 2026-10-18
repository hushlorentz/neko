#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "catch.hpp"
#include "neko_system.hpp"
#include "save_state_mutation.hpp"

namespace
{
  using Events = std::vector<NekoTraceEvent>;
  using Subsystem = NekoTraceSubsystem;
  using Type = NekoTraceEventType;

  void requireEvents(const Events &actual, const Events &expected)
  {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
      CAPTURE(index);
      REQUIRE(actual[index].masterCycle == expected[index].masterCycle);
      REQUIRE(actual[index].subsystem == expected[index].subsystem);
      REQUIRE(actual[index].type == expected[index].type);
      REQUIRE(actual[index].value0 == expected[index].value0);
      REQUIRE(actual[index].value1 == expected[index].value1);
      REQUIRE(actual[index].value2 == expected[index].value2);
      REQUIRE(actual[index].value3 == expected[index].value3);
    }
    REQUIRE(nekoTraceHash(actual) == nekoTraceHash(expected));
  }

  Events subsystemEvents(const NekoSystem &system, Subsystem subsystem)
  {
    Events events;
    for (const auto &event : system.trace())
    {
      if (event.subsystem == subsystem)
      {
        events.push_back(event);
      }
    }
    return events;
  }

  void requireMachine(const NekoSystem &actual, const NekoSystem &expected)
  {
    REQUIRE(actual.masterClockScheduler().currentCycle() ==
      expected.masterClockScheduler().currentCycle());
    REQUIRE(actual.iopCore().elapsedCycles() == expected.iopCore().elapsedCycles());
    REQUIRE(actual.iopCore().retiredInstructions() ==
      expected.iopCore().retiredInstructions());
    REQUIRE(actual.iopCore().programCounter() == expected.iopCore().programCounter());
    REQUIRE(actual.iopCore().executionState() == expected.iopCore().executionState());
    REQUIRE(actual.iopCore().stopReason() == expected.iopCore().stopReason());
    REQUIRE(actual.iopStateHash() == expected.iopStateHash());
    REQUIRE(actual.eeStateHash() == expected.eeStateHash());
    const bool identicalState = actual.saveState() == expected.saveState();
    REQUIRE(identicalState);
  }

  void applyControl(NekoSystem &system, void (NekoSystem::*control)())
  {
    const auto before = system.saveState();
    const auto iopHash = system.iopStateHash();
    const auto eeHash = system.eeStateHash();
    (system.*control)();
    REQUIRE(system.iopStateHash() == iopHash);
    REQUIRE(system.eeStateHash() == eeHash);
    const bool identicalState = system.saveState() == before;
    REQUIRE(identicalState);
  }

  void instruction(NekoSystem &system, IOPAddress address, IOPWord word)
  {
    REQUIRE(system.iopBus().write32(address, word).status ==
      IOPBusStatus::Completed);
  }

  void prepare(NekoSystem &system)
  {
    const std::array<IOPWord, 16> program = {{
      0x3c018000, // lui r1, 0x8000
      0x8c220100, // lw r2, 0x100(r1)
      0x10000002, // beq r0, r0, 0x14
      0x8c230104, // lw r3, 0x104(r1), delay slot
      0xffffffff, // skipped reserved instruction
      0x24640001, // addiu r4, r3, 1, before the delayed load commits
      0xac230108, // sw r3, 0x108(r1)
      0x3c05bf80, // lui r5, 0xbf80
      0x8ca61104, // lw r6, timer 0 mode(r5)
      0xaca01070, // sw r0, I_STAT(r5)
      0x8ca71078, // lw r7, I_CTRL(r5)
      0,
      0x14000001, // bne r0, r0, 0x38, not taken
      0,
      0x0800000c, // j 0xa0000030
      0
    }};
    for (std::size_t index = 0; index < program.size(); ++index)
    {
      instruction(system, static_cast<IOPAddress>(4 * index), program[index]);
    }
    instruction(system, 0x100, 0x12345678);
    instruction(system, 0x104, 0x89abcdef);
    system.iopCore().startExecution(0xa0000000);
    system.iopTimers().writeTarget(0, 4);
    system.iopTimers().writeMode(0, 0x58);
    system.iopTimers().writeMode(1, 0x107);
    system.iopTimers().writeMode(2, 0x200);
    system.iopTimers().writeTarget(3, 2);
    system.iopTimers().writeMode(3, 0x1d8);
    system.iopTimers().writeMode(4, 0x6000);
    system.iopTimers().writeTarget(5, 3);
    system.iopTimers().writeMode(5, 0x50);
    system.iopInterruptController().writeMask(0x14010);
    system.iopInterruptController().writeControl(1);
    system.gsDisplay().configureTiming({23, 41, {7, 13, 1}});

    system.eeBus().write32(0, 0x24010007); // addiu r1, r0, 7
    system.eeBus().write32(4, 0xac410100); // sw r1, 0x100(r2)
    system.eeBus().write32(8, 0x10000001); // beq r0, r0, 0x10
    system.eeBus().write32(12, 0);
    system.eeBus().write32(16, 0x0000000c); // syscall
    system.eeCore().setGeneralRegister(2, {0xa0000000, 0});
    system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
    system.eeCore().startExecution(0xa0000000);
  }

  void requireBoundary(const NekoSystem &system, unsigned checkpoint)
  {
    const auto &core = system.iopCore();
    if (checkpoint == 9 || checkpoint == 25 || checkpoint == 57)
    {
      REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::Load);
      REQUIRE(core.delayedResultContinuation().destination ==
        (checkpoint == 9 ? 2 : checkpoint == 25 ? 3 : 6));
    }
    if (checkpoint == 17)
    {
      REQUIRE(core.branchContinuation().active);
      REQUIRE(core.branchContinuation().targetAddress == 0xa0000014);
    }
    if (checkpoint == 25)
    {
      REQUIRE(system.iopTimers().counter(0).pulseReleasePending);
      REQUIRE((system.iopInterruptController().status() & 0x10) != 0);
    }
    REQUIRE(system.iopTimers().counter(4).prescalerRemainder != 0);
    REQUIRE(system.iopTimerTiming().pixelPhase() != 0);
  }
}

TEST_CASE("IOP tracing and collection controls are observational through active execution",
  "[iop][trace][lifecycle]")
{
  NekoSystem plain;
  NekoSystem traced;
  NekoSystem toggled;
  prepare(plain);
  prepare(traced);
  prepare(toggled);
  traced.startTrace();
  toggled.startTrace();
  for (const unsigned checkpoint : {1u, 9u, 17u, 25u, 33u, 57u, 65u, 73u, 81u, 129u})
  {
    CAPTURE(checkpoint);
    const auto cycles = checkpoint - plain.masterClockScheduler().currentCycle();
    plain.runMasterCycles(cycles);
    traced.runMasterCycles(cycles);
    toggled.runMasterCycles(cycles);
    requireMachine(traced, plain);
    requireMachine(toggled, plain);
    if (checkpoint == 9 || checkpoint == 57)
    {
      applyControl(toggled, &NekoSystem::stopTrace);
      REQUIRE_FALSE(toggled.traceEnabled());
    }
    else if (checkpoint == 17 || checkpoint == 73 || checkpoint == 81)
    {
      applyControl(toggled, &NekoSystem::startTrace);
      REQUIRE(toggled.traceEnabled());
      REQUIRE(toggled.trace().empty());
    }
    else if (checkpoint == 25 || checkpoint == 65)
    {
      const auto enabled = toggled.traceEnabled();
      applyControl(toggled, &NekoSystem::clearTrace);
      REQUIRE(toggled.traceEnabled() == enabled);
      REQUIRE(toggled.trace().empty());
    }
  }
  REQUIRE(plain.trace().empty());
  REQUIRE_FALSE(traced.trace().empty());
  REQUIRE(traced.iopCore().generalRegister(2) == 0x12345678);
  REQUIRE(traced.iopCore().generalRegister(3) == 0x89abcdef);
  REQUIRE(traced.iopCore().generalRegister(4) == 1);
  REQUIRE(traced.iopBus().read32(0x108).value == 0x89abcdef);
  REQUIRE(traced.iopCore().generalRegister(7) == 1);
  REQUIRE(traced.iopInterruptController().control() == 0);
}

TEST_CASE("IOP trace controls retain only the requested collection window",
  "[iop][trace][lifecycle]")
{
  NekoSystem system;
  prepare(system);
  system.startTrace();
  system.runMasterCycles(9);
  const auto retained = system.trace();
  REQUIRE_FALSE(retained.empty());
  applyControl(system, &NekoSystem::stopTrace);
  applyControl(system, &NekoSystem::stopTrace);
  system.runMasterCycles(8);
  requireEvents(system.trace(), retained);
  applyControl(system, &NekoSystem::clearTrace);
  REQUIRE_FALSE(system.traceEnabled());
  REQUIRE(system.trace().empty());
  system.runMasterCycles(8);
  REQUIRE(system.trace().empty());

  NekoSystem reference;
  reference.loadState(system.saveState());
  applyControl(system, &NekoSystem::startTrace);
  reference.startTrace();
  system.runMasterCycles(7);
  reference.runMasterCycles(7);
  REQUIRE(subsystemEvents(system, Subsystem::IOP).empty());
  requireEvents(system.trace(), reference.trace());
  system.clockMasterCycle();
  reference.clockMasterCycle();
  requireEvents(system.trace(), reference.trace());
  requireMachine(system, reference);

  applyControl(system, &NekoSystem::clearTrace);
  REQUIRE(system.traceEnabled());
  REQUIRE(system.trace().empty());
  reference.startTrace();
  system.runMasterCycles(8);
  reference.runMasterCycles(8);
  requireEvents(system.trace(), reference.trace());
  applyControl(system, &NekoSystem::startTrace);
  REQUIRE(system.traceEnabled());
  REQUIRE(system.trace().empty());
  reference.clearTrace();
  system.runMasterCycles(24);
  reference.runMasterCycles(24);
  requireEvents(system.trace(), reference.trace());
  requireMachine(system, reference);
}

TEST_CASE("IOP traced reset discards collection and restarts deterministic execution",
  "[iop][trace][lifecycle]")
{
  NekoSystem system;
  prepare(system);
  system.startTrace();
  system.runMasterCycles(25);
  requireBoundary(system, 25);
  REQUIRE_FALSE(system.trace().empty());
  system.reset();
  NekoSystem fresh;
  REQUIRE_FALSE(system.traceEnabled());
  REQUIRE(system.trace().empty());
  requireMachine(system, fresh);
  REQUIRE(system.iopCore().executionState() == IOPExecutionState::Halted);
  REQUIRE_FALSE(system.iopCore().branchContinuation().active);
  REQUIRE(system.iopCore().delayedResultContinuation().source ==
    IOPDelayedResultSource::None);
  REQUIRE_FALSE(system.iopTimers().counter(0).pulseReleasePending);

  prepare(system);
  prepare(fresh);
  system.startTrace();
  fresh.startTrace();
  system.runMasterCycles(129);
  fresh.runMasterCycles(129);
  requireEvents(system.trace(), fresh.trace());
  requireMachine(system, fresh);
}

TEST_CASE("IOP trace continuation restores delayed loads branches and timer phases",
  "[iop][trace][lifecycle]")
{
  for (const unsigned checkpoint : {9u, 17u, 25u, 57u})
  {
    for (const bool sourceEnabled : {false, true})
    {
      for (const bool destinationEnabled : {false, true})
      {
        CAPTURE(checkpoint);
        CAPTURE(sourceEnabled);
        CAPTURE(destinationEnabled);
        NekoSystem original;
        prepare(original);
        if (sourceEnabled)
        {
          original.startTrace();
        }
        original.runMasterCycles(checkpoint);
        requireBoundary(original, checkpoint);
        const auto state = original.saveState();
        const auto sourceTrace = original.trace();

        NekoSystem restored;
        prepare(restored);
        restored.startTrace();
        restored.runMasterCycles(1);
        if (!destinationEnabled)
        {
          restored.stopTrace();
        }
        const auto destinationTrace = restored.trace();
        REQUIRE_FALSE(destinationTrace.empty());
        restored.loadState(state);
        REQUIRE(original.traceEnabled() == sourceEnabled);
        REQUIRE(restored.traceEnabled() == destinationEnabled);
        requireEvents(original.trace(), sourceTrace);
        requireEvents(restored.trace(), destinationTrace);
        requireBoundary(restored, checkpoint);
        requireMachine(restored, original);

        original.startTrace();
        original.clockMasterCycle();
        restored.clockMasterCycle();
        requireMachine(restored, original);
        REQUIRE(restored.trace().size() >= destinationTrace.size());
        requireEvents(
          Events(restored.trace().begin(),
            restored.trace().begin() + destinationTrace.size()),
          destinationTrace);
        if (!destinationEnabled)
        {
          requireEvents(restored.trace(), destinationTrace);
        }
        else
        {
          requireEvents(
            Events(restored.trace().begin() + destinationTrace.size(),
              restored.trace().end()),
            original.trace());
        }
        for (const auto &event : subsystemEvents(restored, Subsystem::IOP))
        {
          REQUIRE(event.masterCycle <= 1);
        }

        original.startTrace();
        restored.startTrace();
        for (const unsigned cycle : {checkpoint + 7, checkpoint + 8, 129u})
        {
          const auto count = cycle - original.masterClockScheduler().currentCycle();
          original.runMasterCycles(count);
          restored.runMasterCycles(count);
          requireEvents(restored.trace(), original.trace());
          requireMachine(restored, original);
        }
        for (const auto &event : restored.trace())
        {
          REQUIRE(event.masterCycle > checkpoint + 1);
        }
      }
    }
  }
}

TEST_CASE("IOP state restoration retains the legacy halted EE snapshot baseline",
  "[iop][trace][lifecycle]")
{
  NekoSystem original;
  prepare(original);
  original.runMasterCycles(25);
  original.eeCore().haltExecution();
  const auto state = original.saveState();

  NekoSystem restored;
  prepare(restored);
  restored.startTrace();
  restored.runMasterCycles(1);
  const auto previousEE = subsystemEvents(restored, Subsystem::EE);
  REQUIRE_FALSE(previousEE.empty());
  REQUIRE(previousEE.back().type == Type::StateSnapshot);
  restored.loadState(state);
  requireMachine(restored, original);
  const auto eeHash = restored.eeCore().stateHash();
  REQUIRE(previousEE.back().value0 != eeHash);
  original.clockMasterCycle();
  restored.clockMasterCycle();
  requireMachine(restored, original);
  auto expectedEE = previousEE;
  expectedEE.push_back({
    26, Subsystem::EE, Type::StateSnapshot,
    eeHash,
    restored.eeCore().elapsedCycles(),
    restored.eeCore().programCounter(),
    (static_cast<std::uint64_t>(
      restored.eeCore().cop0Register(EECOP0Register::Status)) << 32) |
      restored.eeCore().cop0Register(EECOP0Register::Cause)
  });
  requireEvents(subsystemEvents(restored, Subsystem::EE), expectedEE);
  REQUIRE(restored.eeCore().stateHash() == eeHash);
  original.clockMasterCycle();
  restored.clockMasterCycle();
  requireEvents(subsystemEvents(restored, Subsystem::EE), expectedEE);
  requireMachine(restored, original);
}

TEST_CASE("IOP failed state loads preserve active collection and future traces",
  "[iop][trace][lifecycle]")
{
  for (const bool enabled : {false, true})
  {
    CAPTURE(enabled);
    NekoSystem original;
    NekoSystem reference;
    prepare(original);
    prepare(reference);
    original.startTrace();
    reference.startTrace();
    original.runMasterCycles(25);
    reference.runMasterCycles(25);
    if (!enabled)
    {
      original.stopTrace();
      reference.stopTrace();
    }
    const auto before = original.saveState();
    const auto trace = original.trace();
    auto truncated = before;
    truncated.pop_back();
    REQUIRE_THROWS_AS(original.loadState(truncated), std::invalid_argument);
    REQUIRE(original.traceEnabled() == enabled);
    requireEvents(original.trace(), trace);
    requireMachine(original, reference);

    SaveStateMutation invalid(before);
    invalid.writeScalar("system.iop.timers.counters[4].prescalerRemainder", 256);
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(original.loadState(invalid), std::invalid_argument);
    REQUIRE(original.traceEnabled() == enabled);
    requireEvents(original.trace(), trace);
    requireBoundary(original, 25);
    requireMachine(original, reference);
    original.runMasterCycles(104);
    reference.runMasterCycles(104);
    requireEvents(original.trace(), reference.trace());
    requireMachine(original, reference);
  }
}

TEST_CASE("IOP trace collection remains observational at admitted interrupts",
  "[iop][trace][lifecycle]")
{
  NekoSystem traced;
  NekoSystem plain;
  for (auto *system : {&traced, &plain})
  {
    instruction(*system, 0, 0x34010401);
    instruction(*system, 4, 0x40816000);
    instruction(*system, 8, 0x24020007);
    instruction(*system, 0x80, 0x8c030100);
    instruction(*system, 0x100, 0x12345678);
    system->iopCore().startExecution(0xa0000000);
    system->iopTimers().writeTarget(0, 3);
    system->iopTimers().writeMode(0, 0x10);
    system->iopInterruptController().writeMask(0x10);
    system->iopInterruptController().writeControl(1);
  }
  traced.startTrace();
  traced.runMasterCycles(17);
  plain.runMasterCycles(17);
  REQUIRE(traced.iopCore().exception().kind == IOPException::Interrupt);
  REQUIRE(traced.iopCore().exceptionProgramCounter() == 0xa0000008);
  REQUIRE(traced.iopCore().generalRegister(2) == 0);
  requireMachine(traced, plain);
  const auto events = subsystemEvents(traced, Subsystem::IOP);
  REQUIRE(events.size() == 8);
  REQUIRE(events[6].type == Type::InterruptDelivered);
  REQUIRE(events[7].type == Type::ExceptionEntered);
  const auto state = traced.saveState();
  NekoSystem restored;
  restored.loadState(state);
  traced.clearTrace();
  restored.startTrace();
  traced.runMasterCycles(16);
  plain.runMasterCycles(16);
  restored.runMasterCycles(16);
  requireEvents(traced.trace(), restored.trace());
  requireMachine(traced, plain);
  requireMachine(restored, plain);
  REQUIRE(traced.iopCore().generalRegister(3) == 0x12345678);
}

TEST_CASE("IOP collection preserves complete legacy EE event encodings",
  "[iop][trace][lifecycle]")
{
  NekoSystem concurrent;
  NekoSystem eeOnly;
  prepare(concurrent);
  prepare(eeOnly);
  eeOnly.iopCore().haltExecution();
  concurrent.startTrace();
  eeOnly.startTrace();
  concurrent.runMasterCycles(129);
  eeOnly.runMasterCycles(129);
  const auto legacy = subsystemEvents(eeOnly, Subsystem::EE);
  REQUIRE_FALSE(legacy.empty());
  requireEvents(subsystemEvents(concurrent, Subsystem::EE), legacy);
  REQUIRE(concurrent.eeStateHash() == eeOnly.eeStateHash());
  bool sawStore = false;
  bool sawBranch = false;
  bool sawException = false;
  for (const auto &event : legacy)
  {
    if (event.type == Type::MemoryAccess &&
        (event.value3 & NekoEETraceMemory::WRITE) != 0)
    {
      sawStore = true;
      REQUIRE(event.value0 == 0xa0000100);
      REQUIRE(event.value1 == 7);
      REQUIRE(event.value2 == 0);
      REQUIRE((event.value3 & NekoEETraceMemory::LEGACY_MASK) == 0x304);
    }
    if (event.type == Type::BranchScheduled)
    {
      sawBranch = true;
      REQUIRE(event.value0 == 0xa0000008);
      REQUIRE(event.value1 == 0xa0000010);
      REQUIRE(event.value2 == NekoEETraceBranch::TAKEN);
    }
    if (event.type == Type::ExceptionEntered)
    {
      sawException = true;
      REQUIRE(event.value0 == static_cast<std::uint8_t>(EEException::SystemCall));
    }
  }
  REQUIRE(sawStore);
  REQUIRE(sawBranch);
  REQUIRE(sawException);
  REQUIRE_FALSE(subsystemEvents(concurrent, Subsystem::IOP).empty());
  REQUIRE(subsystemEvents(eeOnly, Subsystem::IOP).empty());
}
