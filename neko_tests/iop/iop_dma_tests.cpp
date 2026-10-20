#include <cstdint>
#include <stdexcept>
#include <vector>

#include "catch.hpp"
#include "iop_dma_controller.hpp"
#include "iop_sif1_dma_channel.hpp"
#include "iop_trace_test_utils.hpp"
#include "save_state_mutation.hpp"

namespace
{
  constexpr IOPAddress DPCR = 0x1f8010f0;
  constexpr IOPAddress DICR = 0x1f8010f4;
  constexpr IOPAddress DPCR2 = 0x1f801570;
  constexpr IOPAddress DICR2 = 0x1f801574;
  constexpr IOPAddress MADR = 0x1f801530;
  constexpr IOPAddress BCR = 0x1f801534;
  constexpr IOPAddress CHCR = 0x1f801538;
  constexpr IOPWord START = 0x01000400;
  constexpr IOPWord FLAG = 0x08000000;
  constexpr IOPWord FORCE = 0x8000;
  constexpr IOPWord SUMMARY = 0x80000000;
  constexpr IOPWord DMAC_CAUSE = 8;
  constexpr IOPDMABank PRIMARY = IOPDMABank::Primary;
  constexpr IOPDMABank SECONDARY = IOPDMABank::Secondary;

  void requireState(const NekoSystem &system, const std::vector<std::uint8_t> &state)
  {
    const bool unchanged = system.saveState() == state;
    REQUIRE(unchanged);
  }
}

TEST_CASE("IOP DMA common controls retain words and decode implemented channels",
  "[iop][dma]")
{
  IOPInterruptController interrupts;
  IOPDMAController dma(interrupts);
  REQUIRE(dma.priority(PRIMARY) == 0);
  REQUIRE(dma.priority(SECONDARY) == 0);
  REQUIRE(dma.interruptStatus(PRIMARY) == 0);
  REQUIRE(dma.interruptStatus(SECONDARY) == 0);
  REQUIRE_FALSE(dma.channelEnabled(IOPDMAChannel::SIF1));
  REQUIRE(dma.channelPriority(IOPDMAChannel::SIF1) == 0);
  for (std::uint32_t priority = 0; priority < 8; ++priority)
  {
    dma.writePriority(SECONDARY, 0xffff0fff | (priority << 12));
    REQUIRE(dma.channelPriority(IOPDMAChannel::SIF1) == priority);
    REQUIRE_FALSE(dma.channelEnabled(IOPDMAChannel::SIF1));
    dma.writePriority(SECONDARY, 0xffff8fff | (priority << 12));
    REQUIRE(dma.channelEnabled(IOPDMAChannel::SIF1));
  }
  dma.writePriority(PRIMARY, 0xffffffff);
  REQUIRE(dma.priority(PRIMARY) == 0xffffffff);
  dma.writeInterrupt(PRIMARY, 0xff7f7fff);
  dma.writeInterrupt(SECONDARY, 0xff7f7fff);
  REQUIRE(dma.interruptStatus(PRIMARY) == 0x007f7fff);
  REQUIRE(dma.interruptStatus(SECONDARY) == 0x007f7fff);
  REQUIRE_FALSE(dma.interruptPending());
  REQUIRE(interrupts.status() == 0);
  REQUIRE_THROWS_AS(dma.priority(static_cast<IOPDMABank>(2)), std::out_of_range);
  REQUIRE_THROWS_AS(
    dma.signalChannelCompletion(static_cast<IOPDMAChannel>(8)), std::out_of_range);
  REQUIRE(interrupts.status() == 0);
}

