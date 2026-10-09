#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#include "catch.hpp"
#include "iop_bus.hpp"

static_assert(
  std::is_final<IOPBus>::value,
  "IOP physical decode must remain concrete and non-overridable.");

namespace
{
  std::shared_ptr<const BootROMImage> makeRom()
  {
    std::vector<std::uint8_t> bytes(
      IOPBus::ROM_SIZE,
      static_cast<std::uint8_t>(0));
    bytes[0] = 0x78;
    bytes[1] = 0x56;
    bytes[2] = 0x34;
    bytes[3] = 0x12;
    bytes.back() = 0xa5;
    return BootROMImage::create(bytes);
  }
}

TEST_CASE("IOP bus RAM is little-endian and mirrored four times", "[iop][bus]")
{
  IOPBus bus;

  REQUIRE(bus.write32(0x100, UINT32_C(0x11223344)).status ==
    IOPBusStatus::Completed);
  REQUIRE(bus.read8(0x100).value == 0x44);
  REQUIRE(bus.read8(0x103).value == 0x11);
  REQUIRE(bus.read16(0x102).value == 0x1122);

  for (IOPAddress mirror = 0; mirror < 4; ++mirror)
  {
    const IOPAddress address = mirror * IOPBus::RAM_SIZE + 0x100;
    const auto result = bus.read32(address);
    REQUIRE(result.status == IOPBusStatus::Completed);
    REQUIRE(result.value == UINT32_C(0x11223344));
  }

  bus.write8(0x7fffff, 0xab);
  REQUIRE(bus.read8(0x1fffff).value == 0xab);
  REQUIRE(bus.read8(0x7fffff).value == 0xab);
}

TEST_CASE("IOP bus does not wrap addresses beyond the RAM mirror", "[iop][bus]")
{
  IOPBus bus;

  REQUIRE(bus.read8(0x007fffff).status == IOPBusStatus::Completed);
  REQUIRE(bus.read8(0x00800000).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.write8(0x00800000, 1).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.read8(0x1f7fffff).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.read8(0x80000000).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.read8(0xffffffff).status == IOPBusStatus::Unmapped);
}

TEST_CASE("IOP bus enforces natural alignment", "[iop][bus]")
{
  IOPBus bus;
  bus.write32(0, UINT32_C(0xffffffff));

  REQUIRE(bus.read16(1).status == IOPBusStatus::Misaligned);
  REQUIRE(bus.read32(2).status == IOPBusStatus::Misaligned);
  REQUIRE(bus.write16(3, 0).status == IOPBusStatus::Misaligned);
  REQUIRE(bus.write32(1, 0).status == IOPBusStatus::Misaligned);
  REQUIRE(bus.read32(0).value == UINT32_C(0xffffffff));
  REQUIRE(bus.read32(0x00800002).status == IOPBusStatus::Misaligned);
}

TEST_CASE("IOP bus scratchpad is distinct from RAM", "[iop][bus]")
{
  IOPBus bus;

  REQUIRE(bus.write32(0x1f800000, UINT32_C(0xcafef00d)).status ==
    IOPBusStatus::Completed);
  REQUIRE(bus.read32(0x1f800000).value == UINT32_C(0xcafef00d));
  REQUIRE(bus.read32(0).value == 0);
  REQUIRE(bus.read8(0x1f8003ff).status == IOPBusStatus::Completed);
  REQUIRE(bus.read8(0x1f800400).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.read8(0x1f7fffff).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.read32(0x1f8003fc).status == IOPBusStatus::Completed);
}

TEST_CASE("IOP bus ROM is unmapped until installed then read-only",
  "[iop][bus]")
{
  IOPBus bus;

  REQUIRE(bus.read32(0x1fc00000).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.write32(0x1fc00000, 1).status == IOPBusStatus::Unmapped);

  const auto rom = makeRom();
  REQUIRE(bus.installRom(rom));
  REQUIRE(bus.read32(0x1fc00000).value == UINT32_C(0x12345678));
  REQUIRE(bus.read8(0x1fffffff).value == 0xa5);
  REQUIRE(bus.read8(0x20000000).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.write32(0x1fc00000, 0).status == IOPBusStatus::ReadOnly);
  REQUIRE(bus.write8(0x1fc00000, 0).status == IOPBusStatus::ReadOnly);
  REQUIRE((*rom)[0] == 0x78);
  REQUIRE(bus.write16(0x1fc00001, 0).status == IOPBusStatus::Misaligned);

  const std::uint8_t byte = 0;
  REQUIRE(bus.loadPhysical(0x1fc00000, &byte, 1) == IOPBusStatus::ReadOnly);
  REQUIRE((*rom)[0] == 0x78);
}

