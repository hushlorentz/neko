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
#include "iop_timers.hpp"
#include "ssbus_test_utils.hpp"

static_assert(
  std::is_final<IOPBus>::value,
  "IOP physical decode must remain concrete and non-overridable.");

namespace
{
  constexpr auto ssbusAddresses = SSBUSTest::addresses;

  const std::array<IOPAddress, 6> timerBases = {{
    0x1f801100, 0x1f801110, 0x1f801120,
    0x1f801480, 0x1f801490, 0x1f8014a0
  }};

  void requireTimersEqual(const IOPTimers &actual, const IOPTimers &expected)
  {
    REQUIRE(actual.gateLevels().horizontalBlank ==
      expected.gateLevels().horizontalBlank);
    REQUIRE(actual.gateLevels().verticalBlank ==
      expected.gateLevels().verticalBlank);
    for (std::size_t index = 0; index < 6; ++index)
    {
      const auto &state = actual.counter(index);
      const auto &before = expected.counter(index);
      REQUIRE(state.count == before.count);
      REQUIRE(state.mode == before.mode);
      REQUIRE(state.target == before.target);
      REQUIRE(state.prescalerRemainder == before.prescalerRemainder);
      REQUIRE(state.gateWaiting == before.gateWaiting);
      REQUIRE(state.oneShotConsumed == before.oneShotConsumed);
      REQUIRE(state.pulseReleasePending == before.pulseReleasePending);
    }
  }

