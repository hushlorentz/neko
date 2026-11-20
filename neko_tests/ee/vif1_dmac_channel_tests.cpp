#include <cstdint>
#include <stdexcept>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "gif_dmac_channel.hpp"
#include "iop/iop_trace_test_utils.hpp"
#include "neko_system.hpp"
#include "save_state_mutation.hpp"
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
    bus->write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
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
    bus->write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
    bus->write32(EEMemoryMap::D1_TADR, address);
    bus->write32(
      EEMemoryMap::D1_CHCR,
      GIFDMACChannelControl::FROM_MEMORY |
      GIFDMACChannelControl::CHAIN_MODE |
      options |
      GIFDMACChannelControl::START);
  }

  void writeScratchpadQuadword(NekoSystem &system,
    std::uint32_t address, const GIFQuadword &value)
  {
    REQUIRE(system.eeMemorySystem().writeScratchpadDMA128(address, {
      value[0] | (static_cast<std::uint64_t>(value[1]) << 32),
      value[2] | (static_cast<std::uint64_t>(value[3]) << 32)
    }) == EEScratchpadAccessResult::Completed);
  }

  void prepareScratchpadChainGuest(NekoSystem &system)
  {
    auto &bus = system.eeBus();
    writeScratchpadQuadword(system, 0x2290,
      dmaTag(GIFDMATagID::Call, 0, 0x2000, false, 0x07001111));
    REQUIRE(bus.writeQuadword(0x2000,
      dmaTag(GIFDMATagID::Return, 1, 0, false, 0x01000404)));
    REQUIRE(bus.writeQuadword(0x2010, {0x07002222, 0, 0, 0}));
    writeScratchpadQuadword(system, 0x22a0, dmaTag(GIFDMATagID::End, 1));
    writeScratchpadQuadword(system, 0x22b0, {0x07003333, 0, 0, 0});
    REQUIRE(bus.writeQuadword(0x2290, dmaTag(GIFDMATagID::End, 0)));
    const std::uint32_t program[] = {
      0x3c10b000, // lui s0, 0xb000
      0x36109000, // ori s0, s0, 0x9000
      0x3c138000, // lui s3, 0x8000
      0x36732290, // ori s3, s3, 0x2290
      0x10000001, // beq zero, zero, next
      0xae130030, // sw s3, 0x30(s0), delay slot
      0x34080145, // ori t0, zero, chain|TTE|STR|DIR
      0xae080000, // sw t0, 0(s0)
      0x1000ffff, // bounded idle loop
      0
    };
    for (std::size_t index = 0; index < sizeof(program) / sizeof(program[0]); ++index)
    {
      bus.write32(0x1000 + static_cast<std::uint32_t>(4 * index), program[index]);
    }
    bus.write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
    system.eeCore().startExecution(0xa0001000);
  }
}

TEST_CASE("VIF1 DMAC stopped direction programming does not admit reverse transfers",
  "[ee][vif1-stopped]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  bus.write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
  bus.write32(EEMemoryMap::D1_MADR, 0x1000);
  bus.write32(EEMemoryMap::D1_QWC, 1);
  const auto status = system.dmacController().status();
  for (const auto control : {0u, 0x123400e4u, 1u, 0u})
  {
    CAPTURE(control);
    REQUIRE(bus.writeData32(EEMemoryMap::D1_CHCR, control));
    REQUIRE(bus.read32(EEMemoryMap::D1_CHCR) == control);
    const auto hash = system.eeStateHash();
    for (const auto start : {0x100u, 0x104u, 0x123401e4u})
    {
      REQUIRE_FALSE(bus.writeData32(EEMemoryMap::D1_CHCR, start));
      REQUIRE_THROWS_AS(bus.write32(EEMemoryMap::D1_CHCR, start),
        std::invalid_argument);
      REQUIRE(system.eeStateHash() == hash);
    }
    system.runMasterCycles(2);
    REQUIRE(bus.read32(EEMemoryMap::D1_CHCR) == control);
    REQUIRE(bus.read32(EEMemoryMap::D1_MADR) == 0x1000);
    REQUIRE(bus.read32(EEMemoryMap::D1_QWC) == 1);
    REQUIRE(system.vif1().fifoQuadwordCount() == 0);
    REQUIRE(system.vif1DMAC().transferredQuadwordCount() == 0);
    REQUIRE(system.dmacController().status() == status);
  }
  REQUIRE(bus.writeQuadword(0x1000, {}));
  REQUIRE(bus.writeData32(EEMemoryMap::D1_CHCR, 0x101));
  const auto activeHash = system.eeStateHash();
  REQUIRE_FALSE(bus.writeData32(EEMemoryMap::D1_CHCR, 0x181));
  REQUIRE(system.eeStateHash() == activeHash);
  system.clockMasterCycle();
  REQUIRE(system.vif1DMAC().transferredQuadwordCount() == 1);
  REQUIRE(bus.read32(EEMemoryMap::D1_CHCR) == 1);
  REQUIRE(bus.writeData32(EEMemoryMap::D1_CHCR, 0));
  REQUIRE(bus.read32(EEMemoryMap::D1_CHCR) == 0);
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
     DMACStatus::CHANNEL_1) != 0);

  system.clockMasterCycle();
  REQUIRE(system.vif1().wordsIngested() == 8);
  REQUIRE(system.vif1DMAC().transferredQuadwordCount() == 2);

  bus.write32(
    EEMemoryMap::D_STAT,
    DMACStatus::CHANNEL_1_MASK);
  REQUIRE(system.interruptPending());
  bus.write32(EEMemoryMap::D_STAT, DMACStatus::CHANNEL_1);
  REQUIRE_FALSE(system.interruptPending());
}

