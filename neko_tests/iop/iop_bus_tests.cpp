#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "catch.hpp"
#include "iop_bus.hpp"
#include "iop_interrupt_controller.hpp"

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

TEST_CASE("IOP bus masked word writes touch only enabled byte lanes",
  "[iop][bus][merge]")
{
  IOPBus bus;
  const IOPAddress addresses[] = {0x100, 0x200100, 0x7ffffc, 0x1f8003fc};
  const std::uint8_t before[] = {0x11, 0x22, 0x33, 0x44};
  const std::uint8_t after[] = {0xdd, 0xcc, 0xbb, 0xaa};
  for (const auto address : addresses)
  {
    for (std::uint8_t mask = 1; mask < 16; ++mask)
    {
      REQUIRE(bus.write32(address, 0x44332211).status == IOPBusStatus::Completed);
      REQUIRE(bus.writeMasked32(address, 0xaabbccdd, mask).status ==
        IOPBusStatus::Completed);
      for (std::uint8_t lane = 0; lane < 4; ++lane)
      {
        REQUIRE(bus.read8(address + lane).value ==
          ((mask & (1 << lane)) != 0 ? after[lane] : before[lane]));
      }
    }
  }
}

TEST_CASE("IOP bus masked writes validate before mutating storage",
  "[iop][bus][merge]")
{
  IOPBus bus;
  REQUIRE(bus.write32(0x100, 0x44332211).status == IOPBusStatus::Completed);
  for (const std::uint8_t mask : {std::uint8_t(0), std::uint8_t(0x10), std::uint8_t(0xff)})
  {
    REQUIRE_THROWS_AS(bus.writeMasked32(0x100, 0, mask), std::invalid_argument);
  }
  REQUIRE(bus.writeMasked32(0x101, 0, 1).status == IOPBusStatus::Misaligned);
  REQUIRE(bus.writeMasked32(0x800000, 0, 0xf).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.writeMasked32(0x1f800400, 0, 1).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.writeMasked32(0xfffffffc, 0, 0xf).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.writeMasked32(IOPBus::ROM_BASE, 0, 1).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.installRom(makeRom()));
  REQUIRE(bus.writeMasked32(IOPBus::ROM_BASE, 0, 0xf).status == IOPBusStatus::ReadOnly);
  REQUIRE(bus.read32(IOPBus::ROM_BASE).value == 0x12345678);
  REQUIRE(bus.read32(0x100).value == 0x44332211);
}

TEST_CASE("IOP bus INTC word routes use the attached controller",
  "[iop][bus][intc]")
{
  REQUIRE(IOPBus::INTC_STAT == 0x1f801070);
  REQUIRE(IOPBus::INTC_MASK == 0x1f801074);
  REQUIRE(IOPBus::INTC_CTRL == 0x1f801078);
  IOPBus bus;
  for (IOPAddress address : {0x1f801070u, 0x1f801074u, 0x1f801078u})
  {
    REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(address, 1).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeMasked32(address, 1, 0xf).status == IOPBusStatus::Unmapped);
  }

  IOPInterruptController interrupts;
  bus.attachInterruptController(&interrupts);
  interrupts.assertSource(IOPInterruptSource::Timer0);
  interrupts.assertSource(IOPInterruptSource::FireWireDMA);
  const auto status = bus.read32(0x1f801070);
  REQUIRE(status.status == IOPBusStatus::Completed);
  REQUIRE(status.value == 0x02000010);
  REQUIRE(bus.write32(0x1f801074, 0xffffffff).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(0x1f801074).value == 0x03ffffff);
  REQUIRE_FALSE(interrupts.interruptPending());
  REQUIRE(bus.write32(0x1f801078, 0xffffffff).status == IOPBusStatus::Completed);
  REQUIRE(interrupts.control() == 1);
  REQUIRE(interrupts.interruptPending());
  REQUIRE(bus.write32(0x1f801070, ~IOPWord(0x10)).status == IOPBusStatus::Completed);
  REQUIRE(bus.read32(0x1f801070).value == 0x02000000);
  REQUIRE(interrupts.interruptPending());

  const auto control = bus.read32(0x1f801078);
  REQUIRE(control.status == IOPBusStatus::Completed);
  REQUIRE(control.value == 1);
  REQUIRE_FALSE(interrupts.interruptPending());
  REQUIRE(interrupts.status() == 0x02000000);
  REQUIRE(interrupts.mask() == 0x03ffffff);
  REQUIRE(bus.read32(0x1f801078).value == 0);
  REQUIRE(bus.write32(0x1f801078, 0xfffffffe).status == IOPBusStatus::Completed);
  REQUIRE(interrupts.control() == 0);
}

