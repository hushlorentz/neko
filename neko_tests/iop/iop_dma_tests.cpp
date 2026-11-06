#include <cstdint>
#include <iterator>
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
  constexpr IOPAddress AUXILIARY = 0x1f801578;
  constexpr IOPAddress DPCR3 = 0x1f8015f0;
  constexpr IOPAddress SPU_MADR[] = {0x1f8010c0, 0x1f801500};
  constexpr IOPAddress CHANNEL_INITIALIZATION_REGISTERS[] = {
    0x1f801080, 0x1f801084, 0x1f801088,
    0x1f801090, 0x1f801094, 0x1f801098,
    0x1f8010a0, 0x1f8010a4, 0x1f8010a8,
    0x1f8010b0, 0x1f8010b4, 0x1f8010b8,
    0x1f8010c4, 0x1f8010c8,
    0x1f8010d0, 0x1f8010d4, 0x1f8010d8,
    0x1f8010e0, 0x1f8010e4, 0x1f8010e8,
    0x1f801504, 0x1f801508,
    0x1f801510, 0x1f801514, 0x1f801518,
    0x1f801540, 0x1f801544, 0x1f801548,
    0x1f801550, 0x1f801554, 0x1f801558
  };
  constexpr IOPAddress SUPPLEMENTAL_INITIALIZATION_REGISTERS[] = {
    0x1f8010cc, 0x1f801560, 0x1f801564, 0x1f801568
  };
  const std::vector<IOPAddress> INITIALIZATION_REGISTERS = []()
  {
    std::vector<IOPAddress> addresses(
      std::begin(CHANNEL_INITIALIZATION_REGISTERS),
      std::end(CHANNEL_INITIALIZATION_REGISTERS));
    addresses.insert(addresses.end(), std::begin(SUPPLEMENTAL_INITIALIZATION_REGISTERS),
      std::end(SUPPLEMENTAL_INITIALIZATION_REGISTERS));
    return addresses;
  }();
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

  void requireInitializationStore(
    IOPAddress physical,
    IOPAddress target,
    IOPWord value,
    IOPWord store,
    IOPStopReason expectedStop)
  {
    CAPTURE(target);
    CAPTURE(value);
    CAPTURE(store);
    IOPInterruptController interrupts;
    IOPDMAController dma(interrupts);
    IOPSIF1DMAChannel channel;
    IOPBus bus;
    bus.attachDMA(&dma, &channel);
    IOPCore core;
    core.attachBus(&bus);
    const IOPWord program[] = {
      0x3c010000 | (target >> 16),
      0x34210000 | (target & 0xffff),
      0x24020000 | value,
      0x10000002, // beq r0, r0, +2
      store
    };
    IOPAddress address = 0;
    for (IOPWord word : program)
    {
      REQUIRE(bus.write32(address, word).status == IOPBusStatus::Completed);
      address += 4;
    }
    core.startExecution(0xa0000000);
    for (unsigned step = 0; step < 5; ++step)
    {
      core.stepInstruction();
    }
    const bool completed = expectedStop == IOPStopReason::None;
    REQUIRE(core.exception().kind == IOPException::None);
    REQUIRE(core.generalRegister(2) == value);
    REQUIRE(core.retiredInstructions() == (completed ? 5 : 4));
    REQUIRE(core.programCounter() == (completed ? 0xa0000018 : 0xa0000010));
    REQUIRE(core.branchContinuation().active == !completed);
    REQUIRE(core.stopReason() == expectedStop);
    if (!completed)
    {
      REQUIRE(core.storeFailure().access.virtualAddress == target + (store & 3));
      REQUIRE(core.storeFailure().access.physicalAddress == physical + (store & 3));
      REQUIRE(core.storeFailure().access.outcome == IOPAccessOutcome::Unmapped);
    }
    core.startExecution(target);
    REQUIRE(core.fetchInstruction().outcome == IOPAccessOutcome::Unmapped);
  }
}

TEST_CASE("IOP SPU DMA addresses retain normalized independent programming",
  "[iop][dma][spu-madr]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  const auto initialHash = system.iopStateHash();
  REQUIRE(bus.write32(0x1f8010c0, 0x0004fa00).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(0x1f8010c0).value == 0x0004fa00);
  REQUIRE(bus.read32(0x1f801500).value == 0);
  REQUIRE(system.iopStateHash() != initialHash);
  REQUIRE(bus.write32(0x1f801500, 0xabfffffc).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(0x1f801500).value == 0x00fffffc);
  REQUIRE(bus.read32(0x1f8010c0).value == 0x0004fa00);
  const auto programmed = system.saveState();
  for (IOPWord low = 1; low < 4; ++low)
  {
    REQUIRE(bus.write32(0x1f8010c0, 0xff04fa00 | low).status ==
      IOPBusStatus::Unmapped);
    REQUIRE(bus.writeMasked32(0x1f801500, 0x00fffffc | low, 15).status ==
      IOPBusStatus::Unmapped);
  }
  requireState(system, programmed);
  REQUIRE(bus.writeMasked32(0x1f8010c0, 0xff04fa00, 15).status ==
    IOPBusStatus::Completed);
  requireState(system, programmed);
  REQUIRE(bus.write32(0x1f8010c0, 0).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(0x1f8010c0).value == 0);
  REQUIRE(bus.read32(0x1f801500).value == 0x00fffffc);
}

TEST_CASE("IOP SPU DMA programming preserves inactive engines and other owners",
  "[iop][dma][spu-madr]")
{
  NekoSystem system;
  auto &dma = system.iopDMAController();
  auto &bus = system.iopBus();
  dma.writePriority(PRIMARY, 0xffffffff);
  dma.writePriority(SECONDARY, 0xffffffff);
  dma.writeAuxiliaryControl(0x89abcdef);
  dma.writeInterrupt(PRIMARY, FORCE);
  dma.signalChannelCompletion(IOPDMAChannel::SIF1);
  REQUIRE(bus.write16(0x1f9001b0, 1).status == IOPBusStatus::Completed);
  REQUIRE(bus.write16(0x1f9005b0, 2).status == IOPBusStatus::Completed);
  REQUIRE(bus.write16(0x1f9001a8, 0x1234).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x100, 0x76543210).status == IOPBusStatus::Completed);
  SaveStateMutation expected(system.saveState());
  for (std::size_t core = 0; core < 2; ++core)
  {
    CAPTURE(core);
    for (IOPWord high = 0; high < 256; ++high)
    {
      const IOPWord value = 0x4fa00 + static_cast<IOPWord>(core * 4);
      REQUIRE(dma.writeSPUMemoryAddress(core, (high << 24) | value) ==
        IOPDMAWriteOutcome::Completed);
      REQUIRE(dma.spuMemoryAddress(core) == value);
      REQUIRE(bus.read32(SPU_MADR[core]).value == value);
    }
    expected.writeScalar("system.iop.dmaController.spuMemoryAddress[" +
      std::to_string(core) + "].value", dma.spuMemoryAddress(core));
    for (IOPAddress offset : {4u, 8u})
    {
      REQUIRE(bus.write32(SPU_MADR[core] + offset, 0).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(SPU_MADR[core] + offset, 1).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write32(SPU_MADR[core] + offset, 0x01000000).status ==
        IOPBusStatus::Unmapped);
      REQUIRE(bus.read32(SPU_MADR[core] + offset).value == 0);
    }
  }
  expected.refreshChecksum();
  requireState(system, expected.bytes());
  REQUIRE_THROWS_AS(dma.spuMemoryAddress(2), std::out_of_range);
  REQUIRE_THROWS_AS(dma.writeSPUMemoryAddress(2, 1), std::out_of_range);
  system.runMasterCycles(80);
  REQUIRE(bus.read32(0x100).value == 0x76543210);
  bus.reset();
  system.iopCore().reset();
  REQUIRE(dma.spuMemoryAddress(0) == 0x4fa00);
  REQUIRE(dma.spuMemoryAddress(1) == 0x4fa04);
  REQUIRE(bus.read16(0x1f9001a8).value == 0x1234);
  REQUIRE(bus.read16(0x1f900344).value == 0);
  REQUIRE(bus.read16(0x1f900744).value == 0);
  REQUIRE(dma.interruptStatus(PRIMARY) == (FORCE | SUMMARY));
  REQUIRE(dma.interruptStatus(SECONDARY) == FLAG);
  system.reset();
  for (const auto address : SPU_MADR)
  {
    REQUIRE(system.iopBus().read32(address).value == 0);
  }
}

