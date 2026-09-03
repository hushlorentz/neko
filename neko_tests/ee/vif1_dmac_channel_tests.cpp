#include <cstdint>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "gif_dmac_channel.hpp"
#include "neko_system.hpp"
#include "vif1_dmac_channel.hpp"

namespace
{
  GIFQuadword dmaTag(
    GIFDMATagID id,
    std::uint16_t qwc,
    std::uint32_t address = 0,
    bool interrupt = false,
    std::uint32_t vifCode0 = 0,
    std::uint32_t vifCode1 = 0)
  {
    return GIFQuadword{{
      qwc |
        (static_cast<std::uint32_t>(id) << 28) |
        (interrupt ? UINT32_C(0x80000000) : 0),
      address,
      vifCode0,
      vifCode1
    }};
  }

  void startNormalTransfer(
    EEBus *bus,
    std::uint32_t address,
    std::uint16_t qwc)
  {
    bus->write32(EEMemoryMap::D_CTRL, GIFDMACControl::DMA_ENABLE);
    bus->write32(EEMemoryMap::D1_MADR, address);
    bus->write32(EEMemoryMap::D1_QWC, qwc);
    bus->write32(
      EEMemoryMap::D1_CHCR,
      GIFDMACChannelControl::FROM_MEMORY |
      GIFDMACChannelControl::START);
  }

  void startSourceChain(
    EEBus *bus,
    std::uint32_t address,
    std::uint32_t options = 0)
  {
    bus->write32(EEMemoryMap::D_CTRL, GIFDMACControl::DMA_ENABLE);
    bus->write32(EEMemoryMap::D1_TADR, address);
    bus->write32(
      EEMemoryMap::D1_CHCR,
      GIFDMACChannelControl::FROM_MEMORY |
      GIFDMACChannelControl::CHAIN_MODE |
      options |
      GIFDMACChannelControl::START);
  }
}

TEST_CASE("VIF1 DMAC normal transfers feed the VIF1 FIFO")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  REQUIRE(bus.writeQuadword(0x1000, {}));
  REQUIRE(bus.writeQuadword(0x1010, {}));

  startNormalTransfer(&bus, 0x1000, 2);
  system.clockMasterCycle();

  REQUIRE(bus.read32(EEMemoryMap::D1_MADR) == 0x1010);
  REQUIRE(bus.read32(EEMemoryMap::D1_QWC) == 1);
  REQUIRE(system.vif1().fifoQuadwordCount() == 1);

  system.clockMasterCycle();
  REQUIRE(bus.read32(EEMemoryMap::D1_MADR) == 0x1020);
  REQUIRE(bus.read32(EEMemoryMap::D1_QWC) == 0);
  REQUIRE(
    (bus.read32(EEMemoryMap::D1_CHCR) &
     GIFDMACChannelControl::START) == 0);
  REQUIRE(
    (bus.read32(EEMemoryMap::D_STAT) &
     GIFDMACStatus::CHANNEL_1) != 0);

  system.clockMasterCycle();
  REQUIRE(system.vif1().wordsIngested() == 8);
  REQUIRE(system.vif1DMAC().transferredQuadwordCount() == 2);

  bus.write32(
    EEMemoryMap::D_STAT,
    GIFDMACStatus::CHANNEL_1_MASK);
  REQUIRE(system.interruptPending());
  bus.write32(EEMemoryMap::D_STAT, GIFDMACStatus::CHANNEL_1);
  REQUIRE_FALSE(system.interruptPending());
}

TEST_CASE("VIF1 DMAC preserves progress while its FIFO is full")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  const EEQuadword interruptedNops = {
    UINT64_C(0x0000000080000000),
    0
  };

  REQUIRE(
    bus.writeGuestData128(
      EEMemoryMap::VIF1_FIFO,
      interruptedNops) ==
    EEDataWriteResult::Completed);
  bus.advanceGuestFIFOs();
  for (std::size_t index = 0; index < 15; ++index)
  {
    REQUIRE(
      bus.writeGuestData128(EEMemoryMap::VIF1_FIFO, {}) ==
      EEDataWriteResult::Completed);
  }
  REQUIRE(system.vif1().fifoQuadwordCount() == 16);
  REQUIRE(bus.writeQuadword(0x1000, {}));
  startNormalTransfer(&bus, 0x1000, 1);

  system.clockMasterCycle();
  REQUIRE(system.vif1DMAC().stalledByVIF1());
  REQUIRE(bus.read32(EEMemoryMap::D1_MADR) == 0x1000);
  REQUIRE(bus.read32(EEMemoryMap::D1_QWC) == 1);

  bus.write32(EEMemoryMap::VIF1_FBRST, 1u << 3);
  system.clockMasterCycle();
  REQUIRE_FALSE(system.vif1DMAC().stalledByVIF1());
  REQUIRE(bus.read32(EEMemoryMap::D1_MADR) == 0x1010);
  REQUIRE(bus.read32(EEMemoryMap::D1_QWC) == 0);
}

