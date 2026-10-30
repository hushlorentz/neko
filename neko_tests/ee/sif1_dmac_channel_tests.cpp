#include <cstdint>
#include <stdexcept>
#include <vector>

#include "catch.hpp"
#include "dmac_channel_state.hpp"
#include "iop/iop_trace_test_utils.hpp"
#include "save_state_mutation.hpp"
#include "sif1_dmac_channel.hpp"

namespace
{
  constexpr std::uint32_t CHCR = 0x1000c400;
  constexpr std::uint32_t MADR = 0x1000c410;
  constexpr std::uint32_t QWC = 0x1000c420;
  constexpr std::uint32_t TADR = 0x1000c430;
  constexpr std::uint32_t START = 0x104;
  constexpr std::uint32_t CHANNEL_6 = 1u << 6;
  constexpr std::uint32_t CHANNEL_6_MASK = 1u << 22;

  void requireState(const NekoSystem &system, const std::vector<std::uint8_t> &state)
  {
    const bool identical = system.saveState() == state;
    REQUIRE(identical);
  }

  DMACChannelState sifState()
  {
    return DMACChannelState("SIF1", DMACChannelDirectionPolicy::Unrestricted,
      DMACChannelProfile::SIF1);
  }

  void writeSource(NekoSystem &system, std::uint32_t address,
    const SIFQuadword &words)
  {
    const EEQuadword value = {
      static_cast<std::uint64_t>(words[0]) |
        (static_cast<std::uint64_t>(words[1]) << 32),
      static_cast<std::uint64_t>(words[2]) |
        (static_cast<std::uint64_t>(words[3]) << 32)};
    if ((address & 0x80000000) != 0)
    {
      REQUIRE(system.eeMemorySystem().writeScratchpadDMA128(
        address & 0x3ff0, value) == EEScratchpadAccessResult::Completed);
    }
    else
    {
      REQUIRE(system.eeBus().writeDMAC128(address, value));
    }
  }

  void requireStream(const NekoSystem &system,
    const std::vector<std::uint32_t> &expected)
  {
    REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
      CAPTURE(index);
      REQUIRE(system.sif().wordAt(SIFDirection::SIF1, index) == expected[index]);
    }
  }
}

TEST_CASE("EE SIF1 registers retain aligned physical and scratchpad state",
  "[ee][sif1][dma]")
{
  NekoSystem system;
  auto &channel = system.sif1DMAC();
  REQUIRE(channel.channelControl() == 0);
  REQUIRE(channel.memoryAddress() == 0);
  REQUIRE(channel.quadwordCount() == 0);
  REQUIRE(channel.tagAddress() == 0);
  REQUIRE_FALSE(channel.active());
  REQUIRE_FALSE(channel.transferEnabled());
  channel.writeMemoryAddress(0x8000123f);
  channel.writeTagAddress(0x8000234f);
  channel.writeQuadwordCount(0xabcd1234);
  REQUIRE(channel.memoryAddress() == 0x80001230);
  REQUIRE(channel.tagAddress() == 0x80002340);
  REQUIRE(channel.quadwordCount() == 0x1234);
  channel.writeChannelControl(0xabcd0030);
  REQUIRE(channel.channelControl() == 0xabcd0030);
  channel.writeChannelControl(START | 0x31);
  REQUIRE(channel.active());
  REQUIRE_FALSE(channel.transferEnabled());
  REQUIRE(system.sif().control() == 0x4000);
  system.dmacController().writeControl(DMACControl::DMA_ENABLE);
  REQUIRE(channel.transferEnabled());
  const auto before = system.saveState();
  REQUIRE_THROWS_AS(channel.writeMemoryAddress(0), std::logic_error);
  REQUIRE_THROWS_AS(channel.writeQuadwordCount(0), std::logic_error);
  REQUIRE_THROWS_AS(channel.writeTagAddress(0), std::logic_error);
  REQUIRE_THROWS_AS(channel.writeChannelControl(START), std::logic_error);
  channel.writeChannelControl(START | 0x31);
  requireState(system, before);
  system.dmacController().writeControl(0);
  system.runMasterCycles(17);
  REQUIRE(channel.active());
  REQUIRE(channel.memoryAddress() == 0x80001230);
  REQUIRE(channel.quadwordCount() == 0x1234);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 0);
  REQUIRE(system.dmacController().status() == 0);
}

TEST_CASE("EE SIF1 TTE and unsupported start rejection precedes all mutation",
  "[ee][sif1][dma]")
{
  NekoSystem system;
  auto &channel = system.sif1DMAC();
  channel.writeMemoryAddress(0x1230);
  channel.writeQuadwordCount(2);
  channel.writeTagAddress(0x2340);
  channel.writeChannelControl(0x44);
  REQUIRE(channel.channelControl() == 0x44);
  for (const auto control : {0x140u, 0x144u, 0x108u, 0x10cu, 0x106u, 0x204u})
  {
    CAPTURE(control);
    const auto before = system.saveState();
    REQUIRE_THROWS_AS(channel.writeChannelControl(control), std::invalid_argument);
    requireState(system, before);
    REQUIRE_FALSE(system.eeBus().writeData32(CHCR, control));
    requireState(system, before);
  }
  channel.writeChannelControl(START);
  const auto active = system.saveState();
  REQUIRE_THROWS_AS(channel.writeChannelControl(START | 0x40), std::invalid_argument);
  requireState(system, active);
}

