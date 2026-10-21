#include <array>
#include <stdexcept>

#include "catch.hpp"
#include "gif_registers.hpp"
#include "neko_system.hpp"
#include "save_state_mutation.hpp"

namespace
{
  constexpr std::uint32_t STADR = 0x1000e060;
  constexpr std::uint32_t SIS = 1u << 13;
  constexpr std::uint32_t SIM = 1u << 29;
  struct Drain
  {
    unsigned selector;
    std::uint32_t chcr;
  };
  constexpr std::array<Drain, 3> DRAINS = {{
    {1, 0x10009000}, {2, 0x1000a000}, {3, 0x1000c400}
  }};

  void grant(NekoSystem &system, Drain drain)
  {
    if (drain.selector == 1) system.vif1DMAC().clock();
    else if (drain.selector == 2) system.gifDMAC().clock();
    else system.dmacController().clockSIF(system.sif1DMAC());
  }

  void arm(NekoSystem &system, Drain drain, unsigned count,
    std::uint32_t address = 0x1000)
  {
    system.eeBus().write32(drain.chcr + 0x10, address);
    system.eeBus().write32(drain.chcr + 0x20, count);
    system.eeBus().write32(drain.chcr,
      DMACChannelControl::START | DMACChannelControl::FROM_MEMORY);
    system.dmacController().writeControl(1 | (drain.selector << 6));
  }
}

TEST_CASE("DMAC stall register selectors and sticky interrupt are shared",
  "[ee][dma][stall]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  REQUIRE(bus.writeData32(STADR, UINT32_MAX));
  REQUIRE(bus.read32(STADR) == 0x7ffffff0);
  for (unsigned source = 0; source < 4; ++source)
    for (unsigned drain = 0; drain < 4; ++drain)
    {
      const auto control = 1 | (source << 4) | (drain << 6);
      if (source == 1 && drain == 3)
      {
        const auto previous = system.dmacController().control();
        REQUIRE_THROWS_AS(system.dmacController().writeControl(control),
          std::invalid_argument);
        REQUIRE(system.dmacController().control() == previous);
      }
      else
      {
        system.dmacController().writeControl(control);
        REQUIRE(bus.read32(EEMemoryMap::D_CTRL) == control);
      }
    }
  std::uint8_t byte = 0;
  std::uint16_t half = 0;
  std::uint64_t wide = 0;
  REQUIRE_FALSE(bus.readData8(STADR, &byte));
  REQUIRE_FALSE(bus.readData16(STADR, &half));
  REQUIRE_FALSE(bus.readData64(STADR, &wide));
  REQUIRE_FALSE(bus.writeData8(STADR, 1));
  REQUIRE_FALSE(bus.writeData16(STADR, 1));
  REQUIRE_FALSE(bus.writeData64(STADR, 1));
  REQUIRE_FALSE(bus.writeData32(STADR + 4, 1));
  system.reset();
  REQUIRE(bus.read32(STADR) == 0);
  REQUIRE(system.dmacController().control() == 0);
}

TEST_CASE("DMAC normal drains wait and resume without clearing SIS or STR",
  "[ee][dma][stall]")
{
  for (const auto drain : DRAINS)
  {
    CAPTURE(drain.selector);
    NekoSystem system;
    auto &bus = system.eeBus();
    arm(system, drain, 1);
    bus.write32(STADR, 0x1000);
    bus.write32(EEMemoryMap::D_STAT, SIM);
    grant(system, drain);
    REQUIRE(bus.read32(drain.chcr + 0x20) == 1);
    REQUIRE((bus.read32(drain.chcr) & DMACChannelControl::START) != 0);
    REQUIRE(system.dmacController().status() == (SIM | SIS));
    REQUIRE(system.dmacController().interruptPending());
    bus.write32(EEMemoryMap::D_STAT, SIS);
    REQUIRE_FALSE(system.dmacController().interruptPending());
    grant(system, drain);
    REQUIRE(system.dmacController().interruptPending());
    bus.write32(STADR, 0x1010);
    grant(system, drain);
    REQUIRE(bus.read32(drain.chcr + 0x20) == 0);
    REQUIRE((bus.read32(drain.chcr) & DMACChannelControl::START) == 0);
    REQUIRE((system.dmacController().status() & SIS) != 0);
    bus.write32(EEMemoryMap::D_STAT, SIS);
    REQUIRE_FALSE(system.dmacController().interruptPending());
    REQUIRE((system.dmacController().status() & SIM) != 0);
    bus.write32(EEMemoryMap::D_STAT, SIM);
    REQUIRE((system.dmacController().status() & SIM) == 0);
  }
}

