#include <array>
#include <stdexcept>

#include "catch.hpp"
#include "gif_registers.hpp"
#include "neko_system.hpp"
#include "save_state_mutation.hpp"

namespace
{
  constexpr std::uint32_t PCR = 0x1000e020;
  constexpr std::uint32_t PCE = UINT32_C(0x80000000);
  constexpr std::uint32_t PCR_MASK = UINT32_C(0x83ff03ff);
  struct Endpoint
  {
    unsigned id;
    std::uint32_t chcr;
  };
  constexpr std::array<Endpoint, 6> ENDPOINTS = {{
    {1, 0x10009000}, {2, 0x1000a000}, {5, 0x1000c000},
    {6, 0x1000c400}, {8, 0x1000d000}, {9, 0x1000d400}
  }};

  void grant(NekoSystem &system, unsigned id)
  {
    switch (id)
    {
      case 1: system.vif1DMAC().clock(); return;
      case 2: system.gifDMAC().clock(); return;
      case 5: system.dmacController().clockSIF(system.sif0DMAC()); return;
      case 6: system.dmacController().clockSIF(system.sif1DMAC()); return;
      case 8: system.fromScratchpadDMAC().clock(); return;
      case 9: system.toScratchpadDMAC().clock(); return;
    }
    FAIL("Unexpected test DMA channel");
  }

  void armNormal(NekoSystem &system, const Endpoint &endpoint, unsigned count)
  {
    auto &bus = system.eeBus();
    bus.write32(endpoint.chcr + 0x10, 0x1000);
    bus.write32(endpoint.chcr + 0x20, count);
    bus.write32(endpoint.chcr, DMACChannelControl::START |
      (endpoint.id == 5 ? 0 : DMACChannelControl::FROM_MEMORY));
  }

  void armChain(NekoSystem &system, const Endpoint &endpoint,
    std::uint32_t options = 0)
  {
    system.eeBus().write32(endpoint.chcr + 0x30, 0x1000);
    system.eeBus().write32(endpoint.chcr,
      DMACChannelControl::START | DMACChannelControl::CHAIN_MODE |
      DMACChannelControl::FROM_MEMORY | options);
    system.dmacController().writeControl(DMACControl::DMA_ENABLE);
  }
}

TEST_CASE("DMAC priority register retains only documented fields",
  "[ee][dma][priority]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  REQUIRE(EEMemoryMap::D_PCR == PCR);
  const auto initialHash = system.eeStateHash();
  std::uint32_t value = 7;
  REQUIRE(bus.readData32(PCR, &value));
  REQUIRE(value == 0);
  REQUIRE(bus.writeData32(PCR, UINT32_MAX));
  REQUIRE(bus.read32(PCR) == PCR_MASK);
  REQUIRE(system.eeStateHash() != initialHash);
  std::uint8_t byte = 0;
  std::uint16_t half = 0;
  std::uint64_t wide = 0;
  REQUIRE_FALSE(bus.readData8(PCR, &byte));
  REQUIRE_FALSE(bus.readData16(PCR, &half));
  REQUIRE_FALSE(bus.readData64(PCR, &wide));
  REQUIRE_FALSE(bus.writeData8(PCR, 0));
  REQUIRE_FALSE(bus.writeData16(PCR, 0));
  REQUIRE_FALSE(bus.writeData64(PCR, 0));
  REQUIRE_THROWS_AS(bus.writeData32(PCR + 1, 0), std::invalid_argument);
  REQUIRE_FALSE(bus.writeData32(PCR + 4, 0));
  REQUIRE(bus.read32(PCR) == PCR_MASK);
  system.reset();
  REQUIRE(bus.read32(PCR) == 0);
}

