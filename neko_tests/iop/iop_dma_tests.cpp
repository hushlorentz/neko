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
  constexpr IOPAddress AUXILIARY = 0x1f801578;
  constexpr IOPAddress DPCR3 = 0x1f8015f0;
  constexpr IOPAddress CHANNEL0_REGISTERS[] = {
    0x1f801080, 0x1f801084, 0x1f801088
  };
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

TEST_CASE("IOP channel0 initialization requires the DMA owner and changes no state",
  "[iop][dma][channel0-init]")
{
  IOPBus detached;
  for (IOPAddress address : CHANNEL0_REGISTERS)
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
  const auto before = system.saveState();
  system.startTrace();
  for (IOPAddress address : CHANNEL0_REGISTERS)
  {
    CAPTURE(address);
    for (unsigned repeat = 0; repeat < 2; ++repeat)
    {
      REQUIRE(system.iopBus().write32(address, 0).status == IOPBusStatus::Completed);
      REQUIRE(system.iopBus().writeMasked32(address, 0, 0xf).status ==
        IOPBusStatus::Completed);
    }
  }
  requireState(system, before);
  REQUIRE(system.trace().empty());
}

TEST_CASE("IOP channel0 initialization rejects other accesses without effects",
  "[iop][dma][channel0-init][bus]")
{
  NekoSystem system;
  auto &bus = system.iopBus();
  const auto before = system.saveState();
  for (IOPAddress address : CHANNEL0_REGISTERS)
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
      const auto wordStatus = lane == 0 ? IOPBusStatus::Unmapped :
        IOPBusStatus::Misaligned;
      REQUIRE(bus.read32(address + lane).status == wordStatus);
      if (lane != 0)
      {
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
  for (IOPAddress address : {
    0x1f80107cu, 0x1f80108cu, 0x1f801090u, 0x1f801094u,
    0x1f801098u, 0x1f8010a0u})
  {
    CAPTURE(address);
    REQUIRE(bus.regionForRange(address, 1) == IOPBusRegion::Unmapped);
    REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeMasked32(address, 0, 0xf).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
  }
  requireState(system, before);
}

TEST_CASE("IOP channel0 guest initialization requires zero words and complete merge masks",
  "[iop][dma][channel0-init][execution][merge]")
{
  for (IOPAddress physical : CHANNEL0_REGISTERS)
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
  "[iop][dma][dpcr3][channel0-init][conformance][save-state]")
{
  for (IOPAddress address : {DPCR3, CHANNEL0_REGISTERS[0],
    CHANNEL0_REGISTERS[1], CHANNEL0_REGISTERS[2]})
  {
    CAPTURE(address);
    const IOPWord value = address == DPCR3 ? 0x777 : 0;
    NekoSystem system;
    const auto cold = system.saveState();
    REQUIRE(system.iopBus().write32(address, value).status == IOPBusStatus::Completed);
    requireState(system, cold);
    system.reset();
    REQUIRE(system.iopBus().write32(address, value).status == IOPBusStatus::Completed);
    requireState(system, cold);
    system.iopDMAController().writePriority(SECONDARY, 0xffffffff);
    system.loadState(cold);
    REQUIRE(system.iopBus().writeMasked32(address, value, 0xf).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.iopBus().read32(address).status == IOPBusStatus::Unmapped);
    requireState(system, cold);
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