  IOPTimers activeTimers()
  {
    IOPTimers timers;
    for (std::size_t index = 0; index < 6; ++index)
    {
      timers.writeMode(index, 0x30);
      timers.writeCount(index, 0xffffffff);
    }
    REQUIRE(timers.advance({true, false, {true, true}}).count == 6);
    for (std::size_t index = 0; index < 6; ++index)
    {
      timers.writeCount(index, static_cast<IOPWord>(0x12345678 + index));
      timers.writeTarget(index, static_cast<IOPWord>(0x89abcdef + index));
    }
    return timers;
  }

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

TEST_CASE("IOP SSBUS configuration retains eighteen independent full words",
  "[iop][bus][ssbus]")
{
  for (IOPAddress address : ssbusAddresses)
  {
    CAPTURE(address);
    IOPBus bus;
    IOPBus independent;
    for (IOPAddress peer : ssbusAddresses)
    {
      if (peer != address)
      {
        REQUIRE(bus.write32(peer, peer).status == IOPBusStatus::Completed);
      }
    }
    REQUIRE(bus.read32(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(address).value == 0);
    for (IOPWord value :
         {0xffffffffu, 0x12345678u, 0x1f802000u, 0x80000000u, 1u, 0u})
    {
      CAPTURE(value);
      REQUIRE(bus.write32(address, value).status == IOPBusStatus::Completed);
      REQUIRE(bus.read32(address).value == value);
      REQUIRE(independent.read32(address).value == 0);
      for (IOPAddress peer : ssbusAddresses)
      {
        if (peer != address)
        {
          REQUIRE(bus.read32(peer).value == peer);
        }
      }
    }
    REQUIRE(bus.writeMasked32(address, 0xfedcba98, 0xf).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.read32(address).value == 0xfedcba98);
    bus.reset();
    REQUIRE(bus.read32(address).status == IOPBusStatus::Completed);
    REQUIRE(bus.read32(address).value == 0);
    for (IOPAddress word : ssbusAddresses)
    {
      REQUIRE(bus.read32(word).value == 0);
    }
    REQUIRE(bus.write32(address, 0x1225).status == IOPBusStatus::Completed);
    REQUIRE(bus.installRom(makeRom()));
    REQUIRE(bus.read32(address).value == 0x1225);
    bus.reset();
    REQUIRE(bus.romIdentity() != nullptr);
    for (IOPAddress word : ssbusAddresses)
    {
      REQUIRE(bus.read32(word).value == 0);
    }
  }
}

TEST_CASE("IOP SSBUS grouped words admit exact reads and writes",
  "[iop][bus][ssbus]")
{
  IOPBus bus;
  for (IOPAddress address : ssbusAddresses)
  {
    CAPTURE(address);
    CHECK(bus.read32(address).status == IOPBusStatus::Completed);
    CHECK(bus.write32(address, address).status == IOPBusStatus::Completed);
    CHECK(bus.read32(address).value == address);
  }
}

TEST_CASE("IOP SSBUS base retention does not change physical decode",
  "[iop][bus][ssbus]")
{
  IOPBus bus;
  REQUIRE(bus.installRom(makeRom()));
  REQUIRE(bus.write32(0x100, 0x89abcdef).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(IOPBus::SCRATCHPAD_BASE, 0x12345678).status ==
    IOPBusStatus::Completed);
  for (IOPAddress base :
       {0x1f801000u, 0x1f801004u, 0x1f801400u, 0x1f801404u,
        0x1f801408u, 0x1f80140cu, 0x1f801410u})
  {
    for (IOPWord value :
         {0u, 0x1f802000u, 0x100u, 0x1fc00000u, 0x1f800000u, 0xffffffffu})
    {
      CAPTURE(base);
      CAPTURE(value);
      REQUIRE(bus.write32(base, value).status == IOPBusStatus::Completed);
      REQUIRE(bus.read32(base).value == value);
      for (IOPAddress mirror : {0x100u, 0x200100u, 0x400100u, 0x600100u})
      {
        REQUIRE(bus.read32(mirror).value == 0x89abcdef);
        REQUIRE(bus.readDMAC32(mirror).value == 0x89abcdef);
      }
      REQUIRE(bus.read32(IOPBus::SCRATCHPAD_BASE).value == 0x12345678);
      REQUIRE(bus.read32(IOPBus::ROM_BASE).value == 0x12345678);
      REQUIRE(bus.write32(IOPBus::ROM_BASE, 0).status == IOPBusStatus::ReadOnly);
      for (IOPAddress address :
           {0x800000u, 0x1f000000u, 0x1f802000u, 0x1f900000u, 0x1f801424u})
      {
        REQUIRE(bus.regionForRange(address, 4) == IOPBusRegion::Unmapped);
        REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
        REQUIRE(bus.write32(address, 1).status == IOPBusStatus::Unmapped);
        REQUIRE(bus.dmaWriteStatus(address) == IOPBusStatus::Unmapped);
      }
    }
  }
}

TEST_CASE("IOP SSBUS configuration rejects unsupported accesses atomically",
  "[iop][bus][ssbus]")
{
  for (IOPAddress base : ssbusAddresses)
  {
    CAPTURE(base);
    IOPBus bus;
    constexpr IOPWord retained = 0x89abcdef;
    REQUIRE(bus.write32(base, retained).status == IOPBusStatus::Completed);
    for (IOPAddress peer : ssbusAddresses)
    {
      if (peer != base)
      {
        REQUIRE(bus.write32(peer, peer).status == IOPBusStatus::Completed);
      }
    }
    REQUIRE(bus.regionForRange(base, 4) == IOPBusRegion::Mmio);
    REQUIRE(bus.regionForRange(base, 0) == IOPBusRegion::Mmio);
    REQUIRE(bus.regionForRange(base + 3, 1) == IOPBusRegion::Mmio);
    REQUIRE(bus.regionForRange(base - 1, 2) == IOPBusRegion::Unmapped);
    REQUIRE(bus.regionForRange(base, 5) == IOPBusRegion::Unmapped);
    for (IOPAddress offset = 0; offset < 4; ++offset)
    {
      CAPTURE(offset);
      const auto address = base + offset;
      REQUIRE(bus.read8(address).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.read8(address).value == 0);
      REQUIRE(bus.write8(address, 0).status == IOPBusStatus::Unmapped);
      const auto halfwordStatus = (offset & 1) == 0
        ? IOPBusStatus::Unmapped : IOPBusStatus::Misaligned;
      REQUIRE(bus.read16(address).status == halfwordStatus);
      REQUIRE(bus.write16(address, 0).status == halfwordStatus);
      if (offset != 0)
      {
        REQUIRE(bus.read32(address).status == IOPBusStatus::Misaligned);
        REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Misaligned);
        REQUIRE(bus.writeMasked32(address, 0, 0xf).status ==
          IOPBusStatus::Misaligned);
      }
    }
    for (std::uint8_t mask = 1; mask < 15; ++mask)
    {
      CAPTURE(mask);
      REQUIRE(bus.writeMasked32(base, 0, mask).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.read32(base).value == retained);
    }
    for (std::uint8_t mask : {std::uint8_t(0), std::uint8_t(0x10), std::uint8_t(0xff)})
    {
      REQUIRE_THROWS_AS(bus.writeMasked32(base, 0, mask), std::invalid_argument);
      REQUIRE_THROWS_AS(bus.writeMasked32(base + 1, 0, mask),
        std::invalid_argument);
    }
    REQUIRE(bus.writeMasked32(base + 1, 0, 1).status == IOPBusStatus::Misaligned);
    for (IOPAddress address :
         {0x1f800ffcu, 0x1f801024u, 0x1f801060u, 0x1f8013fcu,
          0x1f801424u, 0x1f802000u, 0xfffe0130u, 0xfffe0140u, 0xfffe0144u})
    {
      CAPTURE(address);
      REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write32(address, 0xffffffff).status == IOPBusStatus::Unmapped);
    }
    REQUIRE(bus.regionForRange(0x1f801010, 0x14) == IOPBusRegion::Unmapped);
    REQUIRE(bus.regionForRange(0x1f80101c, 8) == IOPBusRegion::Unmapped);
    REQUIRE(bus.regionForRange(0x1f80101f, 2) == IOPBusRegion::Unmapped);
    REQUIRE(bus.regionForRange(0x1f801424, 0) == IOPBusRegion::Unmapped);
    REQUIRE(bus.read32(base).value == retained);
    for (IOPAddress peer : ssbusAddresses)
    {
      if (peer != base)
      {
        REQUIRE(bus.read32(peer).value == peer);
      }
    }
  }
}