TEST_CASE("IOP SPU DMA addresses reject excluded transports without mutation",
  "[iop][dma][spu-madr][bus]")
{
  IOPBus detached;
  NekoSystem system;
  auto &bus = system.iopBus();
  for (const auto address : SPU_MADR)
  {
    REQUIRE(detached.read32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(detached.write32(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(address, 0x4fa00).status == IOPBusStatus::Completed);
  }
  const auto before = system.saveState();
  for (const auto address : SPU_MADR)
  {
    for (std::uint8_t mask = 1; mask < 15; ++mask)
    {
      REQUIRE(bus.writeMasked32(address, 0, mask).status == IOPBusStatus::Unmapped);
    }
    for (IOPAddress lane = 0; lane < 4; ++lane)
    {
      REQUIRE(bus.read8(address + lane).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write8(address + lane, 0).status == IOPBusStatus::Unmapped);
      const auto half = lane % 2 == 0 ? IOPBusStatus::Unmapped : IOPBusStatus::Misaligned;
      REQUIRE(bus.read16(address + lane).status == half);
      REQUIRE(bus.write16(address + lane, 0).status == half);
      if (lane != 0)
      {
        REQUIRE(bus.read32(address + lane).status == IOPBusStatus::Misaligned);
        REQUIRE(bus.write32(address + lane, 0).status == IOPBusStatus::Misaligned);
      }
    }
    REQUIRE(bus.readInstruction32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readDMAC32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(address, 0).status == IOPBusStatus::Unmapped);
    std::uint8_t byte = 0;
    REQUIRE(bus.inspectPhysical(address, &byte, 1) == IOPBusStatus::Unmapped);
    REQUIRE(bus.loadPhysical(address, &byte, 1) == IOPBusStatus::Unmapped);
    std::uint32_t eeValue = 0;
    REQUIRE_FALSE(system.eeBus().readData32(address, &eeValue));
    REQUIRE_FALSE(system.eeBus().writeData32(address, 0));
    REQUIRE(bus.write32(address + 0x100000, 0).status == IOPBusStatus::Unmapped);
  }
  requireState(system, before);
}

TEST_CASE("IOP SPU DMA pending word stores and loads restore canonically",
  "[iop][dma][spu-madr][save-state]")
{
  for (const auto physical : SPU_MADR)
  for (const IOPAddress segment : {0u, 0x80000000u, 0xa0000000u})
  for (const std::uint64_t cut : {32u, 40u, 41u, 48u, 56u})
  {
    const auto target = physical | segment;
    CAPTURE(target);
    CAPTURE(cut);
    NekoSystem original;
    const IOPWord program[] = {
      0x3c010000 | (target >> 16), 0x34210000 | (target & 0xffff),
      0x3c02ab04, 0x3442fa00, 0x10000002, 0xac220000,
      0, 0x8c230000, 0, 0x1000ffff, 0
    };
    IOPAddress address = 0;
    for (const auto word : program)
    {
      REQUIRE(original.iopBus().write32(address, word).status ==
        IOPBusStatus::Completed);
      address += 4;
    }
    original.iopCore().startExecution(0xa0000000);
    original.runMasterCycles(cut);
    const auto checkpoint = original.saveState();
    NekoSystem restored;
    NekoSystem plain;
    restored.loadState(checkpoint);
    plain.loadState(checkpoint);
    REQUIRE(restored.iopBus().read32(physical).value ==
      original.iopBus().read32(physical).value);
    original.startTrace();
    restored.startTrace();
    original.runMasterCycles(96 - cut);
    restored.runMasterCycles(96 - cut);
    plain.runMasterCycles(96 - cut);
    REQUIRE(original.iopCore().stopReason() == IOPStopReason::None);
    REQUIRE(original.iopCore().generalRegister(3) == 0x4fa00);
    REQUIRE(original.iopBus().read32(physical).value == 0x4fa00);
    REQUIRE(restored.iopBus().write32(physical, 0xab04fa00).status ==
      IOPBusStatus::Completed);
    IOPTraceTest::requireEvents(restored.trace(), original.trace());
    IOPTraceTest::requireMachine(restored, original);
    IOPTraceTest::requireMachine(plain, original);
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

TEST_CASE("IOP DMA auxiliary word retains every bit without assigned effects",
  "[iop][dma][auxiliary]")
{
  IOPBus detached;
  REQUIRE(detached.read32(AUXILIARY).status == IOPBusStatus::Unmapped);
  REQUIRE(detached.write32(AUXILIARY, 1).status == IOPBusStatus::Unmapped);
  NekoSystem system;
  auto &bus = system.iopBus();
  REQUIRE(bus.read32(AUXILIARY).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(AUXILIARY).value == 0);
  system.iopDMAController().writePriority(SECONDARY, 0x8800);
  const auto initial = system.iopStateHash();
  const auto eeHash = system.eeStateHash();
  const auto sifHash = system.sifStateHash();
  for (unsigned bit = 0; bit < 32; ++bit)
  {
    CAPTURE(bit);
    const IOPWord value = IOPWord(1) << bit;
    REQUIRE(bus.write32(AUXILIARY, value).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(AUXILIARY).value == value);
    REQUIRE(system.iopStateHash() != initial);
    REQUIRE(system.iopDMAController().priority(PRIMARY) == 0);
    REQUIRE(system.iopDMAController().priority(SECONDARY) == 0x8800);
    REQUIRE(system.iopDMAController().channelEnabled(IOPDMAChannel::SIF0));
    REQUIRE(system.iopDMAController().channelEnabled(IOPDMAChannel::SIF1));
    REQUIRE_FALSE(system.iopDMAController().interruptPending());
    REQUIRE(system.iopInterruptController().status() == 0);
  }
  REQUIRE(bus.writeMasked32(AUXILIARY, 0, 15).status ==
    IOPBusStatus::Completed);
  REQUIRE(system.iopStateHash() == initial);
  REQUIRE(system.eeStateHash() == eeHash);
  REQUIRE(system.sifStateHash() == sifHash);
  REQUIRE(bus.readDMAC32(AUXILIARY).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.writeDMAC32(AUXILIARY, 1).status == IOPBusStatus::Unmapped);
  REQUIRE(system.iopStateHash() == initial);
  system.reset();
  REQUIRE(system.iopBus().read32(AUXILIARY).value == 0);
}

TEST_CASE("IOP DMA auxiliary word supports ordinary guest LW and SW",
  "[iop][dma][auxiliary]")
{
  NekoSystem system;
  const IOPWord program[] = {
    0x3c01bf80, // lui r1, 0xbf80
    0x34211578, // ori r1, r1, 0x1578
    0x24021234, // addiu r2, r0, 0x1234
    0xac220000, // sw r2, 0(r1)
    0x8c230000, // lw r3, 0(r1)
    0
  };
  IOPAddress address = 0;
  for (const IOPWord word : program)
  {
    REQUIRE(system.iopBus().write32(address, word).status ==
      IOPBusStatus::Completed);
    address += 4;
  }
  system.iopCore().startExecution(0x80000000);
  for (unsigned step = 0; step < 6; ++step)
  {
    system.iopCore().stepInstruction();
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
  }
  REQUIRE(system.iopCore().exception().kind == IOPException::None);
  REQUIRE(system.iopCore().retiredInstructions() == 6);
  REQUIRE(system.iopCore().generalRegister(3) == 0x1234);
  REQUIRE(system.iopBus().read32(AUXILIARY).value == 0x1234);
}

TEST_CASE("IOP DPCR3 initialization requires the common DMA owner and changes no state",
  "[iop][dma][dpcr3]")
{
  IOPBus detached;
  REQUIRE(detached.write32(DPCR3, 0x777).status == IOPBusStatus::Unmapped);
  REQUIRE(detached.writeMasked32(DPCR3, 0x777, 0xf).status ==
    IOPBusStatus::Unmapped);

  NekoSystem system;
  auto &dma = system.iopDMAController();
  dma.writePriority(PRIMARY, 0x12345678);
  dma.writePriority(SECONDARY, 0x8800);
  dma.writeAuxiliaryControl(0x89abcdef);
  dma.writeInterrupt(SECONDARY, 0x00800000);
  dma.signalChannelCompletion(IOPDMAChannel::SIF1);
  system.iopInterruptController().writeStatus(~DMAC_CAUSE);
  REQUIRE(dma.interruptPending());
  REQUIRE(system.iopInterruptController().status() == 0);
  REQUIRE(system.iopSIF1DMAC().writeMemoryAddress(0x200) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopSIF1DMAC().writeChannelControl(START) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(system.sif().pushSIF1Quadword({{1, 2, 3, 4}}) ==
    SIFTransferOutcome::Completed);
  const auto before = system.saveState();
  const auto iopHash = system.iopStateHash();
  const auto sifHash = system.sifStateHash();
  system.startTrace();
  REQUIRE(dma.writeDPCR3Initialization(0x777) == IOPDMAWriteOutcome::Completed);
  REQUIRE(dma.writeDPCR3Initialization(0) == IOPDMAWriteOutcome::UnsupportedControl);
  for (unsigned repeat = 0; repeat < 2; ++repeat)
  {
    REQUIRE(system.iopBus().write32(DPCR3, 0x777).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.iopBus().writeMasked32(DPCR3, 0x777, 0xf).status ==
      IOPBusStatus::Completed);
  }
  REQUIRE(system.iopStateHash() == iopHash);
  REQUIRE(system.sifStateHash() == sifHash);
  requireState(system, before);
  REQUIRE(system.trace().empty());
}

TEST_CASE("IOP DPCR3 rejects other values widths masks and neighboring addresses",
  "[iop][dma][dpcr3][bus]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  const auto before = system.saveState();
  for (unsigned bit = 0; bit < 32; ++bit)
  {
    CAPTURE(bit);
    REQUIRE(bus.write32(DPCR3, 0x777 ^ (IOPWord{1} << bit)).status ==
      IOPBusStatus::Unmapped);
  }
  for (IOPWord value : {0u, 0xffffffffu, 0x77777777u})
  {
    REQUIRE(bus.writeMasked32(DPCR3, value, 0xf).status ==
      IOPBusStatus::Unmapped);
  }
  for (std::uint8_t mask = 1; mask < 0xf; ++mask)
  {
    REQUIRE(bus.writeMasked32(DPCR3, 0x777, mask).status == IOPBusStatus::Unmapped);
  }
  for (IOPAddress lane = 0; lane < 4; ++lane)
  {
    CAPTURE(lane);
    REQUIRE(bus.read8(DPCR3 + lane).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write8(DPCR3 + lane, 0x77).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read32(DPCR3 + lane).status ==
      (lane == 0 ? IOPBusStatus::Unmapped : IOPBusStatus::Misaligned));
    const auto status = lane == 0 ? IOPBusStatus::Completed : IOPBusStatus::Misaligned;
    REQUIRE(bus.write32(DPCR3 + lane, 0x777).status == status);
    REQUIRE(bus.writeMasked32(DPCR3 + lane, 0x777, 0xf).status == status);
    REQUIRE(bus.read16(DPCR3 + lane).status ==
      ((lane & 1) != 0 ? IOPBusStatus::Misaligned : IOPBusStatus::Unmapped));
    REQUIRE(bus.write16(DPCR3 + lane, 0x777).status ==
      ((lane & 1) != 0 ? IOPBusStatus::Misaligned : IOPBusStatus::Unmapped));
  }
  for (IOPAddress address : {DPCR3 - 4, DPCR3 + 4, 0x9f8015f0u, 0xbf8015f0u})
  {
    CAPTURE(address);
    REQUIRE(bus.regionForRange(address, 1) == IOPBusRegion::Unmapped);
    REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(address, 0x777).status == IOPBusStatus::Unmapped);
  }
  for (std::uint8_t mask : {0u, 0x10u, 0xffu})
  {
    REQUIRE_THROWS_AS(bus.writeMasked32(DPCR3 + 1, 0x777, mask),
      std::invalid_argument);
  }
  REQUIRE(bus.regionForRange(DPCR3, 4) == IOPBusRegion::Mmio);
  REQUIRE(bus.regionForRange(DPCR3, 5) == IOPBusRegion::Unmapped);
  REQUIRE(bus.readDMAC32(DPCR3).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.writeDMAC32(DPCR3, 0x777).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.dmaWriteStatus(DPCR3) == IOPBusStatus::Unmapped);
  std::uint8_t byte = 0xa5;
  REQUIRE(bus.loadPhysical(DPCR3, &byte, 1) == IOPBusStatus::Unmapped);
  REQUIRE(bus.inspectPhysical(DPCR3, &byte, 1) == IOPBusStatus::Unmapped);
  REQUIRE(byte == 0xa5);
  requireState(system, before);
}

TEST_CASE("IOP DMA disabled channel words read zero before and after initialization",
  "[iop][dma][channel-read][bus]")
{
  IOPBus detached;
  NekoSystem system;
  auto &bus = system.iopBus();
  for (const IOPAddress address : CHANNEL_INITIALIZATION_REGISTERS)
  {
    CAPTURE(address);
    REQUIRE(detached.read32(address).status == IOPBusStatus::Unmapped);
    auto read = bus.read32(address);
    REQUIRE(read.status == IOPBusStatus::Completed);
    REQUIRE(read.value == 0);
    REQUIRE(bus.write32(address, 1).status == IOPBusStatus::Unmapped);
    for (unsigned repeat = 0; repeat < 2; ++repeat)
    {
      REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Completed);
      REQUIRE(bus.writeMasked32(address, 0, 0xf).status == IOPBusStatus::Completed);
      read = bus.read32(address);
      REQUIRE(read.status == IOPBusStatus::Completed);
      REQUIRE(read.value == 0);
    }
  }
}

TEST_CASE("IOP DMA register initialization and channel reads preserve other owners",
  "[iop][dma][channel-read][channel-init][supplemental-init][channel0-init][channel1-init][channel2-init][channel3-init]")
{
  IOPBus detached;
  for (IOPAddress address : INITIALIZATION_REGISTERS)
  {
    REQUIRE(detached.write32(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(detached.writeMasked32(address, 0, 0xf).status == IOPBusStatus::Unmapped);
  }

  NekoSystem system;
  auto &dma = system.iopDMAController();
  dma.writePriority(PRIMARY, 0x89abcdef);
  dma.writePriority(SECONDARY, 0x8800);
  dma.writeAuxiliaryControl(0x12345678);
  dma.writeInterrupt(SECONDARY, 0x00800000);
  dma.signalChannelCompletion(IOPDMAChannel::SIF1);
  system.iopInterruptController().writeStatus(~DMAC_CAUSE);
  REQUIRE(dma.interruptPending());
  REQUIRE(system.iopInterruptController().status() == 0);
  REQUIRE(system.iopSIF1DMAC().writeMemoryAddress(0x200) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopSIF1DMAC().writeChannelControl(START) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(system.sif().pushSIF1Quadword({{1, 2, 3, 4}}) ==
    SIFTransferOutcome::Completed);
  const IOPWord descriptor[] = {0xc0002000, 4, 0xdead, 0xbeef, 0xf00d, 0xcafe};
  for (std::size_t index = 0; index < sizeof(descriptor) / sizeof(descriptor[0]); ++index)
  {
    REQUIRE(system.iopBus().write32(0x1000 + static_cast<IOPAddress>(index * 4),
      descriptor[index]).status == IOPBusStatus::Completed);
  }
  REQUIRE(system.iopSIF0DMAC().writeTagAddress(0x1000) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(system.iopSIF0DMAC().writeChannelControl(0x01000401) ==
    IOPDMAWriteOutcome::Completed);
  REQUIRE(dma.clockSIF(system.iopSIF0DMAC(), system.iopBus(), system.sif()).outcome ==
    IOPDMABeatOutcome::Accepted);
  REQUIRE(system.iopSIF0DMAC().remainingWords() == 4);
  const auto before = system.saveState();
  system.startTrace();
  REQUIRE(dma.writeChannelRegisterInitialization(0) == IOPDMAWriteOutcome::Completed);
  REQUIRE(dma.writeSupplementalInitialization(0) == IOPDMAWriteOutcome::Completed);
  for (IOPAddress address : INITIALIZATION_REGISTERS)
  {
    CAPTURE(address);
    for (unsigned repeat = 0; repeat < 2; ++repeat)
    {
      REQUIRE(system.iopBus().write32(address, 0).status == IOPBusStatus::Completed);
      REQUIRE(system.iopBus().writeMasked32(address, 0, 0xf).status ==
        IOPBusStatus::Completed);
    }
  }
  for (const IOPAddress address : CHANNEL_INITIALIZATION_REGISTERS)
  {
    CAPTURE(address);
    const auto read = system.iopBus().read32(address);
    REQUIRE(read.status == IOPBusStatus::Completed);
    REQUIRE(read.value == 0);
  }
  REQUIRE(system.iopBus().read32(CHCR).value == START);
  REQUIRE(system.iopBus().read32(0x1f801528).value == 0x01000401);
  REQUIRE(dma.interruptPending());
  REQUIRE(system.iopInterruptController().status() == 0);
  requireState(system, before);
  REQUIRE(system.trace().empty());
  dma.signalChannelCompletion(IOPDMAChannel::SIF1);
  REQUIRE(system.iopInterruptController().status() == DMAC_CAUSE);
  const auto pending = system.saveState();
  for (const IOPAddress address : CHANNEL_INITIALIZATION_REGISTERS)
  {
    CAPTURE(address);
    const auto read = system.iopBus().read32(address);
    REQUIRE(read.status == IOPBusStatus::Completed);
    REQUIRE(read.value == 0);
  }
  requireState(system, pending);
}

TEST_CASE("IOP DMA register initialization rejects other accesses without effects",
  "[iop][dma][channel-read][channel-init][supplemental-init][channel0-init][channel1-init][channel2-init][channel3-init][bus]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  const auto before = system.saveState();
  for (unsigned bit = 0; bit < 32; ++bit)
  {
    REQUIRE(system.iopDMAController().writeChannelRegisterInitialization(
      IOPWord{1} << bit) == IOPDMAWriteOutcome::UnsupportedControl);
    REQUIRE(system.iopDMAController().writeSupplementalInitialization(
      IOPWord{1} << bit) == IOPDMAWriteOutcome::UnsupportedControl);
  }
  for (IOPAddress address : INITIALIZATION_REGISTERS)
  {
    CAPTURE(address);
    for (unsigned bit = 0; bit < 32; ++bit)
    {
      const IOPWord value = IOPWord{1} << bit;
      REQUIRE(bus.write32(address, value).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.writeMasked32(address, value, 0xf).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.write32(address, 0xffffffff).status == IOPBusStatus::Unmapped);
    for (std::uint8_t mask = 1; mask < 0xf; ++mask)
    {
      REQUIRE(bus.writeMasked32(address, 0, mask).status == IOPBusStatus::Unmapped);
    }
    for (IOPAddress lane = 0; lane < 4; ++lane)
    {
      CAPTURE(lane);
      REQUIRE(bus.read8(address + lane).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write8(address + lane, 0).status == IOPBusStatus::Unmapped);
      if (lane != 0)
      {
        REQUIRE(bus.read32(address + lane).status == IOPBusStatus::Misaligned);
        REQUIRE(bus.write32(address + lane, 0).status == IOPBusStatus::Misaligned);
        REQUIRE(bus.writeMasked32(address + lane, 0, 0xf).status ==
          IOPBusStatus::Misaligned);
      }
      const auto halfStatus = (lane & 1) == 0 ? IOPBusStatus::Unmapped :
        IOPBusStatus::Misaligned;
      REQUIRE(bus.read16(address + lane).status == halfStatus);
      REQUIRE(bus.write16(address + lane, 0).status == halfStatus);
    }
    for (std::uint8_t mask : {0u, 0x10u, 0xffu})
    {
      REQUIRE_THROWS_AS(bus.writeMasked32(address + 1, 0, mask),
        std::invalid_argument);
    }
    for (IOPAddress alias : {address | 0x80000000u, address | 0xa0000000u})
    {
      REQUIRE(bus.write32(alias, 0).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.read32(alias).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.regionForRange(address, 4) == IOPBusRegion::Mmio);
    REQUIRE(bus.regionForRange(address, 5) == IOPBusRegion::Unmapped);
    REQUIRE(bus.readDMAC32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.dmaWriteStatus(address) == IOPBusStatus::Unmapped);
    std::uint8_t byte = 0xa5;
    REQUIRE(bus.loadPhysical(address, &byte, 1) == IOPBusStatus::Unmapped);
    REQUIRE(bus.inspectPhysical(address, &byte, 1) == IOPBusStatus::Unmapped);
    REQUIRE(byte == 0xa5);
  }
  for (const IOPAddress address : SUPPLEMENTAL_INITIALIZATION_REGISTERS)
  {
    CAPTURE(address);
    REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
  }
  for (IOPAddress address : {
    0x1f80107cu, 0x1f80108cu, 0x1f80109cu, 0x1f8010acu, 0x1f8010bcu,
    0x1f8010dcu, 0x1f8010ecu,
    0x1f80150cu, 0x1f80151cu, 0x1f80153cu, 0x1f80154cu, 0x1f80155cu,
    0x1f80156cu, 0x1f80157cu, 0x1f801580u})
  {
    CAPTURE(address);
    REQUIRE(bus.regionForRange(address, 1) == IOPBusRegion::Unmapped);
    REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeMasked32(address, 0, 0xf).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
  }
  requireState(system, before);
}

TEST_CASE("IOP DMA register guest initialization requires zero words and complete merge masks",
  "[iop][dma][channel-read][channel-init][supplemental-init][channel0-init][channel1-init][channel2-init][channel3-init][execution][merge]")
{
  for (IOPAddress physical : INITIALIZATION_REGISTERS)
  {
    for (IOPAddress target : {physical, physical | 0x80000000u, physical | 0xa0000000u})
    {
      for (IOPWord value : {0u, 1u})
      {
        for (IOPWord store : {
          0xac220000u,
          0xa8220000u, 0xa8220001u, 0xa8220002u, 0xa8220003u,
          0xb8220000u, 0xb8220001u, 0xb8220002u, 0xb8220003u})
        {
          const bool completed = value == 0 &&
            (store == 0xac220000 || store == 0xa8220003 || store == 0xb8220000);
          requireInitializationStore(physical, target, value, store,
            completed ? IOPStopReason::None : IOPStopReason::StoreFailure);
        }
      }
    }
  }
}

TEST_CASE("IOP DMA initialization remains stateless after cold reset and restore",
  "[iop][dma][channel-read][dpcr3][channel-init][supplemental-init][channel0-init][channel1-init][channel2-init][channel3-init][conformance][save-state]")
{
  NekoSystem system;
  const auto initialize = [&system]()
  {
    REQUIRE(system.iopBus().write32(DPCR3, 0x777).status == IOPBusStatus::Completed);
    REQUIRE(system.iopBus().writeMasked32(DPCR3, 0x777, 0xf).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.iopBus().read32(DPCR3).status == IOPBusStatus::Unmapped);
    for (const IOPAddress address : CHANNEL_INITIALIZATION_REGISTERS)
    {
      CAPTURE(address);
      const auto read = system.iopBus().read32(address);
      REQUIRE(read.status == IOPBusStatus::Completed);
      REQUIRE(read.value == 0);
    }
    for (IOPAddress address : INITIALIZATION_REGISTERS)
    {
      CAPTURE(address);
      REQUIRE(system.iopBus().write32(address, 0).status == IOPBusStatus::Completed);
      REQUIRE(system.iopBus().writeMasked32(address, 0, 0xf).status ==
        IOPBusStatus::Completed);
    }
    for (const IOPAddress address : CHANNEL_INITIALIZATION_REGISTERS)
    {
      CAPTURE(address);
      const auto read = system.iopBus().read32(address);
      REQUIRE(read.status == IOPBusStatus::Completed);
      REQUIRE(read.value == 0);
    }
    for (const IOPAddress address : SUPPLEMENTAL_INITIALIZATION_REGISTERS)
    {
      REQUIRE(system.iopBus().read32(address).status == IOPBusStatus::Unmapped);
    }
  };
  const auto cold = system.saveState();
  initialize();
  requireState(system, cold);
  system.reset();
  initialize();
  requireState(system, cold);
  system.iopDMAController().writePriority(SECONDARY, 0xffffffff);
  system.loadState(cold);
  initialize();
  requireState(system, cold);
}

TEST_CASE("IOP DMA channel word loads preserve delay aliases and completed trace metadata",
  "[iop][dma][channel-read][execution][trace]")
{
  for (const IOPAddress prefix : {0u, 0x80000000u, 0xa0000000u})
  {
    CAPTURE(prefix);
    NekoSystem traced;
    NekoSystem plain;
    const IOPAddress base = 0x1f800000 | prefix;
    std::vector<IOPWord> program = {0x3c010000 | (base >> 16)};
    IOPAddress destination = 0x400;
    for (const IOPAddress address : CHANNEL_INITIALIZATION_REGISTERS)
    {
      program.push_back(0x24021234); // addiu r2, r0, 0x1234
      program.push_back(0x8c220000 | (address & 0xffff));
      program.push_back(0xac020000 | destination); // store old r2 in load-delay slot
      program.push_back(0xac020000 | (destination + 4));
      program.push_back(0xac200000 | (address & 0xffff));
      destination += 8;
    }
    for (auto *system : {&traced, &plain})
    {
      IOPAddress pc = 0;
      for (const IOPWord word : program)
      {
        REQUIRE(system->iopBus().write32(pc, word).status == IOPBusStatus::Completed);
        pc += 4;
      }
      system->iopCore().startExecution(0xa0000000);
    }
    traced.startTrace();
    traced.runMasterCycles(program.size() * 8);
    plain.runMasterCycles(program.size() * 8);
    REQUIRE(traced.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(traced.iopCore().exception().kind == IOPException::None);
    REQUIRE(traced.iopCore().retiredInstructions() == program.size());
    REQUIRE(traced.iopCore().programCounter() == 0xa0000000 + program.size() * 4);
    REQUIRE(traced.iopCore().generalRegister(2) == 0);
    destination = 0x400;
    for (const IOPAddress address : CHANNEL_INITIALIZATION_REGISTERS)
    {
      CAPTURE(address);
      REQUIRE(traced.iopBus().read32(destination).value == 0x1234);
      REQUIRE(traced.iopBus().read32(destination + 4).value == 0);
      destination += 8;
    }
    IOPTraceTest::requireMachine(traced, plain);
    REQUIRE(plain.trace().empty());
    namespace Memory = NekoIOPTraceMemory;
    std::size_t accesses = 0;
    for (const auto &event : traced.trace())
    {
      if (event.subsystem != NekoTraceSubsystem::IOP ||
          event.type != NekoTraceEventType::MemoryAccess ||
          (event.value0 & 0xffff0000) != base)
      {
        continue;
      }
      const auto index = accesses / 2;
      REQUIRE(index < sizeof(CHANNEL_INITIALIZATION_REGISTERS) /
        sizeof(CHANNEL_INITIALIZATION_REGISTERS[0]));
      const bool store = accesses % 2 != 0;
      const IOPAddress physical = CHANNEL_INITIALIZATION_REGISTERS[index];
      REQUIRE(event.masterCycle == (1 + index * 5 + (store ? 4 : 1)) * 8 + 1);
      REQUIRE(event.value0 == (physical | prefix));
      REQUIRE(event.value1 == 0);
      REQUIRE(event.value2 == 0);
      REQUIRE(event.value3 == nekoPackIOPMemoryTraceMetadata({
        physical, 4, store ? Memory::AccessKind::DataStore : Memory::AccessKind::DataLoad,
        Memory::Outcome::Completed, prefix == 0xa0000000 ?
          Memory::CacheRoute::Uncached : Memory::CacheRoute::Cached,
        true, static_cast<std::uint8_t>(store ? 0xf : 0)}));
      ++accesses;
    }
    REQUIRE(accesses == 2 * sizeof(CHANNEL_INITIALIZATION_REGISTERS) /
      sizeof(CHANNEL_INITIALIZATION_REGISTERS[0]));
  }
}

TEST_CASE("IOP DPCR3 guest stores translate aliases and require complete merge masks",
  "[iop][dma][dpcr3][execution][merge]")
{
  for (IOPAddress target : {DPCR3, 0x9f8015f0u, 0xbf8015f0u})
  {
    for (IOPWord value : {0x777u, 0x776u})
    {
      for (IOPWord store : {
        0xac220000u,
        0xa8220000u, 0xa8220001u, 0xa8220002u, 0xa8220003u,
        0xb8220000u, 0xb8220001u, 0xb8220002u, 0xb8220003u})
      {
        const bool completed = value == 0x777 &&
          (store == 0xac220000 || store == 0xa8220003 || store == 0xb8220000);
        requireInitializationStore(DPCR3, target, value, store,
          completed ? IOPStopReason::None : IOPStopReason::StoreFailure);
      }
    }
  }
}

TEST_CASE("IOP DMA auxiliary word follows existing IOP MMIO fetch routing",
  "[iop][dma][auxiliary][cache]")
{
  for (const IOPAddress address : {0x1f801578u, 0x9f801578u, 0xbf801578u})
  {
    CAPTURE(address);
    NekoSystem system;
    auto &core = system.iopCore();
    REQUIRE(system.iopBus().write32(AUXILIARY, 0x24021234).status ==
      IOPBusStatus::Completed);
    core.startExecution(address);
    const auto fetch = core.fetchInstruction();
    REQUIRE(fetch.outcome == IOPAccessOutcome::Completed);
    REQUIRE(fetch.physicalAddress == AUXILIARY);
    REQUIRE(fetch.value == 0x24021234);
    core.stepInstruction();
    REQUIRE(core.executionState() == IOPExecutionState::Running);
    REQUIRE(core.retiredInstructions() == 1);
    REQUIRE(core.generalRegister(2) == 0x1234);
    REQUIRE(system.iopBus().read32(AUXILIARY).value == 0x24021234);
  }
}

TEST_CASE("IOP DMA auxiliary word round trips transactionally with destination wiring",
  "[iop][dma][auxiliary][save-state]")
{
  NekoSystem source;
  REQUIRE(source.iopBus().write32(AUXILIARY, 0x89abcdef).status ==
    IOPBusStatus::Completed);
  const auto state = source.saveState();
  const auto snapshot = inspectSaveState(state);
  const auto *field = snapshot.find("system.iop.dmaController.auxiliaryControl");
  REQUIRE(field != nullptr);
  REQUIRE(field->kind == SaveStateFieldKind::U32);
  REQUIRE(field->scalarValue == 0x89abcdef);
  NekoSystem destination;
  REQUIRE(destination.iopBus().write32(AUXILIARY, 0xffffffff).status ==
    IOPBusStatus::Completed);
  destination.loadState(state);
  REQUIRE(destination.iopStateHash() == source.iopStateHash());
  REQUIRE(destination.iopBus().read32(AUXILIARY).value == 0x89abcdef);
  REQUIRE(destination.saveState() == state);

  REQUIRE(destination.iopBus().write32(AUXILIARY, 0x13579bdf).status ==
    IOPBusStatus::Completed);
  const auto before = destination.saveState();
  SaveStateMutation invalid(state);
  invalid.writeScalar("system.iop.dmaController.banks[0].flags", 1);
  invalid.refreshChecksum();
  REQUIRE_THROWS_AS(destination.loadState(invalid), std::invalid_argument);
  requireState(destination, before);
  REQUIRE(destination.iopCore().writeData32(0xbf801578, 0x2468ace0).outcome ==
    IOPAccessOutcome::Completed);
  REQUIRE(destination.iopBus().read32(AUXILIARY).value == 0x2468ace0);
  REQUIRE(source.iopBus().read32(AUXILIARY).value == 0x89abcdef);
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
    0x01000000u, 0x01000401u, 0x01000600u, 0x21000400u, 0x81000400u})
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
  REQUIRE(channel.writeChannelControl(0x01000200) == IOPDMAWriteOutcome::ActiveTransfer);
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

TEST_CASE("IOP SIF1 guest startup control stores retire with precise word metadata",
  "[iop][dma][bus][trace][chcr-startup]")
{
  for (const IOPAddress target : {0x9f801538u, 0xbf801538u})
  {
    CAPTURE(target);
    NekoSystem traced;
    NekoSystem plain;
    const IOPWord program[] = {
      0x3c010000 | (target >> 16),
      0x34210000 | (target & 0xffff),
      0x3c024100, 0x34420300,
      0x10000001, 0xac220000,
      0x8c230000, 0
    };
    for (auto *system : {&traced, &plain})
    {
      IOPAddress pc = 0;
      for (const IOPWord word : program)
      {
        REQUIRE(system->iopBus().write32(pc, word).status ==
          IOPBusStatus::Completed);
        pc += 4;
      }
      system->iopCore().startExecution(0xa0000000);
    }
    traced.startTrace();
    traced.runMasterCycles(64);
    plain.runMasterCycles(64);
    REQUIRE(traced.iopCore().stopReason() == IOPStopReason::None);
    REQUIRE(traced.iopCore().exception().kind == IOPException::None);
    REQUIRE(traced.iopCore().retiredInstructions() == 8);
    REQUIRE(traced.iopCore().programCounter() == 0xa0000020);
    REQUIRE(traced.iopCore().generalRegister(3) == 0x41000300);
    REQUIRE(traced.iopSIF1DMAC().channelControl() == 0x41000300);
    REQUIRE(traced.iopStateHash() == plain.iopStateHash());
    REQUIRE(traced.eeStateHash() == plain.eeStateHash());
    REQUIRE(traced.sifStateHash() == plain.sifStateHash());
    REQUIRE(plain.trace().empty());
    namespace Memory = NekoIOPTraceMemory;
    unsigned stores = 0;
    for (const auto &event : traced.trace())
    {
      if (event.subsystem != NekoTraceSubsystem::IOP ||
          event.type != NekoTraceEventType::MemoryAccess ||
          event.value0 != target || event.masterCycle != 41)
      {
        continue;
      }
      REQUIRE(event.value1 == 0x41000300);
      REQUIRE(event.value2 == 0);
      REQUIRE(event.value3 == nekoPackIOPMemoryTraceMetadata({
        CHCR, 4, Memory::AccessKind::DataStore, Memory::Outcome::Completed,
        target < 0xa0000000 ? Memory::CacheRoute::Cached :
          Memory::CacheRoute::Uncached, true, 0xf}));
      ++stores;
    }
    REQUIRE(stores == 1);
  }
}

TEST_CASE("IOP SIF whole-word stops validate atomically and retain other owners",
  "[iop][dma][bus][iop-active-stop]")
{
  for (const IOPAddress physical : {0x1f801528u, CHCR})
  {
    const IOPWord control = physical == CHCR ? 0x41000300 : 0x01000501;
    for (const IOPWord stopped : {
      0u, 0x701u, physical == CHCR ? 0x40000100u : 0x100u})
    {
      CAPTURE(physical);
      CAPTURE(stopped);
      NekoSystem system;
      auto &bus = system.iopBus();
      REQUIRE(bus.write32(physical - 8, 0x123450).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(physical - 4, 0xabcd0020).status == IOPBusStatus::Completed);
      if (physical != CHCR)
        REQUIRE(bus.write32(physical + 4, 0x1800).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(physical, control).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(DICR2, FORCE).status == IOPBusStatus::Completed);
      system.iopDMAController().signalChannelCompletion(IOPDMAChannel::SIF0);
      system.iopDMAController().signalChannelCompletion(IOPDMAChannel::SIF1);
      const auto iopHash = system.iopStateHash();
      const auto eeHash = system.eeStateHash();
      const auto sifHash = system.sifStateHash();
      const auto status = system.iopDMAController().interruptStatus(SECONDARY);
      const auto interrupts = system.iopInterruptController().status();
      REQUIRE(interrupts == DMAC_CAUSE);
      REQUIRE(bus.write32(physical, control).status == IOPBusStatus::Completed);
      REQUIRE(system.iopStateHash() == iopHash);
      for (const IOPWord invalid : {
        2u, 0x80000000u, control ^ 0x100u,
        physical == CHCR ? 0x20000000u : 0x40000000u})
      {
        CAPTURE(invalid);
        REQUIRE(bus.write32(physical, invalid).status == IOPBusStatus::Unmapped);
        REQUIRE(system.iopStateHash() == iopHash);
        REQUIRE(system.eeStateHash() == eeHash);
        REQUIRE(system.sifStateHash() == sifHash);
      }
      REQUIRE(bus.write32(physical, stopped).status == IOPBusStatus::Completed);
      REQUIRE(bus.read32(physical).value == stopped);
      REQUIRE(bus.read32(physical - 8).value == 0x123450);
      REQUIRE(bus.read32(physical - 4).value == 0xabcd0020);
      if (physical != CHCR)
        REQUIRE(bus.read32(physical + 4).value == 0x1800);
      REQUIRE_FALSE(system.iopSIF0DMAC().active());
      REQUIRE_FALSE(system.iopSIF1DMAC().active());
      REQUIRE(system.eeStateHash() == eeHash);
      REQUIRE(system.sifStateHash() == sifHash);
      REQUIRE(system.iopDMAController().interruptStatus(SECONDARY) == status);
      REQUIRE(system.iopInterruptController().status() == interrupts);
      REQUIRE(bus.write32(physical, 0x101).status == IOPBusStatus::Completed);
      REQUIRE(bus.read32(physical).value == 0x101);
      REQUIRE(bus.write32(physical, control).status == IOPBusStatus::Completed);
      REQUIRE(bus.read32(physical).value == control);
    }
  }
}

TEST_CASE("IOP SIF guest zero stops retire through aliases with exact store metadata",
  "[iop][dma][bus][trace][iop-active-stop]")
{
  for (const IOPAddress physical : {0x1f801528u, CHCR})
  {
    for (const IOPAddress segment : {0x80000000u, 0xa0000000u})
    {
      const auto target = physical | segment;
      CAPTURE(target);
      NekoSystem traced;
      NekoSystem plain;
      const IOPWord program[] = {
        0x3c010000 | (target >> 16),
        0x34210000 | (target & 0xffff),
        0x24030055,
        0x10000001, 0xac200000,
        0x8c230000, 0
      };
      for (auto *system : {&traced, &plain})
      {
        REQUIRE(system->iopBus().write32(physical,
          physical == CHCR ? 0x41000300 : 0x01000401).status ==
          IOPBusStatus::Completed);
        IOPAddress pc = 0;
        for (const IOPWord word : program)
        {
          REQUIRE(system->iopBus().write32(pc, word).status ==
            IOPBusStatus::Completed);
          pc += 4;
        }
        system->iopCore().startExecution(0xa0000000);
      }
      traced.startTrace();
      traced.runMasterCycles(56);
      plain.runMasterCycles(56);
      REQUIRE(traced.iopCore().stopReason() == IOPStopReason::None);
      REQUIRE(traced.iopCore().exception().kind == IOPException::None);
      REQUIRE(traced.iopCore().retiredInstructions() == 7);
      REQUIRE(traced.iopCore().programCounter() == 0xa000001c);
      REQUIRE_FALSE(traced.iopCore().branchContinuation().active);
      REQUIRE(traced.iopCore().generalRegister(3) == 0);
      REQUIRE(traced.iopBus().read32(physical).value == 0);
      IOPTraceTest::requireMachine(traced, plain);
      REQUIRE(plain.trace().empty());
      namespace Memory = NekoIOPTraceMemory;
      unsigned stores = 0;
      for (const auto &event : traced.trace())
      {
        if (event.subsystem != NekoTraceSubsystem::IOP ||
            event.type != NekoTraceEventType::MemoryAccess ||
            event.value0 != target || event.masterCycle != 33)
          continue;
        REQUIRE(event.value1 == 0);
        REQUIRE(event.value2 == 0);
        REQUIRE(event.value3 == nekoPackIOPMemoryTraceMetadata({
          physical, 4, Memory::AccessKind::DataStore, Memory::Outcome::Completed,
          segment == 0x80000000 ? Memory::CacheRoute::Cached :
            Memory::CacheRoute::Uncached, true, 0xf}));
        ++stores;
      }
      REQUIRE(stores == 1);
    }
  }
}

TEST_CASE("IOP SIF DMA BCR halfword writes preserve the other lane through both owners",
  "[iop][dma][bus][bcr-halfword]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  const auto initial = system.iopStateHash();
  const auto eeHash = system.eeStateHash();
  const auto sifHash = system.sifStateHash();
  for (const IOPAddress address : {0x1f801524u, BCR})
  {
    CAPTURE(address);
    REQUIRE(bus.write32(address, 0xabcd1234).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(address, 0x20).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(address).value == 0xabcd0020);
    REQUIRE(bus.write16(address + 2, 0xfedc).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(address).value == 0xfedc0020);
    for (IOPWord value : {0u, 0xffffu, 0x8001u})
    {
      REQUIRE(bus.write16(address, static_cast<std::uint16_t>(value)).status ==
        IOPBusStatus::Completed);
      REQUIRE(bus.read32(address).value == (0xfedc0000 | value));
      REQUIRE(bus.write16(address + 2, static_cast<std::uint16_t>(value)).status ==
        IOPBusStatus::Completed);
      REQUIRE(bus.read32(address).value == ((value << 16) | value));
      REQUIRE(bus.write16(address + 2, 0xfedc).status == IOPBusStatus::Completed);
    }
    const auto retained = address == BCR ? system.iopSIF1DMAC().blockControl() :
      system.iopSIF0DMAC().blockControl();
    REQUIRE(retained == 0xfedc8001);
    REQUIRE(system.iopStateHash() != initial);
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Completed);
    REQUIRE(bus.write16(address + 2, 0).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(address).value == 0);
    REQUIRE(system.iopStateHash() == initial);
  }
  REQUIRE(system.eeStateHash() == eeHash);
  REQUIRE(system.sifStateHash() == sifHash);
  REQUIRE(system.iopDMAController().interruptStatus(SECONDARY) == 0);
  REQUIRE(system.iopInterruptController().status() == 0);
}

TEST_CASE("IOP SIF DMA BCR halfword writes preserve attachment width and active boundaries",
  "[iop][dma][bus][bcr-halfword]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  IOPBus detached;
  IOPBus receiverOnly;
  receiverOnly.attachDMA(&system.iopDMAController(), &system.iopSIF1DMAC());
  for (const IOPAddress address : {0x1f801524u, BCR})
  {
    REQUIRE(bus.write32(address, 0xabcd1234).status == IOPBusStatus::Completed);
    const auto before = system.iopStateHash();
    for (IOPAddress lane : {0u, 2u})
    {
      REQUIRE(detached.write16(address + lane, 0).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.read16(address + lane).status == IOPBusStatus::Unmapped);
    }
    for (IOPAddress lane = 0; lane < 4; ++lane)
    {
      REQUIRE(bus.read8(address + lane).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write8(address + lane, 0).status == IOPBusStatus::Unmapped);
    }
    for (IOPAddress lane : {1u, 3u})
    {
      REQUIRE(bus.write16(address + lane, 0).status == IOPBusStatus::Misaligned);
    }
    for (std::uint8_t mask = 1; mask < 15; ++mask)
    {
      REQUIRE(bus.writeMasked32(address, 0, mask).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.writeDMAC32(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.readDMAC32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(system.iopStateHash() == before);
  }
  for (const IOPAddress address : {0x1f801524u, 0x1f801526u})
  {
    REQUIRE(receiverOnly.write16(address, 0).status == IOPBusStatus::Unmapped);
  }
  for (const IOPAddress address : {
    DPCR, DICR, DPCR2, DICR2, AUXILIARY,
    0x1f801520u, 0x1f801522u, 0x1f801528u, 0x1f80152au,
    0x1f80152cu, 0x1f80152eu, MADR, MADR + 2, CHCR, CHCR + 2})
  {
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(receiverOnly.write16(address, 0).status == IOPBusStatus::Unmapped);
  }
  REQUIRE(receiverOnly.write16(BCR, 0x20).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(BCR).value == 0xabcd0020);
  REQUIRE(bus.write32(0x1f801528, 0x01000401).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(CHCR, START).status == IOPBusStatus::Completed);
  REQUIRE(system.sif().pushSIF1Quadword({{0x80001000, 4, 1, 2}}) ==
    SIFTransferOutcome::Completed);
  system.iopDMAController().writePriority(SECONDARY, 0x8000);
  system.runMasterCycles(1);
  REQUIRE(system.iopSIF1DMAC().remainingWords() == 4);
  const auto active = system.iopStateHash();
  const auto sifHash = system.sifStateHash();
  for (const IOPAddress address : {0x1f801524u, 0x1f801526u, BCR, BCR + 2})
  {
    REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Unmapped);
  }
  REQUIRE(system.iopStateHash() == active);
  REQUIRE(system.sifStateHash() == sifHash);
  REQUIRE(system.iopSIF1DMAC().remainingWords() == 4);
  REQUIRE(system.iopSIF1DMAC().terminalPacket());
  REQUIRE(system.iopSIF0DMAC().active());
  REQUIRE(system.iopSIF1DMAC().active());
}

TEST_CASE("IOP SIF DMA guest BCR halfword stores retire with precise lane metadata",
  "[iop][dma][bus][trace][bcr-halfword]")
{
  for (IOPAddress target : {0x9f801524u, 0xbf801524u, 0x9f801534u, 0xbf801534u})
  {
    CAPTURE(target);
    NekoSystem traced;
    NekoSystem plain;
    const auto physical = target & 0x1fffffff;
    const IOPWord program[] = {
      0x3c010000 | (target >> 16),
      0x34210000 | (target & 0xffff),
      0x24020020, // addiu r2, r0, 0x20
      0x10000001, // beq r0, r0, +1
      0xa4220000, // sh r2, 0(r1), in the delay slot
      0x3402ffff, // ori r2, r0, 0xffff
      0xa4220002, // sh r2, 2(r1)
      0x8c230000, // lw r3, 0(r1)
      0
    };
    for (auto *system : {&traced, &plain})
    {
      REQUIRE(system->iopBus().write32(physical, 0xabcd1234).status ==
        IOPBusStatus::Completed);
      IOPAddress pc = 0;
      for (IOPWord word : program)
      {
        REQUIRE(system->iopBus().write32(pc, word).status ==
          IOPBusStatus::Completed);
        pc += 4;
      }
      system->iopCore().startExecution(0xa0000000);
    }
    traced.startTrace();
    traced.runMasterCycles(9 * 8);
    plain.runMasterCycles(9 * 8);
    REQUIRE(traced.iopCore().stopReason() == IOPStopReason::None);
    REQUIRE(traced.iopCore().exception().kind == IOPException::None);
    REQUIRE(traced.iopCore().retiredInstructions() == 9);
    REQUIRE(traced.iopCore().programCounter() == 0xa0000024);
    REQUIRE(traced.iopCore().generalRegister(3) == 0xffff0020);
    REQUIRE(traced.iopBus().read32(physical).value == 0xffff0020);
    REQUIRE(traced.iopStateHash() == plain.iopStateHash());
    REQUIRE(traced.eeStateHash() == plain.eeStateHash());
    REQUIRE(traced.sifStateHash() == plain.sifStateHash());
    REQUIRE(plain.trace().empty());
    namespace Memory = NekoIOPTraceMemory;
    unsigned stores = 0;
    for (const auto &event : traced.trace())
    {
      if (event.subsystem != NekoTraceSubsystem::IOP ||
          event.type != NekoTraceEventType::MemoryAccess ||
          (event.value0 != target && event.value0 != target + 2) ||
          event.value1 == 0xffff0020)
      {
        continue;
      }
      REQUIRE(stores < 2);
      const auto lane = stores * 2;
      REQUIRE(event.masterCycle == 33 + stores * 16);
      REQUIRE(event.value0 == target + lane);
      REQUIRE(event.value1 == (stores == 0 ? 0x20 : 0xffff));
      REQUIRE(event.value2 == 0);
      REQUIRE(event.value3 == nekoPackIOPMemoryTraceMetadata({
        physical + lane, 2, Memory::AccessKind::DataStore,
        Memory::Outcome::Completed,
        target < 0xa0000000 ? Memory::CacheRoute::Cached :
          Memory::CacheRoute::Uncached,
        true, static_cast<std::uint8_t>(3 << lane)}));
      ++stores;
    }
    REQUIRE(stores == 2);
  }
}

TEST_CASE("IOP SIF DMA guest BCR stores retain precise unsupported access failures",
  "[iop][dma][bus][bcr-halfword]")
{
  for (const IOPWord store : {
    0xa0220000u, // sb r2, 0(r1)
    0xa8220001u, // swl r2, 1(r1), partial low halfword mask
    0xb8220002u, // swr r2, 2(r1), partial high halfword mask
    0xa4220000u  // sh r2, 0(r1), while active
  })
  {
    CAPTURE(store);
    IOPInterruptController interrupts;
    IOPDMAController dma(interrupts);
    IOPSIF1DMAChannel channel;
    IOPBus bus;
    bus.attachDMA(&dma, &channel);
    IOPCore core;
    core.attachBus(&bus);
    REQUIRE(channel.writeBlockControl(0xabcd1234) == IOPDMAWriteOutcome::Completed);
    if (store == 0xa4220000)
    {
      REQUIRE(channel.writeChannelControl(START) == IOPDMAWriteOutcome::Completed);
    }
    const IOPWord program[] = {
      0x3c01bf80, 0x34211534, 0x24020020, 0x10000002, store
    };
    IOPAddress pc = 0;
    for (IOPWord word : program)
    {
      REQUIRE(bus.write32(pc, word).status == IOPBusStatus::Completed);
      pc += 4;
    }
    core.startExecution(0xa0000000);
    for (unsigned step = 0; step < 5; ++step)
    {
      core.stepInstruction();
    }
    REQUIRE(core.stopReason() == IOPStopReason::StoreFailure);
    REQUIRE(core.exception().kind == IOPException::None);
    REQUIRE(core.retiredInstructions() == 4);
    REQUIRE(core.programCounter() == 0xa0000010);
    REQUIRE(core.branchContinuation().active);
    REQUIRE(core.storeFailure().access.virtualAddress == 0xbf801534 + (store & 3));
    REQUIRE(core.storeFailure().access.physicalAddress == BCR + (store & 3));
    REQUIRE(core.storeFailure().access.outcome == IOPAccessOutcome::Unmapped);
    REQUIRE(channel.blockControl() == 0xabcd1234);
    REQUIRE(channel.active() == (store == 0xa4220000));
    REQUIRE(channel.remainingWords() == 0);
    REQUIRE(dma.interruptStatus(SECONDARY) == 0);
    REQUIRE(interrupts.status() == 0);
  }
}

TEST_CASE("IOP DMA MMIO admits selected widths and rejects unsupported boundaries atomically",
  "[iop][dma][bus]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  for (const IOPAddress address :
       {DPCR, DICR, DPCR2, DICR2, AUXILIARY, MADR, BCR, CHCR})
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
      if (address != BCR)
      {
        REQUIRE(bus.write16(address + lane, 0xffff).status == IOPBusStatus::Unmapped);
      }
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
    0x1f80153cu, 0x1f801560u, 0x1f80157cu, 0x1f801580u})
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
  REQUIRE(source.iopBus().write32(SPU_MADR[0], 0x4fa00).status ==
    IOPBusStatus::Completed);
  REQUIRE(source.iopBus().write32(SPU_MADR[1], 0x00fffffc).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.iopBus().write32(SPU_MADR[0], 0x100).status ==
    IOPBusStatus::Completed);
  REQUIRE(destination.iopBus().write32(SPU_MADR[1], 0x200).status ==
    IOPBusStatus::Completed);
  destination.iopDMAController().writeInterrupt(PRIMARY, FORCE);
  destination.runMasterCycles(9);
  destination.startTrace();
  const auto before = destination.saveState();
  const auto traceHash = destination.traceHash();
  const auto sourceState = source.saveState();
  for (const auto field : {
    InvalidField{"system.iop.dmaController.spuMemoryAddress[0].value", 1},
    InvalidField{"system.iop.dmaController.spuMemoryAddress[0].value", 0x01000000},
    InvalidField{"system.iop.dmaController.spuMemoryAddress[1].value", 3},
    InvalidField{"system.iop.dmaController.spuMemoryAddress[1].value", 0x80000000},
    InvalidField{"system.iop.dmaController.banks[0].control", 0x01000000},
    InvalidField{"system.iop.dmaController.banks[0].flags", 1},
    InvalidField{"system.iop.dmaController.banks[1].flags", 2},
    InvalidField{"system.iop.sif1DMA.memoryAddress", 0x01000000},
    InvalidField{"system.iop.sif1DMA.memoryAddress", 1},
    InvalidField{"system.iop.sif1DMA.channelControl", 0x01000000},
    InvalidField{"system.iop.sif1DMA.channelControl", 0x20000000}})
  {
    CAPTURE(field.path);
    CAPTURE(field.value);
    SaveStateMutation corrupt(sourceState);
    corrupt.writeScalar(field.path, field.value);
    corrupt.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(corrupt.bytes()), std::invalid_argument);
    requireState(destination, before);
    REQUIRE(destination.traceEnabled());
    REQUIRE(destination.traceHash() == traceHash);
  }
  SaveStateMutation checksum(sourceState);
  checksum.writeScalar("system.iop.dmaController.spuMemoryAddress[0].value", 0x100);
  REQUIRE_THROWS_AS(destination.loadState(checksum.bytes()), std::invalid_argument);
  requireState(destination, before);
}

TEST_CASE("IOP DMA canonical registers independently contribute to IOP hashes",
  "[iop][dma][save-state]")
{
  NekoSystem system;
  const auto hash = system.iopStateHash();
  for (std::size_t core = 0; core < 2; ++core)
  {
    REQUIRE(system.iopDMAController().writeSPUMemoryAddress(core, 4) ==
      IOPDMAWriteOutcome::Completed);
    REQUIRE(system.iopStateHash() != hash);
    REQUIRE(system.iopDMAController().writeSPUMemoryAddress(core, 0) ==
      IOPDMAWriteOutcome::Completed);
    REQUIRE(system.iopStateHash() == hash);
  }
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
