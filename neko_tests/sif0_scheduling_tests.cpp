#include <cstdint>
#include <stdexcept>
#include <vector>

#include "catch.hpp"
#include "iop/iop_trace_test_utils.hpp"
#include "save_state_mutation.hpp"

namespace
{
  constexpr auto BANK = IOPDMABank::Secondary;
  constexpr std::uint32_t START = DMACChannelControl::START;
  constexpr std::uint32_t CHAIN = DMACChannelControl::CHAIN_MODE;
  constexpr SIFQuadword DATA = {{1, 2, 3, 4}};

  void push(NekoSystem &system, const SIFQuadword &words)
  {
    REQUIRE(system.sif().pushSIF0Prologue(words) == SIFTransferOutcome::Completed);
  }

  void writeIOP(NekoSystem &system, IOPAddress address, IOPWord value)
  {
    REQUIRE(system.iopBus().write32(address, value).status == IOPBusStatus::Completed);
  }

  void source(NekoSystem &system, unsigned count = 4,
    std::uint32_t tag = 0x70000001)
  {
    writeIOP(system, 0x1000, 0x80002000);
    writeIOP(system, 0x1004, count);
    writeIOP(system, 0x1008, tag);
    writeIOP(system, 0x100c, 0x3000);
    writeIOP(system, 0x1010, 0xaaaa);
    writeIOP(system, 0x1014, 0xbbbb);
    for (unsigned index = 0; index < count; ++index)
      writeIOP(system, 0x2000 + index * 4, index + 1);
    REQUIRE(system.iopSIF0DMAC().writeTagAddress(0x1000) == IOPDMAWriteOutcome::Completed);
    REQUIRE(system.iopSIF0DMAC().writeChannelControl(0x01000401) ==
      IOPDMAWriteOutcome::Completed);
    system.iopDMAController().writePriority(BANK, 0x800);
  }

  void receiver(NekoSystem &system, unsigned count = 0)
  {
    system.sif0DMAC().writeMemoryAddress(0x3000);
    system.sif0DMAC().writeQuadwordCount(count);
    system.sif0DMAC().writeChannelControl(START | CHAIN);
    system.dmacController().writeControl(1);
  }

  void eeSender(NekoSystem &system, unsigned count = 2,
    std::uint32_t address = 0x5000)
  {
    REQUIRE(system.eeBus().writeQuadword(0x5000, {{0x80006000, 4, 0, 0}}));
    REQUIRE(system.eeBus().writeQuadword(0x5010, DATA));
    system.sif1DMAC().writeMemoryAddress(address);
    system.sif1DMAC().writeQuadwordCount(count);
    system.sif1DMAC().writeChannelControl(START);
    system.dmacController().writeControl(1);
  }

  void iopReceiver(NekoSystem &system)
  {
    REQUIRE(system.iopSIF1DMAC().writeChannelControl(0x01000400) ==
      IOPDMAWriteOutcome::Completed);
  }

  void requireData(NekoSystem &system)
  {
    EEQuadword value = {};
    REQUIRE(system.eeBus().readDMAC128(0x3000, &value));
    REQUIRE(value.low == UINT64_C(0x0000000200000001));
    REQUIRE(value.high == UINT64_C(0x0000000400000003));
  }

  std::vector<NekoTraceEvent> beats(const NekoSystem &system)
  {
    std::vector<NekoTraceEvent> result;
    for (const auto &event : system.trace())
      if (event.subsystem == NekoTraceSubsystem::SIF &&
          event.type == NekoTraceEventType::DMABeat)
        result.push_back(event);
    return result;
  }

  NekoTraceEvent beat(std::uint64_t cycle, std::uint64_t metadata,
    std::uint32_t address, std::uint32_t before, std::uint32_t after,
    std::uint32_t queuedBefore, std::uint32_t queuedAfter)
  {
    return {cycle, NekoTraceSubsystem::SIF, NekoTraceEventType::DMABeat,
      metadata, address, before | (static_cast<std::uint64_t>(after) << 32),
      queuedBefore | (static_cast<std::uint64_t>(queuedAfter) << 32)};
  }

  void bidirectionalPackets(NekoSystem &system, unsigned sif0Words,
    unsigned sif1Words)
  {
    source(system, sif0Words, 0x70000000 | (sif0Words / 4));
    eeSender(system, 1 + sif1Words / 4);
    REQUIRE(system.eeBus().writeQuadword(0x5000, {{0x80006000, sif1Words, 0, 0}}));
    for (unsigned index = 0; index < sif1Words; index += 4)
      REQUIRE(system.eeBus().writeQuadword(0x5010 + index * 4,
        {{0x10000000 + index, 0x10000001 + index,
          0x10000002 + index, 0x10000003 + index}}));
    system.iopDMAController().writePriority(BANK, 0x8800);
  }

  void drainBidirectional(NekoSystem &system)
  {
    unsigned elapsed = 0;
    while ((system.sif0DMAC().active() || system.sif1DMAC().active() ||
        system.iopSIF0DMAC().active() || system.iopSIF1DMAC().active()) &&
        elapsed < 4096)
    {
      system.clockMasterCycle();
      REQUIRE(system.sif().wordCount(SIFDirection::SIF0) +
        system.sif().wordCount(SIFDirection::SIF1) +
        system.sif().freeWordCount() == SIF::WORD_CAPACITY);
      ++elapsed;
    }
    REQUIRE_FALSE(system.sif0DMAC().active());
    REQUIRE_FALSE(system.sif1DMAC().active());
    REQUIRE_FALSE(system.iopSIF0DMAC().active());
    REQUIRE_FALSE(system.iopSIF1DMAC().active());
    REQUIRE(system.sif().freeWordCount() == SIF::WORD_CAPACITY);
  }

  void requireBidirectionalPayloads(NekoSystem &system, unsigned sif0Words,
    unsigned sif1Words)
  {
    for (unsigned index = 0; index < sif0Words; index += 4)
    {
      EEQuadword value = {};
      REQUIRE(system.eeBus().readDMAC128(0x3000 + index * 4, &value));
      REQUIRE(value.low == (static_cast<std::uint64_t>(index + 2) << 32 | (index + 1)));
      REQUIRE(value.high == (static_cast<std::uint64_t>(index + 4) << 32 | (index + 3)));
    }
    for (unsigned index = 0; index < sif1Words; ++index)
    {
      const auto value = system.iopBus().read32(0x6000 + index * 4);
      REQUIRE(value.status == IOPBusStatus::Completed);
      REQUIRE(value.value == 0x10000000 + index);
    }
    EEQuadword sentinel = {};
    REQUIRE(system.eeBus().readDMAC128(0x3000 + sif0Words * 4, &sentinel));
    REQUIRE(sentinel.low == 0);
    REQUIRE(sentinel.high == 0);
    const auto sentinelIOP = system.iopBus().read32(0x6000 + sif1Words * 4);
    REQUIRE(sentinelIOP.status == IOPBusStatus::Completed);
    REQUIRE(sentinelIOP.value == 0);
    REQUIRE(system.dmacController().status() ==
      (DMACStatus::CHANNEL_5 | DMACStatus::CHANNEL_6));
    REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0x0c000000);
    REQUIRE(system.iopInterruptController().status() == 8);
  }

  void bidirectionalExecution(NekoSystem &system, unsigned sif0Words = 64,
    unsigned sif1Words = 64)
  {
    bidirectionalPackets(system, sif0Words, sif1Words);
    receiver(system);
    iopReceiver(system);
    REQUIRE(system.eeBus().writeData32(0x800, 0x0c000204)); // jal 0xa0000810
    REQUIRE(system.eeBus().writeData32(0x804, 0x37e40000)); // ori r4, r31, 0
    REQUIRE(system.eeBus().writeData32(0x810, 0x08000204)); // j 0xa0000810
    writeIOP(system, 0x800, 0x8c032000); // lw r3, 0x2000(r0)
    writeIOP(system, 0x804, 0x10000002); // beq r0, r0, 0x810
    writeIOP(system, 0x808, 0x8c042004); // lw r4, 0x2004(r0)
    writeIOP(system, 0x810, 0x08000204);
    system.eeCore().startExecution(0xa0000800);
    system.iopCore().startExecution(0xa0000800);
  }

  void requireCombinedState(const NekoSystem &actual, const NekoSystem &expected)
  {
    IOPTraceTest::requireMachine(actual, expected);
    REQUIRE(actual.sifStateHash() == expected.sifStateHash());
    REQUIRE(actual.sif0DMAC().stateHash() == expected.sif0DMAC().stateHash());
    REQUIRE(actual.sif1DMAC().stateHash() == expected.sif1DMAC().stateHash());
    REQUIRE(actual.dmacController().sifGrantOrder() ==
      expected.dmacController().sifGrantOrder());
    REQUIRE(actual.iopDMAController().sifGrantOrder() ==
      expected.iopDMAController().sifGrantOrder());
  }
}

TEST_CASE("SIF0 scheduled source uses the preincrement IOP phase and receiver follows next cycle",
  "[sif0][scheduling]")
{
  NekoSystem system;
  source(system);
  receiver(system);
  system.clockMasterCycle();
  REQUIRE(system.masterClockScheduler().currentCycle() == 1);
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 4);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 4);
  REQUIRE(system.sif0DMAC().quadwordCount() == 0);
  system.clockMasterCycle();
  REQUIRE(system.sif0DMAC().quadwordCount() == 1);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 0);
  system.runMasterCycles(6);
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 4);
  system.clockMasterCycle();
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 3);
  system.runMasterCycles(23);
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 1);
  REQUIRE(system.sif0DMAC().active());
  system.clockMasterCycle();
  REQUIRE_FALSE(system.iopSIF0DMAC().active());
  REQUIRE(system.sif0DMAC().active());
  REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 4);
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0x04000000);
  REQUIRE(system.dmacController().status() == 0);
  system.clockMasterCycle();
  REQUIRE_FALSE(system.sif0DMAC().active());
  REQUIRE(system.dmacController().status() == DMACStatus::CHANNEL_5);
  requireData(system);
  REQUIRE(system.eeCore().executionState() == EEExecutionState::Halted);
  REQUIRE(system.iopCore().executionState() == IOPExecutionState::Halted);
}

TEST_CASE("SIF0 automatic source completion does not drain or rearm its late receiver",
  "[sif0][scheduling][completion]")
{
  NekoSystem system;
  source(system);
  system.runMasterCycles(33);
  REQUIRE_FALSE(system.iopSIF0DMAC().active());
  REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 8);
  REQUIRE(system.iopInterruptController().status() == 8);
  system.iopDMAController().writeInterrupt(BANK, 0x04000000);
  system.iopInterruptController().writeStatus(~8u);
  receiver(system);
  system.clockMasterCycle();
  REQUIRE(system.sif0DMAC().active());
  system.clockMasterCycle();
  REQUIRE_FALSE(system.sif0DMAC().active());
  requireData(system);
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0);
  REQUIRE(system.iopInterruptController().status() == 0);
}

TEST_CASE("Common EE SIF grants rotate after acceptance and skip a waiting receiver",
  "[sif0][scheduling][arbitration]")
{
  NekoSystem system;
  push(system, DATA);
  push(system, DATA);
  receiver(system, 2);
  eeSender(system);
  system.clockMasterCycle();
  REQUIRE(system.sif0DMAC().quadwordCount() == 1);
  REQUIRE(system.sif1DMAC().quadwordCount() == 2);
  system.clockMasterCycle();
  REQUIRE(system.sif0DMAC().quadwordCount() == 1);
  REQUIRE(system.sif1DMAC().quadwordCount() == 1);
  system.clockMasterCycle();
  REQUIRE(system.sif0DMAC().quadwordCount() == 0);
  REQUIRE(system.sif0DMAC().active());
  system.clockMasterCycle();
  REQUIRE_FALSE(system.sif1DMAC().active());
  const auto before = system.sif0DMAC().stateHash();
  system.clockMasterCycle();
  REQUIRE(system.sif0DMAC().stateHash() == before);
  eeSender(system, 0);
  system.clockMasterCycle();
  REQUIRE_FALSE(system.sif1DMAC().active());
  REQUIRE(system.sif0DMAC().active());
}