TEST_CASE("IOP bus INTC rejects narrow and misaligned accesses without side effects",
  "[iop][bus][intc]")
{
  IOPBus bus;
  IOPInterruptController interrupts;
  bus.attachInterruptController(&interrupts);
  interrupts.assertSource(IOPInterruptSource::Timer0);
  interrupts.writeMask(0x10);
  interrupts.writeControl(1);

  for (IOPAddress base : {0x1f801070u, 0x1f801074u, 0x1f801078u})
  {
    for (IOPAddress offset = 0; offset < 4; ++offset)
    {
      const IOPAddress address = base + offset;
      CAPTURE(address);
      const auto byte = bus.read8(address);
      REQUIRE(byte.status == IOPBusStatus::Unmapped);
      REQUIRE(byte.value == 0);
      REQUIRE(bus.write8(address, 0).status == IOPBusStatus::Unmapped);
      const auto halfword = bus.read16(address);
      REQUIRE(halfword.status == ((offset & 1) == 0
        ? IOPBusStatus::Unmapped : IOPBusStatus::Misaligned));
      REQUIRE(halfword.value == 0);
      REQUIRE(bus.write16(address, 0).status == ((offset & 1) == 0
        ? IOPBusStatus::Unmapped : IOPBusStatus::Misaligned));
      if (offset != 0)
      {
        REQUIRE(bus.read32(address).status == IOPBusStatus::Misaligned);
        REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Misaligned);
        REQUIRE(bus.writeMasked32(address, 0, 0xf).status ==
          IOPBusStatus::Misaligned);
      }
      REQUIRE(interrupts.status() == 0x10);
      REQUIRE(interrupts.mask() == 0x10);
      REQUIRE(interrupts.control() == 1);
      REQUIRE(interrupts.interruptPending());
    }
  }
}

TEST_CASE("IOP bus INTC accepts only full-byte-mask stores",
  "[iop][bus][intc]")
{
  IOPBus bus;
  IOPInterruptController interrupts;
  bus.attachInterruptController(&interrupts);
  interrupts.assertSource(IOPInterruptSource::Timer0);
  interrupts.writeMask(0x10);
  interrupts.writeControl(1);

  for (IOPAddress address : {0x1f801070u, 0x1f801074u, 0x1f801078u})
  {
    for (std::uint8_t mask = 1; mask < 0xf; ++mask)
    {
      CAPTURE(address);
      CAPTURE(mask);
      REQUIRE(bus.writeMasked32(address, 0, mask).status == IOPBusStatus::Unmapped);
      REQUIRE(interrupts.status() == 0x10);
      REQUIRE(interrupts.mask() == 0x10);
      REQUIRE(interrupts.control() == 1);
    }
    for (std::uint8_t mask : {std::uint8_t(0), std::uint8_t(0x10),
         std::uint8_t(0xff)})
    {
      REQUIRE_THROWS_AS(bus.writeMasked32(address, 0, mask), std::invalid_argument);
      REQUIRE(interrupts.status() == 0x10);
      REQUIRE(interrupts.mask() == 0x10);
      REQUIRE(interrupts.control() == 1);
    }
  }

  REQUIRE(bus.writeMasked32(0x1f801074, 0xffffffff, 0xf).status ==
    IOPBusStatus::Completed);
  REQUIRE(interrupts.mask() == 0x03ffffff);
  REQUIRE(bus.writeMasked32(0x1f801078, 0, 0xf).status == IOPBusStatus::Completed);
  REQUIRE(interrupts.control() == 0);
  REQUIRE(interrupts.status() == 0x10);
  REQUIRE(bus.writeMasked32(0x1f801070, 0, 0xf).status == IOPBusStatus::Completed);
  REQUIRE(interrupts.status() == 0);
}

