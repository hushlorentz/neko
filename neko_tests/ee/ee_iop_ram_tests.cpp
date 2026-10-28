#include <array>
#include <cstdint>
#include <stdexcept>

#include "catch.hpp"
#include "neko_system.hpp"

namespace
{
  constexpr std::uint32_t BASE = 0x1c000000;
  constexpr std::uint32_t SIZE = 0x200000;
  constexpr std::uint32_t END = BASE + SIZE;
  constexpr EEQuadword VALUE = {
    UINT64_C(0x87654321fedcba98), UINT64_C(0x0123456789abcdef)};

  EEExecutionResult execute(NekoSystem &system, std::uint8_t opcode,
    std::uint32_t address, std::uint16_t lane = 0)
  {
    system.eeBus().write32(0,
      (std::uint32_t(opcode) << 26) | (1u << 21) | (2u << 16) | lane);
    auto &core = system.eeCore();
    core.setGeneralRegister(1, {address, 0});
    core.setGeneralRegister(2, {VALUE.low, VALUE.high});
    core.setCOP0Register(EECOP0Register::Status, 0);
    core.startExecution(0xa0000000);
    system.startTrace(100);
    return system.stepEEInstruction(16);
  }
}

TEST_CASE("EE IOP RAM aperture shares every data width with the IOP owner",
  "[ee][iop][bus][iop-ram]")
{
  NekoSystem system;
  auto &ee = system.eeBus();
  auto &iop = system.iopBus();
  for (const auto offset : {0u, 0x3f0u, 0x123450u, SIZE - 16})
  {
    CAPTURE(offset);
    const auto address = BASE + offset;
    REQUIRE(ee.writeData128(address, VALUE));
    std::array<std::uint8_t, 16> bytes{};
    REQUIRE(iop.inspectPhysical(offset, bytes.data(), bytes.size()) ==
      IOPBusStatus::Completed);
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
      const auto half = index < 8 ? VALUE.low : VALUE.high;
      REQUIRE(bytes[index] == static_cast<std::uint8_t>(half >> ((index % 8) * 8)));
    }
    REQUIRE(iop.write32(offset, 0x11223344).status == IOPBusStatus::Completed);
    std::uint8_t byte = 0;
    std::uint16_t half = 0;
    std::uint32_t word = 0;
    std::uint64_t wide = 0;
    EEQuadword quad{};
    REQUIRE(ee.readData8(address + 1, &byte));
    REQUIRE(byte == 0x33);
    REQUIRE(ee.readData16(address + 2, &half));
    REQUIRE(half == 0x1122);
    REQUIRE(ee.readData32(address, &word));
    REQUIRE(word == 0x11223344);
    REQUIRE(ee.readData64(address, &wide));
    REQUIRE(wide == UINT64_C(0x8765432111223344));
    REQUIRE(ee.readData128(address, &quad));
    REQUIRE(quad.low == wide);
    REQUIRE(quad.high == VALUE.high);
    REQUIRE(ee.writeData8(address + 1, 0xaa));
    REQUIRE(ee.writeData16(address + 2, 0xbbcc));
    REQUIRE(iop.readDMAC32(offset).value == 0xbbccaa44);
    REQUIRE(ee.writeData32(address, 0x55667788));
    REQUIRE(iop.read32(offset).value == 0x55667788);
    REQUIRE(ee.writeData64(address + 8, UINT64_C(0x1020304050607080)));
    REQUIRE(iop.read32(offset + 8).value == 0x50607080);
    REQUIRE(iop.read32(offset + 12).value == 0x10203040);
    REQUIRE(iop.writeDMAC32(offset, 0xaabbccdd).status == IOPBusStatus::Completed);
    REQUIRE(ee.readData32(address, &word));
    REQUIRE(word == 0xaabbccdd);
  }
}