TEST_CASE("Common EE terminal faults consume the grant without stepping a ready peer",
  "[sif0][scheduling][arbitration]")
{
  NekoSystem system;
  push(system, DATA);
  receiver(system, 1);
  system.sif0DMAC().writeChannelControl(CHAIN);
  system.sif0DMAC().writeMemoryAddress(0x1fc00000);
  system.sif0DMAC().writeChannelControl(START | CHAIN);
  eeSender(system, 0);
  system.clockMasterCycle();
  REQUIRE_FALSE(system.sif0DMAC().active());
  REQUIRE(system.sif1DMAC().active());
  REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 4);
  REQUIRE(system.dmacController().status() ==
    (DMACStatus::CHANNEL_5 | DMACStatus::BUS_ERROR));
  REQUIRE(system.dmacController().sifGrantOrder()[0] == SIFDirection::SIF1);
  system.clockMasterCycle();
  REQUIRE_FALSE(system.sif1DMAC().active());
}

TEST_CASE("Common IOP SIF grants honor priority and rotate ties without a second accepted beat",
  "[sif0][scheduling][arbitration]")
{
  for (const unsigned priority : {0x8800u, 0x8900u, 0x9800u})
  {
    CAPTURE(priority);
    NekoSystem system;
    source(system);
    iopReceiver(system);
    REQUIRE(system.sif().pushSIF1Quadword({{0x80006000, 4, 0, 0}}) ==
      SIFTransferOutcome::Completed);
    REQUIRE(system.sif().pushSIF1Quadword(DATA) == SIFTransferOutcome::Completed);
    system.iopDMAController().writePriority(BANK, priority);
    system.clockMasterCycle();
    const bool firstSource = priority != 0x8900;
    REQUIRE(system.iopSIF0DMAC().remainingWords() == (firstSource ? 4 : 0));
    REQUIRE(system.iopSIF1DMAC().remainingWords() == (firstSource ? 0 : 4));
    system.runMasterCycles(8);
    if (priority == 0x8800)
    {
      REQUIRE(system.iopSIF0DMAC().remainingWords() == 4);
      REQUIRE(system.iopSIF1DMAC().remainingWords() == 4);
    }
    else if (firstSource)
    {
      REQUIRE(system.iopSIF0DMAC().remainingWords() == 3);
      REQUIRE(system.iopSIF1DMAC().remainingWords() == 0);
    }
    else
    {
      REQUIRE(system.iopSIF0DMAC().remainingWords() == 0);
      REQUIRE(system.iopSIF1DMAC().remainingWords() == 3);
    }
  }
}

TEST_CASE("Common SIF phases bypass backpressure but surface IOP rejection before a ready peer",
  "[sif0][scheduling][arbitration]")
{
  SECTION("Full source stream can be bypassed by a ready IOP receiver")
  {
    NekoSystem system;
    source(system);
    iopReceiver(system);
    system.iopDMAController().writePriority(BANK, 0x8800);
    for (unsigned index = 0; index < 7; ++index) push(system, DATA);
    REQUIRE(system.sif().pushSIF1Quadword({{0x80006000, 0, 0, 0}}) ==
      SIFTransferOutcome::Completed);
    system.clockMasterCycle();
    REQUIRE(system.iopSIF0DMAC().remainingWords() == 0);
    REQUIRE_FALSE(system.iopSIF1DMAC().active());
    system.runMasterCycles(8);
    REQUIRE(system.iopSIF0DMAC().remainingWords() == 4);
  }
  SECTION("Rejected descriptor cannot be hidden by a successful receiver")
  {
    NekoSystem system;
    source(system);
    REQUIRE(system.iopSIF0DMAC().writeChannelControl(0x401) ==
      IOPDMAWriteOutcome::Completed);
    REQUIRE(system.iopSIF0DMAC().writeTagAddress(0x800000) ==
      IOPDMAWriteOutcome::Completed);
    REQUIRE(system.iopSIF0DMAC().writeChannelControl(0x01000401) ==
      IOPDMAWriteOutcome::Completed);
    iopReceiver(system);
    system.iopDMAController().writePriority(BANK, 0x8800);
    REQUIRE(system.sif().pushSIF1Quadword({{0x80006000, 0, 0, 0}}) ==
      SIFTransferOutcome::Completed);
    REQUIRE_THROWS_AS(system.clockMasterCycle(), IOPDMATransferError);
    REQUIRE(system.iopDMAController().sifGrantOrder()[0] == IOPDMAChannel::SIF0);
    REQUIRE(system.iopSIF1DMAC().active());
    REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 4);
    REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0);
  }
}

TEST_CASE("SIF0 scheduled completion masks acknowledgements and SBUS stay independent",
  "[sif0][scheduling][completion]")
{
  for (const bool dmaFirst : {false, true})
    for (const auto mask : {0u, DMACStatus::CHANNEL_5_MASK})
    {
      NekoSystem system;
      source(system);
      receiver(system);
      system.dmacController().writeStatus(mask);
      system.interruptController().setSource(EEInterruptSource::SBUS, true);
      system.iopInterruptController().assertSource(IOPInterruptSource::SBUS);
      system.dmacController().signalChannelCompletion(DMACStatus::CHANNEL_6);
      system.iopDMAController().signalChannelCompletion(IOPDMAChannel::SIF1);
      system.runMasterCycles(34);
      REQUIRE(system.dmacController().status() ==
        (mask | DMACStatus::CHANNEL_5 | DMACStatus::CHANNEL_6));
      REQUIRE(system.dmacController().interruptPending() == (mask != 0));
      REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0x0c000000);
      REQUIRE(system.iopInterruptController().status() == 10);
      const auto sifHash = system.sifStateHash();
      if (dmaFirst)
      {
        system.iopDMAController().writeInterrupt(BANK, 0x04000000);
        REQUIRE(system.iopInterruptController().status() == 10);
        system.iopInterruptController().writeStatus(~8u);
      }
      else
      {
        system.iopInterruptController().writeStatus(~8u);
        system.runMasterCycles(8);
        REQUIRE(system.iopInterruptController().status() == 2);
        system.iopDMAController().writeInterrupt(BANK, 0x04000000);
        REQUIRE(system.iopInterruptController().status() == 10);
        system.iopInterruptController().writeStatus(~8u);
      }
      REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0x08000000);
      REQUIRE(system.iopInterruptController().status() == 2);
      REQUIRE(system.interruptController().status() == 2);
      const auto iopHash = system.iopStateHash();
      system.dmacController().writeStatus(DMACStatus::CHANNEL_5);
      REQUIRE(system.dmacController().status() == (mask | DMACStatus::CHANNEL_6));
      REQUIRE(system.iopStateHash() == iopHash);
      REQUIRE(system.sifStateHash() == sifHash);
    }
}

TEST_CASE("SIF0 receiver termination retains the next packet until explicit rearming",
  "[sif0][scheduling][completion]")
{
  NekoSystem system;
  push(system, {{0x70000000, 0x3000, 0, 0}});
  push(system, {{0x70000001, 0x3000, 0, 0}});
  push(system, DATA);
  receiver(system);
  system.clockMasterCycle();
  REQUIRE_FALSE(system.sif0DMAC().active());
  REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 8);
  system.dmacController().writeStatus(DMACStatus::CHANNEL_5);
  system.runMasterCycles(16);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 8);
  REQUIRE(system.dmacController().status() == 0);
  receiver(system);
  system.runMasterCycles(2);
  requireData(system);
  REQUIRE_FALSE(system.sif0DMAC().active());
}

TEST_CASE("SIF0 automatic grants freeze under controller disable and resume accepted packets",
  "[sif0][scheduling]")
{
  NekoSystem system;
  source(system);
  receiver(system);
  system.runMasterCycles(9);
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 3);
  REQUIRE(system.sif0DMAC().quadwordCount() == 1);
  system.dmacController().writeControl(0);
  system.iopDMAController().writePriority(BANK, 0);
  const auto sourceControl = system.iopSIF0DMAC().channelControl();
  const auto destination = system.sif0DMAC().stateHash();
  const auto sif = system.sifStateHash();
  system.runMasterCycles(40);
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 3);
  REQUIRE(system.iopSIF0DMAC().channelControl() == sourceControl);
  REQUIRE(system.sif0DMAC().stateHash() == destination);
  REQUIRE(system.sifStateHash() == sif);
  system.dmacController().writeControl(1);
  system.iopDMAController().writePriority(BANK, 0x800);
  system.runMasterCycles(25);
  REQUIRE_FALSE(system.sif0DMAC().active());
  REQUIRE_FALSE(system.iopSIF0DMAC().active());
  requireData(system);
}

TEST_CASE("SIF0 scheduled traces retain directional counts and append-only endpoint codes",
  "[sif0][scheduling][observations]")
{
  NekoSystem system;
  source(system);
  receiver(system);
  system.startTrace();
  system.runMasterCycles(34);
  std::vector<NekoTraceEvent> expected;
  for (unsigned cycle = 1; cycle <= 34; ++cycle)
  {
    if (cycle == 1)
      expected.push_back(beat(cycle, 2 | (4 << 8) | (1 << 16), 0x3000, 0, 0, 0, 0));
    else if (cycle == 2)
      expected.push_back(beat(cycle, 2 | (4 << 8), 0x3000, 0, 1, 4, 0));
    else if (cycle == 34)
      expected.push_back(beat(cycle, 2 | (2 << 8) | (2 << 16), 0x3000, 1, 0, 4, 0));
    else
    {
      const auto queued = (cycle - 2) / 8;
      expected.push_back(beat(cycle, 2 | (2 << 8) | (1 << 16),
        0x3000, 1, 1, queued, queued));
    }
    if (cycle == 1)
      expected.push_back(beat(cycle, 3, 0x1000, 0, 4, 0, 4));
    else if ((cycle - 1) % 8 == 0)
    {
      const auto word = (cycle - 9) / 8;
      expected.push_back(beat(cycle, 3 | (2 << 8) | (word == 3 ? 2 << 16 : 0),
        0x2000 + word * 4, 4 - word, 3 - word, word, word + 1));
    }
  }
  IOPTraceTest::requireEvents(beats(system), expected);
}

TEST_CASE("Common SIF trace journal holds four attempts and orders completion before INTC",
  "[sif0][scheduling][observations]")
{
  NekoSystem system;
  receiver(system);
  eeSender(system, 1);
  source(system);
  iopReceiver(system);
  system.iopDMAController().writePriority(BANK, 0x8800);
  for (unsigned index = 0; index < SIF::WORD_CAPACITY / 4; ++index)
    REQUIRE(system.sif().pushSIF1Quadword({{0x80006000, 0, 0, 0}}) ==
      SIFTransferOutcome::Completed);
  system.startTrace();
  system.clockMasterCycle();
  IOPTraceTest::requireEvents(beats(system), {
    beat(1, 2 | (4 << 8) | (1 << 16), 0x3000, 0, 0, 0, 0),
    beat(1, (2 << 8) | (1 << 16), 0x5000, 1, 1,
      SIF::WORD_CAPACITY, SIF::WORD_CAPACITY),
    beat(1, 3 | (1 << 16), 0x1000, 0, 0, 0, 0),
    beat(1, 1 | (1 << 8) | (2 << 16), 0x6000, 0, 0,
      SIF::WORD_CAPACITY, SIF::WORD_CAPACITY - 4)});
  const auto &events = system.trace();
  std::size_t completion = events.size();
  std::size_t interrupt = events.size();
  for (std::size_t index = 0; index < events.size(); ++index)
  {
    if (events[index].subsystem == NekoTraceSubsystem::SIF &&
        events[index].type == NekoTraceEventType::DMABeat &&
        events[index].value0 == (1 | (1 << 8) | (2 << 16)))
      completion = index;
    if (events[index].subsystem == NekoTraceSubsystem::IOPInterruptController)
      interrupt = index;
  }
  REQUIRE(completion < interrupt);
  REQUIRE(interrupt < events.size());
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 0);
  REQUIRE(system.sif1DMAC().quadwordCount() == 1);
  system.clockMasterCycle();
  REQUIRE_FALSE(system.sif1DMAC().active());
}