TEST_CASE("EE SIF1 CTRL events preserve independent canonical SIF state",
  "[ee][sif1][dma]")
{
  NekoSystem system;
  auto &sif = system.sif();
  REQUIRE(sif.pushSIF0Prologue({{1, 2, 3, 4}}) == SIFTransferOutcome::Completed);
  REQUIRE(sif.pushSIF0PayloadWord(5, SIFPayloadBoundary::Continuing) ==
    SIFTransferOutcome::Completed);
  REQUIRE(sif.pushSIF1Quadword({{6, 7, 8, 9}}) == SIFTransferOutcome::Completed);
  sif.writeEE(SIFRegister::MSCOM, 0x1234);
  sif.writeIOP(SIFRegister::SMCOM, 0x5678);
  sif.writeEE(SIFRegister::MSFLAG, 0x80000000);
  sif.writeIOP(SIFRegister::SMFLAG, 0x40000000);
  sif.writeEE(SIFRegister::CTRL, 0x100);
  sif.writeIOP(SIFRegister::CTRL, 0xf0);
  const auto initial = system.saveState();
  REQUIRE(sif.control() == 0x21f0);
  system.sif1DMAC().writeChannelControl(START);
  REQUIRE(sif.control() == 0x61f0);
  sif.writeIOP(SIFRegister::CTRL, 0x20);
  REQUIRE(sif.control() == 0x21d0);
  const auto unchanged = system.saveState();
  system.sif1DMAC().writeChannelControl(START);
  requireState(system, unchanged);
  system.dmacController().writeControl(1);
  system.dmacController().writeControl(0);
  REQUIRE(sif.control() == 0x21d0);
  system.sif1DMAC().writeChannelControl(4);
  REQUIRE(sif.control() == 0x2190);
  REQUIRE(system.dmacController().status() == 0);
  REQUIRE(system.iopInterruptController().status() == 0);
  REQUIRE(system.interruptController().status() == 0);
  REQUIRE(sif.wordCount(SIFDirection::SIF0) == 5);
  REQUIRE(sif.wordCount(SIFDirection::SIF1) == 4);
  REQUIRE(sif.sif0PackingCursor() == 1);
  REQUIRE(sif.sif0PackingLatch()[0] == 5);
  REQUIRE(sif.readEE(SIFRegister::MSCOM) == 0x1234);
  REQUIRE(sif.readIOP(SIFRegister::SMCOM) == 0x5678);
  REQUIRE(sif.readEE(SIFRegister::MSFLAG) == 0x80000000);
  REQUIRE(sif.readIOP(SIFRegister::SMFLAG) == 0x40000000);
  system.loadState(initial);
  system.sif1DMAC().writeChannelControl(4);
  REQUIRE(system.sif().control() == 0x21f0);
  for (const auto event : {
    SIFEEDMAEvent::Complete, SIFEEDMAEvent::BusError, SIFEEDMAEvent::Abort})
  {
    system.loadState(initial);
    system.sif().applyEEDMAEvent(SIFDirection::SIF1, SIFEEDMAEvent::Start);
    REQUIRE(system.sif().control() == 0x61f0);
    system.sif().applyEEDMAEvent(SIFDirection::SIF1, event);
    REQUIRE(system.sif().control() == 0x21b0);
  }
}

TEST_CASE("EE SIF1 uses shared source transitions without acquiring address stacks",
  "[ee][sif1][dma]")
{
  for (const auto id : {0u, 1u, 2u, 3u, 4u, 7u})
  {
    CAPTURE(id);
    auto state = sifState();
    state.writeMemoryAddress(0x6000);
    state.writeTagAddress(0x80001000);
    state.writeChannelControl(START | 0x30);
    REQUIRE(state.acceptSourceChainTag((id << 28) | 2, 0x80002000) ==
      DMACChannelTransition::Continue);
    const auto expectedMADR = id == 0 || id == 3 || id == 4
      ? 0x80002000 : 0x80001010;
    const auto expectedTADR = id == 2 ? 0x80002000 :
      id == 1 || id == 7 ? 0x80001030 : 0x80001010;
    REQUIRE(state.memoryAddress() == expectedMADR);
    REQUIRE(state.tagAddress() == expectedTADR);
    REQUIRE(state.quadwordCount() == 2);
    REQUIRE((state.channelControl() & 0xffff0000) == (id << 28));
    REQUIRE((state.channelControl() & 0x30) == 0x30);
    REQUIRE(state.acceptQuadword() == DMACChannelTransition::Continue);
    REQUIRE(state.acceptQuadword() ==
      (id == 0 || id == 7 ? DMACChannelTransition::Complete :
        DMACChannelTransition::Continue));
  }
  for (const auto id : {5u, 6u})
  {
    auto state = sifState();
    state.writeMemoryAddress(0x6000);
    state.writeTagAddress(0x1000);
    state.writeChannelControl(START);
    REQUIRE(state.acceptSourceChainTag((id << 28) | 2, 0x80002000) ==
      DMACChannelTransition::Complete);
    REQUIRE(state.tagAddress() == 0x1000);
    REQUIRE(state.memoryAddress() == 0x6000);
    REQUIRE((state.channelControl() & 0xffff0000) == (id << 28));
  }
  auto state = sifState();
  REQUIRE_THROWS_AS(state.writeAddressStack(0, 0x1000), std::out_of_range);
  REQUIRE_THROWS_AS(state.addressStack(0), std::out_of_range);
  REQUIRE_THROWS_AS(
    DMACChannelState("bad", DMACChannelDirectionPolicy::Unrestricted,
      static_cast<DMACChannelProfile>(3)), std::invalid_argument);
}

TEST_CASE("EE SIF1 source transitions retain tag fields and honor zero counts and TIE",
  "[ee][sif1][dma]")
{
  for (const auto id : {0u, 1u, 2u, 3u, 4u, 7u})
  {
    for (const auto count : {0u, 1u})
    {
      CAPTURE(id);
      CAPTURE(count);
      auto state = sifState();
      state.writeTagAddress(0x1000);
      state.writeChannelControl(START | 0xb0);
      const std::uint32_t tag = 0x8c000000 | (id << 28) | count;
      REQUIRE(state.acceptSourceChainTag(tag, 0x2000) ==
        (count == 0 ? DMACChannelTransition::Complete :
          DMACChannelTransition::Continue));
      REQUIRE((state.channelControl() & 0xffff0000) == (tag & 0xffff0000));
      REQUIRE((state.channelControl() & 0x30) == 0x30);
      if (count != 0)
      {
        REQUIRE(state.acceptQuadword() == DMACChannelTransition::Complete);
      }
      state.completeTransfer();
      REQUIRE_FALSE(state.active());
    }
    auto state = sifState();
    state.writeTagAddress(0x1000);
    state.writeChannelControl(START);
    REQUIRE(state.acceptSourceChainTag((id << 28) | 0x80000000, 0x2000) ==
      (id == 0 || id == 7 ? DMACChannelTransition::Complete :
        DMACChannelTransition::Continue));
  }
}