TEST_CASE("IOP bus INTC decode stays physical and host helpers stay storage-only",
  "[iop][bus][intc]")
{
  IOPBus bus;
  IOPInterruptController interrupts;
  bus.attachInterruptController(&interrupts);
  interrupts.assertSource(IOPInterruptSource::Timer0);
  interrupts.writeMask(0x10);
  interrupts.writeControl(1);
  std::array<std::uint8_t, 16> bytes;
  bytes.fill(0xa5);
  const auto before = bytes;
  const IOPBus &inspection = bus;

  for (IOPAddress address = 0x1f801070; address < 0x1f80107c; ++address)
  {
    REQUIRE(inspection.regionForRange(address, 0) == IOPBusRegion::Mmio);
    REQUIRE(inspection.regionForRange(address, 1) == IOPBusRegion::Mmio);
    for (std::size_t count : {0u, 1u, 4u, 12u, 16u})
    {
      REQUIRE(bus.loadPhysical(address, bytes.data(), count) == IOPBusStatus::Unmapped);
      REQUIRE(inspection.inspectPhysical(address, bytes.data(), count) ==
        IOPBusStatus::Unmapped);
      REQUIRE(bytes == before);
      REQUIRE(interrupts.status() == 0x10);
      REQUIRE(interrupts.mask() == 0x10);
      REQUIRE(interrupts.control() == 1);
    }
  }
  REQUIRE(inspection.regionForRange(0x1f801070, 12) == IOPBusRegion::Mmio);
  REQUIRE(inspection.regionForRange(0x1f801070, 13) == IOPBusRegion::Unmapped);
  REQUIRE(inspection.regionForRange(0x1f801078, 4) == IOPBusRegion::Mmio);
  REQUIRE(inspection.regionForRange(0x1f801078, 5) == IOPBusRegion::Unmapped);

  for (IOPAddress address : {0x1f80106cu, 0x1f80107cu, 0x1f801080u,
       0x9f801070u, 0xbf801070u})
  {
    REQUIRE(inspection.regionForRange(address, 4) == IOPBusRegion::Unmapped);
    REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeMasked32(address, 0, 0xf).status == IOPBusStatus::Unmapped);
  }
  REQUIRE(interrupts.status() == 0x10);
  REQUIRE(interrupts.mask() == 0x10);
  REQUIRE(interrupts.control() == 1);
}

TEST_CASE("IOP bus INTC attachment and bus reset preserve external device state",
  "[iop][bus][intc]")
{
  IOPBus bus;
  IOPInterruptController first;
  IOPInterruptController second;
  first.assertSource(IOPInterruptSource::Timer0);
  first.writeMask(0x10);
  first.writeControl(1);
  second.assertSource(IOPInterruptSource::FireWireDMA);
  bus.attachInterruptController(&first);
  bus.reset();
  REQUIRE(first.status() == 0x10);
  REQUIRE(first.mask() == 0x10);
  REQUIRE(first.control() == 1);
  REQUIRE(bus.read32(0x1f801070).value == 0x10);

  bus.attachInterruptController(nullptr);
  REQUIRE(bus.regionForRange(0x1f801078, 4) == IOPBusRegion::Mmio);
  REQUIRE(bus.read32(0x1f801078).status == IOPBusStatus::Unmapped);
  REQUIRE(bus.write32(0x1f801070, 0).status == IOPBusStatus::Unmapped);
  REQUIRE(first.status() == 0x10);
  REQUIRE(first.control() == 1);
  bus.attachInterruptController(&second);
  REQUIRE(bus.read32(0x1f801070).value == 0x02000000);
  REQUIRE(first.status() == 0x10);
  REQUIRE(first.mask() == 0x10);
  REQUIRE(first.control() == 1);
  REQUIRE(bus.write32(0x1f801070, 0).status == IOPBusStatus::Completed);
  REQUIRE(second.status() == 0);

  bus.attachInterruptController(&first);
  first.reset();
  REQUIRE(bus.read32(0x1f801070).value == 0);
  REQUIRE(bus.read32(0x1f801074).value == 0);
  REQUIRE(bus.read32(0x1f801078).value == 0);
}
