#include <stdexcept>

#include "catch.hpp"
#include "dmac_channel_state.hpp"
#include "dmac_controller.hpp"
#include "ee_bus.hpp"
#include "neko_system.hpp"
#include "scratchpad_dmac_channel.hpp"

namespace
{
  void requireQuadword(
    const EEQuadword &actual,
    const EEQuadword &expected)
  {
    REQUIRE(actual.low == expected.low);
    REQUIRE(actual.high == expected.high);
  }
}

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
  bus.write32(
    EEMemoryMap::D8_CHCR,
    DMACChannelControl::INTERLEAVE_MODE |
      DMACChannelControl::START);

  REQUIRE_FALSE(system.fromScratchpadDMAC().clockActive());
  bus.write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
  REQUIRE(system.fromScratchpadDMAC().clockActive());

  system.runMasterCycles(3);

  REQUIRE(bus.read32(EEMemoryMap::D8_MADR) == 0x1000);
  REQUIRE(bus.read32(EEMemoryMap::D8_QWC) == 2);
  REQUIRE(bus.read32(EEMemoryMap::D8_SADR) == 0x200);
  REQUIRE(
    bus.read32(EEMemoryMap::D8_CHCR) ==
    (DMACChannelControl::INTERLEAVE_MODE |
     DMACChannelControl::START));
  REQUIRE(
    (bus.read32(EEMemoryMap::D_STAT) &
     DMACStatus::CHANNEL_8) == 0);
}

TEST_CASE("fromSPR normal mode transfers qwords and wraps SADR")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  EEMemorySystem &memory = system.eeMemorySystem();
  const EEQuadword finalScratchpadQword = {
    UINT64_C(0x0123456789abcdef),
    UINT64_C(0xfedcba9876543210)
  };
  const EEQuadword wrappedScratchpadQword = {
    UINT64_C(0x1111222233334444),
    UINT64_C(0xaaaabbbbccccdddd)
  };
  REQUIRE(
    memory.writeScratchpadDMA128(
      0x3ff0,
      finalScratchpadQword) ==
    EEScratchpadAccessResult::Completed);
  REQUIRE(
    memory.writeScratchpadDMA128(
      0,
      wrappedScratchpadQword) ==
    EEScratchpadAccessResult::Completed);
  bus.write32(
    EEMemoryMap::D_STAT,
    DMACStatus::CHANNEL_8_MASK);
  bus.write32(EEMemoryMap::D8_MADR, 0x100);
  bus.write32(EEMemoryMap::D8_QWC, 2);
  bus.write32(EEMemoryMap::D8_SADR, 0x3ff0);
  bus.write32(EEMemoryMap::D8_CHCR, DMACChannelControl::START);

  system.clockMasterCycle();
  EEQuadword destination = {};
  REQUIRE(bus.readDMAC128(0x100, &destination));
  requireQuadword(destination, EEQuadword{});
  REQUIRE(bus.read32(EEMemoryMap::D8_MADR) == 0x100);
  REQUIRE(bus.read32(EEMemoryMap::D8_QWC) == 2);
  REQUIRE(bus.read32(EEMemoryMap::D8_SADR) == 0x3ff0);

  bus.write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
  system.clockMasterCycle();

  REQUIRE(bus.readDMAC128(0x100, &destination));
  requireQuadword(destination, finalScratchpadQword);
  REQUIRE(bus.read32(EEMemoryMap::D8_MADR) == 0x110);
  REQUIRE(bus.read32(EEMemoryMap::D8_QWC) == 1);
  REQUIRE(bus.read32(EEMemoryMap::D8_SADR) == 0);
  REQUIRE(system.fromScratchpadDMAC().active());
  REQUIRE_FALSE(system.interruptPending());

  system.clockMasterCycle();

  REQUIRE(bus.readDMAC128(0x110, &destination));
  requireQuadword(destination, wrappedScratchpadQword);
  REQUIRE(bus.read32(EEMemoryMap::D8_MADR) == 0x120);
  REQUIRE(bus.read32(EEMemoryMap::D8_QWC) == 0);
  REQUIRE(bus.read32(EEMemoryMap::D8_SADR) == 0x10);
  REQUIRE_FALSE(system.fromScratchpadDMAC().active());
  REQUIRE(
    (bus.read32(EEMemoryMap::D_STAT) &
     DMACStatus::CHANNEL_8) != 0);
  REQUIRE(system.interruptPending());
}

TEST_CASE("toSPR normal mode transfers qwords and wraps SADR")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  EEMemorySystem &memory = system.eeMemorySystem();
  const EEQuadword firstSource = {
    UINT64_C(0x0011223344556677),
    UINT64_C(0x8899aabbccddeeff)
  };
  const EEQuadword secondSource = {
    UINT64_C(0x1020304050607080),
    UINT64_C(0x90a0b0c0d0e0f000)
  };
  REQUIRE(bus.writeDMAC128(0x200, firstSource));
  REQUIRE(bus.writeDMAC128(0x210, secondSource));
  bus.write32(
    EEMemoryMap::D_STAT,
    DMACStatus::CHANNEL_9_MASK);
  bus.write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
  bus.write32(EEMemoryMap::D9_MADR, 0x200);
  bus.write32(EEMemoryMap::D9_QWC, 2);
  bus.write32(EEMemoryMap::D9_SADR, 0x3ff0);
  bus.write32(EEMemoryMap::D9_CHCR, DMACChannelControl::START);

  system.clockMasterCycle();

  EEQuadword scratchpad = {};
  REQUIRE(
    memory.readScratchpadDMA128(0x3ff0, &scratchpad) ==
    EEScratchpadAccessResult::Completed);
  requireQuadword(scratchpad, firstSource);
  REQUIRE(bus.read32(EEMemoryMap::D9_MADR) == 0x210);
  REQUIRE(bus.read32(EEMemoryMap::D9_QWC) == 1);
  REQUIRE(bus.read32(EEMemoryMap::D9_SADR) == 0);
  REQUIRE(system.toScratchpadDMAC().active());

  system.clockMasterCycle();

  REQUIRE(
    memory.readScratchpadDMA128(0, &scratchpad) ==
    EEScratchpadAccessResult::Completed);
  requireQuadword(scratchpad, secondSource);
  REQUIRE(bus.read32(EEMemoryMap::D9_MADR) == 0x220);
  REQUIRE(bus.read32(EEMemoryMap::D9_QWC) == 0);
  REQUIRE(bus.read32(EEMemoryMap::D9_SADR) == 0x10);
  REQUIRE_FALSE(system.toScratchpadDMAC().active());
  REQUIRE(
    (bus.read32(EEMemoryMap::D_STAT) &
     DMACStatus::CHANNEL_9) != 0);
  REQUIRE(system.interruptPending());
}

