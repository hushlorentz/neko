#include <cstdint>
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
