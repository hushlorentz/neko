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
  REQUIRE(EEMemoryMap::D_SQWC == UINT32_C(0x1000e030));

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

  bus.write32(
    EEMemoryMap::D_SQWC,
    UINT32_C(0xffffffff));
  REQUIRE(
    bus.read32(EEMemoryMap::D_SQWC) ==
    UINT32_C(0x00ff00ff));
}

TEST_CASE("Scratchpad DMAC rejects unsupported channel modes")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();

  REQUIRE_THROWS_WITH(
    bus.write32(
      EEMemoryMap::D8_CHCR,
      DMACChannelControl::CHAIN_MODE),
    "fromSPR DMAC supports only normal and interleave modes.");
  REQUIRE_THROWS_WITH(
    bus.write32(
      EEMemoryMap::D9_CHCR,
      DMACChannelControl::MODE_MASK),
    "toSPR DMAC supports only normal and interleave modes.");
  REQUIRE_FALSE(
    bus.writeData32(
      EEMemoryMap::D9_CHCR,
      DMACChannelControl::CHAIN_MODE |
        DMACChannelControl::START));
  REQUIRE(bus.read32(EEMemoryMap::D8_CHCR) == 0);
  REQUIRE(bus.read32(EEMemoryMap::D9_CHCR) == 0);
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

TEST_CASE("Scratchpad DMAC interleave clocks are D_CTRL gated")
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

TEST_CASE("fromSPR interleave mode transfers and skips main memory")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  EEMemorySystem &memory = system.eeMemorySystem();
  const EEQuadword source[] = {
    {UINT64_C(0x0000000000000001), UINT64_C(0x10)},
    {UINT64_C(0x0000000000000002), UINT64_C(0x20)},
    {UINT64_C(0x0000000000000003), UINT64_C(0x30)},
    {UINT64_C(0x0000000000000004), UINT64_C(0x40)}
  };
  const std::uint32_t sourceAddresses[] = {
    0x3fd0,
    0x3fe0,
    0x3ff0,
    0
  };
  for (std::uint32_t index = 0; index < 4; ++index)
  {
    REQUIRE(
      memory.writeScratchpadDMA128(
        sourceAddresses[index],
        source[index]) ==
      EEScratchpadAccessResult::Completed);
  }
  bus.write32(
    EEMemoryMap::D_SQWC,
    (2u << 16) | 2u);
  bus.write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
  bus.write32(EEMemoryMap::D8_MADR, 0x100);
  bus.write32(EEMemoryMap::D8_QWC, 4);
  bus.write32(EEMemoryMap::D8_SADR, 0x3fd0);
  bus.write32(
    EEMemoryMap::D8_CHCR,
    DMACChannelControl::INTERLEAVE_MODE |
      DMACChannelControl::START);

  system.clockMasterCycle();
  REQUIRE(bus.read32(EEMemoryMap::D8_MADR) == 0x110);
  REQUIRE(bus.read32(EEMemoryMap::D8_QWC) == 3);
  REQUIRE(bus.read32(EEMemoryMap::D8_SADR) == 0x3fe0);

  bus.write32(EEMemoryMap::D_CTRL, 0);
  system.runMasterCycles(2);
  REQUIRE(bus.read32(EEMemoryMap::D8_MADR) == 0x110);
  REQUIRE(bus.read32(EEMemoryMap::D8_QWC) == 3);
  REQUIRE(bus.read32(EEMemoryMap::D8_SADR) == 0x3fe0);

  bus.write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
  system.clockMasterCycle();
  REQUIRE(bus.read32(EEMemoryMap::D8_MADR) == 0x140);
  REQUIRE(bus.read32(EEMemoryMap::D8_QWC) == 2);
  REQUIRE(bus.read32(EEMemoryMap::D8_SADR) == 0x3ff0);

  system.runMasterCycles(2);

  REQUIRE(bus.read32(EEMemoryMap::D8_MADR) == 0x180);
  REQUIRE(bus.read32(EEMemoryMap::D8_QWC) == 0);
  REQUIRE(bus.read32(EEMemoryMap::D8_SADR) == 0x10);
  REQUIRE_FALSE(system.fromScratchpadDMAC().active());
  REQUIRE(
    (bus.read32(EEMemoryMap::D_STAT) &
     DMACStatus::CHANNEL_8) != 0);

  const std::uint32_t destinationAddresses[] = {
    0x100,
    0x110,
    0x140,
    0x150
  };
  for (std::uint32_t index = 0; index < 4; ++index)
  {
    EEQuadword destination = {};
    REQUIRE(
      bus.readDMAC128(
        destinationAddresses[index],
        &destination));
    requireQuadword(destination, source[index]);
  }
  for (const std::uint32_t skippedAddress : {
    0x120u,
    0x130u,
    0x160u,
    0x170u})
  {
    EEQuadword skipped = {};
    REQUIRE(bus.readDMAC128(skippedAddress, &skipped));
    requireQuadword(skipped, EEQuadword{});
  }
}