TEST_CASE("Scratchpad DMAC zero-length normal transfers complete")
{
  for (const ScratchpadDMACChannelKind kind : {
    ScratchpadDMACChannelKind::FromScratchpad,
    ScratchpadDMACChannelKind::ToScratchpad})
  {
    NekoSystem system;
    EEBus &bus = system.eeBus();
    const bool fromScratchpad =
      kind == ScratchpadDMACChannelKind::FromScratchpad;
    const std::uint32_t channelControl =
      fromScratchpad ?
        EEMemoryMap::D8_CHCR :
        EEMemoryMap::D9_CHCR;
    const std::uint32_t channelStatus =
      fromScratchpad ?
        DMACStatus::CHANNEL_8 :
        DMACStatus::CHANNEL_9;

    bus.write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
    bus.write32(channelControl, DMACChannelControl::START);
    system.clockMasterCycle();

    REQUIRE((bus.read32(channelControl) &
             DMACChannelControl::START) == 0);
    REQUIRE(
      (bus.read32(EEMemoryMap::D_STAT) &
       channelStatus) != 0);
  }
}

TEST_CASE("Scratchpad DMAC uses physical main-bus addresses")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  EEMemorySystem &memory = system.eeMemorySystem();
  const EEQuadword scratchpad = {
    UINT64_C(0x0123456789abcdef),
    UINT64_C(0xfedcba9876543210)
  };
  REQUIRE(
    memory.writeScratchpadDMA128(0, scratchpad) ==
    EEScratchpadAccessResult::Completed);
  REQUIRE(bus.writeDMAC128(0x100, EEQuadword{}));
  bus.write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
  bus.write32(EEMemoryMap::D8_MADR, UINT32_C(0x20000100));
  bus.write32(EEMemoryMap::D8_QWC, 1);
  bus.write32(EEMemoryMap::D8_SADR, 0);
  bus.write32(EEMemoryMap::D8_CHCR, DMACChannelControl::START);

  REQUIRE_THROWS_WITH(
    system.clockMasterCycle(),
    "fromSPR DMAC main-bus address is invalid.");

  EEQuadword mainMemory = {};
  REQUIRE(bus.readDMAC128(0x100, &mainMemory));
  requireQuadword(mainMemory, EEQuadword{});
  REQUIRE(bus.read32(EEMemoryMap::D8_MADR) == 0x20000100);
  REQUIRE(bus.read32(EEMemoryMap::D8_QWC) == 1);
  REQUIRE(bus.read32(EEMemoryMap::D8_SADR) == 0);
  REQUIRE(system.fromScratchpadDMAC().active());

  NekoSystem toScratchpadSystem;
  EEBus &toScratchpadBus = toScratchpadSystem.eeBus();
  EEMemorySystem &toScratchpadMemory =
    toScratchpadSystem.eeMemorySystem();
  const EEQuadword initialScratchpad = {
    UINT64_C(0xaaaaaaaa55555555),
    UINT64_C(0x0123456789abcdef)
  };
  REQUIRE(
    toScratchpadMemory.writeScratchpadDMA128(
      0,
      initialScratchpad) ==
    EEScratchpadAccessResult::Completed);
  REQUIRE(
    toScratchpadBus.writeDMAC128(
      0x100,
      scratchpad));
  toScratchpadBus.write32(
    EEMemoryMap::D_CTRL,
    DMACControl::DMA_ENABLE);
  toScratchpadBus.write32(
    EEMemoryMap::D9_MADR,
    UINT32_C(0x20000100));
  toScratchpadBus.write32(EEMemoryMap::D9_QWC, 1);
  toScratchpadBus.write32(EEMemoryMap::D9_SADR, 0);
  toScratchpadBus.write32(
    EEMemoryMap::D9_CHCR,
    DMACChannelControl::START);

  REQUIRE_THROWS_WITH(
    toScratchpadSystem.clockMasterCycle(),
    "toSPR DMAC main-bus address is invalid.");

  EEQuadword unchangedScratchpad = {};
  REQUIRE(
    toScratchpadMemory.readScratchpadDMA128(
      0,
      &unchangedScratchpad) ==
    EEScratchpadAccessResult::Completed);
  requireQuadword(unchangedScratchpad, initialScratchpad);
  REQUIRE(
    toScratchpadBus.read32(EEMemoryMap::D9_MADR) ==
    UINT32_C(0x20000100));
  REQUIRE(
    toScratchpadBus.read32(EEMemoryMap::D9_QWC) == 1);
  REQUIRE(
    toScratchpadBus.read32(EEMemoryMap::D9_SADR) == 0);
  REQUIRE(toScratchpadSystem.toScratchpadDMAC().active());
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
