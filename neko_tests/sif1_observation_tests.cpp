#include <cstdint>
#include <stdexcept>
#include <vector>

#include "catch.hpp"
#include "iop/iop_trace_test_utils.hpp"
#include "save_state_mutation.hpp"

namespace
{
  // Append-only trace contract: endpoint | kind << 8 | outcome << 16 | failure << 24.
  constexpr auto DMA_BEAT = static_cast<NekoTraceEventType>(20);
  constexpr std::uint64_t EE = 0;
  constexpr std::uint64_t IOP = 1;
  constexpr std::uint64_t TAG = 0;
  constexpr std::uint64_t HEADER = 1 << 8;
  constexpr std::uint64_t PAYLOAD = 2 << 8;
  constexpr std::uint64_t COMPLETION = 3 << 8;
  constexpr std::uint64_t ACCEPTED = 0;
  constexpr std::uint64_t WAIT = 1 << 16;
  constexpr std::uint64_t COMPLETED = 2 << 16;
  constexpr std::uint64_t REJECTED = 3 << 16;
  constexpr IOPDMABank BANK = IOPDMABank::Secondary;

  std::uint64_t pair(std::uint32_t before, std::uint32_t after)
  {
    return before | (static_cast<std::uint64_t>(after) << 32);
  }

  void writeSource(NekoSystem &system, std::uint32_t address,
    const SIFQuadword &words)
  {
    REQUIRE(system.eeBus().writeDMAC128(address, {
      words[0] | (static_cast<std::uint64_t>(words[1]) << 32),
      words[2] | (static_cast<std::uint64_t>(words[3]) << 32)}));
  }

  void startSender(NekoSystem &system, std::uint32_t address,
    std::uint32_t count)
  {
    system.sif1DMAC().writeMemoryAddress(address);
    system.sif1DMAC().writeQuadwordCount(count);
    system.sif1DMAC().writeChannelControl(0x100);
    system.dmacController().writeControl(1);
  }

  void startReceiver(NekoSystem &system)
  {
    REQUIRE(system.iopSIF1DMAC().writeChannelControl(0x01000400) ==
      IOPDMAWriteOutcome::Completed);
    system.iopDMAController().writePriority(BANK, 0x8000);
  }

  void installTransfer(NekoSystem &system)
  {
    writeSource(system, 0x1000, {{0x80002000, 4, 0, 0}});
    writeSource(system, 0x1010, {{1, 2, 3, 4}});
    startSender(system, 0x1000, 2);
    startReceiver(system);
  }

  void installChainTransfer(NekoSystem &system)
  {
    writeSource(system, 0x1000, {{0x70000002, 0, 0, 0}});
    writeSource(system, 0x1010, {{0x80002000, 4, 0, 0}});
    writeSource(system, 0x1020, {{1, 2, 3, 4}});
    system.sif1DMAC().writeTagAddress(0x1000);
    system.sif1DMAC().writeChannelControl(0x104);
    system.dmacController().writeControl(1);
    startReceiver(system);
  }

  void requireTransferCompleted(NekoSystem &system)
  {
    REQUIRE_FALSE(system.sif1DMAC().active());
    REQUIRE_FALSE(system.iopSIF1DMAC().active());
    REQUIRE(system.dmacController().status() == DMACStatus::CHANNEL_6);
    REQUIRE(system.iopDMAController().interruptStatus(BANK) == 0x08000000);
    REQUIRE(system.iopInterruptController().status() == 8);
    REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 0);
    for (IOPAddress index = 0; index < 4; ++index)
    {
      const auto word = system.iopBus().read32(0x2000 + 4 * index);
      REQUIRE(word.status == IOPBusStatus::Completed);
      REQUIRE(word.value == index + 1);
    }
  }

  std::vector<NekoTraceEvent> dmaEvents(const NekoSystem &system)
  {
    std::vector<NekoTraceEvent> events;
    for (const auto &event : system.trace())
    {
      if (event.subsystem == NekoTraceSubsystem::SIF && event.type == DMA_BEAT)
      {
        events.push_back(event);
      }
    }
    return events;
  }

  NekoTraceEvent beat(std::uint64_t cycle, std::uint64_t metadata,
    std::uint32_t address, std::uint32_t before, std::uint32_t after,
    std::uint32_t queuedBefore, std::uint32_t queuedAfter)
  {
    return {cycle, NekoTraceSubsystem::SIF, DMA_BEAT, metadata, address,
      pair(before, after), pair(queuedBefore, queuedAfter)};
  }
}

