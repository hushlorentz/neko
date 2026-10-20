#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "catch.hpp"
#include "iop/iop_trace_test_utils.hpp"
#include "save_state_mutation.hpp"

namespace
{
  constexpr IOPWord START = 0x01000401;
  constexpr auto BANK = IOPDMABank::Secondary;
  constexpr IOPAddress TAG = 0x1000;
  constexpr IOPAddress DATA = 0x2000;
  const SIFQuadword PROLOGUE = {{0x70000002, 0x3000, 0xaabbccdd, 0x11223344}};

  void writeWord(NekoSystem &system, IOPAddress address, IOPWord value)
  {
    REQUIRE(system.iopBus().write32(address, value).status == IOPBusStatus::Completed);
  }

  void descriptor(NekoSystem &system, IOPAddress source, IOPWord count,
    IOPWord terminal = 0x80000000)
  {
    writeWord(system, TAG, source | terminal);
    writeWord(system, TAG + 4, count);
    for (std::size_t lane = 0; lane < 4; ++lane)
    {
      writeWord(system, TAG + 8 + lane * 4, PROLOGUE[lane]);
    }
    REQUIRE(system.iopSIF0DMAC().writeTagAddress(TAG) == IOPDMAWriteOutcome::Completed);
  }

  void startSource(NekoSystem &system)
  {
    REQUIRE(system.iopSIF0DMAC().writeChannelControl(START) ==
      IOPDMAWriteOutcome::Completed);
    system.iopDMAController().writePriority(BANK, 0x800);
  }

  IOPDMABeatResult sourceBeat(NekoSystem &system)
  {
    return system.iopDMAController().clockSIF(
      system.iopSIF0DMAC(), system.iopBus(), system.sif());
  }

  SIF0DMABeatOutcome destinationBeat(NekoSystem &system)
  {
    return system.dmacController().clockSIF(system.sif0DMAC());
  }

  void startDestination(NekoSystem &system, std::uint32_t address, std::uint32_t count)
  {
    system.sif0DMAC().writeMemoryAddress(address);
    system.sif0DMAC().writeQuadwordCount(count);
    system.sif0DMAC().writeChannelControl(0x100);
    system.dmacController().writeControl(1);
  }

  void requireStream(const NekoSystem &system, const std::vector<IOPWord> &expected)
  {
    REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
      CAPTURE(index);
      REQUIRE(system.sif().wordAt(SIFDirection::SIF0, index) == expected[index]);
    }
  }

  void requireState(const NekoSystem &system, const std::vector<std::uint8_t> &expected)
  {
    const bool unchanged = system.saveState() == expected;
    REQUIRE(unchanged);
  }

  void requireQuadword(const EEQuadword &value, const SIFQuadword &words)
  {
    REQUIRE(static_cast<std::uint32_t>(value.low) == words[0]);
    REQUIRE(static_cast<std::uint32_t>(value.low >> 32) == words[1]);
    REQUIRE(static_cast<std::uint32_t>(value.high) == words[2]);
    REQUIRE(static_cast<std::uint32_t>(value.high >> 32) == words[3]);
  }
}