TEST_CASE("EE SIF1 exact MMIO mapping excludes nonexistent register reads and wider stores",
  "[ee][sif1][dma][bus]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  REQUIRE(EEMemoryMap::D6_CHCR == CHCR);
  REQUIRE(EEMemoryMap::D6_MADR == MADR);
  REQUIRE(EEMemoryMap::D6_QWC == QWC);
  REQUIRE(EEMemoryMap::D6_TADR == TADR);
  std::uint32_t word = 0;
  std::uint16_t half = 0;
  std::uint64_t doubleword = 0;
  EEQuadword quadword = {};
  for (const auto address : {CHCR, MADR, QWC, TADR})
  {
    REQUIRE(bus.readData32(address, &word));
    REQUIRE(word == 0);
    REQUIRE_FALSE(bus.readData16(address, &half));
    REQUIRE_FALSE(bus.writeData8(address, 0xff));
    REQUIRE_FALSE(bus.writeData16(address, 0xffff));
    REQUIRE_FALSE(bus.readData64(address, &doubleword));
    REQUIRE_FALSE(bus.writeData64(address, UINT64_C(0xffffffffffffffff)));
    REQUIRE_FALSE(bus.readData128(address, &quadword));
    REQUIRE_FALSE(bus.writeData128(address, quadword));
    REQUIRE_FALSE(bus.readData32(address + 4, &word));
    REQUIRE_FALSE(bus.writeData32(address + 4, 0xffffffff));
    REQUIRE_THROWS_AS(bus.read64(address), std::out_of_range);
    REQUIRE_THROWS_AS(bus.write64(address, 0x1230), std::out_of_range);
    REQUIRE_THROWS_AS(bus.readQuadword(address), std::out_of_range);
    REQUIRE_THROWS_AS(bus.writeQuadword(address, {{1, 2, 3, 4}}), std::out_of_range);
  }
  REQUIRE(system.sif1DMAC().channelControl() == 0);
  REQUIRE(system.sif1DMAC().memoryAddress() == 0);
  REQUIRE(system.sif1DMAC().quadwordCount() == 0);
  REQUIRE(system.sif1DMAC().tagAddress() == 0);
  const auto beforeAbsentWrites = system.eeStateHash();
  for (const auto address : {0x1000c440u, 0x1000c450u, 0x1000c480u, 0x1000c030u})
  {
    REQUIRE_FALSE(bus.readData32(address, &word));
    REQUIRE(bus.writeData32(address, 0xffffffff));
    REQUIRE_FALSE(bus.readData32(address, &word));
  }
  REQUIRE(system.eeStateHash() == beforeAbsentWrites);
  REQUIRE(bus.writeData32(MADR, 0x8000123f));
  REQUIRE(bus.read32(MADR) == 0x80001230);
  REQUIRE(bus.writeData32(TADR, 0x8000234f));
  REQUIRE(bus.writeData32(QWC, 0xffff0002));
  REQUIRE(bus.writeData32(CHCR, START));
  REQUIRE(system.sif().control() == 0x4000);
  REQUIRE_FALSE(bus.writeData32(MADR, 0));
  REQUIRE_FALSE(bus.writeData32(QWC, 0));
  REQUIRE_FALSE(bus.writeData32(TADR, 0));
  REQUIRE(bus.writeData32(CHCR, 4));
  REQUIRE(system.sif().control() == 0);
  bus.write32(0xb000c410, 0x80003450);
  REQUIRE(bus.read32(0xb000c410) == 0x80003450);
}

TEST_CASE("EE SIF1 canonical hashes track every register without conflating SIF state",
  "[ee][sif1][dma][save-state]")
{
  NekoSystem system;
  const auto eeHash = system.eeStateHash();
  const auto sifHash = system.sifStateHash();
  const auto channelHash = system.sif1DMAC().stateHash();
  for (const auto address : {CHCR, MADR, QWC, TADR})
  {
    CAPTURE(address);
    system.eeBus().write32(address, address == CHCR ? 0x40 : 0x1230);
    REQUIRE(system.eeStateHash() != eeHash);
    REQUIRE(system.sif1DMAC().stateHash() != channelHash);
    REQUIRE(system.sifStateHash() == sifHash);
    NekoSystem restored;
    restored.loadState(system.saveState());
    IOPTraceTest::requireMachine(restored, system);
    system.eeBus().write32(address, 0);
    REQUIRE(system.eeStateHash() == eeHash);
    REQUIRE(system.sif1DMAC().stateHash() == channelHash);
  }
  system.sif().applyEEDMAEvent(SIFDirection::SIF1, SIFEEDMAEvent::Start);
  REQUIRE_FALSE(system.sif1DMAC().active());
  NekoSystem restored;
  restored.loadState(system.saveState());
  REQUIRE(restored.sif().control() == 0x4000);
  REQUIRE_FALSE(restored.sif1DMAC().active());
  REQUIRE(restored.eeStateHash() == eeHash);
}

TEST_CASE("SIF typed EE DMA lifecycle events isolate directions and reject invalid inputs",
  "[sif1][sif][dma]")
{
  SIF sif;
  sif.writeIOP(SIFRegister::CTRL, 0xf0);
  sif.applyEEDMAEvent(SIFDirection::SIF1, SIFEEDMAEvent::Start);
  REQUIRE(sif.control() == 0x60f0);
  sif.applyEEDMAEvent(SIFDirection::SIF0, SIFEEDMAEvent::Start);
  REQUIRE(sif.control() == 0x60f0);
  sif.applyEEDMAEvent(SIFDirection::SIF0, SIFEEDMAEvent::Complete);
  REQUIRE(sif.control() == 0x40d0);
  REQUIRE_THROWS_AS(
    sif.applyEEDMAEvent(static_cast<SIFDirection>(2), SIFEEDMAEvent::Start),
    std::out_of_range);
  REQUIRE(sif.control() == 0x40d0);
  REQUIRE_THROWS_AS(
    sif.applyEEDMAEvent(SIFDirection::SIF1, static_cast<SIFEEDMAEvent>(4)),
    std::out_of_range);
  REQUIRE(sif.control() == 0x40d0);
  sif.applyEEDMAEvent(SIFDirection::SIF1, SIFEEDMAEvent::Complete);
  REQUIRE(sif.control() == 0x90);
}