TEST_CASE("SIF1 DMA traces distinguish endpoint grants counts and local completions",
  "[sif1][observations][trace]")
{
  NekoSystem system;
  installTransfer(system);
  system.startTrace();
  system.runMasterCycles(33);
  IOPTraceTest::requireEvents(dmaEvents(system), {
    beat(1, EE | PAYLOAD | ACCEPTED, 0x1000, 2, 1, 0, 4),
    beat(1, IOP | HEADER | ACCEPTED, 0x2000, 0, 4, 4, 0),
    beat(2, EE | PAYLOAD | COMPLETED, 0x1010, 1, 0, 0, 4),
    beat(9, IOP | PAYLOAD | ACCEPTED, 0x2000, 4, 3, 4, 3),
    beat(17, IOP | PAYLOAD | ACCEPTED, 0x2004, 3, 2, 3, 2),
    beat(25, IOP | PAYLOAD | ACCEPTED, 0x2008, 2, 1, 2, 1),
    beat(33, IOP | PAYLOAD | COMPLETED, 0x200c, 1, 0, 1, 0)});
  const auto &events = system.trace();
  std::size_t completion = events.size();
  std::size_t interrupt = events.size();
  for (std::size_t index = 0; index < events.size(); ++index)
  {
    if (events[index].subsystem == NekoTraceSubsystem::SIF &&
        events[index].value0 == (IOP | PAYLOAD | COMPLETED))
    {
      completion = index;
    }
    if (events[index].subsystem == NekoTraceSubsystem::IOPInterruptController)
    {
      interrupt = index;
    }
  }
  REQUIRE(completion < interrupt);
  REQUIRE(interrupt < events.size());
}

TEST_CASE("SIF1 DMA traces observe eligible waits but not disabled grants or host polling",
  "[sif1][observations][trace]")
{
  NekoSystem system;
  for (unsigned index = 0; index < SIF::WORD_CAPACITY / 4; ++index)
  {
    REQUIRE(system.sif().pushSIF0Prologue({{1, 2, 3, 4}}) ==
      SIFTransferOutcome::Completed);
  }
  writeSource(system, 0x1000, {{5, 6, 7, 8}});
  startSender(system, 0x1000, 1);
  startReceiver(system);
  system.startTrace();
  system.clockMasterCycle();
  IOPTraceTest::requireEvents(dmaEvents(system), {
    beat(1, EE | PAYLOAD | WAIT, 0x1000, 1, 1, 0, 0),
    beat(1, IOP | HEADER | WAIT, 0, 0, 0, 0, 0)});
  system.runMasterCycles(16);
  const auto waits = dmaEvents(system);
  REQUIRE(waits.size() == 20);
  for (std::uint64_t cycle = 1; cycle <= 17; ++cycle)
  {
    unsigned count = 0;
    for (const auto &event : waits)
    {
      if (event.masterCycle == cycle) ++count;
    }
    REQUIRE(count <= 2);
  }
  system.clearTrace();
  const auto saved = system.saveState();
  REQUIRE(system.eeBus().read32(0x1000c400) == 0x100);
  REQUIRE(system.iopBus().read32(0x1f801538).status == IOPBusStatus::Completed);
  REQUIRE(system.iopDMAController().inspectSIF(system.iopSIF1DMAC(),
    system.iopBus(), system.sif()).outcome == IOPDMABeatOutcome::Wait);
  REQUIRE(system.trace().empty());
  REQUIRE(system.saveState() == saved);
  system.dmacController().writeControl(0);
  system.iopDMAController().writePriority(BANK, 0);
  system.runMasterCycles(24);
  REQUIRE(dmaEvents(system).empty());
}

