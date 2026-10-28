#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include "catch.hpp"
#include "iop_trace_test_utils.hpp"
#include "neko_system.hpp"
#include "save_state_mutation.hpp"

struct CDVDObservationTestAccess
{
  static void enable(CDVDController &controller, NekoObservationOrder *order)
  {
    controller.enableObservations(order);
  }

  static void disable(CDVDController &controller)
  {
    controller.disableObservations();
  }

  static bool attached(const CDVDController &controller, NekoObservationOrder *order)
  {
    return controller.observationsEnabled && controller.observationOrder == order;
  }

  static bool detached(const CDVDController &controller)
  {
    return !controller.observationsEnabled && controller.observationOrder == nullptr &&
      controller.observationCount == 0;
  }

  static std::size_t count(const CDVDController &controller)
  {
    return controller.observationCount;
  }

  static std::size_t sequence(const CDVDController &controller, std::size_t index)
  {
    return controller.observations.at(index).sequence;
  }
};

namespace
{
  constexpr auto CDVD = static_cast<NekoTraceSubsystem>(15);
  constexpr auto ACCEPTED = static_cast<NekoTraceEventType>(23);
  constexpr auto COMPLETED = static_cast<NekoTraceEventType>(24);
  constexpr std::uint32_t BASE = 0x1f402000;
  constexpr std::uint64_t EPOCH = UINT64_C(0x0001010000000000);

  void write(NekoSystem &system, unsigned offset, std::uint8_t value)
  {
    REQUIRE(system.iopBus().write8(BASE + offset, value).status ==
      IOPBusStatus::Completed);
  }

  void prepareStore(NekoSystem &system, IOPWord instruction = 0xa0200004)
  {
    const std::array<IOPWord, 3> program = {{
      0x3c01bf40, 0x34212000, instruction
    }};
    for (std::size_t index = 0; index < program.size(); ++index)
    {
      REQUIRE(system.iopBus().write32(static_cast<IOPAddress>(index * 4),
        program[index]).status == IOPBusStatus::Completed);
    }
    system.iopCore().startExecution(0xa0000000);
    system.runMasterCycles(16);
    write(system, 4, 0);
    write(system, 0x16, 8);
  }

  class Observer final : public NekoTraceObserver
  {
    public:
      void observe(const NekoTraceEvent &event) noexcept override
      {
        if (count < events.size())
        {
          events[count++] = event;
        }
        else
        {
          overflow = true;
        }
      }

      std::array<NekoTraceEvent, 2048> events = {};
      std::size_t count = 0;
      bool overflow = false;
  };

  template<std::size_t Size>
  void installProgram(NekoSystem &system, IOPAddress address,
    const std::array<IOPWord, Size> &words)
  {
    for (const auto word : words)
    {
      REQUIRE(system.iopBus().write32(address, word).status ==
        IOPBusStatus::Completed);
      address += 4;
    }
  }

  enum class TraceMode { Disabled, Retained, Prefix, Stream };

