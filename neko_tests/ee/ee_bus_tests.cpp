#include <cstdint>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "gif_registers.hpp"
#include "neko_system.hpp"

namespace
{
  std::uint32_t immediateInstruction(
    std::uint8_t opcode,
    std::uint8_t source,
    std::uint8_t target,
    std::uint16_t immediate = 0)
  {
    return
      (static_cast<std::uint32_t>(opcode) << 26) |
      (static_cast<std::uint32_t>(source) << 21) |
      (static_cast<std::uint32_t>(target) << 16) |
      immediate;
  }

  GIFQuadword gifTag()
  {
    const std::uint64_t low =
      UINT64_C(1) |
      (UINT64_C(1) << 15) |
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
}

TEST_CASE("EE Main Memory Map Tests")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();

  bus.write32(0x00000100, UINT32_C(0x12345678));
  REQUIRE(bus.read32(0x20000100) == 0x12345678);
  REQUIRE(bus.read32(0x30000100) == 0x12345678);
  REQUIRE(bus.read32(0x80000100) == 0x12345678);
  REQUIRE(bus.read32(0xa0000100) == 0x12345678);

  std::uint8_t byte = 0;
  REQUIRE(bus.readData8(0x20000101, &byte));
  REQUIRE(byte == 0x56);
  REQUIRE(bus.writeData8(0xa0000102, 0xab));
  REQUIRE(bus.readData8(0x00000102, &byte));
  REQUIRE(byte == 0xab);
  REQUIRE_FALSE(
    bus.readData8(EEMemoryMap::MAIN_MEMORY_SIZE, &byte));
  REQUIRE_FALSE(
    bus.writeData8(EEMemoryMap::MAIN_MEMORY_SIZE, 0));
  std::uint16_t halfword = 0;
  REQUIRE(bus.writeData16(0x30000104, 0x89ab));
  REQUIRE(bus.readData16(0x80000104, &halfword));
  REQUIRE(halfword == 0x89ab);
  REQUIRE_THROWS_WITH(
    bus.readData16(0x105, &halfword),
    "EE halfword load must be naturally aligned.");
  REQUIRE_FALSE(
    bus.readData16(EEMemoryMap::MAIN_MEMORY_SIZE, &halfword));
  std::uint32_t word = 0;
  REQUIRE(bus.writeData32(0x20000108, UINT32_C(0x89abcdef)));
  REQUIRE(bus.readData32(0xa0000108, &word));
  REQUIRE(word == UINT32_C(0x89abcdef));
  REQUIRE_THROWS_WITH(
    bus.writeData32(0x10a, 0),
    "EE word store must be naturally aligned.");
  REQUIRE_FALSE(
    bus.writeData32(EEMemoryMap::MAIN_MEMORY_SIZE, 0));
  std::uint64_t doubleword = 0;
  REQUIRE(
    bus.writeData64(
      0x30000110,
      UINT64_C(0x0123456789abcdef)));
  REQUIRE(bus.readData64(0x80000110, &doubleword));
  REQUIRE(doubleword == UINT64_C(0x0123456789abcdef));
  REQUIRE_THROWS_WITH(
    bus.readData64(0x114, &doubleword),
    "EE doubleword load must be naturally aligned.");
  REQUIRE_FALSE(
    bus.readData64(
      EEMemoryMap::MAIN_MEMORY_SIZE,
      &doubleword));
  const EEQuadword storedQuadword = {
    UINT64_C(0x7766554433221100),
    UINT64_C(0xffeeddccbbaa9988)
  };
  REQUIRE(bus.writeData128(0x20000120, storedQuadword));
  EEQuadword loadedQuadword;
  REQUIRE(bus.readData128(0xa0000120, &loadedQuadword));
  REQUIRE(loadedQuadword.low == storedQuadword.low);
  REQUIRE(loadedQuadword.high == storedQuadword.high);
  REQUIRE_THROWS_WITH(
    bus.writeData128(0x128, storedQuadword),
    "EE quadword store must be naturally aligned.");
  REQUIRE_FALSE(
    bus.readData128(
      EEMemoryMap::MAIN_MEMORY_SIZE,
      &loadedQuadword));

