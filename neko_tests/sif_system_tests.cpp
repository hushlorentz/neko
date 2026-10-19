#include <cstdint>
#include <array>
#include <stdexcept>
#include <string>
#include <vector>

#include "catch.hpp"
#include "neko_system.hpp"
#include "save_state_mutation.hpp"

namespace
{
  void seed(NekoSystem &system)
  {
    REQUIRE(system.eeBus().writeData32(EEMemoryMap::SIF_BASE, 0xfedcba98));
    REQUIRE(system.iopBus().write32(IOPBus::SIF_BASE + 0x10, 0x87654321).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.eeBus().writeData32(EEMemoryMap::SIF_BASE + 0x20, 0xabcdef01));
    REQUIRE(system.iopBus().write32(IOPBus::SIF_BASE + 0x30, 0x12345678).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.iopBus().write32(IOPBus::SIF_BASE + 0x40, 0xb0).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.eeBus().writeData32(EEMemoryMap::SIF_BASE + 0x40, 0x40100));
    REQUIRE(system.iopBus().write32(IOPBus::SIF_DOORBELL, 0x80000002).status ==
      IOPBusStatus::Completed);
    auto &sif = system.sif();
    REQUIRE(sif.pushSIF0Prologue({{1, 2, 3, 4}}) == SIFTransferOutcome::Completed);
    for (std::uint32_t value : {5, 6, 7})
    {
      REQUIRE(sif.pushSIF0PayloadWord(value, SIFPayloadBoundary::Continuing) ==
        SIFTransferOutcome::Completed);
    }
    REQUIRE(sif.pushSIF1Quadword({{8, 9, 10, 11}}) == SIFTransferOutcome::Completed);
    REQUIRE(sif.pushSIF1Quadword({{12, 13, 14, 15}}) == SIFTransferOutcome::Completed);
    REQUIRE(sif.consumeWords(SIFDirection::SIF0, 3) == SIFTransferOutcome::Completed);
    REQUIRE(sif.consumeWords(SIFDirection::SIF1, 3) == SIFTransferOutcome::Completed);
  }

  std::vector<NekoTraceEvent> sifEvents(const NekoSystem &system)
  {
    std::vector<NekoTraceEvent> result;
    for (const auto &event : system.trace())
    {
      if (event.subsystem == NekoTraceSubsystem::SIF)
      {
        result.push_back(event);
      }
    }
    return result;
  }

  void eeStore(NekoSystem &system, std::uint32_t value)
  {
    system.eeBus().write32(0, 0xac220000);
    system.eeCore().setGeneralRegister(1, {0xb000f240, 0});
    system.eeCore().setGeneralRegister(2, {value, 0});
    system.eeCore().startExecution(0xa0000000);
  }

  void iopDoorbellProgram(NekoSystem &system)
  {
    const std::array<IOPWord, 5> program = {{
      0x3c01bf80, 0x34211450, 0x34020002, 0, 0xac220000}};
    for (std::size_t index = 0; index < program.size(); ++index)
    {
      REQUIRE(system.iopBus().write32(
        static_cast<IOPAddress>(4 * index), program[index]).status ==
        IOPBusStatus::Completed);
    }
    system.iopCore().startExecution(0xa0000000);
    system.runMasterCycles(32);
    REQUIRE(system.iopCore().retiredInstructions() == 4);
  }
}

TEST_CASE("SIF system composition shares both register views and signal routes",
  "[sif][system]")
{
  NekoSystem system;
  REQUIRE(system.eeBus().writeData32(EEMemoryMap::SIF_BASE, 0x12345678));
  REQUIRE(system.iopBus().read32(IOPBus::SIF_BASE).value == 0x12345678);
  REQUIRE(system.iopBus().write32(IOPBus::SIF_BASE + 0x10, 0x87654321).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.eeBus().read32(EEMemoryMap::SIF_BASE + 0x10) == 0x87654321);
  REQUIRE(system.eeBus().writeData32(EEMemoryMap::SIF_BASE + 0x40, 0x40100));
  REQUIRE(system.iopInterruptController().status() == 2);
  REQUIRE(system.iopBus().write32(IOPBus::SIF_DOORBELL, 2).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.interruptController().status() == 2);
  REQUIRE(system.masterClockScheduler().currentCycle() == 0);

  const auto state = system.saveState();
  NekoSystem restored;
  restored.loadState(state);
  REQUIRE(restored.saveState() == state);
  REQUIRE(restored.iopBus().read32(IOPBus::SIF_BASE).value == 0x12345678);
  REQUIRE(restored.eeBus().read32(EEMemoryMap::SIF_BASE + 0x10) == 0x87654321);
  REQUIRE(restored.interruptController().status() == 2);
  REQUIRE(restored.iopInterruptController().status() == 2);
}

