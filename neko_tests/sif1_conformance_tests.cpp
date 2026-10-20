#include <cstddef>
#include <cstdint>

#include "catch.hpp"
#include "iop/iop_trace_test_utils.hpp"

namespace
{
  constexpr std::uint32_t ENTRY = 0xa0001000;
  constexpr std::uint64_t BUDGET = 16384;
  constexpr std::uint32_t WORDS = 36;

  enum class StartOrder { SenderFirst, ReceiverFirst };
  using DMAEndpoint = NekoSIFDMATrace::Endpoint;
  using DMAKind = NekoSIFDMATrace::Kind;
  using DMAOutcome = NekoSIFDMATrace::Outcome;

  void prepare(NekoSystem &system, StartOrder order)
  {
    // Original BIOS-free programs. Only guest stores configure SIF1 DMA and
    // acknowledge completion. Uncached aliases avoid manufacturing coherence.
    const std::uint32_t ee[] = {
      0x3c01b000, // 00: lui r1, 0xb000
      0x3421c400, // 01: ori r1, r1, 0xc400: EE6
      0x3c02b000,
      0x3442e000, // 03: D_CTRL / D_STAT
      0x3c03b000,
      0x3463f200, // 05: SIF registers
      0x3c04a000,
      0x34843000, // 07: uncached source packet
      0x3c05a000,
      0x34a52000, // 09: uncached results
      0x34140000, // 10: round index
      0x34100001, // 11: first payload value
      0x3c118000,
      0x36313000, // 13: terminal IOP destination
      0x34120001,
      0xac520000, // 15: enable EE DMA
      0x3c120040,
      0xac520010, // 17: toggle CIM6 on once
      0xac910000, // 18: packet header destination
      0x34060027, // 19: raw count 39 masks to 36 words, not 40
      0xac860004,
      0xac800008,
      0xac80000c,
      0x24860010, // 23: payload cursor
      0x02003825, // 24: or r7, r16, r0
      0x34080024,
      0xacc70000, // 26: fill payload
      0x24c60004,
      0x24e70001,
      0x2508ffff,
      0x1500fffb, // 30: bne r8, r0, 26
      0,
      0x8c89fff0, // 32: load configured guest delay
      0,
      0x11200004, // 34: beq r9, r0, 39
      0,
      0x2529ffff, // 36: delay loop
      0x1520fffe,
      0,
      0x16800007, // 39: bne r20, r0, 47: second round uses source chain
      0,
      0x34063000,
      0xac260010, // 42: normal MADR
      0x3406000a,
      0xac260020, // 44: QWC = header + nine payload qwords
      0x10000011, // 45: beq r0, r0, 63
      0x34060100, // 46: normal START in delay slot
      0x3c06a000,
      0x34c64000, // 48: source-chain storage
      0x3c071000,
      0x34e70001, // 50: CNT, QWC 1
      0xacc70000,
      0xacc00004,
      0xacd10010, // 53: inline IOP header
      0x34070027,
      0xacc70014,
      0x34070009, // 56: REFE, QWC 9
      0xacc70020,
      0x34073010,
      0xacc70024, // 59: discontiguous physical payload address
      0x34074000,
      0xac270030, // 61: TADR
      0x34060104, // 62: chain START, TTE disabled
      0xac260000, // 63: start sender
      0x8c260000, // 64: poll sender STR
      0,
      0x30c60100,
      0x14c0fffc,
      0,
      0x8c460010,
      0,
      0xaca60000, // 71: save completion before acknowledgement
      0x34070040,
      0xac470010, // 73: acknowledge only CIS6
      0x8c460010,
      0,
      0xaca60004, // 76: save retained mask / unrelated status
      0x26870001,
      0x8c660010, // 78: wait for IOP's validated round response
      0,
      0x14c7fffd,
      0,
      0xaca60008, // 82: save response
      0x26940001,
      0x26100100,
      0x26310100,
      0x24a50010,
      0x2e860002,
      0x14c0ffb9, // 88: bne r6, r0, 18
      0,
      0x3406600d,
      0xaca60000, // 91: completion marker at 0x2020
      0x1000ffff, // 92: bounded host-observed success loop
      0
    };
    const std::uint32_t iop[] = {
      0x3c01bf80,
      0x34211538, // 01: channel 10 CHCR
      0x3c02bf80,
      0x34421570, // 03: DPCR2 / DICR2
      0x3c03bf80,
      0x34631070, // 05: I_STAT / I_MASK / I_CTRL
      0x3c04bd00, // 06: SIF
      0x3c05a000,
      0x34a52000, // 08: results
      0x3c06a000,
      0x34c63000, // 10: payload destination
      0x34140000,
      0x34100001,
      0x34070008,
      0xac670004, // 14: unmask INTC DMA
      0x34070001,
      0xac670008, // 16: enable INTC output; CPU exceptions remain disabled
      0x8cc7fff0, // 17: configured per-round delay
      0,
      0x10e00004, // 19: beq r7, r0, 24
      0,
      0x24e7ffff,
      0x14e0fffe,
      0,
      0x34078000,
      0xac470000, // 25: enable channel 10 in DPCR2
      0x3c070100,
      0x34e70400,
      0xac270000, // 28: arm receiver; no implicit acknowledgement
      0x8c270000, // 29: poll TR
      0,
      0x3c080100,
      0x00e83824,
      0x14e0fffb, // 33: bne r7, r0, 29
      0,
      0x8c470004,
      0,
      0xaca70000, // 37: DICR2 completion with stored DMA masks disabled
      0x8c670000,
      0,
      0xaca70004, // 40: INTC cause before acknowledgement
      0x3c070800,
      0xac470004, // 42: acknowledge DICR2, not I_STAT
      0x8c670000,
      0,
      0xaca70008, // 45: I_STAT must still retain DMA
      0x2407fff7,
      0xac670000, // 47: acknowledge only DMA with I_STAT W0C
      0x8c670000,
      0,
      0xaca7000c,
      0x00c03825, // 51: validation cursor
      0x02004025, // 52: expected first value
      0x34090024,
      0x8cea0000, // 54: validate all received words
      0,
      0x15480013, // 56: bne r10, r8, 76: failure marker
      0,
      0x24e70004,
      0x25080001,
      0x2529ffff,
      0x1520fff8, // 61: bne r9, r0, 54
      0,
      0x26870001,
      0xac870010, // 64: publish validated round to SMCOM
      0x26940001,
      0x26100100,
      0x24c60100,
      0x24a50010,
      0x2e870002,
      0x14e0ffca, // 70: bne r7, r0, 17: rearm for next header
      0,
      0x3407600d,
      0xaca70000,
      0x1000ffff, // 74: success loop
      0,
      0x3407dead,
      0xaca70000, // 77: failed payload validation, never publish response
      0x1000ffff,
      0
    };
    for (std::size_t index = 0; index < sizeof(ee) / sizeof(ee[0]); ++index)
    {
      REQUIRE(system.eeBus().writeData32(0x1000 + 4 * index, ee[index]));
    }
    for (std::size_t index = 0; index < sizeof(iop) / sizeof(iop[0]); ++index)
    {
      REQUIRE(system.iopBus().write32(
        static_cast<IOPAddress>(0x1000 + 4 * index), iop[index]).status ==
        IOPBusStatus::Completed);
    }
    REQUIRE(system.eeBus().writeData32(0x2ff0,
      order == StartOrder::ReceiverFirst ? 512 : 0));
    for (const auto address : {0x2ff0u, 0x30f0u})
    {
      REQUIRE(system.iopBus().write32(address,
        order == StartOrder::SenderFirst ? 64 : 0).status == IOPBusStatus::Completed);
    }
    system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
    system.eeCore().startExecution(ENTRY);
    system.iopCore().startExecution(ENTRY);
  }