  void requireContinuationModes(const NekoSystem &reference, NekoSystem &source)
  {
    const auto cut = source.masterClockScheduler().currentCycle();
    const auto remaining = reference.masterClockScheduler().currentCycle() - cut;
    const auto checkpoint = source.saveState();
    std::vector<NekoTraceEvent> suffix;
    for (const auto &event : reference.trace())
    {
      if (event.masterCycle > cut)
      {
        suffix.push_back(event);
      }
    }
    REQUIRE(suffix.size() > 3);
    for (const auto mode : {TraceMode::Disabled, TraceMode::Retained,
         TraceMode::Prefix, TraceMode::Stream})
    {
      CAPTURE(mode);
      NekoSystem restored;
      restored.loadState(checkpoint);
      const auto observer = std::make_shared<Observer>();
      switch (mode)
      {
        case TraceMode::Disabled:
          break;
        case TraceMode::Retained:
          restored.startTrace();
          break;
        case TraceMode::Prefix:
          restored.startTrace(3, observer);
          break;
        case TraceMode::Stream:
          restored.startTrace(0, observer);
          break;
      }
      restored.runMasterCycles(remaining);
      IOPTraceTest::requireMachine(restored, reference);
      REQUIRE(CDVDObservationTestAccess::detached(restored.cdvdController()));
      if (mode == TraceMode::Retained)
      {
        IOPTraceTest::requireEvents(restored.trace(), suffix);
        REQUIRE_FALSE(restored.traceTruncated());
      }
      else if (mode == TraceMode::Prefix || mode == TraceMode::Stream)
      {
        REQUIRE_FALSE(observer->overflow);
        const std::vector<NekoTraceEvent> observed(
          observer->events.begin(), observer->events.begin() + observer->count);
        IOPTraceTest::requireEvents(observed, suffix);
        const auto retained = mode == TraceMode::Prefix ? 3u : 0u;
        IOPTraceTest::requireEvents(restored.trace(),
          {suffix.begin(), suffix.begin() + retained});
        REQUIRE(restored.traceTruncated());
      }
      else
      {
        REQUIRE(restored.trace().empty());
        REQUIRE_FALSE(restored.traceEnabled());
      }
    }
    source.runMasterCycles(remaining);
    IOPTraceTest::requireMachine(source, reference);
  }
}

TEST_CASE("CDVD trace observes both completions and guest acceptance in mutation order",
  "[iop][cdvd-trace]")
{
  NekoSystem system;
  prepareStore(system);
  system.startTrace();
  system.clockMasterCycle();
  IOPTraceTest::requireEvents(IOPTraceTest::subsystemEvents(system, CDVD), {
    {17, CDVD, COMPLETED, UINT64_C(0x010000010000), 17, 0, 0},
    {17, CDVD, COMPLETED, UINT64_C(0x010102010801), 17, EPOCH, 0},
    {17, CDVD, ACCEPTED, UINT64_C(0x010101000000), 17, 0, 0}
  });
  std::size_t commandCount = 0;
  bool interruptSeen = false;
  bool storeSeen = false;
  for (const auto &event : system.trace())
  {
    if (event.subsystem == CDVD)
    {
      ++commandCount;
      if (commandCount == 2)
      {
        REQUIRE(interruptSeen);
      }
    }
    if (event.subsystem == NekoTraceSubsystem::IOPInterruptController)
    {
      REQUIRE(commandCount == 1);
      interruptSeen = true;
    }
    if (event.subsystem == NekoTraceSubsystem::IOP &&
        event.type == NekoTraceEventType::MemoryAccess &&
        event.value0 == 0xbf402004)
    {
      REQUIRE(commandCount == 3);
      storeSeen = true;
    }
  }
  REQUIRE(interruptSeen);
  REQUIRE(storeSeen);
}

TEST_CASE("CDVD trace snapshots RTC on completion rather than command acceptance",
  "[iop][cdvd-trace]")
{
  NekoSystem system;
  SaveStateMutation seeded(system.saveState());
  seeded.writeScalar("system.iop.cdvd.rtcTicks", PS2Clock::MASTER_HZ - 26);
  seeded.refreshChecksum();
  system.loadState(seeded);
  const std::array<IOPWord, 4> program = {{
    0x3c01bf40, 0x34212000, 0x34020008, 0xa0220016
  }};
  for (std::size_t index = 0; index < program.size(); ++index)
  {
    REQUIRE(system.iopBus().write32(static_cast<IOPAddress>(index * 4),
      program[index]).status == IOPBusStatus::Completed);
  }
  system.iopCore().startExecution(0xa0000000);
  system.startTrace();
  system.runMasterCycles(33);
  IOPTraceTest::requireEvents(IOPTraceTest::subsystemEvents(system, CDVD), {
    {25, CDVD, ACCEPTED, 0x01000801, PS2Clock::MASTER_HZ - 1, 0, 0},
    {33, CDVD, COMPLETED, 0x02010801, PS2Clock::MASTER_HZ + 7, EPOCH | 0x100, 0}
  });
}