TEST_CASE("VIF1 DMAC source chains transfer tags before packet data")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  constexpr std::uint32_t STCYCL_0404 = UINT32_C(0x01000404);

  REQUIRE(bus.writeQuadword(
    0x1000,
    dmaTag(
      GIFDMATagID::End,
      1,
      0,
      false,
      STCYCL_0404)));
  REQUIRE(bus.writeQuadword(0x1010, {}));
  startSourceChain(
    &bus,
    0x1000,
    GIFDMACChannelControl::TAG_TRANSFER_ENABLE);

  system.runMasterCycles(3);

  REQUIRE(system.vif1().cycle() == 0x0404);
  REQUIRE(system.vif1().wordsIngested() == 8);
  REQUIRE(system.vif1DMAC().transferredQuadwordCount() == 2);
  REQUIRE(bus.read32(EEMemoryMap::D1_TADR) == 0x1020);
  REQUIRE(
    (bus.read32(EEMemoryMap::D1_CHCR) &
     GIFDMACChannelControl::START) == 0);
  REQUIRE(
    (bus.read32(EEMemoryMap::D1_CHCR) >> 16) ==
    (static_cast<std::uint32_t>(GIFDMATagID::End) << 12));
}

TEST_CASE("VIF1 DMAC source-chain addressing follows tag control")
{
  SECTION("REFE transfers from its address and terminates")
  {
    NekoSystem system;
    EEBus &bus = system.eeBus();
    REQUIRE(bus.writeQuadword(
      0x1000,
      dmaTag(GIFDMATagID::ReferenceEnd, 1, 0x2000)));
    REQUIRE(bus.writeQuadword(0x2000, {}));

    startSourceChain(&bus, 0x1000);
    system.runMasterCycles(2);

    REQUIRE(bus.read32(EEMemoryMap::D1_MADR) == 0x2010);
    REQUIRE(
      (bus.read32(EEMemoryMap::D1_CHCR) &
       GIFDMACChannelControl::START) == 0);
  }

  SECTION("NEXT redirects tag traversal")
  {
    NekoSystem system;
    EEBus &bus = system.eeBus();
    REQUIRE(bus.writeQuadword(
      0x1000,
      dmaTag(GIFDMATagID::Next, 0, 0x2000)));
    REQUIRE(bus.writeQuadword(
      0x2000,
      dmaTag(GIFDMATagID::End, 0)));

    startSourceChain(&bus, 0x1000);
    system.runMasterCycles(2);

    REQUIRE(bus.read32(EEMemoryMap::D1_TADR) == 0x2010);
    REQUIRE(
      (bus.read32(EEMemoryMap::D1_CHCR) &
       GIFDMACChannelControl::START) == 0);
  }

  SECTION("CALL and RET maintain the two-entry address stack")
  {
    NekoSystem system;
    EEBus &bus = system.eeBus();
    REQUIRE(bus.writeQuadword(
      0x1000,
      dmaTag(GIFDMATagID::Call, 0, 0x2000)));
    REQUIRE(bus.writeQuadword(
      0x1010,
      dmaTag(GIFDMATagID::End, 0)));
    REQUIRE(bus.writeQuadword(
      0x2000,
      dmaTag(GIFDMATagID::Return, 0)));

    startSourceChain(&bus, 0x1000);
    system.clockMasterCycle();
    REQUIRE(bus.read32(EEMemoryMap::D1_ASR0) == 0x1010);
    REQUIRE(
      (bus.read32(EEMemoryMap::D1_CHCR) &
       GIFDMACChannelControl::ADDRESS_STACK_MASK) ==
      (1u << 4));

    system.runMasterCycles(2);
    REQUIRE(
      (bus.read32(EEMemoryMap::D1_CHCR) &
       GIFDMACChannelControl::ADDRESS_STACK_MASK) == 0);
    REQUIRE(
      (bus.read32(EEMemoryMap::D1_CHCR) &
       GIFDMACChannelControl::START) == 0);
  }

  SECTION("An enabled tag interrupt stops after its packet")
  {
    NekoSystem system;
    EEBus &bus = system.eeBus();
    REQUIRE(bus.writeQuadword(
      0x1000,
      dmaTag(GIFDMATagID::Count, 1, 0, true)));
    REQUIRE(bus.writeQuadword(0x1010, {}));
    REQUIRE(bus.writeQuadword(
      0x1020,
      dmaTag(GIFDMATagID::End, 0)));

    startSourceChain(
      &bus,
      0x1000,
      GIFDMACChannelControl::TAG_INTERRUPT_ENABLE);
    system.runMasterCycles(2);

    REQUIRE(bus.read32(EEMemoryMap::D1_TADR) == 0x1020);
    REQUIRE(
      (bus.read32(EEMemoryMap::D1_CHCR) &
       GIFDMACChannelControl::START) == 0);
    REQUIRE(
      (bus.read32(EEMemoryMap::D_STAT) &
       GIFDMACStatus::CHANNEL_1) != 0);
  }
}