TEST_CASE("IOP DMA completion and INTC acknowledgement remain independent",
  "[iop][dma]")
{
  IOPInterruptController interrupts;
  IOPDMAController dma(interrupts);
  dma.signalChannelCompletion(IOPDMAChannel::SIF1);
  REQUIRE(dma.interruptStatus(SECONDARY) == FLAG);
  REQUIRE(dma.interruptPending());
  REQUIRE(interrupts.status() == DMAC_CAUSE);
  REQUIRE_FALSE(interrupts.interruptPending());
  interrupts.writeMask(DMAC_CAUSE);
  interrupts.writeControl(1);
  REQUIRE(interrupts.interruptPending());
  interrupts.writeStatus(~DMAC_CAUSE);
  REQUIRE(interrupts.status() == 0);
  REQUIRE(dma.interruptPending());
  dma.writePriority(SECONDARY, 0xffffffff);
  REQUIRE(dma.interruptStatus(SECONDARY) == FLAG);
  REQUIRE(interrupts.status() == 0);
  dma.writeInterrupt(PRIMARY, 0);
  REQUIRE(interrupts.status() == DMAC_CAUSE);

  dma.writeInterrupt(SECONDARY, 0x00800000);
  REQUIRE(dma.interruptStatus(SECONDARY) == (FLAG | SUMMARY | 0x00800000));
  dma.writeInterrupt(SECONDARY, FLAG);
  REQUIRE(dma.interruptStatus(SECONDARY) == 0);
  REQUIRE_FALSE(dma.interruptPending());
  REQUIRE(interrupts.status() == DMAC_CAUSE);
  interrupts.writeStatus(~DMAC_CAUSE);
  REQUIRE(interrupts.status() == 0);

  for (const auto bank : {PRIMARY, SECONDARY})
  {
    dma.writeInterrupt(bank, FORCE);
    REQUIRE(dma.interruptStatus(bank) == (FORCE | SUMMARY));
    REQUIRE(interrupts.status() == DMAC_CAUSE);
    interrupts.writeStatus(~DMAC_CAUSE);
    REQUIRE(dma.interruptPending());
    REQUIRE(interrupts.status() == 0);
    dma.writeInterrupt(bank, FORCE);
    REQUIRE(interrupts.status() == DMAC_CAUSE);
    dma.writeInterrupt(bank, SUMMARY);
    REQUIRE(dma.interruptStatus(bank) == 0);
    REQUIRE_FALSE(dma.interruptPending());
    interrupts.writeStatus(~DMAC_CAUSE);
  }
  dma.writeInterrupt(PRIMARY, FORCE);
  dma.signalChannelCompletion(IOPDMAChannel::SIF1);
  interrupts.writeStatus(~DMAC_CAUSE);
  dma.writeInterrupt(SECONDARY, FLAG);
  REQUIRE(dma.interruptStatus(SECONDARY) == 0);
  REQUIRE(dma.interruptPending());
  REQUIRE(interrupts.status() == DMAC_CAUSE);
  dma.writeInterrupt(PRIMARY, 0);
  REQUIRE_FALSE(dma.interruptPending());
  interrupts.writeStatus(~DMAC_CAUSE);
  dma.signalChannelCompletion(IOPDMAChannel::SIF1);
  interrupts.writeStatus(~DMAC_CAUSE);
  dma.signalChannelCompletion(IOPDMAChannel::SIF1);
  REQUIRE(interrupts.status() == DMAC_CAUSE);
}