TEST_CASE("SIF0 source admits overlapping prologues and exact real word counts",
  "[sif0][dma][engine]")
{
  for (IOPWord count = 0; count <= 9; ++count)
  {
    CAPTURE(count);
    NekoSystem system;
    descriptor(system, DATA, 0xfff00000 | count);
    REQUIRE(system.iopSIF0DMAC().writeBlockControl(0xabcd1234) ==
      IOPDMAWriteOutcome::Completed);
    for (IOPWord index = 0; index < count; ++index)
    {
      writeWord(system, DATA + index * 4, 0x100 + index);
    }
    startSource(system);
    const auto first = sourceBeat(system);
    REQUIRE(first.channel == IOPDMAChannel::SIF0);
    REQUIRE(first.outcome == (count == 0 ?
      IOPDMABeatOutcome::Completed : IOPDMABeatOutcome::Accepted));
    REQUIRE(system.iopSIF0DMAC().tagAddress() == TAG + 16);
    REQUIRE(system.iopSIF0DMAC().remainingWords() == count);
    REQUIRE(system.iopSIF0DMAC().terminalPacket() == (count != 0));
    std::vector<IOPWord> expected(PROLOGUE.begin(), PROLOGUE.end());
    requireStream(system, expected);
    auto latch = PROLOGUE;
    for (IOPWord index = 0; index < count; ++index)
    {
      const auto beat = sourceBeat(system);
      const bool final = index + 1 == count;
      REQUIRE(beat.outcome == (final ?
        IOPDMABeatOutcome::Completed : IOPDMABeatOutcome::Accepted));
      latch[index % 4] = 0x100 + index;
      expected.push_back(0x100 + index);
      if (final)
      {
        for (std::size_t lane = index % 4 + 1; lane < 4; ++lane)
        {
          expected.push_back(latch[lane]);
        }
      }
      requireStream(system, expected);
      REQUIRE(system.iopSIF0DMAC().memoryAddress() == DATA + (index + 1) * 4);
    }
    REQUIRE(system.iopSIF0DMAC().memoryAddress() == DATA + count * 4);
    REQUIRE(system.iopSIF0DMAC().blockControl() == 0xabcd1234);
    REQUIRE_FALSE(system.iopSIF0DMAC().active());
    REQUIRE(system.iopSIF0DMAC().remainingWords() == 0);
    REQUIRE_FALSE(system.iopSIF0DMAC().terminalPacket());
    REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0x04000000);
    REQUIRE(system.iopInterruptController().status() == 8);
    REQUIRE(system.dmacController().status() == 0);
    REQUIRE(system.sif().control() == 0);
  }
}

TEST_CASE("SIF0 source backpressure admits final fill with its last real word",
  "[sif0][dma][engine]")
{
  NekoSystem system;
  descriptor(system, DATA, 1);
  writeWord(system, DATA, 0xface);
  startSource(system);
  for (unsigned index = 0; index < 7; ++index)
  {
    REQUIRE(system.sif().pushSIF1Quadword({{1, 2, 3, 4}}) ==
      SIFTransferOutcome::Completed);
  }
  REQUIRE(sourceBeat(system).outcome == IOPDMABeatOutcome::Accepted);
  REQUIRE(system.sif().freeWordCount() == 0);
  for (unsigned free = 0; free < 4; ++free)
  {
    const auto before = system.saveState();
    REQUIRE(sourceBeat(system).outcome == IOPDMABeatOutcome::Wait);
    requireState(system, before);
    REQUIRE(system.sif().consumeWords(SIFDirection::SIF1, 1) ==
      SIFTransferOutcome::Completed);
  }
  REQUIRE(sourceBeat(system).outcome == IOPDMABeatOutcome::Completed);
  requireStream(system, {PROLOGUE[0], PROLOGUE[1], PROLOGUE[2], PROLOGUE[3],
    0xface, PROLOGUE[1], PROLOGUE[2], PROLOGUE[3]});
  REQUIRE(system.iopSIF0DMAC().memoryAddress() == DATA + 4);
}

TEST_CASE("SIF0 source preflights all six descriptor words before admission",
  "[sif0][dma][engine]")
{
  for (const IOPAddress address : {0x7fffecu, 0x7ffff0u, 0x800000u})
  {
    CAPTURE(address);
    NekoSystem system;
    REQUIRE(system.iopSIF0DMAC().writeTagAddress(address) ==
      IOPDMAWriteOutcome::Completed);
    startSource(system);
    const auto before = system.saveState();
    const auto inspected = system.iopDMAController().inspectSIF(
      system.iopSIF0DMAC(), system.iopBus(), system.sif());
    REQUIRE(inspected.channel == IOPDMAChannel::SIF0);
    const auto result = sourceBeat(system);
    REQUIRE(inspected.outcome == IOPDMABeatOutcome::Rejected);
    REQUIRE(result.outcome == IOPDMABeatOutcome::Rejected);
    REQUIRE(result.failure == IOPDMAFailure::DescriptorRead);
    REQUIRE(result.status == IOPBusStatus::Unmapped);
    REQUIRE(result.address == 0x800000);
    requireState(system, before);
  }
}