TEST_CASE("SIF1 DMA traces distinguish source tags zero-count completion and bus errors",
  "[sif1][observations][trace]")
{
  NekoSystem system;
  writeSource(system, 0x1000, {{0x70000001, 0, 0, 0}});
  writeSource(system, 0x1010, {{1, 2, 3, 4}});
  system.sif1DMAC().writeTagAddress(0x1000);
  system.sif1DMAC().writeChannelControl(0x104);
  system.dmacController().writeControl(1);
  system.startTrace();
  system.runMasterCycles(2);
  IOPTraceTest::requireEvents(dmaEvents(system), {
    beat(1, EE | TAG | ACCEPTED, 0x1000, 0, 1, 0, 0),
    beat(2, EE | PAYLOAD | COMPLETED, 0x1010, 1, 0, 0, 4)});
  system.clearTrace();
  startSender(system, 0x1020, 0);
  system.clockMasterCycle();
  IOPTraceTest::requireEvents(dmaEvents(system), {
    beat(3, EE | COMPLETION | COMPLETED, 0x1020, 0, 0, 4, 4)});
  system.clearTrace();
  startSender(system, EEMemoryMap::MAIN_MEMORY_SIZE, 1);
  system.clockMasterCycle();
  IOPTraceTest::requireEvents(dmaEvents(system), {
    beat(4, EE | PAYLOAD | REJECTED | (UINT64_C(1) << 24),
      EEMemoryMap::MAIN_MEMORY_SIZE, 1, 1, 4, 4)});
}

TEST_CASE("SIF1 DMA traces cover terminal empty beats and preserve the SPR selector",
  "[sif1][observations][trace]")
{
  NekoSystem system;
  SECTION("Zero-count terminal source tag")
  {
    writeSource(system, 0x1000, {{0x70000000, 0, 0, 0}});
    system.sif1DMAC().writeTagAddress(0x1000);
    system.sif1DMAC().writeChannelControl(0x104);
    system.dmacController().writeControl(1);
    system.startTrace();
    system.clockMasterCycle();
    IOPTraceTest::requireEvents(dmaEvents(system), {
      beat(1, EE | TAG | COMPLETED, 0x1000, 0, 0, 0, 0)});
  }
  SECTION("Zero-count terminal receiver header")
  {
    writeSource(system, 0x1000, {{0x80002000, 0, 0, 0}});
    startSender(system, 0x1000, 1);
    startReceiver(system);
    system.startTrace();
    system.clockMasterCycle();
    IOPTraceTest::requireEvents(dmaEvents(system), {
      beat(1, EE | PAYLOAD | COMPLETED, 0x1000, 1, 0, 0, 4),
      beat(1, IOP | HEADER | COMPLETED, 0x2000, 0, 0, 4, 0)});
    REQUIRE(system.iopInterruptController().status() == 8);
  }
  SECTION("Wrapped physical scratchpad payload")
  {
    REQUIRE(system.eeMemorySystem().writeScratchpadDMA128(
      0, {UINT64_C(0x0000000200000001), UINT64_C(0x0000000400000003)}) ==
      EEScratchpadAccessResult::Completed);
    startSender(system, 0x80004000, 1);
    system.startTrace();
    system.clockMasterCycle();
    IOPTraceTest::requireEvents(dmaEvents(system), {
      beat(1, EE | PAYLOAD | COMPLETED, 0x80004000, 1, 0, 0, 4)});
    REQUIRE(system.sif().wordAt(SIFDirection::SIF1, 0) == 1);
  }
}

TEST_CASE("SIF1 DMA rejection traces retain the completed prefix without advancing the receiver",
  "[sif1][observations][trace]")
{
  for (const bool header : {false, true})
  {
    CAPTURE(header);
    NekoSystem system;
    writeSource(system, 0x1000, {{header ? 0x80002001u : 0x80800000u, 4, 0, 0}});
    writeSource(system, 0x1010, {{1, 2, 3, 4}});
    startSender(system, 0x1000, 2);
    startReceiver(system);
    system.startTrace();
    if (!header)
    {
      system.runMasterCycles(8);
    }
    NekoSystem plain;
    plain.loadState(system.saveState());
    REQUIRE_THROWS_AS(system.clockMasterCycle(), IOPDMATransferError);
    REQUIRE_THROWS_AS(plain.clockMasterCycle(), IOPDMATransferError);
    IOPTraceTest::requireMachine(system, plain);
    const auto events = dmaEvents(system);
    REQUIRE_FALSE(events.empty());
    const auto expected = beat(header ? 1 : 9,
      IOP | (header ? HEADER : PAYLOAD) | REJECTED |
        (static_cast<std::uint64_t>(header ? 2 : 3) << 24),
      header ? 0x2001 : 0x800000, header ? 0 : 4, header ? 0 : 4,
      4, 4);
    if (header)
    {
      IOPTraceTest::requireEvents(events, {
        beat(1, EE | PAYLOAD | ACCEPTED, 0x1000, 2, 1, 0, 4), expected});
    }
    else
    {
      IOPTraceTest::requireEvents(events, {
        beat(1, EE | PAYLOAD | ACCEPTED, 0x1000, 2, 1, 0, 4),
        beat(1, IOP | HEADER | ACCEPTED, 0x800000, 0, 4, 4, 0),
        beat(2, EE | PAYLOAD | COMPLETED, 0x1010, 1, 0, 0, 4), expected});
    }
    REQUIRE(system.iopSIF1DMAC().active());
    REQUIRE(system.iopInterruptController().status() == 0);
    const auto count = events.size();
    system.sif1DMAC().writeChannelControl(0);
    system.iopDMAController().writePriority(BANK, 0);
    system.clockMasterCycle();
    REQUIRE(dmaEvents(system).size() == count);
  }
}

