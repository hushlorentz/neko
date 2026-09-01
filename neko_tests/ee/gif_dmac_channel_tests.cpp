#include <cstdint>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "gif_dmac_channel.hpp"
#include "gif_registers.hpp"
#include "neko_system.hpp"

namespace
{
  GIFQuadword gifTag(std::uint8_t primitive)
  {
    const std::uint64_t low =
      UINT64_C(1) |
      (UINT64_C(1) << 15) |
      (static_cast<std::uint64_t>(primitive) << 47) |
      (static_cast<std::uint64_t>(GIFDataFormat::Packed) << 58) |
      (UINT64_C(1) << 60);
    return GIFQuadword{{
      static_cast<std::uint32_t>(low),
      static_cast<std::uint32_t>(low >> 32),
      GIFRegisterDescriptor::AD,
      0
    }};
  }

  GIFQuadword adWrite(
    std::uint8_t address,
    std::uint64_t value)
  {
    return GIFQuadword{{
      static_cast<std::uint32_t>(value),
      static_cast<std::uint32_t>(value >> 32),
      address,
      0
    }};
  }

  GIFQuadword dmaTag(
    GIFDMATagID id,
    std::uint16_t qwc,
    std::uint32_t address = 0,
    bool interrupt = false)
  {
    return GIFQuadword{{
      qwc |
        (static_cast<std::uint32_t>(id) << 28) |
        (interrupt ? UINT32_C(0x80000000) : 0),
      address,
      0,
      0
    }};
  }

  void writePacket(
    EEBus *bus,
    std::uint32_t address,
    GSPrimitiveType primitive)
  {
    REQUIRE(bus->writeQuadword(
      address,
      gifTag(static_cast<std::uint8_t>(primitive))));
    REQUIRE(bus->writeQuadword(
      address + 16,
      adWrite(
        GSRegisterAddress::PRIM,
        static_cast<std::uint8_t>(primitive))));
  }

  void startNormalTransfer(
    EEBus *bus,
    std::uint32_t address,
    std::uint16_t qwc)
  {
    bus->write32(EEMemoryMap::D_CTRL, GIFDMACControl::DMA_ENABLE);
    bus->write32(EEMemoryMap::D2_MADR, address);
    bus->write32(EEMemoryMap::D2_QWC, qwc);
    bus->write32(EEMemoryMap::D2_CHCR, GIFDMACChannelControl::START);
  }
}

TEST_CASE("GIF DMAC Normal Transfer Tests")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  writePacket(&bus, 0x1000, GSPrimitiveType::Sprite);
  startNormalTransfer(&bus, 0x1000, 2);

  system.clockMasterCycle();

  REQUIRE(bus.read32(EEMemoryMap::D2_MADR) == 0x1010);
  REQUIRE(bus.read32(EEMemoryMap::D2_QWC) == 1);
  REQUIRE(
    (bus.read32(EEMemoryMap::D2_CHCR) &
     GIFDMACChannelControl::START) != 0);

  system.clockMasterCycle();

  REQUIRE(
    system.gs().primitive().type ==
    GSPrimitiveType::Sprite);
  REQUIRE(bus.read32(EEMemoryMap::D2_QWC) == 0);
  REQUIRE(
    (bus.read32(EEMemoryMap::D2_CHCR) &
     GIFDMACChannelControl::START) == 0);
  REQUIRE(
    (bus.read32(EEMemoryMap::D_STAT) &
     GIFDMACStatus::CHANNEL_2) != 0);

  bus.write32(
    EEMemoryMap::D_STAT,
    GIFDMACStatus::CHANNEL_2_MASK);
  REQUIRE(system.interruptPending());

  bus.write32(EEMemoryMap::D_STAT, GIFDMACStatus::CHANNEL_2);
  REQUIRE_FALSE(system.interruptPending());
}

TEST_CASE("GIF DMAC PATH3 Backpressure Tests")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  writePacket(&bus, 0x1000, GSPrimitiveType::Triangle);
  startNormalTransfer(&bus, 0x1000, 2);
  bus.write32(EEMemoryMap::GIF_MODE, GIFMode::M3R);

  system.runMasterCycles(3);

  REQUIRE(bus.read32(EEMemoryMap::D2_MADR) == 0x1000);
  REQUIRE(bus.read32(EEMemoryMap::D2_QWC) == 2);
  REQUIRE(system.gifDMAC().stalledByPATH3());

  bus.write32(EEMemoryMap::GIF_MODE, 0);
  system.runMasterCycles(2);

  REQUIRE_FALSE(system.gifDMAC().stalledByPATH3());
  REQUIRE(
    system.gs().primitive().type ==
    GSPrimitiveType::Triangle);
}

