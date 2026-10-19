#include <cstddef>
#include <cstdint>

#include "catch.hpp"
#include "iop/iop_trace_test_utils.hpp"
#include "neko_system.hpp"

namespace
{
  using IOPTraceTest::requireEvents;
  using IOPTraceTest::requireMachine;
  using IOPTraceTest::subsystemEvents;
  using Type = NekoTraceEventType;
  constexpr std::uint32_t ENTRY = 0xa0001000;
  constexpr std::uint32_t RESULT = 0x2000;
  constexpr std::uint64_t BUDGET = 1024;
  constexpr std::uint32_t EE_OTHER = EEInterruptSource::mask(EEInterruptSource::VIF0);
  constexpr std::uint32_t IOP_OTHER = 8;

  void prepare(NekoSystem &system)
  {
    // Independently authored guests use uncached code/data and poll SBUS with
    // CPU exception admission disabled. Only guests write the SIF registers.
    const std::uint32_t ee[] = {
      0x3c01b000, // 1000: lui r1, 0xb000
      0x3421f200, // ori r1, r1, 0xf200: SIF
      0x3c02b000,
      0x3442f000, // ori r2, r2, 0xf000: EE INTC
      0x3c03a000,
      0x34632000, // ori r3, r3, 0x2000: results
      0x3c040004,
      0x34840100, // ori r4, r4, 0x100: repeated CTRL request
      0x3c068123,
      0x34c60001, // ori r6, r6, 1: first request payload
      0x34070002,
      0xac470010, // sw r7, 0x10(r2): unmask SBUS
      0x3c0a8000,
      0x354a0001, // ori r10, r10, 1: ready flag and retained sentinel
      0x340b0001,
      0x34140002, // ori r20, r0, 2: round count
      0xac260000, // 1040: sw r6, MSCOM(r1)
      0xac2a0020, // sw r10, MSFLAG(r1)
      0xac240040, // sw r4, CTRL(r1): request IOP
      0x8c480000, // 104c: lw r8, INTC_STAT(r2)
      0,
      0x31080002, // andi r8, r8, 2
      0x1100fffc, // beq r8, r0, 0x104c
      0,
      0x8c290010, // lw r9, SMCOM(r1)
      0,
      0xac690000, // sw r9, 0(r3)
      0x8c2c0030, // lw r12, SMFLAG(r1)
      0,
      0xac6c0004,
      0xac200010, // sw r0, SMCOM(r1): accepted read-only no-op
      0xac2b0030, // sw r11, SMFLAG(r1): clear ready, not sentinel
      0xac470000, // sw r7, INTC_STAT(r2): write-one acknowledgement
      0x8c4d0000,
      0,
      0xac6d0008, // save unrelated interrupt after acknowledgement
      0x8c2e0020,
      0,
      0xac6e000c, // save peer-cleared MSFLAG
      0x24c60001, // addiu r6, r6, 1
      0x24630010, // addiu r3, r3, 16
      0x2694ffff, // addiu r20, r20, -1
      0x1680ffe5, // bne r20, r0, 0x1040
      0,
      0x340f600d,
      0xac6f0000, // completion marker at 0x2020
      0x1000ffff, // 10b8: beq r0, r0, 0x10b8
      0
    };
    const std::uint32_t iop[] = {
      0x3c01bd00, // 1000: lui r1, 0xbd00: SIF
      0x3c02bf80,
      0x34421070, // ori r2, r2, 0x1070: IOP INTC
      0x3c03a000,
      0x34632000, // ori r3, r3, 0x2000: results
      0x3c04bf80,
      0x34841450, // ori r4, r4, 0x1450: doorbell
      0x34060002,
      0x3c074000,
      0x34e70001, // ori r7, r7, 1: ready flag and retained sentinel
      0x2408fffd, // addiu r8, r0, -3: acknowledge only SBUS
      0x340b0001,
      0x34140002,
      0xac460004, // sw r6, I_MASK(r2)
      0xac4b0008, // sw r11, I_CTRL(r2)
      0x8c4c0000, // 103c: lw r12, I_STAT(r2)
      0,          // load delay
      0x318c0002, // andi r12, r12, 2
      0x1180fffc, // beq r12, r0, 0x103c
      0,
      0x8c290000, // 1050: lw r9, MSCOM(r1)
      0,
      0xac690000, // save request payload
      0x8c2a0020,
      0,
      0xac6a0004, // save MSFLAG before clearing ready
      0xac200000, // sw r0, MSCOM(r1): accepted read-only no-op
      0xac2b0020, // sw r11, MSFLAG(r1)
      0xac480000, // sw r8, I_STAT(r2): write-zero acknowledgement
      0x8c4d0000,
      0,
      0xac6d0008, // save unrelated interrupt after acknowledgement
      0x25290010, // addiu r9, r9, 0x10
      0xac69000c, // save response payload
      0xac290010, // sw r9, SMCOM(r1)
      0xac270030, // sw r7, SMFLAG(r1)
      0xac860000, // sw r6, 0(r4): request EE
      0x24630010,
      0x2694ffff,
      0x1680ffe7, // bne r20, r0, 0x103c
      0,
      0x340f600d,
      0xac6f0000, // completion marker at 0x2020
      0x1000ffff, // 10ac: beq r0, r0, 0x10ac
      0
    };
    std::uint32_t address = 0x1000;
    for (const auto word : ee)
    {
      system.eeBus().write32(address, word);
      address += 4;
    }
    address = 0x1000;
    for (const auto word : iop)
    {
      REQUIRE(system.iopBus().write32(address, word).status ==
        IOPBusStatus::Completed);
      address += 4;
    }
    system.interruptController().setSource(EEInterruptSource::VIF0, true);
    system.iopInterruptController().assertSource(IOPInterruptSource::DMAC);
    system.dmacController().signalChannelCompletion(DMACStatus::CHANNEL_2);
    system.dmacController().writeStatus(DMACStatus::CHANNEL_2_MASK);
    system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
    system.eeCore().startExecution(ENTRY);
    system.iopCore().startExecution(ENTRY);
  }

