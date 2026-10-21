#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "catch.hpp"
#include "iop/iop_trace_test_utils.hpp"
#include "save_state_mutation.hpp"

namespace
{
  constexpr std::uint32_t START = DMACChannelControl::START;
  constexpr std::uint32_t CHAIN = DMACChannelControl::CHAIN_MODE;
  constexpr std::uint32_t TIE = DMACChannelControl::TAG_INTERRUPT_ENABLE;
  constexpr std::uint32_t IRQ = UINT32_C(0x80000000);
  constexpr SIFQuadword PAYLOAD = {{0x12345678, 0xabcdef01, 3, 4}};

  void push(NekoSystem &system, const SIFQuadword &words)
  {
    REQUIRE(system.sif().pushSIF0Prologue(words) == SIFTransferOutcome::Completed);
  }

  void start(NekoSystem &system, std::uint32_t count = 0,
    std::uint32_t address = 0x4000, std::uint32_t options = 0)
  {
    system.sif0DMAC().writeMemoryAddress(address);
    system.sif0DMAC().writeQuadwordCount(count);
    system.sif0DMAC().writeChannelControl(START | CHAIN | options);
    system.dmacController().writeControl(1);
  }

  SIF0DMABeatOutcome grant(NekoSystem &system)
  {
    return system.dmacController().clockSIF(system.sif0DMAC());
  }

  void requireUnchanged(const NekoSystem &system,
    const std::vector<std::uint8_t> &before)
  {
    const bool equal = system.saveState() == before;
    REQUIRE(equal);
  }

  void requireQuadword(NekoSystem &system, std::uint32_t address,
    const SIFQuadword &expected)
  {
    EEQuadword value = {};
    if ((address & IRQ) != 0)
      REQUIRE(system.eeMemorySystem().readScratchpadDMA128(address & 0x3fff, &value) ==
        EEScratchpadAccessResult::Completed);
    else
      REQUIRE(system.eeBus().readDMAC128(address, &value));
    REQUIRE(static_cast<std::uint32_t>(value.low) == expected[0]);
    REQUIRE(static_cast<std::uint32_t>(value.low >> 32) == expected[1]);
    REQUIRE(static_cast<std::uint32_t>(value.high) == expected[2]);
    REQUIRE(static_cast<std::uint32_t>(value.high >> 32) == expected[3]);
  }

  void prefix(NekoSystem &system, const SIFQuadword &words, unsigned count)
  {
    if (count == 0) return;
    SIFQuadword padded = {};
    for (unsigned index = 0; index < count; ++index)
      padded[4 - count + index] = words[index];
    push(system, padded);
    REQUIRE(system.sif().consumeWords(SIFDirection::SIF0, 4 - count) ==
      SIFTransferOutcome::Completed);
  }
}

TEST_CASE("SIF0 destination tags consume full headers without storing tag lanes",
  "[sif0][dma][destination]")
{
  NekoSystem system;
  push(system, {{0x10000001, 0x300f, 0xdeadbeef, 0xcafebabe}});
  push(system, PAYLOAD);
  push(system, {{0x70000001, 0x5000, 0, 0}});
  push(system, {{5, 6, 7, 8}});
  start(system, 0, 0x6000);
  REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
  REQUIRE(system.sif0DMAC().memoryAddress() == 0x3000);
  REQUIRE(system.sif0DMAC().quadwordCount() == 1);
  REQUIRE(system.sif0DMAC().channelControl() == (START | CHAIN | 0x10000000));
  requireQuadword(system, 0x6000, {});
  requireQuadword(system, 0x3000, {});
  REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
  requireQuadword(system, 0x3000, PAYLOAD);
  REQUIRE(system.sif0DMAC().active());
  REQUIRE(system.sif0DMAC().quadwordCount() == 0);
  REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
  REQUIRE(system.sif0DMAC().memoryAddress() == 0x5000);
  REQUIRE(system.sif0DMAC().active());
  REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
  requireQuadword(system, 0x5000, {{5, 6, 7, 8}});
  REQUIRE_FALSE(system.sif0DMAC().active());
  REQUIRE(system.dmacController().status() == DMACStatus::CHANNEL_5);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 0);
}