TEST_CASE("CDVD completes within the actual ten-transition IOP interrupt cycle",
  "[iop][cdvd-trace]")
{
  NekoSystem system;
  const std::array<IOPWord, 3> program = {{
    0x3c01bf80, 0x34211078, 0x8c220000 // lw r2, I_CTRL
  }};
  for (std::size_t index = 0; index < program.size(); ++index)
  {
    REQUIRE(system.iopBus().write32(static_cast<IOPAddress>(index * 4),
      program[index]).status == IOPBusStatus::Completed);
  }
  system.iopCore().startExecution(0xa0000000);
  system.runMasterCycles(16);
  for (std::size_t index = 0; index < 6; ++index)
  {
    system.iopTimers().writeTarget(index, 0);
    system.iopTimers().writeMode(index, 0x30);
    system.iopTimers().writeCount(index, index < 3 ? 0xffff : 0xffffffff);
  }
  REQUIRE(system.sif().pushSIF1Quadword({{0x80001000, 0, 0, 0}}) ==
    SIFTransferOutcome::Completed);
  REQUIRE(system.iopSIF1DMAC().writeChannelControl(0x01000400) ==
    IOPDMAWriteOutcome::Completed);
  system.iopDMAController().writePriority(IOPDMABank::Secondary, 0x8000);
  system.iopInterruptController().writeMask(0x1c07e);
  system.iopInterruptController().writeControl(1);
  write(system, 4, 0);
  write(system, 0x16, 8);
  system.eeBus().write32(0, 0xac220000);
  system.eeCore().setGeneralRegister(1, {0xb000f240, 0});
  system.eeCore().setGeneralRegister(2, {0x40100, 0});
  system.eeCore().startExecution(0xa0000000);
  system.startTrace();
  system.clockMasterCycle();
  const auto events = IOPTraceTest::subsystemEvents(
    system, NekoTraceSubsystem::IOPInterruptController);
  const std::array<std::uint32_t, 10> expected = {{
    0x2, 0x12, 0x32, 0x72, 0x4072, 0xc072, 0x1c072, 0x1c076, 0x1c07e, 0x1c07e
  }};
  REQUIRE(events.size() == expected.size());
  for (std::size_t index = 0; index < events.size(); ++index)
  {
    CAPTURE(index);
    REQUIRE(events[index].masterCycle == 17);
    REQUIRE(events[index].value0 == expected[index]);
    REQUIRE(events[index].value1 == 0x1c07e);
    REQUIRE(events[index].value2 == (index == 9 ? 0 : 1));
  }
  REQUIRE(IOPTraceTest::subsystemEvents(system, CDVD).size() == 2);
  REQUIRE(system.iopCore().retiredInstructions() == 3);
  REQUIRE(system.iopInterruptController().control() == 0);
}

TEST_CASE("CDVD tracing preserves machines and streams past a full retained prefix",
  "[iop][cdvd-trace][save-state]")
{
  NekoSystem plain;
  prepareStore(plain);
  const auto initial = plain.saveState();
  NekoSystem full;
  NekoSystem limited;
  full.loadState(initial);
  limited.loadState(initial);
  const auto observer = std::make_shared<Observer>();
  full.startTrace();
  limited.startTrace(1, observer);
  plain.runMasterCycles(9);
  full.runMasterCycles(9);
  limited.runMasterCycles(9);
  IOPTraceTest::requireMachine(full, plain);
  IOPTraceTest::requireMachine(limited, plain);
  REQUIRE(plain.trace().empty());
  REQUIRE(limited.trace().size() == 1);
  REQUIRE(limited.traceTruncated());
  REQUIRE_FALSE(observer->overflow);
  const std::vector<NekoTraceEvent> observed(
    observer->events.begin(), observer->events.begin() + observer->count);
  IOPTraceTest::requireEvents(observed, full.trace());
  const auto commands = IOPTraceTest::subsystemEvents(full, CDVD);
  REQUIRE(commands.size() == 4);
  REQUIRE(commands.back().masterCycle == 25);
  REQUIRE(commands.back().type == COMPLETED);
  REQUIRE(commands.back().value0 == UINT64_C(0x010100010000));
  for (const auto *system : {&plain, &full, &limited})
  {
    REQUIRE(CDVDObservationTestAccess::detached(system->cdvdController()));
  }
}