TEST_CASE("toSPR interleave mode skips main-memory source rows")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  EEMemorySystem &memory = system.eeMemorySystem();
  const std::uint32_t sourceAddresses[] = {
    0x200,
    0x210,
    0x240,
    0x250
  };
  const EEQuadword source[] = {
    {UINT64_C(0x1111111111111111), UINT64_C(0x10)},
    {UINT64_C(0x2222222222222222), UINT64_C(0x20)},
    {UINT64_C(0x3333333333333333), UINT64_C(0x30)},
    {UINT64_C(0x4444444444444444), UINT64_C(0x40)}
  };
  for (std::uint32_t index = 0; index < 4; ++index)
  {
    REQUIRE(
      bus.writeDMAC128(
        sourceAddresses[index],
        source[index]));
  }
  bus.write32(
    EEMemoryMap::D_STAT,
    DMACStatus::CHANNEL_9_MASK);
  bus.write32(
    EEMemoryMap::D_SQWC,
    (2u << 16) | 2u);
  bus.write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
  bus.write32(EEMemoryMap::D9_MADR, 0x200);
  bus.write32(EEMemoryMap::D9_QWC, 4);
  bus.write32(EEMemoryMap::D9_SADR, 0);
  bus.write32(
    EEMemoryMap::D9_CHCR,
    DMACChannelControl::INTERLEAVE_MODE |
      DMACChannelControl::START);

  system.runMasterCycles(4);

  REQUIRE(bus.read32(EEMemoryMap::D9_MADR) == 0x280);
  REQUIRE(bus.read32(EEMemoryMap::D9_QWC) == 0);
  REQUIRE(bus.read32(EEMemoryMap::D9_SADR) == 0x40);
  REQUIRE_FALSE(system.toScratchpadDMAC().active());
  REQUIRE(system.interruptPending());
  for (std::uint32_t index = 0; index < 4; ++index)
  {
    EEQuadword destination = {};
    REQUIRE(
      memory.readScratchpadDMA128(
        index * 16,
        &destination) ==
      EEScratchpadAccessResult::Completed);
    requireQuadword(destination, source[index]);
  }
}

