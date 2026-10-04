#include <stdexcept>

#include "catch.hpp"
#include "dmac_channel_state.hpp"
#include "dmac_controller.hpp"
#include "ee_bus.hpp"
#include "neko_system.hpp"
#include "scratchpad_dmac_channel.hpp"

TEST_CASE("Scratchpad DMAC registers are guest-visible")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();

  REQUIRE(EEMemoryMap::D8_CHCR == UINT32_C(0x1000d000));
  REQUIRE(EEMemoryMap::D8_MADR == UINT32_C(0x1000d010));
  REQUIRE(EEMemoryMap::D8_QWC == UINT32_C(0x1000d020));
  REQUIRE(EEMemoryMap::D8_SADR == UINT32_C(0x1000d080));
  REQUIRE(EEMemoryMap::D9_CHCR == UINT32_C(0x1000d400));
  REQUIRE(EEMemoryMap::D9_MADR == UINT32_C(0x1000d410));
  REQUIRE(EEMemoryMap::D9_QWC == UINT32_C(0x1000d420));
  REQUIRE(EEMemoryMap::D9_TADR == UINT32_C(0x1000d430));
  REQUIRE(EEMemoryMap::D9_SADR == UINT32_C(0x1000d480));

  bus.write32(
    EEMemoryMap::D8_CHCR,
    DMACChannelControl::INTERLEAVE_MODE |
      DMACChannelControl::TAG_INTERRUPT_ENABLE |
      UINT32_C(0xabcd0000));
  bus.write32(EEMemoryMap::D8_MADR, UINT32_C(0xffffffff));
  bus.write32(EEMemoryMap::D8_QWC, UINT32_C(0x12345678));
  bus.write32(EEMemoryMap::D8_SADR, UINT32_C(0xffffffff));

  REQUIRE(
    bus.read32(EEMemoryMap::D8_CHCR) ==
    (DMACChannelControl::INTERLEAVE_MODE |
     DMACChannelControl::TAG_INTERRUPT_ENABLE |
     UINT32_C(0xabcd0000)));
  REQUIRE(bus.read32(EEMemoryMap::D8_MADR) == 0x7ffffff0);
  REQUIRE(bus.read32(EEMemoryMap::D8_QWC) == 0x5678);
  REQUIRE(bus.read32(EEMemoryMap::D8_SADR) == 0x3ff0);

  std::uint32_t value = 0;
  REQUIRE_FALSE(
    bus.readData32(UINT32_C(0x1000d030), &value));
  REQUIRE_FALSE(
    bus.writeData32(UINT32_C(0x1000d030), 0x1000));

  bus.write32(
    EEMemoryMap::D9_CHCR,
    DMACChannelControl::INTERLEAVE_MODE |
      DMACChannelControl::TAG_TRANSFER_ENABLE |
      UINT32_C(0x12340000));
  bus.write32(EEMemoryMap::D9_MADR, UINT32_C(0xffffffff));
  bus.write32(EEMemoryMap::D9_QWC, UINT32_C(0x87654321));
  bus.write32(EEMemoryMap::D9_TADR, UINT32_C(0xfffffff7));
  bus.write32(EEMemoryMap::D9_SADR, UINT32_C(0x12345678));

  REQUIRE(
    bus.read32(EEMemoryMap::D9_CHCR) ==
    (DMACChannelControl::INTERLEAVE_MODE |
     DMACChannelControl::TAG_TRANSFER_ENABLE |
     UINT32_C(0x12340000)));
  REQUIRE(bus.read32(EEMemoryMap::D9_MADR) == 0x7ffffff0);
  REQUIRE(bus.read32(EEMemoryMap::D9_QWC) == 0x4321);
  REQUIRE(bus.read32(EEMemoryMap::D9_TADR) == 0xfffffff0);
  REQUIRE(bus.read32(EEMemoryMap::D9_SADR) == 0x1670);
}

TEST_CASE("Scratchpad DMAC active registers only accept STR changes")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  const std::uint32_t control =
    DMACChannelControl::INTERLEAVE_MODE |
    DMACChannelControl::TAG_INTERRUPT_ENABLE;

  bus.write32(EEMemoryMap::D8_MADR, 0x1000);
  bus.write32(EEMemoryMap::D8_QWC, 2);
  bus.write32(EEMemoryMap::D8_SADR, 0x200);
  bus.write32(
    EEMemoryMap::D8_CHCR,
    control | DMACChannelControl::START);

  REQUIRE_FALSE(bus.writeData32(EEMemoryMap::D8_MADR, 0x3000));
  REQUIRE_FALSE(bus.writeData32(EEMemoryMap::D8_QWC, 3));
  REQUIRE_FALSE(bus.writeData32(EEMemoryMap::D8_SADR, 0x400));
  REQUIRE_FALSE(
    bus.writeData32(
      EEMemoryMap::D8_CHCR,
      DMACChannelControl::START));
  REQUIRE(bus.read32(EEMemoryMap::D8_MADR) == 0x1000);
  REQUIRE(bus.read32(EEMemoryMap::D8_QWC) == 2);
  REQUIRE(bus.read32(EEMemoryMap::D8_SADR) == 0x200);
  REQUIRE(
    bus.read32(EEMemoryMap::D8_CHCR) ==
    (control | DMACChannelControl::START));

  REQUIRE(bus.writeData32(EEMemoryMap::D8_CHCR, control));
  REQUIRE_FALSE(system.fromScratchpadDMAC().active());

  bus.write32(EEMemoryMap::D9_TADR, 0x5000);
  bus.write32(
    EEMemoryMap::D9_CHCR,
    DMACChannelControl::START);
  REQUIRE_FALSE(bus.writeData32(EEMemoryMap::D9_TADR, 0x6000));
  REQUIRE(bus.read32(EEMemoryMap::D9_TADR) == 0x5000);
  REQUIRE(bus.writeData32(EEMemoryMap::D9_CHCR, 0));
}