TEST_CASE("CDVD polling result reads acknowledgements and rejection add no command events",
  "[iop][cdvd-trace]")
{
  for (const IOPWord instruction :
       {0x90220017u, 0x90220018u, 0xa0200008u, 0xa0200016u})
  {
    CAPTURE(instruction);
    NekoSystem system;
    prepareStore(system, instruction);
    system.startTrace();
    system.clockMasterCycle();
    const auto commands = IOPTraceTest::subsystemEvents(system, CDVD);
    REQUIRE(commands.size() == 2);
    REQUIRE(commands[0].type == COMPLETED);
    REQUIRE(commands[1].type == COMPLETED);
    REQUIRE(CDVDObservationTestAccess::detached(system.cdvdController()));
  }
}

TEST_CASE("CDVD command observations disarm after a later DMA exception",
  "[iop][cdvd-trace][save-state]")
{
  NekoSystem system;
  write(system, 4, 0);
  write(system, 0x16, 8);
  REQUIRE(system.sif().pushSIF1Quadword({{0x80002001, 4, 0, 0}}) ==
    SIFTransferOutcome::Completed);
  REQUIRE(system.iopSIF1DMAC().writeChannelControl(0x01000400) ==
    IOPDMAWriteOutcome::Completed);
  system.iopDMAController().writePriority(IOPDMABank::Secondary, 0x8000);
  NekoSystem plain;
  plain.loadState(system.saveState());
  system.startTrace();
  REQUIRE_THROWS_AS(system.clockMasterCycle(), IOPDMATransferError);
  REQUIRE_THROWS_AS(plain.clockMasterCycle(), IOPDMATransferError);
  IOPTraceTest::requireMachine(system, plain);
  REQUIRE(IOPTraceTest::subsystemEvents(system, CDVD).size() == 2);
  REQUIRE(CDVDObservationTestAccess::detached(system.cdvdController()));
  const auto retained = system.trace().size();
  write(system, 4, 0);
  REQUIRE(CDVDObservationTestAccess::detached(system.cdvdController()));
  REQUIRE(system.trace().size() == retained);
  system.iopDMAController().writePriority(IOPDMABank::Secondary, 0);
  system.startTrace();
  system.runMasterCycles(8);
  const auto commands = IOPTraceTest::subsystemEvents(system, CDVD);
  REQUIRE(commands.size() == 1);
  REQUIRE(commands[0].type == COMPLETED);
  REQUIRE(commands[0].masterCycle == 9);
  REQUIRE(CDVDObservationTestAccess::detached(system.cdvdController()));
}