TEST_CASE("Zero D_SQWC fields use contiguous transfer semantics")
{
  for (const std::uint32_t interleaveSize : {
    UINT32_C(0x00000003),
    UINT32_C(0x00020000)})
  {
    NekoSystem system;
    EEBus &bus = system.eeBus();
    EEMemorySystem &memory = system.eeMemorySystem();
    const EEQuadword source[] = {
      {UINT64_C(0x0123456789abcdef), UINT64_C(0x10)},
      {UINT64_C(0xfedcba9876543210), UINT64_C(0x20)}
    };
    REQUIRE(
      memory.writeScratchpadDMA128(0, source[0]) ==
      EEScratchpadAccessResult::Completed);
    REQUIRE(
      memory.writeScratchpadDMA128(0x10, source[1]) ==
      EEScratchpadAccessResult::Completed);
    bus.write32(EEMemoryMap::D_SQWC, interleaveSize);
    bus.write32(
      EEMemoryMap::D_CTRL,
      DMACControl::DMA_ENABLE);
    bus.write32(EEMemoryMap::D8_MADR, 0x300);
    bus.write32(EEMemoryMap::D8_QWC, 2);
    bus.write32(EEMemoryMap::D8_SADR, 0);
    bus.write32(
      EEMemoryMap::D8_CHCR,
      DMACChannelControl::INTERLEAVE_MODE |
        DMACChannelControl::START);

    system.runMasterCycles(2);

    REQUIRE(bus.read32(EEMemoryMap::D8_MADR) == 0x320);
    for (std::uint32_t index = 0; index < 2; ++index)
    {
      EEQuadword destination = {};
      REQUIRE(
        bus.readDMAC128(
          0x300 + index * 16,
          &destination));
      requireQuadword(destination, source[index]);
    }
  }
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

TEST_CASE("Scratchpad DMAC zero-length transfers complete")
{
  for (const ScratchpadDMACChannelKind kind : {
    ScratchpadDMACChannelKind::FromScratchpad,
    ScratchpadDMACChannelKind::ToScratchpad})
  {
    for (const std::uint32_t mode : {
      0u,
      DMACChannelControl::INTERLEAVE_MODE})
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

      bus.write32(
        EEMemoryMap::D_CTRL,
        DMACControl::DMA_ENABLE);
      bus.write32(
        channelControl,
        mode | DMACChannelControl::START);
      system.clockMasterCycle();

      REQUIRE((bus.read32(channelControl) &
               DMACChannelControl::START) == 0);
      REQUIRE(
        (bus.read32(EEMemoryMap::D_STAT) &
         channelStatus) != 0);
    }
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

  for (const std::uint32_t alias : {
    UINT32_C(0x80000100),
    UINT32_C(0xa0000100),
    UINT32_C(0x20000100),
    UINT32_C(0x30000100)})
  {
    EEQuadword value = {};
    REQUIRE_FALSE(bus.readDMAC128(alias, &value));
    REQUIRE_FALSE(bus.writeDMAC128(alias, scratchpad));
  }
}

TEST_CASE("Scratchpad DMAC normal mode reaches VU data-memory mappings")
{
  struct Endpoint
  {
    std::uint32_t address;
    VPU &(NekoSystem::*vpu)();
    std::size_t qwordIndex;
  };
  const Endpoint endpoints[] = {
    {UINT32_C(0x11004030), &NekoSystem::vu0, 3},
    {UINT32_C(0x11005040), &NekoSystem::vu0, 4},
    {UINT32_C(0x1100c050), &NekoSystem::vu1, 5}
  };

  for (const Endpoint &endpoint : endpoints)
  {
    NekoSystem system;
    EEBus &bus = system.eeBus();
    EEMemorySystem &memory = system.eeMemorySystem();
    VPU &vpu = (system.*endpoint.vpu)();
    const EEQuadword fromScratchpad = {
      UINT64_C(0x7766554433221100),
      UINT64_C(0xffeeddccbbaa9988)
    };
    const EEQuadword toScratchpad = {
      UINT64_C(0x0123456789abcdef),
      UINT64_C(0xfedcba9876543210)
    };
    REQUIRE(
      memory.writeScratchpadDMA128(
        0,
        fromScratchpad) ==
      EEScratchpadAccessResult::Completed);
    bus.write32(
      EEMemoryMap::D_CTRL,
      DMACControl::DMA_ENABLE);
    bus.write32(EEMemoryMap::D8_MADR, endpoint.address);
    bus.write32(EEMemoryMap::D8_QWC, 1);
    bus.write32(EEMemoryMap::D8_SADR, 0);
    bus.write32(
      EEMemoryMap::D8_CHCR,
      DMACChannelControl::START);

    system.clockMasterCycle();

    const std::array<std::uint32_t, 4> vuValue =
      vpu.readDataQuadword(endpoint.qwordIndex);
    REQUIRE(vuValue[0] == UINT32_C(0x33221100));
    REQUIRE(vuValue[1] == UINT32_C(0x77665544));
    REQUIRE(vuValue[2] == UINT32_C(0xbbaa9988));
    REQUIRE(vuValue[3] == UINT32_C(0xffeeddcc));

    vpu.writeDataQuadword(
      endpoint.qwordIndex,
      {
        UINT32_C(0x89abcdef),
        UINT32_C(0x01234567),
        UINT32_C(0x76543210),
        UINT32_C(0xfedcba98)
      });
    bus.write32(EEMemoryMap::D9_MADR, endpoint.address);
    bus.write32(EEMemoryMap::D9_QWC, 1);
    bus.write32(EEMemoryMap::D9_SADR, 0x10);
    bus.write32(
      EEMemoryMap::D9_CHCR,
      DMACChannelControl::START);

    system.clockMasterCycle();

    EEQuadword scratchpad = {};
    REQUIRE(
      memory.readScratchpadDMA128(
        0x10,
        &scratchpad) ==
      EEScratchpadAccessResult::Completed);
    requireQuadword(scratchpad, toScratchpad);
  }
}

TEST_CASE("Scratchpad DMAC VU endpoints reject interleave mode")
{
  for (const ScratchpadDMACChannelKind kind : {
    ScratchpadDMACChannelKind::FromScratchpad,
    ScratchpadDMACChannelKind::ToScratchpad})
  {
    NekoSystem system;
    EEBus &bus = system.eeBus();
    EEMemorySystem &memory = system.eeMemorySystem();
    const bool fromScratchpad =
      kind == ScratchpadDMACChannelKind::FromScratchpad;
    const EEQuadword sentinel = {
      UINT64_C(0x0123456789abcdef),
      UINT64_C(0xfedcba9876543210)
    };
    system.vu0().writeDataQuadword(
      0,
      {
        UINT32_C(0x89abcdef),
        UINT32_C(0x01234567),
        UINT32_C(0x76543210),
        UINT32_C(0xfedcba98)
      });
    REQUIRE(
      memory.writeScratchpadDMA128(0, sentinel) ==
      EEScratchpadAccessResult::Completed);
    bus.write32(
      EEMemoryMap::D_SQWC,
      1 | (1u << 16));
    bus.write32(
      EEMemoryMap::D_CTRL,
      DMACControl::DMA_ENABLE);
    bus.write32(
      fromScratchpad ?
        EEMemoryMap::D8_MADR :
        EEMemoryMap::D9_MADR,
      UINT32_C(0x11004000));
    bus.write32(
      fromScratchpad ?
        EEMemoryMap::D8_QWC :
        EEMemoryMap::D9_QWC,
      1);
    bus.write32(
      fromScratchpad ?
        EEMemoryMap::D8_SADR :
        EEMemoryMap::D9_SADR,
      0);
    bus.write32(
      fromScratchpad ?
        EEMemoryMap::D8_CHCR :
        EEMemoryMap::D9_CHCR,
      DMACChannelControl::INTERLEAVE_MODE |
        DMACChannelControl::START);

    REQUIRE_THROWS_WITH(
      system.clockMasterCycle(),
      "SPR DMAC VU memory endpoints require normal mode.");

    EEQuadword unchangedScratchpad = {};
    REQUIRE(
      memory.readScratchpadDMA128(
        0,
        &unchangedScratchpad) ==
      EEScratchpadAccessResult::Completed);
    requireQuadword(unchangedScratchpad, sentinel);
    const std::array<std::uint32_t, 4> unchangedVU =
      system.vu0().readDataQuadword(0);
    REQUIRE(unchangedVU[0] == UINT32_C(0x89abcdef));
    REQUIRE(unchangedVU[1] == UINT32_C(0x01234567));
    REQUIRE(unchangedVU[2] == UINT32_C(0x76543210));
    REQUIRE(unchangedVU[3] == UINT32_C(0xfedcba98));
  }
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
  system.eeBus().write32(
    EEMemoryMap::D_SQWC,
    UINT32_C(0x00120034));
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
  REQUIRE(system.eeBus().read32(EEMemoryMap::D_SQWC) == 0);
  REQUIRE(
    (system.eeBus().read32(EEMemoryMap::D_STAT) &
     (DMACStatus::CHANNEL_8 |
      DMACStatus::CHANNEL_9 |
      DMACStatus::CHANNEL_8_MASK |
      DMACStatus::CHANNEL_9_MASK)) == 0);
}