TEST_CASE("SIF0 destination chain transfers preloaded QWC before reading any tag",
  "[sif0][dma][destination]")
{
  NekoSystem system;
  push(system, PAYLOAD);
  push(system, {{0x70000000, 0x7000, 0, 0}});
  start(system, 1, 0x4000);
  system.dmacController().writeControl(1 | (1u << 4));
  system.dmacController().writeStallAddress(0x8000);
  REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
  requireQuadword(system, 0x4000, PAYLOAD);
  REQUIRE(system.dmacController().stallAddress() == 0x8000);
  REQUIRE(system.sif0DMAC().active());
  REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
  REQUIRE_FALSE(system.sif0DMAC().active());
  REQUIRE(system.sif0DMAC().memoryAddress() == 0x7000);
}

TEST_CASE("SIF0 zero count destination tags make one bounded transition per grant",
  "[sif0][dma][destination]")
{
  for (const unsigned id : {0u, 1u, 7u})
  {
    CAPTURE(id);
    NekoSystem system;
    push(system, {{id << 28, 0x1fc00000, 0, 0}});
    push(system, {{0x70000000, 0x3000, 0, 0}});
    start(system);
    REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
    REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 4);
    REQUIRE(system.sif0DMAC().active() == (id != 7));
    REQUIRE(system.sif0DMAC().memoryAddress() == 0x1fc00000);
    REQUIRE((system.dmacController().status() & DMACStatus::BUS_ERROR) == 0);
  }
}

TEST_CASE("SIF0 destination tag IRQ waits for payload and depends on TIE",
  "[sif0][dma][destination]")
{
  for (const unsigned count : {0u, 1u})
    for (const auto tie : {0u, TIE})
    {
      NekoSystem system;
      push(system, {{IRQ | 0x10000000 | count, 0x3000, 0, 0}});
      if (count != 0) push(system, PAYLOAD);
      start(system, 0, 0x4000, tie);
      REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
      REQUIRE(system.sif0DMAC().active() == (tie == 0 || count != 0));
      if (count != 0)
      {
        REQUIRE(system.dmacController().status() == 0);
        REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
        requireQuadword(system, 0x3000, PAYLOAD);
      }
      REQUIRE(system.sif0DMAC().active() == (tie == 0));
      REQUIRE(system.dmacController().status() == (tie == 0 ? 0 : DMACStatus::CHANNEL_5));
    }
}

TEST_CASE("SIF0 undefined destination IDs complete without transferring payload",
  "[sif0][dma][destination]")
{
  for (const unsigned id : {2u, 3u, 4u, 5u, 6u})
  {
    NekoSystem system;
    push(system, {{(id << 28) | 3, 0x3000, 0, 0}});
    push(system, PAYLOAD);
    start(system);
    REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
    REQUIRE_FALSE(system.sif0DMAC().active());
    REQUIRE(system.sif0DMAC().channelControl() == (CHAIN | (id << 28)));
    REQUIRE(system.dmacController().status() == DMACStatus::CHANNEL_5);
    REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 4);
    requireQuadword(system, 0x3000, {});
  }
}

TEST_CASE("SIF0 destination tag priority commits exactly with accepted headers",
  "[sif0][dma][destination]")
{
  for (const unsigned pce : {0u, 1u, 2u, 3u})
    for (const auto initial : {0u, DMACPriority::ENABLE})
    {
      NekoSystem system;
      const auto low = 0x70000001u | (pce << 26);
      push(system, {{low, 0x3000, 0, 0}});
      push(system, PAYLOAD);
      start(system);
      const auto fields = 1u << 21;
      system.dmacController().writePriorityControl(initial | fields);
      const auto before = system.saveState();
      if (pce == 1)
      {
        REQUIRE_THROWS_AS(grant(system), std::invalid_argument);
        requireUnchanged(system, before);
      }
      else
      {
        REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
        REQUIRE(system.dmacController().priorityControl() ==
          (fields | (pce == 0 ? initial : pce == 3 ? DMACPriority::ENABLE : 0)));
        REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
        REQUIRE_FALSE(system.sif0DMAC().active());
      }
    }
}

TEST_CASE("SIF0 CNTS publishes only accepted payload progress through common stall control",
  "[sif0][dma][destination]")
{
  for (const unsigned id : {0u, 1u, 7u})
  {
    NekoSystem system;
    push(system, {{(id << 28) | 2, 0x3000, 0, 0}});
    push(system, PAYLOAD);
    start(system);
    system.dmacController().writeControl(1 | (1u << 4));
    system.dmacController().writeStallAddress(0x6000);
    REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
    REQUIRE(system.dmacController().stallAddress() == 0x6000);
    REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
    REQUIRE(system.dmacController().stallAddress() == (id == 0 ? 0x3010 : 0x6000));
    const auto before = system.saveState();
    REQUIRE(grant(system) == SIF0DMABeatOutcome::Wait);
    requireUnchanged(system, before);
    push(system, PAYLOAD);
    REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
    REQUIRE(system.dmacController().stallAddress() == (id == 0 ? 0x3020 : 0x6000));
  }
}

