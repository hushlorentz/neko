#include <cstddef>
#include <cstdint>

#include "catch.hpp"
#include "iop/iop_trace_test_utils.hpp"

namespace
{
  constexpr std::uint32_t ENTRY = 0xa0001000;
  constexpr std::uint64_t BUDGET = 16384;
  enum class StartOrder { SenderFirst, ReceiverFirst };
  using DMAEndpoint = NekoSIFDMATrace::Endpoint;
  using DMAKind = NekoSIFDMATrace::Kind;
  using DMAOutcome = NekoSIFDMATrace::Outcome;

  void prepare(NekoSystem &system, StartOrder order)
  {
    // Original BIOS-free guests construct their descriptors/data, configure
    // DMA, acknowledge each local completion, and rearm through MMIO only.
    // Uncached aliases keep this a transport test, not implicit cache repair.
    const std::uint32_t ee[] = {
      0x3c01b000, // 00: EE5
      0x3421c000,
      0x3c02b000, // 02: D_CTRL / D_STAT / D_STADR
      0x3442e000,
      0x3c03b000, // 04: SIF
      0x3463f200,
      0x3c04a000, // 06: results
      0x34842000,
      0x3c05a000, // 08: receive buffer
      0x34a56000,
      0x34140000, // 10: round index
      0x34156000, // 11: physical receive address
      0x34160100, // 12: normal START
      0x3417000a, // 13: header + nine payload qwords
      0x34070011,
      0xac470000, // 15: DMAE, STS = SIF0; no stall drain
      0x3c070020,
      0xac470010, // 17: toggle CIM5 once
      0x8c870ff0, // 18: configured first-start delay
      0,
      0x10e00004, // 20: beq r7, r0, 25
      0,
      0x24e7ffff,
      0x14e0fffe,
      0,
      0xac350010, // 25: MADR
      0xac370020, // 26: QWC
      0xac360000, // 27: arm normal/chain receiver
      0x8c270000, // 28: poll STR
      0,
      0x30e70100,
      0x14e0fffc,
      0,
      0x8c470010,
      0,
      0xac870000, // 35: completion before acknowledgement
      0x34070020,
      0xac470010, // 37: acknowledge CIS5 only
      0x8c470010,
      0,
      0xac870004, // 40: retained masks / other channels
      0x8c470060,
      0,
      0xac87000c, // 43: CNTS progress
      0x26880001,
      0x8c670010, // 45: source's acknowledged round in SMCOM
      0,
      0x14e8fffd,
      0,
      0xac870008,
      0x1680001d, // 50: bne r20, r0, 80
      0,
      0x3c077000,
      0x34e70009,
      0x8ca80000,
      0,
      0x1507004b, // 56: raw stream header must remain in normal-mode RAM
      0,
      0x34076000,
      0x8ca80004,
      0,
      0x15070046,
      0,
      0x3407aaaa,
      0x8ca80008,
      0,
      0x15070041,
      0,
      0x3407bbbb,
      0x8ca8000c,
      0,
      0x1507003c,
      0,
      0x24a90010,
      0x340a0001,
      0x340b0024,
      0x0c000479, // 76: jal 121: validate all 36 payload words
      0,
      0x1000001b, // 78: beq r0, r0, 106
      0,
      0x34070001, // 80: second round is CNTS/END; third is zero length
      0x16870014, // 81: bne r20, r7, 102
      0,
      0x00a04825,
      0x340a0101,
      0x340b0005,
      0x0c000479, // 86: five real words
      0,
      0x340a0102,
      0x340b0003,
      0x0c000479, // 90: three retained lanes from the preceding qword
      0,
      0x340a0201,
      0x340b0007,
      0x0c000479, // 94: seven real words
      0,
      0x340a0204,
      0x340b0001,
      0x0c000479, // 98: retained last lane
      0,
      0x10000005,
      0,
      0x8cac0000, // 102: zero-length END must not write RAM
      0,
      0x1580001b,
      0,
      0x26870001, // 106: publish only a validated round
      0xac670000, // 107: MSCOM response
      0x26940001,
      0x26b50100,
      0x24a50100,
      0x24840010,
      0x34160104, // 112: subsequent rounds use destination chain
      0x34170000,
      0x2e870003,
      0x14e0ffa5, // 115: bne r7, r0, 25
      0,
      0x3407600d,
      0xac870000, // 118: completion marker at 0x2030
      0x1000ffff,
      0,
      0x8d2c0000, // 121: validate contiguous words, with an explicit failure path
      0,
      0x158a0008,
      0,
      0x25290004,
      0x254a0001,
      0x256bffff,
      0x1560fff8,
      0,
      0x03e00008,
      0,
      0x3407dead, // 132: failure never publishes a peer response
      0xac870000,
      0x1000ffff,
      0
    };
    const std::uint32_t iop[] = {
      0x3c01bf80, // 00: channel 9 MADR/BCR/CHCR/TADR
      0x34211520,
      0x3c02bf80, // 02: DPCR2 / DICR2
      0x34421570,
      0x3c03bf80, // 04: I_STAT / I_MASK / I_CTRL
      0x34631070,
      0x3c04bd00, // 06: SIF
      0x3c05a000,
      0x34a52000,
      0x3c06a000,
      0x34c64000, // 10: descriptor storage
      0x3c078000,
      0x34e75000,
      0xacc70000,
      0x34070024,
      0xacc70004,
      0x3c077000,
      0x34e70009,
      0xacc70008,
      0x34076000,
      0xacc7000c,
      0x3407aaaa,
      0xacc70010,
      0x3407bbbb,
      0xacc70014, // 24: terminal 36-word source, full raw EE prologue
      0x34075100,
      0xacc70100,
      0x34070005,
      0xacc70104,
      0x34070002,
      0xacc70108, // 30: CNTS, two destination qwords
      0x34076100,
      0xacc7010c,
      0x3c078000,
      0x34e75200,
      0xacc70110, // 35: unused EE lane overlaps next IOP descriptor L0
      0x34070007,
      0xacc70114, // 37: next IOP descriptor L1
      0x3c077000,
      0x34e70002,
      0xacc70118, // 40: second transmitted EE tag: END, two qwords
      0x34076120,
      0xacc7011c,
      0x3407cccc,
      0xacc70120,
      0x3407dddd,
      0xacc70124,
      0x3c078000,
      0x34e75300,
      0xacc70200,
      0xacc00204,
      0x3c077000,
      0xacc70208, // 52: terminal zero-word source emits an EE END(0)
      0x34076200,
      0xacc7020c,
      0xacc00210,
      0xacc00214,
      0x3c08a000,
      0x35085000,
      0x34090001,
      0x340a0024,
      0xad090000, // 61: construct all 36 real payload words
      0x25080004,
      0x25290001,
      0x254affff,
      0x1540fffb,
      0,
      0x3c08a000,
      0x35085100,
      0x34090101,
      0x340a0005,
      0xad090000,
      0x25080004,
      0x25290001,
      0x254affff,
      0x1540fffb,
      0,
      0x3c08a000,
      0x35085200,
      0x34090201,
      0x340a0007,
      0xad090000,
      0x25080004,
      0x25290001,
      0x254affff,
      0x1540fffb,
      0,
      0x34070008,
      0xac670004, // 88: unmask INTC DMA
      0x34070001,
      0xac670008, // 90: enable INTC output, not CPU exception admission
      0x34070800,
      0xac470000, // 92: enable channel 9 in DPCR2
      0x34140000,
      0x34154000,
      0x8ca70ff0,
      0,
      0x10e00004,
      0,
      0x24e7ffff,
      0x14e0fffe,
      0,
      0xac35000c, // 102: TADR, including explicit rearm after completion
      0x3c070100,
      0x34e70401,
      0xac270008, // 105: start packet-chain source
      0x8c270008,
      0,
      0x3c080100,
      0x00e83824,
      0x14e0fffb,
      0,
      0x8c470004,
      0,
      0xaca70000, // 114: DICR2 with stored completion masks disabled
      0x8c670000,
      0,
      0xaca70004,
      0x3c070400,
      0xac470004, // 119: acknowledge DICR2, not I_STAT
      0x8c670000,
      0,
      0xaca70008, // 122: I_STAT retains DMA
      0x2407fff7,
      0xac670000,
      0x8c670000,
      0,
      0xaca7000c, // 127: I_STAT after its own W0C acknowledgement
      0x26870001,
      0xac870010, // 129: publish acknowledged source round to SMCOM
      0x8c880000,
      0,
      0x1507fffd, // 132: await EE's validated MSCOM response
      0,
      0x26940001,
      0x26b50100,
      0x24a50010,
      0x2e870003,
      0x14e0ffdb, // 138: bne r7, r0, 102
      0,
      0x3407600d,
      0xaca70000, // 141: completion marker at 0x2030
      0x1000ffff,
      0
    };
    for (std::size_t index = 0; index < sizeof(ee) / sizeof(ee[0]); ++index)
      REQUIRE(system.eeBus().writeData32(0x1000 + 4 * index, ee[index]));
    for (std::size_t index = 0; index < sizeof(iop) / sizeof(iop[0]); ++index)
      REQUIRE(system.iopBus().write32(
        static_cast<IOPAddress>(0x1000 + 4 * index), iop[index]).status ==
        IOPBusStatus::Completed);
    REQUIRE(system.eeBus().writeData32(0x2ff0,
      order == StartOrder::SenderFirst ? 2048 : 0));
    REQUIRE(system.iopBus().write32(0x2ff0,
      order == StartOrder::ReceiverFirst ? 64 : 0).status == IOPBusStatus::Completed);
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
    REQUIRE(system.eeBus().read32(0x2030) == 0x600d);
    REQUIRE(system.iopBus().read32(0x2030).value == 0x600d);
    REQUIRE(system.eeCore().generalRegister(20).low == 3);
    REQUIRE(system.iopCore().generalRegister(20) == 3);
    for (std::uint32_t round = 0; round < 3; ++round)
    {
      CAPTURE(round);
      const auto results = 0x2000 + 16 * round;
      REQUIRE(system.eeBus().read32(results) ==
        (DMACStatus::CHANNEL_5 | DMACStatus::CHANNEL_5_MASK | otherEEStatus));
      REQUIRE(system.eeBus().read32(results + 4) ==
        (DMACStatus::CHANNEL_5_MASK | otherEEStatus));
      REQUIRE(system.eeBus().read32(results + 8) == round + 1);
      REQUIRE(system.eeBus().read32(results + 12) == (round == 0 ? 0x60a0 : 0x6120));
      REQUIRE(system.iopBus().read32(results).value == 0x04000000);
      REQUIRE(system.iopBus().read32(results + 4).value == 8);
      REQUIRE(system.iopBus().read32(results + 8).value == 8);
      REQUIRE(system.iopBus().read32(results + 12).value == 0);
    }
    const std::uint32_t header[] = {0x70000009, 0x6000, 0xaaaa, 0xbbbb};
    for (std::uint32_t index = 0; index < 4; ++index)
      REQUIRE(system.eeBus().read32(0x6000 + index * 4) == header[index]);
    for (std::uint32_t index = 0; index < 36; ++index)
    {
      CAPTURE(index);
      REQUIRE(system.eeBus().read32(0x6010 + index * 4) == index + 1);
    }
    const std::uint32_t chain[] = {
      0x101, 0x102, 0x103, 0x104, 0x105, 0x102, 0x103, 0x104,
      0x201, 0x202, 0x203, 0x204, 0x205, 0x206, 0x207, 0x204
    };
    for (std::uint32_t index = 0; index < 16; ++index)
    {
      CAPTURE(index);
      REQUIRE(system.eeBus().read32(0x6100 + index * 4) == chain[index]);
    }
    for (const auto address : {0x60a0u, 0x6140u, 0x6200u})
      REQUIRE(system.eeBus().read32(address) == 0);
    REQUIRE_FALSE(system.sif0DMAC().active());
    REQUIRE(system.sif0DMAC().quadwordCount() == 0);
    REQUIRE(system.sif0DMAC().memoryAddress() == 0x6200);
    REQUIRE_FALSE(system.iopSIF0DMAC().active());
    REQUIRE(system.iopSIF0DMAC().remainingWords() == 0);
    REQUIRE(system.iopSIF0DMAC().memoryAddress() == 0x5300);
    REQUIRE(system.iopSIF0DMAC().tagAddress() == 0x4210);
    REQUIRE_FALSE(system.sif1DMAC().active());
    REQUIRE_FALSE(system.iopSIF1DMAC().active());
    REQUIRE(system.sif().freeWordCount() == SIF::WORD_CAPACITY);
    REQUIRE(system.sif().control() == 0);
    REQUIRE(system.sif().readEE(SIFRegister::MSCOM) == 3);
    REQUIRE(system.sif().readIOP(SIFRegister::SMCOM) == 3);
    REQUIRE(system.sif().iopDoorbell() == 0);
    REQUIRE(system.dmacController().status() ==
      (DMACStatus::CHANNEL_5_MASK | otherEEStatus));
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

  void requireDMATrace(const NekoSystem &system)
  {
    constexpr std::uint32_t descriptors[] = {0x4000, 0x4100, 0x4110, 0x4200};
    constexpr std::uint32_t counts[] = {36, 5, 7, 0};
    constexpr std::uint32_t sources[] = {0x5000, 0x5100, 0x5200, 0x5300};
    constexpr std::uint32_t destinations[] = {0x6100, 0x6120, 0x6200};
    unsigned sourceTags = 0;
    unsigned destinationTags = 0;
    unsigned sourceWords = 0;
    unsigned packetWords = 0;
    unsigned destinationQuadwords = 0;
    unsigned sourceCompletions = 0;
    unsigned destinationCompletions = 0;
    unsigned waits = 0;
    unsigned attempts = 0;
    std::uint64_t lastEECycle = 0;
    std::uint64_t lastIOPCycle = 0;
    std::uint64_t sourceCompletionCycle = 0;
    for (const auto &event : system.trace())
    {
      if (event.subsystem != NekoTraceSubsystem::SIF ||
          event.type != NekoTraceEventType::DMABeat)
        continue;
      ++attempts;
      const auto endpoint = static_cast<DMAEndpoint>(event.value0 & 0xff);
      const auto kind = static_cast<DMAKind>(
        (event.value0 >> NekoSIFDMATrace::KIND_SHIFT) & 0xff);
      const auto outcome = static_cast<DMAOutcome>(
        (event.value0 >> NekoSIFDMATrace::OUTCOME_SHIFT) & 0xff);
      const auto before = static_cast<std::uint32_t>(event.value2);
      const auto after = static_cast<std::uint32_t>(event.value2 >> 32);
      const auto queuedBefore = static_cast<std::uint32_t>(event.value3);
      const auto queuedAfter = static_cast<std::uint32_t>(event.value3 >> 32);
      CAPTURE(event.masterCycle);
      CAPTURE(event.value0);
      REQUIRE((event.value0 >> NekoSIFDMATrace::FAILURE_SHIFT) == 0);
      REQUIRE(outcome != DMAOutcome::Rejected);
      REQUIRE(queuedBefore <= SIF::WORD_CAPACITY);
      REQUIRE(queuedAfter <= SIF::WORD_CAPACITY);
      if (endpoint == DMAEndpoint::IOPSIF0)
      {
        REQUIRE(event.masterCycle % 8 == 1);
        REQUIRE(event.masterCycle > lastIOPCycle);
        lastIOPCycle = event.masterCycle;
        REQUIRE((kind == DMAKind::SourceTag || kind == DMAKind::Payload));
      }
      else
      {
        REQUIRE(endpoint == DMAEndpoint::EESIF0);
        REQUIRE(event.masterCycle > lastEECycle);
        REQUIRE(event.masterCycle > lastIOPCycle);
        lastEECycle = event.masterCycle;
        REQUIRE((kind == DMAKind::DestinationTag || kind == DMAKind::Payload));
      }
      if (outcome == DMAOutcome::Wait)
      {
        ++waits;
        REQUIRE(after == before);
        REQUIRE(queuedAfter == queuedBefore);
        continue;
      }
      if (endpoint == DMAEndpoint::IOPSIF0)
      {
        if (kind == DMAKind::SourceTag)
        {
          REQUIRE(sourceTags < 4);
          REQUIRE(event.value1 == descriptors[sourceTags]);
          REQUIRE(before == 0);
          REQUIRE(after == counts[sourceTags]);
          REQUIRE(queuedAfter == queuedBefore + 4);
          REQUIRE(outcome == (sourceTags == 3 ? DMAOutcome::Completed : DMAOutcome::Accepted));
          packetWords = 0;
          ++sourceTags;
        }
        else
        {
          REQUIRE(sourceTags != 0);
          const auto packet = sourceTags - 1;
          REQUIRE(packetWords < counts[packet]);
          REQUIRE(event.value1 == sources[packet] + packetWords * 4);
          REQUIRE(before == counts[packet] - packetWords);
          REQUIRE(after + 1 == before);
          REQUIRE(queuedAfter == queuedBefore + (after == 0 ? 4 - packetWords % 4 : 1));
          REQUIRE(outcome == (after == 0 && packet != 1 ?
            DMAOutcome::Completed : DMAOutcome::Accepted));
          ++packetWords;
          ++sourceWords;
        }
        if (outcome == DMAOutcome::Completed)
        {
          ++sourceCompletions;
          sourceCompletionCycle = event.masterCycle;
        }
      }
      else
      {
        REQUIRE(queuedBefore == queuedAfter + 4);
        if (kind == DMAKind::DestinationTag)
        {
          REQUIRE(destinationTags < 3);
          REQUIRE(event.value1 == destinations[destinationTags]);
          REQUIRE(before == 0);
          REQUIRE(after == (destinationTags == 2 ? 0 : 2));
          REQUIRE(outcome == (destinationTags == 2 ?
            DMAOutcome::Completed : DMAOutcome::Accepted));
          ++destinationTags;
        }
        else
        {
          REQUIRE(destinationQuadwords < 14);
          REQUIRE(event.value1 == (destinationQuadwords < 10 ?
            0x6000 + destinationQuadwords * 16 :
            0x6100 + (destinationQuadwords - 10) * 16));
          REQUIRE(before == (destinationQuadwords < 10 ?
            10 - destinationQuadwords : 2 - (destinationQuadwords - 10) % 2));
          REQUIRE(after + 1 == before);
          REQUIRE(outcome == (destinationQuadwords == 9 || destinationQuadwords == 13 ?
            DMAOutcome::Completed : DMAOutcome::Accepted));
          ++destinationQuadwords;
        }
        if (outcome == DMAOutcome::Completed)
        {
          ++destinationCompletions;
          REQUIRE(sourceCompletions == destinationCompletions);
          REQUIRE(sourceCompletionCycle < event.masterCycle);
        }
      }
    }
    REQUIRE(sourceTags == 4);
    REQUIRE(destinationTags == 3);
    REQUIRE(sourceWords == 48);
    REQUIRE(destinationQuadwords == 14);
    REQUIRE(sourceCompletions == 3);
    REQUIRE(destinationCompletions == 3);
    REQUIRE(waits != 0);
    REQUIRE(attempts <= BUDGET + BUDGET / 8);
  }

  enum class Checkpoint
  {
    FullStream,
    PartialPayload,
    SourceCompleted,
    ReceiverCompleted,
    ChainAccepted,
    BetweenPackets,
    ChainSourceCompleted,
    ZeroSourceCompleted,
    PollLoad
  };

  void reachCheckpoint(NekoSystem &system, Checkpoint checkpoint)
  {
    bool reached = false;
    while (!reached && system.masterClockScheduler().currentCycle() < BUDGET)
    {
      system.clockMasterCycle();
      const auto &source = system.iopSIF0DMAC();
      const auto &receiver = system.sif0DMAC();
      switch (checkpoint)
      {
        case Checkpoint::FullStream:
          reached = source.active() && !receiver.active() &&
            system.sif().freeWordCount() == 0;
          break;
        case Checkpoint::PartialPayload:
          reached = source.tagAddress() == 0x4010 && source.remainingWords() == 17;
          break;
        case Checkpoint::SourceCompleted:
          reached = source.tagAddress() == 0x4010 && !source.active() && receiver.active() &&
            system.iopDMAController().interruptStatus(IOPDMABank::Secondary) == 0x04000000;
          break;
        case Checkpoint::ReceiverCompleted:
          reached = !receiver.active() &&
            (system.dmacController().status() & DMACStatus::CHANNEL_5) != 0;
          break;
        case Checkpoint::ChainAccepted:
          reached = source.tagAddress() == 0x4110 && receiver.quadwordCount() == 2 &&
            receiver.memoryAddress() == 0x6100;
          break;
        case Checkpoint::BetweenPackets:
          reached = source.active() && source.tagAddress() == 0x4110 &&
            source.remainingWords() == 0;
          break;
        case Checkpoint::ChainSourceCompleted:
          reached = source.tagAddress() == 0x4120 && !source.active() && receiver.active() &&
            system.iopDMAController().interruptStatus(IOPDMABank::Secondary) == 0x04000000;
          break;
        case Checkpoint::ZeroSourceCompleted:
          reached = source.tagAddress() == 0x4210 && !source.active() &&
            receiver.active() && system.sif().wordCount(SIFDirection::SIF0) == 4;
          break;
        case Checkpoint::PollLoad:
          reached = source.active() && source.tagAddress() == 0x4010 &&
            system.iopCore().delayedResultContinuation().source == IOPDelayedResultSource::Load &&
            system.iopCore().delayedResultContinuation().destination == 7;
          break;
      }
    }
    CAPTURE(static_cast<unsigned>(checkpoint));
    CAPTURE(system.eeCore().programCounter());
    CAPTURE(system.iopCore().programCounter());
    REQUIRE(reached);
    REQUIRE(system.eeCore().executionState() == EEExecutionState::Running);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
  }
}

TEST_CASE("SIF0 two-CPU guests configure validate acknowledge and rearm normal chain and zero packets",
  "[sif0][conformance][guest]")
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
      senderFirst |= system.iopSIF0DMAC().active() && !system.sif0DMAC().active() &&
        system.sif().freeWordCount() == 0 && system.iopCore().generalRegister(20) == 0;
      receiverFirst |= system.sif0DMAC().active() && !system.iopSIF0DMAC().active() &&
        system.iopSIF0DMAC().tagAddress() == 0 &&
        system.sif().wordCount(SIFDirection::SIF0) == 0;
    }
    requireCompleted(system);
    REQUIRE((order == StartOrder::SenderFirst ? senderFirst : receiverFirst));
    requireDMATrace(system);
  }
}