TEST_CASE("EE DMA common status includes channel 6 with existing W1C and toggle masks",
  "[ee][sif1][dma]")
{
  NekoSystem system;
  auto &controller = system.dmacController();
  REQUIRE(DMACStatus::CHANNEL_6 == CHANNEL_6);
  REQUIRE(DMACStatus::CHANNEL_6_MASK == CHANNEL_6_MASK);
  controller.signalChannelCompletion(CHANNEL_6);
  REQUIRE(controller.status() == CHANNEL_6);
  REQUIRE_FALSE(controller.interruptPending());
  controller.writeStatus(CHANNEL_6_MASK);
  REQUIRE(controller.status() == (CHANNEL_6 | CHANNEL_6_MASK));
  REQUIRE(controller.interruptPending());
  controller.writeStatus(CHANNEL_6);
  REQUIRE(controller.status() == CHANNEL_6_MASK);
  REQUIRE_FALSE(controller.interruptPending());
  controller.writeStatus(CHANNEL_6_MASK);
  REQUIRE(controller.status() == 0);
  controller.signalBusError(CHANNEL_6);
  REQUIRE(controller.status() == (CHANNEL_6 | DMACStatus::BUS_ERROR));
  REQUIRE(controller.interruptPending());
  controller.writeStatus(CHANNEL_6 | DMACStatus::BUS_ERROR);
  REQUIRE(controller.status() == 0);
}

TEST_CASE("EE SIF1 restores active registers without regenerating CTRL or destination wiring",
  "[ee][sif1][dma][save-state]")
{
  NekoSystem original;
  original.sif1DMAC().writeMemoryAddress(0x80001230);
  original.sif1DMAC().writeQuadwordCount(2);
  original.sif1DMAC().writeTagAddress(0x80002340);
  original.sif1DMAC().writeChannelControl(START);
  original.sif().writeIOP(SIFRegister::CTRL, 0x20);
  REQUIRE(original.sif().control() == 0x2020);
  original.dmacController().signalChannelCompletion(CHANNEL_6);
  original.dmacController().writeStatus(CHANNEL_6_MASK);
  original.runMasterCycles(9);
  NekoSystem restored;
  original.startTrace();
  restored.startTrace();
  restored.loadState(original.saveState());
  original.clearTrace();
  restored.clearTrace();
  IOPTraceTest::requireMachine(restored, original);
  REQUIRE(restored.sif().control() == 0x2020);
  REQUIRE(restored.sif1DMAC().active());
  original.runMasterCycles(16);
  restored.runMasterCycles(16);
  IOPTraceTest::requireMachine(restored, original);
  IOPTraceTest::requireEvents(restored.trace(), original.trace());
  REQUIRE(restored.sif().control() == 0x2020);
  restored.sif1DMAC().writeChannelControl(4);
  restored.sif1DMAC().writeChannelControl(START);
  REQUIRE(restored.sif().control() == 0x6020);
  REQUIRE(original.sif().control() == 0x2020);
  restored.reset();
  NekoSystem fresh;
  IOPTraceTest::requireMachine(restored, fresh);
}

TEST_CASE("EE SIF1 malformed snapshots reject transactionally",
  "[ee][sif1][dma][save-state]")
{
  struct InvalidField
  {
    const char *path;
    std::uint32_t value;
  };
  NekoSystem source;
  NekoSystem destination;
  destination.sif1DMAC().writeChannelControl(START);
  const auto before = destination.saveState();
  for (const auto field : {
    InvalidField{"system.sif1DMAC.channelControlRegister", 0x144},
    InvalidField{"system.sif1DMAC.channelControlRegister", 0x108},
    InvalidField{"system.sif1DMAC.memoryAddressRegister", 1},
    InvalidField{"system.sif1DMAC.tagAddressRegister", 1},
    InvalidField{"system.sif1DMAC.quadwordCountRegister", 0x10000},
    InvalidField{"system.sif1DMAC.terminateAfterPacket", 1}})
  {
    CAPTURE(field.path);
    SaveStateMutation corrupt(source.saveState());
    corrupt.writeScalar(field.path, field.value);
    corrupt.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(corrupt.bytes()), std::invalid_argument);
    requireState(destination, before);
  }
}

TEST_CASE("EE SIF1 normal sender moves one physical qword per enabled EE phase",
  "[ee][sif1][dma][sender]")
{
  NekoSystem system;
  auto &channel = system.sif1DMAC();
  writeSource(system, 0x1000, {{1, 2, 3, 4}});
  writeSource(system, 0x1010, {{5, 6, 7, 8}});
  channel.writeMemoryAddress(0x1000);
  channel.writeQuadwordCount(2);
  channel.writeChannelControl(0x100);
  system.runMasterCycles(1);
  requireStream(system, {});
  REQUIRE(channel.quadwordCount() == 2);
  system.dmacController().writeControl(1);
  system.runMasterCycles(1);
  requireStream(system, {1, 2, 3, 4});
  REQUIRE(channel.memoryAddress() == 0x1010);
  REQUIRE(channel.quadwordCount() == 1);
  REQUIRE(channel.active());
  REQUIRE(system.dmacController().status() == 0);
  system.runMasterCycles(1);
  requireStream(system, {1, 2, 3, 4, 5, 6, 7, 8});
  REQUIRE(channel.memoryAddress() == 0x1020);
  REQUIRE(channel.quadwordCount() == 0);
  REQUIRE_FALSE(channel.active());
  REQUIRE(system.dmacController().status() == CHANNEL_6);
  REQUIRE(system.sif().control() == 0);
  REQUIRE_FALSE(system.dmacController().interruptPending());
  REQUIRE(system.iopDMAController().interruptPending() == false);
  REQUIRE(system.iopInterruptController().status() == 0);
  system.runMasterCycles(8);
  requireStream(system, {1, 2, 3, 4, 5, 6, 7, 8});
}