TEST_CASE("SIF0 destination headers and payloads wait for all four words",
  "[sif0][dma][destination][save-state]")
{
  const SIFQuadword tag = {{0x70000001, 0x3000, 0, 0}};
  for (const bool header : {false, true})
    for (unsigned count = 0; count < 4; ++count)
    {
      NekoSystem system;
      start(system);
      if (!header)
      {
        push(system, tag);
        REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
      }
      const auto &words = header ? tag : PAYLOAD;
      prefix(system, words, count);
      const auto before = system.saveState();
      REQUIRE(grant(system) == SIF0DMABeatOutcome::Wait);
      requireUnchanged(system, before);
      NekoSystem restored;
      restored.loadState(before);
      for (unsigned index = count; index < 4; ++index)
      {
        REQUIRE(system.sif().pushSIF0PayloadWord(words[index], SIFPayloadBoundary::Continuing) ==
          SIFTransferOutcome::Completed);
        REQUIRE(restored.sif().pushSIF0PayloadWord(words[index], SIFPayloadBoundary::Continuing) ==
          SIFTransferOutcome::Completed);
      }
      REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
      REQUIRE(grant(restored) == SIF0DMABeatOutcome::Completed);
      IOPTraceTest::requireMachine(restored, system);
      if (header) REQUIRE(system.sif0DMAC().quadwordCount() == 1);
      else requireQuadword(system, 0x3000, PAYLOAD);
    }
}

TEST_CASE("SIF0 destination SPR payloads wrap the bank without losing the selector",
  "[sif0][dma][destination][save-state]")
{
  NekoSystem system;
  push(system, {{0x70000002, 0xfffffff0, 0, 0}});
  push(system, PAYLOAD);
  push(system, {{5, 6, 7, 8}});
  start(system, 0, 0x4000, DMACChannelControl::TAG_TRANSFER_ENABLE);
  REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
  REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
  REQUIRE(system.sif0DMAC().memoryAddress() == 0x80000000);
  requireQuadword(system, 0x80003ff0, PAYLOAD);
  NekoSystem restored;
  restored.loadState(system.saveState());
  REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
  REQUIRE(grant(restored) == SIF0DMABeatOutcome::Completed);
  IOPTraceTest::requireMachine(restored, system);
  requireQuadword(restored, 0x80000000, {{5, 6, 7, 8}});
  REQUIRE_FALSE(restored.sif0DMAC().active());
}

TEST_CASE("SIF0 destination bus errors retain payload and do not publish CNTS progress",
  "[sif0][dma][destination]")
{
  NekoSystem system;
  push(system, {{1, 0x1fc00000, 0, 0}});
  push(system, PAYLOAD);
  start(system);
  system.dmacController().writeControl(1 | (1u << 4));
  system.dmacController().writeStallAddress(0x6000);
  REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
  REQUIRE(grant(system) == SIF0DMABeatOutcome::BusError);
  REQUIRE(system.sif0DMAC().quadwordCount() == 1);
  REQUIRE(system.sif0DMAC().memoryAddress() == 0x1fc00000);
  REQUIRE(system.dmacController().stallAddress() == 0x6000);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 4);
  REQUIRE(system.dmacController().status() ==
    (DMACStatus::CHANNEL_5 | DMACStatus::BUS_ERROR));
}

TEST_CASE("SIF0 residual words survive termination and short producers leave a wait",
  "[sif0][dma][destination]")
{
  NekoSystem system;
  push(system, {{0x70000000, 0x3000, 0, 0}});
  push(system, {{0x70000002, 0x4000, 0, 0}});
  push(system, PAYLOAD);
  start(system);
  REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
  REQUIRE_FALSE(system.sif0DMAC().active());
  REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 8);
  start(system);
  REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
  REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
  const auto before = system.saveState();
  REQUIRE(grant(system) == SIF0DMABeatOutcome::Wait);
  requireUnchanged(system, before);
  REQUIRE(system.sif0DMAC().quadwordCount() == 1);
  REQUIRE(system.sif0DMAC().active());
  requireQuadword(system, 0x4000, PAYLOAD);
}