TEST_CASE("GIF DMAC Source Chain Tests")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();

  REQUIRE(bus.writeQuadword(
    0x1000,
    dmaTag(GIFDMATagID::Count, 2)));
  writePacket(&bus, 0x1010, GSPrimitiveType::TriangleFan);
  REQUIRE(bus.writeQuadword(
    0x1030,
    dmaTag(GIFDMATagID::End, 2, 0, true)));
  writePacket(&bus, 0x1040, GSPrimitiveType::Sprite);

  bus.write32(EEMemoryMap::D_CTRL, GIFDMACControl::DMA_ENABLE);
  bus.write32(EEMemoryMap::D2_TADR, 0x1000);
  bus.write32(
    EEMemoryMap::D2_CHCR,
    GIFDMACChannelControl::CHAIN_MODE |
    GIFDMACChannelControl::TAG_INTERRUPT_ENABLE |
    GIFDMACChannelControl::START);

  system.runMasterCycles(6);

  REQUIRE(
    system.gs().primitive().type ==
    GSPrimitiveType::Sprite);
  REQUIRE(bus.read32(EEMemoryMap::D2_TADR) == 0x1060);
  REQUIRE(
    (bus.read32(EEMemoryMap::D2_CHCR) &
     GIFDMACChannelControl::START) == 0);
  REQUIRE(
    (bus.read32(EEMemoryMap::D2_CHCR) >> 16) ==
    (static_cast<std::uint32_t>(GIFDMATagID::End) << 12 |
     UINT32_C(0x8000)));
  REQUIRE(
    (bus.read32(EEMemoryMap::D_STAT) &
     GIFDMACStatus::CHANNEL_2) != 0);
}

TEST_CASE("GIF DMAC Source Chain Address Tests")
{
  SECTION("REFE transfers from its address and terminates")
  {
    NekoSystem system;
    EEBus &bus = system.eeBus();
    REQUIRE(bus.writeQuadword(
      0x1000,
      dmaTag(GIFDMATagID::ReferenceEnd, 2, 0x2000)));
    writePacket(&bus, 0x2000, GSPrimitiveType::TriangleStrip);

    bus.write32(EEMemoryMap::D_CTRL, GIFDMACControl::DMA_ENABLE);
    bus.write32(EEMemoryMap::D2_TADR, 0x1000);
    bus.write32(
      EEMemoryMap::D2_CHCR,
      GIFDMACChannelControl::CHAIN_MODE |
      GIFDMACChannelControl::START);

    system.runMasterCycles(3);

    REQUIRE(
      system.gs().primitive().type ==
      GSPrimitiveType::TriangleStrip);
    REQUIRE(bus.read32(EEMemoryMap::D2_MADR) == 0x2020);
    REQUIRE(
      (bus.read32(EEMemoryMap::D2_CHCR) &
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

    bus.write32(EEMemoryMap::D_CTRL, GIFDMACControl::DMA_ENABLE);
    bus.write32(EEMemoryMap::D2_TADR, 0x1000);
    bus.write32(
      EEMemoryMap::D2_CHCR,
      GIFDMACChannelControl::CHAIN_MODE |
      GIFDMACChannelControl::START);

    system.runMasterCycles(2);

    REQUIRE(bus.read32(EEMemoryMap::D2_TADR) == 0x2010);
    REQUIRE(
      (bus.read32(EEMemoryMap::D2_CHCR) &
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

    bus.write32(EEMemoryMap::D_CTRL, GIFDMACControl::DMA_ENABLE);
    bus.write32(EEMemoryMap::D2_TADR, 0x1000);
    bus.write32(
      EEMemoryMap::D2_CHCR,
      GIFDMACChannelControl::CHAIN_MODE |
      GIFDMACChannelControl::START);

    system.clockMasterCycle();
    REQUIRE(bus.read32(EEMemoryMap::D2_ASR0) == 0x1010);
    REQUIRE(
      (bus.read32(EEMemoryMap::D2_CHCR) &
       GIFDMACChannelControl::ADDRESS_STACK_MASK) ==
      (1u << 4));

    system.runMasterCycles(2);

    REQUIRE(
      (bus.read32(EEMemoryMap::D2_CHCR) &
       GIFDMACChannelControl::ADDRESS_STACK_MASK) == 0);
    REQUIRE(
      (bus.read32(EEMemoryMap::D2_CHCR) &
       GIFDMACChannelControl::START) == 0);
  }
}