TEST_CASE("EE SIF1 source tags and payload consume distinct bounded grants",
  "[ee][sif1][dma][sender]")
{
  for (const auto id : {0u, 1u, 2u, 3u, 4u, 7u})
  {
    CAPTURE(id);
    NekoSystem system;
    auto &channel = system.sif1DMAC();
    writeSource(system, 0x1000, {{(id << 28) | 1, 0x2000, 0xdead, 0xbeef}});
    writeSource(system, 0x1010, {{1, 2, 3, 4}});
    writeSource(system, 0x2000, {{5, 6, 7, 8}});
    const auto nextTag = id == 2 ? 0x2000u :
      id == 1 || id == 7 ? 0x1020u : 0x1010u;
    channel.writeTagAddress(0x1000);
    channel.writeChannelControl(START);
    system.dmacController().writeControl(1);
    system.runMasterCycles(1);
    requireStream(system, {});
    REQUIRE(channel.active());
    REQUIRE(channel.quadwordCount() == 1);
    REQUIRE(channel.tagAddress() == nextTag);
    REQUIRE((channel.channelControl() & 0xffff0000) == (id << 28));
    system.runMasterCycles(1);
    const bool referenced = id == 0 || id == 3 || id == 4;
    requireStream(system, referenced ? std::vector<std::uint32_t>{5, 6, 7, 8} :
      std::vector<std::uint32_t>{1, 2, 3, 4});
    REQUIRE(channel.active() == (id != 0 && id != 7));
    REQUIRE(channel.quadwordCount() == 0);
    REQUIRE(channel.memoryAddress() == (referenced ? 0x2010u : 0x1020u));
  }
}

TEST_CASE("EE SIF1 source chains follow references jumps and inline packets",
  "[ee][sif1][dma][sender]")
{
  NekoSystem system;
  writeSource(system, 0x1000, {{0x30000001, 0x3000, 0, 0}});
  writeSource(system, 0x1010, {{0x20000001, 0x2000, 0, 0}});
  writeSource(system, 0x1020, {{5, 6, 7, 8}});
  writeSource(system, 0x2000, {{0x10000001, 0, 0, 0}});
  writeSource(system, 0x2010, {{9, 10, 11, 12}});
  writeSource(system, 0x2020, {{0x70000001, 0, 0, 0}});
  writeSource(system, 0x2030, {{13, 14, 15, 16}});
  writeSource(system, 0x3000, {{1, 2, 3, 4}});
  system.sif1DMAC().writeTagAddress(0x1000);
  system.sif1DMAC().writeChannelControl(START);
  system.dmacController().writeControl(1);
  system.runMasterCycles(8);
  requireStream(system, {1, 2, 3, 4, 5, 6, 7, 8,
    9, 10, 11, 12, 13, 14, 15, 16});
  REQUIRE_FALSE(system.sif1DMAC().active());
  REQUIRE(system.sif1DMAC().tagAddress() == 0x2040);
  REQUIRE(system.dmacController().status() == CHANNEL_6);
}

TEST_CASE("EE SIF1 zero and undefined tags complete locally without stream capacity",
  "[ee][sif1][dma][sender]")
{
  for (const auto id : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u})
  {
    CAPTURE(id);
    NekoSystem system;
    auto &channel = system.sif1DMAC();
    for (unsigned index = 0; index < SIF::WORD_CAPACITY / 4; ++index)
    {
      REQUIRE(system.sif().pushSIF0Prologue({{1, 2, 3, 4}}) ==
        SIFTransferOutcome::Completed);
    }
    const bool undefined = id == 5 || id == 6;
    writeSource(system, 0x1000, {{(id << 28) | (undefined ? 3u : 0u),
      0x2000, 0xdead, 0xbeef}});
    channel.writeMemoryAddress(0x3000);
    channel.writeTagAddress(0x1000);
    channel.writeChannelControl(START | 0x30);
    system.dmacController().writeControl(1);
    system.runMasterCycles(1);
    requireStream(system, {});
    REQUIRE(system.sif().freeWordCount() == 0);
    const bool terminal = id == 0 || id == 7 || undefined;
    REQUIRE(channel.active() == !terminal);
    REQUIRE(system.dmacController().status() == (terminal ? CHANNEL_6 : 0));
    REQUIRE((channel.channelControl() & 0xffff0030) == ((id << 28) | 0x30));
    if (undefined)
    {
      REQUIRE(channel.tagAddress() == 0x1000);
      REQUIRE(channel.memoryAddress() == 0x3000);
      REQUIRE(channel.quadwordCount() == 3);
    }
  }

  NekoSystem system;
  system.sif1DMAC().writeMemoryAddress(0x10000000);
  system.sif1DMAC().writeChannelControl(0x100);
  system.runMasterCycles(1);
  REQUIRE(system.sif1DMAC().active());
  system.dmacController().writeControl(1);
  system.runMasterCycles(1);
  REQUIRE_FALSE(system.sif1DMAC().active());
  REQUIRE(system.dmacController().status() == CHANNEL_6);
}

