#include <cstdint>
#include <stdexcept>

#include "catch.hpp"
#include "iop/iop_trace_test_utils.hpp"
#include "save_state_mutation.hpp"

namespace
{
  constexpr IOPAddress IOP_MADR = 0x1f801520;
  constexpr IOPAddress IOP_BCR = 0x1f801524;
  constexpr IOPAddress IOP_CHCR = 0x1f801528;
  constexpr IOPAddress IOP_TADR = 0x1f80152c;
  constexpr IOPWord IOP_START = 0x01000401;
  constexpr std::uint32_t EE_CHCR = 0x1000c000;
  constexpr std::uint32_t EE_MADR = 0x1000c010;
  constexpr std::uint32_t EE_QWC = 0x1000c020;

  void requireUnchanged(const NekoSystem &system,
    const std::vector<std::uint8_t> &before)
  {
    const bool unchanged = system.saveState() == before;
    REQUIRE(unchanged);
  }
}

TEST_CASE("SIF0 IOP source registers admit exact words and bounded starts",
  "[sif0][dma][foundation]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  for (const auto address : {IOP_MADR, IOP_BCR, IOP_CHCR, IOP_TADR})
  {
    CAPTURE(address);
    REQUIRE(bus.read32(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(address).value == 0);
    const auto before = system.saveState();
    for (IOPAddress lane = 0; lane < 4; ++lane)
    {
      REQUIRE(bus.read8(address + lane).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write8(address + lane, 0xff).status == IOPBusStatus::Unmapped);
    }
    for (const IOPAddress lane : {0u, 2u})
    {
      REQUIRE(bus.read16(address + lane).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write16(address + lane, 0xffff).status == IOPBusStatus::Unmapped);
    }
    for (std::uint8_t mask = 1; mask < 15; ++mask)
    {
      REQUIRE(bus.writeMasked32(address, 0xffffffff, mask).status ==
        IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.read32(address + 1).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write32(address + 1, 0).status == IOPBusStatus::Misaligned);
    requireUnchanged(system, before);
  }
  REQUIRE(bus.write32(IOP_MADR, 0xff123454).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(IOP_TADR, 0xfe234568).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(IOP_BCR, 0xabcd1234).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(IOP_MADR).value == 0x123454);
  REQUIRE(bus.read32(IOP_TADR).value == 0x234568);
  REQUIRE(bus.read32(IOP_BCR).value == 0xabcd1234);
  const auto stopped = system.saveState();
  for (const auto address : {IOP_MADR, IOP_TADR})
  {
    REQUIRE(bus.write32(address, 3).status == IOPBusStatus::Unmapped);
    requireUnchanged(system, stopped);
  }
  for (const auto control : {
    0x01000000u, 0x01000201u, 0x01000400u, 0x41000401u, 0x800u})
  {
    REQUIRE(bus.write32(IOP_CHCR, control).status == IOPBusStatus::Unmapped);
    requireUnchanged(system, stopped);
  }
  for (const auto control : {IOP_START, IOP_START | 0x300})
  {
    REQUIRE(bus.write32(IOP_CHCR, control).status == IOPBusStatus::Completed);
    const auto active = system.saveState();
    REQUIRE(bus.write32(IOP_CHCR, control).status == IOPBusStatus::Completed);
    for (const auto address : {IOP_MADR, IOP_BCR, IOP_TADR})
    {
      REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.write32(IOP_CHCR, control ^ 0x200).status == IOPBusStatus::Unmapped);
    requireUnchanged(system, active);
    REQUIRE(bus.write32(IOP_CHCR, control & ~0x01000000u).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.read32(IOP_TADR).value == 0x234568);
    REQUIRE(bus.read32(IOP_MADR).value == 0x123454);
    REQUIRE(bus.read32(IOP_BCR).value == 0xabcd1234);
  }
  REQUIRE(system.iopDMAController().interruptStatus(IOPDMABank::Secondary) == 0);
  REQUIRE(system.iopInterruptController().status() == 0);
}

TEST_CASE("SIF0 EE destination registers have no source tag or address stack mapping",
  "[sif0][dma][foundation]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  std::uint32_t word = 0;
  std::uint16_t half = 0;
  std::uint64_t wide = 0;
  EEQuadword quad = {};
  for (const auto address : {EE_CHCR, EE_MADR, EE_QWC})
  {
    REQUIRE(bus.readData32(address, &word));
    REQUIRE(word == 0);
    const auto before = system.saveState();
    REQUIRE_FALSE(bus.readData16(address, &half));
    REQUIRE_FALSE(bus.writeData8(address, 1));
    REQUIRE_FALSE(bus.writeData16(address, 1));
    REQUIRE_FALSE(bus.readData64(address, &wide));
    REQUIRE_FALSE(bus.writeData64(address, 0x100));
    REQUIRE_FALSE(bus.readData128(address, &quad));
    REQUIRE_FALSE(bus.writeData128(address, quad));
    REQUIRE_THROWS_AS(bus.write64(address, 0x100), std::out_of_range);
    REQUIRE_THROWS_AS(bus.writeQuadword(address, {{1, 2, 3, 4}}), std::out_of_range);
    REQUIRE_FALSE(bus.readData32(address + 4, &word));
    REQUIRE_FALSE(bus.writeData32(address + 4, 1));
    requireUnchanged(system, before);
  }
  for (const auto address : {0x1000c030u, 0x1000c040u, 0x1000c050u, 0x1000c080u})
  {
    REQUIRE_FALSE(bus.readData32(address, &word));
    REQUIRE_FALSE(bus.writeData32(address, 0));
  }
  REQUIRE(bus.writeData32(EE_MADR, 0x8000123f));
  REQUIRE(bus.read32(EE_MADR) == 0x80001230);
  REQUIRE(bus.writeData32(EE_QWC, 0xabcd1234));
  REQUIRE(bus.read32(EE_QWC) == 0x1234);
  REQUIRE(bus.writeData32(EE_CHCR, 0xffff0071));
  const auto stopped = system.saveState();
  for (const auto control : {0x108u, 0x10cu, 0x106u, 0x204u})
  {
    REQUIRE_FALSE(bus.writeData32(EE_CHCR, control));
    requireUnchanged(system, stopped);
  }
  REQUIRE(bus.writeData32(EE_CHCR, 0x175));
  REQUIRE(system.sif().control() == 0x2000);
  const auto active = system.saveState();
  REQUIRE(bus.writeData32(EE_CHCR, 0x175));
  REQUIRE_FALSE(bus.writeData32(EE_CHCR, 0x174));
  REQUIRE_FALSE(bus.writeData32(EE_MADR, 0));
  REQUIRE_FALSE(bus.writeData32(EE_QWC, 0));
  requireUnchanged(system, active);
  REQUIRE(bus.writeData32(EE_CHCR, 0x75));
  REQUIRE(system.sif().control() == 0);
  REQUIRE(bus.read32(EE_MADR) == 0x80001230);
  REQUIRE(bus.read32(EE_QWC) == 0x1234);
  REQUIRE(system.dmacController().status() == 0);
}

TEST_CASE("SIF0 canonical fields and common completion slots remain independent",
  "[sif0][dma][foundation][save-state]")
{
  NekoSystem system;
  const auto eeHash = system.eeStateHash();
  const auto iopHash = system.iopStateHash();
  const auto sifHash = system.sifStateHash();
  const auto sif0Hash = system.sif0DMAC().stateHash();
  const auto sif1Hash = system.sif1DMAC().stateHash();
  for (const auto address : {EE_CHCR, EE_MADR, EE_QWC})
  {
    REQUIRE(system.eeBus().writeData32(address, address == EE_CHCR ? 0x71 : 0x1230));
    REQUIRE(system.eeStateHash() != eeHash);
    REQUIRE(system.sif0DMAC().stateHash() != sif0Hash);
    REQUIRE(system.sif1DMAC().stateHash() == sif1Hash);
    REQUIRE(system.iopStateHash() == iopHash);
    REQUIRE(system.sifStateHash() == sifHash);
    NekoSystem restored;
    restored.loadState(system.saveState());
    IOPTraceTest::requireMachine(restored, system);
    REQUIRE(system.eeBus().writeData32(address, 0));
    REQUIRE(system.eeStateHash() == eeHash);
  }
  for (const auto address : {IOP_MADR, IOP_BCR, IOP_CHCR, IOP_TADR})
  {
    REQUIRE(system.iopBus().write32(address, address == IOP_CHCR ? 0x701 : 0x1234).
      status == IOPBusStatus::Completed);
    REQUIRE(system.iopStateHash() != iopHash);
    REQUIRE(system.eeStateHash() == eeHash);
    REQUIRE(system.sifStateHash() == sifHash);
    NekoSystem restored;
    restored.loadState(system.saveState());
    IOPTraceTest::requireMachine(restored, system);
    REQUIRE(system.iopBus().write32(address, 0).status == IOPBusStatus::Completed);
    REQUIRE(system.iopStateHash() == iopHash);
  }
  auto &iopDMA = system.iopDMAController();
  for (std::uint32_t priority = 0; priority < 8; ++priority)
  {
    iopDMA.writePriority(IOPDMABank::Secondary, priority << 8);
    REQUIRE(iopDMA.channelPriority(IOPDMAChannel::SIF0) == priority);
    REQUIRE_FALSE(iopDMA.channelEnabled(IOPDMAChannel::SIF0));
    iopDMA.writePriority(IOPDMABank::Secondary, (priority | 8) << 8);
    REQUIRE(iopDMA.channelEnabled(IOPDMAChannel::SIF0));
    REQUIRE_FALSE(iopDMA.channelEnabled(IOPDMAChannel::SIF1));
  }
  iopDMA.signalChannelCompletion(IOPDMAChannel::SIF0);
  iopDMA.signalChannelCompletion(IOPDMAChannel::SIF1);
  REQUIRE(iopDMA.interruptStatus(IOPDMABank::Secondary) == 0x0c000000);
  iopDMA.writeInterrupt(IOPDMABank::Secondary, 0x04000000);
  REQUIRE(iopDMA.interruptStatus(IOPDMABank::Secondary) == 0x08000000);
  system.iopInterruptController().writeStatus(~8u);
  auto &eeDMA = system.dmacController();
  eeDMA.signalChannelCompletion(DMACStatus::CHANNEL_5);
  eeDMA.signalChannelCompletion(DMACStatus::CHANNEL_6);
  REQUIRE_FALSE(eeDMA.interruptPending());
  eeDMA.writeStatus(DMACStatus::CHANNEL_5_MASK);
  REQUIRE(eeDMA.interruptPending());
  eeDMA.writeStatus(DMACStatus::CHANNEL_5);
  REQUIRE_FALSE(eeDMA.interruptPending());
  REQUIRE((eeDMA.status() & DMACStatus::CHANNEL_6) != 0);
  NekoSystem restored;
  restored.loadState(system.saveState());
  IOPTraceTest::requireMachine(restored, system);
  REQUIRE(restored.iopInterruptController().status() == 0);
}

TEST_CASE("SIF0 register owners reject source-chain policy and preserve peer state",
  "[sif0][dma][foundation]")
{
  DMACChannelState state("SIF0", DMACChannelDirectionPolicy::Unrestricted,
    DMACChannelProfile::SIF0);
  REQUIRE_THROWS_AS(state.writeTagAddress(0x1000), std::logic_error);
  REQUIRE_THROWS_AS(state.writeAddressStack(0, 0x1000), std::out_of_range);
  state.writeChannelControl(0x104);
  REQUIRE_THROWS_AS(state.acceptSourceChainTag(0, 0x1000), std::logic_error);
  REQUIRE(state.channelControl() == 0x104);
  REQUIRE(state.quadwordCount() == 0);

  NekoSystem system;
  auto &sif = system.sif();
  sif.writeIOP(SIFRegister::CTRL, 0xf0);
  system.sif1DMAC().writeChannelControl(0x104);
  REQUIRE(system.iopSIF1DMAC().writeChannelControl(0x01000400) ==
    IOPDMAWriteOutcome::Completed);
  const auto peerEE = system.sif1DMAC().stateHash();
  const auto peerIOP = system.iopSIF1DMAC().channelControl();
  system.sif0DMAC().writeChannelControl(0x104);
  REQUIRE(sif.control() == 0x60f0);
  sif.writeIOP(SIFRegister::CTRL, 0x20);
  REQUIRE(sif.control() == 0x20d0);
  system.sif0DMAC().writeChannelControl(0x104);
  REQUIRE(sif.control() == 0x20d0);
  system.sif0DMAC().writeChannelControl(4);
  REQUIRE(sif.control() == 0xd0);
  system.sif0DMAC().writeChannelControl(0x100);
  REQUIRE(sif.control() == 0x20d0);
  REQUIRE(system.sif0DMAC().active());
  REQUIRE_FALSE(system.sif0DMAC().transferEnabled());
  system.dmacController().writeControl(1);
  REQUIRE(system.sif0DMAC().transferEnabled());
  REQUIRE(system.iopSIF0DMAC().writeChannelControl(IOP_START) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopSIF0DMAC().writeTagAddress(0) ==
    IOPDMAWriteOutcome::ActiveTransfer);
  REQUIRE(system.iopSIF0DMAC().writeChannelControl(IOP_START & ~0x01000000u) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(sif.control() == 0x20d0);
  REQUIRE(system.sif1DMAC().stateHash() == peerEE);
  REQUIRE(system.iopSIF1DMAC().channelControl() == peerIOP);
}

TEST_CASE("SIF0 register owners restore without lifecycle replay or implicit grants",
  "[sif0][dma][foundation][save-state]")
{
  NekoSystem original;
  const auto eeHash = original.eeStateHash();
  const auto iopHash = original.iopStateHash();
  original.sif().writeIOP(SIFRegister::CTRL, 0x20);
  REQUIRE(original.sif().pushSIF0Prologue({{1, 2, 3, 4}}) ==
    SIFTransferOutcome::Completed);
  REQUIRE(original.sif().pushSIF0PayloadWord(5, SIFPayloadBoundary::Continuing) ==
    SIFTransferOutcome::Completed);
  REQUIRE(original.iopBus().write32(IOP_MADR, 0x2340).status ==
    IOPBusStatus::Completed);
  REQUIRE(original.iopBus().write32(IOP_BCR, 0x87654321).status ==
    IOPBusStatus::Completed);
  REQUIRE(original.iopBus().write32(IOP_TADR, 0x1230).status ==
    IOPBusStatus::Completed);
  REQUIRE(original.iopBus().write32(IOP_CHCR, IOP_START).status ==
    IOPBusStatus::Completed);
  REQUIRE(original.eeBus().writeData32(EE_MADR, 0x80003450));
  REQUIRE(original.eeBus().writeData32(EE_QWC, 3));
  REQUIRE(original.eeBus().writeData32(EE_CHCR, 0x104));
  REQUIRE(original.eeStateHash() != eeHash);
  REQUIRE(original.iopStateHash() != iopHash);
  const auto sifHash = original.sifStateHash();
  original.dmacController().writeControl(1);
  original.iopDMAController().writePriority(IOPDMABank::Secondary, 0x800);
  original.runMasterCycles(17);
  REQUIRE(original.sifStateHash() == sifHash);
  REQUIRE(original.eeBus().read32(EE_QWC) == 3);
  REQUIRE(original.iopBus().read32(IOP_TADR).value == 0x1230);
  NekoSystem restored;
  restored.sif().writeIOP(SIFRegister::CTRL, 0x20);
  restored.startTrace();
  restored.loadState(original.saveState());
  REQUIRE(restored.traceEnabled());
  IOPTraceTest::requireMachine(restored, original);
  original.runMasterCycles(16);
  restored.runMasterCycles(16);
  IOPTraceTest::requireMachine(restored, original);
  restored.reset();
  NekoSystem fresh;
  IOPTraceTest::requireMachine(restored, fresh);
}

TEST_CASE("SIF0 malformed register snapshots reject transactionally",
  "[sif0][dma][foundation][save-state]")
{
  struct InvalidField { const char *path; std::uint32_t value; };
  NekoSystem source;
  NekoSystem destination;
  destination.runMasterCycles(9);
  destination.startTrace();
  const auto before = destination.saveState();
  const auto traceHash = destination.traceHash();
  for (const auto field : {
    InvalidField{"system.iop.sif0DMA.memoryAddress", 1},
    InvalidField{"system.iop.sif0DMA.tagAddress", 0x01000000},
    InvalidField{"system.iop.sif0DMA.channelControl", 0x01000400},
    InvalidField{"system.sif0DMAC.memoryAddressRegister", 1},
    InvalidField{"system.sif0DMAC.quadwordCountRegister", 0x10000},
    InvalidField{"system.sif0DMAC.channelControlRegister", 0x108}})
  {
    CAPTURE(field.path);
    SaveStateMutation corrupt(source.saveState());
    corrupt.writeScalar(field.path, field.value);
    corrupt.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(corrupt.bytes()), std::invalid_argument);
    requireUnchanged(destination, before);
    REQUIRE(destination.traceEnabled());
    REQUIRE(destination.traceHash() == traceHash);
  }
}