TEST_CASE("SIF0 scheduled trace failure codes distinguish descriptor source and destination",
  "[sif0][scheduling][observations]")
{
  for (unsigned failure = 4; failure <= 6; ++failure)
  {
    CAPTURE(failure);
    NekoSystem system;
    if (failure == 4)
    {
      push(system, DATA);
      receiver(system, 1);
      system.sif0DMAC().writeChannelControl(CHAIN);
      system.sif0DMAC().writeMemoryAddress(0x1fc00000);
      system.sif0DMAC().writeChannelControl(START | CHAIN);
    }
    else
    {
      source(system);
      if (failure == 5)
      {
        REQUIRE(system.iopSIF0DMAC().writeChannelControl(0x401) ==
          IOPDMAWriteOutcome::Completed);
        REQUIRE(system.iopSIF0DMAC().writeTagAddress(0x800000) ==
          IOPDMAWriteOutcome::Completed);
        REQUIRE(system.iopSIF0DMAC().writeChannelControl(0x01000401) ==
          IOPDMAWriteOutcome::Completed);
      }
      else
      {
        writeIOP(system, 0x1000, 0x80800000);
        system.clockMasterCycle();
        system.runMasterCycles(7);
      }
    }
    system.startTrace();
    if (failure == 4) REQUIRE_NOTHROW(system.clockMasterCycle());
    else REQUIRE_THROWS_AS(system.clockMasterCycle(), IOPDMATransferError);
    const auto events = beats(system);
    REQUIRE(events.size() == 1);
    REQUIRE((events[0].value0 >> 24) == failure);
    REQUIRE(((events[0].value0 >> 16) & 0xff) == 3);
    REQUIRE(events[0].value1 == (failure == 4 ? 0x1fc00000 : 0x800000));
  }
}

TEST_CASE("SIF cursors and SIF0 completion state affect their owning canonical hashes",
  "[sif0][scheduling][save-state]")
{
  for (unsigned field = 0; field < 4; ++field)
  {
    CAPTURE(field);
    NekoSystem original;
    NekoSystem changed;
    SaveStateMutation state(original.saveState());
    switch (field)
    {
      case 0: state.writeScalar("system.gifDMAC.sifNextDirection", 1); break;
      case 1: state.writeScalar("system.iop.dmaController.sifNextChannel", 10); break;
      case 2: state.writeScalar("system.gifDMAC.controllerStatusRegister",
        DMACStatus::CHANNEL_5); break;
      case 3: state.writeScalar("system.gifDMAC.controllerStatusMaskRegister",
        DMACStatus::CHANNEL_5_MASK); break;
    }
    state.refreshChecksum();
    changed.loadState(state.bytes());
    CHECK((changed.eeStateHash() != original.eeStateHash()) == (field != 1));
    CHECK((changed.iopStateHash() != original.iopStateHash()) == (field == 1));
    REQUIRE(changed.sifStateHash() == original.sifStateHash());
    REQUIRE(changed.saveState() == state.bytes());
  }
}

TEST_CASE("SIF cursors reject invalid domains transactionally",
  "[sif0][scheduling][save-state]")
{
  NekoSystem original;
  NekoSystem destination;
  destination.startTrace();
  destination.runMasterCycles(3);
  const auto before = destination.saveState();
  const auto traceHash = destination.traceHash();
  for (const bool ee : {false, true})
    for (const auto value : {2, 8, 11, 255})
    {
      SaveStateMutation state(original.saveState());
      state.writeScalar(ee ? "system.gifDMAC.sifNextDirection" :
        "system.iop.dmaController.sifNextChannel", value);
      state.refreshChecksum();
      REQUIRE_THROWS_AS(destination.loadState(state.bytes()), std::invalid_argument);
      REQUIRE(destination.saveState() == before);
      REQUIRE(destination.traceEnabled());
      REQUIRE(destination.traceHash() == traceHash);
    }
}

TEST_CASE("Common SIF waiting inspection acknowledgements and faults preserve grant policy",
  "[sif0][scheduling][arbitration]")
{
  NekoSystem system;
  receiver(system);
  source(system);
  REQUIRE(system.dmacController().sifGrantOrder()[0] == SIFDirection::SIF0);
  REQUIRE(system.iopDMAController().sifGrantOrder()[0] == IOPDMAChannel::SIF0);
  REQUIRE(system.iopDMAController().inspectSIF(system.iopSIF0DMAC(),
    system.iopBus(), system.sif()).outcome == IOPDMABeatOutcome::Accepted);
  REQUIRE(system.iopDMAController().sifGrantOrder()[0] == IOPDMAChannel::SIF0);
  system.clockMasterCycle();
  REQUIRE(system.iopDMAController().sifGrantOrder()[0] == IOPDMAChannel::SIF1);
  system.clockMasterCycle();
  REQUIRE(system.dmacController().sifGrantOrder()[0] == SIFDirection::SIF1);
  system.iopDMAController().writePriority(BANK, 0x8800);
  system.dmacController().writeStatus(DMACStatus::CHANNEL_5);
  system.iopDMAController().writeInterrupt(BANK, 0x04000000);
  system.runMasterCycles(6);
  REQUIRE(system.dmacController().sifGrantOrder()[0] == SIFDirection::SIF1);
  REQUIRE(system.iopDMAController().sifGrantOrder()[0] == IOPDMAChannel::SIF1);
  system.dmacController().writeControl(0);
  system.iopDMAController().writePriority(BANK, 0);
  system.runMasterCycles(9);
  REQUIRE(system.dmacController().sifGrantOrder()[0] == SIFDirection::SIF1);
  REQUIRE(system.iopDMAController().sifGrantOrder()[0] == IOPDMAChannel::SIF1);
  system.reset();
  REQUIRE(system.dmacController().sifGrantOrder()[0] == SIFDirection::SIF0);
  REQUIRE(system.iopDMAController().sifGrantOrder()[0] == IOPDMAChannel::SIF0);

  push(system, {{0x74000000, 0x3000, 0, 0}});
  receiver(system);
  eeSender(system, 0);
  REQUIRE_THROWS_AS(system.clockMasterCycle(), std::invalid_argument);
  REQUIRE(system.dmacController().sifGrantOrder()[0] == SIFDirection::SIF0);
  REQUIRE(system.sif1DMAC().active());
  REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 4);
}

TEST_CASE("Common SIF arbitration restores both directions at every IOP phase residue",
  "[sif0][scheduling][save-state][observations]")
{
  for (unsigned residue = 0; residue < 8; ++residue)
  {
    CAPTURE(residue);
    NekoSystem original;
    source(original);
    receiver(original);
    eeSender(original);
    iopReceiver(original);
    original.iopDMAController().writePriority(BANK, 0x8800);
    original.runMasterCycles(8 + residue);
    NekoSystem restored;
    restored.startTrace();
    restored.loadState(original.saveState());
    IOPTraceTest::requireMachine(restored, original);
    original.startTrace();
    original.runMasterCycles(80);
    restored.runMasterCycles(80);
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    REQUIRE_FALSE(restored.sif0DMAC().active());
    REQUIRE_FALSE(restored.iopSIF0DMAC().active());
    REQUIRE_FALSE(restored.sif1DMAC().active());
    REQUIRE_FALSE(restored.iopSIF1DMAC().active());
    requireData(restored);
  }
}

TEST_CASE("All waiting SIF candidates retain their controller cursors",
  "[sif0][scheduling][arbitration]")
{
  NekoSystem system;
  source(system);
  REQUIRE(system.iopDMAController().clockSIF(system.iopSIF0DMAC(),
    system.iopBus(), system.sif()).outcome == IOPDMABeatOutcome::Accepted);
  for (unsigned index = 0; index < (SIF::WORD_CAPACITY - 4) / 4; ++index)
    push(system, DATA);
  iopReceiver(system);
  system.iopDMAController().writePriority(BANK, 0x8800);
  eeSender(system, 1);
  const auto stream = system.sifStateHash();
  system.runMasterCycles(9);
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 4);
  REQUIRE(system.iopSIF1DMAC().remainingWords() == 0);
  REQUIRE(system.sif1DMAC().quadwordCount() == 1);
  REQUIRE(system.sifStateHash() == stream);
  REQUIRE(system.dmacController().sifGrantOrder()[0] == SIFDirection::SIF0);
  REQUIRE(system.iopDMAController().sifGrantOrder()[0] == IOPDMAChannel::SIF1);
}

TEST_CASE("SIF0 scheduled tracing leaves canonical machine behavior unchanged",
  "[sif0][scheduling][observations]")
{
  NekoSystem plain;
  source(plain);
  receiver(plain);
  eeSender(plain);
  iopReceiver(plain);
  plain.iopDMAController().writePriority(BANK, 0x8800);
  NekoSystem traced;
  traced.loadState(plain.saveState());
  traced.startTrace();
  plain.runMasterCycles(90);
  traced.runMasterCycles(90);
  IOPTraceTest::requireMachine(traced, plain);
  REQUIRE_FALSE(beats(traced).empty());
}

TEST_CASE("Either SIF completion fits the full timer SBUS and acknowledgement trace budget",
  "[sif0][scheduling][observations]")
{
  for (const bool sif0 : {false, true})
  {
    CAPTURE(sif0);
    NekoSystem system;
    const IOPWord program[] = {
      0x3c01bf80, 0x34211070, 0x3c02ffff, 0x3442fffd, 0xac220000};
    for (unsigned index = 0; index < 5; ++index)
      writeIOP(system, index * 4, program[index]);
    system.iopCore().startExecution(0xa0000000);
    system.runMasterCycles(32);
    REQUIRE(system.iopCore().retiredInstructions() == 4);
    for (std::size_t index = 0; index < IOPTimers::COUNTER_COUNT; ++index)
    {
      system.iopTimers().writeTarget(index, 1);
      system.iopTimers().writeMode(index, IOPTimerMode::INTERRUPT_ON_TARGET);
    }
    if (sif0)
      source(system, 0, 0x70000000);
    else
    {
      iopReceiver(system);
      system.iopDMAController().writePriority(BANK, 0x8000);
      REQUIRE(system.sif().pushSIF1Quadword({{0x80006000, 0, 0, 0}}) ==
        SIFTransferOutcome::Completed);
    }
    REQUIRE(system.eeBus().writeData32(0, 0xac220000));
    system.eeCore().setGeneralRegister(1, {0xb000f240, 0});
    system.eeCore().setGeneralRegister(2, {1u << 18, 0});
    system.eeCore().startExecution(0xa0000000);
    system.startTrace();
    REQUIRE_NOTHROW(system.clockMasterCycle());
    const std::uint32_t statuses[] = {
      2, 0x12, 0x32, 0x72, 0x4072, 0xc072, 0x1c072, 0x1c07a, 0x1c078};
    std::vector<NekoTraceEvent> expected;
    for (const auto status : statuses)
      expected.push_back({33, NekoTraceSubsystem::IOPInterruptController,
        NekoTraceEventType::InterruptChanged, status, 0, 0, 0});
    IOPTraceTest::requireEvents(IOPTraceTest::subsystemEvents(system,
      NekoTraceSubsystem::IOPInterruptController), expected);
    REQUIRE(system.iopInterruptController().status() == 0x1c078);
    REQUIRE(system.iopCore().retiredInstructions() == 5);
  }
}