TEST_CASE("IOP SSBUS configuration is excluded from DMA and host byte access",
  "[iop][bus][ssbus]")
{
  for (IOPAddress address : ssbusAddresses)
  {
    CAPTURE(address);
    IOPBus bus;
    REQUIRE(bus.write32(address, 0x76543210).status == IOPBusStatus::Completed);
    REQUIRE(bus.readDMAC32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.dmaWriteStatus(address) == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeDMAC32(address, 0).status == IOPBusStatus::Unmapped);
    const std::uint8_t bytes[] = {1, 2, 3, 4};
    std::array<std::uint8_t, 4> output = {{0xaa, 0xaa, 0xaa, 0xaa}};
    const auto before = output;
    REQUIRE(bus.loadPhysical(address, bytes, 4) == IOPBusStatus::Unmapped);
    REQUIRE(bus.inspectPhysical(address, output.data(), output.size()) ==
      IOPBusStatus::Unmapped);
    REQUIRE(bus.loadPhysical(address, bytes, 0) == IOPBusStatus::Unmapped);
    REQUIRE(bus.inspectPhysical(address, output.data(), 0) == IOPBusStatus::Unmapped);
    REQUIRE(output == before);
    REQUIRE(bus.read32(address).value == 0x76543210);
    REQUIRE(bus.read32(0).value == 0);
    REQUIRE(bus.read32(IOPBus::SCRATCHPAD_BASE).value == 0);
  }
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

TEST_CASE("IOP timer MMIO word routes use all six independent counter profiles",
  "[iop][bus][timer]")
{
  IOPBus bus;
  for (IOPAddress base : timerBases)
  {
    for (IOPAddress offset : {0u, 4u, 8u})
    {
      REQUIRE(bus.regionForRange(base + offset, 4) == IOPBusRegion::Mmio);
      REQUIRE(bus.read32(base + offset).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write32(base + offset, 1).status == IOPBusStatus::Unmapped);
    }
  }
  IOPTimers timers;
  bus.attachTimers(&timers);
  for (std::size_t index = 0; index < 6; ++index)
  {
    CAPTURE(index);
    const IOPAddress base = timerBases[index];
    const IOPWord mask = index < 3 ? 0xffff : 0xffffffff;
    REQUIRE(bus.write32(base, 0x12345678).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(base + 8, 0x89abcdef).status == IOPBusStatus::Completed);
    const auto count = bus.read32(base);
    const auto target = bus.read32(base + 8);
    REQUIRE(count.status == IOPBusStatus::Completed);
    REQUIRE(count.value == (0x12345678 & mask));
    REQUIRE(target.status == IOPBusStatus::Completed);
    REQUIRE(target.value == (0x89abcdef & mask));
    REQUIRE(bus.write32(base + 4, 0xffffffff).status == IOPBusStatus::Completed);
    const auto mode = bus.read32(base + 4);
    REQUIRE(mode.status == IOPBusStatus::Completed);
    REQUIRE(mode.value == 0x67ff);
    REQUIRE(timers.counter(index).count == 0);
    REQUIRE(timers.counter(index).target == (0x89abcdef & mask));
    REQUIRE(timers.counter(index).gateWaiting ==
      (index == 0 || index == 1 || index == 3));
  }
}

TEST_CASE("IOP timer MMIO halfword admission follows the approved shared subset",
  "[iop][bus][timer]")
{
  IOPBus bus;
  IOPTimers timers = activeTimers();
  bus.attachTimers(&timers);
  for (std::size_t index = 0; index < 6; ++index)
  {
    for (IOPAddress offset : {0u, 8u})
    {
      CAPTURE(index);
      CAPTURE(offset);
      const IOPAddress address = timerBases[index] + offset;
      const IOPWord value = offset == 0 ?
        timers.counter(index).count : timers.counter(index).target;
      const IOPTimers before = timers;
      const auto low = bus.read16(address);
      REQUIRE(low.status == IOPBusStatus::Completed);
      REQUIRE(low.value == (value & 0xffff));
      const auto high = bus.read16(address + 2);
      REQUIRE(high.status ==
        (index < 3 ? IOPBusStatus::Unmapped : IOPBusStatus::Completed));
      REQUIRE(high.value == (index < 3 ? 0 : value >> 16));
      requireTimersEqual(timers, before);
      REQUIRE(bus.write16(address + 2, 0x9876).status == IOPBusStatus::Unmapped);
      requireTimersEqual(timers, before);
      const auto write = bus.write16(address, 0x9876);
      REQUIRE(write.status ==
        (index < 3 ? IOPBusStatus::Completed : IOPBusStatus::Unmapped));
      IOPTimers expected = before;
      if (index < 3)
      {
        if (offset == 0)
        {
          expected.writeCount(index, 0x9876);
        }
        else
        {
          expected.writeTarget(index, 0x9876);
        }
      }
      requireTimersEqual(timers, expected);
    }
  }
}

TEST_CASE("IOP timer MMIO mode accesses acknowledge exactly one selected owner",
  "[iop][bus][timer]")
{
  for (std::size_t index = 0; index < 6; ++index)
  {
    for (std::size_t width : {2u, 4u})
    {
      CAPTURE(index);
      CAPTURE(width);
      IOPBus bus;
      IOPTimers timers = activeTimers();
      IOPTimers expected = timers;
      IOPInterruptController interrupts;
      interrupts.assertSource(IOPTimers::capabilities(index).interruptSource);
      const IOPWord pending = interrupts.status();
      bus.attachTimers(&timers);
      bus.attachInterruptController(&interrupts);
      const IOPAddress address = timerBases[index] + 4;
      const auto read = width == 2 ? bus.read16(address) : bus.read32(address);
      REQUIRE(read.status == IOPBusStatus::Completed);
      REQUIRE(read.value == 0x1830);
      REQUIRE(expected.readMode(index) == 0x1830);
      requireTimersEqual(timers, expected);
      REQUIRE(bus.read16(address).value == 0x0430);
      REQUIRE(interrupts.status() == pending);
      const auto write = width == 2 ?
        bus.write16(address, 0xffff) : bus.write32(address, 0xffffffff);
      REQUIRE(write.status == IOPBusStatus::Completed);
      expected.writeMode(index, 0xffff);
      requireTimersEqual(timers, expected);
      REQUIRE(interrupts.status() == pending);
    }
  }
}

TEST_CASE("IOP timer MMIO rejected widths offsets and masks are atomic",
  "[iop][bus][timer]")
{
  IOPBus bus;
  IOPTimers timers = activeTimers();
  const IOPTimers before = timers;
  bus.attachTimers(&timers);
  for (IOPAddress base : timerBases)
  {
    for (IOPAddress offset = 0; offset < 12; ++offset)
    {
      const IOPAddress address = base + offset;
      CAPTURE(address);
      const auto byte = bus.read8(address);
      REQUIRE(byte.status == IOPBusStatus::Unmapped);
      REQUIRE(byte.value == 0);
      REQUIRE(bus.write8(address, 0).status == IOPBusStatus::Unmapped);
      if ((address & 1) != 0)
      {
        REQUIRE(bus.read16(address).status == IOPBusStatus::Misaligned);
        REQUIRE(bus.write16(address, 0).status == IOPBusStatus::Misaligned);
      }
      if ((address & 3) != 0)
      {
        REQUIRE(bus.read32(address).status == IOPBusStatus::Misaligned);
        REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Misaligned);
        REQUIRE(bus.writeMasked32(address, 0, 0xf).status ==
          IOPBusStatus::Misaligned);
      }
      requireTimersEqual(timers, before);
    }
    REQUIRE(bus.read16(base + 6).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read16(base + 6).value == 0);
    REQUIRE(bus.write16(base + 6, 0xffff).status == IOPBusStatus::Unmapped);
    for (IOPAddress offset : {0u, 4u, 8u})
    {
      const IOPAddress address = base + offset;
      for (std::uint8_t mask = 1; mask < 0xf; ++mask)
      {
        REQUIRE(bus.writeMasked32(address, 0, mask).status == IOPBusStatus::Unmapped);
        requireTimersEqual(timers, before);
      }
      for (std::uint8_t mask : {std::uint8_t{0}, std::uint8_t{0x10}, std::uint8_t{0xff}})
      {
        REQUIRE_THROWS_AS(bus.writeMasked32(address, 0, mask), std::invalid_argument);
        requireTimersEqual(timers, before);
      }
    }
    requireTimersEqual(timers, before);
  }
}