TEST_CASE("SIF1 DMA trace order includes guest SIF writes and same-pulse interrupt delivery",
  "[sif1][observations][trace]")
{
  NekoSystem system;
  REQUIRE(system.iopBus().write32(0, 0x34010401).status ==
    IOPBusStatus::Completed); // ori r1, r0, IEc | IM2
  REQUIRE(system.iopBus().write32(4, 0x40816000).status ==
    IOPBusStatus::Completed); // mtc0 r1, Status
  system.iopCore().startExecution(0xa0000000);
  system.runMasterCycles(16);
  system.iopInterruptController().writeMask(8);
  system.iopInterruptController().writeControl(1);
  installTransfer(system);
  system.eeBus().write32(0, 0xac220000);
  system.eeCore().setGeneralRegister(1, {0xb000f200, 0});
  system.eeCore().setGeneralRegister(2, {0x1234, 0});
  system.eeCore().startExecution(0xa0000000);
  system.startTrace();
  system.clockMasterCycle();
  auto sif = IOPTraceTest::subsystemEvents(system, NekoTraceSubsystem::SIF);
  REQUIRE(sif.size() == 3);
  REQUIRE(sif[0].type == NekoTraceEventType::RegisterWrite);
  REQUIRE(sif[0].value1 == 0x1234);
  REQUIRE(sif[1].value0 == (EE | PAYLOAD | ACCEPTED));
  REQUIRE(sif[2].value0 == (IOP | HEADER | ACCEPTED));
  system.eeCore().haltExecution();
  system.runMasterCycles(31);
  system.clearTrace();
  system.clockMasterCycle();
  const auto &events = system.trace();
  std::size_t completion = events.size();
  std::size_t request = events.size();
  std::size_t delivery = events.size();
  for (std::size_t index = 0; index < events.size(); ++index)
  {
    if (events[index].subsystem == NekoTraceSubsystem::SIF &&
        events[index].type == DMA_BEAT) completion = index;
    if (events[index].subsystem == NekoTraceSubsystem::IOPInterruptController)
      request = index;
    if (events[index].subsystem == NekoTraceSubsystem::IOP &&
        events[index].type == NekoTraceEventType::InterruptDelivered)
      delivery = index;
  }
  REQUIRE(completion < request);
  REQUIRE(request < delivery);
  REQUIRE(delivery < events.size());
  REQUIRE(events[completion].value0 == (IOP | PAYLOAD | COMPLETED));
  REQUIRE(events[completion].masterCycle == 49);
}

TEST_CASE("SIF1 composed acceptance boundaries restore traces and reset both endpoint owners",
  "[sif1][observations][lifecycle][save-state]")
{
  for (const unsigned prefix : {0u, 1u, 2u, 3u, 8u, 9u, 16u, 17u,
    25u, 33u, 41u})
  {
    CAPTURE(prefix);
    NekoSystem original;
    installChainTransfer(original);
    original.runMasterCycles(prefix);
    const auto saved = original.saveState();
    NekoSystem restored;
    restored.startTrace();
    restored.clockMasterCycle();
    const auto destinationHistory = restored.trace();
    restored.loadState(saved);
    REQUIRE(restored.traceEnabled());
    IOPTraceTest::requireEvents(restored.trace(), destinationHistory);
    REQUIRE(restored.saveState() == saved);
    // Only the restored machine observes this first suffix cycle.
    original.clockMasterCycle();
    restored.clockMasterCycle();
    IOPTraceTest::requireMachine(restored, original);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(48);
    restored.runMasterCycles(48);
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    requireTransferCompleted(original);
    for (auto *system : {&original, &restored})
    {
      system->loadState(saved);
      system->reset();
      REQUIRE_FALSE(system->traceEnabled());
      REQUIRE(system->trace().empty());
      NekoSystem fresh;
      IOPTraceTest::requireMachine(*system, fresh);
      installChainTransfer(*system);
      system->startTrace();
      system->runMasterCycles(41);
      requireTransferCompleted(*system);
    }
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
  }
}