TEST_CASE("SIF0 checked destination chains consume partial source packets under backpressure",
  "[sif0][dma][destination]")
{
  for (const unsigned count : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 65u})
  {
    CAPTURE(count);
    NekoSystem system;
    const auto quadwords = (count + 3) / 4;
    const SIFQuadword tag = {{0x70000000u | quadwords, 0x3000, 0xaaaabbbb, 0xccccdddd}};
    REQUIRE(system.iopBus().write32(0x1000, 0x80002000).status == IOPBusStatus::Completed);
    REQUIRE(system.iopBus().write32(0x1004, count).status == IOPBusStatus::Completed);
    for (unsigned lane = 0; lane < 4; ++lane)
      REQUIRE(system.iopBus().write32(0x1008 + lane * 4, tag[lane]).status ==
        IOPBusStatus::Completed);
    for (unsigned word = 0; word < count; ++word)
      REQUIRE(system.iopBus().write32(0x2000 + word * 4, 0x100 + word).status ==
        IOPBusStatus::Completed);
    REQUIRE(system.iopSIF0DMAC().writeTagAddress(0x1000) == IOPDMAWriteOutcome::Completed);
    REQUIRE(system.iopSIF0DMAC().writeChannelControl(0x01000401) ==
      IOPDMAWriteOutcome::Completed);
    system.iopDMAController().writePriority(IOPDMABank::Secondary, 0x800);
    REQUIRE(system.sif().pushSIF1Quadword({{9, 10, 11, 12}}) ==
      SIFTransferOutcome::Completed);
    start(system);
    bool waited = false;
    for (unsigned beat = 0;
         beat < 2048 && (system.iopSIF0DMAC().active() || system.sif0DMAC().active());
         ++beat)
    {
      if (system.iopSIF0DMAC().active())
      {
        const auto result = system.iopDMAController().clockSIF(
          system.iopSIF0DMAC(), system.iopBus(), system.sif());
        REQUIRE(result.outcome != IOPDMABeatOutcome::Rejected);
        waited |= result.outcome == IOPDMABeatOutcome::Wait;
      }
      if (beat % 8 == 0 && system.sif0DMAC().active())
        REQUIRE(grant(system) != SIF0DMABeatOutcome::BusError);
      REQUIRE(system.sif().wordCount(SIFDirection::SIF0) +
        system.sif().wordCount(SIFDirection::SIF1) <= SIF::WORD_CAPACITY);
    }
    REQUIRE_FALSE(system.iopSIF0DMAC().active());
    REQUIRE_FALSE(system.sif0DMAC().active());
    REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 0);
    REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 4);
    if (count == 65) REQUIRE(waited);
    auto expected = tag;
    for (unsigned index = 0; index < quadwords; ++index)
    {
      for (unsigned lane = 0; lane < 4 && index * 4 + lane < count; ++lane)
        expected[lane] = 0x100 + index * 4 + lane;
      requireQuadword(system, 0x3000 + index * 16, expected);
    }
    REQUIRE(system.dmacController().status() == DMACStatus::CHANNEL_5);
    REQUIRE(system.iopDMAController().interruptStatus(IOPDMABank::Secondary) ==
      0x04000000);
  }
}

TEST_CASE("SIF0 destination priority gates headers and payloads without consuming words",
  "[sif0][dma][destination]")
{
  for (const bool payload : {false, true})
  {
    NekoSystem system;
    push(system, {{0x70000001, 0x3000, 0, 0}});
    push(system, PAYLOAD);
    start(system);
    if (payload) REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
    for (const bool dmaEnable : {false, true})
    {
      system.dmacController().writeControl(dmaEnable ? 1 : 0);
      system.dmacController().writePriorityControl(dmaEnable ? DMACPriority::ENABLE : 0);
      const auto before = system.saveState();
      REQUIRE(grant(system) == SIF0DMABeatOutcome::Wait);
      requireUnchanged(system, before);
    }
    system.dmacController().writePriorityControl(DMACPriority::ENABLE | (1u << 21));
    REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
  }
}

TEST_CASE("SIF0 CNTS publication resumes a stalled common drain without clearing SIS",
  "[sif0][dma][destination][stall]")
{
  NekoSystem system;
  push(system, {{1, 0x3000, 0, 0}});
  push(system, PAYLOAD);
  start(system);
  system.vif1DMAC().writeMemoryAddress(0x3000);
  system.vif1DMAC().writeQuadwordCount(1);
  system.vif1DMAC().writeChannelControl(START | DMACChannelControl::FROM_MEMORY);
  system.dmacController().writeControl(1 | (1u << 4) | (1u << 6));
  system.dmacController().writeStallAddress(0x3000);
  system.vif1DMAC().clock();
  REQUIRE(system.vif1DMAC().quadwordCount() == 1);
  REQUIRE((system.dmacController().status() & DMACStatus::STALL) != 0);
  REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
  REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
  system.vif1DMAC().clock();
  REQUIRE(system.vif1DMAC().quadwordCount() == 0);
  REQUIRE((system.dmacController().status() & DMACStatus::STALL) != 0);
}