TEST_CASE("EE SIF1 TIE terminates after its packet but not at tag acceptance",
  "[ee][sif1][dma][sender]")
{
  for (const auto tie : {0u, 0x80u})
  {
    NekoSystem system;
    writeSource(system, 0x1000, {{0x9c000001, 0, 0, 0}});
    writeSource(system, 0x1010, {{1, 2, 3, 4}});
    writeSource(system, 0x1020, {{0x70000000, 0, 0, 0}});
    system.sif1DMAC().writeTagAddress(0x1000);
    system.sif1DMAC().writeChannelControl(START | tie);
    system.dmacController().writePriorityControl(1u << 22);
    system.dmacController().writeControl(1);
    system.runMasterCycles(1);
    REQUIRE(system.sif1DMAC().active());
    REQUIRE(system.dmacController().status() == 0);
    REQUIRE(system.dmacController().priorityControl() ==
      (DMACPriority::ENABLE | (1u << 22)));
    system.runMasterCycles(1);
    requireStream(system, {1, 2, 3, 4});
    REQUIRE(system.sif1DMAC().active() == (tie == 0));
    REQUIRE(system.sif1DMAC().tagAddress() == 0x1020);
    REQUIRE((system.sif1DMAC().channelControl() & 0xffff0000) == 0x9c000000);
    if (tie == 0)
    {
      system.runMasterCycles(1);
    }
    REQUIRE_FALSE(system.sif1DMAC().active());
    REQUIRE(system.dmacController().status() == CHANNEL_6);
  }
}

TEST_CASE("EE SIF1 payload waits atomically for four shared free words and retries once",
  "[ee][sif1][dma][sender]")
{
  NekoSystem system;
  auto &channel = system.sif1DMAC();
  for (unsigned index = 0; index < SIF::WORD_CAPACITY / 4; ++index)
  {
    REQUIRE(system.sif().pushSIF0Prologue({{index, index, index, index}}) ==
      SIFTransferOutcome::Completed);
  }
  writeSource(system, 0x1000, {{0x70000001, 0, 0, 0}});
  writeSource(system, 0x1010, {{1, 2, 3, 4}});
  channel.writeTagAddress(0x1000);
  channel.writeChannelControl(START);
  system.dmacController().writeControl(1);
  system.runMasterCycles(1);
  REQUIRE(channel.quadwordCount() == 1);
  for (unsigned freeWords = 0; freeWords < 4; ++freeWords)
  {
    CAPTURE(freeWords);
    const auto channelHash = channel.stateHash();
    const auto sifHash = system.sifStateHash();
    system.runMasterCycles(2);
    REQUIRE(channel.stateHash() == channelHash);
    REQUIRE(system.sifStateHash() == sifHash);
    REQUIRE(system.dmacController().status() == 0);
    REQUIRE(system.sif().consumeWords(SIFDirection::SIF0, 1) ==
      SIFTransferOutcome::Completed);
  }
  writeSource(system, 0x1010, {{5, 6, 7, 8}});
  system.runMasterCycles(1);
  requireStream(system, {5, 6, 7, 8});
  REQUIRE(system.sif().freeWordCount() == 0);
  REQUIRE_FALSE(channel.active());
  REQUIRE(system.dmacController().status() == CHANNEL_6);
  system.runMasterCycles(1);
  requireStream(system, {5, 6, 7, 8});
}

TEST_CASE("EE SIF1 long packets stream without fitting entirely in the FIFO",
  "[ee][sif1][dma][sender]")
{
  NekoSystem system;
  for (std::uint32_t index = 0; index < 10; ++index)
  {
    writeSource(system, 0x1000 + index * 16,
      {{index * 4, index * 4 + 1, index * 4 + 2, index * 4 + 3}});
  }
  system.sif1DMAC().writeMemoryAddress(0x1000);
  system.sif1DMAC().writeQuadwordCount(10);
  system.sif1DMAC().writeChannelControl(0x100);
  system.dmacController().writeControl(1);
  system.runMasterCycles(9);
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 32);
  REQUIRE(system.sif1DMAC().quadwordCount() == 2);
  for (std::uint32_t index = 0; index < 10; ++index)
  {
    SIFQuadword words = {};
    REQUIRE(system.sif().peekWords(SIFDirection::SIF1, words, 4) ==
      SIFTransferOutcome::Completed);
    const SIFQuadword expected = {{index * 4, index * 4 + 1,
      index * 4 + 2, index * 4 + 3}};
    REQUIRE(words == expected);
    REQUIRE(system.sif().consumeWords(SIFDirection::SIF1, 4) ==
      SIFTransferOutcome::Completed);
    system.runMasterCycles(1);
  }
  REQUIRE_FALSE(system.sif1DMAC().active());
  requireStream(system, {});
  REQUIRE(system.sif1DMAC().memoryAddress() == 0x10a0);
}

TEST_CASE("EE SIF1 DMA reads physical scratchpad tags and payload using its low 14 bits",
  "[ee][sif1][dma][sender]")
{
  NekoSystem system;
  writeSource(system, 0x80003ff0, {{0x70000002, 0, 0, 0}});
  writeSource(system, 0x80000000, {{1, 2, 3, 4}});
  writeSource(system, 0x80000010, {{5, 6, 7, 8}});
  system.sif1DMAC().writeTagAddress(0x80003ff0);
  system.sif1DMAC().writeChannelControl(START);
  system.dmacController().writeControl(1);
  system.runMasterCycles(1);
  REQUIRE(system.sif1DMAC().memoryAddress() == 0x80004000);
  REQUIRE(system.sif1DMAC().tagAddress() == 0x80004020);
  system.runMasterCycles(2);
  requireStream(system, {1, 2, 3, 4, 5, 6, 7, 8});
  REQUIRE_FALSE(system.sif1DMAC().active());
  REQUIRE(system.sif1DMAC().memoryAddress() == 0x80004020);
  REQUIRE(system.dmacController().status() == CHANNEL_6);

  system.sif1DMAC().writeTagAddress(0x1000);
  writeSource(system, 0x1000, {{1, 0x81230010, 0, 0}});
  system.sif1DMAC().writeChannelControl(START);
  system.runMasterCycles(2);
  requireStream(system, {1, 2, 3, 4, 5, 6, 7, 8, 5, 6, 7, 8});
  REQUIRE(system.sif1DMAC().memoryAddress() == 0x81230020);
}