  const GIFQuadword quadword = {{
    UINT32_C(0x01020304),
    UINT32_C(0x11121314),
    UINT32_C(0x21222324),
    UINT32_C(0x31323334)
  }};
  REQUIRE(bus.writeQuadword(0x00000200, quadword));
  REQUIRE(bus.readQuadword(0x20000200) == quadword);

  REQUIRE_THROWS_WITH(
    bus.read32(2),
    "EE bus 32-bit access must be naturally aligned.");
  REQUIRE_THROWS_WITH(
    bus.read32(EEMemoryMap::MAIN_MEMORY_SIZE),
    "EE bus read from an unmapped address.");
}

TEST_CASE("EE direct-mapped kernel segments cover the system map")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();

  bus.write32(
    EEMemoryMap::KSEG0_BASE + 0x100,
    UINT32_C(0x12345678));
  REQUIRE(
    bus.read32(EEMemoryMap::KSEG1_BASE + 0x100) ==
    UINT32_C(0x12345678));

  REQUIRE(bus.writeData32(
    EEMemoryMap::KSEG0_BASE + EEMemoryMap::INTC_MASK,
    EEInterruptSource::mask(EEInterruptSource::VIF0)));
  std::uint32_t interruptMask = 0;
  REQUIRE(bus.readData32(
    EEMemoryMap::KSEG1_BASE + EEMemoryMap::INTC_MASK,
    &interruptMask));
  REQUIRE(
    interruptMask ==
    EEInterruptSource::mask(EEInterruptSource::VIF0));

  REQUIRE(bus.writeData64(
    EEMemoryMap::KSEG1_BASE + EEMemoryMap::GS_BUSDIR,
    1));
  REQUIRE(system.gs().hostInterfaceReversed());
  std::uint64_t busDirection = 0;
  REQUIRE_FALSE(bus.readData64(
    EEMemoryMap::KSEG0_BASE + EEMemoryMap::GS_BUSDIR,
    &busDirection));
  REQUIRE(
    bus.read64(
      EEMemoryMap::KSEG0_BASE + EEMemoryMap::GS_BUSDIR) ==
    1);

  REQUIRE(bus.writeData128(
    EEMemoryMap::KSEG1_BASE + EEMemoryMap::VIF0_FIFO,
    EEQuadword{}));
  REQUIRE(system.vif0().fifoQuadwordCount() == 1);
  REQUIRE(bus.writeQuadword(
    EEMemoryMap::KSEG1_BASE + EEMemoryMap::VIF0_FIFO,
    GIFQuadword{}));

  std::uint8_t byte = 0;
  REQUIRE_FALSE(
    bus.readData8(EEMemoryMap::KSEG2_BASE, &byte));
  REQUIRE_FALSE(
    bus.readData8(UINT32_C(0xe0000000), &byte));
  REQUIRE_THROWS_WITH(
    bus.read32(EEMemoryMap::KSEG2_BASE),
    "EE bus read from an unmapped address.");
}