TEST_CASE("SIF1 shared-capacity backpressure restores without importing trace history",
  "[sif1][observations][lifecycle][save-state]")
{
  NekoSystem original;
  installTransfer(original);
  for (unsigned index = 0; index < SIF::WORD_CAPACITY / 4; ++index)
  {
    REQUIRE(original.sif().pushSIF0Prologue({{1, 2, 3, 4}}) ==
      SIFTransferOutcome::Completed);
  }
  original.startTrace();
  original.runMasterCycles(9);
  const auto waitingState = original.saveState();
  NekoSystem restored;
  restored.loadState(waitingState);
  REQUIRE_FALSE(restored.traceEnabled());
  REQUIRE(restored.trace().empty());
  original.clearTrace();
  restored.startTrace();
  for (auto *system : {&original, &restored})
  {
    REQUIRE(system->sif().consumeWords(SIFDirection::SIF0, 4) ==
      SIFTransferOutcome::Completed);
    system->runMasterCycles(48);
    requireTransferCompleted(*system);
  }
  IOPTraceTest::requireMachine(restored, original);
  IOPTraceTest::requireEvents(restored.trace(), original.trace());
  original.stopTrace();
  const auto events = original.trace();
  original.runMasterCycles(16);
  IOPTraceTest::requireEvents(original.trace(), events);
  original.loadState(waitingState);
  original.reset();
  NekoSystem fresh;
  IOPTraceTest::requireMachine(original, fresh);
  REQUIRE(original.trace().empty());
}

TEST_CASE("SIF1 partial header and empty payload waits restore without consuming data",
  "[sif1][observations][lifecycle][save-state]")
{
  for (const unsigned queued : {0u, 1u, 2u, 3u, 4u})
  {
    CAPTURE(queued);
    NekoSystem original;
    if (queued != 0)
    {
      REQUIRE(original.sif().pushSIF1Quadword({{0x80002000, 4, 0, 0}}) ==
        SIFTransferOutcome::Completed);
      if (queued < 4)
      {
        REQUIRE(original.sif().consumeWords(SIFDirection::SIF1, 4 - queued) ==
          SIFTransferOutcome::Completed);
      }
    }
    startReceiver(original);
    original.clockMasterCycle();
    const auto saved = original.saveState();
    NekoSystem restored;
    restored.loadState(saved);
    const auto sifHash = original.sifStateHash();
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(16);
    restored.runMasterCycles(16);
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    REQUIRE(original.sifStateHash() == sifHash);
    const auto waiting = dmaEvents(original);
    REQUIRE(waiting.size() == 2);
    REQUIRE(waiting[0].value0 == (IOP | (queued == 4 ? PAYLOAD : HEADER) | WAIT));
    REQUIRE(waiting[0].value1 == (queued == 4 ? 0x2000 : 0));
    original.reset();
    restored.reset();
    IOPTraceTest::requireMachine(restored, original);
  }
}

TEST_CASE("SIF1 malformed composed boundary states leave destination and observations intact",
  "[sif1][observations][lifecycle][save-state]")
{
  NekoSystem destination;
  installTransfer(destination);
  destination.startTrace();
  destination.runMasterCycles(9);
  const auto before = destination.saveState();
  const auto history = destination.trace();
  for (const unsigned prefix : {0u, 1u, 2u, 3u, 9u, 17u, 25u, 33u, 41u})
  {
    CAPTURE(prefix);
    NekoSystem source;
    installChainTransfer(source);
    source.runMasterCycles(prefix);
    struct Field
    {
      const char *path;
      std::uint64_t value;
    };
    for (const auto field : {
      Field{"system.sif1DMAC.memoryAddressRegister", 0x1001},
      Field{"system.iop.sif1DMA.remainingWords", 0x100000},
      Field{"system.sif.packingCursor", 4}})
    {
      CAPTURE(field.path);
      SaveStateMutation invalid(source.saveState());
      invalid.writeScalar(field.path, field.value);
      invalid.refreshChecksum();
      REQUIRE_THROWS_AS(destination.loadState(invalid.bytes()), std::invalid_argument);
      REQUIRE(destination.saveState() == before);
      REQUIRE(destination.traceEnabled());
      IOPTraceTest::requireEvents(destination.trace(), history);
    }
  }
}