TEST_CASE("SIF0 source rejects unaligned headers and non RAM payloads atomically",
  "[sif0][dma][engine]")
{
  for (const IOPAddress source : {DATA + 1, 0x800000u})
  {
    NekoSystem system;
    descriptor(system, source, 1);
    startSource(system);
    if ((source & 3) != 0)
    {
      const auto before = system.saveState();
      const auto result = sourceBeat(system);
      REQUIRE(result.outcome == IOPDMABeatOutcome::Rejected);
      REQUIRE(result.failure == IOPDMAFailure::UnalignedHeader);
      requireState(system, before);
    }
    else
    {
      REQUIRE(sourceBeat(system).outcome == IOPDMABeatOutcome::Accepted);
      const auto before = system.saveState();
      const auto result = sourceBeat(system);
      REQUIRE(result.outcome == IOPDMABeatOutcome::Rejected);
      REQUIRE(result.failure == IOPDMAFailure::SourceRead);
      requireState(system, before);
    }
  }
  NekoSystem system;
  system.iopInterruptController().writeControl(1);
  const auto before = system.saveState();
  for (const IOPAddress address : {0x1f801070u, 0x1f800000u, 0x1fc00000u})
  {
    const auto result = system.iopBus().readDMAC32(address);
    REQUIRE(result.status == IOPBusStatus::Unmapped);
  }
  REQUIRE(system.iopBus().readDMAC32(0x1f801078).status == IOPBusStatus::Unmapped);
  requireState(system, before);
  REQUIRE(system.iopBus().readDMAC32(1).status == IOPBusStatus::Misaligned);
  writeWord(system, 0x20, 0x12345678);
  REQUIRE(system.iopBus().readDMAC32(0x600020).value == 0x12345678);
}

TEST_CASE("SIF0 source nonterminal zero packets advance one descriptor per grant",
  "[sif0][dma][engine]")
{
  for (const IOPWord terminal : {0x40000000u, 0x80000000u, 0xc0000000u})
  {
    NekoSystem system;
    descriptor(system, DATA, 0, 0);
    writeWord(system, TAG + 16, terminal | DATA);
    writeWord(system, TAG + 20, 0);
    writeWord(system, TAG + 24, 0x1111);
    writeWord(system, TAG + 28, 0x2222);
    writeWord(system, TAG + 32, 0x3333);
    writeWord(system, TAG + 36, 0x4444);
    startSource(system);
    REQUIRE(sourceBeat(system).outcome == IOPDMABeatOutcome::Accepted);
    REQUIRE(system.iopSIF0DMAC().active());
    REQUIRE(system.iopSIF0DMAC().tagAddress() == TAG + 16);
    requireStream(system, {PROLOGUE[0], PROLOGUE[1], terminal | DATA, 0});
    REQUIRE(sourceBeat(system).outcome == IOPDMABeatOutcome::Completed);
    REQUIRE(system.iopSIF0DMAC().tagAddress() == TAG + 32);
    requireStream(system, {PROLOGUE[0], PROLOGUE[1], terminal | DATA, 0,
      0x1111, 0x2222, 0x3333, 0x4444});
  }
}

TEST_CASE("SIF0 source pause abort and restore preserve admitted packing history",
  "[sif0][dma][engine][save-state]")
{
  NekoSystem system;
  descriptor(system, DATA, 5);
  for (unsigned index = 0; index < 5; ++index)
  {
    writeWord(system, DATA + index * 4, 0x100 + index);
  }
  startSource(system);
  REQUIRE(sourceBeat(system).outcome == IOPDMABeatOutcome::Accepted);
  for (unsigned accepted = 0; accepted < 5; ++accepted)
  {
    NekoSystem restored;
    restored.loadState(system.saveState());
    IOPTraceTest::requireMachine(restored, system);
    REQUIRE(sourceBeat(system).outcome == sourceBeat(restored).outcome);
    IOPTraceTest::requireMachine(restored, system);
  }
  NekoSystem partial;
  descriptor(partial, DATA, 5);
  startSource(partial);
  REQUIRE(sourceBeat(partial).outcome == IOPDMABeatOutcome::Accepted);
  REQUIRE(sourceBeat(partial).outcome == IOPDMABeatOutcome::Accepted);
  partial.iopDMAController().writePriority(BANK, 0);
  const auto paused = partial.saveState();
  REQUIRE(sourceBeat(partial).outcome == IOPDMABeatOutcome::Wait);
  requireState(partial, paused);
  const auto transport = partial.sifStateHash();
  REQUIRE(partial.iopSIF0DMAC().writeChannelControl(START & ~0x01000000u) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(partial.iopSIF0DMAC().remainingWords() == 0);
  REQUIRE_FALSE(partial.iopSIF0DMAC().terminalPacket());
  REQUIRE(partial.sifStateHash() == transport);
  REQUIRE(partial.iopDMAController().interruptStatus(BANK) == 0);
  REQUIRE(partial.iopSIF0DMAC().tagAddress() == TAG + 16);
}

TEST_CASE("SIF0 source retains the full twenty bit packet count",
  "[sif0][dma][engine][save-state]")
{
  NekoSystem system;
  descriptor(system, DATA, 0xfffff);
  startSource(system);
  REQUIRE(sourceBeat(system).outcome == IOPDMABeatOutcome::Accepted);
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 0xfffff);
  REQUIRE(sourceBeat(system).outcome == IOPDMABeatOutcome::Accepted);
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 0xffffe);
  NekoSystem restored;
  restored.loadState(system.saveState());
  IOPTraceTest::requireMachine(restored, system);
}