TEST_CASE("CDVD transient journal is bounded and excluded from persistence",
  "[iop][cdvd-trace][save-state]")
{
  NekoSystem system;
  NekoObservationOrder order;
  order.next = 9;
  const auto initial = system.saveState();
  CDVDObservationTestAccess::enable(system.cdvdController(), &order);
  REQUIRE(system.saveState() == initial);
  write(system, 4, 0);
  REQUIRE(CDVDObservationTestAccess::count(system.cdvdController()) == 1);
  REQUIRE(CDVDObservationTestAccess::sequence(system.cdvdController(), 0) == 9);
  const auto pending = system.saveState();
  SaveStateMutation invalid(pending);
  invalid.writeScalar("system.iop.cdvd.normalPending", 2);
  invalid.refreshChecksum();
  REQUIRE_THROWS_AS(system.loadState(invalid), std::invalid_argument);
  REQUIRE(system.saveState() == pending);
  REQUIRE(CDVDObservationTestAccess::attached(system.cdvdController(), &order));
  REQUIRE(CDVDObservationTestAccess::count(system.cdvdController()) == 1);
  system.loadState(pending);
  REQUIRE(CDVDObservationTestAccess::attached(system.cdvdController(), &order));
  REQUIRE(CDVDObservationTestAccess::count(system.cdvdController()) == 0);
  REQUIRE(system.cdvdController().completeNormalCommand().completed);
  REQUIRE(CDVDObservationTestAccess::sequence(system.cdvdController(), 0) == 10);
  write(system, 4, 0);
  REQUIRE(system.cdvdController().completeNormalCommand().completed);
  REQUIRE(CDVDObservationTestAccess::count(system.cdvdController()) == 3);
  REQUIRE_THROWS_AS(system.iopBus().write8(BASE + 0x16, 8), std::logic_error);
  CDVDObservationTestAccess::disable(system.cdvdController());
  REQUIRE(CDVDObservationTestAccess::detached(system.cdvdController()));
  REQUIRE(system.cdvdController().completeSubcommand());
  REQUIRE(CDVDObservationTestAccess::detached(system.cdvdController()));
}

TEST_CASE("CDVD trace codec round trips all result bytes and ignores reserved words",
  "[cdvd-trace][trace]")
{
  using namespace NekoCDVDTrace;
  Command command;
  command.transport = Transport::Subcommand;
  command.opcode = 8;
  command.beforePhase = Phase::Pending;
  command.afterPhase = Phase::Result;
  command.beforeCause = 1;
  command.afterCause = 1;
  command.rtcTicks = UINT64_MAX;
  command.result = {{0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0}};
  const auto metadata = nekoPackCDVDTraceMetadata(command);
  const auto result = nekoPackCDVDTraceResult(command.result);
  REQUIRE(metadata == UINT64_C(0x010102010801));
  REQUIRE(result == UINT64_C(0xf0debc9a78563412));
  NekoTraceEvent event = {
    1, CDVD, COMPLETED, metadata | UINT64_C(0xffff000000000000),
    command.rtcTicks, result, UINT64_MAX
  };
  const auto decoded = nekoDecodeCDVDTrace(event);
  REQUIRE(decoded.transport == command.transport);
  REQUIRE(decoded.opcode == command.opcode);
  REQUIRE(decoded.beforePhase == command.beforePhase);
  REQUIRE(decoded.afterPhase == command.afterPhase);
  REQUIRE(decoded.beforeCause == command.beforeCause);
  REQUIRE(decoded.afterCause == command.afterCause);
  REQUIRE(decoded.rtcTicks == command.rtcTicks);
  REQUIRE(decoded.result == command.result);
  event.subsystem = NekoTraceSubsystem::IOP;
  REQUIRE_THROWS_AS(nekoDecodeCDVDTrace(event), std::invalid_argument);
  event.subsystem = CDVD;
  event.type = NekoTraceEventType::Progress;
  REQUIRE_THROWS_AS(nekoDecodeCDVDTrace(event), std::invalid_argument);
}

