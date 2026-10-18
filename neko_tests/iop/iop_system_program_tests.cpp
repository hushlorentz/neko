#include <algorithm>
#include <array>
#include <vector>

#include "catch.hpp"
#include "iop_semantic_fixtures.hpp"
#include "iop_trace_test_utils.hpp"

namespace
{
  using Fixture = IOPSemanticFixtures::Fixture;
  using Op = IOPOperation;
  using Subsystem = NekoTraceSubsystem;
  using Type = NekoTraceEventType;
  using IOPTraceTest::Events;
  using IOPTraceTest::requireEvents;
  using IOPTraceTest::requireMachine;
  using IOPTraceTest::subsystemEvents;

  enum class ProgramKind
  {
    ReservedAfterLoad,
    OverflowInSlot,
    TimerService
  };

  struct Scenario
  {
    ProgramKind kind;
    Fixture image;
    unsigned endCycle;
    std::vector<unsigned> checkpoints;
    std::vector<IOPAddress> retiredPCs;
    Events entries;
  };

  Fixture sharedFixture(IOPProgramOutcome outcome, IOPException exception)
  {
    const auto &fixtures = IOPSemanticFixtures::all();
    const auto found = std::find_if(fixtures.begin(), fixtures.end(),
      [outcome, exception](const Fixture &fixture)
      {
        return fixture.expected.outcome == outcome &&
          fixture.expected.exception.kind == exception;
      });
    REQUIRE(found != fixtures.end());
    return *found;
  }

  Scenario handledException(IOPException exception)
  {
    Scenario scenario;
    scenario.image = sharedFixture(IOPProgramOutcome::Exception, exception);
    auto &image = scenario.image;
    const IOPAddress continuation = image.code.back().address;
    image.config.entryPoint = 0xf00;
    image.config.returnAddress = 0x1100;
    image.code.insert(image.code.end(), {
      {0xf00, 0x40806000, Op::MoveToCOP0}, // mtc0 r0, Status: RAM vector
      {0xf04, 0x08000400, Op::Jump}, // j 0x1000
      {0xf08, 0, Op::Nop},
      {0x80, 0x401a6800, Op::MoveFromCOP0}, // mfc0 r26, Cause
      {0x84, 0x401b7000, Op::MoveFromCOP0}, // mfc0 r27, EPC
      {0x88, 0xac1a2040, Op::StoreWord}, // sw r26, 0x2040(r0)
      {0x8c, 0xac1b2044, Op::StoreWord}, // sw r27, 0x2044(r0)
      {0x90, 0x07400004, Op::BranchLessThanZero}, // bltz r26, 0xa4: Cause.BD
      {0x94, 0, Op::Nop},
      {0x98, 0x277b0004, Op::AddImmediateUnsigned}, // addiu r27, r27, 4
      {0x9c, 0x10000002, Op::BranchEqual}, // beq r0, r0, 0xa8
      {0xa0, 0, Op::Nop},
      {0xa4, 0x277b0008, Op::AddImmediateUnsigned}, // addiu r27, r27, 8
      {0xa8, 0x03600008, Op::JumpRegister}, // jr r27
      {0xac, 0x42000010, Op::ReturnFromException}, // rfe
      {continuation + 4, 0xac022048, Op::StoreWord}, // sw r2, 0x2048(r0)
      {continuation + 8, 0x03e00008, Op::JumpRegister}, // jr r31
      {continuation + 12, 0, Op::Nop}
    });
    image.initialMemory.insert(image.initialMemory.end(), {
      {0x203c, 0xcafebabe}, {0x2040, 0xaaaaaaaa}, {0x2044, 0xbbbbbbbb},
      {0x2048, 0xcccccccc}, {0x204c, 0xdeadbeef}
    });
    const bool delaySlot = exception == IOPException::ArithmeticOverflow;
    const IOPWord cause = delaySlot ? 0x80000030 : 0x28;
    image.name = delaySlot ? "handled branch-slot overflow" : "handled reserved decode after load";
    scenario.kind = delaySlot ? ProgramKind::OverflowInSlot : ProgramKind::ReservedAfterLoad;
    scenario.endCycle = delaySlot ? 153 : 161;
    scenario.checkpoints = delaySlot
      ? std::vector<unsigned>{41, 49, 65, 113, 121}
      : std::vector<unsigned>{33, 41, 57, 121, 129};
    auto &expected = image.expected;
    expected.instructions = delaySlot ? 19 : 20;
    expected.cycles = delaySlot ? 20 : 21;
    expected.masterCycles = delaySlot ? 160 : 168;
    expected.outcome = IOPProgramOutcome::Completed;
    expected.programCounter = image.config.returnAddress;
    expected.exception = {};
    expected.status = 0x00200000;
    expected.cause = cause;
    expected.registers.insert(expected.registers.end(), {
      {2, 7}, {26, cause}, {27, continuation}
    });
    expected.changedMemory = {{0x2040, cause}, {0x2044, 0x1008}, {0x2048, 7}};
    scenario.retiredPCs = delaySlot
      ? std::vector<IOPAddress>{
          0xf00, 0xf04, 0xf08, 0x1000, 0x1004, 0x1008,
          0x80000080, 0x80000084, 0x80000088, 0x8000008c,
          0x80000090, 0x80000094, 0x800000a4, 0x800000a8, 0x800000ac,
          0x1010, 0x1014, 0x1018, 0x101c}
      : std::vector<IOPAddress>{
          0xf00, 0xf04, 0xf08, 0x1000, 0x1004,
          0x80000080, 0x80000084, 0x80000088, 0x8000008c,
          0x80000090, 0x80000094, 0x80000098, 0x8000009c, 0x800000a0,
          0x800000a8, 0x800000ac, 0x100c, 0x1010, 0x1014, 0x1018};
    scenario.entries = {{
      delaySlot ? 49u : 41u, Subsystem::IOP, Type::ExceptionEntered,
      delaySlot ? 12u : 10u, 0x1008, 0x80000080, cause
    }};
    return scenario;
  }