TEST_CASE("SIF0 source waits before publishing any prologue or packing state",
  "[sif0][dma][engine]")
{
  NekoSystem system;
  descriptor(system, DATA, 0);
  for (unsigned index = 0; index < 7; ++index)
  {
    REQUIRE(system.sif().pushSIF1Quadword({{1, 2, 3, 4}}) ==
      SIFTransferOutcome::Completed);
  }
  REQUIRE(system.sif().pushSIF0Prologue({{5, 6, 7, 8}}) ==
    SIFTransferOutcome::Completed);
  startSource(system);
  for (unsigned free = 0; free < 4; ++free)
  {
    const auto before = system.saveState();
    REQUIRE(system.iopDMAController().inspectSIF(
      system.iopSIF0DMAC(), system.iopBus(), system.sif()).outcome ==
      IOPDMABeatOutcome::Wait);
    REQUIRE(sourceBeat(system).outcome == IOPDMABeatOutcome::Wait);
    requireState(system, before);
    REQUIRE(system.sif().consumeWords(SIFDirection::SIF1, 1) ==
      SIFTransferOutcome::Completed);
  }
  REQUIRE(sourceBeat(system).outcome == IOPDMABeatOutcome::Completed);
  REQUIRE(system.sif().sif0PackingLatch() == PROLOGUE);
}

TEST_CASE("SIF0 final fill never reads beyond the final real source word",
  "[sif0][dma][engine]")
{
  NekoSystem system;
  descriptor(system, 0x7ffffc, 1);
  writeWord(system, 0x7ffffc, 0x87654321);
  startSource(system);
  REQUIRE(sourceBeat(system).outcome == IOPDMABeatOutcome::Accepted);
  REQUIRE(sourceBeat(system).outcome == IOPDMABeatOutcome::Completed);
  REQUIRE(system.iopSIF0DMAC().memoryAddress() == 0x800000);
  requireStream(system, {PROLOGUE[0], PROLOGUE[1], PROLOGUE[2], PROLOGUE[3],
    0x87654321, PROLOGUE[1], PROLOGUE[2], PROLOGUE[3]});
}

TEST_CASE("SIF0 EE normal receiver writes raw stream qwords without tag decoding",
  "[sif0][dma][engine]")
{
  NekoSystem system;
  REQUIRE(system.sif().pushSIF0Prologue(PROLOGUE) == SIFTransferOutcome::Completed);
  REQUIRE(system.sif().pushSIF0Prologue({{1, 2, 3, 4}}) ==
    SIFTransferOutcome::Completed);
  startDestination(system, 0x3000, 2);
  REQUIRE(destinationBeat(system) == SIF0DMABeatOutcome::Completed);
  EEQuadword value = {};
  REQUIRE(system.eeBus().readDMAC128(0x3000, &value));
  requireQuadword(value, PROLOGUE);
  REQUIRE(system.sif0DMAC().memoryAddress() == 0x3010);
  REQUIRE(system.sif0DMAC().quadwordCount() == 1);
  REQUIRE(system.sif0DMAC().active());
  REQUIRE(destinationBeat(system) == SIF0DMABeatOutcome::Completed);
  REQUIRE(system.eeBus().readDMAC128(0x3010, &value));
  requireQuadword(value, {{1, 2, 3, 4}});
  REQUIRE_FALSE(system.sif0DMAC().active());
  REQUIRE(system.dmacController().status() == DMACStatus::CHANNEL_5);
  REQUIRE(system.sif().control() == 0);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 0);
  REQUIRE(system.iopInterruptController().status() == 0);
}