  void requireCompleted(NekoSystem &system, std::uint32_t otherEEStatus = 0)
  {
    REQUIRE(system.eeCore().executionState() == EEExecutionState::Running);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(system.eeCore().pendingException() == EEException::None);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.eeBus().read32(0x2020) == 0x600d);
    REQUIRE(system.iopBus().read32(0x2020).value == 0x600d);
    REQUIRE(system.eeCore().generalRegister(20).low == 2);
    REQUIRE(system.iopCore().generalRegister(20) == 2);
    for (std::uint32_t round = 0; round < 2; ++round)
    {
      CAPTURE(round);
      const auto results = 0x2000 + 16 * round;
      REQUIRE(system.eeBus().read32(results) ==
        (DMACStatus::CHANNEL_6 | DMACStatus::CHANNEL_6_MASK | otherEEStatus));
      REQUIRE(system.eeBus().read32(results + 4) ==
        (DMACStatus::CHANNEL_6_MASK | otherEEStatus));
      REQUIRE(system.eeBus().read32(results + 8) == round + 1);
      REQUIRE(system.iopBus().read32(results).value == 0x08000000);
      REQUIRE(system.iopBus().read32(results + 4).value == 8);
      REQUIRE(system.iopBus().read32(results + 8).value == 8);
      REQUIRE(system.iopBus().read32(results + 12).value == 0);
      for (IOPAddress index = 0; index < WORDS; ++index)
      {
        CAPTURE(index);
        const auto result = system.iopBus().read32(0x3000 + round * 0x100 + index * 4);
        REQUIRE(result.status == IOPBusStatus::Completed);
        REQUIRE(result.value == 1 + round * 0x100 + index);
      }
      REQUIRE(system.iopBus().read32(0x3090 + round * 0x100).value == 0);
    }
    REQUIRE_FALSE(system.sif1DMAC().active());
    REQUIRE_FALSE(system.iopSIF1DMAC().active());
    REQUIRE(system.sif().freeWordCount() == SIF::WORD_CAPACITY);
    REQUIRE(system.sif().control() == 0);
    REQUIRE(system.sif().readIOP(SIFRegister::SMCOM) == 2);
    REQUIRE(system.dmacController().status() ==
      (DMACStatus::CHANNEL_6_MASK | otherEEStatus));
    REQUIRE(system.iopDMAController().interruptStatus(IOPDMABank::Secondary) == 0);
    REQUIRE(system.iopInterruptController().status() == 0);
    REQUIRE(system.iopInterruptController().mask() == 8);
    REQUIRE(system.iopInterruptController().control() == 1);
    REQUIRE_FALSE(system.iopInterruptController().interruptPending());
    REQUIRE(system.interruptController().status() == 0);
  }

  void requireSameMachine(const NekoSystem &actual, const NekoSystem &expected)
  {
    REQUIRE(actual.sifStateHash() == expected.sifStateHash());
    IOPTraceTest::requireMachine(actual, expected);
  }

  enum class Checkpoint
  {
    FullStream,
    HeaderAccepted,
    PartialPayload,
    SenderCompleted,
    ReceiverCompleted,
    ChainTagAccepted
  };

  void reachCheckpoint(NekoSystem &system, Checkpoint checkpoint)
  {
    bool reached = false;
    while (!reached && system.masterClockScheduler().currentCycle() < BUDGET)
    {
      system.clockMasterCycle();
      const auto &sender = system.sif1DMAC();
      const auto &receiver = system.iopSIF1DMAC();
      switch (checkpoint)
      {
        case Checkpoint::FullStream:
          reached = sender.active() && !receiver.active() &&
            system.sif().freeWordCount() == 0;
          break;
        case Checkpoint::HeaderAccepted:
          reached = receiver.remainingWords() == WORDS &&
            receiver.memoryAddress() == 0x3000;
          break;
        case Checkpoint::PartialPayload:
          reached = receiver.remainingWords() == 17;
          break;
        case Checkpoint::SenderCompleted:
          reached = receiver.active() && !sender.active() &&
            (system.dmacController().status() & DMACStatus::CHANNEL_6) != 0;
          break;
        case Checkpoint::ReceiverCompleted:
          reached = !receiver.active() &&
            system.iopDMAController().interruptStatus(IOPDMABank::Secondary) != 0;
          break;
        case Checkpoint::ChainTagAccepted:
          reached = system.eeCore().generalRegister(20).low == 1 &&
            sender.active() && sender.tagAddress() == 0x4020 &&
            sender.memoryAddress() == 0x4010 && sender.quadwordCount() == 1;
          break;
      }
    }
    CAPTURE(static_cast<unsigned>(checkpoint));
    REQUIRE(reached);
    REQUIRE(system.eeCore().executionState() == EEExecutionState::Running);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
  }
}