TEST_CASE("SIF persistence canonicalizes allocation while retaining live packing state",
  "[sif][system][save-state]")
{
  NekoSystem original;
  NekoSystem reordered;
  auto &alternate = reordered.sif();
  REQUIRE(alternate.pushSIF1Quadword({{99, 98, 97, 96}}) ==
    SIFTransferOutcome::Completed);
  REQUIRE(alternate.consumeWords(SIFDirection::SIF1, 4) ==
    SIFTransferOutcome::Completed);
  seed(original);
  seed(reordered);
  REQUIRE(original.sifStateHash() == reordered.sifStateHash());
  const auto state = original.saveState();
  REQUIRE(state == reordered.saveState());
  REQUIRE_NOTHROW(inspectSaveState(state));
  NekoSystem restored;
  restored.loadState(state);
  REQUIRE(restored.sifStateHash() == original.sifStateHash());
  REQUIRE(restored.saveState() == state);
  REQUIRE(restored.sif().control() == 0x21b0);
  REQUIRE(restored.sif().iopDoorbell() == 0x80000002);
  REQUIRE(restored.sif().sif0PackingCursor() == 3);
  REQUIRE(restored.sif().sif0PackingLatch() == SIFQuadword{{5, 6, 7, 4}});
  REQUIRE(restored.sif().wordCount(SIFDirection::SIF0) == 4);
  REQUIRE(restored.sif().wordCount(SIFDirection::SIF1) == 5);
  REQUIRE(restored.sif().wordAt(SIFDirection::SIF0, 0) == 4);
  REQUIRE(restored.sif().wordAt(SIFDirection::SIF1, 0) == 11);
  for (auto *system : {&original, &restored})
  {
    auto &sif = system->sif();
    for (unsigned beat = 0; beat < 5; ++beat)
    {
      REQUIRE(sif.pushSIF1Quadword({{21, 22, 23, 24}}) ==
        SIFTransferOutcome::Completed);
    }
    REQUIRE(sif.pushSIF0PayloadWord(16, SIFPayloadBoundary::Final) ==
      SIFTransferOutcome::Completed);
    REQUIRE(sif.sif0PackingCursor() == 0);
    REQUIRE(sif.freeWordCount() == 2);
    const auto beforeWait = system->sifStateHash();
    REQUIRE(sif.pushSIF0PayloadWord(17, SIFPayloadBoundary::Final) ==
      SIFTransferOutcome::Wait);
    REQUIRE(system->sifStateHash() == beforeWait);
    REQUIRE(sif.consumeWords(SIFDirection::SIF1, 4) ==
      SIFTransferOutcome::Completed);
    REQUIRE(sif.pushSIF0PayloadWord(17, SIFPayloadBoundary::Final) ==
      SIFTransferOutcome::Completed);
    REQUIRE(sif.wordAt(SIFDirection::SIF0, 5) == 17);
    REQUIRE(sif.wordAt(SIFDirection::SIF0, 6) == 6);
    REQUIRE(sif.wordAt(SIFDirection::SIF0, 7) == 7);
    REQUIRE(sif.wordAt(SIFDirection::SIF0, 8) == 16);
  }
  REQUIRE(restored.sifStateHash() == original.sifStateHash());
  REQUIRE(restored.saveState() == original.saveState());
}

TEST_CASE("SIF persistence preserves full shared capacity and independent signal acknowledgements",
  "[sif][system][save-state]")
{
  NekoSystem original;
  seed(original);
  for (unsigned word = 0; word < 23; ++word)
  {
    REQUIRE(original.sif().pushSIF0PayloadWord(
      100 + word, SIFPayloadBoundary::Continuing) == SIFTransferOutcome::Completed);
  }
  REQUIRE(original.sif().freeWordCount() == 0);
  original.interruptController().acknowledge(2);
  original.iopInterruptController().writeStatus(~UINT32_C(2));
  const auto state = original.saveState();
  NekoSystem restored;
  restored.startTrace();
  restored.loadState(state);
  REQUIRE(restored.traceEnabled());
  REQUIRE(restored.trace().empty());
  REQUIRE(restored.saveState() == state);
  REQUIRE(restored.sif().freeWordCount() == 0);
  REQUIRE(restored.interruptController().status() == 0);
  REQUIRE(restored.iopInterruptController().status() == 0);
  restored.runMasterCycles(9);
  REQUIRE(restored.interruptController().status() == 0);
  REQUIRE(restored.iopInterruptController().status() == 0);
  REQUIRE(sifEvents(restored).empty());
  REQUIRE(restored.sifStateHash() == original.sifStateHash());

  REQUIRE(restored.sif().consumeWords(SIFDirection::SIF0, 4) ==
    SIFTransferOutcome::Completed);
  REQUIRE(restored.sif().pushSIF1Quadword({{90, 91, 92, 93}}) ==
    SIFTransferOutcome::Completed);
  REQUIRE(original.sif().wordCount(SIFDirection::SIF1) == 5);
  REQUIRE(restored.sif().wordCount(SIFDirection::SIF1) == 9);
  REQUIRE(restored.eeBus().writeData32(EEMemoryMap::SIF_BASE + 0x40, 0x40100));
  REQUIRE(restored.iopInterruptController().status() == 2);
  REQUIRE(original.iopInterruptController().status() == 0);
  REQUIRE(restored.iopBus().write32(IOPBus::SIF_DOORBELL, 2).status ==
    IOPBusStatus::Completed);
  REQUIRE(restored.interruptController().status() == 2);
  REQUIRE(original.interruptController().status() == 0);
}