TEST_CASE("SIF0 authored guest replay is repeatable and tracing is observational",
  "[sif0][conformance][guest][trace]")
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

TEST_CASE("SIF0 authored guest restore preserves CPU and DMA acceptance boundaries",
  "[sif0][conformance][guest][save-state][trace]")
{
  for (const auto checkpoint : {Checkpoint::FullStream, Checkpoint::PartialPayload,
    Checkpoint::SourceCompleted, Checkpoint::ReceiverCompleted, Checkpoint::ChainAccepted,
    Checkpoint::BetweenPackets, Checkpoint::ZeroSourceCompleted, Checkpoint::PollLoad})
  {
    NekoSystem original;
    prepare(original, StartOrder::SenderFirst);
    original.startTrace();
    reachCheckpoint(original, checkpoint);
    const auto saved = original.saveState();
    NekoSystem restored;
    restored.startTrace();
    restored.loadState(saved);
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

TEST_CASE("SIF0 authored guest reset discards active work and repeats cold execution",
  "[sif0][conformance][guest][reset][trace]")
{
  for (const auto checkpoint : {Checkpoint::FullStream, Checkpoint::BetweenPackets,
    Checkpoint::ZeroSourceCompleted})
  {
    NekoSystem system;
    NekoSystem fresh;
    prepare(system, StartOrder::SenderFirst);
    system.startTrace();
    reachCheckpoint(system, checkpoint);
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

TEST_CASE("SIF0 guest payload and retained-lane validation rejects corruption before peer response",
  "[sif0][conformance][guest][validation]")
{
  for (const auto checkpoint : {Checkpoint::SourceCompleted, Checkpoint::ChainSourceCompleted})
  {
    NekoSystem system;
    prepare(system, StartOrder::SenderFirst);
    reachCheckpoint(system, checkpoint);
    const bool chain = checkpoint == Checkpoint::ChainSourceCompleted;
    REQUIRE(system.eeBus().writeData32(chain ? 0x6114 : 0x6010, 0xbad));
    system.runMasterCycles(BUDGET - system.masterClockScheduler().currentCycle());
    REQUIRE(system.eeCore().pendingException() == EEException::None);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.eeCore().executionState() == EEExecutionState::Running);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(system.eeBus().read32(chain ? 0x2010 : 0x2000) == 0xdead);
    REQUIRE(system.eeBus().read32(0x2030) == 0);
    REQUIRE(system.iopBus().read32(0x2030).value == 0);
    REQUIRE(system.sif().readEE(SIFRegister::MSCOM) == (chain ? 1 : 0));
    REQUIRE(system.sif().readIOP(SIFRegister::SMCOM) == (chain ? 2 : 1));
    REQUIRE(system.iopCore().generalRegister(20) == (chain ? 1 : 0));
  }
}

TEST_CASE("SIF0 authored guests preserve concurrent VIF1 GIF and scratchpad DMA completion",
  "[sif0][conformance][guest][dmac]")
{
  NekoSystem system;
  prepare(system, StartOrder::SenderFirst);
  reachCheckpoint(system, Checkpoint::FullStream);
  auto &bus = system.eeBus();
  for (std::uint32_t index = 0; index < 2; ++index)
  {
    REQUIRE(system.eeMemorySystem().writeScratchpadDMA128(0x200 + 16 * index,
      {0x12340000 + index, 0x56780000 + index}) == EEScratchpadAccessResult::Completed);
    REQUIRE(bus.writeDMAC128(0x9000 + 16 * index,
      {0x88880000 + index, 0x99990000 + index}));
    REQUIRE(bus.writeDMAC128(0xb000 + 16 * index, {}));
  }
  REQUIRE(bus.writeQuadword(0xc000, {{0x8001, 0x10000000, GIFRegisterDescriptor::AD, 0}}));
  REQUIRE(bus.writeQuadword(0xc010, {{
    static_cast<std::uint32_t>(GSPrimitiveType::Sprite), 0, GSRegisterAddress::PRIM, 0}}));
  struct Transfer { std::uint32_t base; std::uint32_t address; std::uint32_t control; };
  for (const auto transfer : {
    Transfer{EEMemoryMap::D1_CHCR, 0xb000, 0x101},
    Transfer{EEMemoryMap::D2_CHCR, 0xc000, 0x100},
    Transfer{EEMemoryMap::D8_CHCR, 0xa000, 0x100},
    Transfer{EEMemoryMap::D9_CHCR, 0x9000, 0x100}})
  {
    bus.write32(transfer.base + 0x10, transfer.address);
    bus.write32(transfer.base + 0x20, 2);
    if (transfer.base == EEMemoryMap::D8_CHCR) bus.write32(transfer.base + 0x80, 0x200);
    if (transfer.base == EEMemoryMap::D9_CHCR) bus.write32(transfer.base + 0x80, 0x300);
    bus.write32(transfer.base, transfer.control);
  }
  system.runMasterCycles(BUDGET - system.masterClockScheduler().currentCycle());
  requireCompleted(system, DMACStatus::CHANNEL_1 | DMACStatus::CHANNEL_2 |
    DMACStatus::CHANNEL_8 | DMACStatus::CHANNEL_9);
  REQUIRE(system.vif1DMAC().transferredQuadwordCount() == 2);
  REQUIRE(system.vif1().wordsIngested() == 8);
  REQUIRE(system.gifDMAC().transferredQuadwordCount() == 2);
  REQUIRE(system.gs().primitive().type == GSPrimitiveType::Sprite);
  REQUIRE_FALSE(system.fromScratchpadDMAC().active());
  REQUIRE_FALSE(system.toScratchpadDMAC().active());
  for (std::uint32_t index = 0; index < 2; ++index)
  {
    EEQuadword value = {};
    REQUIRE(bus.readDMAC128(0xa000 + 16 * index, &value));
    REQUIRE(value.low == 0x12340000 + index);
    REQUIRE(value.high == 0x56780000 + index);
    REQUIRE(system.eeMemorySystem().readScratchpadDMA128(0x300 + 16 * index, &value) ==
      EEScratchpadAccessResult::Completed);
    REQUIRE(value.low == 0x88880000 + index);
    REQUIRE(value.high == 0x99990000 + index);
  }
}