  void requireCompleted(NekoSystem &system)
  {
    REQUIRE(system.eeCore().executionState() == EEExecutionState::Running);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(system.eeCore().pendingException() == EEException::None);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.eeCore().generalRegister(20).low == 0);
    REQUIRE(system.iopCore().generalRegister(20) == 0);
    for (std::uint32_t round = 0; round < 2; ++round)
    {
      CAPTURE(round);
      const auto address = RESULT + 16 * round;
      REQUIRE(system.eeBus().read32(address) == 0x81230011 + round);
      REQUIRE(system.eeBus().read32(address + 4) == 0x40000001);
      REQUIRE(system.eeBus().read32(address + 8) == EE_OTHER);
      REQUIRE(system.eeBus().read32(address + 12) == 0x80000000);
      REQUIRE(system.iopBus().read32(address).value == 0x81230001 + round);
      REQUIRE(system.iopBus().read32(address + 4).value == 0x80000001);
      REQUIRE(system.iopBus().read32(address + 8).value == IOP_OTHER);
      REQUIRE(system.iopBus().read32(address + 12).value == 0x81230011 + round);
    }
    REQUIRE(system.eeBus().read32(RESULT + 32) == 0x600d);
    REQUIRE(system.iopBus().read32(RESULT + 32).value == 0x600d);
    REQUIRE(system.sif().readEE(SIFRegister::MSCOM) == 0x81230002);
    REQUIRE(system.sif().readIOP(SIFRegister::SMCOM) == 0x81230012);
    REQUIRE(system.sif().readEE(SIFRegister::MSFLAG) == 0x80000000);
    REQUIRE(system.sif().readIOP(SIFRegister::SMFLAG) == 0x40000000);
    REQUIRE(system.sif().control() == 0x100);
    REQUIRE(system.sif().iopDoorbell() == 2);
    REQUIRE(system.interruptController().status() == EE_OTHER);
    REQUIRE(system.iopInterruptController().status() == IOP_OTHER);
    REQUIRE(system.interruptController().mask() == 2);
    REQUIRE(system.iopInterruptController().mask() == 2);
    REQUIRE(system.iopInterruptController().control() == 1);
    REQUIRE_FALSE(system.interruptController().interruptPending());
    REQUIRE_FALSE(system.iopInterruptController().interruptPending());
    REQUIRE(system.dmacController().status() ==
      (DMACStatus::CHANNEL_2 | DMACStatus::CHANNEL_2_MASK));
    REQUIRE(system.dmacController().interruptPending());
    REQUIRE(system.sif().freeWordCount() == SIF::WORD_CAPACITY);
  }

  enum class Checkpoint
  {
    PollBranch,
    MailboxLoad,
    RequestAcknowledged,
    ResponsePending,
    SecondRequest
  };

  void reachCheckpoint(NekoSystem &system, Checkpoint checkpoint)
  {
    bool reached = false;
    while (!reached && system.masterClockScheduler().currentCycle() < BUDGET)
    {
      system.clockMasterCycle();
      const auto &core = system.iopCore();
      switch (checkpoint)
      {
        case Checkpoint::PollBranch:
          reached = core.branchContinuation().active &&
            core.branchContinuation().instructionAddress == ENTRY + 0x48;
          break;
        case Checkpoint::MailboxLoad:
          reached = core.delayedResultContinuation().source ==
              IOPDelayedResultSource::Load &&
            core.delayedResultContinuation().destination == 9;
          break;
        case Checkpoint::RequestAcknowledged:
          reached = system.sif().readEE(SIFRegister::MSFLAG) == 0x80000000 &&
            system.iopInterruptController().status() == IOP_OTHER &&
            system.sif().readIOP(SIFRegister::SMCOM) == 0;
          break;
        case Checkpoint::ResponsePending:
          reached = (system.interruptController().status() & 2) != 0;
          break;
        case Checkpoint::SecondRequest:
          reached = system.sif().readEE(SIFRegister::MSCOM) == 0x81230002 &&
            (system.iopInterruptController().status() & 2) != 0;
          break;
      }
    }
    CAPTURE(static_cast<unsigned>(checkpoint));
    REQUIRE(reached);
    REQUIRE(system.masterClockScheduler().currentCycle() % 8 != 0);
    REQUIRE(system.eeCore().executionState() == EEExecutionState::Running);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
  }

  void requireSameMachine(const NekoSystem &actual, const NekoSystem &expected)
  {
    REQUIRE(actual.sifStateHash() == expected.sifStateHash());
    requireMachine(actual, expected);
  }
}