TEST_CASE("EE guest device accesses enforce register contracts")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();

  std::uint8_t byte = 0;
  std::uint16_t halfword = 0;
  std::uint32_t word = 0;
  std::uint64_t doubleword = 0;
  EEQuadword quadword;

  REQUIRE_FALSE(
    bus.readData8(EEMemoryMap::VIF0_STAT, &byte));
  REQUIRE_FALSE(
    bus.writeData8(EEMemoryMap::VIF0_FBRST, 0));
  REQUIRE_FALSE(
    bus.readData16(EEMemoryMap::INTC_STAT, &halfword));
  REQUIRE_FALSE(
    bus.writeData16(EEMemoryMap::INTC_MASK, 0));

  REQUIRE(bus.readData32(EEMemoryMap::VIF0_STAT, &word));
  REQUIRE_FALSE(
    bus.writeData32(EEMemoryMap::VIF0_STAT, 0));
  REQUIRE_FALSE(
    bus.readData32(EEMemoryMap::VIF0_FBRST, &word));
  REQUIRE(bus.writeData32(EEMemoryMap::VIF0_FBRST, 0));
  REQUIRE_FALSE(
    bus.writeData32(EEMemoryMap::VIF0_FBRST, 1));
  REQUIRE_FALSE(
    bus.writeData32(EEMemoryMap::GS_BUSDIR, 1));

  REQUIRE_FALSE(
    bus.readData64(EEMemoryMap::GS_BUSDIR, &doubleword));
  REQUIRE(bus.writeData64(EEMemoryMap::GS_BUSDIR, 1));
  REQUIRE(system.gs().hostInterfaceReversed());
  REQUIRE_FALSE(
    bus.readData64(EEMemoryMap::GS_PMODE, &doubleword));
  REQUIRE(bus.writeData64(EEMemoryMap::GS_PMODE, 0));
  REQUIRE_FALSE(
    bus.writeData64(
      EEMemoryMap::GS_DISPFB1,
      UINT64_C(1) << 15));
  REQUIRE_FALSE(
    bus.writeData64(EEMemoryMap::INTC_MASK, 0));

  REQUIRE_FALSE(
    bus.readData128(EEMemoryMap::GIF_STAT, &quadword));
  REQUIRE(
    bus.writeData128(EEMemoryMap::GIF_FIFO, quadword));

  REQUIRE_FALSE(
    bus.writeData32(EEMemoryMap::D_CTRL, 2));
  REQUIRE(system.gifDMAC().globalControl() == 0);
  REQUIRE_FALSE(
    bus.writeData32(
      EEMemoryMap::D1_CHCR,
      GIFDMACChannelControl::START));
  REQUIRE(system.vif1DMAC().channelControl() == 0);
  REQUIRE(bus.writeData32(EEMemoryMap::D1_MADR, 0x1234));
  REQUIRE(bus.readData32(EEMemoryMap::D1_MADR, &word));
  REQUIRE(word == 0x1230);
  REQUIRE_THROWS_WITH(
    bus.write32(EEMemoryMap::D_CTRL, 2),
    "Only D_CTRL.DMAE is implemented.");
}

TEST_CASE("EE guest VIF FIFO stores are atomic and backpressured")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  const EEQuadword interruptedNops = {
    UINT64_C(0x0000000080000000),
    0
  };
  const EEQuadword nops = {};

  REQUIRE(
    bus.writeGuestData128(
      EEMemoryMap::KSEG1_BASE + EEMemoryMap::VIF0_FIFO,
      interruptedNops) ==
    EEDataWriteResult::Completed);
  bus.advanceGuestFIFOs();
  REQUIRE(system.vif0().interruptPending());
  REQUIRE(system.vif0().wordsIngested() == 1);
  REQUIRE(system.vif0().fifoQuadwordCount() == 1);

  for (std::size_t index = 0; index < 7; ++index)
  {
    REQUIRE(
      bus.writeGuestData128(EEMemoryMap::VIF0_FIFO, nops) ==
      EEDataWriteResult::Completed);
  }
  REQUIRE(system.vif0().fifoQuadwordCount() == 8);
  REQUIRE(
    bus.writeGuestData128(EEMemoryMap::VIF0_FIFO, nops) ==
    EEDataWriteResult::Stalled);
  REQUIRE(system.vif0().fifoQuadwordCount() == 8);
  REQUIRE(system.vif0().wordsIngested() == 1);

  bus.write32(EEMemoryMap::VIF0_FBRST, 1u << 3);
  bus.advanceGuestFIFOs();
  REQUIRE(system.vif0().fifoQuadwordCount() == 0);
  REQUIRE(system.vif0().wordsIngested() == 32);
}