TEST_CASE("DMAC priority gates every implemented channel without completing it",
  "[ee][dma][priority]")
{
  for (const auto endpoint : ENDPOINTS)
  {
    CAPTURE(endpoint.id);
    NekoSystem system;
    auto &bus = system.eeBus();
    REQUIRE(system.sif().pushSIF0Prologue({{1, 2, 3, 4}}) ==
      SIFTransferOutcome::Completed);
    armNormal(system, endpoint, 1);
    bus.write32(PCR, PCE);
    system.dmacController().writeControl(1);
    const auto sif = system.sifStateHash();
    grant(system, endpoint.id);
    REQUIRE(bus.read32(endpoint.chcr + 0x10) == 0x1000);
    REQUIRE(bus.read32(endpoint.chcr + 0x20) == 1);
    REQUIRE((bus.read32(endpoint.chcr) & DMACChannelControl::START) != 0);
    REQUIRE(system.dmacController().status() == 0);
    REQUIRE(system.sifStateHash() == sif);
    if (endpoint.id == 5) REQUIRE_FALSE(system.sif0DMAC().transferEnabled());
    if (endpoint.id == 6) REQUIRE_FALSE(system.sif1DMAC().transferEnabled());

    // A different channel's CDE must not release this request.
    const auto other = endpoint.id == 1 ? 2u : 1u;
    bus.write32(PCR, PCE | (1u << (16 + other)));
    grant(system, endpoint.id);
    REQUIRE(bus.read32(endpoint.chcr + 0x20) == 1);
    bus.write32(PCR, PCE | (1u << (16 + endpoint.id)));
    system.dmacController().writeControl(0);
    grant(system, endpoint.id);
    REQUIRE(bus.read32(endpoint.chcr + 0x20) == 1);
    system.dmacController().writeControl(1);
    grant(system, endpoint.id);
    REQUIRE(bus.read32(endpoint.chcr + 0x10) == 0x1010);
    REQUIRE(bus.read32(endpoint.chcr + 0x20) == 0);
    REQUIRE((bus.read32(endpoint.chcr) & DMACChannelControl::START) == 0);
    REQUIRE(system.dmacController().status() == (1u << endpoint.id));
    REQUIRE(bus.read32(PCR) == (PCE | (1u << (16 + endpoint.id))));
  }
}

TEST_CASE("DMAC disabled priority ignores CDE including zero count completion",
  "[ee][dma][priority]")
{
  for (const auto endpoint : ENDPOINTS)
  {
    CAPTURE(endpoint.id);
    NekoSystem system;
    armNormal(system, endpoint, 0);
    system.eeBus().write32(PCR, PCE);
    system.dmacController().writeControl(1);
    grant(system, endpoint.id);
    REQUIRE(system.dmacController().status() == 0);
    REQUIRE((system.eeBus().read32(endpoint.chcr) & DMACChannelControl::START) != 0);
    system.eeBus().write32(PCR, 0);
    grant(system, endpoint.id);
    REQUIRE(system.dmacController().status() == (1u << endpoint.id));
  }
}

TEST_CASE("DMAC accepted source tags control shared priority without losing fields",
  "[ee][dma][priority]")
{
  for (const auto endpoint : {ENDPOINTS[0], ENDPOINTS[1], ENDPOINTS[3]})
  {
    for (const auto initial : {0u, PCE})
    {
      for (const auto pce : {0u, 2u, 3u})
      {
        CAPTURE(endpoint.id);
        CAPTURE(initial);
        CAPTURE(pce);
        NekoSystem system;
        const auto fields = (1u << (16 + endpoint.id)) | 0x155;
        system.eeBus().write32(PCR, fields | initial);
        REQUIRE(system.eeBus().writeQuadword(0x1000,
          {{0x10000000u | (pce << 26), 0, 0, 0}}));
        REQUIRE(system.eeBus().writeQuadword(0x1010, {{0x78000000, 0, 0, 0}}));
        armChain(system, endpoint);
        grant(system, endpoint.id);
        const auto expected = pce == 0 ? initial : (pce == 3 ? PCE : 0);
        REQUIRE(system.eeBus().read32(PCR) == (fields | expected));
        REQUIRE(system.eeBus().read32(endpoint.chcr + 0x30) == 0x1010);
        REQUIRE(system.dmacController().status() == 0);
        grant(system, endpoint.id);
        REQUIRE(system.eeBus().read32(PCR) == fields);
        REQUIRE(system.dmacController().status() == (1u << endpoint.id));
      }
    }
  }
}

TEST_CASE("DMAC reserved tag priority rejects before tag transfer or state mutation",
  "[ee][dma][priority]")
{
  for (const auto endpoint : {ENDPOINTS[0], ENDPOINTS[1], ENDPOINTS[3]})
  {
    CAPTURE(endpoint.id);
    NekoSystem system;
    system.eeBus().write32(PCR, 0x3ff0000);
    REQUIRE(system.eeBus().writeQuadword(0x1000, {{0x74000001, 0, 0, 0}}));
    armChain(system, endpoint, endpoint.id == 6 ? 0 :
      DMACChannelControl::TAG_TRANSFER_ENABLE);
    const auto before = system.saveState();
    REQUIRE_THROWS_AS(grant(system, endpoint.id), std::invalid_argument);
    const bool unchanged = system.saveState() == before;
    REQUIRE(unchanged);
  }
}