  Scenario timerService()
  {
    Scenario scenario;
    scenario.kind = ProgramKind::TimerService;
    const auto &fixtures = IOPSemanticFixtures::all();
    const auto arithmetic = std::find_if(fixtures.begin(), fixtures.end(),
      [](const Fixture &fixture)
      {
        return std::any_of(fixture.code.begin(), fixture.code.end(),
          [](const IOPSemanticFixtures::Instruction &instruction)
          {
            return instruction.operation == Op::Nor;
          });
      });
    REQUIRE(arithmetic != fixtures.end());
    scenario.image = *arithmetic;
    auto &image = scenario.image;
    image.name = "guest-configured dual timer service followed by arithmetic";
    image.config.entryPoint = 0xf00;
    image.config.returnAddress = 0x1100;
    image.code.insert(image.code.end(), {
      {0xf00, 0x3c14bf80, Op::LoadUpperImmediate}, // lui r20, 0xbf80
      {0xf04, 0x36941070, Op::OrImmediate}, // ori r20, r20, 0x1070: INTC
      {0xf08, 0x3c15bf80, Op::LoadUpperImmediate}, // lui r21, 0xbf80
      {0xf0c, 0x36b51100, Op::OrImmediate}, // ori r21, r21, 0x1100: timer 0
      {0xf10, 0x3c16bf80, Op::LoadUpperImmediate}, // lui r22, 0xbf80
      {0xf14, 0x36d614a0, Op::OrImmediate}, // ori r22, r22, 0x14a0: timer 5
      {0xf18, 0x3c170001, Op::LoadUpperImmediate}, // lui r23, 1
      {0xf1c, 0x36f70010, Op::OrImmediate}, // ori r23, r23, 0x10
      {0xf20, 0xae970004, Op::StoreWord}, // sw r23, I_MASK(r20)
      {0xf24, 0x24180001, Op::AddImmediateUnsigned}, // addiu r24, r0, 1
      {0xf28, 0xaeb80008, Op::StoreWord}, // sw r24, timer 0 target(r21)
      {0xf2c, 0xaed80008, Op::StoreWord}, // sw r24, timer 5 target(r22)
      {0xf30, 0xae980008, Op::StoreWord}, // sw r24, I_CTRL(r20)
      {0xf34, 0x24190010, Op::AddImmediateUnsigned}, // addiu r25, r0, 0x10
      {0xf38, 0xaeb90004, Op::StoreWord}, // sw r25, timer 0 mode(r21)
      {0xf3c, 0xaed90004, Op::StoreWord}, // sw r25, timer 5 mode(r22)
      {0xf40, 0x24010401, Op::AddImmediateUnsigned}, // addiu r1, r0, IEc | IM2
      {0xf44, 0x40816000, Op::MoveToCOP0}, // mtc0 r1, Status
      {0xf48, 0x08000400, Op::Jump}, // j 0x1000, interrupted before fetch
      {0xf4c, 0, Op::Nop},
      {0x80, 0x8eba0004, Op::LoadWord}, // lw r26, timer 0 mode(r21)
      {0x84, 0x8edb0004, Op::LoadWord}, // lw r27, timer 5 mode(r22)
      {0x88, 0xac1a2050, Op::StoreWord}, // sw r26, 0x2050(r0)
      {0x8c, 0xac1b2054, Op::StoreWord}, // sw r27, 0x2054(r0)
      {0x90, 0x241affef, Op::AddImmediateUnsigned}, // addiu r26, r0, ~0x10
      {0x94, 0xae9a0000, Op::StoreWord}, // sw r26, I_STAT(r20): timer 0 only
      {0x98, 0x8e9a0000, Op::LoadWord}, // lw r26, I_STAT(r20)
      {0x9c, 0, Op::Nop},
      {0xa0, 0xac1a2058, Op::StoreWord}, // sw r26, 0x2058(r0)
      {0xa4, 0x3c1afffe, Op::LoadUpperImmediate}, // lui r26, 0xfffe
      {0xa8, 0x375affff, Op::OrImmediate}, // ori r26, r26, 0xffff
      {0xac, 0xae9a0000, Op::StoreWord}, // sw r26, I_STAT(r20): timer 5 only
      {0xb0, 0x401a7000, Op::MoveFromCOP0}, // mfc0 r26, EPC
      {0xb4, 0, Op::Nop},
      {0xb8, 0xac1a205c, Op::StoreWord}, // sw r26, 0x205c(r0)
      {0xbc, 0x03400008, Op::JumpRegister}, // jr r26
      {0xc0, 0x42000010, Op::ReturnFromException} // rfe
    });
    image.initialMemory.insert(image.initialMemory.end(), {
      {0x204c, 0xdeadbeef}, {0x2050, 0xaaaaaaaa}, {0x2054, 0xbbbbbbbb},
      {0x2058, 0xcccccccc}, {0x205c, 0xdddddddd}, {0x2060, 0xcafebabe}
    });
    auto &expected = image.expected;
    expected.instructions = 58;
    expected.cycles = 59;
    expected.masterCycles = 472;
    expected.programCounter = image.config.returnAddress;
    expected.status = 0x00200401;
    expected.exceptionProgramCounter = 0xf48;
    expected.registers.insert(expected.registers.end(), {
      {1, 0x401}, {24, 1}, {25, 0x10}, {26, 0xf48}, {27, 0xc10}
    });
    expected.changedMemory.insert(expected.changedMemory.end(), {
      {0x2050, 0xc10}, {0x2054, 0xc10}, {0x2058, 0x10000}, {0x205c, 0xf48}
    });
    scenario.endCycle = 465;
    scenario.checkpoints = {121, 129, 137, 145, 153, 161, 193, 249, 273, 281};
    for (IOPAddress pc = 0xf00; pc <= 0xf44; pc += 4)
    {
      scenario.retiredPCs.push_back(pc);
    }
    for (IOPAddress pc = 0x80000080; pc <= 0x800000c0; pc += 4)
    {
      scenario.retiredPCs.push_back(pc);
    }
    scenario.retiredPCs.insert(scenario.retiredPCs.end(), {0xf48, 0xf4c});
    for (IOPAddress pc = 0x1000; pc <= 0x1050; pc += 4)
    {
      scenario.retiredPCs.push_back(pc);
    }
    scenario.entries = {
      {145, Subsystem::IOP, Type::InterruptDelivered, 0xf48, 0x00200401, 0x400, 0x80000080},
      {145, Subsystem::IOP, Type::ExceptionEntered, 0, 0xf48, 0x80000080, 0x400}
    };
    return scenario;
  }