TEST_CASE("DMAC admission covers actual short and unaligned slices",
  "[ee][dma][stall]")
{
  for (const auto drain : DRAINS)
  {
    for (const auto address : {0x1000u, 0x1070u})
    {
      CAPTURE(drain.selector);
      CAPTURE(address);
      NekoSystem system;
      auto &bus = system.eeBus();
      arm(system, drain, 9, address);
      const auto firstSlice = 8u - ((address >> 4) & 7);
      bus.write32(STADR, address + firstSlice * 16 - 16);
      grant(system, drain);
      REQUIRE(bus.read32(drain.chcr + 0x20) == 9);
      bus.write32(STADR, address + firstSlice * 16);
      grant(system, drain);
      bus.write32(STADR, 0);
      for (unsigned index = 1; index < firstSlice; ++index)
        grant(system, drain);
      REQUIRE(bus.read32(drain.chcr + 0x20) == 9 - firstSlice);
      grant(system, drain);
      REQUIRE(bus.read32(drain.chcr + 0x20) == 9 - firstSlice);
    }
  }
}

TEST_CASE("DMAC only REFS source-chain payloads participate in stall control",
  "[ee][dma][stall]")
{
  for (const auto drain : DRAINS)
    for (const auto id : {0u, 1u, 2u, 3u, 4u, 7u})
    {
      CAPTURE(drain.selector);
      CAPTURE(id);
      NekoSystem system;
      auto &bus = system.eeBus();
      REQUIRE(bus.writeQuadword(0x1000, {{(id << 28) | 1, 0x2000, 0, 0}}));
      bus.write32(drain.chcr + 0x30, 0x1000);
      bus.write32(drain.chcr, DMACChannelControl::START |
        DMACChannelControl::CHAIN_MODE | DMACChannelControl::FROM_MEMORY);
      system.dmacController().writeControl(1 | (drain.selector << 6));
      grant(system, drain);
      REQUIRE(bus.read32(drain.chcr + 0x20) == 1);
      REQUIRE((system.dmacController().status() & SIS) == 0);
      grant(system, drain);
      REQUIRE(bus.read32(drain.chcr + 0x20) == (id == 4 ? 1 : 0));
      REQUIRE(((system.dmacController().status() & SIS) != 0) == (id == 4));
    }
}

TEST_CASE("DMAC selected normal producers publish only accepted physical writes",
  "[ee][dma][stall]")
{
  for (unsigned source = 0; source < 4; ++source)
    for (const unsigned producer : {5u, 8u})
    {
      CAPTURE(source);
      CAPTURE(producer);
      NekoSystem system;
      auto &bus = system.eeBus();
      const auto chcr = producer == 5 ? 0x1000c000u : 0x1000d000u;
      REQUIRE(system.sif().pushSIF0Prologue({{1, 2, 3, 4}}) ==
        SIFTransferOutcome::Completed);
      bus.write32(chcr + 0x10, 0x1000);
      bus.write32(chcr + 0x20, 1);
      bus.write32(chcr, DMACChannelControl::START);
      bus.write32(STADR, 0x5000);
      system.dmacController().writeControl(1 | (source << 4));
      if (producer == 5) system.dmacController().clockSIF(system.sif0DMAC());
      else system.fromScratchpadDMAC().clock();
      const bool selected = source == (producer == 5 ? 1u : 2u);
      REQUIRE(bus.read32(STADR) == (selected ? 0x1010 : 0x5000));
    }
}

TEST_CASE("DMAC stall and admitted slice continuation restore transactionally",
  "[ee][dma][stall][save-state]")
{
  for (const auto drain : DRAINS)
  {
    NekoSystem original;
    arm(original, drain, 3);
    original.eeBus().write32(STADR, 0x1030);
    grant(original, drain);
    original.eeBus().write32(STADR, 0);
    const auto saved = original.saveState();
    NekoSystem restored;
    restored.loadState(saved);
    REQUIRE(restored.eeStateHash() == original.eeStateHash());
    grant(original, drain);
    grant(restored, drain);
    REQUIRE(restored.eeBus().read32(drain.chcr + 0x20) == 1);
    const bool equal = original.saveState() == restored.saveState();
    REQUIRE(equal);
  }
  NekoSystem original;
  arm(original, DRAINS[2], 1);
  grant(original, DRAINS[2]);
  NekoSystem destination;
  destination.loadState(original.saveState());
  REQUIRE((destination.dmacController().status() & SIS) != 0);
  destination.eeBus().write32(STADR, 0x1010);
  grant(destination, DRAINS[2]);
  REQUIRE(destination.sif1DMAC().quadwordCount() == 0);
}

