#include <cstdint>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "gif_registers.hpp"
#include "neko_system.hpp"

namespace
{
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