TEST_CASE("IOP timer MMIO full masks are direct word writes without a mode read",
  "[iop][bus][timer]")
{
  IOPBus bus;
  IOPTimers timers = activeTimers();
  bus.attachTimers(&timers);
  IOPTimers expected = timers;
  for (std::size_t index = 0; index < 6; ++index)
  {
    for (IOPAddress offset : {0u, 8u, 4u})
    {
      REQUIRE(bus.writeMasked32(timerBases[index] + offset, 0xa5fa3333, 0xf).status ==
        IOPBusStatus::Completed);
      if (offset == 0)
      {
        expected.writeCount(index, 0xa5fa3333);
      }
      else if (offset == 8)
      {
        expected.writeTarget(index, 0xa5fa3333);
      }
      else
      {
        expected.writeMode(index, 0xa5fa3333);
      }
      requireTimersEqual(timers, expected);
    }
  }
}

TEST_CASE("IOP timer MMIO physical ranges and host helpers exclude holes and aliases",
  "[iop][bus][timer]")
{
  IOPBus bus;
  IOPTimers timers = activeTimers();
  bus.attachTimers(&timers);
  const IOPTimers before = timers;
  const IOPBus &inspection = bus;
  std::array<std::uint8_t, 16> bytes;
  bytes.fill(0xa5);
  const auto oldBytes = bytes;
  for (IOPAddress base : timerBases)
  {
    for (IOPAddress offset = 0; offset < 12; ++offset)
    {
      const IOPAddress address = base + offset;
      REQUIRE(inspection.regionForRange(address, 0) == IOPBusRegion::Mmio);
      REQUIRE(inspection.regionForRange(address, 1) == IOPBusRegion::Mmio);
      for (std::size_t count : {0u, 1u, 4u, 12u, 16u})
      {
        REQUIRE(bus.loadPhysical(address, bytes.data(), count) == IOPBusStatus::Unmapped);
        REQUIRE(inspection.inspectPhysical(address, bytes.data(), count) ==
          IOPBusStatus::Unmapped);
        REQUIRE(bytes == oldBytes);
      }
    }
    REQUIRE(inspection.regionForRange(base, 12) == IOPBusRegion::Mmio);
    REQUIRE(inspection.regionForRange(base, 13) == IOPBusRegion::Unmapped);
    REQUIRE(inspection.regionForRange(base + 8, 4) == IOPBusRegion::Mmio);
    REQUIRE(inspection.regionForRange(base + 8, 5) == IOPBusRegion::Unmapped);
    for (IOPAddress address : {base + 12, base | 0x80000000, base | 0xa0000000})
    {
      REQUIRE(inspection.regionForRange(address, 4) == IOPBusRegion::Unmapped);
      REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Unmapped);
      REQUIRE(bus.writeMasked32(address, 0, 0xf).status == IOPBusStatus::Unmapped);
    }
  }
  for (IOPAddress address : {0x1f8010fcu, 0x1f801130u, 0x1f80147cu,
       0x1f8014b0u, 0x1f8014c0u, 0x1f8014c4u, 0x1f8014c8u})
  {
    REQUIRE(inspection.regionForRange(address, 4) == IOPBusRegion::Unmapped);
    REQUIRE(bus.read32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(address, 0).status == IOPBusStatus::Unmapped);
  }
  requireTimersEqual(timers, before);
}