TEST_CASE("DMAC tag priority waits for VIF tag FIFO acceptance",
  "[ee][dma][priority]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  REQUIRE(bus.writeGuestData128(EEMemoryMap::VIF1_FIFO,
    {UINT64_C(0x80000000), 0}) == EEDataWriteResult::Completed);
  bus.advanceGuestFIFOs();
  for (unsigned index = 0; index < 15; ++index)
    REQUIRE(bus.writeGuestData128(EEMemoryMap::VIF1_FIFO, {}) ==
      EEDataWriteResult::Completed);
  REQUIRE(system.vif1().fifoQuadwordCount() == 16);
  REQUIRE(bus.writeQuadword(0x1000, {{0x7c000000, 0, 0, 0}}));
  bus.write32(PCR, 1u << 17);
  armChain(system, ENDPOINTS[0], DMACChannelControl::TAG_TRANSFER_ENABLE);
  grant(system, 1);
  REQUIRE(system.vif1DMAC().stalledByVIF1());
  REQUIRE(bus.read32(PCR) == (1u << 17));
  REQUIRE(system.vif1DMAC().tagAddress() == 0x1000);
  bus.write32(EEMemoryMap::VIF1_FBRST, 1u << 3);
  bus.advanceGuestFIFOs();
  grant(system, 1);
  REQUIRE(bus.read32(PCR) == (PCE | (1u << 17)));
  REQUIRE(system.dmacController().status() == DMACStatus::CHANNEL_1);
  REQUIRE(system.vif1DMAC().transferredQuadwordCount() == 1);
}

TEST_CASE("DMAC tag priority waits for GIF tag FIFO acceptance",
  "[ee][dma][priority]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  REQUIRE(bus.writeQuadword(0x1000, {{0x7c000000, 0, 0, 0}}));
  bus.write32(PCR, 1u << 18);
  bus.write32(EEMemoryMap::GIF_MODE, GIFMode::M3R);
  armChain(system, ENDPOINTS[1], DMACChannelControl::TAG_TRANSFER_ENABLE);
  grant(system, 2);
  REQUIRE(system.gifDMAC().stalledByPATH3());
  REQUIRE(bus.read32(PCR) == (1u << 18));
  REQUIRE(system.gifDMAC().tagAddress() == 0x1000);
  bus.write32(EEMemoryMap::GIF_MODE, 0);
  grant(system, 2);
  REQUIRE(bus.read32(PCR) == (PCE | (1u << 18)));
  REQUIRE(system.dmacController().status() == DMACStatus::CHANNEL_2);
  REQUIRE(system.gifDMAC().transferredQuadwordCount() == 1);
}

TEST_CASE("DMAC tag priority changes eligibility across existing channel owners",
  "[ee][dma][priority]")
{
  for (const auto owner : {ENDPOINTS[0], ENDPOINTS[1], ENDPOINTS[3]})
  {
    CAPTURE(owner.id);
    NekoSystem system;
    auto &bus = system.eeBus();
    REQUIRE(bus.writeQuadword(0x1000, {{0x1c000000, 0, 0, 0}}));
    REQUIRE(bus.writeQuadword(0x1010, {{0x78000000, 0, 0, 0}}));
    REQUIRE(system.sif().pushSIF0Prologue({{0, 0, 0, 0}}) ==
      SIFTransferOutcome::Completed);
    for (const auto endpoint : ENDPOINTS)
    {
      if (endpoint.id != owner.id) armNormal(system, endpoint, 1);
    }
    bus.write32(PCR, 1u << (16 + owner.id));
    armChain(system, owner);
    grant(system, owner.id);
    for (const auto endpoint : ENDPOINTS)
    {
      if (endpoint.id == owner.id) continue;
      grant(system, endpoint.id);
      REQUIRE(bus.read32(endpoint.chcr + 0x20) == 1);
    }
    REQUIRE(system.dmacController().status() == 0);
    grant(system, owner.id);
    REQUIRE(bus.read32(PCR) == (1u << (16 + owner.id)));
    REQUIRE(bus.writeQuadword(0x1000, {}));
    for (const auto endpoint : ENDPOINTS)
    {
      if (endpoint.id == owner.id) continue;
      grant(system, endpoint.id);
      REQUIRE(bus.read32(endpoint.chcr + 0x20) == 0);
    }
  }
}