  std::array<Scenario, 3> scenarios()
  {
    return {{
      handledException(IOPException::ReservedInstruction),
      handledException(IOPException::ArithmeticOverflow),
      timerService()
    }};
  }

  void prepare(NekoSystem &system, const Scenario &scenario)
  {
    for (const auto &instruction : scenario.image.code)
    {
      CAPTURE(instruction.address);
      const auto decoded = decodeIOPInstruction(instruction.word);
      REQUIRE(decoded.disposition == instruction.disposition);
      REQUIRE(decoded.instruction.operation == instruction.operation);
    }
    IOPSemanticFixtures::load(system.iopBus(), scenario.image);
    const auto &config = scenario.image.config;
    system.iopCore().prepareFreshExecution(
      config.entryPoint, config.stackPointer, config.returnAddress);
    system.iopCore().startExecution(config.entryPoint);
  }

  void requireFinal(NekoSystem &system, const Scenario &scenario)
  {
    // Stop at the guest return address before another fetch, not by host halt.
    REQUIRE(system.masterClockScheduler().currentCycle() == scenario.endCycle);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(system.iopCore().stopReason() == IOPStopReason::None);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopCore().storeFailure().width == IOPStoreWidth::None);
    REQUIRE(system.eeCore().elapsedCycles() == 0);
    IOPSemanticFixtures::requireState(system.iopBus(), system.iopCore(), scenario.image);
    REQUIRE(system.iopInterruptController().status() == 0);
    if (scenario.kind == ProgramKind::TimerService)
    {
      REQUIRE(system.iopInterruptController().mask() == 0x10010);
      REQUIRE(system.iopInterruptController().control() == 1);
      REQUIRE(system.iopTimers().counter(0).count == 44);
      REQUIRE(system.iopTimers().counter(5).count == 43);
      for (const unsigned timer : {0u, 5u})
      {
        REQUIRE(system.iopTimers().counter(timer).mode == 0x410);
        REQUIRE_FALSE(system.iopTimers().counter(timer).oneShotConsumed);
        REQUIRE_FALSE(system.iopTimers().counter(timer).pulseReleasePending);
      }
    }
  }

  void requireProgramTrace(const NekoSystem &system, const Scenario &scenario)
  {
    Events entries;
    std::size_t retired = 0;
    unsigned issued = 0;
    for (const auto &event : subsystemEvents(system, Subsystem::IOP))
    {
      if (event.type == Type::ExceptionEntered || event.type == Type::InterruptDelivered)
      {
        entries.push_back(event);
      }
      if (event.type == Type::InstructionRetired)
      {
        REQUIRE(retired < scenario.retiredPCs.size());
        REQUIRE(event.value0 == scenario.retiredPCs[retired]);
        ++retired;
        REQUIRE(event.value2 == retired);
        REQUIRE(event.value3 == (event.masterCycle + 7) / 8);
      }
      if (event.type == Type::InstructionIssued)
      {
        ++issued;
      }
      if (scenario.kind == ProgramKind::TimerService && event.masterCycle == 145)
      {
        REQUIRE((event.type == Type::InterruptDelivered || event.type == Type::ExceptionEntered));
      }
    }
    REQUIRE(retired == scenario.image.expected.instructions);
    REQUIRE(issued == (scenario.kind == ProgramKind::TimerService ? 58 : scenario.image.expected.cycles));
    requireEvents(entries, scenario.entries);
    if (scenario.kind != ProgramKind::TimerService)
    {
      REQUIRE(subsystemEvents(system, Subsystem::IOPInterruptController).empty());
      return;
    }
    requireEvents(subsystemEvents(system, Subsystem::IOPInterruptController), {
      {65, Subsystem::IOPInterruptController, Type::InterruptChanged, 0, 0x10010, 0, 0},
      {97, Subsystem::IOPInterruptController, Type::InterruptChanged, 0, 0x10010, 1, 0},
      {121, Subsystem::IOPInterruptController, Type::InterruptChanged, 0x10, 0x10010, 1, 1},
      {129, Subsystem::IOPInterruptController, Type::InterruptChanged, 0x10010, 0x10010, 1, 1},
      {193, Subsystem::IOPInterruptController, Type::InterruptChanged, 0x10000, 0x10010, 1, 1},
      {241, Subsystem::IOPInterruptController, Type::InterruptChanged, 0, 0x10010, 1, 0}
    });
    namespace Timer = NekoIOPTraceTimer;
    struct TimerEvent
    {
      unsigned cycle;
      unsigned index;
      std::uint64_t flags;
    };
    const std::array<TimerEvent, 10> expected = {{
      {81, 0, Timer::REGISTER_WRITTEN},
      {89, 5, Timer::REGISTER_WRITTEN},
      {113, 0, Timer::REGISTER_WRITTEN},
      {121, 0, Timer::TARGET_REACHED | Timer::INTERRUPT_REQUESTED},
      {121, 5, Timer::REGISTER_WRITTEN},
      {129, 0, Timer::PULSE_RELEASED},
      {129, 5, Timer::TARGET_REACHED | Timer::INTERRUPT_REQUESTED},
      {137, 5, Timer::PULSE_RELEASED},
      {153, 0, Timer::MODE_ACKNOWLEDGED},
      {161, 5, Timer::MODE_ACKNOWLEDGED}
    }};
    const auto events = subsystemEvents(system, Subsystem::IOPTimers);
    const auto eventFlags = Timer::TARGET_REACHED | Timer::OVERFLOW_REACHED |
      Timer::INTERRUPT_REQUESTED | Timer::PULSE_RELEASED | Timer::GATE_CHANGED |
      Timer::MODE_ACKNOWLEDGED | Timer::REGISTER_WRITTEN;
    REQUIRE(events.size() == expected.size());
    for (std::size_t index = 0; index < events.size(); ++index)
    {
      CAPTURE(index);
      REQUIRE(events[index].type == Type::TimerEvent);
      REQUIRE(events[index].masterCycle == expected[index].cycle);
      REQUIRE((events[index].value0 & Timer::INDEX_MASK) == expected[index].index);
      REQUIRE((events[index].value0 & eventFlags) == expected[index].flags);
    }
  }

  void requireCheckpoint(const NekoSystem &system, const Scenario &scenario, unsigned cycle)
  {
    CAPTURE(cycle);
    REQUIRE(system.masterClockScheduler().currentCycle() == cycle);
    const auto &core = system.iopCore();
    if (scenario.kind != ProgramKind::TimerService)
    {
      const unsigned faultCycle = scenario.kind == ProgramKind::OverflowInSlot ? 49 : 41;
      if (cycle == faultCycle)
      {
        REQUIRE(core.exception().kind == (scenario.kind == ProgramKind::OverflowInSlot
          ? IOPException::ArithmeticOverflow : IOPException::ReservedInstruction));
        REQUIRE(core.exceptionProgramCounter() == 0x1008);
        REQUIRE(core.programCounter() == 0x80000080);
        REQUIRE(core.cause() == scenario.entries.front().value3);
      }
      else if (cycle == faultCycle - 8)
      {
        if (scenario.kind == ProgramKind::OverflowInSlot)
        {
          REQUIRE(core.branchContinuation().active);
          REQUIRE(core.branchContinuation().targetAddress == 0x1010);
        }
        else
        {
          REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::Load);
          REQUIRE(core.delayedResultContinuation().destination == 9);
          REQUIRE(core.delayedResultContinuation().value == 0xdeadbeef);
        }
      }
      else if (cycle == faultCycle + 16)
      {
        REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::COP0Transfer);
        REQUIRE(core.delayedResultContinuation().destination == 27);
        REQUIRE(core.delayedResultContinuation().value == 0x1008);
      }
      else if (cycle == scenario.endCycle - 32)
      {
        REQUIRE(core.status() == 0x00200000);
        REQUIRE(core.programCounter() == (scenario.kind == ProgramKind::OverflowInSlot ? 0x1010 : 0x100c));
        REQUIRE_FALSE(core.branchContinuation().active);
      }
      else
      {
        REQUIRE(cycle == scenario.endCycle - 40);
        REQUIRE(core.branchContinuation().active);
        REQUIRE(core.branchContinuation().targetAddress ==
          (scenario.kind == ProgramKind::OverflowInSlot ? 0x1010 : 0x100c));
      }
      return;
    }
    switch (cycle)
    {
      case 121:
        REQUIRE(system.iopInterruptController().status() == 0x10);
        REQUIRE(system.iopTimers().counter(0).pulseReleasePending);
        break;
      case 129:
        REQUIRE(system.iopInterruptController().status() == 0x10010);
        REQUIRE_FALSE(system.iopTimers().counter(0).pulseReleasePending);
        REQUIRE(system.iopTimers().counter(5).pulseReleasePending);
        break;
      case 137:
        REQUIRE(core.status() == 0x00200401);
        REQUIRE(core.programCounter() == 0xf48);
        REQUIRE_FALSE(system.iopTimers().counter(5).pulseReleasePending);
        break;
      case 145:
        REQUIRE(core.exception().kind == IOPException::Interrupt);
        REQUIRE(core.exceptionProgramCounter() == 0xf48);
        REQUIRE(core.status() == 0x00200404);
        break;
      case 153:
      case 161:
        REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::Load);
        REQUIRE(core.delayedResultContinuation().destination == (cycle == 153 ? 26 : 27));
        REQUIRE(core.delayedResultContinuation().value == 0xc10);
        REQUIRE(system.iopTimers().counter(cycle == 153 ? 0 : 5).mode == 0x410);
        break;
      case 193:
        REQUIRE(system.iopInterruptController().status() == 0x10000);
        REQUIRE(system.iopInterruptController().interruptPending());
        REQUIRE((core.status() & 1) == 0);
        break;
      case 249:
        REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::COP0Transfer);
        REQUIRE(core.delayedResultContinuation().destination == 26);
        REQUIRE(core.delayedResultContinuation().value == 0xf48);
        break;
      case 273:
        REQUIRE(core.branchContinuation().active);
        REQUIRE(core.branchContinuation().targetAddress == 0xf48);
        REQUIRE((core.status() & 1) == 0);
        break;
      case 281:
        REQUIRE_FALSE(core.branchContinuation().active);
        REQUIRE(core.programCounter() == 0xf48);
        REQUIRE(core.status() == 0x00200401);
        REQUIRE_FALSE(system.iopInterruptController().interruptPending());
        break;
      default:
        FAIL("Unknown timer-service checkpoint.");
    }
  }

  Events traceAfter(const NekoSystem &system, unsigned cycle)
  {
    Events events;
    for (const auto &event : system.trace())
    {
      if (event.masterCycle > cycle)
      {
        events.push_back(event);
      }
    }
    return events;
  }
}