TEST_CASE("IOP timer MMIO attachment and resets preserve independent owners",
  "[iop][bus][timer]")
{
  IOPBus bus;
  IOPTimers first = activeTimers();
  IOPTimers second;
  const IOPTimers before = first;
  IOPInterruptController interrupts;
  interrupts.assertSource(IOPInterruptSource::Timer0);
  bus.attachInterruptController(&interrupts);
  bus.attachTimers(&first);
  REQUIRE(bus.write32(0x100, 0xffffffff).status == IOPBusStatus::Completed);
  bus.reset();
  REQUIRE(bus.read32(0x100).value == 0);
  requireTimersEqual(first, before);
  REQUIRE(interrupts.status() == 0x10);
  REQUIRE(bus.read32(timerBases[0]).value == before.counter(0).count);
  bus.attachTimers(nullptr);
  for (IOPAddress base : timerBases)
  {
    REQUIRE(bus.regionForRange(base, 12) == IOPBusRegion::Mmio);
    REQUIRE(bus.read16(base + 4).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.read32(base).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write32(base + 4, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.write16(base + 4, 0).status == IOPBusStatus::Unmapped);
    REQUIRE(bus.writeMasked32(base + 4, 0, 0xf).status == IOPBusStatus::Unmapped);
  }
  requireTimersEqual(first, before);
  bus.attachTimers(&second);
  REQUIRE(bus.write16(timerBases[0], 0x1234).status == IOPBusStatus::Completed);
  REQUIRE(second.counter(0).count == 0x1234);
  requireTimersEqual(first, before);
  second.reset();
  REQUIRE(bus.read32(timerBases[0]).value == 0);
  bus.attachTimers(&first);
  REQUIRE(bus.read32(timerBases[0]).value == before.counter(0).count);
  interrupts.reset();
  requireTimersEqual(first, before);
  REQUIRE(bus.write32(IOPBus::INTC_MASK, 0xffffffff).status == IOPBusStatus::Completed);
  REQUIRE(interrupts.mask() == IOPInterruptController::SOURCE_MASK);
}