TEST_CASE("Bidirectional SIF grants retain exact shared master and IOP phases",
  "[sif][scheduling][cross-channel]")
{
  NekoSystem system;
  source(system);
  receiver(system);
  eeSender(system);
  iopReceiver(system);
  system.iopDMAController().writePriority(BANK, 0x8800);
  system.startTrace();
  system.clockMasterCycle();
  IOPTraceTest::requireEvents(beats(system), {
    beat(1, 2 | (4 << 8) | (1 << 16), 0x3000, 0, 0, 0, 0),
    beat(1, 2 << 8, 0x5000, 2, 1, 0, 4),
    beat(1, 3, 0x1000, 0, 4, 0, 4)});
  REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 4);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 4);
  REQUIRE(system.iopSIF1DMAC().remainingWords() == 0);

  system.clearTrace();
  system.clockMasterCycle();
  IOPTraceTest::requireEvents(beats(system), {
    beat(2, 2 | (4 << 8), 0x3000, 0, 1, 4, 0)});
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 4);
  system.clearTrace();
  system.clockMasterCycle();
  IOPTraceTest::requireEvents(beats(system), {
    beat(3, (2 << 8) | (2 << 16), 0x5010, 1, 0, 4, 8)});
  REQUIRE(system.dmacController().status() == DMACStatus::CHANNEL_6);
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0);

  system.runMasterCycles(5);
  REQUIRE(system.masterClockScheduler().currentCycle() == 8);
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 4);
  REQUIRE(system.iopSIF1DMAC().remainingWords() == 0);
  system.clearTrace();
  system.clockMasterCycle();
  IOPTraceTest::requireEvents(beats(system), {
    beat(9, 2 | (2 << 8) | (1 << 16), 0x3000, 1, 1, 0, 0),
    beat(9, 1 | (1 << 8), 0x6000, 0, 4, 8, 4)});
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 4);
  REQUIRE(system.iopSIF1DMAC().remainingWords() == 4);
}

TEST_CASE("SIF1 output is visible to the same-pulse IOP grant without a second EE beat",
  "[sif][scheduling][cross-channel]")
{
  NekoSystem system;
  source(system);
  receiver(system);
  eeSender(system);
  iopReceiver(system);
  system.iopDMAController().writePriority(BANK, 0x8900);
  system.clockMasterCycle();
  REQUIRE(system.iopSIF1DMAC().remainingWords() == 4);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 0);
  REQUIRE(system.sif1DMAC().quadwordCount() == 1);
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 0);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 0);
  system.clockMasterCycle();
  REQUIRE_FALSE(system.sif1DMAC().active());
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 4);
  REQUIRE(system.iopSIF1DMAC().remainingWords() == 4);
}

TEST_CASE("SIF shared capacity freed by EE is available to the same-pulse IOP source",
  "[sif][scheduling][cross-channel]")
{
  NekoSystem system;
  source(system);
  for (unsigned index = 0; index < SIF::WORD_CAPACITY / 4; ++index)
    push(system, DATA);
  system.sif0DMAC().writeMemoryAddress(0x3000);
  system.sif0DMAC().writeQuadwordCount(1);
  system.sif0DMAC().writeChannelControl(START);
  system.dmacController().writeControl(1);
  REQUIRE(system.sif().freeWordCount() == 0);
  system.clockMasterCycle();
  requireData(system);
  REQUIRE_FALSE(system.sif0DMAC().active());
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 4);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == SIF::WORD_CAPACITY);
  REQUIRE(system.sif().wordAt(SIFDirection::SIF0, SIF::WORD_CAPACITY - 4) ==
    0x70000001);
  REQUIRE(system.sif().wordAt(SIFDirection::SIF0, SIF::WORD_CAPACITY - 3) == 0x3000);
}

TEST_CASE("EE guest SIF starts participate in the current master-cycle grant",
  "[sif][scheduling][cross-channel]")
{
  for (const bool receive : {false, true})
  {
    CAPTURE(receive);
    NekoSystem system;
    if (receive)
    {
      push(system, DATA);
      system.sif0DMAC().writeMemoryAddress(0x3000);
      system.sif0DMAC().writeQuadwordCount(1);
    }
    else
    {
      eeSender(system, 1);
      system.sif1DMAC().writeChannelControl(0);
      iopReceiver(system);
      system.iopDMAController().writePriority(BANK, 0x8000);
    }
    system.dmacController().writeControl(1);
    REQUIRE(system.eeBus().writeData32(0, 0xac220000)); // sw r2, 0(r1)
    system.eeCore().setGeneralRegister(1,
      {0xa0000000 | (receive ? EEMemoryMap::D5_CHCR : EEMemoryMap::D6_CHCR), 0});
    system.eeCore().setGeneralRegister(2, {START, 0});
    system.eeCore().startExecution(0xa0000000);
    system.clockMasterCycle();
    REQUIRE(system.eeCore().pendingException() == EEException::None);
    REQUIRE(system.dmacController().status() ==
      (receive ? DMACStatus::CHANNEL_5 : DMACStatus::CHANNEL_6));
    if (receive)
    {
      requireData(system);
      REQUIRE_FALSE(system.sif0DMAC().active());
    }
    else
    {
      REQUIRE_FALSE(system.sif1DMAC().active());
      REQUIRE(system.iopSIF1DMAC().remainingWords() == 4);
      REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 0);
    }
  }
}

TEST_CASE("IOP guest SIF starts wait for the next phase-zero grant without callback DMA",
  "[sif][scheduling][cross-channel]")
{
  for (const bool send : {false, true})
  {
    CAPTURE(send);
    NekoSystem system;
    if (send)
    {
      source(system);
      REQUIRE(system.iopSIF0DMAC().writeChannelControl(0x401) ==
        IOPDMAWriteOutcome::Completed);
      receiver(system);
    }
    else
    {
      eeSender(system);
      system.iopDMAController().writePriority(BANK, 0x8000);
    }
    const IOPWord program[] = {
      0x3c01bf80,                         // lui r1, 0xbf80
      send ? 0x34211528u : 0x34211538u,   // ori r1, r1, channel CHCR
      0x3c020100,                         // lui r2, 0x0100
      send ? 0x34420401u : 0x34420400u,   // ori r2, r2, direction/mode
      0xac220000};                        // sw r2, 0(r1)
    for (unsigned index = 0; index < 5; ++index)
      writeIOP(system, index * 4, program[index]);
    system.iopCore().startExecution(0xa0000000);
    system.runMasterCycles(32);
    REQUIRE(system.iopCore().retiredInstructions() == 4);
    system.clockMasterCycle();
    REQUIRE(system.iopCore().retiredInstructions() == 5);
    REQUIRE(system.iopCore().exception().kind == IOPException::None);
    REQUIRE(system.iopSIF0DMAC().remainingWords() == 0);
    REQUIRE(system.iopSIF1DMAC().remainingWords() == 0);
    REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 0);
    REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == (send ? 0 : 8));
    REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0);
    system.runMasterCycles(7);
    REQUIRE(system.iopSIF0DMAC().remainingWords() == 0);
    REQUIRE(system.iopSIF1DMAC().remainingWords() == 0);
    system.clockMasterCycle();
    REQUIRE(system.iopSIF0DMAC().remainingWords() == (send ? 4 : 0));
    REQUIRE(system.iopSIF1DMAC().remainingWords() == (send ? 0 : 4));
    REQUIRE(system.sif0DMAC().quadwordCount() == 0);
    if (send)
    {
      REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 4);
      system.clockMasterCycle();
      REQUIRE(system.sif0DMAC().quadwordCount() == 1);
      REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 0);
    }
  }
}

TEST_CASE("SIF0 completions publish before IOP admission and after EE admission",
  "[sif][scheduling][cross-channel]")
{
  NekoSystem system;
  writeIOP(system, 0, 0x34010401); // ori r1, r0, IEc | IM2
  writeIOP(system, 4, 0x40816000); // mtc0 r1, Status
  system.iopCore().startExecution(0xa0000000);
  system.runMasterCycles(16);
  system.iopInterruptController().writeMask(8);
  system.iopInterruptController().writeControl(1);
  source(system, 0, 0x70000000);
  push(system, DATA);
  system.sif0DMAC().writeMemoryAddress(0x3000);
  system.sif0DMAC().writeQuadwordCount(1);
  system.sif0DMAC().writeChannelControl(START);
  system.dmacController().writeControl(1);
  system.dmacController().writeStatus(DMACStatus::CHANNEL_5_MASK);
  system.eeCore().setCOP0Register(EECOP0Register::Status,
    EECOP0Status::INTERRUPT_ENABLE | EECOP0Status::MASTER_INTERRUPT_ENABLE |
      EECOP0Status::DMAC_MASK);
  system.eeCore().startExecution(0xa0000000);
  const auto nextIOP = system.iopCore().programCounter();
  const auto retiredIOP = system.iopCore().retiredInstructions();
  system.clockMasterCycle();
  requireData(system);
  REQUIRE_FALSE(system.sif0DMAC().active());
  REQUIRE_FALSE(system.iopSIF0DMAC().active());
  REQUIRE(system.dmacController().status() ==
    (DMACStatus::CHANNEL_5_MASK | DMACStatus::CHANNEL_5));
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0x04000000);
  REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
  REQUIRE(system.iopCore().exceptionProgramCounter() == nextIOP);
  REQUIRE(system.iopCore().retiredInstructions() == retiredIOP);
  REQUIRE(system.eeCore().pendingException() == EEException::None);
  REQUIRE((system.eeCore().cop0Register(EECOP0Register::Cause) &
    EECOP0Cause::DMAC_PENDING) != 0);
  const auto nextEE = system.eeCore().programCounter();
  system.clockMasterCycle();
  REQUIRE(system.eeCore().pendingException() == EEException::Interrupt);
  REQUIRE(system.eeCore().cop0Register(EECOP0Register::EPC) == nextEE);
  REQUIRE(system.iopCore().retiredInstructions() == retiredIOP);
}

TEST_CASE("Simultaneous SIF directions drain unequal oversized packets under either IOP priority",
  "[sif][scheduling][cross-channel][progress]")
{
  for (const auto priority : {0x8800u, 0x8900u, 0x9800u})
    for (unsigned size = 0; size < 3; ++size)
    {
      const unsigned sif0Words = size == 0 ? 4 : 64;
      const unsigned sif1Words = size == 1 ? 12 : 64;
      CAPTURE(priority);
      CAPTURE(sif0Words);
      CAPTURE(sif1Words);
      NekoSystem system;
      bidirectionalPackets(system, sif0Words, sif1Words);
      receiver(system);
      iopReceiver(system);
      system.iopDMAController().writePriority(BANK, priority);
      drainBidirectional(system);
      requireBidirectionalPayloads(system, sif0Words, sif1Words);
    }
}