TEST_CASE("EE IOP RAM aperture retains physical alignment and excluded routes",
  "[ee][iop][bus][iop-ram]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  REQUIRE(bus.writeData8(END - 1, 0x12));
  REQUIRE(bus.writeData16(END - 2, 0x3456));
  REQUIRE(bus.writeData32(END - 4, 0x789abcde));
  REQUIRE(bus.writeData64(END - 8, VALUE.low));
  REQUIRE(bus.writeData128(END - 16, VALUE));
  for (const auto address : {BASE - 16, END, BASE + SIZE * 2,
       BASE + SIZE * 3, 0x9c000000u, 0xbc000000u})
  {
    CAPTURE(address);
    std::uint8_t byte = 0xa5;
    std::uint16_t half = 0xa5a5;
    std::uint32_t word = 0xa5a5a5a5;
    std::uint64_t wide = UINT64_MAX;
    EEQuadword quad = VALUE;
    REQUIRE_FALSE(bus.readData8(address, &byte));
    REQUIRE_FALSE(bus.readData16(address, &half));
    REQUIRE_FALSE(bus.readData32(address, &word));
    REQUIRE_FALSE(bus.readData64(address, &wide));
    REQUIRE_FALSE(bus.readData128(address, &quad));
    REQUIRE(byte == 0xa5);
    REQUIRE(half == 0xa5a5);
    REQUIRE(word == 0xa5a5a5a5);
    REQUIRE(wide == UINT64_MAX);
    REQUIRE(quad.low == VALUE.low);
    REQUIRE(quad.high == VALUE.high);
    REQUIRE_FALSE(bus.writeData8(address, 0));
    REQUIRE_FALSE(bus.writeData16(address, 0));
    REQUIRE_FALSE(bus.writeData32(address, 0));
    REQUIRE_FALSE(bus.writeData64(address, 0));
    REQUIRE_FALSE(bus.writeData128(address, {0, 0}));
  }
  for (const auto address : {BASE, BASE + 0x3f0, END - 16})
  {
    CAPTURE(address);
    std::uint32_t instruction = 0x12345678;
    EEQuadword quad = VALUE;
    REQUIRE_FALSE(bus.readInstruction32(address, &instruction));
    REQUIRE(instruction == 0x12345678);
    REQUIRE_FALSE(bus.isCacheFillRange(address, 16));
    REQUIRE_FALSE(bus.isMainMemoryRange(address, 16));
    REQUIRE_FALSE(bus.readDMAC128(address, &quad));
    REQUIRE(quad.low == VALUE.low);
    REQUIRE(quad.high == VALUE.high);
    REQUIRE_FALSE(bus.writeDMAC128(address, {0, 0}));
    REQUIRE_FALSE(bus.readScratchpadDMAC128(address,
      EEScratchpadDMACMode::Normal, &quad));
    REQUIRE_FALSE(bus.writeScratchpadDMAC128(address,
      EEScratchpadDMACMode::Normal, {0, 0}));
  }
  std::uint16_t half = 0;
  std::uint32_t word = 0;
  std::uint64_t wide = 0;
  EEQuadword quad{};
  REQUIRE_THROWS_AS(bus.readData16(END - 1, &half), std::invalid_argument);
  REQUIRE_THROWS_AS(bus.writeData16(END - 1, 0), std::invalid_argument);
  REQUIRE_THROWS_AS(bus.readData32(END - 2, &word), std::invalid_argument);
  REQUIRE_THROWS_AS(bus.writeData32(END - 2, 0), std::invalid_argument);
  REQUIRE_THROWS_AS(bus.readData64(END - 4, &wide), std::invalid_argument);
  REQUIRE_THROWS_AS(bus.writeData64(END - 4, 0), std::invalid_argument);
  REQUIRE_THROWS_AS(bus.readData128(END - 8, &quad), std::invalid_argument);
  REQUIRE_THROWS_AS(bus.writeData128(END - 8, {0, 0}), std::invalid_argument);
  for (const auto offset : {SIZE - 1, SIZE, UINT32_MAX, IOPBus::SCRATCHPAD_BASE})
  {
    CAPTURE(offset);
    std::array<std::uint8_t, 16> bytes{};
    bytes.fill(0xa5);
    REQUIRE(system.iopBus().readRAMBytes(offset, bytes.data(), bytes.size()) ==
      IOPBusStatus::Unmapped);
    for (const auto byte : bytes)
    {
      REQUIRE(byte == 0xa5);
    }
    REQUIRE(system.iopBus().writeRAMBytes(offset, bytes.data(), bytes.size()) ==
      IOPBusStatus::Unmapped);
  }
  REQUIRE(bus.readData128(END - 16, &quad));
  REQUIRE(quad.low == VALUE.low);
  REQUIRE(quad.high == VALUE.high);
}

TEST_CASE("EE guest IOP RAM data accesses use normal translated memory traces",
  "[ee][iop][bus][guest][trace][iop-ram]")
{
  struct Access
  {
    std::uint8_t load;
    std::uint8_t store;
    std::uint8_t width;
    std::uint64_t loaded;
  };
  const Access accesses[] = {
    {0x24, 0x28, 1, 0x98}, {0x25, 0x29, 2, 0xba98},
    {0x23, 0x2b, 4, UINT64_C(0xfffffffffedcba98)},
    {0x37, 0x3f, 8, VALUE.low}, {0x1e, 0x1f, 16, VALUE.low}};
  constexpr std::uint32_t OFFSET = 0x3f0;
  for (const auto access : accesses)
  {
    for (const bool store : {false, true})
    {
      CAPTURE(access.width);
      CAPTURE(store);
      NekoSystem system;
      if (!store)
      {
        REQUIRE(system.eeBus().writeData128(BASE + OFFSET, VALUE));
      }
      const auto result = execute(system, store ? access.store : access.load,
        0xbc000000 + OFFSET);
      REQUIRE(result.pendingException == EEException::None);
      REQUIRE_FALSE(result.cycleLimitReached);
      if (store)
      {
        std::array<std::uint8_t, 16> bytes{};
        REQUIRE(system.iopBus().inspectPhysical(OFFSET, bytes.data(), bytes.size()) ==
          IOPBusStatus::Completed);
        for (std::size_t index = 0; index < bytes.size(); ++index)
        {
          const auto half = index < 8 ? VALUE.low : VALUE.high;
          REQUIRE(bytes[index] == (index < access.width
            ? static_cast<std::uint8_t>(half >> ((index % 8) * 8)) : 0));
        }
      }
      else
      {
        REQUIRE(system.eeCore().generalRegister(2).low == access.loaded);
        REQUIRE(system.eeCore().generalRegister(2).high == VALUE.high);
      }
      std::size_t observed = 0;
      for (const auto &event : system.trace())
      {
        if (event.subsystem == NekoTraceSubsystem::EE &&
            event.type == NekoTraceEventType::MemoryAccess &&
            event.value0 == 0xbc000000 + OFFSET)
        {
          ++observed;
          REQUIRE(event.value3 == nekoPackEEMemoryTraceMetadata({
            BASE + OFFSET, access.width,
            store ? NekoEETraceMemory::AccessKind::DataStore :
                    NekoEETraceMemory::AccessKind::DataLoad,
            NekoEETraceMemory::TranslationOutcome::Translated,
            NekoEETraceMemory::CacheRoute::Uncached, 2, false, true,
            NekoEETraceMemory::CacheAccess::Bypassed,
            NekoEETraceMemory::TransferOutcome::Completed,
            NekoEETraceMemory::FailurePhase::None}));
        }
      }
      REQUIRE(observed == 1);
      REQUIRE_FALSE(system.traceTruncated());
    }
  }
}