TEST_CASE("IOP bus ROM cannot change through a surviving mutable alias",
  "[iop][bus]")
{
  IOPBus bus;
  std::vector<std::uint8_t> mutableSource(
    IOPBus::ROM_SIZE,
    static_cast<std::uint8_t>(0));
  mutableSource[0] = 0x12;
  const auto installedRom = BootROMImage::create(mutableSource);

  REQUIRE(bus.installRom(installedRom));
  REQUIRE(bus.read8(IOPBus::ROM_BASE).value == 0x12);

  mutableSource[0] = 0x34;

  REQUIRE(bus.read8(IOPBus::ROM_BASE).value == 0x12);
}

TEST_CASE("IOP bus rejects malformed ROM images", "[iop][bus]")
{
  IOPBus bus;

  REQUIRE_FALSE(bus.installRom(nullptr));
  REQUIRE_FALSE(BootROMImage::create(
    std::vector<std::uint8_t>(16)));
  REQUIRE(bus.read8(0x1fc00000).status == IOPBusStatus::Unmapped);
}

TEST_CASE("IOP bus host loading is atomic and physical only", "[iop][bus]")
{
  IOPBus bus;
  const std::uint8_t bytes[4] = {1, 2, 3, 4};

  REQUIRE(bus.loadPhysical(0x200, bytes, 4) == IOPBusStatus::Completed);
  REQUIRE(bus.read32(0x200).value == UINT32_C(0x04030201));

  std::uint8_t out[4] = {};
  REQUIRE(bus.inspectPhysical(0x200 + IOPBus::RAM_SIZE, out, 4) ==
    IOPBusStatus::Completed);
  REQUIRE(out[3] == 4);

  REQUIRE(bus.loadPhysical(0x7ffffe, bytes, 4) == IOPBusStatus::Unmapped);
  REQUIRE(bus.read8(0x7ffffe).value == 0);
  REQUIRE(bus.read8(0x1ffffe).value == 0);

  REQUIRE(bus.loadPhysical(0x1f8003fe, bytes, 4) == IOPBusStatus::Unmapped);
  REQUIRE(bus.read8(0x1f8003fe).value == 0);
  REQUIRE(bus.loadPhysical(0x1f800000, bytes, 4) == IOPBusStatus::Completed);
  REQUIRE(bus.read32(0x1f800000).value == UINT32_C(0x04030201));
  REQUIRE(bus.loadPhysical(0x100, bytes, 0) == IOPBusStatus::Completed);
  REQUIRE(bus.loadPhysical(0xffffffff, bytes, 2) == IOPBusStatus::Unmapped);
}

TEST_CASE("IOP bus host inspection reads ROM", "[iop][bus]")
{
  IOPBus bus;
  std::uint8_t out[4] = {};
  REQUIRE(bus.inspectPhysical(0x1fc00000, out, 4) == IOPBusStatus::Unmapped);

  REQUIRE(bus.installRom(makeRom()));
  REQUIRE(bus.inspectPhysical(0x1fc00000, out, 4) ==
    IOPBusStatus::Completed);
  REQUIRE(out[0] == 0x78);
  REQUIRE(out[3] == 0x12);
  REQUIRE(bus.inspectPhysical(0x1ffffffe, out, 4) == IOPBusStatus::Unmapped);
}

TEST_CASE("IOP bus reset clears storage and preserves ROM", "[iop][bus]")
{
  IOPBus bus;
  REQUIRE(bus.installRom(makeRom()));
  bus.write32(0x40, UINT32_C(0xffffffff));
  bus.write32(0x1f800010, UINT32_C(0xffffffff));

  bus.reset();

  REQUIRE(bus.read32(0x40).value == 0);
  REQUIRE(bus.read32(0x1f800010).value == 0);
  REQUIRE(bus.read32(0x1fc00000).value == UINT32_C(0x12345678));
}