TEST_CASE("SIF malformed persistence rejects atomically with exact diagnostic paths",
  "[sif][system][save-state]")
{
  NekoSystem system;
  seed(system);
  const auto before = system.saveState();
  const auto hash = system.sifStateHash();
  const std::vector<std::pair<std::string, std::uint64_t>> mutations = {
    {"system.sif.control", 1},
    {"system.sif.control", 0x40000},
    {"system.sif.control", 0x1000},
    {"system.sif.sif0.wordCount", 33},
    {"system.sif.sif1.wordCount", 29},
    {"system.sif.packingCursor", 4}
  };
  for (const auto &mutation : mutations)
  {
    CAPTURE(mutation.first);
    SaveStateMutation invalid(before);
    invalid.writeScalar(mutation.first, mutation.second);
    invalid.refreshChecksum();
    try
    {
      system.loadState(invalid);
      FAIL("Malformed SIF state was accepted");
    }
    catch (const std::invalid_argument &error)
    {
      REQUIRE(std::string(error.what()).find(mutation.first) != std::string::npos);
    }
    REQUIRE(system.saveState() == before);
    REQUIRE(system.sifStateHash() == hash);
    REQUIRE_THROWS_AS(inspectSaveState(invalid), std::invalid_argument);
  }
  SaveStateMutation oldVersion(before);
  oldVersion.writeScalar("container.version", 34);
  REQUIRE_THROWS_AS(system.loadState(oldVersion), std::invalid_argument);
  REQUIRE(system.saveState() == before);
}

TEST_CASE("SIF reset clears transport and rewires both views and interrupt routes",
  "[sif][system]")
{
  NekoSystem system;
  const auto initialHash = system.sifStateHash();
  seed(system);
  system.startTrace();
  system.reset();
  REQUIRE(system.sifStateHash() == initialHash);
  REQUIRE(system.sif().freeWordCount() == SIF::WORD_CAPACITY);
  REQUIRE(system.sif().sif0PackingLatch() == SIFQuadword{});
  REQUIRE(system.sif().sif0PackingCursor() == 0);
  REQUIRE_FALSE(system.traceEnabled());
  REQUIRE(system.trace().empty());
  REQUIRE(system.interruptController().status() == 0);
  REQUIRE(system.iopInterruptController().status() == 0);
  seed(system);
  REQUIRE(system.iopBus().read32(IOPBus::SIF_BASE).value == 0xfedcba98);
  REQUIRE(system.eeBus().read32(EEMemoryMap::SIF_BASE + 0x10) == 0x87654321);
  REQUIRE(system.interruptController().status() == 2);
  REQUIRE(system.iopInterruptController().status() == 2);
}