TEST_CASE("EE SIF1 invalid physical sources terminate with local CIS and BEIS only",
  "[ee][sif1][dma][sender]")
{
  for (const auto address : {0x02000000u, 0x20001000u, 0x70000000u,
    EEMemoryMap::SIF_BASE, EEMemoryMap::VU0_DATA_MEMORY_BASE,
    EEMemoryMap::BOOT_ROM_BASE})
  {
    for (const auto chain : {false, true})
    {
      CAPTURE(address);
      CAPTURE(chain);
      NekoSystem system;
      auto &channel = system.sif1DMAC();
      REQUIRE(system.sif().pushSIF1Quadword({{1, 2, 3, 4}}) ==
        SIFTransferOutcome::Completed);
      channel.writeMemoryAddress(address);
      channel.writeTagAddress(address);
      channel.writeQuadwordCount(chain ? 0 : 1);
      channel.writeChannelControl((chain ? START : 0x100) | 0xabcd0000);
      system.dmacController().writeControl(1);
      system.runMasterCycles(1);
      REQUIRE_FALSE(channel.active());
      REQUIRE(channel.memoryAddress() == address);
      REQUIRE(channel.tagAddress() == address);
      REQUIRE(channel.quadwordCount() == (chain ? 0 : 1));
      REQUIRE((channel.channelControl() & 0xffff0000) == 0xabcd0000);
      REQUIRE(system.dmacController().status() == (CHANNEL_6 | DMACStatus::BUS_ERROR));
      REQUIRE(system.dmacController().interruptPending());
      REQUIRE(system.sif().control() == 0);
      requireStream(system, {1, 2, 3, 4});
      REQUIRE(system.iopInterruptController().status() == 0);
    }
  }
}

TEST_CASE("EE SIF1 accepted terminal packet survives pause restore and source bus error",
  "[ee][sif1][dma][sender][save-state]")
{
  NekoSystem original;
  const auto last = EEMemoryMap::MAIN_MEMORY_SIZE - 16;
  writeSource(original, 0x1000, {{2, last, 0, 0}});
  writeSource(original, last, {{1, 2, 3, 4}});
  original.sif1DMAC().writeTagAddress(0x1000);
  original.sif1DMAC().writeChannelControl(START);
  original.dmacController().writeControl(1);
  original.runMasterCycles(1);
  NekoSystem restored;
  restored.loadState(original.saveState());
  IOPTraceTest::requireMachine(restored, original);
  original.startTrace();
  restored.startTrace();
  original.runMasterCycles(1);
  restored.runMasterCycles(1);
  IOPTraceTest::requireMachine(restored, original);
  requireStream(original, {1, 2, 3, 4});
  REQUIRE(original.sif1DMAC().active());
  original.dmacController().writeControl(0);
  original.runMasterCycles(3);
  REQUIRE(original.sif1DMAC().quadwordCount() == 1);
  REQUIRE(original.sif1DMAC().active());
  restored.loadState(original.saveState());
  original.clearTrace();
  restored.clearTrace();
  original.dmacController().writeControl(1);
  restored.dmacController().writeControl(1);
  original.runMasterCycles(1);
  restored.runMasterCycles(1);
  IOPTraceTest::requireMachine(restored, original);
  IOPTraceTest::requireEvents(restored.trace(), original.trace());
  REQUIRE_FALSE(original.sif1DMAC().active());
  REQUIRE(original.sif1DMAC().memoryAddress() == EEMemoryMap::MAIN_MEMORY_SIZE);
  REQUIRE(original.sif1DMAC().quadwordCount() == 1);
  REQUIRE(original.dmacController().status() == (CHANNEL_6 | DMACStatus::BUS_ERROR));
  requireStream(original, {1, 2, 3, 4});
}

TEST_CASE("EE SIF1 stalled terminal continuation restores and abort preserves queued data",
  "[ee][sif1][dma][sender][save-state]")
{
  NekoSystem original;
  for (unsigned index = 0; index < SIF::WORD_CAPACITY / 4; ++index)
  {
    REQUIRE(original.sif().pushSIF1Quadword({{1, 2, 3, 4}}) ==
      SIFTransferOutcome::Completed);
  }
  writeSource(original, 0x1000, {{0x70000001, 0, 0, 0}});
  writeSource(original, 0x1010, {{5, 6, 7, 8}});
  original.sif1DMAC().writeTagAddress(0x1000);
  original.sif1DMAC().writeChannelControl(START);
  original.dmacController().writeControl(1);
  original.runMasterCycles(2);
  REQUIRE(original.sif1DMAC().quadwordCount() == 1);
  NekoSystem restored;
  restored.loadState(original.saveState());
  IOPTraceTest::requireMachine(restored, original);
  for (auto *system : {&original, &restored})
  {
    REQUIRE(system->sif().consumeWords(SIFDirection::SIF1, 4) ==
      SIFTransferOutcome::Completed);
    system->runMasterCycles(1);
    REQUIRE_FALSE(system->sif1DMAC().active());
    REQUIRE(system->sif().wordCount(SIFDirection::SIF1) == 32);
    REQUIRE(system->sif().wordAt(SIFDirection::SIF1, 28) == 5);
  }
  IOPTraceTest::requireMachine(restored, original);
  original.dmacController().writeStatus(CHANNEL_6);
  original.sif1DMAC().writeMemoryAddress(0x1010);
  original.sif1DMAC().writeQuadwordCount(1);
  original.sif1DMAC().writeChannelControl(0x100);
  original.runMasterCycles(1);
  original.sif1DMAC().writeChannelControl(0);
  REQUIRE(original.sif1DMAC().quadwordCount() == 1);
  REQUIRE(original.sif().wordCount(SIFDirection::SIF1) == 32);
  REQUIRE(original.dmacController().status() == 0);
  restored.loadState(original.saveState());
  original.reset();
  restored.reset();
  IOPTraceTest::requireMachine(restored, original);
  NekoSystem fresh;
  IOPTraceTest::requireMachine(original, fresh);
}