TEST_CASE("SIF1 two-CPU guests configure transfer acknowledge and rearm normal and chain DMA",
  "[sif1][conformance][guest]")
{
  for (const auto order : {StartOrder::SenderFirst, StartOrder::ReceiverFirst})
  {
    CAPTURE(static_cast<unsigned>(order));
    NekoSystem system;
    prepare(system, order);
    system.startTrace();
    bool senderFirst = false;
    bool receiverFirst = false;
    for (std::uint64_t cycle = 0; cycle < BUDGET; ++cycle)
    {
      system.clockMasterCycle();
      senderFirst |= system.sif1DMAC().active() &&
        !system.iopSIF1DMAC().active() && system.sif().freeWordCount() == 0 &&
        system.eeCore().generalRegister(20).low == 0;
      receiverFirst |= system.iopSIF1DMAC().active() &&
        system.iopSIF1DMAC().remainingWords() == 0 &&
        !system.sif1DMAC().active() && system.sif1DMAC().memoryAddress() == 0 &&
        system.sif().wordCount(SIFDirection::SIF1) == 0 &&
        system.eeCore().generalRegister(20).low == 0;
    }
    requireCompleted(system);
    REQUIRE((order == StartOrder::SenderFirst ? senderFirst : receiverFirst));
    std::size_t eeCompletions = 0;
    std::size_t iopCompletions = 0;
    std::size_t tags = 0;
    std::size_t headers = 0;
    std::size_t payloadQuadwords = 0;
    std::size_t payloadWords = 0;
    std::size_t waits = 0;
    std::uint64_t senderCompletionCycle = 0;
    for (const auto &event : system.trace())
    {
      if (event.subsystem != NekoTraceSubsystem::SIF ||
          event.type != NekoTraceEventType::DMABeat)
      {
        continue;
      }
      const auto endpoint = static_cast<DMAEndpoint>(event.value0 & 0xff);
      const auto kind = static_cast<DMAKind>(
        (event.value0 >> NekoSIFDMATrace::KIND_SHIFT) & 0xff);
      const auto outcome = static_cast<DMAOutcome>(
        (event.value0 >> NekoSIFDMATrace::OUTCOME_SHIFT) & 0xff);
      REQUIRE(outcome != DMAOutcome::Rejected);
      if (outcome == DMAOutcome::Wait)
      {
        ++waits;
        continue;
      }
      if (endpoint == DMAEndpoint::EE)
      {
        if (kind == DMAKind::SourceTag) ++tags;
        if (kind == DMAKind::Payload) ++payloadQuadwords;
        if (outcome == DMAOutcome::Completed)
        {
          ++eeCompletions;
          senderCompletionCycle = event.masterCycle;
          REQUIRE((event.value3 >> 32) != 0);
        }
      }
      else
      {
        if (kind == DMAKind::Header)
        {
          ++headers;
          REQUIRE((event.value2 >> 32) == WORDS);
        }
        if (kind == DMAKind::Payload) ++payloadWords;
        if (outcome == DMAOutcome::Completed)
        {
          ++iopCompletions;
          REQUIRE(eeCompletions == iopCompletions);
          REQUIRE(senderCompletionCycle < event.masterCycle);
        }
      }
    }
    REQUIRE(tags == 2);
    REQUIRE(headers == 2);
    REQUIRE(payloadQuadwords == 20);
    REQUIRE(payloadWords == 2 * WORDS);
    REQUIRE(waits != 0);
    REQUIRE(eeCompletions == 2);
    REQUIRE(iopCompletions == 2);
  }
}