TEST_CASE("SIF0 EE normal receiver uses indexed SPR writes and preserves its selector",
  "[sif0][dma][engine][save-state]")
{
  for (const std::uint32_t address : {0x80003ff0u, 0xfffffff0u})
  {
    NekoSystem system;
    REQUIRE(system.sif().pushSIF0Prologue(PROLOGUE) == SIFTransferOutcome::Completed);
    REQUIRE(system.sif().pushSIF0Prologue({{5, 6, 7, 8}}) ==
      SIFTransferOutcome::Completed);
    startDestination(system, address, 2);
    REQUIRE(destinationBeat(system) == SIF0DMABeatOutcome::Completed);
    EEQuadword value = {};
    REQUIRE(system.eeMemorySystem().readScratchpadDMA128(0x3ff0, &value) ==
      EEScratchpadAccessResult::Completed);
    requireQuadword(value, PROLOGUE);
    REQUIRE(system.sif0DMAC().memoryAddress() ==
      (address == 0xfffffff0 ? 0x80000000u : 0x80004000u));
    NekoSystem restored;
    restored.loadState(system.saveState());
    REQUIRE(destinationBeat(system) == SIF0DMABeatOutcome::Completed);
    REQUIRE(destinationBeat(restored) == SIF0DMABeatOutcome::Completed);
    IOPTraceTest::requireMachine(restored, system);
    REQUIRE(system.eeMemorySystem().readScratchpadDMA128(0, &value) ==
      EEScratchpadAccessResult::Completed);
    requireQuadword(value, {{5, 6, 7, 8}});
  }
}

TEST_CASE("SIF0 EE normal receiver waits for complete qwords and common enable",
  "[sif0][dma][engine]")
{
  for (unsigned prefix = 0; prefix < 4; ++prefix)
  {
    NekoSystem system;
    if (prefix != 0)
    {
      REQUIRE(system.sif().pushSIF0Prologue(PROLOGUE) == SIFTransferOutcome::Completed);
      REQUIRE(system.sif().consumeWords(SIFDirection::SIF0, 4 - prefix) ==
        SIFTransferOutcome::Completed);
    }
    startDestination(system, 0x3000, 1);
    const auto before = system.saveState();
    REQUIRE(destinationBeat(system) == SIF0DMABeatOutcome::Wait);
    requireState(system, before);
  }
  NekoSystem system;
  REQUIRE(system.sif().pushSIF0Prologue(PROLOGUE) == SIFTransferOutcome::Completed);
  startDestination(system, 0x3000, 1);
  system.dmacController().writeControl(0);
  const auto before = system.saveState();
  REQUIRE(destinationBeat(system) == SIF0DMABeatOutcome::Wait);
  requireState(system, before);
}

TEST_CASE("SIF0 EE normal bus errors preserve queued data and register progress",
  "[sif0][dma][engine]")
{
  for (const std::uint32_t address :
    {0x02000000u, 0x1000f000u, 0x1fc00000u, 0x20003000u})
  {
    NekoSystem system;
    REQUIRE(system.sif().pushSIF0Prologue(PROLOGUE) == SIFTransferOutcome::Completed);
    startDestination(system, address, 1);
    const auto transport = system.sifStateHash();
    REQUIRE(destinationBeat(system) == SIF0DMABeatOutcome::BusError);
    REQUIRE_FALSE(system.sif0DMAC().active());
    REQUIRE(system.sif0DMAC().memoryAddress() == address);
    REQUIRE(system.sif0DMAC().quadwordCount() == 1);
    REQUIRE(system.dmacController().status() ==
      (DMACStatus::CHANNEL_5 | DMACStatus::BUS_ERROR));
    REQUIRE(system.dmacController().interruptPending());
    REQUIRE(system.sifStateHash() != transport);
    requireStream(system, {PROLOGUE[0], PROLOGUE[1], PROLOGUE[2], PROLOGUE[3]});
    REQUIRE(system.sif().sif0PackingLatch() == PROLOGUE);
    REQUIRE(system.iopInterruptController().status() == 0);
  }
  NekoSystem zero;
  startDestination(zero, 0x1fc00000, 0);
  REQUIRE(destinationBeat(zero) == SIF0DMABeatOutcome::Completed);
  REQUIRE(zero.dmacController().status() == DMACStatus::CHANNEL_5);
}