TEST_CASE("Either late SIF receiver releases a genuinely full shared transport",
  "[sif][scheduling][cross-channel][progress]")
{
  for (const bool lateEE : {false, true})
  {
    CAPTURE(lateEE);
    NekoSystem system;
    bidirectionalPackets(system, 64, 64);
    if (lateEE) iopReceiver(system);
    else receiver(system);
    bool full = false;
    for (unsigned elapsed = 0; elapsed < 2048; ++elapsed)
    {
      system.clockMasterCycle();
      full = full || system.sif().freeWordCount() == 0;
    }
    REQUIRE(full);
    REQUIRE(system.sif().freeWordCount() == 0);
    REQUIRE(system.sif().wordCount(lateEE ? SIFDirection::SIF0 : SIFDirection::SIF1) ==
      SIF::WORD_CAPACITY);
    const auto transport = system.sifStateHash();
    const auto eeSource = system.sif1DMAC().stateHash();
    const auto iopSourceAddress = system.iopSIF0DMAC().memoryAddress();
    const auto iopSourceCount = system.iopSIF0DMAC().remainingWords();
    const auto priorityOrder = system.iopDMAController().sifGrantOrder();
    system.runMasterCycles(32);
    REQUIRE(system.sifStateHash() == transport);
    REQUIRE(system.sif1DMAC().stateHash() == eeSource);
    REQUIRE(system.iopSIF0DMAC().memoryAddress() == iopSourceAddress);
    REQUIRE(system.iopSIF0DMAC().remainingWords() == iopSourceCount);
    REQUIRE(system.iopDMAController().sifGrantOrder() == priorityOrder);
    if (lateEE) receiver(system);
    else iopReceiver(system);
    drainBidirectional(system);
    requireBidirectionalPayloads(system, 64, 64);
  }
}

TEST_CASE("Common and per-channel SIF eligibility pauses preserve accepted continuation",
  "[sif][scheduling][cross-channel][progress]")
{
  for (unsigned paused = 0; paused < 6; ++paused)
  {
    CAPTURE(paused);
    NekoSystem system;
    bidirectionalPackets(system, 64, 64);
    receiver(system);
    iopReceiver(system);
    system.runMasterCycles(25);
    REQUIRE(system.iopSIF0DMAC().remainingWords() > 0);
    REQUIRE(system.iopSIF1DMAC().remainingWords() > 0);
    if (paused == 0) system.dmacController().writeControl(0);
    if (paused == 1) system.iopDMAController().writePriority(BANK, 0);
    if (paused == 2 || paused == 3)
      system.dmacController().writePriorityControl(DMACPriority::ENABLE |
        ((paused == 2 ? DMACStatus::CHANNEL_6 : DMACStatus::CHANNEL_5) << 16));
    if (paused == 4 || paused == 5)
      system.iopDMAController().writePriority(BANK, paused == 4 ? 0x8000 : 0x800);
    const auto ee0 = system.sif0DMAC().stateHash();
    const auto ee1 = system.sif1DMAC().stateHash();
    const auto iop0Address = system.iopSIF0DMAC().memoryAddress();
    const auto iop0Count = system.iopSIF0DMAC().remainingWords();
    const auto iop0Tag = system.iopSIF0DMAC().tagAddress();
    const auto iop1Address = system.iopSIF1DMAC().memoryAddress();
    const auto iop1Count = system.iopSIF1DMAC().remainingWords();
    const auto ctrl = system.sif().control();
    system.runMasterCycles(64);
    if (paused == 0 || paused == 2)
      REQUIRE(system.sif0DMAC().stateHash() == ee0);
    if (paused == 0 || paused == 3)
      REQUIRE(system.sif1DMAC().stateHash() == ee1);
    if (paused == 1 || paused == 4)
    {
      REQUIRE(system.iopSIF0DMAC().memoryAddress() == iop0Address);
      REQUIRE(system.iopSIF0DMAC().remainingWords() == iop0Count);
      REQUIRE(system.iopSIF0DMAC().tagAddress() == iop0Tag);
    }
    if (paused == 1 || paused == 5)
    {
      REQUIRE(system.iopSIF1DMAC().memoryAddress() == iop1Address);
      REQUIRE(system.iopSIF1DMAC().remainingWords() == iop1Count);
    }
    if (paused == 0 || paused == 1)
      REQUIRE(system.sif().control() == ctrl);
    if (paused == 2 || paused == 4)
      REQUIRE(system.iopSIF1DMAC().remainingWords() < iop1Count);
    if (paused == 3)
      REQUIRE(system.iopSIF0DMAC().remainingWords() < iop0Count);
    if (paused == 1)
    {
      REQUIRE(system.sif().freeWordCount() == 0);
      REQUIRE(system.sif().wordCount(SIFDirection::SIF1) > 0);
    }
    if (paused == 5)
    {
      REQUIRE(system.sif().freeWordCount() == 0);
      REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 1);
      REQUIRE(system.iopSIF0DMAC().remainingWords() == iop0Count);
    }
    system.dmacController().writeControl(1);
    system.dmacController().writePriorityControl(0);
    system.iopDMAController().writePriority(BANK, 0x8800);
    drainBidirectional(system);
    requireBidirectionalPayloads(system, 64, 64);
  }
}

TEST_CASE("SIF endpoint abort and restart retain shared words without remote completion",
  "[sif][scheduling][cross-channel][progress]")
{
  for (unsigned endpoint = 0; endpoint < 4; ++endpoint)
  {
    CAPTURE(endpoint);
    NekoSystem system;
    source(system);
    receiver(system);
    eeSender(system);
    if (endpoint == 3)
      REQUIRE(system.eeBus().writeQuadword(0x5010, {{0x80007000, 0, 0xaaaa, 0xbbbb}}));
    iopReceiver(system);
    system.iopDMAController().writePriority(BANK, 0x8800);
    system.runMasterCycles(endpoint == 1 ? 1 : 9);
    REQUIRE(system.sif0DMAC().quadwordCount() == (endpoint == 1 ? 0 : 1));
    REQUIRE(system.iopSIF0DMAC().remainingWords() == 4);
    REQUIRE(system.iopSIF1DMAC().remainingWords() == (endpoint == 1 ? 0 : 4));
    const auto transport = system.sif();
    const auto eeStatus = system.dmacController().status();
    const auto iopStatus = system.iopDMAController().interruptStatus(BANK);
    const auto ee0 = system.sif0DMAC().stateHash();
    const auto ee1 = system.sif1DMAC().stateHash();
    const auto iop0Address = system.iopSIF0DMAC().memoryAddress();
    const auto iop1Address = system.iopSIF1DMAC().memoryAddress();
    const auto iop0Control = system.iopSIF0DMAC().channelControl();
    const auto iop1Control = system.iopSIF1DMAC().channelControl();
    if (endpoint == 0)
      system.sif0DMAC().writeChannelControl(system.sif0DMAC().channelControl() & ~START);
    else if (endpoint == 1)
      system.sif1DMAC().writeChannelControl(0);
    else if (endpoint == 2)
      REQUIRE(system.iopSIF0DMAC().writeChannelControl(0x401) ==
        IOPDMAWriteOutcome::Completed);
    else
      REQUIRE(system.iopSIF1DMAC().writeChannelControl(0x400) ==
        IOPDMAWriteOutcome::Completed);
    for (const auto direction : {SIFDirection::SIF0, SIFDirection::SIF1})
    {
      REQUIRE(system.sif().wordCount(direction) == transport.wordCount(direction));
      for (unsigned index = 0; index < transport.wordCount(direction); ++index)
        REQUIRE(system.sif().wordAt(direction, index) == transport.wordAt(direction, index));
    }
    REQUIRE(system.sif().sif0PackingLatch() == transport.sif0PackingLatch());
    REQUIRE(system.sif().sif0PackingCursor() == transport.sif0PackingCursor());
    const auto clearedCTRL = endpoint == 0 ? (1u << 5 | 1u << 13) :
      endpoint == 1 ? (1u << 6 | 1u << 14) : 0;
    REQUIRE(system.sif().control() == (transport.control() & ~clearedCTRL));
    REQUIRE(system.dmacController().status() == eeStatus);
    REQUIRE(system.iopDMAController().interruptStatus(BANK) == iopStatus);
    REQUIRE(system.iopInterruptController().status() == 0);
    if (endpoint != 0) REQUIRE(system.sif0DMAC().stateHash() == ee0);
    if (endpoint != 1) REQUIRE(system.sif1DMAC().stateHash() == ee1);
    REQUIRE(system.iopSIF0DMAC().memoryAddress() == iop0Address);
    REQUIRE(system.iopSIF1DMAC().memoryAddress() == iop1Address);
    if (endpoint != 2)
    {
      REQUIRE(system.iopSIF0DMAC().channelControl() == iop0Control);
      REQUIRE(system.iopSIF0DMAC().remainingWords() == 4);
    }
    if (endpoint != 3)
    {
      REQUIRE(system.iopSIF1DMAC().channelControl() == iop1Control);
      REQUIRE(system.iopSIF1DMAC().remainingWords() == (endpoint == 1 ? 0 : 4));
    }
    const auto stoppedEE0 = system.sif0DMAC().stateHash();
    const auto stoppedEE1 = system.sif1DMAC().stateHash();
    system.runMasterCycles(8);
    if (endpoint == 0)
    {
      REQUIRE(system.sif0DMAC().stateHash() == stoppedEE0);
      REQUIRE(system.iopSIF0DMAC().remainingWords() == 3);
    }
    if (endpoint == 1)
    {
      REQUIRE(system.sif1DMAC().stateHash() == stoppedEE1);
      REQUIRE(system.sif0DMAC().quadwordCount() == 1);
      REQUIRE(system.iopSIF1DMAC().remainingWords() == 4);
    }
    if (endpoint == 2)
    {
      REQUIRE_FALSE(system.iopSIF0DMAC().active());
      REQUIRE(system.iopSIF0DMAC().memoryAddress() == iop0Address);
      REQUIRE(system.iopSIF1DMAC().remainingWords() == 3);
    }
    if (endpoint == 3)
    {
      REQUIRE_FALSE(system.iopSIF1DMAC().active());
      REQUIRE(system.iopSIF1DMAC().memoryAddress() == iop1Address);
      REQUIRE(system.iopSIF0DMAC().remainingWords() == 3);
    }

    if (endpoint == 0)
    {
      REQUIRE(system.sif0DMAC().quadwordCount() == 1);
      system.sif0DMAC().writeChannelControl(START);
    }
    else if (endpoint == 1)
    {
      REQUIRE(system.sif1DMAC().quadwordCount() == 1);
      system.sif1DMAC().writeChannelControl(START);
    }
    else if (endpoint == 2)
    {
      REQUIRE(system.iopSIF0DMAC().remainingWords() == 0);
      REQUIRE_FALSE(system.iopSIF0DMAC().terminalPacket());
      REQUIRE(system.iopSIF0DMAC().tagAddress() == 0x1010);
      // A fresh descriptor is appended; accepted words are not rolled back.
      writeIOP(system, 0x1010, 0x80002000);
      writeIOP(system, 0x1014, 0);
      writeIOP(system, 0x1018, 0x70000000);
      writeIOP(system, 0x101c, 0x3000);
      writeIOP(system, 0x1020, 0xaaaa);
      writeIOP(system, 0x1024, 0xbbbb);
      REQUIRE(system.iopSIF0DMAC().writeChannelControl(0x01000401) ==
        IOPDMAWriteOutcome::Completed);
    }
    else
    {
      REQUIRE(system.iopSIF1DMAC().remainingWords() == 0);
      REQUIRE_FALSE(system.iopSIF1DMAC().terminalPacket());
      iopReceiver(system);
    }
    system.runMasterCycles(128);
    REQUIRE_FALSE(system.sif0DMAC().active());
    REQUIRE_FALSE(system.sif1DMAC().active());
    REQUIRE_FALSE(system.iopSIF0DMAC().active());
    REQUIRE_FALSE(system.iopSIF1DMAC().active());
    REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0x0c000000);
    if (endpoint == 2)
    {
      EEQuadword value = {};
      REQUIRE(system.eeBus().readDMAC128(0x3000, &value));
      REQUIRE(value.low == UINT64_C(0x0000300070000000));
      REQUIRE(value.high == UINT64_C(0x0000bbbb0000aaaa));
    }
    else requireData(system);
    if (endpoint == 3)
    {
      REQUIRE(system.iopSIF1DMAC().memoryAddress() == 0x7000);
      for (const auto address : {0x6000u, 0x7000u})
      {
        const auto value = system.iopBus().read32(address);
        REQUIRE(value.status == IOPBusStatus::Completed);
        REQUIRE(value.value == 0);
      }
    }
    REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 0);
    REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 0);
  }
}