TEST_CASE("SIF traces append stable register and signal identities in mutation order",
  "[sif][system][trace]")
{
  REQUIRE(static_cast<std::uint8_t>(NekoTraceSubsystem::SIF) == 14);
  REQUIRE(static_cast<std::uint8_t>(NekoTraceEventType::RegisterWrite) == 18);
  REQUIRE(static_cast<std::uint8_t>(NekoTraceEventType::SignalRequested) == 19);
  NekoSystem system;
  iopDoorbellProgram(system);
  eeStore(system, 0x40100);
  system.startTrace();
  system.clockMasterCycle();
  const auto events = sifEvents(system);
  REQUIRE(events.size() == 4);
  const std::array<NekoTraceEventType, 4> types = {{
    NekoTraceEventType::RegisterWrite, NekoTraceEventType::SignalRequested,
    NekoTraceEventType::RegisterWrite, NekoTraceEventType::SignalRequested}};
  for (std::size_t index = 0; index < events.size(); ++index)
  {
    REQUIRE(events[index].masterCycle == 33);
    REQUIRE(events[index].type == types[index]);
  }
  REQUIRE(events[0].value0 == 4);
  REQUIRE(events[0].value1 == 0x40100);
  REQUIRE(events[0].value2 == 0);
  REQUIRE(events[0].value3 == 0x100);
  REQUIRE(events[1].value0 == 1);
  REQUIRE(events[1].value1 == 4);
  REQUIRE(events[1].value2 == 0x40100);
  REQUIRE(events[1].value3 == 0);
  REQUIRE(events[2].value0 == 0x105);
  REQUIRE(events[2].value1 == 2);
  REQUIRE(events[2].value2 == 0);
  REQUIRE(events[2].value3 == 2);
  REQUIRE(events[3].value0 == 2);
  REQUIRE(events[3].value1 == 0x105);
  REQUIRE(events[3].value2 == 2);
  REQUIRE(events[3].value3 == 0);
  std::size_t signalIndex = 0;
  std::size_t interruptIndex = 0;
  std::size_t iopStoreIndex = 0;
  for (std::size_t index = 0; index < system.trace().size(); ++index)
  {
    const auto &event = system.trace()[index];
    if (event.subsystem == NekoTraceSubsystem::SIF &&
        event.type == NekoTraceEventType::SignalRequested && event.value0 == 1)
    {
      signalIndex = index;
    }
    if (event.subsystem == NekoTraceSubsystem::IOPInterruptController)
    {
      interruptIndex = index;
    }
    if (event.subsystem == NekoTraceSubsystem::IOP &&
        event.type == NekoTraceEventType::MemoryAccess && event.value1 == 2)
    {
      iopStoreIndex = index;
    }
  }
  REQUIRE(signalIndex < interruptIndex);
  REQUIRE(interruptIndex < iopStoreIndex);
  system.iopCore().haltExecution();
  eeStore(system, 0x40100);
  system.clearTrace();
  system.clockMasterCycle();
  const auto repeated = sifEvents(system);
  REQUIRE(repeated.size() == 2);
  REQUIRE(repeated[0].value2 == 0x100);
  REQUIRE(repeated[0].value3 == 0x100);
  REQUIRE(repeated[1].value0 == 1);
}

TEST_CASE("SIF trace rejection polling and lifecycle remain observational",
  "[sif][system][trace]")
{
  NekoSystem plain;
  NekoSystem traced;
  for (auto *system : {&plain, &traced})
  {
    seed(*system);
    eeStore(*system, 0xc0100);
  }
  traced.startTrace();
  plain.clockMasterCycle();
  traced.clockMasterCycle();
  REQUIRE(plain.saveState() == traced.saveState());
  REQUIRE(plain.sifStateHash() == traced.sifStateHash());
  const auto rejected = sifEvents(traced);
  REQUIRE(rejected.size() == 1);
  REQUIRE(rejected[0].type == NekoTraceEventType::RegisterWrite);
  REQUIRE(rejected[0].value0 == 0x10004);
  REQUIRE(rejected[0].value1 == 0xc0100);
  REQUIRE(rejected[0].value2 == 0x21b0);
  REQUIRE(rejected[0].value3 == 0x21b0);
  const auto traceHash = traced.traceHash();
  const auto saved = traced.saveState();
  const auto sifHash = traced.sifStateHash();
  traced.loadState(saved);
  REQUIRE(traced.traceHash() == traceHash);
  REQUIRE(traced.traceEnabled());
  REQUIRE(traced.sifStateHash() == sifHash);
  REQUIRE(traced.saveState() == saved);
  traced.clearTrace();
  REQUIRE(traced.traceEnabled());
  // Host inspection/setup is outside the scheduled observation boundary.
  REQUIRE(traced.eeBus().read32(EEMemoryMap::SIF_BASE + 0x40) == 0xf00021b2);
  REQUIRE(traced.iopBus().read32(IOPBus::SIF_DOORBELL).value == 0x80000002);
  REQUIRE(traced.eeBus().writeData32(EEMemoryMap::SIF_BASE + 0x40, 0x40100));
  REQUIRE(traced.trace().empty());
  traced.eeBus().write32(0, 0x8c220000);
  traced.eeCore().startExecution(0xa0000000);
  traced.clockMasterCycle();
  REQUIRE(sifEvents(traced).empty());
  traced.stopTrace();
  eeStore(traced, 0x40100);
  traced.clockMasterCycle();
  REQUIRE(sifEvents(traced).empty());
  traced.startTrace();
  eeStore(traced, 0x40100);
  traced.clockMasterCycle();
  REQUIRE(sifEvents(traced).size() == 2);
}