TEST_CASE("IOP whole-system programs trace complete exception and dual timer service",
  "[iop][system][program][conformance][trace]")
{
  for (const auto &scenario : scenarios())
  {
    INFO(scenario.image.name);
    NekoSystem reference;
    NekoSystem repeated;
    NekoSystem untraced;
    for (auto *system : {&reference, &repeated, &untraced})
    {
      prepare(*system, scenario);
    }
    reference.startTrace();
    repeated.startTrace();
    for (auto *system : {&reference, &repeated, &untraced})
    {
      system->runMasterCycles(scenario.endCycle);
      requireFinal(*system, scenario);
    }
    requireProgramTrace(reference, scenario);
    requireEvents(repeated.trace(), reference.trace());
    requireMachine(repeated, reference);
    requireMachine(untraced, reference);
    REQUIRE(untraced.trace().empty());
  }
}

TEST_CASE("IOP whole-system programs restore identical handler and device continuations",
  "[iop][system][program][conformance][trace][save-state]")
{
  for (const auto &scenario : scenarios())
  {
    INFO(scenario.image.name);
    NekoSystem reference;
    prepare(reference, scenario);
    reference.startTrace();
    reference.runMasterCycles(scenario.endCycle);
    requireFinal(reference, scenario);
    for (const unsigned checkpoint : scenario.checkpoints)
    {
      CAPTURE(checkpoint);
      NekoSystem original;
      prepare(original, scenario);
      original.startTrace();
      original.runMasterCycles(checkpoint);
      requireCheckpoint(original, scenario, checkpoint);
      const auto state = original.saveState();
      NekoSystem restored;
      prepare(restored, scenario);
      restored.runMasterCycles(3);
      restored.loadState(state);
      requireCheckpoint(restored, scenario, checkpoint);
      requireMachine(restored, original);
      original.startTrace();
      restored.startTrace();
      original.runMasterCycles(scenario.endCycle - checkpoint);
      restored.runMasterCycles(scenario.endCycle - checkpoint);
      requireFinal(original, scenario);
      requireFinal(restored, scenario);
      requireEvents(original.trace(), traceAfter(reference, checkpoint));
      requireEvents(restored.trace(), original.trace());
      requireMachine(original, reference);
      requireMachine(restored, reference);
    }
  }
}

TEST_CASE("IOP whole-system program reset discards active service and replays identically",
  "[iop][system][program][conformance][trace][reset]")
{
  NekoSystem pristine;
  for (const auto &scenario : scenarios())
  {
    INFO(scenario.image.name);
    NekoSystem reference;
    prepare(reference, scenario);
    reference.startTrace();
    reference.runMasterCycles(scenario.endCycle);
    requireFinal(reference, scenario);
    for (const unsigned checkpoint : scenario.checkpoints)
    {
      CAPTURE(checkpoint);
      NekoSystem system;
      prepare(system, scenario);
      system.startTrace();
      system.runMasterCycles(checkpoint);
      requireCheckpoint(system, scenario, checkpoint);
      system.reset();
      REQUIRE_FALSE(system.traceEnabled());
      REQUIRE(system.trace().empty());
      requireMachine(system, pristine);
      prepare(system, scenario);
      system.startTrace();
      system.runMasterCycles(scenario.endCycle);
      requireFinal(system, scenario);
      requireEvents(system.trace(), reference.trace());
      requireMachine(system, reference);
    }
  }
}