TEST_CASE("Both completed SIF directions acknowledge DMA and INTC independently",
  "[sif][scheduling][cross-channel][progress]")
{
  for (const bool dmaFirst : {false, true})
    for (const bool sif0First : {false, true})
    {
      CAPTURE(dmaFirst);
      CAPTURE(sif0First);
      NekoSystem system;
      bidirectionalPackets(system, 64, 12);
      receiver(system);
      iopReceiver(system);
      system.dmacController().writeStatus(
        DMACStatus::CHANNEL_5_MASK | DMACStatus::CHANNEL_6_MASK);
      system.interruptController().setSource(EEInterruptSource::SBUS, true);
      system.iopInterruptController().assertSource(IOPInterruptSource::SBUS);
      system.iopInterruptController().writeMask(8);
      system.iopInterruptController().writeControl(1);
      drainBidirectional(system);
      REQUIRE(system.iopInterruptController().interruptPending());
      REQUIRE(system.dmacController().interruptPending());
      const auto transport = system.sifStateHash();
      const auto firstEE = sif0First ? DMACStatus::CHANNEL_5 : DMACStatus::CHANNEL_6;
      const auto otherEE = sif0First ? DMACStatus::CHANNEL_6 : DMACStatus::CHANNEL_5;
      const auto firstIOP = sif0First ? 0x04000000u : 0x08000000u;
      const auto otherIOP = sif0First ? 0x08000000u : 0x04000000u;
      const auto masks = DMACStatus::CHANNEL_5_MASK | DMACStatus::CHANNEL_6_MASK;
      if (!dmaFirst)
      {
        system.iopInterruptController().writeStatus(~8u);
        system.runMasterCycles(16);
        REQUIRE(system.iopInterruptController().status() == 2);
      }
      system.iopDMAController().writeInterrupt(BANK, firstIOP);
      REQUIRE(system.iopDMAController().interruptStatus(BANK) == otherIOP);
      REQUIRE(system.iopInterruptController().status() == 10);
      REQUIRE(system.iopInterruptController().interruptPending());
      system.dmacController().writeStatus(firstEE);
      REQUIRE(system.dmacController().status() == (otherEE | masks));
      REQUIRE(system.dmacController().interruptPending());
      if (dmaFirst) system.iopInterruptController().writeStatus(~8u);
      REQUIRE(system.iopInterruptController().status() == (dmaFirst ? 2 : 10));
      system.runMasterCycles(16);
      REQUIRE(system.iopInterruptController().status() == (dmaFirst ? 2 : 10));
      system.iopDMAController().writeInterrupt(BANK, otherIOP);
      REQUIRE_FALSE(system.iopDMAController().interruptPending());
      REQUIRE(system.iopInterruptController().status() == (dmaFirst ? 2 : 10));
      system.iopInterruptController().writeStatus(~8u);
      REQUIRE(system.iopInterruptController().status() == 2);
      system.dmacController().writeStatus(otherEE);
      REQUIRE(system.dmacController().status() == masks);
      REQUIRE_FALSE(system.dmacController().interruptPending());
      REQUIRE(system.sifStateHash() == transport);
      REQUIRE(system.interruptController().status() == 2);
      REQUIRE(system.sif().freeWordCount() == SIF::WORD_CAPACITY);
    }
}

TEST_CASE("Empty SIF receivers wait without completion until both producers start",
  "[sif][scheduling][cross-channel][progress]")
{
  NekoSystem system;
  receiver(system);
  iopReceiver(system);
  system.iopDMAController().writePriority(BANK, 0x8800);
  const auto destination = system.sif0DMAC().stateHash();
  const auto transport = system.sifStateHash();
  system.runMasterCycles(40);
  REQUIRE(system.sif0DMAC().stateHash() == destination);
  REQUIRE(system.sifStateHash() == transport);
  REQUIRE(system.iopSIF1DMAC().active());
  REQUIRE(system.iopSIF1DMAC().remainingWords() == 0);
  REQUIRE(system.iopSIF1DMAC().memoryAddress() == 0);
  REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0);
  REQUIRE(system.dmacController().status() == 0);
  REQUIRE(system.iopInterruptController().status() == 0);
  REQUIRE(system.dmacController().sifGrantOrder()[0] == SIFDirection::SIF0);
  REQUIRE(system.iopDMAController().sifGrantOrder()[0] == IOPDMAChannel::SIF0);
  bidirectionalPackets(system, 64, 64);
  drainBidirectional(system);
  requireBidirectionalPayloads(system, 64, 64);
}

TEST_CASE("EE uncached accesses precede a same-cycle SIF0 destination write",
  "[sif][cross-channel][lifecycle][overlap]")
{
  for (const bool store : {false, true})
  {
    CAPTURE(store);
    NekoSystem system;
    bidirectionalPackets(system, 4, 4);
    receiver(system);
    iopReceiver(system);
    REQUIRE(system.eeBus().writeData32(0x3000, 0x3333));
    REQUIRE(system.eeBus().writeData32(0x800, store ? 0xac220000 : 0x8c230000));
    system.eeCore().setGeneralRegister(1, {0xa0003000, 0});
    system.eeCore().setGeneralRegister(2, {0x7777, 0});
    system.runMasterCycles(65);
    REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 4);
    REQUIRE(system.sif0DMAC().quadwordCount() == 1);
    REQUIRE(system.eeBus().read32(0x3000) == 0x3333);
    system.eeCore().startExecution(0xa0000800);
    system.clockMasterCycle();
    REQUIRE(system.eeCore().pendingException() == EEException::None);
    REQUIRE(system.eeCore().acceptanceRecordsThisCycle().size() == 1);
    if (!store) REQUIRE(system.eeCore().generalRegister(3).low == 0x3333);
    requireData(system);
    drainBidirectional(system);
    requireBidirectionalPayloads(system, 4, 4);
  }
}

TEST_CASE("EE uncached store feeds the same-cycle SIF1 source beat",
  "[sif][cross-channel][lifecycle][overlap]")
{
  NekoSystem system;
  bidirectionalPackets(system, 4, 4);
  receiver(system);
  iopReceiver(system);
  REQUIRE(system.eeBus().writeData32(0x800, 0xac220000)); // sw r2, 0(r1)
  system.eeCore().setGeneralRegister(1, {0xa0005010, 0});
  system.eeCore().setGeneralRegister(2, {0x7777, 0});
  system.runMasterCycles(2);
  REQUIRE(system.sif1DMAC().memoryAddress() == 0x5010);
  REQUIRE(system.sif1DMAC().quadwordCount() == 1);
  system.eeCore().startExecution(0xa0000800);
  system.clockMasterCycle();
  REQUIRE(system.eeCore().acceptanceRecordsThisCycle().size() == 1);
  REQUIRE_FALSE(system.sif1DMAC().active());
  REQUIRE(system.sif().wordAt(SIFDirection::SIF1, 4) == 0x7777);
  drainBidirectional(system);
  requireData(system);
  for (unsigned index = 0; index < 4; ++index)
    REQUIRE(system.iopBus().read32(0x6000 + 4 * index).value ==
      (index == 0 ? 0x7777 : 0x10000000 + index));
}

TEST_CASE("IOP uncached accesses follow a same-pulse SIF1 destination write",
  "[sif][cross-channel][lifecycle][overlap]")
{
  for (const bool store : {false, true})
  {
    CAPTURE(store);
    NekoSystem system;
    bidirectionalPackets(system, 4, 4);
    receiver(system);
    iopReceiver(system);
    writeIOP(system, 0x6000, 0x3333);
    writeIOP(system, 0x800, 0x3c01a000); // lui r1, 0xa000
    writeIOP(system, 0x804, 0x34027777); // ori r2, r0, 0x7777
    writeIOP(system, 0x808, 0);
    writeIOP(system, 0x80c, store ? 0xac226000 : 0x8c236000);
    system.iopCore().startExecution(0xa0000800);
    system.runMasterCycles(24);
    REQUIRE(system.iopSIF1DMAC().remainingWords() == 4);
    REQUIRE(system.iopBus().read32(0x6000).value == 0x3333);
    system.clockMasterCycle();
    REQUIRE(system.iopSIF1DMAC().remainingWords() == 3);
    REQUIRE(system.iopCore().retiredInstructions() == 4);
    REQUIRE(system.iopBus().read32(0x6000).value == (store ? 0x7777 : 0x10000000));
    if (!store)
    {
      REQUIRE(system.iopCore().generalRegister(3) == 0);
      REQUIRE(system.iopCore().delayedResultContinuation().source ==
        IOPDelayedResultSource::Load);
      REQUIRE(system.iopCore().delayedResultContinuation().value == 0x10000000);
      system.runMasterCycles(8);
      REQUIRE(system.iopCore().generalRegister(3) == 0x10000000);
    }
    drainBidirectional(system);
    requireData(system);
    for (unsigned index = 1; index < 4; ++index)
      REQUIRE(system.iopBus().read32(0x6000 + 4 * index).value == 0x10000000 + index);
  }
}

TEST_CASE("IOP uncached stores cannot change an accepted SIF0 word but feed later beats",
  "[sif][cross-channel][lifecycle][overlap]")
{
  NekoSystem system;
  bidirectionalPackets(system, 4, 4);
  receiver(system);
  iopReceiver(system);
  writeIOP(system, 0x800, 0x3c01a000); // lui r1, 0xa000
  writeIOP(system, 0x804, 0x34027777); // ori r2, r0, 0x7777
  writeIOP(system, 0x808, 0xac222000); // sw r2, 0x2000(r1)
  writeIOP(system, 0x80c, 0xac222004); // sw r2, 0x2004(r1)
  system.iopCore().startExecution(0xa0000800);
  system.runMasterCycles(16);
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 4);
  system.clockMasterCycle();
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 3);
  REQUIRE(system.iopBus().read32(0x2000).value == 0x7777);
  REQUIRE(system.sif().wordAt(SIFDirection::SIF0, 0) == 1);
  system.runMasterCycles(8);
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 3);
  REQUIRE(system.iopBus().read32(0x2004).value == 0x7777);
  drainBidirectional(system);
  EEQuadword value = {};
  REQUIRE(system.eeBus().readDMAC128(0x3000, &value));
  REQUIRE(value.low == UINT64_C(0x0000777700000001));
  REQUIRE(value.high == UINT64_C(0x0000000400000003));
}