TEST_CASE("DMAC stall ignores zero packets and priority-disabled grants",
  "[ee][dma][stall]")
{
  for (const auto drain : DRAINS)
  {
    NekoSystem system;
    arm(system, drain, 0);
    grant(system, drain);
    REQUIRE((system.dmacController().status() & SIS) == 0);
    arm(system, drain, 1, 0x7ffffff0);
    system.dmacController().writePriorityControl(DMACPriority::ENABLE);
    const auto saved = system.saveState();
    grant(system, drain);
    REQUIRE(system.saveState() == saved);
    system.dmacController().writePriorityControl(0);
    // Stall admission precedes a physical read that would fail.
    REQUIRE_NOTHROW(grant(system, drain));
    REQUIRE((system.dmacController().status() & SIS) != 0);
    REQUIRE(system.eeBus().read32(drain.chcr + 0x20) == 1);
  }
}

TEST_CASE("DMAC FIFO retry does not reserve a drain slice before acceptance",
  "[ee][dma][stall]")
{
  for (const auto drain : {DRAINS[1], DRAINS[2]})
  {
    NekoSystem system;
    auto &bus = system.eeBus();
    if (drain.selector == 2)
      bus.write32(EEMemoryMap::GIF_MODE, GIFMode::M3R);
    else
      for (unsigned index = 0; index < 8; ++index)
        REQUIRE(system.sif().pushSIF1Quadword({{0, 0, 0, 0}}) ==
          SIFTransferOutcome::Completed);
    arm(system, drain, 2);
    bus.write32(STADR, 0x1020);
    grant(system, drain);
    REQUIRE(bus.read32(drain.chcr + 0x20) == 2);
    bus.write32(STADR, 0x1000);
    grant(system, drain);
    REQUIRE((system.dmacController().status() & SIS) != 0);
    REQUIRE(bus.read32(drain.chcr + 0x20) == 2);
  }
}

TEST_CASE("DMAC producers do not publish waits errors zero counts or interleave",
  "[ee][dma][stall]")
{
  for (const auto producer : {5u, 8u})
    for (unsigned scenario = 0; scenario < 4; ++scenario)
    {
      CAPTURE(producer);
      CAPTURE(scenario);
      NekoSystem system;
      auto &bus = system.eeBus();
      const auto chcr = producer == 5 ? 0x1000c000u : 0x1000d000u;
      const auto source = producer == 5 ? 1u : 2u;
      if (scenario != 0)
        REQUIRE(system.sif().pushSIF0Prologue({{1, 2, 3, 4}}) ==
          SIFTransferOutcome::Completed);
      bus.write32(STADR, 0x5000);
      bus.write32(chcr + 0x10, scenario == 1 ? 0x1fc00000 : 0x1000);
      bus.write32(chcr + 0x20, scenario == 2 ? 0 : 1);
      bus.write32(chcr, DMACChannelControl::START |
        (producer == 8 && scenario == 3 ? DMACChannelControl::INTERLEAVE_MODE : 0));
      system.dmacController().writeControl(1 | (source << 4));
      if (producer == 5) system.dmacController().clockSIF(system.sif0DMAC());
      else system.fromScratchpadDMAC().clock();
      // Scenario 3 is a normal accepted SIF0 transfer, but interleave for SPR.
      const auto expected = scenario == 3 && producer == 5 ? 0x1010u :
        scenario == 0 && producer == 8 ? 0x1010u : 0x5000u;
      REQUIRE(bus.read32(STADR) == expected);
    }
}