TEST_CASE("SIF0 destination continuations restore terminal and CNTS payloads exactly",
  "[sif0][dma][destination][save-state]")
{
  for (const unsigned id : {0u, 1u, 7u})
    for (const auto tie : {0u, TIE})
    {
      NekoSystem system;
      push(system, {{(id << 28) | IRQ | 2, 0x3000, 0, 0}});
      push(system, PAYLOAD);
      push(system, PAYLOAD);
      start(system, 0, 0x4000, tie);
      system.dmacController().writeControl(1 | (1u << 4));
      REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
      REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
      const auto before = system.saveState();
      system.sif0DMAC().writeChannelControl(system.sif0DMAC().channelControl());
      requireUnchanged(system, before);
      NekoSystem restored;
      restored.loadState(before);
      IOPTraceTest::requireMachine(restored, system);
      REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
      REQUIRE(grant(restored) == SIF0DMABeatOutcome::Completed);
      IOPTraceTest::requireMachine(restored, system);
      REQUIRE(system.sif0DMAC().active() == (id != 7 && tie == 0));
      REQUIRE(system.dmacController().stallAddress() == (id == 0 ? 0x3020 : 0));
    }
}

TEST_CASE("SIF0 abort and rearm discard destination continuation without flushing payload",
  "[sif0][dma][destination][save-state]")
{
  NekoSystem system;
  push(system, {{IRQ | 2, 0x3000, 0, 0}});
  push(system, PAYLOAD);
  push(system, PAYLOAD);
  start(system, 0, 0x4000, TIE);
  system.dmacController().writeControl(1 | (1u << 4));
  REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
  REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
  const auto control = system.sif0DMAC().channelControl();
  system.sif0DMAC().writeChannelControl(control & ~START);
  REQUIRE(system.dmacController().status() == 0);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF0) == 4);
  system.sif0DMAC().writeChannelControl(control);
  NekoSystem restored;
  restored.loadState(system.saveState());
  REQUIRE(grant(system) == SIF0DMABeatOutcome::Completed);
  REQUIRE(grant(restored) == SIF0DMABeatOutcome::Completed);
  IOPTraceTest::requireMachine(restored, system);
  REQUIRE(system.sif0DMAC().active());
  REQUIRE(system.dmacController().stallAddress() == 0x3010);
  REQUIRE(system.dmacController().status() == 0);
  requireQuadword(system, 0x3010, PAYLOAD);
}

TEST_CASE("SIF0 malformed destination continuations reject before live restore",
  "[sif0][dma][destination][save-state]")
{
  NekoSystem source;
  push(source, {{0x70000002, 0x3000, 0, 0}});
  start(source);
  REQUIRE(grant(source) == SIF0DMABeatOutcome::Completed);
  NekoSystem destination;
  destination.runMasterCycles(9);
  destination.startTrace();
  const auto before = destination.saveState();
  const auto traceHash = destination.traceHash();
  struct InvalidField { const char *name; std::uint32_t value; };
  for (const auto field : {
    InvalidField{"destinationPacketAccepted", 0},
    InvalidField{"terminateAfterPacket", 0},
    InvalidField{"quadwordCountRegister", 0},
    InvalidField{"channelControlRegister", CHAIN | 0x70000000},
    InvalidField{"channelControlRegister", START | 0x70000000},
    InvalidField{"channelControlRegister", START | CHAIN | 0x20000000},
    InvalidField{"channelControlRegister", START | CHAIN | 0x74000000}})
  {
    CAPTURE(field.name);
    CAPTURE(field.value);
    SaveStateMutation mutation(source.saveState());
    mutation.writeScalar(std::string("system.sif0DMAC.") + field.name, field.value);
    mutation.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(mutation.bytes()), std::invalid_argument);
    requireUnchanged(destination, before);
    REQUIRE(destination.traceEnabled());
    REQUIRE(destination.traceHash() == traceHash);
  }
  SaveStateMutation forged(before);
  forged.writeScalar("system.sif0DMAC.destinationPacketAccepted", 1);
  forged.refreshChecksum();
  REQUIRE_THROWS_AS(destination.loadState(forged.bytes()), std::invalid_argument);
  requireUnchanged(destination, before);
}