TEST_CASE("VIF1 DMAC scratchpad payloads preserve bank selection across wrap",
  "[ee][dma][vif1-spr]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  REQUIRE(system.eeMemorySystem().writeScratchpadDMA128(0x3ff0,
    {0x07001234, 0}) == EEScratchpadAccessResult::Completed);
  REQUIRE(system.eeMemorySystem().writeScratchpadDMA128(0,
    {0x07005678, 0}) == EEScratchpadAccessResult::Completed);
  REQUIRE(bus.writeQuadword(0x3ff0, {0x0700dead, 0, 0, 0}));
  REQUIRE(bus.writeQuadword(0x4000, {0x0700beef, 0, 0, 0}));

  startNormalTransfer(&bus, 0xfffffff0, 2);
  system.runMasterCycles(4);
  REQUIRE(system.vif1().mark() == 0x5678);
  REQUIRE(system.vif1DMAC().memoryAddress() == 0x80000010);
  REQUIRE(system.vif1DMAC().quadwordCount() == 0);
  REQUIRE(system.vif1DMAC().channelControl() == 1);
  REQUIRE(system.vif1DMAC().transferredQuadwordCount() == 2);
  REQUIRE(system.dmacController().status() == DMACStatus::CHANNEL_1);
  REQUIRE(bus.read32(0x3ff0) == 0x0700dead);
}

TEST_CASE("VIF1 DMAC tag mismatch masks preserve actual payload and cancellation",
  "[ee][dma][vif-err]")
{
  for (const auto mask : {0u, 2u})
  {
    CAPTURE(mask);
    NekoSystem system;
    auto &bus = system.eeBus();
    auto &vif = system.vif1();
    REQUIRE(bus.writeData32(0x10003c20, mask));
    vif.ingestWord(0x30000000);
    vif.ingestWord(0x11111111);
    REQUIRE(bus.writeQuadword(0x1000,
      dmaTag(GIFDMATagID::End, 0, 0, false, 0x22222222, 0x33333333)));
    startSourceChain(&bus, 0x1000, GIFDMACChannelControl::TAG_TRANSFER_ENABLE);
    system.vif1DMAC().clock();
    vif.advanceFIFO();
    if (mask == 0)
    {
      REQUIRE(vif.payloadWordsRemaining() == 3);
      REQUIRE((bus.read32(0x10003c00) & (1u << 12)) != 0);
      REQUIRE(bus.writeData32(0x10003c20, 2));
      vif.advanceFIFO();
      REQUIRE(vif.payloadWordsRemaining() == 3);
      REQUIRE(bus.writeData32(0x10003c10, 8));
      vif.advanceFIFO();
    }
    REQUIRE(vif.payloadWordsRemaining() == 1);
    REQUIRE(vif.row(0) == 0x11111111);
    REQUIRE(vif.row(1) == 0x22222222);
    REQUIRE(vif.row(2) == 0x33333333);
    REQUIRE(vif.submitQuadword({0x44444444, 0x07001234, 0, 0}));
    vif.advanceFIFO();
    REQUIRE_FALSE(vif.awaitingPayload());
    REQUIRE(vif.row(3) == 0x44444444);
    REQUIRE(vif.mark() == 0x1234);
    REQUIRE((bus.read32(0x10003c00) & (3u << 12)) == 0);
  }
}