TEST_CASE("CDVD authored RTC draining and IRQ rearming continue across every trace mode",
  "[iop][cdvd-trace][cdvd-authored][save-state]")
{
  NekoSystem reference;
  std::array<IOPWord, 40> words = {{
    0x3c01bf40, // lui r1, CDVD upper
    0x34212000, // ori r1, r1, CDVD lower
    0x34020008, // ori r2, r0, ReadClock
    0xa0220016, // sb r2, SCMD
    0xa0200004, // sb r0, NCMD
    0x90230017, // lbu r3, SREADY
    0          // load delay
  }};
  for (IOPWord index = 0; index < 8; ++index)
  {
    words[7 + index * 2] = 0x90200018 | ((8 + index) << 16);
  }
  words[23] = 0x90300017; // lbu r16, SREADY
  words[25] = 0x90310008; // lbu r17, device cause
  words[27] = 0x34040001; // ori r4, r0, bit0
  words[28] = 0xa0240008; // sb r4, device cause W1C
  words[29] = 0x3c05bf80; // lui r5, INTC upper
  words[30] = 0x34a51070; // ori r5, r5, INTC lower
  words[31] = 0x2406fffb; // addiu r6, r0, ~source2
  words[32] = 0xaca60000; // sw r6, I_STAT W0C
  words[33] = 0xa0200004; // sb r0, NCMD again
  words[35] = 0x90320008; // lbu r18, device cause
  words[37] = 0x90330005; // lbu r19, NREADY
  words[39] = 0x34140001; // ori r20, r0, done
  installProgram(reference, 0x1000, words);
  SaveStateMutation seeded(reference.saveState());
  seeded.writeScalar("system.iop.cdvd.rtcTicks", PS2Clock::MASTER_HZ - 30);
  seeded.refreshChecksum();
  reference.loadState(seeded);
  reference.iopCore().startExecution(0xa0001000);
  const auto initial = reference.saveState();
  reference.startTrace();
  reference.runMasterCycles(313);
  REQUIRE(reference.iopCore().retiredInstructions() == 40);
  REQUIRE(reference.iopCore().stopReason() == IOPStopReason::None);
  const std::array<std::uint8_t, 8> expected = {{0, 1, 0, 0, 0, 1, 1, 0}};
  for (std::size_t index = 0; index < expected.size(); ++index)
  {
    REQUIRE(reference.iopCore().generalRegister(8 + index) == expected[index]);
  }
  REQUIRE(reference.iopCore().generalRegister(3) == 0);
  REQUIRE(reference.iopCore().generalRegister(16) == 0x40);
  REQUIRE(reference.iopCore().generalRegister(17) == 1);
  REQUIRE(reference.iopCore().generalRegister(18) == 1);
  REQUIRE(reference.iopCore().generalRegister(19) == 0x40);
  REQUIRE(reference.iopCore().generalRegister(20) == 1);
  REQUIRE(reference.iopInterruptController().status() == 4);
  const auto commands = IOPTraceTest::subsystemEvents(reference, CDVD);
  REQUIRE(commands.size() == 6);
  const std::array<std::uint64_t, 6> cycles = {{25, 33, 33, 41, 265, 273}};
  for (std::size_t index = 0; index < commands.size(); ++index)
  {
    REQUIRE(commands[index].masterCycle == cycles[index]);
  }
  REQUIRE(commands[1].type == COMPLETED);
  REQUIRE(commands[1].value2 == (EPOCH | 0x100));
  REQUIRE(commands[2].type == ACCEPTED);
  for (const std::uint64_t cut : {25, 32, 33, 57, 73, 185, 225, 257, 265})
  {
    CAPTURE(cut);
    NekoSystem source;
    source.loadState(initial);
    source.runMasterCycles(cut);
    if (cut == 57 || cut == 73 || cut == 185)
    {
      REQUIRE(source.iopCore().delayedResultContinuation().source ==
        IOPDelayedResultSource::Load);
    }
    if (cut == 225)
    {
      REQUIRE(source.iopBus().read8(BASE + 8).value == 0);
      REQUIRE(source.iopInterruptController().status() == 4);
    }
    if (cut == 257)
    {
      REQUIRE(source.iopInterruptController().status() == 0);
    }
    requireContinuationModes(reference, source);
  }
}