TEST_CASE("EE IOP RAM merge stores preserve unselected peer bytes",
  "[ee][iop][bus][guest][iop-ram]")
{
  for (const std::uint8_t opcode : {0x2a, 0x2e, 0x2c, 0x2d})
  {
    const unsigned width = opcode == 0x2a || opcode == 0x2e ? 4 : 8;
    const bool left = opcode == 0x2a || opcode == 0x2c;
    for (std::uint16_t lane = 0; lane < width; ++lane)
    {
      CAPTURE(opcode);
      CAPTURE(lane);
      NekoSystem system;
      REQUIRE(system.eeBus().writeData128(END - 16, {UINT64_MAX, UINT64_MAX}));
      const auto result = execute(system, opcode, 0xbc000000 + SIZE - 16, lane);
      REQUIRE(result.pendingException == EEException::None);
      REQUIRE_FALSE(result.cycleLimitReached);
      std::array<std::uint8_t, 16> bytes{};
      REQUIRE(system.iopBus().inspectPhysical(SIZE - 16, bytes.data(), bytes.size()) ==
        IOPBusStatus::Completed);
      for (unsigned index = 0; index < bytes.size(); ++index)
      {
        std::uint8_t expected = 0xff;
        if (left && index <= lane)
        {
          expected = static_cast<std::uint8_t>(
            VALUE.low >> ((width - 1 - lane + index) * 8));
        }
        else if (!left && index >= lane && index < width)
        {
          expected = static_cast<std::uint8_t>(VALUE.low >> ((index - lane) * 8));
        }
        REQUIRE(bytes[index] == expected);
      }
    }
  }
}

TEST_CASE("EE IOP RAM aperture follows canonical restore and reset without duplicate storage",
  "[ee][iop][bus][save-state][iop-ram]")
{
  NekoSystem original;
  REQUIRE(original.eeBus().writeData128(BASE + 0x3f0, VALUE));
  REQUIRE(original.iopBus().write32(SIZE - 4, 0x12345678).status ==
    IOPBusStatus::Completed);
  const auto saved = original.saveState();
  const auto hash = original.iopStateHash();
  NekoSystem restored;
  REQUIRE_NOTHROW(restored.loadState(saved));
  REQUIRE(restored.saveState() == saved);
  REQUIRE(restored.iopStateHash() == hash);
  EEQuadword quad{};
  REQUIRE(restored.eeBus().readData128(BASE + 0x3f0, &quad));
  REQUIRE(quad.low == VALUE.low);
  REQUIRE(quad.high == VALUE.high);
  REQUIRE(restored.eeBus().read32(END - 4) == 0x12345678);
  REQUIRE(restored.eeBus().writeData32(BASE + 0x3f0, 0x76543210));
  REQUIRE(restored.iopStateHash() != hash);
  REQUIRE(restored.iopBus().read32(0x3f0).value == 0x76543210);
  REQUIRE(original.iopBus().read32(0x3f0).value == 0xfedcba98);
  REQUIRE(restored.iopBus().write32(0x3f4, 0xaabbccdd).status ==
    IOPBusStatus::Completed);
  REQUIRE(restored.eeBus().read32(BASE + 0x3f4) == 0xaabbccdd);
  restored.reset();
  REQUIRE(restored.eeBus().readData128(BASE + 0x3f0, &quad));
  REQUIRE(quad.low == 0);
  REQUIRE(quad.high == 0);
  REQUIRE(restored.eeBus().read32(END - 4) == 0);
  REQUIRE(restored.eeBus().writeData32(END - 4, 0xabcdef01));
  REQUIRE(restored.iopBus().read32(SIZE - 4).value == 0xabcdef01);
}