TEST_CASE("SIF two-CPU guests exchange and acknowledge two independent rounds",
  "[sif][conformance][system]")
{
  NekoSystem system;
  prepare(system);
  system.startTrace();
  system.runMasterCycles(BUDGET);
  requireCompleted(system);
  REQUIRE(system.iopCore().elapsedCycles() == BUDGET / 8);
  REQUIRE(system.iopCore().retiredInstructions() == BUDGET / 8);

  const auto events = subsystemEvents(system, NekoTraceSubsystem::SIF);
  REQUIRE(events.size() == 24);
  struct Write
  {
    std::uint32_t metadata;
    std::uint32_t submitted;
    std::uint32_t before;
    std::uint32_t after;
    std::uint32_t signal = 0;
  };
  std::size_t index = 0;
  for (std::uint32_t round = 0; round < 2; ++round)
  {
    const auto request = 0x81230001 + round;
    const auto response = request + 0x10;
    const Write writes[] = {
      {0x000, request, round == 0 ? 0 : request - 1, request},
      {0x002, 0x80000001, round == 0 ? 0u : 0x80000000, 0x80000001},
      {0x004, 0x40100, round == 0 ? 0u : 0x100, 0x100, 1},
      {0x100, 0, request, request},
      {0x102, 1, 0x80000001, 0x80000000},
      {0x101, response, round == 0 ? 0 : response - 1, response},
      {0x103, 0x40000001, round == 0 ? 0u : 0x40000000, 0x40000001},
      {0x105, 2, round == 0 ? 0u : 2, 2, 2},
      {0x001, 0, response, response},
      {0x003, 1, 0x40000001, 0x40000000}
    };
    for (const auto &write : writes)
    {
      CAPTURE(round);
      CAPTURE(index);
      const auto &event = events.at(index++);
      REQUIRE(event.type == Type::RegisterWrite);
      REQUIRE(event.value0 == write.metadata);
      REQUIRE(event.value1 == write.submitted);
      REQUIRE(event.value2 == write.before);
      REQUIRE(event.value3 == write.after);
      if (write.signal != 0)
      {
        const auto &signal = events.at(index++);
        REQUIRE(signal.masterCycle == event.masterCycle);
        REQUIRE(signal.type == Type::SignalRequested);
        REQUIRE(signal.value0 == write.signal);
        REQUIRE(signal.value1 == write.metadata);
        REQUIRE(signal.value2 == write.submitted);
        REQUIRE(signal.value3 == 0);
      }
    }
  }
  REQUIRE(index == events.size());
}