TEST_CASE("DMAC stall snapshots reject invalid control address and slice state",
  "[ee][dma][stall][save-state]")
{
  NekoSystem original;
  arm(original, DRAINS[2], 3);
  original.eeBus().write32(STADR, 0x1030);
  grant(original, DRAINS[2]);
  const auto saved = original.saveState();
  NekoSystem destination;
  destination.runMasterCycles(3);
  const auto before = destination.saveState();
  struct InvalidField
  {
    const char *path;
    std::uint32_t value;
  };
  for (const auto field : {
    InvalidField{"system.gifDMAC.controllerControlRegister", 0xd1},
    InvalidField{"system.gifDMAC.controllerControlRegister", 2},
    InvalidField{"system.gifDMAC.controllerStallAddressRegister", 1},
    InvalidField{"system.gifDMAC.controllerStallAddressRegister", 0x80000000},
    InvalidField{"system.sif1DMAC.sliceQuadwordsRemaining", 3},
    InvalidField{"system.sif1DMAC.sliceQuadwordsRemaining", 8},
    InvalidField{"system.vif1DMAC.sliceQuadwordsRemaining", 1},
    InvalidField{"system.gifDMAC.sliceQuadwordsRemaining", 1}})
  {
    CAPTURE(field.path);
    SaveStateMutation mutation(saved);
    mutation.writeScalar(field.path, field.value);
    mutation.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(mutation.bytes()), std::invalid_argument);
    REQUIRE(destination.saveState() == before);
  }
}

TEST_CASE("DMAC producer progress releases a drain only when its whole slice is ready",
  "[ee][dma][stall]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  arm(system, DRAINS[2], 2);
  bus.write32(EEMemoryMap::D8_MADR, 0x1000);
  bus.write32(EEMemoryMap::D8_QWC, 2);
  bus.write32(EEMemoryMap::D8_CHCR, DMACChannelControl::START);
  system.dmacController().writeControl(1 | (2u << 4) | (3u << 6));
  grant(system, DRAINS[2]);
  REQUIRE(system.sif1DMAC().quadwordCount() == 2);
  system.fromScratchpadDMAC().clock();
  REQUIRE(bus.read32(STADR) == 0x1010);
  grant(system, DRAINS[2]);
  REQUIRE(system.sif1DMAC().quadwordCount() == 2);
  system.fromScratchpadDMAC().clock();
  REQUIRE(bus.read32(STADR) == 0x1020);
  grant(system, DRAINS[2]);
  REQUIRE(system.sif1DMAC().quadwordCount() == 1);
  REQUIRE((system.dmacController().status() & SIS) != 0);
  const auto saved = system.saveState();
  NekoSystem restored;
  restored.loadState(saved);
  grant(system, DRAINS[2]);
  grant(restored, DRAINS[2]);
  REQUIRE(restored.sif1DMAC().quadwordCount() == 0);
  REQUIRE(system.saveState() == restored.saveState());
  REQUIRE((system.dmacController().status() & SIS) != 0);
}

TEST_CASE("DMAC aborted drains discard slice admission before a fresh start",
  "[ee][dma][stall]")
{
  for (const auto drain : DRAINS)
  {
    NekoSystem system;
    auto &bus = system.eeBus();
    arm(system, drain, 3);
    bus.write32(STADR, 0x1030);
    grant(system, drain);
    bus.write32(drain.chcr, DMACChannelControl::FROM_MEMORY);
    bus.write32(drain.chcr, DMACChannelControl::START |
      DMACChannelControl::FROM_MEMORY);
    bus.write32(STADR, 0x1010);
    grant(system, drain);
    REQUIRE(bus.read32(drain.chcr + 0x20) == 2);
    REQUIRE((system.dmacController().status() & SIS) != 0);
    system.reset();
    REQUIRE(bus.read32(STADR) == 0);
    REQUIRE(system.dmacController().status() == 0);
    REQUIRE((bus.read32(drain.chcr) & DMACChannelControl::START) == 0);
  }
}

TEST_CASE("DMAC aligned drain addresses cannot restore an admitted slice",
  "[ee][dma][stall][save-state]")
{
  for (const auto drain : DRAINS)
  {
    CAPTURE(drain.selector);
    NekoSystem original;
    arm(original, drain, 3);
    SaveStateMutation mutation(original.saveState());
    const char *path = drain.selector == 1 ?
      "system.vif1DMAC.sliceQuadwordsRemaining" : drain.selector == 2 ?
      "system.gifDMAC.sliceQuadwordsRemaining" :
      "system.sif1DMAC.sliceQuadwordsRemaining";
    mutation.writeScalar(path, 3);
    mutation.refreshChecksum();
    NekoSystem destination;
    const auto before = destination.saveState();
    REQUIRE_THROWS_AS(destination.loadState(mutation.bytes()), std::invalid_argument);
    REQUIRE(destination.saveState() == before);
  }
}