TEST_CASE("SIF0 checked endpoints stream a packet larger than shared capacity",
  "[sif0][dma][engine][save-state]")
{
  constexpr unsigned count = 65;
  NekoSystem system;
  descriptor(system, DATA, count);
  std::vector<IOPWord> expected(PROLOGUE.begin(), PROLOGUE.end());
  for (unsigned index = 0; index < count; ++index)
  {
    writeWord(system, DATA + index * 4, 0x100 + index);
    expected.push_back(0x100 + index);
  }
  expected.insert(expected.end(), {0x13d, 0x13e, 0x13f});
  startSource(system);
  startDestination(system, 0x3000, static_cast<std::uint32_t>(expected.size() / 4));
  bool waited = false;
  for (unsigned grant = 0;
       grant < 512 && (system.iopSIF0DMAC().active() || system.sif0DMAC().active());
       ++grant)
  {
    const auto source = sourceBeat(system);
    REQUIRE(source.outcome != IOPDMABeatOutcome::Rejected);
    waited |= system.iopSIF0DMAC().active() &&
      source.outcome == IOPDMABeatOutcome::Wait;
    if (grant % 8 == 0)
    {
      REQUIRE(destinationBeat(system) != SIF0DMABeatOutcome::BusError);
    }
    REQUIRE(system.sif().wordCount(SIFDirection::SIF0) <= SIF::WORD_CAPACITY);
  }
  REQUIRE(waited);
  REQUIRE_FALSE(system.iopSIF0DMAC().active());
  REQUIRE_FALSE(system.sif0DMAC().active());
  REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 0);
  for (std::size_t index = 0; index < expected.size(); index += 4)
  {
    EEQuadword value = {};
    REQUIRE(system.eeBus().readDMAC128(0x3000 + index * 4, &value));
    requireQuadword(value,
      {{expected[index], expected[index + 1], expected[index + 2], expected[index + 3]}});
  }
  system.iopInterruptController().writeStatus(~8u);
  NekoSystem restored;
  restored.loadState(system.saveState());
  IOPTraceTest::requireMachine(restored, system);
  REQUIRE(restored.iopInterruptController().status() == 0);
  REQUIRE(restored.iopDMAController().interruptStatus(BANK) == 0x04000000);
}

TEST_CASE("SIF0 checked grants reject foreign owners and unimplemented chain work",
  "[sif0][dma][engine]")
{
  NekoSystem system;
  NekoSystem foreign;
  const auto before = system.saveState();
  REQUIRE_THROWS_AS(system.iopDMAController().clockSIF(
    foreign.iopSIF0DMAC(), system.iopBus(), system.sif()), std::invalid_argument);
  REQUIRE_THROWS_AS(system.iopDMAController().inspectSIF(
    system.iopSIF0DMAC(), foreign.iopBus(), system.sif()), std::invalid_argument);
  REQUIRE_THROWS_AS(foreign.dmacController().clockSIF(system.sif0DMAC()),
    std::invalid_argument);
  requireState(system, before);
  system.sif0DMAC().writeChannelControl(0x104);
  system.dmacController().writeControl(1);
  const auto armed = system.saveState();
  REQUIRE_THROWS_AS(destinationBeat(system), std::logic_error);
  requireState(system, armed);
}

TEST_CASE("SIF0 source malformed continuations reject before live restore",
  "[sif0][dma][engine][save-state]")
{
  NekoSystem source;
  descriptor(source, DATA, 5);
  startSource(source);
  REQUIRE(sourceBeat(source).outcome == IOPDMABeatOutcome::Accepted);
  NekoSystem destination;
  destination.runMasterCycles(9);
  const auto before = destination.saveState();
  for (const auto path : {"system.iop.sif0DMA.remainingWords",
    "system.iop.sif0DMA.channelControl"})
  {
    SaveStateMutation mutation(source.saveState());
    mutation.writeScalar(path, path == std::string("system.iop.sif0DMA.remainingWords") ?
      0x100000 : (START & ~0x01000000u));
    mutation.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(mutation.bytes()), std::invalid_argument);
    requireState(destination, before);
  }
  SaveStateMutation emptyTerminal(source.saveState());
  emptyTerminal.writeScalar("system.iop.sif0DMA.remainingWords", 0);
  emptyTerminal.refreshChecksum();
  REQUIRE_THROWS_AS(destination.loadState(emptyTerminal.bytes()), std::invalid_argument);
  requireState(destination, before);
}