TEST_CASE("EE SQ retries a full guest FIFO without raising an exception")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  EECore &core = system.eeCore();
  const EEQuadword interruptedNops = {
    UINT64_C(0x0000000080000000),
    0
  };

  REQUIRE(
    bus.writeGuestData128(
      EEMemoryMap::VIF0_FIFO,
      interruptedNops) ==
    EEDataWriteResult::Completed);
  bus.advanceGuestFIFOs();
  for (std::size_t index = 0; index < 7; ++index)
  {
    REQUIRE(
      bus.writeGuestData128(EEMemoryMap::VIF0_FIFO, {}) ==
      EEDataWriteResult::Completed);
  }

  core.setGeneralRegister(1, {EEMemoryMap::VIF0_FIFO, 0});
  core.setGeneralRegister(
    2,
    {
      UINT64_C(0x1111111122222222),
      UINT64_C(0x3333333344444444)
    });
  bus.write32(
    0,
    immediateInstruction(0x1f, 1, 2));
  core.startExecution(0);

  system.clockMasterCycle();
  REQUIRE(core.programCounter() == 0);
  REQUIRE(core.pendingException() == EEException::None);
  REQUIRE(system.vif0().fifoQuadwordCount() == 8);

  bus.write32(EEMemoryMap::VIF0_FBRST, 1u << 3);
  system.clockMasterCycle();
  REQUIRE(core.programCounter() == 4);
  REQUIRE(core.pendingException() == EEException::None);
  REQUIRE(system.vif0().fifoQuadwordCount() == 1);
}

TEST_CASE("EE guest GIF FIFO retains refused PATH3 transfers")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  const GIFQuadword blockingTag = gifTag();
  const EEQuadword guestTag = {
    UINT64_C(1) << 15,
    0
  };

  REQUIRE(system.gifPathArbiter().transferQuadword(
    GIFPath::Path2,
    blockingTag).accepted);
  for (std::size_t index = 0; index < 16; ++index)
  {
    REQUIRE(
      bus.writeGuestData128(
        EEMemoryMap::GIF_FIFO,
        guestTag) ==
      EEDataWriteResult::Completed);
  }
  REQUIRE(
    bus.writeGuestData128(
      EEMemoryMap::GIF_FIFO,
      guestTag) ==
    EEDataWriteResult::Stalled);

  bus.advanceGuestFIFOs();
  REQUIRE(system.gifPath3().guestFIFOQuadwordCount() == 16);
  REQUIRE(system.gifPath3().transferredQuadwordCount() == 0);
  REQUIRE(
    ((bus.read32(EEMemoryMap::GIF_STAT) &
      GIFStatus::FQC_MASK) >>
      GIFStatus::FQC_SHIFT) == 16);

  REQUIRE(system.gifPathArbiter().transferQuadword(
    GIFPath::Path2,
    adWrite(GSRegisterAddress::PRIM, 0)).accepted);
  bus.advanceGuestFIFOs();
  REQUIRE(system.gifPath3().guestFIFOQuadwordCount() == 0);
  REQUIRE(system.gifPath3().transferredQuadwordCount() == 16);
}