TEST_CASE("DMAC priority does not change on failed SIF source tag reads",
  "[ee][dma][priority]")
{
  NekoSystem system;
  system.eeBus().write32(PCR, PCE | (1u << 22) | 0x155);
  system.sif1DMAC().writeTagAddress(0x1fc00000);
  system.sif1DMAC().writeChannelControl(
    DMACChannelControl::START | DMACChannelControl::CHAIN_MODE);
  system.dmacController().writeControl(1);
  REQUIRE(system.dmacController().clockSIF(system.sif1DMAC()) ==
    SIF1DMABeatOutcome::BusError);
  REQUIRE(system.eeBus().read32(PCR) == (PCE | (1u << 22) | 0x155));
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 0);
}

TEST_CASE("DMAC accepted priority tags can pause their own payload",
  "[ee][dma][priority]")
{
  NekoSystem system;
  REQUIRE(system.eeBus().writeQuadword(0x1000, {{0x9c000001, 0, 0, 0}}));
  REQUIRE(system.eeBus().writeQuadword(0x1010, {{1, 2, 3, 4}}));
  armChain(system, ENDPOINTS[3], DMACChannelControl::TAG_INTERRUPT_ENABLE);
  grant(system, 6);
  REQUIRE(system.sif1DMAC().active());
  REQUIRE_FALSE(system.sif1DMAC().transferEnabled());
  REQUIRE(system.sif1DMAC().quadwordCount() == 1);
  REQUIRE(system.eeBus().read32(PCR) == PCE);
  const auto paused = system.saveState();
  grant(system, 6);
  const bool unchanged = system.saveState() == paused;
  REQUIRE(unchanged);
  system.eeBus().write32(PCR, PCE | (1u << 22));
  grant(system, 6);
  REQUIRE_FALSE(system.sif1DMAC().active());
  REQUIRE(system.sif().wordCount(SIFDirection::SIF1) == 4);
  REQUIRE(system.dmacController().status() == DMACStatus::CHANNEL_6);
  REQUIRE(system.eeBus().read32(PCR) == (PCE | (1u << 22)));
}

TEST_CASE("DMAC priority-blocked work persists and resumes without rearming",
  "[ee][dma][priority][save-state]")
{
  NekoSystem original;
  armNormal(original, ENDPOINTS[3], 1);
  original.eeBus().write32(PCR, PCE | 0x155);
  original.dmacController().writeControl(1);
  const auto hash = original.eeStateHash();
  const auto saved = original.saveState();
  NekoSystem restored;
  restored.loadState(saved);
  REQUIRE(restored.eeBus().read32(PCR) == (PCE | 0x155));
  REQUIRE(restored.eeStateHash() == hash);
  grant(restored, 6);
  const bool unchanged = restored.saveState() == saved;
  REQUIRE(unchanged);
  restored.eeBus().write32(PCR, 0x155);
  REQUIRE(restored.eeStateHash() != hash);
  grant(restored, 6);
  REQUIRE(restored.sif().wordCount(SIFDirection::SIF1) == 4);
  REQUIRE(restored.dmacController().status() == DMACStatus::CHANNEL_6);
}

TEST_CASE("DMAC malformed priority state rejects transactionally",
  "[ee][dma][priority][save-state]")
{
  NekoSystem original;
  original.eeBus().write32(PCR, PCE | (1u << 22));
  armNormal(original, ENDPOINTS[3], 1);
  NekoSystem destination;
  destination.runMasterCycles(3);
  const auto before = destination.saveState();
  for (const auto reserved : {1u << 10, 1u << 26, 1u << 30})
  {
    CAPTURE(reserved);
    SaveStateMutation mutation(original.saveState());
    mutation.writeScalar("system.gifDMAC.controllerPriorityControlRegister",
      PCE | reserved);
    mutation.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(mutation.bytes()), std::invalid_argument);
    const bool unchanged = destination.saveState() == before;
    REQUIRE(unchanged);
  }
}