TEST_CASE("SIF1 authored guest replay is repeatable and tracing is observational",
  "[sif1][conformance][guest][trace]")
{
  for (const auto order : {StartOrder::SenderFirst, StartOrder::ReceiverFirst})
  {
    NekoSystem plain;
    NekoSystem traced;
    NekoSystem repeated;
    for (auto *system : {&plain, &traced, &repeated}) prepare(*system, order);
    traced.startTrace();
    repeated.startTrace();
    for (auto *system : {&plain, &traced, &repeated}) system->runMasterCycles(BUDGET);
    requireCompleted(plain);
    requireSameMachine(traced, plain);
    requireSameMachine(repeated, plain);
    IOPTraceTest::requireEvents(repeated.trace(), traced.trace());
    REQUIRE(plain.trace().empty());
  }
}

TEST_CASE("SIF1 authored guest restore preserves live instructions packets and acknowledgements",
  "[sif1][conformance][guest][save-state][trace]")
{
  for (const auto checkpoint : {Checkpoint::FullStream, Checkpoint::HeaderAccepted,
         Checkpoint::PartialPayload, Checkpoint::SenderCompleted,
         Checkpoint::ReceiverCompleted, Checkpoint::ChainTagAccepted})
  {
    NekoSystem original;
    prepare(original, StartOrder::SenderFirst);
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
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
  }
}