TEST_CASE("Bidirectional SIF DMA survives precise guest delay-slot exceptions on both CPUs",
  "[sif][cross-channel][lifecycle][exception]")
{
  NekoSystem system;
  bidirectionalPackets(system, 64, 64);
  receiver(system);
  iopReceiver(system);
  writeIOP(system, 0x800, 0x34010000); // ori r1, r0, 0
  writeIOP(system, 0x804, 0x40816000); // mtc0 r1, Status
  writeIOP(system, 0x808, 0x10000001); // beq r0, r0, 0x810
  writeIOP(system, 0x80c, 0x0000000c); // syscall
  writeIOP(system, 0x80, 0x24070066); // addiu r7, r0, 0x66
  REQUIRE(system.eeBus().writeData32(0x800, 0x08000204)); // j 0xa0000810
  REQUIRE(system.eeBus().writeData32(0x804, 0x0000000c)); // syscall
  REQUIRE(system.eeBus().writeData32(0x180, 0x24070077));
  system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
  system.iopCore().startExecution(0xa0000800);
  system.runMasterCycles(23);
  system.eeCore().startExecution(0xa0000800);
  system.clockMasterCycle();
  system.clockMasterCycle();
  REQUIRE(system.eeCore().pendingException() == EEException::SystemCall);
  REQUIRE(system.eeCore().cop0Register(EECOP0Register::EPC) == 0xa0000800);
  REQUIRE((system.eeCore().cop0Register(EECOP0Register::Cause) &
    EECOP0Cause::BRANCH_DELAY) != 0);
  REQUIRE(system.iopCore().exception().kind == IOPException::SystemCall);
  REQUIRE(system.iopCore().exceptionProgramCounter() == 0xa0000808);
  REQUIRE((system.iopCore().cause() & IOPCOP0Cause::BRANCH_DELAY) != 0);
  REQUIRE(system.eeCore().executionState() == EEExecutionState::Running);
  REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 63);
  REQUIRE(system.iopSIF1DMAC().remainingWords() == 63);
  drainBidirectional(system);
  requireBidirectionalPayloads(system, 64, 64);
  REQUIRE(system.eeCore().generalRegister(7).low == 0x77);
  REQUIRE(system.iopCore().generalRegister(7) == 0x66);
}

TEST_CASE("Host halt and resume preserve CPU continuations while both SIF directions drain",
  "[sif][cross-channel][lifecycle][halt]")
{
  for (const unsigned checkpoint : {1u, 9u, 17u})
    for (unsigned halted = 1; halted <= 3; ++halted)
    {
      CAPTURE(checkpoint);
      CAPTURE(halted);
      NekoSystem system;
      bidirectionalPackets(system, 64, 64);
      receiver(system);
      iopReceiver(system);
      REQUIRE(system.eeBus().writeData32(0x800, 0x0c000204)); // jal 0xa0000810
      REQUIRE(system.eeBus().writeData32(0x804, 0x37e40000)); // ori r4, r31, 0
      REQUIRE(system.eeBus().writeData32(0x80c, 0x2406dead));
      REQUIRE(system.eeBus().writeData32(0x810, 0x24050055));
      writeIOP(system, 0x800, 0x8c032000); // lw r3, 0x2000(r0)
      writeIOP(system, 0x804, 0x10000002); // beq r0, r0, 0x810
      writeIOP(system, 0x808, 0x8c042004); // lw r4, 0x2004(r0)
      writeIOP(system, 0x80c, 0x2406dead);
      writeIOP(system, 0x810, 0x00803821); // addu r7, r4, r0
      writeIOP(system, 0x814, 0x00804021); // addu r8, r4, r0
      system.iopCore().startExecution(0xa0000800);
      system.runMasterCycles(checkpoint - 1);
      system.eeCore().startExecution(0xa0000800);
      system.clockMasterCycle();
      if (checkpoint == 9)
        REQUIRE(system.iopCore().branchContinuation().active);
      else
        REQUIRE(system.iopCore().delayedResultContinuation().source ==
          IOPDelayedResultSource::Load);
      const auto eePC = system.eeCore().programCounter();
      const auto iopPC = system.iopCore().programCounter();
      const auto eeCycles = system.eeCore().elapsedCycles();
      const auto iopCycles = system.iopCore().elapsedCycles();
      if ((halted & 1) != 0) system.eeCore().haltExecution();
      if ((halted & 2) != 0) system.iopCore().haltExecution();
      drainBidirectional(system);
      requireBidirectionalPayloads(system, 64, 64);
      if ((halted & 1) != 0)
      {
        REQUIRE(system.eeCore().programCounter() == eePC);
        REQUIRE(system.eeCore().elapsedCycles() == eeCycles);
        system.eeCore().startExecution(eePC);
      }
      else REQUIRE(system.eeCore().elapsedCycles() > eeCycles);
      if ((halted & 2) != 0)
      {
        REQUIRE(system.iopCore().programCounter() == iopPC);
        REQUIRE(system.iopCore().elapsedCycles() == iopCycles);
        system.iopCore().resumeExecution();
      }
      else REQUIRE(system.iopCore().elapsedCycles() > iopCycles);
      system.runMasterCycles(40);
      REQUIRE(system.eeCore().generalRegister(4).low ==
        system.eeCore().generalRegister(31).low);
      REQUIRE(static_cast<std::uint32_t>(system.eeCore().generalRegister(4).low) ==
        0xa0000808);
      REQUIRE(system.eeCore().generalRegister(5).low == 0x55);
      REQUIRE(system.eeCore().generalRegister(6).low == 0);
      REQUIRE(system.iopCore().generalRegister(3) == 1);
      REQUIRE(system.iopCore().generalRegister(4) == 2);
      REQUIRE(system.iopCore().generalRegister(6) == 0);
      REQUIRE(system.iopCore().generalRegister(7) == 0);
      REQUIRE(system.iopCore().generalRegister(8) == 2);
    }
}

TEST_CASE("SIF completions pending during host halt enter on CPU resume without losing an older load",
  "[sif][cross-channel][lifecycle][interrupt]")
{
  NekoSystem system;
  bidirectionalPackets(system, 64, 64);
  receiver(system);
  iopReceiver(system);
  writeIOP(system, 0x800, 0x34010401); // ori r1, r0, IEc | IM2
  writeIOP(system, 0x804, 0x40816000); // mtc0 r1, Status
  writeIOP(system, 0x808, 0x8c032000); // lw r3, 0x2000(r0)
  system.iopCore().startExecution(0xa0000800);
  system.eeCore().startExecution(0xa0000800);
  system.runMasterCycles(17);
  REQUIRE(system.iopCore().delayedResultContinuation().source ==
    IOPDelayedResultSource::Load);
  const auto eePC = system.eeCore().programCounter();
  const auto iopPC = system.iopCore().programCounter();
  const auto iopRetired = system.iopCore().retiredInstructions();
  system.eeCore().haltExecution();
  system.iopCore().haltExecution();
  system.dmacController().writeStatus(DMACStatus::CHANNEL_5_MASK |
    DMACStatus::CHANNEL_6_MASK);
  system.eeCore().setCOP0Register(EECOP0Register::Status,
    EECOP0Status::INTERRUPT_ENABLE | EECOP0Status::MASTER_INTERRUPT_ENABLE |
      EECOP0Status::DMAC_MASK);
  system.iopInterruptController().writeMask(8);
  system.iopInterruptController().writeControl(1);
  drainBidirectional(system);
  REQUIRE(system.dmacController().interruptPending());
  REQUIRE(system.iopInterruptController().interruptPending());
  REQUIRE(system.iopCore().generalRegister(3) == 0);
  REQUIRE(system.iopCore().delayedResultContinuation().value == 1);
  system.eeCore().startExecution(eePC);
  system.iopCore().resumeExecution();
  const auto untilIOPPulse =
    (8 - system.masterClockScheduler().currentCycle() % 8) % 8 + 1;
  system.clockMasterCycle();
  REQUIRE(system.eeCore().pendingException() == EEException::Interrupt);
  REQUIRE(system.eeCore().cop0Register(EECOP0Register::EPC) == eePC);
  system.runMasterCycles(untilIOPPulse - 1);
  REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
  REQUIRE(system.iopCore().exceptionProgramCounter() == iopPC);
  REQUIRE(system.iopCore().retiredInstructions() == iopRetired);
  REQUIRE(system.iopCore().generalRegister(3) == 1);
  REQUIRE(system.iopCore().delayedResultContinuation().source ==
    IOPDelayedResultSource::None);
}

TEST_CASE("CPU execution failures do not abort bidirectional SIF or the running peer",
  "[sif][cross-channel][lifecycle][failure]")
{
  for (const bool failEE : {false, true})
  {
    CAPTURE(failEE);
    NekoSystem system;
    bidirectionalPackets(system, 64, 64);
    receiver(system);
    iopReceiver(system);
    REQUIRE(system.eeBus().writeData32(0x800, failEE ? 0x40003800 : 0));
    writeIOP(system, 0x800, failEE ? 0 : 0x3c011000);
    writeIOP(system, 0x804, failEE ? 0 : 0xac200000);
    system.eeCore().startExecution(0xa0000800);
    system.iopCore().startExecution(0xa0000800);
    system.runMasterCycles(9);
    REQUIRE(system.eeCore().stopReason() ==
      (failEE ? EEStopReason::UnsupportedInstruction : EEStopReason::None));
    REQUIRE(system.iopCore().stopReason() ==
      (failEE ? IOPStopReason::None : IOPStopReason::StoreFailure));
    const auto eePC = system.eeCore().programCounter();
    const auto iopPC = system.iopCore().programCounter();
    const auto eeCycles = system.eeCore().elapsedCycles();
    const auto iopCycles = system.iopCore().elapsedCycles();
    drainBidirectional(system);
    requireBidirectionalPayloads(system, 64, 64);
    if (failEE)
    {
      REQUIRE(system.eeCore().programCounter() == eePC);
      REQUIRE(system.eeCore().elapsedCycles() == eeCycles);
      REQUIRE(system.iopCore().elapsedCycles() > iopCycles);
    }
    else
    {
      REQUIRE(system.iopCore().programCounter() == iopPC);
      REQUIRE(system.iopCore().elapsedCycles() == iopCycles);
      REQUIRE(system.eeCore().elapsedCycles() > eeCycles);
      REQUIRE_THROWS_AS(system.iopCore().resumeExecution(), std::logic_error);
    }
  }
}

TEST_CASE("Whole-machine reset cancels combined SIF continuations and pending completions",
  "[sif][cross-channel][lifecycle][reset]")
{
  for (unsigned checkpoint = 0; checkpoint < 3; ++checkpoint)
  {
    CAPTURE(checkpoint);
    NekoSystem system;
    std::vector<std::uint8_t> rom(BootROMImage::SIZE, 0);
    rom.back() = 0xab;
    system.installBootROM(rom);
    const auto identity = system.eeBus().bootROMIdentity();
    bidirectionalPackets(system, 64, 64);
    if (checkpoint != 1) receiver(system);
    iopReceiver(system);
    REQUIRE(system.eeBus().writeData32(0x800, 0x08000200)); // j 0xa0000800
    writeIOP(system, 0x800, 0x08000200);
    system.eeCore().startExecution(0xa0000800);
    system.iopCore().startExecution(0xa0000800);
    system.startTrace();
    system.runMasterCycles(checkpoint == 0 ? 25 : 2048);
    if (checkpoint == 0)
    {
      REQUIRE(system.sif().sif0PackingCursor() == 1);
      REQUIRE(system.iopSIF0DMAC().remainingWords() == 63);
      REQUIRE(system.iopSIF1DMAC().remainingWords() == 63);
    }
    else if (checkpoint == 1)
    {
      REQUIRE(system.sif().freeWordCount() == 0);
      REQUIRE(system.iopSIF0DMAC().active());
    }
    else
    {
      requireBidirectionalPayloads(system, 64, 64);
    }
    system.reset();
    NekoSystem fresh;
    fresh.installBootROM(rom);
    fresh.reset();
    REQUIRE(system.eeBus().bootROMIdentity() == identity);
    REQUIRE(system.iopBus().romIdentity() == identity);
    REQUIRE(system.trace().empty());
    REQUIRE_FALSE(system.traceEnabled());
    REQUIRE(system.masterClockScheduler().currentCycle() == 0);
    REQUIRE(system.saveState() == fresh.saveState());
    system.runMasterCycles(64);
    fresh.runMasterCycles(64);
    REQUIRE(system.saveState() == fresh.saveState());
    bidirectionalPackets(system, 64, 64);
    receiver(system);
    iopReceiver(system);
    drainBidirectional(system);
    requireBidirectionalPayloads(system, 64, 64);
  }
}