TEST_CASE("EE guest instructions reject invalid device access directions")
{
  SECTION("Write-only GS register cannot be loaded")
  {
    NekoSystem system;
    system.eeCore().setGeneralRegister(
      1,
      {EEMemoryMap::GS_BUSDIR, 0});
    system.eeBus().write32(
      0,
      immediateInstruction(0x37, 1, 2));
    system.eeCore().startExecution(0);

    const EEExecutionResult result = system.runEE(1);

    REQUIRE(
      result.pendingException ==
      EEException::DataBusErrorLoad);
    REQUIRE(
      result.exceptionAddress ==
      EEMemoryMap::GS_BUSDIR);
  }

  SECTION("64-bit GS register rejects word stores")
  {
    NekoSystem system;
    system.eeCore().setGeneralRegister(
      1,
      {EEMemoryMap::GS_BUSDIR, 0});
    system.eeCore().setGeneralRegister(2, {1, 0});
    system.eeBus().write32(
      0,
      immediateInstruction(0x2b, 1, 2));
    system.eeCore().startExecution(0);

    const EEExecutionResult result = system.runEE(1);

    REQUIRE(
      result.pendingException ==
      EEException::DataBusErrorStore);
    REQUIRE_FALSE(system.gs().hostInterfaceReversed());
  }

  SECTION("32-bit EE register rejects doubleword stores")
  {
    NekoSystem system;
    system.eeCore().setGeneralRegister(
      1,
      {EEMemoryMap::INTC_MASK, 0});
    system.eeCore().setGeneralRegister(
      2,
      {EEInterruptSource::mask(EEInterruptSource::VIF0), 0});
    system.eeBus().write32(
      0,
      immediateInstruction(0x3f, 1, 2));
    system.eeCore().startExecution(0);

    const EEExecutionResult result = system.runEE(1);

    REQUIRE(
      result.pendingException ==
      EEException::DataBusErrorStore);
    REQUIRE(system.interruptController().mask() == 0);
  }

  SECTION("Invalid register values become store bus errors")
  {
    NekoSystem system;
    system.eeCore().setGeneralRegister(
      1,
      {EEMemoryMap::D_CTRL, 0});
    system.eeCore().setGeneralRegister(2, {2, 0});
    system.eeBus().write32(
      0,
      immediateInstruction(0x2b, 1, 2));
    system.eeCore().startExecution(0);

    const EEExecutionResult result = system.runEE(1);

    REQUIRE(
      result.pendingException ==
      EEException::DataBusErrorStore);
    REQUIRE(system.gifDMAC().globalControl() == 0);
  }
}

TEST_CASE("EE GIF Memory Map Tests")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();

  bus.write32(EEMemoryMap::GIF_MODE, GIFMode::IMT);
  REQUIRE(
    (bus.read32(EEMemoryMap::GIF_STAT) & GIFStatus::IMT) != 0);

  REQUIRE(bus.writeQuadword(EEMemoryMap::GIF_FIFO, gifTag()));
  REQUIRE(bus.writeQuadword(
    EEMemoryMap::GIF_FIFO,
    adWrite(GSRegisterAddress::PRIM, 6)));
  REQUIRE(
    system.gs().primitive().type ==
    GSPrimitiveType::Sprite);
}

TEST_CASE("EE VIF and Interrupt Coordination Tests")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  const GIFQuadword interruptedNops = {{
    0,
    0,
    0,
    UINT32_C(0x80000000)
  }};

  REQUIRE(bus.writeQuadword(
    EEMemoryMap::VIF1_FIFO,
    interruptedNops));
  REQUIRE(
    (bus.read32(EEMemoryMap::VIF1_STAT) &
     EEVIFStatus::INTERRUPT) != 0);
  REQUIRE(bus.read32(EEMemoryMap::VIF1_CODE) == 0x80000000);

  system.clockMasterCycle();
  REQUIRE(
    (bus.read32(EEMemoryMap::INTC_STAT) &
     EEInterruptSource::mask(EEInterruptSource::VIF1)) != 0);
  REQUIRE_FALSE(system.interruptPending());

  bus.write32(
    EEMemoryMap::INTC_MASK,
    EEInterruptSource::mask(EEInterruptSource::VIF1));
  REQUIRE(system.interruptPending());

  bus.write32(EEMemoryMap::VIF1_FBRST, 1u << 3);
  system.clockMasterCycle();
  REQUIRE(
    (bus.read32(EEMemoryMap::VIF1_STAT) &
     EEVIFStatus::INTERRUPT) == 0);
  REQUIRE(system.interruptPending());

  bus.write32(
    EEMemoryMap::INTC_STAT,
    EEInterruptSource::mask(EEInterruptSource::VIF1));
  REQUIRE_FALSE(system.interruptPending());
}

TEST_CASE("EE GS Privileged Memory Map Tests")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();

  bus.write64(EEMemoryMap::GS_BUSDIR, 1);

  REQUIRE(system.gs().hostInterfaceReversed());
  REQUIRE(bus.read64(EEMemoryMap::GS_BUSDIR) == 1);
}