TEST_CASE("CDVD authored IRQ handlers acknowledge both latches return and service again",
  "[iop][cdvd-trace][cdvd-authored][save-state]")
{
  for (const auto firstAck : {0xa0270008u, 0xacb50000u})
  {
    CAPTURE(firstAck);
    NekoSystem reference;
    const std::array<IOPWord, 14> words = {{
      0x3c01bf40, // lui r1, CDVD upper
      0x34212000, // ori r1, r1, CDVD lower
      0x3c05bf80, // lui r5, INTC upper
      0x34a51070, // ori r5, r5, INTC lower
      0x34060004, // ori r6, r0, source2
      0xaca60004, // sw r6, I_MASK
      0x34070001, // ori r7, r0, enabled
      0xaca70008, // sw r7, I_CTRL
      0x34060401, // ori r6, r0, IEc | IM2
      0x40866000, // mtc0 r6, Status
      0xa0200004, // sb r0, NCMD
      0x34080001, // ori r8, r0, first return
      0xa0200004, // sb r0, NCMD again
      0x34090001  // ori r9, r0, second return
    }};
    const std::array<IOPWord, 8> handler = {{
      0x26940001, // addiu r20, r20, service count
      0x2415fffb, // addiu r21, r0, ~source2
      firstAck,
      firstAck == 0xa0270008u ? 0xacb50000u : 0xa0270008u,
      0x401a7000, // mfc0 r26, EPC
      0,          // load delay
      0x03400008, // jr r26
      0x42000010  // rfe in branch delay slot
    }};
    installProgram(reference, 0x1000, words);
    installProgram(reference, 0x80, handler);
    reference.iopCore().startExecution(0xa0001000);
    const auto initial = reference.saveState();
    reference.startTrace();
    reference.runMasterCycles(249);
    REQUIRE(reference.iopCore().stopReason() == IOPStopReason::None);
    REQUIRE(reference.iopCore().retiredInstructions() == 30);
    REQUIRE(reference.iopCore().programCounter() == 0xa0001038);
    REQUIRE(reference.iopCore().generalRegister(8) == 1);
    REQUIRE(reference.iopCore().generalRegister(9) == 1);
    REQUIRE(reference.iopCore().generalRegister(20) == 2);
    REQUIRE(reference.iopCore().generalRegister(26) == 0xa0001034);
    REQUIRE((reference.iopCore().status() & 1) == 1);
    REQUIRE(reference.iopBus().read8(BASE + 8).value == 0);
    REQUIRE(reference.iopInterruptController().status() == 0);
    const auto commands = IOPTraceTest::subsystemEvents(reference, CDVD);
    REQUIRE(commands.size() == 4);
    const std::array<std::uint64_t, 4> cycles = {{81, 89, 169, 177}};
    for (std::size_t index = 0; index < commands.size(); ++index)
    {
      REQUIRE(commands[index].masterCycle == cycles[index]);
      REQUIRE(commands[index].type == (index % 2 == 0 ? ACCEPTED : COMPLETED));
    }
    unsigned delivered = 0;
    for (const auto &event : reference.trace())
    {
      if (event.subsystem == NekoTraceSubsystem::IOP &&
          event.type == NekoTraceEventType::InterruptDelivered)
      {
        ++delivered;
        REQUIRE(event.masterCycle == (delivered == 1 ? 89 : 177));
      }
    }
    REQUIRE(delivered == 2);
    for (const std::uint64_t cut : {81, 89, 113, 129, 145, 153, 169, 177})
    {
      CAPTURE(cut);
      NekoSystem source;
      source.loadState(initial);
      source.runMasterCycles(cut);
      if (cut == 113)
      {
        REQUIRE(source.iopBus().read8(BASE + 8).value ==
          (firstAck == 0xa0270008u ? 0 : 1));
        REQUIRE(source.iopInterruptController().status() ==
          (firstAck == 0xa0270008u ? 4 : 0));
      }
      requireContinuationModes(reference, source);
    }
  }
}
