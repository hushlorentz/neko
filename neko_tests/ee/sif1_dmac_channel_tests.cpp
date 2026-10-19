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
      DMACSourceChainProfile::SIF1);
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
      static_cast<DMACSourceChainProfile>(2)), std::invalid_argument);
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

TEST_CASE("EE SIF1 exact MMIO mapping excludes nonexistent registers and wider stores",
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
  for (const auto address : {0x1000c440u, 0x1000c450u, 0x1000c480u, 0x1000c000u})
  {
    REQUIRE_FALSE(bus.readData32(address, &word));
    REQUIRE_FALSE(bus.writeData32(address, 0xffffffff));
  }
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