TEST_CASE("Unsupported guest reset requests reject atomically without resetting active SIF peers",
  "[sif][cross-channel][lifecycle][reset][guest]")
{
  NekoSystem system;
  bidirectionalPackets(system, 64, 64);
  receiver(system);
  iopReceiver(system);
  system.runMasterCycles(17);
  const auto before = system.saveState();
  for (const std::uint32_t command : {0x80000u, 0xc0000u, 0xc0100u, 0xffffffffu})
  {
    CAPTURE(command);
    REQUIRE_FALSE(system.eeBus().writeData32(0x1000f240, command));
    REQUIRE(system.saveState() == before);
  }
  REQUIRE(system.eeBus().writeData32(0x800, 0xac220000)); // sw r2, 0(r1)
  system.eeCore().setGeneralRegister(1, {0xb000f240, 0});
  system.eeCore().setGeneralRegister(2, {0xc0100, 0});
  system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
  system.eeCore().startExecution(0xa0000800);
  const auto transport = system.sifStateHash();
  const auto sourceAddress = system.iopSIF0DMAC().memoryAddress();
  const auto destinationAddress = system.iopSIF1DMAC().memoryAddress();
  system.clockMasterCycle();
  REQUIRE(system.eeCore().pendingException() == EEException::DataBusErrorStore);
  REQUIRE(system.eeCore().cop0Register(EECOP0Register::EPC) == 0xa0000800);
  REQUIRE(system.masterClockScheduler().currentCycle() == 18);
  REQUIRE(system.sifStateHash() == transport);
  REQUIRE(system.iopSIF0DMAC().memoryAddress() == sourceAddress);
  REQUIRE(system.iopSIF1DMAC().memoryAddress() == destinationAddress);
  REQUIRE(system.iopInterruptController().status() == 0);
  drainBidirectional(system);
  requireBidirectionalPayloads(system, 64, 64);
}

TEST_CASE("Concurrent SIF runs repeat canonically with observational tracing under either priority",
  "[sif][cross-channel][persistence][determinism]")
{
  for (const auto priority : {0x8800u, 0x8900u, 0x9800u})
  {
    CAPTURE(priority);
    NekoSystem plain;
    NekoSystem traced;
    NekoSystem repeated;
    for (auto *system : {&plain, &traced, &repeated})
    {
      bidirectionalExecution(*system, 64, 96);
      system->iopDMAController().writePriority(BANK, priority);
    }
    traced.startTrace();
    repeated.startTrace();
    for (const unsigned boundary : {0u, 1u, 2u, 9u, 17u, 25u, 33u,
      65u, 257u, 513u, 1025u, 2048u})
    {
      CAPTURE(boundary);
      for (auto *system : {&plain, &traced, &repeated})
        system->runMasterCycles(boundary - system->masterClockScheduler().currentCycle());
      requireCombinedState(traced, plain);
      requireCombinedState(repeated, plain);
    }
    for (auto *system : {&plain, &traced, &repeated})
    {
      requireBidirectionalPayloads(*system, 64, 96);
      REQUIRE(system->eeCore().executionState() == EEExecutionState::Running);
      REQUIRE(system->iopCore().executionState() == IOPExecutionState::Running);
      REQUIRE(system->eeCore().generalRegister(4).low ==
        system->eeCore().generalRegister(31).low);
      REQUIRE(system->iopCore().generalRegister(3) == 1);
      REQUIRE(system->iopCore().generalRegister(4) == 2);
    }
    REQUIRE(plain.trace().empty());
    REQUIRE_FALSE(beats(traced).empty());
    IOPTraceTest::requireEvents(repeated.trace(), traced.trace());
  }
}

TEST_CASE("Concurrent SIF restores byte-identically across grants packing and shared backpressure",
  "[sif][cross-channel][persistence][restore]")
{
  for (const unsigned prefix : {0u, 1u, 2u, 9u, 10u, 17u, 25u,
    33u, 65u, 257u, 513u, 1025u})
  {
    CAPTURE(prefix);
    NekoSystem original;
    bidirectionalExecution(original);
    original.startTrace();
    original.runMasterCycles(prefix);
    if (prefix == 1)
    {
      REQUIRE(original.iopCore().delayedResultContinuation().source ==
        IOPDelayedResultSource::Load);
      REQUIRE(original.sif().wordCount(SIFDirection::SIF0) == 4);
    }
    if (prefix == 9) REQUIRE(original.iopCore().branchContinuation().active);
    if (prefix == 10) REQUIRE(original.sif().freeWordCount() == 0);
    if (prefix == 17)
      REQUIRE(original.iopCore().delayedResultContinuation().source ==
        IOPDelayedResultSource::Load);
    if (prefix == 25)
    {
      REQUIRE(original.sif().sif0PackingCursor() == 1);
      REQUIRE(original.sif().wordCount(SIFDirection::SIF0) == 1);
      REQUIRE(original.sif().wordCount(SIFDirection::SIF1) != 0);
    }
    const auto saved = original.saveState();
    NekoSystem restored;
    bidirectionalExecution(restored, 8, 12);
    restored.startTrace();
    restored.runMasterCycles(9);
    const auto history = restored.trace();
    restored.loadState(saved);
    REQUIRE(restored.traceEnabled());
    IOPTraceTest::requireEvents(restored.trace(), history);
    REQUIRE(restored.saveState() == saved);
    requireCombinedState(restored, original);
    original.startTrace();
    restored.startTrace();
    original.clockMasterCycle();
    restored.clockMasterCycle();
    requireCombinedState(restored, original);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    original.runMasterCycles(2048 - prefix - 1);
    restored.runMasterCycles(2048 - prefix - 1);
    requireCombinedState(restored, original);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    requireBidirectionalPayloads(original, 64, 64);
    requireBidirectionalPayloads(restored, 64, 64);
  }
}

TEST_CASE("Disabled common controllers restore accepted SIF packets without rearming or losing progress",
  "[sif][cross-channel][persistence][restore]")
{
  NekoSystem original;
  bidirectionalExecution(original);
  original.runMasterCycles(33);
  original.dmacController().writeControl(0);
  original.iopDMAController().writePriority(BANK, 0);
  const auto transport = original.sifStateHash();
  const auto sourceWords = original.iopSIF0DMAC().remainingWords();
  const auto destinationWords = original.iopSIF1DMAC().remainingWords();
  REQUIRE(sourceWords != 0);
  REQUIRE(destinationWords != 0);
  original.runMasterCycles(17);
  REQUIRE(original.sifStateHash() == transport);
  REQUIRE(original.iopSIF0DMAC().remainingWords() == sourceWords);
  REQUIRE(original.iopSIF1DMAC().remainingWords() == destinationWords);
  NekoSystem restored;
  restored.loadState(original.saveState());
  requireCombinedState(restored, original);
  for (auto *system : {&original, &restored})
  {
    system->startTrace();
    system->runMasterCycles(16);
    REQUIRE(system->sifStateHash() == transport);
    REQUIRE(system->iopSIF0DMAC().remainingWords() == sourceWords);
    REQUIRE(system->iopSIF1DMAC().remainingWords() == destinationWords);
    system->dmacController().writeControl(1);
    system->iopDMAController().writePriority(BANK, 0x8800);
    drainBidirectional(*system);
    requireBidirectionalPayloads(*system, 64, 64);
  }
  requireCombinedState(restored, original);
  IOPTraceTest::requireEvents(restored.trace(), original.trace());
}

TEST_CASE("Combined SIF canonical state ignores free-slot history and dead transport storage",
  "[sif][cross-channel][persistence][canonical]")
{
  NekoSystem original;
  NekoSystem recycled;
  REQUIRE(recycled.sif().pushSIF1Quadword({{0xdead, 0xbeef, 0xaaaa, 0xbbbb}}) ==
    SIFTransferOutcome::Completed);
  REQUIRE(recycled.sif().consumeWords(SIFDirection::SIF1, 4) ==
    SIFTransferOutcome::Completed);
  requireCombinedState(recycled, original);
  for (auto *system : {&original, &recycled})
  {
    bidirectionalExecution(*system);
    system->startTrace();
  }
  for (const unsigned boundary : {1u, 10u, 25u, 65u, 513u, 2048u})
  {
    CAPTURE(boundary);
    for (auto *system : {&original, &recycled})
      system->runMasterCycles(boundary - system->masterClockScheduler().currentCycle());
    requireCombinedState(recycled, original);
  }
  requireBidirectionalPayloads(original, 64, 64);
  requireBidirectionalPayloads(recycled, 64, 64);
  IOPTraceTest::requireEvents(recycled.trace(), original.trace());
}

TEST_CASE("Malformed combined SIF ownership rejects without changing a distinct live destination",
  "[sif][cross-channel][persistence][transactional]")
{
  NekoSystem sourceSystem;
  bidirectionalExecution(sourceSystem);
  sourceSystem.runMasterCycles(25);
  REQUIRE(sourceSystem.sif().wordCount(SIFDirection::SIF0) == 1);
  REQUIRE(sourceSystem.sif().wordCount(SIFDirection::SIF1) == 31);
  const auto saved = sourceSystem.saveState();
  NekoSystem destination;
  bidirectionalExecution(destination, 8, 12);
  destination.startTrace();
  destination.runMasterCycles(9);
  const auto before = destination.saveState();
  const auto history = destination.trace();
  NekoSystem expected;
  expected.loadState(before);
  struct InvalidField
  {
    const char *path;
    std::uint32_t value;
    const char *diagnostic;
  };
  for (const auto field : {
    InvalidField{"system.sif.sif1.wordCount", 32, "SIF streams exceed shared word capacity"},
    InvalidField{"system.sif.packingCursor", 4, "SIF0 packing cursor is invalid"},
    InvalidField{"system.sif0DMAC.destinationPacketAccepted", 0,
      "SIF0 DMAC destination packet continuation is invalid"},
    InvalidField{"system.sif0DMAC.terminateAfterPacket", 0,
      "SIF0 DMAC destination packet continuation is invalid"},
    InvalidField{"system.sif1DMAC.terminateAfterPacket", 1,
      "SIF1 DMAC terminal continuation is invalid"},
    InvalidField{"system.iop.sif0DMA.channelControl", 0x401,
      "IOP SIF0 DMA packet continuation is incompatible with channel state"},
    InvalidField{"system.iop.sif1DMA.channelControl", 0x400,
      "IOP SIF1 DMA packet continuation is incompatible with channel state"},
    InvalidField{"system.iop.dmaController.sifNextChannel", 11,
      "IOP SIF arbitration cursor is invalid"},
    InvalidField{"system.gifDMAC.sifNextDirection", 2,
      "EE SIF arbitration cursor is invalid"}})
  {
    CAPTURE(field.path);
    SaveStateMutation invalid(saved);
    invalid.writeScalar(field.path, field.value);
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(invalid.bytes()), std::invalid_argument);
    REQUIRE_THROWS_WITH(destination.loadState(invalid.bytes()), Catch::Contains(field.diagnostic));
    REQUIRE(destination.saveState() == before);
    requireCombinedState(destination, expected);
    REQUIRE(destination.traceEnabled());
    IOPTraceTest::requireEvents(destination.trace(), history);
  }
  for (auto *system : {&destination, &expected})
  {
    system->startTrace();
    drainBidirectional(*system);
    requireBidirectionalPayloads(*system, 8, 12);
  }
  requireCombinedState(destination, expected);
  IOPTraceTest::requireEvents(destination.trace(), expected.trace());
}