TEST_CASE("EE SIF1 address progression retains SPR across the 31-bit address boundary",
  "[ee][sif1][dma][sender]")
{
  for (const auto chain : {false, true})
  {
    CAPTURE(chain);
    NekoSystem system;
    writeSource(system, 0xfffffff0,
      chain ? SIFQuadword{{0x70000001, 0, 0, 0}} : SIFQuadword{{1, 2, 3, 4}});
    writeSource(system, 0x80000000, {{5, 6, 7, 8}});
    system.sif1DMAC().writeMemoryAddress(0xfffffff0);
    system.sif1DMAC().writeTagAddress(0xfffffff0);
    system.sif1DMAC().writeQuadwordCount(chain ? 0 : 2);
    system.sif1DMAC().writeChannelControl(chain ? START : 0x100);
    system.dmacController().writeControl(1);
    system.runMasterCycles(1);
    REQUIRE(system.sif1DMAC().memoryAddress() == 0x80000000);
    if (chain)
    {
      REQUIRE(system.sif1DMAC().tagAddress() == 0x80000010);
    }
    system.runMasterCycles(1);
    requireStream(system, chain ? std::vector<std::uint32_t>{5, 6, 7, 8} :
      std::vector<std::uint32_t>{1, 2, 3, 4, 5, 6, 7, 8});
    REQUIRE(system.sif1DMAC().memoryAddress() == 0x80000010);
    REQUIRE_FALSE(system.sif1DMAC().active());
    REQUIRE(system.dmacController().status() == CHANNEL_6);
  }
}

TEST_CASE("EE SIF1 persistence rejects impossible terminal packet continuations",
  "[ee][sif1][dma][sender][save-state]")
{
  NekoSystem source;
  writeSource(source, 0x1000, {{0x70000001, 0, 0, 0}});
  source.sif1DMAC().writeTagAddress(0x1000);
  source.sif1DMAC().writeChannelControl(START);
  source.dmacController().writeControl(1);
  source.runMasterCycles(1);
  REQUIRE(source.sif1DMAC().quadwordCount() == 1);
  NekoSystem destination;
  const auto before = destination.saveState();
  struct InvalidField
  {
    const char *path;
    std::uint32_t value;
  };
  for (const auto field : {
    InvalidField{"system.sif1DMAC.channelControlRegister", 0x70000004},
    InvalidField{"system.sif1DMAC.channelControlRegister", 0x70000100},
    InvalidField{"system.sif1DMAC.channelControlRegister", 0x10000104},
    InvalidField{"system.sif1DMAC.channelControlRegister", 0xd0000184},
    InvalidField{"system.sif1DMAC.channelControlRegister", 0xe0000184},
    InvalidField{"system.sif1DMAC.quadwordCountRegister", 0}})
  {
    CAPTURE(field.path);
    CAPTURE(field.value);
    SaveStateMutation corrupt(source.saveState());
    corrupt.writeScalar(field.path, field.value);
    corrupt.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(corrupt.bytes()), std::invalid_argument);
    requireState(destination, before);
  }
}

TEST_CASE("EE SIF1 grants follow legacy DMA writes and publish local completion immediately",
  "[ee][sif1][dma][sender]")
{
  NekoSystem system;
  writeSource(system, 0x80000000, {{1, 2, 3, 4}});
  auto &fromSPR = system.fromScratchpadDMAC();
  fromSPR.writeMemoryAddress(0x1000);
  fromSPR.writeQuadwordCount(1);
  fromSPR.writeScratchpadAddress(0);
  fromSPR.writeChannelControl(0x100);
  system.sif1DMAC().writeMemoryAddress(0x1000);
  system.sif1DMAC().writeQuadwordCount(1);
  system.sif1DMAC().writeChannelControl(0x100);
  system.dmacController().writeControl(1);
  system.dmacController().writeStatus(CHANNEL_6_MASK);
  system.clockMasterCycle();
  requireStream(system, {1, 2, 3, 4});
  REQUIRE_FALSE(fromSPR.active());
  REQUIRE_FALSE(system.sif1DMAC().active());
  REQUIRE(system.dmacController().status() ==
    (CHANNEL_6_MASK | CHANNEL_6 | DMACStatus::CHANNEL_8));
  REQUIRE(system.interruptPending());
}

TEST_CASE("EE SIF1 grants return explicit wait accepted and terminal fault outcomes",
  "[ee][sif1][dma][sender]")
{
  NekoSystem system;
  auto &controller = system.dmacController();
  auto &channel = system.sif1DMAC();
  REQUIRE(controller.clockSIF(channel) == SIF1DMABeatOutcome::Wait);
  channel.writeChannelControl(0x100);
  REQUIRE(controller.clockSIF(channel) == SIF1DMABeatOutcome::Wait);
  controller.writeControl(1);
  REQUIRE(controller.clockSIF(channel) == SIF1DMABeatOutcome::Completed);
  channel.writeMemoryAddress(0x02000000);
  channel.writeQuadwordCount(1);
  channel.writeChannelControl(0x100);
  REQUIRE(controller.clockSIF(channel) == SIF1DMABeatOutcome::BusError);
  REQUIRE_FALSE(channel.active());
  DMACController foreign;
  const auto before = system.saveState();
  REQUIRE_THROWS_AS(foreign.clockSIF(channel), std::invalid_argument);
  requireState(system, before);
}

TEST_CASE("EE SIF1 accepted tag and payload boundaries replay identically at every phase",
  "[ee][sif1][dma][sender][save-state]")
{
  for (unsigned prefix = 0; prefix <= 8; ++prefix)
  {
    CAPTURE(prefix);
    NekoSystem original;
    writeSource(original, 0x1000, {{0x10000001, 0, 0, 0}});
    writeSource(original, 0x1010, {{1, 2, 3, 4}});
    writeSource(original, 0x1020, {{0x70000002, 0, 0, 0}});
    writeSource(original, 0x1030, {{5, 6, 7, 8}});
    writeSource(original, 0x1040, {{9, 10, 11, 12}});
    original.sif1DMAC().writeTagAddress(0x1000);
    original.sif1DMAC().writeChannelControl(START);
    original.dmacController().writeControl(1);
    original.runMasterCycles(prefix);
    NekoSystem restored;
    restored.loadState(original.saveState());
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(9);
    restored.runMasterCycles(9);
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    requireStream(original, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
    REQUIRE_FALSE(original.sif1DMAC().active());
  }
}