TEST_CASE("IOP DMA channel 10 validates starts active writes and local abort",
  "[iop][dma]")
{
  NekoSystem system;
  auto &channel = system.iopSIF1DMAC();
  REQUIRE(channel.memoryAddress() == 0);
  REQUIRE(channel.blockControl() == 0);
  REQUIRE(channel.channelControl() == 0);
  REQUIRE(channel.writeMemoryAddress(0xff123454) == IOPDMAWriteOutcome::Completed);
  REQUIRE(channel.memoryAddress() == 0x123454);
  REQUIRE(channel.writeMemoryAddress(0x123455) ==
    IOPDMAWriteOutcome::UnalignedAddress);
  REQUIRE(channel.memoryAddress() == 0x123454);
  REQUIRE(channel.writeBlockControl(0xabcd1234) == IOPDMAWriteOutcome::Completed);
  REQUIRE(channel.writeChannelControl(0x701) == IOPDMAWriteOutcome::Completed);
  for (const IOPWord control : {
    0x01000000u, 0x01000401u, 0x01000600u, 0x41000400u, 0x81000400u})
  {
    CAPTURE(control);
    const auto before = system.saveState();
    REQUIRE(channel.writeChannelControl(control) ==
      IOPDMAWriteOutcome::UnsupportedControl);
    requireState(system, before);
  }
  REQUIRE(channel.writeChannelControl(START | 0x100) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(channel.active());
  REQUIRE_FALSE(system.iopDMAController().channelEnabled(IOPDMAChannel::SIF1));
  const auto active = system.saveState();
  REQUIRE(channel.writeChannelControl(START | 0x100) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(channel.writeChannelControl(START) == IOPDMAWriteOutcome::ActiveTransfer);
  REQUIRE(channel.writeChannelControl(0x400) == IOPDMAWriteOutcome::ActiveTransfer);
  REQUIRE(channel.writeMemoryAddress(0x4000) == IOPDMAWriteOutcome::ActiveTransfer);
  REQUIRE(channel.writeBlockControl(0) == IOPDMAWriteOutcome::ActiveTransfer);
  requireState(system, active);
  system.iopDMAController().writePriority(SECONDARY, 0x8000);
  system.runMasterCycles(65);
  REQUIRE(channel.active());
  REQUIRE(channel.memoryAddress() == 0x123454);
  REQUIRE(channel.blockControl() == 0xabcd1234);
  REQUIRE(system.iopDMAController().interruptStatus(SECONDARY) == 0);
  REQUIRE(system.iopInterruptController().status() == 0);
  REQUIRE(system.sif().pushSIF1Quadword({{1, 2, 3, 4}}) ==
    SIFTransferOutcome::Completed);
  system.iopDMAController().signalChannelCompletion(IOPDMAChannel::SIF1);
  const auto sifHash = system.sifStateHash();
  REQUIRE(channel.writeChannelControl(0x500) == IOPDMAWriteOutcome::Completed);
  REQUIRE_FALSE(channel.active());
  REQUIRE(channel.memoryAddress() == 0x123454);
  REQUIRE(channel.blockControl() == 0xabcd1234);
  REQUIRE(system.sifStateHash() == sifHash);
  REQUIRE(system.iopDMAController().interruptStatus(SECONDARY) == FLAG);
  REQUIRE(channel.writeChannelControl(START) == IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopDMAController().interruptStatus(SECONDARY) == FLAG);
}

TEST_CASE("IOP DMA MMIO admits exact words and rejects unsupported boundaries atomically",
  "[iop][dma][bus]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  for (const IOPAddress address : {DPCR, DICR, DPCR2, DICR2, MADR, BCR, CHCR})
  {
    CAPTURE(address);
    REQUIRE(bus.regionForRange(address, 4) == IOPBusRegion::Mmio);
    REQUIRE(bus.read32(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(address).value == 0);
    const auto before = system.saveState();
    for (IOPAddress lane = 0; lane < 4; ++lane)
    {
      REQUIRE(bus.read8(address + lane).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write8(address + lane, 0xff).status == IOPBusStatus::Unmapped);
    }
    for (IOPAddress lane : {0u, 2u})
    {
      REQUIRE(bus.read16(address + lane).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write16(address + lane, 0xffff).status == IOPBusStatus::Unmapped);
    }
    for (std::uint8_t lanes = 1; lanes < 15; ++lanes)
    {
      REQUIRE(bus.writeMasked32(address, 0xffffffff, lanes).status ==
        IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.read32(address + 1).status == IOPBusStatus::Misaligned);
    REQUIRE(bus.write32(address + 1, 0).status == IOPBusStatus::Misaligned);
    requireState(system, before);
  }
  const auto before = system.saveState();
  for (const IOPAddress address : {
    0x1f8010f8u, 0x1f80151cu,
    0x1f80153cu, 0x1f801560u, 0x1f801578u, 0x1f80157cu, 0x1f801580u})
  {
    REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(address, 0xffffffff).status == IOPBusStatus::Unmapped);
  }
  requireState(system, before);
  for (const auto bank : {PRIMARY, SECONDARY})
  {
    const auto priorityAddress = bank == PRIMARY ? DPCR : DPCR2;
    const auto interruptAddress = bank == PRIMARY ? DICR : DICR2;
    REQUIRE(bus.write32(priorityAddress, 0x87654321).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.read32(priorityAddress).value == 0x87654321);
    REQUIRE(system.iopDMAController().priority(bank) == 0x87654321);
    REQUIRE(bus.write32(interruptAddress, FORCE | 0x321).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.read32(interruptAddress).value == (SUMMARY | FORCE | 0x321));
    REQUIRE(system.iopDMAController().interruptStatus(bank) ==
      (SUMMARY | FORCE | 0x321));
    REQUIRE(bus.write32(interruptAddress, 0).status == IOPBusStatus::Completed);
  }
  REQUIRE(bus.writeMasked32(BCR, 0xabcd1234, 15).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(BCR).value == 0xabcd1234);
  REQUIRE(bus.write32(MADR, 1).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.write32(CHCR, 0x01000000).status == IOPBusStatus::Unmapped);
  REQUIRE(system.iopCore().writeData32(0xbf801530, 0xff001234).outcome ==
    IOPAccessOutcome::Completed);
  REQUIRE(system.iopCore().readData32(0xbf801530).value == 0x1234);
  REQUIRE(bus.write32(CHCR, START).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(BCR, 0).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.write32(CHCR, 0x400).status == IOPBusStatus::Completed);
  IOPBus unattached;
  REQUIRE(unattached.read32(DPCR).status == IOPBusStatus::Unmapped);
  REQUIRE(unattached.write32(CHCR, START).status == IOPBusStatus::Unmapped);
  REQUIRE_THROWS_AS(
    unattached.attachDMA(nullptr, &system.iopSIF1DMAC()), std::invalid_argument);
  REQUIRE_THROWS_AS(
    unattached.attachDMA(&system.iopDMAController(), nullptr), std::invalid_argument);
  unattached.attachDMA(&system.iopDMAController(), &system.iopSIF1DMAC());
  REQUIRE_THROWS_AS(
    unattached.attachDMA(&system.iopDMAController(), &system.iopSIF1DMAC()),
    std::logic_error);
}

TEST_CASE("IOP DMA force reaches real cause 3 and the existing IOP exception owner",
  "[iop][dma][system]")
{
  NekoSystem system;
  REQUIRE(system.iopBus().write32(0, 0x34010401).status ==
    IOPBusStatus::Completed); // ori r1, r0, IEc | IM2
  REQUIRE(system.iopBus().write32(4, 0x40816000).status ==
    IOPBusStatus::Completed); // mtc0 r1, Status
  system.iopCore().startExecution(0xa0000000);
  system.runMasterCycles(9);
  system.iopInterruptController().writeMask(DMAC_CAUSE);
  system.iopInterruptController().writeControl(1);
  REQUIRE(system.iopBus().write32(DICR, FORCE).status == IOPBusStatus::Completed);
  system.runMasterCycles(7);
  REQUIRE(system.iopCore().programCounter() == 0xa0000008);
  system.clockMasterCycle();
  REQUIRE(system.iopCore().exception().kind == IOPException::Interrupt);
  REQUIRE(system.iopCore().exceptionProgramCounter() == 0xa0000008);
  REQUIRE(system.iopCore().retiredInstructions() == 2);
  REQUIRE(system.iopInterruptController().status() == DMAC_CAUSE);
}

TEST_CASE("IOP DMA persistence preserves independent acknowledgement without republication",
  "[iop][dma][save-state]")
{
  NekoSystem original;
  auto &dma = original.iopDMAController();
  auto &channel = original.iopSIF1DMAC();
  const auto emptyHash = original.iopStateHash();
  dma.writePriority(PRIMARY, 0x76543210);
  dma.writePriority(SECONDARY, 0xffffbfff);
  REQUIRE(channel.writeMemoryAddress(0x1234) == IOPDMAWriteOutcome::Completed);
  REQUIRE(channel.writeBlockControl(0x12345678) == IOPDMAWriteOutcome::Completed);
  REQUIRE(channel.writeChannelControl(START) == IOPDMAWriteOutcome::Completed);
  dma.writeInterrupt(PRIMARY, FORCE);
  dma.signalChannelCompletion(IOPDMAChannel::SIF1);
  dma.writeInterrupt(SECONDARY, 0x00800000);
  original.iopInterruptController().writeMask(0x0a);
  original.iopInterruptController().writeControl(1);
  original.iopInterruptController().assertSource(IOPInterruptSource::SBUS);
  original.iopInterruptController().writeStatus(~DMAC_CAUSE);
  REQUIRE(original.iopInterruptController().status() == 2);
  REQUIRE(original.iopStateHash() != emptyHash);
  original.runMasterCycles(9);
  original.startTrace();
  NekoSystem restored;
  restored.iopDMAController().writeInterrupt(SECONDARY, FORCE);
  REQUIRE(restored.iopSIF1DMAC().writeChannelControl(0x701) ==
    IOPDMAWriteOutcome::Completed);
  restored.startTrace();
  restored.loadState(original.saveState());
  restored.clearTrace();
  IOPTraceTest::requireMachine(restored, original);
  REQUIRE(restored.iopDMAController().interruptPending());
  REQUIRE(restored.iopInterruptController().status() == 2);
  original.runMasterCycles(16);
  restored.runMasterCycles(16);
  IOPTraceTest::requireMachine(restored, original);
  IOPTraceTest::requireEvents(restored.trace(), original.trace());
  REQUIRE(restored.iopInterruptController().status() == 2);
  REQUIRE(restored.iopBus().write32(DICR2, FLAG).status == IOPBusStatus::Completed);
  REQUIRE(restored.iopInterruptController().status() == 0x0a);
  restored.iopInterruptController().writeStatus(~DMAC_CAUSE);
  REQUIRE(restored.iopBus().write32(DICR, 0).status == IOPBusStatus::Completed);
  REQUIRE_FALSE(restored.iopDMAController().interruptPending());
  REQUIRE(restored.iopInterruptController().status() == 2);
  restored.iopDMAController().signalChannelCompletion(IOPDMAChannel::SIF1);
  REQUIRE(restored.iopInterruptController().status() == 0x0a);
  restored.reset();
  NekoSystem fresh;
  IOPTraceTest::requireMachine(restored, fresh);
  REQUIRE(restored.iopBus().write32(DICR, FORCE).status == IOPBusStatus::Completed);
  REQUIRE(restored.iopInterruptController().status() == DMAC_CAUSE);
}

TEST_CASE("IOP DMA malformed persistence rejects before destination mutation",
  "[iop][dma][save-state]")
{
  struct InvalidField
  {
    const char *path;
    std::uint64_t value;
  };
  NekoSystem source;
  NekoSystem destination;
  destination.iopDMAController().writeInterrupt(PRIMARY, FORCE);
  destination.runMasterCycles(9);
  destination.startTrace();
  const auto before = destination.saveState();
  const auto traceHash = destination.traceHash();
  for (const auto field : {
    InvalidField{"system.iop.dmaController.banks[0].control", 0x01000000},
    InvalidField{"system.iop.dmaController.banks[0].flags", 1},
    InvalidField{"system.iop.dmaController.banks[1].flags", 2},
    InvalidField{"system.iop.sif1DMA.memoryAddress", 0x01000000},
    InvalidField{"system.iop.sif1DMA.memoryAddress", 1},
    InvalidField{"system.iop.sif1DMA.channelControl", 0x01000000},
    InvalidField{"system.iop.sif1DMA.channelControl", 0x40000000}})
  {
    CAPTURE(field.path);
    CAPTURE(field.value);
    SaveStateMutation corrupt(source.saveState());
    corrupt.writeScalar(field.path, field.value);
    corrupt.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(corrupt.bytes()), std::invalid_argument);
    requireState(destination, before);
    REQUIRE(destination.traceEnabled());
    REQUIRE(destination.traceHash() == traceHash);
  }
}

TEST_CASE("IOP DMA canonical registers independently contribute to IOP hashes",
  "[iop][dma][save-state]")
{
  NekoSystem system;
  const auto hash = system.iopStateHash();
  for (const auto bank : {PRIMARY, SECONDARY})
  {
    auto &dma = system.iopDMAController();
    dma.writePriority(bank, 1);
    REQUIRE(system.iopStateHash() != hash);
    dma.writePriority(bank, 0);
    REQUIRE(system.iopStateHash() == hash);
    dma.writeInterrupt(bank, 1);
    REQUIRE(system.iopStateHash() != hash);
    dma.writeInterrupt(bank, 0);
    REQUIRE(system.iopStateHash() == hash);
  }
  auto &channel = system.iopSIF1DMAC();
  REQUIRE(channel.writeMemoryAddress(4) == IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopStateHash() != hash);
  REQUIRE(channel.writeMemoryAddress(0) == IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopStateHash() == hash);
  REQUIRE(channel.writeBlockControl(1) == IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopStateHash() != hash);
  REQUIRE(channel.writeBlockControl(0) == IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopStateHash() == hash);
  REQUIRE(channel.writeChannelControl(0x100) == IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopStateHash() != hash);
  REQUIRE(channel.writeChannelControl(0) == IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopStateHash() == hash);
  system.iopDMAController().signalChannelCompletion(IOPDMAChannel::SIF1);
  system.iopInterruptController().writeStatus(~DMAC_CAUSE);
  REQUIRE(system.iopStateHash() != hash);
  system.iopDMAController().writeInterrupt(SECONDARY, FLAG);
  REQUIRE(system.iopStateHash() == hash);
}