TEST_CASE("VIF1 DMAC preserves progress while its FIFO is full",
  "[ee][dma][active-stop]")
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

  REQUIRE(bus.writeData32(EEMemoryMap::D1_CHCR, 0));
  REQUIRE(bus.read32(EEMemoryMap::D1_CHCR) == 1);
  REQUIRE_FALSE(system.vif1DMAC().stalledByVIF1());
  REQUIRE(system.vif1().fifoQuadwordCount() == 16);
  REQUIRE(system.dmacController().status() == 0);
  bus.write32(EEMemoryMap::D1_CHCR, 0x101);

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
       DMACStatus::CHANNEL_1) != 0);
  }
}

  TEST_CASE("VIF1 DMAC scratchpad tag references retain their own source bank",
    "[ee][dma][vif1-spr]")
  {
    for (const auto tagBank : {0u, 0x80000000u})
    {
      for (const auto dataBank : {0u, 0x80000000u})
      {
        CAPTURE(tagBank);
        CAPTURE(dataBank);
        NekoSystem system;
        const auto tag = dmaTag(GIFDMATagID::ReferenceEnd, 1, dataBank | 0x3000);
        if (tagBank == 0)
        {
          REQUIRE(system.eeBus().writeQuadword(0x2000, tag));
        }
        else
        {
          writeScratchpadQuadword(system, 0x2000, tag);
        }
        REQUIRE(system.eeBus().writeQuadword(0x3000, {0x07001234, 0, 0, 0}));
        writeScratchpadQuadword(system, 0x3000, {0x07005678, 0, 0, 0});
        startSourceChain(&system.eeBus(), tagBank | 0x2000);
        system.runMasterCycles(3);
        REQUIRE(system.vif1().mark() == (dataBank == 0 ? 0x1234 : 0x5678));
        REQUIRE(system.vif1DMAC().memoryAddress() == (dataBank | 0x3010));
        REQUIRE(system.vif1DMAC().tagAddress() == (tagBank | 0x2010));
        REQUIRE_FALSE(system.vif1DMAC().clockActive());
      }
    }
  }

  TEST_CASE("VIF1 DMAC scratchpad tag waits leave chain state untouched",
    "[ee][dma][vif1-spr]")
  {
    NekoSystem system;
    auto &bus = system.eeBus();
    writeScratchpadQuadword(system, 0x2290,
      dmaTag(GIFDMATagID::End, 1, 0, false, 0x07001234));
    writeScratchpadQuadword(system, 0x22a0, {0x07005678, 0, 0, 0});
    for (std::size_t index = 0; index < 16; ++index)
    {
      REQUIRE(bus.writeGuestData128(EEMemoryMap::VIF1_FIFO, {}) ==
        EEDataWriteResult::Completed);
    }
    startSourceChain(&bus, 0x80002290, GIFDMACChannelControl::TAG_TRANSFER_ENABLE);
    system.vif1DMAC().clock();
    REQUIRE(system.vif1DMAC().stalledByVIF1());
    REQUIRE(system.vif1DMAC().tagAddress() == 0x80002290);
    REQUIRE(system.vif1DMAC().channelControl() == 0x145);
    REQUIRE(system.vif1DMAC().quadwordCount() == 0);
    REQUIRE(system.vif1DMAC().transferredQuadwordCount() == 0);
    REQUIRE(system.dmacController().status() == 0);
    const auto hash = system.eeStateHash();
    REQUIRE_FALSE(bus.writeData32(EEMemoryMap::D1_TADR, 0x80002000));
    REQUIRE_FALSE(bus.writeData32(EEMemoryMap::D1_MADR, 0x80003000));
    REQUIRE_FALSE(bus.writeData32(EEMemoryMap::D1_ASR0, 0x80002000));
    REQUIRE(system.eeStateHash() == hash);
    system.vif1DMAC().clock();
    REQUIRE(system.eeStateHash() == hash);
    system.runMasterCycles(20);
    REQUIRE(system.vif1().mark() == 0x5678);
    REQUIRE(system.vif1DMAC().transferredQuadwordCount() == 2);
    REQUIRE(system.vif1DMAC().tagAddress() == 0x800022b0);
  }

  TEST_CASE("VIF1 scratchpad guest chains agree cold restored traced and plain",
    "[ee][dma][vif1-spr][conformance]")
  {
    NekoSystem origin;
    prepareScratchpadChainGuest(origin);
    const auto staged = origin.saveState();
    std::uint64_t checkpointCycle = 0;
    while (checkpointCycle < 32 && origin.vif1DMAC().addressStack(0) != 0x800022a0)
    {
      origin.clockMasterCycle();
      ++checkpointCycle;
    }
    REQUIRE(checkpointCycle < 32);
    REQUIRE((origin.vif1DMAC().channelControl() & 0x30) == 0x10);
    REQUIRE(origin.vif1DMAC().tagAddress() == 0x2000);
    const auto active = origin.saveState();
    origin.runMasterCycles(96 - checkpointCycle);
    REQUIRE(origin.vif1().mark() == 0x3333);
    REQUIRE(origin.vif1().cycle() == 0x0404);
    REQUIRE(origin.vif1DMAC().transferredQuadwordCount() == 5);
    REQUIRE(origin.vif1DMAC().tagAddress() == 0x800022c0);
    REQUIRE(origin.vif1DMAC().memoryAddress() == 0x800022c0);
    REQUIRE((origin.vif1DMAC().channelControl() & 0x130) == 0);
    REQUIRE(origin.dmacController().status() == DMACStatus::CHANNEL_1);
    REQUIRE((origin.eeCore().cop0Register(EECOP0Register::Cause) & 0x7c) == 0);
    const auto expected = origin.saveState();

    for (const auto restoredAt : {UINT64_C(0), checkpointCycle})
    {
      CAPTURE(restoredAt);
      NekoSystem coldTraced;
      NekoSystem coldPlain;
      NekoSystem restoredTraced;
      NekoSystem restoredPlain;
      prepareScratchpadChainGuest(coldTraced);
      prepareScratchpadChainGuest(coldPlain);
      const auto &checkpoint = restoredAt == 0 ? staged : active;
      restoredTraced.loadState(checkpoint);
      restoredPlain.loadState(checkpoint);
      REQUIRE(restoredTraced.saveState() == checkpoint);
      coldTraced.startTrace();
      restoredTraced.startTrace();
      coldTraced.runMasterCycles(96);
      coldPlain.runMasterCycles(96);
      restoredTraced.runMasterCycles(96 - restoredAt);
      restoredPlain.runMasterCycles(96 - restoredAt);
      REQUIRE(coldTraced.saveState() == expected);
      REQUIRE(coldPlain.saveState() == expected);
      REQUIRE(restoredTraced.saveState() == expected);
      REQUIRE(restoredPlain.saveState() == expected);
      IOPTraceTest::Events suffix;
      for (const auto &event : coldTraced.trace())
      {
        if (event.masterCycle > restoredAt)
        {
          suffix.push_back(event);
        }
      }
      IOPTraceTest::requireEvents(restoredTraced.trace(), suffix);
    }

    NekoSystem destination;
    const auto unchanged = destination.saveState();
    for (const auto *field : {"memoryAddressRegister", "tagAddressRegister",
      "addressStackRegisters[0].value", "addressStackRegisters[1].value"})
    {
      SaveStateMutation corrupt(active);
      corrupt.writeScalar(std::string("system.vif1DMAC.") + field, 0x80002291);
      corrupt.refreshChecksum();
      REQUIRE_THROWS_AS(destination.loadState(corrupt.bytes()), std::invalid_argument);
      REQUIRE(destination.saveState() == unchanged);
    }
    destination.loadState(active);
    destination.reset();
    REQUIRE(destination.vif1DMAC().memoryAddress() == 0);
    REQUIRE(destination.vif1DMAC().tagAddress() == 0);
    REQUIRE(destination.vif1DMAC().addressStack(0) == 0);
  }