TEST_CASE("SIF1 authored guest reset drops active work and repeats the cold execution",
  "[sif1][conformance][guest][reset][trace]")
{
  for (const auto checkpoint : {Checkpoint::FullStream, Checkpoint::PartialPayload,
         Checkpoint::ReceiverCompleted, Checkpoint::ChainTagAccepted})
  {
    NekoSystem system;
    NekoSystem fresh;
    prepare(system, StartOrder::SenderFirst);
    system.startTrace();
    reachCheckpoint(system, checkpoint);
    REQUIRE_FALSE(system.trace().empty());
    system.reset();
    requireSameMachine(system, fresh);
    REQUIRE(system.trace().empty());
    REQUIRE_FALSE(system.traceEnabled());
    for (auto *machine : {&system, &fresh})
    {
      prepare(*machine, StartOrder::ReceiverFirst);
      machine->startTrace();
      machine->runMasterCycles(BUDGET);
      requireCompleted(*machine);
    }
    requireSameMachine(system, fresh);
    IOPTraceTest::requireEvents(system.trace(), fresh.trace());
  }
}

TEST_CASE("SIF1 authored guests coexist with VIF1 GIF and both scratchpad DMA paths",
  "[sif1][conformance][guest][dmac]")
{
  NekoSystem system;
  prepare(system, StartOrder::SenderFirst);
  reachCheckpoint(system, Checkpoint::HeaderAccepted);
  auto &bus = system.eeBus();
  REQUIRE(system.sif1DMAC().memoryAddress() == 0x3080);
  REQUIRE(system.sif1DMAC().quadwordCount() == 2);
  REQUIRE(system.sif().freeWordCount() == 4);

  // The next SIF grant must see fromSPR's physical writes, not these poisons.
  for (std::uint32_t index = 0; index < 8; ++index)
  {
    REQUIRE(bus.writeData32(0x3080 + 4 * index, 0xbad));
  }
  const EEQuadword tail[] = {
    {UINT64_C(0x0000001e0000001d), UINT64_C(0x000000200000001f)},
    {UINT64_C(0x0000002200000021), UINT64_C(0x0000002400000023)}
  };
  for (std::uint32_t index = 0; index < 2; ++index)
  {
    REQUIRE(system.eeMemorySystem().writeScratchpadDMA128(
      0x200 + 16 * index, tail[index]) == EEScratchpadAccessResult::Completed);
    REQUIRE(bus.writeDMAC128(0x6000 + 16 * index,
      {0x12340000 + index, 0x56780000 + index}));
    REQUIRE(bus.writeDMAC128(0x8000 + 16 * index, {}));
  }
  REQUIRE(bus.writeQuadword(0x7000, {{
    0x8001, 0x10000000, GIFRegisterDescriptor::AD, 0}}));
  REQUIRE(bus.writeQuadword(0x7010, {{
    static_cast<std::uint32_t>(GSPrimitiveType::Sprite), 0,
    GSRegisterAddress::PRIM, 0}}));
  struct Transfer
  {
    std::uint32_t base;
    std::uint32_t address;
    std::uint32_t control;
  };
  for (const auto transfer : {
    Transfer{EEMemoryMap::D1_CHCR, 0x8000, 0x101},
    Transfer{EEMemoryMap::D2_CHCR, 0x7000, 0x100},
    Transfer{EEMemoryMap::D8_CHCR, 0x3080, 0x100},
    Transfer{EEMemoryMap::D9_CHCR, 0x6000, 0x100}})
  {
    bus.write32(transfer.base + 0x10, transfer.address);
    bus.write32(transfer.base + 0x20, 2);
    if (transfer.base == EEMemoryMap::D8_CHCR) bus.write32(transfer.base + 0x80, 0x200);
    if (transfer.base == EEMemoryMap::D9_CHCR) bus.write32(transfer.base + 0x80, 0x300);
    bus.write32(transfer.base, transfer.control);
  }
  system.startTrace();
  system.clockMasterCycle();
  REQUIRE(system.sif1DMAC().memoryAddress() == 0x3090);
  REQUIRE(system.sif1DMAC().quadwordCount() == 1);
  REQUIRE(system.sif().freeWordCount() == 0);
  system.runMasterCycles(BUDGET - system.masterClockScheduler().currentCycle());
  const auto legacyStatus = DMACStatus::CHANNEL_1 | DMACStatus::CHANNEL_2 |
    DMACStatus::CHANNEL_8 | DMACStatus::CHANNEL_9;
  requireCompleted(system, legacyStatus);
  REQUIRE(system.vif1DMAC().transferredQuadwordCount() == 2);
  REQUIRE(system.vif1().wordsIngested() == 8);
  REQUIRE(system.gifDMAC().transferredQuadwordCount() == 2);
  REQUIRE(system.gs().primitive().type == GSPrimitiveType::Sprite);
  REQUIRE_FALSE(system.fromScratchpadDMAC().active());
  REQUIRE_FALSE(system.toScratchpadDMAC().active());
  REQUIRE(system.fromScratchpadDMAC().memoryAddress() == 0x30a0);
  REQUIRE(system.toScratchpadDMAC().memoryAddress() == 0x6020);
  REQUIRE(system.fromScratchpadDMAC().scratchpadAddress() == 0x220);
  REQUIRE(system.toScratchpadDMAC().scratchpadAddress() == 0x320);
  for (std::uint32_t index = 0; index < 2; ++index)
  {
    EEQuadword result = {};
    REQUIRE(system.eeMemorySystem().readScratchpadDMA128(0x300 + 16 * index,
      &result) == EEScratchpadAccessResult::Completed);
    REQUIRE(result.low == 0x12340000 + index);
    REQUIRE(result.high == 0x56780000 + index);
  }
}