TEST_CASE("Scratchpad DMAC clocks are gated and inert")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();

  bus.write32(EEMemoryMap::D8_MADR, 0x1000);
  bus.write32(EEMemoryMap::D8_QWC, 2);
  bus.write32(EEMemoryMap::D8_SADR, 0x200);
  bus.write32(EEMemoryMap::D8_CHCR, DMACChannelControl::START);

  REQUIRE_FALSE(system.fromScratchpadDMAC().clockActive());
  bus.write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
  REQUIRE(system.fromScratchpadDMAC().clockActive());

  system.runMasterCycles(3);

  REQUIRE(bus.read32(EEMemoryMap::D8_MADR) == 0x1000);
  REQUIRE(bus.read32(EEMemoryMap::D8_QWC) == 2);
  REQUIRE(bus.read32(EEMemoryMap::D8_SADR) == 0x200);
  REQUIRE(
    bus.read32(EEMemoryMap::D8_CHCR) ==
    DMACChannelControl::START);
  REQUIRE(
    (bus.read32(EEMemoryMap::D_STAT) &
     DMACStatus::CHANNEL_8) == 0);
}

TEST_CASE("Scratchpad DMAC status bits drive the DMAC interrupt")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  DMACController &controller = system.dmacController();

  controller.signalChannelCompletion(DMACStatus::CHANNEL_8);
  controller.signalChannelCompletion(DMACStatus::CHANNEL_9);
  REQUIRE(
    (bus.read32(EEMemoryMap::D_STAT) &
     (DMACStatus::CHANNEL_8 | DMACStatus::CHANNEL_9)) ==
    (DMACStatus::CHANNEL_8 | DMACStatus::CHANNEL_9));
  REQUIRE_FALSE(controller.interruptPending());

  bus.write32(
    EEMemoryMap::D_STAT,
    DMACStatus::CHANNEL_8_MASK |
      DMACStatus::CHANNEL_9_MASK);
  REQUIRE(controller.interruptPending());

  bus.write32(EEMemoryMap::D_STAT, DMACStatus::CHANNEL_8);
  REQUIRE(controller.interruptPending());
  bus.write32(EEMemoryMap::D_STAT, DMACStatus::CHANNEL_9);
  REQUIRE_FALSE(controller.interruptPending());

  REQUIRE_THROWS_WITH(
    controller.signalChannelCompletion(
      DMACStatus::CHANNEL_8 | DMACStatus::CHANNEL_9),
    "Invalid DMAC completion channel.");
}

TEST_CASE("Scratchpad DMAC registers and status reset with the system")
{
  NekoSystem system;
  system.eeBus().write32(EEMemoryMap::D8_MADR, 0x1000);
  system.eeBus().write32(EEMemoryMap::D9_TADR, 0x2000);
  system.dmacController().signalChannelCompletion(
    DMACStatus::CHANNEL_8);
  system.eeBus().write32(
    EEMemoryMap::D_STAT,
    DMACStatus::CHANNEL_8_MASK);

  system.reset();

  REQUIRE(system.eeBus().read32(EEMemoryMap::D8_CHCR) == 0);
  REQUIRE(system.eeBus().read32(EEMemoryMap::D8_MADR) == 0);
  REQUIRE(system.eeBus().read32(EEMemoryMap::D8_QWC) == 0);
  REQUIRE(system.eeBus().read32(EEMemoryMap::D8_SADR) == 0);
  REQUIRE(system.eeBus().read32(EEMemoryMap::D9_CHCR) == 0);
  REQUIRE(system.eeBus().read32(EEMemoryMap::D9_MADR) == 0);
  REQUIRE(system.eeBus().read32(EEMemoryMap::D9_QWC) == 0);
  REQUIRE(system.eeBus().read32(EEMemoryMap::D9_TADR) == 0);
  REQUIRE(system.eeBus().read32(EEMemoryMap::D9_SADR) == 0);
  REQUIRE(
    (system.eeBus().read32(EEMemoryMap::D_STAT) &
     (DMACStatus::CHANNEL_8 |
      DMACStatus::CHANNEL_9 |
      DMACStatus::CHANNEL_8_MASK |
      DMACStatus::CHANNEL_9_MASK)) == 0);
}