TEST_CASE("SIF two-CPU guest execution is repeatable and tracing is observational",
  "[sif][conformance][trace]")
{
  NekoSystem plain;
  NekoSystem traced;
  NekoSystem repeated;
  prepare(plain);
  prepare(traced);
  prepare(repeated);
  traced.startTrace();
  repeated.startTrace();
  plain.runMasterCycles(BUDGET);
  traced.runMasterCycles(BUDGET);
  repeated.runMasterCycles(BUDGET);
  requireCompleted(plain);
  requireSameMachine(traced, plain);
  requireSameMachine(repeated, plain);
  requireEvents(repeated.trace(), traced.trace());
  REQUIRE(plain.trace().empty());
}

TEST_CASE("SIF two-CPU guest restore preserves every live handshake boundary",
  "[sif][conformance][save-state][trace]")
{
  for (const auto checkpoint : {Checkpoint::PollBranch, Checkpoint::MailboxLoad,
         Checkpoint::RequestAcknowledged, Checkpoint::ResponsePending,
         Checkpoint::SecondRequest})
  {
    CAPTURE(static_cast<unsigned>(checkpoint));
    NekoSystem original;
    prepare(original);
    original.startTrace();
    reachCheckpoint(original, checkpoint);
    const auto state = original.saveState();
    NekoSystem restored;
    restored.startTrace();
    restored.loadState(state);
    REQUIRE(restored.trace().empty());
    requireSameMachine(restored, original);
    original.clearTrace();
    const auto remaining = BUDGET - original.masterClockScheduler().currentCycle();
    original.runMasterCycles(remaining);
    restored.runMasterCycles(remaining);
    requireCompleted(original);
    requireCompleted(restored);
    requireSameMachine(restored, original);
    requireEvents(restored.trace(), original.trace());
  }
}

TEST_CASE("SIF active two-CPU reset clears continuations and reruns from cold state",
  "[sif][conformance][reset][trace]")
{
  for (const auto checkpoint : {Checkpoint::PollBranch, Checkpoint::MailboxLoad,
         Checkpoint::ResponsePending})
  {
    CAPTURE(static_cast<unsigned>(checkpoint));
    NekoSystem system;
    NekoSystem fresh;
    prepare(system);
    system.startTrace();
    reachCheckpoint(system, checkpoint);
    REQUIRE(system.sif().pushSIF0Prologue({{1, 2, 3, 4}}) ==
      SIFTransferOutcome::Completed);
    REQUIRE(system.sif().pushSIF0PayloadWord(5, SIFPayloadBoundary::Continuing) ==
      SIFTransferOutcome::Completed);
    REQUIRE(system.sif().pushSIF1Quadword({{6, 7, 8, 9}}) ==
      SIFTransferOutcome::Completed);
    REQUIRE_FALSE(system.trace().empty());
    system.reset();
    requireSameMachine(system, fresh);
    REQUIRE(system.trace().empty());
    REQUIRE_FALSE(system.iopCore().branchContinuation().active);
    REQUIRE(system.iopCore().delayedResultContinuation().source ==
      IOPDelayedResultSource::None);
    REQUIRE(system.sif().freeWordCount() == SIF::WORD_CAPACITY);
    REQUIRE(system.sif().sif0PackingLatch() == SIFQuadword{{0, 0, 0, 0}});
    REQUIRE(system.sif().sif0PackingCursor() == 0);

    prepare(system);
    prepare(fresh);
    system.startTrace();
    fresh.startTrace();
    system.runMasterCycles(BUDGET);
    fresh.runMasterCycles(BUDGET);
    requireCompleted(system);
    requireSameMachine(system, fresh);
    requireEvents(system.trace(), fresh.trace());
  }
}
