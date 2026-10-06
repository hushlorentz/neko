#include <cstdint>
#include <type_traits>

#include "catch.hpp"
#include "ee_memory_system.hpp"
#include "neko_system.hpp"

struct EEMemorySystemTestAccess
{
  static std::array<std::uint8_t, 4> cacheLineRefillOrder(
    std::uint32_t physicalAddress)
  {
    return EEMemorySystem::cacheLineRefillOrder(physicalAddress);
  }

  static EECacheLineFillResult fillCacheLine(
    const EEMemorySystem &memorySystem,
    const EEBus &bus,
    std::uint32_t physicalAddress)
  {
    return memorySystem.fillCacheLine(bus, physicalAddress);
  }

  static EECacheLineTransferResult writeBackDataCacheLine(
    const EEMemorySystem &memorySystem,
    EEBus *bus,
    std::size_t set,
    const EECacheLine &line)
  {
    return memorySystem.writeBackDataCacheLine(bus, set, line);
  }

  static EEInstructionCacheFetchResult fetchInstruction(
    EEMemorySystem *memorySystem,
    const EEBus &bus,
    const EEAddressTranslationResult &translation)
  {
    return memorySystem->fetchInstruction(bus, translation);
  }

  static EEDataCacheLoadResult loadData(
    EEMemorySystem *memorySystem,
    EEBus &bus,
    const EEAddressTranslationResult &translation,
    std::size_t width)
  {
    return memorySystem->loadData(bus, translation, width);
  }

  static EEDataCacheStoreResult storeData(
    EEMemorySystem *memorySystem,
    EEBus *bus,
    const EEAddressTranslationResult &translation,
    const std::array<std::uint8_t, 16> &data,
    std::size_t width)
  {
    return memorySystem->storeData(
      bus,
      translation,
      data,
      width);
  }

  static EEPrefetchResult prefetchData(
    EEMemorySystem *memorySystem,
    EEBus *bus,
    const EEPrefetchRequest &request)
  {
    return memorySystem->prefetchData(bus, request);
  }

  static EECacheMaintenanceResult maintainCache(
    EEMemorySystem *memorySystem,
    EEBus *bus,
    const EECacheMaintenanceRequest &request)
  {
    return memorySystem->maintainCache(bus, request);
  }

  static void setInstructionCacheLine(
    EEMemorySystem *memorySystem,
    std::size_t set,
    std::size_t way,
    const EECacheLine &line)
  {
    memorySystem->instructionCache[set][way] = line;
  }

  static void setDataCacheLine(
    EEMemorySystem *memorySystem,
    std::size_t set,
    std::size_t way,
    const EECacheLine &line)
  {
    memorySystem->dataCache[set][way] = line;
  }
};

static_assert(
  std::is_trivially_copyable<
    EEAddressTranslationContext>::value,
  "EE translation context must remain trivially copyable.");
static_assert(
  std::is_trivially_copyable<
    EEAddressTranslationResult>::value,
  "EE translation result must remain trivially copyable.");
static_assert(
  std::is_trivially_copyable<
    EECacheLineTransferResult>::value,
  "EE cache-line transfer results must remain trivially copyable.");
static_assert(
  std::is_trivially_copyable<
    EECacheLineFillResult>::value,
  "EE cache-line fill results must remain trivially copyable.");
static_assert(
  std::is_trivially_copyable<
    EEDataCacheLoadResult>::value,
  "EE data-cache load results must remain trivially copyable.");
static_assert(
  std::is_trivially_copyable<
    EEDataCacheStoreResult>::value,
  "EE data-cache store results must remain trivially copyable.");
static_assert(
  std::is_trivially_copyable<
    EEPrefetchRequest>::value,
  "EE prefetch requests must remain trivially copyable.");
static_assert(
  std::is_trivially_copyable<
    EEPrefetchResult>::value,
  "EE prefetch results must remain trivially copyable.");
static_assert(
  std::is_trivially_copyable<
    EECacheMaintenanceRequest>::value,
  "EE cache-maintenance requests must remain trivially copyable.");
static_assert(
  std::is_trivially_copyable<
    EECacheMaintenanceResult>::value,
  "EE cache-maintenance results must remain trivially copyable.");
static_assert(
  EEMemorySystem::ITLB_ENTRY_COUNT == 2,
  "The EE ITLB capacity must remain architectural.");
static_assert(
  EEMemorySystem::DTLB_ENTRY_COUNT == 4,
  "The EE DTLB capacity must remain architectural.");
static_assert(
  EEMemorySystem::SCRATCHPAD_SIZE == 16 * 1024,
  "The EE scratchpad capacity must remain architectural.");
static_assert(
  EEMemorySystem::SCRATCHPAD_QWORD_COUNT == 1024,
  "The EE scratchpad organization must remain architectural.");
static_assert(
  EEMemorySystem::CACHE_LINE_SIZE == 64,
  "EE cache lines must remain 64 bytes.");
static_assert(
  EEMemorySystem::CACHE_WAY_COUNT == 2,
  "EE caches must remain two-way set associative.");
static_assert(
  EEMemorySystem::INSTRUCTION_CACHE_SET_COUNT == 128,
  "The EE instruction cache must remain 16 KiB.");
static_assert(
  EEMemorySystem::DATA_CACHE_SET_COUNT == 64,
  "The EE data cache must remain 8 KiB.");

namespace
{
  EEAddressTranslationContext context(
    EEPrivilegeMode privilege,
    bool exceptionLevel = false,
    bool errorLevel = false)
  {
    return {privilege, exceptionLevel, errorLevel};
  }

  void requireTLBRoute(
    const EEAddressTranslationResult &result,
    std::uint32_t virtualAddress)
  {
    REQUIRE(result.outcome == EEAddressTranslationOutcome::TLBLookup);
    REQUIRE(result.virtualAddress == virtualAddress);
    REQUIRE(result.cacheRoute == EECacheRoute::TLBSelected);
  }

  void requireDirectRoute(
    const EEAddressTranslationResult &result,
    std::uint32_t virtualAddress,
    std::uint32_t physicalAddress,
    EECacheRoute cacheRoute)
  {
    REQUIRE(result.outcome == EEAddressTranslationOutcome::Translated);
    REQUIRE(result.virtualAddress == virtualAddress);
    REQUIRE(result.physicalAddress == physicalAddress);
    REQUIRE(result.cacheRoute == cacheRoute);
  }

  EEAddressTranslationResult instructionTranslation(
    std::uint32_t virtualAddress,
    std::uint32_t physicalAddress,
    EECacheRoute cacheRoute = EECacheRoute::CachedNoncoherent)
  {
    return {
      EEAddressTranslationOutcome::Translated,
      virtualAddress,
      physicalAddress,
      cacheRoute,
      static_cast<std::uint8_t>(
        cacheRoute == EECacheRoute::CachedNoncoherent ? 3 : 2),
      EEAddressRoute::MainBus,
      0xff
    };
  }

  EEAddressTranslationResult dataTranslation(
    std::uint32_t virtualAddress,
    std::uint32_t physicalAddress,
    EECacheRoute cacheRoute = EECacheRoute::CachedNoncoherent)
  {
    return instructionTranslation(
      virtualAddress,
      physicalAddress,
      cacheRoute);
  }

  std::array<std::uint8_t, 16> storeBytes(
    std::uint64_t low,
    std::uint64_t high = 0)
  {
    std::array<std::uint8_t, 16> data = {};
    for (std::size_t index = 0; index < 8; ++index)
    {
      data[index] =
        static_cast<std::uint8_t>(low >> (index * 8));
      data[index + 8] =
        static_cast<std::uint8_t>(high >> (index * 8));
    }
    return data;
  }
}

TEST_CASE("EE scratchpad provides checked little-endian access")
{
  EEMemorySystem memorySystem;
  const EEQuadword quadword = {
    UINT64_C(0x7766554433221100),
    UINT64_C(0xffeeddccbbaa9988)
  };

  REQUIRE(memorySystem.writeScratchpad128(0x100, quadword));

  std::uint8_t byte = 0;
  std::uint16_t halfword = 0;
  std::uint32_t word = 0;
  std::uint64_t doubleword = 0;
  EEQuadword loadedQuadword = {};
  REQUIRE(memorySystem.readScratchpad8(0x10f, &byte));
  REQUIRE(byte == 0xff);
  REQUIRE(memorySystem.readScratchpad16(0x10e, &halfword));
  REQUIRE(halfword == 0xffee);
  REQUIRE(memorySystem.readScratchpad32(0x10c, &word));
  REQUIRE(word == UINT32_C(0xffeeddcc));
  REQUIRE(memorySystem.readScratchpad64(0x108, &doubleword));
  REQUIRE(doubleword == UINT64_C(0xffeeddccbbaa9988));
  REQUIRE(memorySystem.readScratchpad128(0x100, &loadedQuadword));
  REQUIRE(loadedQuadword.low == quadword.low);
  REQUIRE(loadedQuadword.high == quadword.high);

  REQUIRE(memorySystem.writeScratchpad8(0x100, 0xaa));
  REQUIRE(memorySystem.writeScratchpad16(0x102, 0xbbcc));
  REQUIRE(memorySystem.writeScratchpad32(0x104, UINT32_C(0xddeeff00)));
  REQUIRE(
    memorySystem.writeScratchpad64(
      0x108,
      UINT64_C(0x1122334455667788)));
  REQUIRE(memorySystem.readScratchpad128(0x100, &loadedQuadword));
  REQUIRE(
    loadedQuadword.low ==
    UINT64_C(0xddeeff00bbcc11aa));
  REQUIRE(
    loadedQuadword.high ==
    UINT64_C(0x1122334455667788));
}

TEST_CASE("EE scratchpad rejects ranges outside its fixed capacity")
{
  EEMemorySystem memorySystem;
  const EEQuadword lastQuadword = {
    UINT64_C(0x0123456789abcdef),
    UINT64_C(0xfedcba9876543210)
  };
  REQUIRE(
    memorySystem.writeScratchpad128(
      EEMemorySystem::SCRATCHPAD_SIZE - 16,
      lastQuadword));

  std::uint8_t byte = 0x5a;
  std::uint16_t halfword = 0x5a5a;
  std::uint32_t word = UINT32_C(0x5a5a5a5a);
  std::uint64_t doubleword = UINT64_C(0x5a5a5a5a5a5a5a5a);
  EEQuadword quadword = {
    UINT64_C(0x5a5a5a5a5a5a5a5a),
    UINT64_C(0x5a5a5a5a5a5a5a5a)
  };
  REQUIRE_FALSE(
    memorySystem.readScratchpad8(
      EEMemorySystem::SCRATCHPAD_SIZE,
      &byte));
  REQUIRE_FALSE(
    memorySystem.readScratchpad16(
      EEMemorySystem::SCRATCHPAD_SIZE - 1,
      &halfword));
  REQUIRE_FALSE(
    memorySystem.readScratchpad32(
      EEMemorySystem::SCRATCHPAD_SIZE - 3,
      &word));
  REQUIRE_FALSE(
    memorySystem.readScratchpad64(
      EEMemorySystem::SCRATCHPAD_SIZE - 7,
      &doubleword));
  REQUIRE_FALSE(
    memorySystem.readScratchpad128(
      EEMemorySystem::SCRATCHPAD_SIZE - 15,
      &quadword));
  REQUIRE(byte == 0x5a);
  REQUIRE(halfword == 0x5a5a);
  REQUIRE(word == UINT32_C(0x5a5a5a5a));
  REQUIRE(doubleword == UINT64_C(0x5a5a5a5a5a5a5a5a));
  REQUIRE(quadword.low == UINT64_C(0x5a5a5a5a5a5a5a5a));
  REQUIRE(quadword.high == UINT64_C(0x5a5a5a5a5a5a5a5a));

  REQUIRE_FALSE(
    memorySystem.writeScratchpad16(
      EEMemorySystem::SCRATCHPAD_SIZE - 1,
      0));
  REQUIRE_FALSE(
    memorySystem.writeScratchpad128(
      EEMemorySystem::SCRATCHPAD_SIZE - 15,
      {}));
  REQUIRE(memorySystem.readScratchpad128(
    EEMemorySystem::SCRATCHPAD_SIZE - 16,
    &quadword));
  REQUIRE(quadword.low == lastQuadword.low);
  REQUIRE(quadword.high == lastQuadword.high);
}

TEST_CASE("EE scratchpad reset clears its fixed storage")
{
  EEMemorySystem memorySystem;
  REQUIRE(
    memorySystem.writeScratchpad64(
      0x1230,
      UINT64_C(0x0123456789abcdef)));

  memorySystem.reset();

  std::uint64_t value = UINT64_MAX;
  REQUIRE(memorySystem.readScratchpad64(0x1230, &value));
  REQUIRE(value == 0);
}

TEST_CASE("EE cache arrays reset to deterministic invalid lines")
{
  EEMemorySystem memorySystem;
  memorySystem.reset();

  for (std::size_t set = 0;
       set < EEMemorySystem::INSTRUCTION_CACHE_SET_COUNT;
       ++set)
  {
    for (std::size_t way = 0;
         way < EEMemorySystem::CACHE_WAY_COUNT;
         ++way)
    {
      const EECacheLine &line =
        memorySystem.instructionCacheLine(set, way);
      REQUIRE(line.physicalTag == 0);
      REQUIRE_FALSE(line.valid);
      REQUIRE_FALSE(line.dirty);
      REQUIRE_FALSE(line.leastRecentlyFilled);
      REQUIRE_FALSE(line.locked);
      for (const std::uint8_t byte : line.data)
      {
        REQUIRE(byte == 0);
      }
    }
  }

  for (std::size_t set = 0;
       set < EEMemorySystem::DATA_CACHE_SET_COUNT;
       ++set)
  {
    for (std::size_t way = 0;
         way < EEMemorySystem::CACHE_WAY_COUNT;
         ++way)
    {
      const EECacheLine &line =
        memorySystem.dataCacheLine(set, way);
      REQUIRE(line.physicalTag == 0);
      REQUIRE_FALSE(line.valid);
      REQUIRE_FALSE(line.dirty);
      REQUIRE_FALSE(line.leastRecentlyFilled);
      REQUIRE_FALSE(line.locked);
      for (const std::uint8_t byte : line.data)
      {
        REQUIRE(byte == 0);
      }
    }
  }
}

TEST_CASE("EE unimplemented cache maintenance remains explicitly unsupported")
{
  NekoSystem system;
  EEMemorySystem &memorySystem = system.eeMemorySystem();
  const EECacheMaintenanceRequest request{
    static_cast<EECacheOperation>(0x02),
    UINT32_C(0x81234567),
    {}
  };

  const EECacheMaintenanceResult result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      &system.eeBus(),
      request);

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::UnsupportedOperation);
  REQUIRE_FALSE(result.cacheHitStatusValid);
  REQUIRE_FALSE(result.cacheHit);
  REQUIRE(
    result.translation.virtualAddress ==
    request.virtualAddress);
}

TEST_CASE("EE data-cache index invalidate clears only the selected line")
{
  EEMemorySystem memorySystem;
  constexpr std::size_t set = 0x2d;
  constexpr std::size_t way = 1;
  EECacheLine selected;
  selected.physicalTag = UINT32_C(0x12345000);
  selected.valid = true;
  selected.dirty = true;
  selected.leastRecentlyFilled = true;
  selected.locked = true;
  selected.data.fill(0xa5);
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    set,
    way,
    selected);
  EECacheLine other;
  other.physicalTag = UINT32_C(0x6789a000);
  other.valid = true;
  other.dirty = true;
  other.locked = true;
  other.data[3] = 0x5a;
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    set,
    0,
    other);

  const EECacheMaintenanceResult result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      nullptr,
      {
        EECacheOperation::DataIndexInvalidate,
        UINT32_C(0x00010000) |
          static_cast<std::uint32_t>(set << 6) |
          static_cast<std::uint32_t>(way),
        {EEPrivilegeMode::User, false, false}
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::Completed);
  REQUIRE_FALSE(result.cacheHitStatusValid);
  const EECacheLine &invalidated =
    memorySystem.dataCacheLine(set, way);
  REQUIRE(invalidated.physicalTag == 0);
  REQUIRE_FALSE(invalidated.valid);
  REQUIRE_FALSE(invalidated.dirty);
  REQUIRE(invalidated.leastRecentlyFilled);
  REQUIRE_FALSE(invalidated.locked);
  for (const std::uint8_t byte : invalidated.data)
  {
    REQUIRE(byte == 0);
  }
  const EECacheLine &unchanged =
    memorySystem.dataCacheLine(set, 0);
  REQUIRE(unchanged.physicalTag == other.physicalTag);
  REQUIRE(unchanged.valid);
  REQUIRE(unchanged.dirty);
  REQUIRE(unchanged.locked);
  REQUIRE(unchanged.data[3] == other.data[3]);
}

TEST_CASE("EE data-cache index writeback invalidates after completion")
{
  NekoSystem system;
  EEMemorySystem &memorySystem = system.eeMemorySystem();
  constexpr std::size_t set = 0x15;
  constexpr std::size_t way = 1;
  constexpr std::uint32_t physicalTag = UINT32_C(0x3000);
  constexpr std::uint32_t lineAddress =
    physicalTag + static_cast<std::uint32_t>(set << 6);
  EECacheLine line;
  line.physicalTag = physicalTag;
  line.valid = true;
  line.dirty = true;
  line.leastRecentlyFilled = true;
  line.locked = true;
  for (std::size_t offset = 0; offset < line.data.size(); ++offset)
  {
    line.data[offset] =
      static_cast<std::uint8_t>((offset * 5) ^ 0x96);
  }
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    set,
    way,
    line);

  const EECacheMaintenanceResult result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      &system.eeBus(),
      {
        EECacheOperation::DataIndexWriteBackInvalidate,
        UINT32_C(0x00020000) |
          static_cast<std::uint32_t>(set << 6) |
          static_cast<std::uint32_t>(way),
        {EEPrivilegeMode::User, false, false}
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::Completed);
  REQUIRE_FALSE(result.cacheHitStatusValid);
  for (std::uint32_t offset = 0; offset < 64; ++offset)
  {
    std::uint8_t byte = 0;
    REQUIRE(system.eeBus().readData8(lineAddress + offset, &byte));
    REQUIRE(
      byte ==
      static_cast<std::uint8_t>((offset * 5) ^ 0x96));
  }
  const EECacheLine &invalidated =
    memorySystem.dataCacheLine(set, way);
  REQUIRE(invalidated.physicalTag == 0);
  REQUIRE_FALSE(invalidated.valid);
  REQUIRE_FALSE(invalidated.dirty);
  REQUIRE(invalidated.leastRecentlyFilled);
  REQUIRE_FALSE(invalidated.locked);
  for (const std::uint8_t byte : invalidated.data)
  {
    REQUIRE(byte == 0);
  }
}

TEST_CASE("EE data-cache index writeback failure preserves the complete line")
{
  NekoSystem system;
  EEMemorySystem &memorySystem = system.eeMemorySystem();
  constexpr std::size_t set = 0x3f;
  constexpr std::size_t way = 0;
  EECacheLine line;
  line.physicalTag = EEMemoryMap::MAIN_MEMORY_SIZE;
  line.valid = true;
  line.dirty = true;
  line.leastRecentlyFilled = true;
  line.locked = true;
  line.data.fill(0x6d);
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    set,
    way,
    line);

  const EECacheMaintenanceResult result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      &system.eeBus(),
      {
        EECacheOperation::DataIndexWriteBackInvalidate,
        static_cast<std::uint32_t>(set << 6) |
          static_cast<std::uint32_t>(way),
        {}
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::PhysicalBusError);
  REQUIRE_FALSE(result.cacheHitStatusValid);
  const EECacheLine &preserved =
    memorySystem.dataCacheLine(set, way);
  REQUIRE(preserved.physicalTag == line.physicalTag);
  REQUIRE(preserved.valid == line.valid);
  REQUIRE(preserved.dirty == line.dirty);
  REQUIRE(
    preserved.leastRecentlyFilled ==
    line.leastRecentlyFilled);
  REQUIRE(preserved.locked == line.locked);
  REQUIRE(preserved.data == line.data);
}

TEST_CASE("EE data-cache hit writeback invalidate writes and clears")
{
  NekoSystem system;
  EEMemorySystem &memorySystem = system.eeMemorySystem();
  constexpr std::uint32_t virtualAddress = UINT32_C(0x80003210);
  constexpr std::size_t set =
    (virtualAddress >> 6) &
    (EEMemorySystem::DATA_CACHE_SET_COUNT - 1);
  constexpr std::size_t way = 1;
  EECacheLine matching;
  matching.physicalTag = UINT32_C(0x3000);
  matching.valid = true;
  matching.dirty = true;
  matching.leastRecentlyFilled = true;
  matching.locked = true;
  for (std::size_t offset = 0;
       offset < matching.data.size();
       ++offset)
  {
    matching.data[offset] =
      static_cast<std::uint8_t>((offset * 7) ^ 0xb4);
  }
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    set,
    way,
    matching);
  EECacheLine other;
  other.physicalTag = UINT32_C(0x9000);
  other.valid = true;
  other.dirty = true;
  other.data[5] = 0x6c;
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    set,
    0,
    other);

  const EECacheMaintenanceResult result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      &system.eeBus(),
      {
        EECacheOperation::DataHitWriteBackInvalidate,
        virtualAddress,
        {}
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::Completed);
  REQUIRE(
    result.translation.physicalAddress ==
    UINT32_C(0x3210));
  REQUIRE(result.cacheHitStatusValid);
  REQUIRE(result.cacheHit);
  for (std::uint32_t offset = 0; offset < 64; ++offset)
  {
    std::uint8_t byte = 0;
    REQUIRE(
      system.eeBus().readData8(
        UINT32_C(0x3200) + offset,
        &byte));
    REQUIRE(
      byte ==
      static_cast<std::uint8_t>((offset * 7) ^ 0xb4));
  }
  const EECacheLine &invalidated =
    memorySystem.dataCacheLine(set, way);
  REQUIRE(invalidated.physicalTag == 0);
  REQUIRE_FALSE(invalidated.valid);
  REQUIRE_FALSE(invalidated.dirty);
  REQUIRE(invalidated.leastRecentlyFilled);
  REQUIRE_FALSE(invalidated.locked);
  for (const std::uint8_t byte : invalidated.data)
  {
    REQUIRE(byte == 0);
  }
  const EECacheLine &unchanged =
    memorySystem.dataCacheLine(set, 0);
  REQUIRE(unchanged.physicalTag == other.physicalTag);
  REQUIRE(unchanged.valid);
  REQUIRE(unchanged.dirty);
  REQUIRE(unchanged.data[5] == other.data[5]);
}

TEST_CASE("EE data-cache hit invalidate discards dirty data")
{
  NekoSystem system;
  EEMemorySystem &memorySystem = system.eeMemorySystem();
  constexpr std::uint32_t virtualAddress = UINT32_C(0x80002420);
  constexpr std::size_t set =
    (virtualAddress >> 6) &
    (EEMemorySystem::DATA_CACHE_SET_COUNT - 1);
  EECacheLine matching;
  matching.physicalTag = UINT32_C(0x2000);
  matching.valid = true;
  matching.dirty = true;
  matching.leastRecentlyFilled = true;
  matching.locked = true;
  matching.data.fill(0xd7);
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    set,
    0,
    matching);

  const EECacheMaintenanceResult result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      &system.eeBus(),
      {
        EECacheOperation::DataHitInvalidate,
        virtualAddress,
        {}
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::Completed);
  REQUIRE(result.cacheHitStatusValid);
  REQUIRE(result.cacheHit);
  std::uint8_t byte = 0xff;
  REQUIRE(system.eeBus().readData8(UINT32_C(0x2400), &byte));
  REQUIRE(byte == 0);
  const EECacheLine &invalidated =
    memorySystem.dataCacheLine(set, 0);
  REQUIRE_FALSE(invalidated.valid);
  REQUIRE_FALSE(invalidated.dirty);
  REQUIRE(invalidated.leastRecentlyFilled);
  REQUIRE_FALSE(invalidated.locked);
}

TEST_CASE("EE data-cache hit writeback preserves a clean valid line")
{
  NekoSystem system;
  EEMemorySystem &memorySystem = system.eeMemorySystem();
  constexpr std::uint32_t virtualAddress = UINT32_C(0x80001834);
  constexpr std::size_t set =
    (virtualAddress >> 6) &
    (EEMemorySystem::DATA_CACHE_SET_COUNT - 1);
  EECacheLine matching;
  matching.physicalTag = UINT32_C(0x1000);
  matching.valid = true;
  matching.dirty = true;
  matching.leastRecentlyFilled = true;
  matching.locked = true;
  matching.data.fill(0x9b);
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    set,
    1,
    matching);

  const EECacheMaintenanceResult result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      &system.eeBus(),
      {
        EECacheOperation::DataHitWriteBack,
        virtualAddress,
        {}
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::Completed);
  REQUIRE(result.cacheHitStatusValid);
  REQUIRE(result.cacheHit);
  const EECacheLine &written =
    memorySystem.dataCacheLine(set, 1);
  REQUIRE(written.physicalTag == matching.physicalTag);
  REQUIRE(written.valid);
  REQUIRE_FALSE(written.dirty);
  REQUIRE(
    written.leastRecentlyFilled ==
    matching.leastRecentlyFilled);
  REQUIRE(written.locked == matching.locked);
  REQUIRE(written.data == matching.data);
  for (std::uint32_t offset = 0; offset < 64; ++offset)
  {
    std::uint8_t byte = 0;
    REQUIRE(
      system.eeBus().readData8(
        UINT32_C(0x1800) + offset,
        &byte));
    REQUIRE(byte == 0x9b);
  }
}

TEST_CASE("EE data-cache hit operations preserve misses and faults")
{
  for (const EECacheOperation operation : {
         EECacheOperation::DataHitWriteBackInvalidate,
         EECacheOperation::DataHitInvalidate,
         EECacheOperation::DataHitWriteBack})
  {
    NekoSystem system;
    EEMemorySystem &memorySystem = system.eeMemorySystem();
    constexpr std::uint32_t virtualAddress =
      UINT32_C(0x80002a10);
    constexpr std::size_t set =
      (virtualAddress >> 6) &
      (EEMemorySystem::DATA_CACHE_SET_COUNT - 1);
    EECacheLine line;
    line.physicalTag = UINT32_C(0x7000);
    line.valid = true;
    line.dirty = true;
    line.leastRecentlyFilled = true;
    line.locked = true;
    line.data.fill(0x4e);
    EEMemorySystemTestAccess::setDataCacheLine(
      &memorySystem,
      set,
      0,
      line);

    EECacheMaintenanceResult result =
      EEMemorySystemTestAccess::maintainCache(
        &memorySystem,
        &system.eeBus(),
        {operation, virtualAddress, {}});

    REQUIRE(
      result.outcome ==
      EECacheMaintenanceOutcome::Completed);
    REQUIRE(result.cacheHitStatusValid);
    REQUIRE_FALSE(result.cacheHit);
    const EECacheLine &afterMiss =
      memorySystem.dataCacheLine(set, 0);
    REQUIRE(afterMiss.physicalTag == line.physicalTag);
    REQUIRE(afterMiss.valid == line.valid);
    REQUIRE(afterMiss.dirty == line.dirty);
    REQUIRE(
      afterMiss.leastRecentlyFilled ==
      line.leastRecentlyFilled);
    REQUIRE(afterMiss.locked == line.locked);
    REQUIRE(afterMiss.data == line.data);

    result =
      EEMemorySystemTestAccess::maintainCache(
        &memorySystem,
        &system.eeBus(),
        {
          operation,
          UINT32_C(0x00010000),
          {EEPrivilegeMode::User, false, false}
        });

    REQUIRE(
      result.outcome ==
      EECacheMaintenanceOutcome::AddressTranslationFailure);
    REQUIRE(
      result.translation.outcome ==
      EEAddressTranslationOutcome::TLBRefillLoadOrFetch);
    REQUIRE_FALSE(result.cacheHitStatusValid);
    const EECacheLine &afterFault =
      memorySystem.dataCacheLine(set, 0);
    REQUIRE(afterFault.physicalTag == line.physicalTag);
    REQUIRE(afterFault.valid == line.valid);
    REQUIRE(afterFault.dirty == line.dirty);
    REQUIRE(afterFault.data == line.data);
  }
}

TEST_CASE("EE data-cache hit writeback failure preserves the line")
{
  NekoSystem system;
  EEMemorySystem &memorySystem = system.eeMemorySystem();
  constexpr std::uint32_t virtualAddress =
    EEMemoryMap::KSEG0_BASE +
    EEMemoryMap::MAIN_MEMORY_SIZE;
  constexpr std::size_t set =
    (virtualAddress >> 6) &
    (EEMemorySystem::DATA_CACHE_SET_COUNT - 1);
  EECacheLine line;
  line.physicalTag = EEMemoryMap::MAIN_MEMORY_SIZE;
  line.valid = true;
  line.dirty = true;
  line.leastRecentlyFilled = true;
  line.locked = true;
  line.data.fill(0x83);
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    set,
    0,
    line);

  const EECacheMaintenanceResult result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      &system.eeBus(),
      {
        EECacheOperation::DataHitWriteBackInvalidate,
        virtualAddress,
        {}
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::PhysicalBusError);
  REQUIRE(result.cacheHitStatusValid);
  REQUIRE(result.cacheHit);
  const EECacheLine &preserved =
    memorySystem.dataCacheLine(set, 0);
  REQUIRE(preserved.physicalTag == line.physicalTag);
  REQUIRE(preserved.valid == line.valid);
  REQUIRE(preserved.dirty == line.dirty);
  REQUIRE(
    preserved.leastRecentlyFilled ==
    line.leastRecentlyFilled);
  REQUIRE(preserved.locked == line.locked);
  REQUIRE(preserved.data == line.data);
}

TEST_CASE("EE instruction-cache index tag operations transfer TagLo state")
{
  EEMemorySystem memorySystem;
  constexpr std::size_t set = 0x35;
  constexpr std::size_t way = 1;
  constexpr std::uint32_t address =
    UINT32_C(0xa0000000) |
    static_cast<std::uint32_t>(set << 6) |
    UINT32_C(1);
  EECacheLine line;
  line.physicalTag = UINT32_C(0x12345000);
  line.valid = true;
  line.dirty = true;
  line.leastRecentlyFilled = true;
  line.locked = true;
  line.data[7] = 0xa5;
  EEMemorySystemTestAccess::setInstructionCacheLine(
    &memorySystem,
    set,
    way,
    line);
  EECacheLine otherWay;
  otherWay.physicalTag = UINT32_C(0x77777000);
  otherWay.data[7] = 0x3c;
  EEMemorySystemTestAccess::setInstructionCacheLine(
    &memorySystem,
    set,
    0,
    otherWay);
  memorySystem.setCOP0Register(
    EECOP0Register::TagHi,
    UINT32_C(0x89abcdef));

  EECacheMaintenanceResult result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      nullptr,
      {
        EECacheOperation::InstructionIndexLoadTag,
        address,
        {}
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::Completed);
  REQUIRE_FALSE(result.cacheHitStatusValid);
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::TagLo) ==
    (line.physicalTag |
     EECOP0TagLo::VALID |
     EECOP0TagLo::LEAST_RECENTLY_FILLED));
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::TagHi) ==
    UINT32_C(0x89abcdef));
  REQUIRE(
    memorySystem.instructionCacheLine(set, way).data[7] ==
    0xa5);

  memorySystem.setCOP0Register(
    EECOP0Register::TagLo,
    UINT32_C(0xabcde000) |
      EECOP0TagLo::DIRTY |
      EECOP0TagLo::VALID |
      EECOP0TagLo::LEAST_RECENTLY_FILLED |
      EECOP0TagLo::LOCK);
  result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      nullptr,
      {
        EECacheOperation::InstructionIndexStoreTag,
        address,
        {}
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::Completed);
  REQUIRE_FALSE(result.cacheHitStatusValid);
  const EECacheLine stored =
    memorySystem.instructionCacheLine(set, way);
  REQUIRE(stored.physicalTag == UINT32_C(0xabcde000));
  REQUIRE(stored.valid);
  REQUIRE_FALSE(stored.dirty);
  REQUIRE(stored.leastRecentlyFilled);
  REQUIRE_FALSE(stored.locked);
  REQUIRE(stored.data[7] == 0xa5);
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::TagHi) ==
    UINT32_C(0x89abcdef));
  const EECacheLine &unchangedOtherWay =
    memorySystem.instructionCacheLine(set, 0);
  REQUIRE(
    unchangedOtherWay.physicalTag ==
    otherWay.physicalTag);
  REQUIRE(unchangedOtherWay.valid == otherWay.valid);
  REQUIRE(unchangedOtherWay.data[7] == otherWay.data[7]);

  memorySystem.setCOP0Register(
    EECOP0Register::TagLo,
    UINT32_C(0x2468a000) | EECOP0TagLo::VALID);
  result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      nullptr,
      {
        EECacheOperation::InstructionIndexStoreTag,
        address & ~UINT32_C(1),
        {}
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::Completed);
  REQUIRE(
    memorySystem.instructionCacheLine(set, 0).physicalTag ==
    UINT32_C(0x2468a000));
  REQUIRE(
    memorySystem.instructionCacheLine(set, 0).valid);
  const EECacheLine &unchangedWayOne =
    memorySystem.instructionCacheLine(set, way);
  REQUIRE(
    unchangedWayOne.physicalTag ==
    stored.physicalTag);
  REQUIRE(unchangedWayOne.valid == stored.valid);
  REQUIRE(
    unchangedWayOne.leastRecentlyFilled ==
    stored.leastRecentlyFilled);
  REQUIRE(unchangedWayOne.data[7] == stored.data[7]);
}

TEST_CASE("EE instruction-cache index data operations transfer one word")
{
  EEMemorySystem memorySystem;
  constexpr std::size_t sourceSet = 0x12;
  constexpr std::size_t targetSet = 0x67;
  constexpr std::size_t way = 1;
  constexpr std::size_t wordOffset = 0x2c;
  EECacheLine source;
  source.physicalTag = UINT32_C(0x11111000);
  source.valid = true;
  source.leastRecentlyFilled = true;
  source.data[wordOffset] = 0xef;
  source.data[wordOffset + 1] = 0xbe;
  source.data[wordOffset + 2] = 0xad;
  source.data[wordOffset + 3] = 0xde;
  EEMemorySystemTestAccess::setInstructionCacheLine(
    &memorySystem,
    sourceSet,
    way,
    source);
  EECacheLine target;
  target.physicalTag = UINT32_C(0x22222000);
  target.valid = true;
  target.leastRecentlyFilled = false;
  target.data.fill(0x5a);
  EEMemorySystemTestAccess::setInstructionCacheLine(
    &memorySystem,
    targetSet,
    way,
    target);
  memorySystem.setCOP0Register(
    EECOP0Register::TagHi,
    UINT32_C(0x76543210));

  const std::uint32_t sourceAddress =
    UINT32_C(0xb0000000) |
    static_cast<std::uint32_t>(sourceSet << 6) |
    wordOffset |
    UINT32_C(3);
  EECacheMaintenanceResult result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      nullptr,
      {
        EECacheOperation::InstructionIndexLoadData,
        sourceAddress,
        {}
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::Completed);
  REQUIRE_FALSE(result.cacheHitStatusValid);
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::TagLo) ==
    UINT32_C(0xdeadbeef));

  const std::uint32_t targetAddress =
    UINT32_C(0xc0000000) |
    static_cast<std::uint32_t>(targetSet << 6) |
    wordOffset |
    UINT32_C(1);
  result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      nullptr,
      {
        EECacheOperation::InstructionIndexStoreData,
        targetAddress,
        {}
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::Completed);
  REQUIRE_FALSE(result.cacheHitStatusValid);
  const EECacheLine &stored =
    memorySystem.instructionCacheLine(targetSet, way);
  REQUIRE(stored.data[wordOffset] == 0xef);
  REQUIRE(stored.data[wordOffset + 1] == 0xbe);
  REQUIRE(stored.data[wordOffset + 2] == 0xad);
  REQUIRE(stored.data[wordOffset + 3] == 0xde);
  REQUIRE(stored.data[wordOffset - 1] == 0x5a);
  REQUIRE(stored.data[wordOffset + 4] == 0x5a);
  REQUIRE(stored.physicalTag == target.physicalTag);
  REQUIRE(stored.valid == target.valid);
  REQUIRE(
    stored.leastRecentlyFilled ==
    target.leastRecentlyFilled);
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::TagHi) ==
    UINT32_C(0x76543210));
}

TEST_CASE("EE data-cache index tag operations transfer TagLo state")
{
  EEMemorySystem memorySystem;
  constexpr std::size_t set = 0x25;
  constexpr std::size_t way = 1;
  constexpr std::uint32_t address =
    UINT32_C(0x00010000) |
    static_cast<std::uint32_t>(set << 6) |
    UINT32_C(1);
  const EEAddressTranslationContext userContext{
    EEPrivilegeMode::User,
    false,
    false
  };
  EECacheLine line;
  line.physicalTag = UINT32_C(0x12345000);
  line.valid = true;
  line.dirty = true;
  line.leastRecentlyFilled = true;
  line.locked = true;
  line.data[7] = 0xa5;
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    set,
    way,
    line);
  EECacheLine otherWay;
  otherWay.physicalTag = UINT32_C(0x77777000);
  otherWay.valid = true;
  otherWay.data[7] = 0x3c;
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    set,
    0,
    otherWay);
  memorySystem.setCOP0Register(
    EECOP0Register::TagHi,
    UINT32_C(0x89abcdef));

  EECacheMaintenanceResult result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      nullptr,
      {
        EECacheOperation::DataIndexLoadTag,
        address,
        userContext
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::Completed);
  REQUIRE_FALSE(result.cacheHitStatusValid);
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::TagLo) ==
    (line.physicalTag |
     EECOP0TagLo::DIRTY |
     EECOP0TagLo::VALID |
     EECOP0TagLo::LEAST_RECENTLY_FILLED |
     EECOP0TagLo::LOCK));
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::TagHi) ==
    UINT32_C(0x89abcdef));
  REQUIRE(
    memorySystem.dataCacheLine(set, way).data[7] ==
    0xa5);

  memorySystem.setCOP0Register(
    EECOP0Register::TagLo,
    UINT32_C(0xabcde000) |
      EECOP0TagLo::VALID |
      EECOP0TagLo::LOCK);
  result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      nullptr,
      {
        EECacheOperation::DataIndexStoreTag,
        address,
        userContext
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::Completed);
  REQUIRE_FALSE(result.cacheHitStatusValid);
  const EECacheLine stored =
    memorySystem.dataCacheLine(set, way);
  REQUIRE(stored.physicalTag == UINT32_C(0xabcde000));
  REQUIRE(stored.valid);
  REQUIRE_FALSE(stored.dirty);
  REQUIRE_FALSE(stored.leastRecentlyFilled);
  REQUIRE(stored.locked);
  REQUIRE(stored.data[7] == 0xa5);
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::TagHi) ==
    UINT32_C(0x89abcdef));
  const EECacheLine &unchangedOtherWay =
    memorySystem.dataCacheLine(set, 0);
  REQUIRE(
    unchangedOtherWay.physicalTag ==
    otherWay.physicalTag);
  REQUIRE(unchangedOtherWay.valid);
  REQUIRE(unchangedOtherWay.data[7] == 0x3c);

  memorySystem.setCOP0Register(
    EECOP0Register::TagLo,
    UINT32_C(0x2468a000) |
      EECOP0TagLo::DIRTY |
      EECOP0TagLo::LEAST_RECENTLY_FILLED);
  result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      nullptr,
      {
        EECacheOperation::DataIndexStoreTag,
        address & ~UINT32_C(1),
        userContext
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::Completed);
  const EECacheLine &storedWayZero =
    memorySystem.dataCacheLine(set, 0);
  REQUIRE(
    storedWayZero.physicalTag ==
    UINT32_C(0x2468a000));
  REQUIRE_FALSE(storedWayZero.valid);
  REQUIRE(storedWayZero.dirty);
  REQUIRE(storedWayZero.leastRecentlyFilled);
  REQUIRE_FALSE(storedWayZero.locked);
  REQUIRE(storedWayZero.data[7] == 0x3c);
  const EECacheLine &unchangedWayOne =
    memorySystem.dataCacheLine(set, way);
  REQUIRE(
    unchangedWayOne.physicalTag ==
    stored.physicalTag);
  REQUIRE(unchangedWayOne.valid == stored.valid);
  REQUIRE(unchangedWayOne.dirty == stored.dirty);
  REQUIRE(
    unchangedWayOne.leastRecentlyFilled ==
    stored.leastRecentlyFilled);
  REQUIRE(unchangedWayOne.locked == stored.locked);
  REQUIRE(unchangedWayOne.data[7] == stored.data[7]);
}

TEST_CASE("EE data-cache index data operations transfer one word")
{
  EEMemorySystem memorySystem;
  constexpr std::size_t sourceSet = 0x12;
  constexpr std::size_t targetSet = 0x37;
  constexpr std::size_t way = 1;
  constexpr std::size_t wordOffset = 0x2c;
  EECacheLine source;
  source.physicalTag = UINT32_C(0x11111000);
  source.valid = true;
  source.dirty = true;
  source.leastRecentlyFilled = true;
  source.locked = true;
  source.data[wordOffset] = 0xef;
  source.data[wordOffset + 1] = 0xbe;
  source.data[wordOffset + 2] = 0xad;
  source.data[wordOffset + 3] = 0xde;
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    sourceSet,
    way,
    source);
  EECacheLine target;
  target.physicalTag = UINT32_C(0x22222000);
  target.valid = true;
  target.dirty = true;
  target.leastRecentlyFilled = false;
  target.locked = true;
  target.data.fill(0x5a);
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    targetSet,
    way,
    target);
  memorySystem.setCOP0Register(
    EECOP0Register::TagHi,
    UINT32_C(0x76543210));

  const std::uint32_t sourceAddress =
    static_cast<std::uint32_t>(sourceSet << 6) |
    wordOffset |
    UINT32_C(3);
  EECacheMaintenanceResult result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      nullptr,
      {
        EECacheOperation::DataIndexLoadData,
        sourceAddress,
        {EEPrivilegeMode::User, false, false}
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::Completed);
  REQUIRE_FALSE(result.cacheHitStatusValid);
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::TagLo) ==
    UINT32_C(0xdeadbeef));

  const std::uint32_t targetAddress =
    static_cast<std::uint32_t>(targetSet << 6) |
    wordOffset |
    UINT32_C(1);
  result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      nullptr,
      {
        EECacheOperation::DataIndexStoreData,
        targetAddress,
        {EEPrivilegeMode::User, false, false}
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::Completed);
  REQUIRE_FALSE(result.cacheHitStatusValid);
  const EECacheLine &stored =
    memorySystem.dataCacheLine(targetSet, way);
  REQUIRE(stored.data[wordOffset] == 0xef);
  REQUIRE(stored.data[wordOffset + 1] == 0xbe);
  REQUIRE(stored.data[wordOffset + 2] == 0xad);
  REQUIRE(stored.data[wordOffset + 3] == 0xde);
  REQUIRE(stored.data[wordOffset - 1] == 0x5a);
  REQUIRE(stored.data[wordOffset + 4] == 0x5a);
  REQUIRE(stored.physicalTag == target.physicalTag);
  REQUIRE(stored.valid == target.valid);
  REQUIRE(stored.dirty == target.dirty);
  REQUIRE(
    stored.leastRecentlyFilled ==
    target.leastRecentlyFilled);
  REQUIRE(stored.locked == target.locked);
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::TagHi) ==
    UINT32_C(0x76543210));
}

TEST_CASE("EE cache index state survives save-state round trips")
{
  NekoSystem original;
  EEMemorySystem &memorySystem = original.eeMemorySystem();
  constexpr std::size_t sourceSet = 3;
  constexpr std::size_t targetSet = 5;
  constexpr std::size_t targetWay = 1;
  EECacheLine source;
  source.valid = true;
  source.data[0] = 0xef;
  source.data[1] = 0xbe;
  source.data[2] = 0xad;
  source.data[3] = 0xde;
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    sourceSet,
    0,
    source);
  EEMemorySystemTestAccess::maintainCache(
    &memorySystem,
    nullptr,
    {
      EECacheOperation::DataIndexLoadData,
      static_cast<std::uint32_t>(sourceSet << 6),
      {}
    });
  EEMemorySystemTestAccess::maintainCache(
    &memorySystem,
    nullptr,
    {
      EECacheOperation::DataIndexStoreData,
      static_cast<std::uint32_t>(targetSet << 6) |
        static_cast<std::uint32_t>(targetWay),
      {}
    });
  const std::vector<std::uint8_t> rawTagState =
    original.saveState();
  NekoSystem rawTagRestored;
  REQUIRE_NOTHROW(rawTagRestored.loadState(rawTagState));
  REQUIRE(
    rawTagRestored.eeMemorySystem().cop0Register(
      EECOP0Register::TagLo) ==
    UINT32_C(0xdeadbeef));
  REQUIRE(rawTagRestored.saveState() == rawTagState);
  REQUIRE(
    rawTagRestored.eeCore().stateHash() ==
    original.eeCore().stateHash());

  memorySystem.setCOP0Register(
    EECOP0Register::TagLo,
    UINT32_C(0x12345000) |
      EECOP0TagLo::DIRTY |
      EECOP0TagLo::LEAST_RECENTLY_FILLED |
      EECOP0TagLo::LOCK);
  EEMemorySystemTestAccess::maintainCache(
    &memorySystem,
    nullptr,
    {
      EECacheOperation::DataIndexStoreTag,
      static_cast<std::uint32_t>(targetSet << 6) |
        static_cast<std::uint32_t>(targetWay),
      {}
    });

  const EECacheLine &originalLine =
    memorySystem.dataCacheLine(targetSet, targetWay);
  REQUIRE_FALSE(originalLine.valid);
  REQUIRE(originalLine.dirty);
  REQUIRE(originalLine.locked);
  REQUIRE(originalLine.data[0] == 0xef);

  const std::vector<std::uint8_t> state = original.saveState();
  NekoSystem restored;
  REQUIRE_NOTHROW(restored.loadState(state));
  const EECacheLine &restoredLine =
    restored.eeMemorySystem().dataCacheLine(
      targetSet,
      targetWay);
  REQUIRE(
    restoredLine.physicalTag ==
    originalLine.physicalTag);
  REQUIRE(restoredLine.valid == originalLine.valid);
  REQUIRE(restoredLine.dirty == originalLine.dirty);
  REQUIRE(
    restoredLine.leastRecentlyFilled ==
    originalLine.leastRecentlyFilled);
  REQUIRE(restoredLine.locked == originalLine.locked);
  REQUIRE(restoredLine.data == originalLine.data);
  REQUIRE(restored.saveState() == state);
  REQUIRE(
    restored.eeCore().stateHash() ==
    original.eeCore().stateHash());
}

TEST_CASE("EE instruction-cache index invalidate clears only the selected line")
{
  EEMemorySystem memorySystem;
  constexpr std::size_t set = 0x2a;
  constexpr std::size_t way = 1;
  EECacheLine selected;
  selected.physicalTag = UINT32_C(0x12345000);
  selected.valid = true;
  selected.leastRecentlyFilled = true;
  selected.data.fill(0xa5);
  EEMemorySystemTestAccess::setInstructionCacheLine(
    &memorySystem,
    set,
    way,
    selected);
  EECacheLine other;
  other.physicalTag = UINT32_C(0x6789a000);
  other.valid = true;
  other.data[3] = 0x5a;
  EEMemorySystemTestAccess::setInstructionCacheLine(
    &memorySystem,
    set,
    0,
    other);

  const EECacheMaintenanceResult result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      nullptr,
      {
        EECacheOperation::InstructionIndexInvalidate,
        static_cast<std::uint32_t>(set << 6) |
          static_cast<std::uint32_t>(way),
        {}
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::Completed);
  REQUIRE_FALSE(result.cacheHitStatusValid);
  const EECacheLine &invalidated =
    memorySystem.instructionCacheLine(set, way);
  REQUIRE_FALSE(invalidated.valid);
  REQUIRE(invalidated.physicalTag == 0);
  REQUIRE(invalidated.leastRecentlyFilled);
  for (const std::uint8_t byte : invalidated.data)
  {
    REQUIRE(byte == 0);
  }
  const EECacheLine &unchanged =
    memorySystem.instructionCacheLine(set, 0);
  REQUIRE(unchanged.physicalTag == other.physicalTag);
  REQUIRE(unchanged.valid);
  REQUIRE(unchanged.data[3] == other.data[3]);
}

TEST_CASE("EE instruction-cache fill translates and installs atomically")
{
  NekoSystem system;
  EEMemorySystem &memorySystem = system.eeMemorySystem();
  constexpr std::uint32_t virtualAddress =
    UINT32_C(0x8000126c);
  constexpr std::size_t set =
    (virtualAddress >> 6) &
    (EEMemorySystem::INSTRUCTION_CACHE_SET_COUNT - 1);
  EECacheLine way0;
  way0.physicalTag = UINT32_C(0x4000);
  way0.valid = true;
  way0.leastRecentlyFilled = true;
  way0.data.fill(0x11);
  EEMemorySystemTestAccess::setInstructionCacheLine(
    &memorySystem,
    set,
    0,
    way0);
  EECacheLine way1;
  way1.physicalTag = UINT32_C(0x5000);
  way1.valid = true;
  way1.leastRecentlyFilled = true;
  way1.data.fill(0x22);
  EEMemorySystemTestAccess::setInstructionCacheLine(
    &memorySystem,
    set,
    1,
    way1);
  for (std::uint32_t offset = 0; offset < 64; ++offset)
  {
    system.eeBus().write8(
      UINT32_C(0x1240) + offset,
      static_cast<std::uint8_t>(offset ^ 0xc3));
  }

  EECacheMaintenanceResult result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      &system.eeBus(),
      {
        EECacheOperation::InstructionFill,
        virtualAddress,
        {}
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::Completed);
  REQUIRE(
    result.translation.outcome ==
    EEAddressTranslationOutcome::Translated);
  REQUIRE(
    result.translation.physicalAddress ==
    UINT32_C(0x126c));
  REQUIRE_FALSE(result.cacheHitStatusValid);
  const EECacheLine &filled =
    memorySystem.instructionCacheLine(set, 0);
  REQUIRE(filled.physicalTag == UINT32_C(0x1000));
  REQUIRE(filled.valid);
  REQUIRE_FALSE(filled.leastRecentlyFilled);
  for (std::uint32_t offset = 0; offset < 64; ++offset)
  {
    REQUIRE(
      filled.data[offset] ==
      static_cast<std::uint8_t>(offset ^ 0xc3));
  }
  REQUIRE(
    memorySystem.instructionCacheLine(set, 1).physicalTag ==
    way1.physicalTag);

  const EECacheLine beforeFailure =
    memorySystem.instructionCacheLine(set, 0);
  result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      &system.eeBus(),
      {
        EECacheOperation::InstructionFill,
        EEMemoryMap::KSEG0_BASE +
          EEMemoryMap::MAIN_MEMORY_SIZE +
          static_cast<std::uint32_t>(set << 6),
        {}
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::PhysicalBusError);
  const EECacheLine &preserved =
    memorySystem.instructionCacheLine(set, 0);
  REQUIRE(
    preserved.physicalTag ==
    beforeFailure.physicalTag);
  REQUIRE(preserved.valid == beforeFailure.valid);
  REQUIRE(
    preserved.leastRecentlyFilled ==
    beforeFailure.leastRecentlyFilled);
  REQUIRE(preserved.data == beforeFailure.data);
}

TEST_CASE("EE instruction-cache hit invalidate reports hit and miss")
{
  EEMemorySystem memorySystem;
  constexpr std::uint32_t virtualAddress = UINT32_C(0x80003210);
  constexpr std::size_t set =
    (virtualAddress >> 6) &
    (EEMemorySystem::INSTRUCTION_CACHE_SET_COUNT - 1);
  EECacheLine matching;
  matching.physicalTag = UINT32_C(0x3000);
  matching.valid = true;
  matching.leastRecentlyFilled = true;
  matching.data.fill(0x7e);
  EEMemorySystemTestAccess::setInstructionCacheLine(
    &memorySystem,
    set,
    1,
    matching);
  EECacheLine other;
  other.physicalTag = UINT32_C(0x9000);
  other.valid = true;
  other.data[9] = 0x4d;
  EEMemorySystemTestAccess::setInstructionCacheLine(
    &memorySystem,
    set,
    0,
    other);

  EECacheMaintenanceResult result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      nullptr,
      {
        EECacheOperation::InstructionHitInvalidate,
        virtualAddress,
        {}
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::Completed);
  REQUIRE(result.cacheHitStatusValid);
  REQUIRE(result.cacheHit);
  const EECacheLine &invalidated =
    memorySystem.instructionCacheLine(set, 1);
  REQUIRE_FALSE(invalidated.valid);
  REQUIRE(invalidated.physicalTag == 0);
  REQUIRE(invalidated.leastRecentlyFilled);
  REQUIRE(
    memorySystem.instructionCacheLine(set, 0).physicalTag ==
    other.physicalTag);

  result =
    EEMemorySystemTestAccess::maintainCache(
      &memorySystem,
      nullptr,
      {
        EECacheOperation::InstructionHitInvalidate,
        virtualAddress,
        {}
      });

  REQUIRE(
    result.outcome ==
    EECacheMaintenanceOutcome::Completed);
  REQUIRE(result.cacheHitStatusValid);
  REQUIRE_FALSE(result.cacheHit);
  REQUIRE(
    memorySystem.instructionCacheLine(set, 0).data[9] ==
    other.data[9]);
}

TEST_CASE("EE instruction-cache addressed operations preserve faults")
{
  EEMemorySystem memorySystem;
  EECacheLine line;
  line.physicalTag = UINT32_C(0x1000);
  line.valid = true;
  line.data[0] = 0xa5;
  EEMemorySystemTestAccess::setInstructionCacheLine(
    &memorySystem,
    0,
    0,
    line);
  const EEAddressTranslationContext userContext{
    EEPrivilegeMode::User,
    false,
    false
  };

  for (const EECacheOperation operation : {
         EECacheOperation::InstructionFill,
         EECacheOperation::InstructionHitInvalidate})
  {
    const EECacheMaintenanceResult result =
      EEMemorySystemTestAccess::maintainCache(
        &memorySystem,
        nullptr,
        {
          operation,
          UINT32_C(0x00010000),
          userContext
        });

    REQUIRE(
      result.outcome ==
      EECacheMaintenanceOutcome::AddressTranslationFailure);
    REQUIRE(
      result.translation.outcome ==
      EEAddressTranslationOutcome::TLBRefillLoadOrFetch);
    REQUIRE_FALSE(result.cacheHitStatusValid);
    REQUIRE(memorySystem.instructionCacheLine(0, 0).valid);
    REQUIRE(
      memorySystem.instructionCacheLine(0, 0).data[0] ==
      0xa5);
  }
}

TEST_CASE("EE cache inspection rejects invalid sets and ways")
{
  EEMemorySystem memorySystem;

  REQUIRE_THROWS_AS(
    memorySystem.instructionCacheLine(
      EEMemorySystem::INSTRUCTION_CACHE_SET_COUNT,
      0),
    std::out_of_range);
  REQUIRE_THROWS_AS(
    memorySystem.instructionCacheLine(
      0,
      EEMemorySystem::CACHE_WAY_COUNT),
    std::out_of_range);
  REQUIRE_THROWS_AS(
    memorySystem.dataCacheLine(
      EEMemorySystem::DATA_CACHE_SET_COUNT,
      0),
    std::out_of_range);
  REQUIRE_THROWS_AS(
    memorySystem.dataCacheLine(
      0,
      EEMemorySystem::CACHE_WAY_COUNT),
    std::out_of_range);
}

TEST_CASE("EE cache refill order starts with the missed quadword")
{
  REQUIRE(
    EEMemorySystemTestAccess::cacheLineRefillOrder(0x1000) ==
    std::array<std::uint8_t, 4>{0, 1, 2, 3});
  REQUIRE(
    EEMemorySystemTestAccess::cacheLineRefillOrder(0x1010) ==
    std::array<std::uint8_t, 4>{1, 2, 3, 0});
  REQUIRE(
    EEMemorySystemTestAccess::cacheLineRefillOrder(0x1020) ==
    std::array<std::uint8_t, 4>{2, 3, 0, 1});
  REQUIRE(
    EEMemorySystemTestAccess::cacheLineRefillOrder(0x1030) ==
    std::array<std::uint8_t, 4>{3, 0, 1, 2});
}

TEST_CASE("EE cache line fill returns a complete candidate")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  for (std::uint32_t offset = 0; offset < 64; ++offset)
  {
    system.eeBus().write8(
      UINT32_C(0x1240) + offset,
      static_cast<std::uint8_t>(offset ^ 0xa5));
  }

  const EECacheLineFillResult result =
    EEMemorySystemTestAccess::fillCacheLine(
      memorySystem,
      system.eeBus(),
      UINT32_C(0x126c));

  REQUIRE(result.outcome == EECacheLineTransferOutcome::Completed);
  REQUIRE(result.lineBaseAddress == UINT32_C(0x1240));
  REQUIRE(result.firstQuadword == 2);
  REQUIRE(result.quadwordsTransferred == 4);
  REQUIRE(result.line.physicalTag == UINT32_C(0x1000));
  REQUIRE(result.line.valid);
  REQUIRE_FALSE(result.line.dirty);
  REQUIRE_FALSE(result.line.leastRecentlyFilled);
  REQUIRE_FALSE(result.line.locked);
  for (std::uint32_t offset = 0; offset < 64; ++offset)
  {
    REQUIRE(
      result.line.data[offset] ==
      static_cast<std::uint8_t>(offset ^ 0xa5));
  }
}

TEST_CASE("EE cache line fill reports physical bus errors atomically")
{
  NekoSystem system;
  EEMemorySystem memorySystem;

  const EECacheLineFillResult result =
    EEMemorySystemTestAccess::fillCacheLine(
      memorySystem,
      system.eeBus(),
      EEMemoryMap::MAIN_MEMORY_SIZE);

  REQUIRE(
    result.outcome ==
    EECacheLineTransferOutcome::PhysicalBusError);
  REQUIRE(
    result.lineBaseAddress ==
    EEMemoryMap::MAIN_MEMORY_SIZE);
  REQUIRE(result.firstQuadword == 0);
  REQUIRE(result.quadwordsTransferred == 0);
  REQUIRE_FALSE(result.line.valid);
  for (const std::uint8_t byte : result.line.data)
  {
    REQUIRE(byte == 0);
  }
}

TEST_CASE("EE dirty cache line writeback uses its physical tag and set")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  EECacheLine line;
  line.physicalTag = UINT32_C(0x3000);
  line.valid = true;
  line.dirty = true;
  line.leastRecentlyFilled = true;
  line.locked = true;
  for (std::size_t offset = 0; offset < line.data.size(); ++offset)
  {
    line.data[offset] =
      static_cast<std::uint8_t>((offset * 3) ^ 0x5a);
  }

  const EECacheLineTransferResult result =
    EEMemorySystemTestAccess::writeBackDataCacheLine(
      memorySystem,
      &system.eeBus(),
      5,
      line);

  REQUIRE(result.outcome == EECacheLineTransferOutcome::Completed);
  REQUIRE(result.lineBaseAddress == UINT32_C(0x3140));
  REQUIRE(result.firstQuadword == 0);
  REQUIRE(result.quadwordsTransferred == 4);
  for (std::uint32_t offset = 0; offset < 64; ++offset)
  {
    std::uint8_t byte = 0;
    REQUIRE(
      system.eeBus().readData8(
        UINT32_C(0x3140) + offset,
        &byte));
    REQUIRE(
      byte ==
      static_cast<std::uint8_t>((offset * 3) ^ 0x5a));
  }
  REQUIRE(line.valid);
  REQUIRE(line.dirty);
  REQUIRE(line.leastRecentlyFilled);
  REQUIRE(line.locked);
}

TEST_CASE("EE cache writeback separates clean and invalid state errors")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  EECacheLine line;

  EECacheLineTransferResult result =
    EEMemorySystemTestAccess::writeBackDataCacheLine(
      memorySystem,
      &system.eeBus(),
      0,
      line);
  REQUIRE(result.outcome == EECacheLineTransferOutcome::Completed);
  REQUIRE(result.quadwordsTransferred == 0);

  line.valid = true;
  line.dirty = true;
  line.physicalTag = 1;
  result =
    EEMemorySystemTestAccess::writeBackDataCacheLine(
      memorySystem,
      &system.eeBus(),
      0,
      line);
  REQUIRE(
    result.outcome ==
    EECacheLineTransferOutcome::InvalidLineState);

  line.physicalTag = EEMemoryMap::MAIN_MEMORY_SIZE;
  result =
    EEMemorySystemTestAccess::writeBackDataCacheLine(
      memorySystem,
      &system.eeBus(),
      0,
      line);
  REQUIRE(
    result.outcome ==
    EECacheLineTransferOutcome::PhysicalBusError);
  REQUIRE(result.quadwordsTransferred == 0);

  REQUIRE_THROWS_AS(
    EEMemorySystemTestAccess::writeBackDataCacheLine(
      memorySystem,
      nullptr,
      0,
      line),
    std::invalid_argument);
  REQUIRE_THROWS_AS(
    EEMemorySystemTestAccess::writeBackDataCacheLine(
      memorySystem,
      &system.eeBus(),
      EEMemorySystem::DATA_CACHE_SET_COUNT,
      line),
    std::out_of_range);
}

TEST_CASE("EE instruction cache bypasses disabled and uncached accesses")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  const EEAddressTranslationResult cached =
    instructionTranslation(0x80000100, 0x100);
  const EEAddressTranslationResult uncached =
    instructionTranslation(
      0xa0000100,
      0x100,
      EECacheRoute::Uncached);

  system.eeBus().write32(0x100, UINT32_C(0x11111111));
  EEInstructionCacheFetchResult result =
    EEMemorySystemTestAccess::fetchInstruction(
      &memorySystem,
      system.eeBus(),
      cached);
  REQUIRE(
    result.outcome ==
    EEInstructionCacheFetchOutcome::Completed);
  REQUIRE(
    result.source ==
    EEInstructionCacheFetchSource::Bypassed);
  REQUIRE(result.instruction == UINT32_C(0x11111111));

  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::INSTRUCTION_CACHE_ENABLE);
  system.eeBus().write32(0x100, UINT32_C(0x22222222));
  result =
    EEMemorySystemTestAccess::fetchInstruction(
      &memorySystem,
      system.eeBus(),
      uncached);
  REQUIRE(
    result.source ==
    EEInstructionCacheFetchSource::Bypassed);
  REQUIRE(result.instruction == UINT32_C(0x22222222));

  const std::size_t set = (cached.virtualAddress >> 6) & 0x7f;
  REQUIRE_FALSE(memorySystem.instructionCacheLine(set, 0).valid);
  REQUIRE_FALSE(memorySystem.instructionCacheLine(set, 1).valid);
}

TEST_CASE("EE instruction cache refills then returns physical-tag hits")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::INSTRUCTION_CACHE_ENABLE);
  const EEAddressTranslationResult translation =
    instructionTranslation(0x8000012c, 0x12c);
  system.eeBus().write32(0x12c, UINT32_C(0x11111111));

  EEInstructionCacheFetchResult result =
    EEMemorySystemTestAccess::fetchInstruction(
      &memorySystem,
      system.eeBus(),
      translation);
  REQUIRE(
    result.outcome ==
    EEInstructionCacheFetchOutcome::Completed);
  REQUIRE(
    result.source ==
    EEInstructionCacheFetchSource::Refilled);
  REQUIRE(result.instruction == UINT32_C(0x11111111));
  REQUIRE(result.set == 4);
  REQUIRE(result.way == 0);

  system.eeBus().write32(0x12c, UINT32_C(0x22222222));
  result =
    EEMemorySystemTestAccess::fetchInstruction(
      &memorySystem,
      system.eeBus(),
      translation);
  REQUIRE(
    result.source ==
    EEInstructionCacheFetchSource::Hit);
  REQUIRE(result.instruction == UINT32_C(0x11111111));
  REQUIRE(result.set == 4);
  REQUIRE(result.way == 0);
}

TEST_CASE("EE instruction cache refill failure leaves its victim unchanged")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::INSTRUCTION_CACHE_ENABLE);
  const EEAddressTranslationResult translation =
    instructionTranslation(
      UINT32_C(0x82000000),
      EEMemoryMap::MAIN_MEMORY_SIZE);

  const EEInstructionCacheFetchResult result =
    EEMemorySystemTestAccess::fetchInstruction(
      &memorySystem,
      system.eeBus(),
      translation);

  REQUIRE(
    result.outcome ==
    EEInstructionCacheFetchOutcome::PhysicalBusError);
  REQUIRE(
    result.source ==
    EEInstructionCacheFetchSource::Refilled);
  REQUIRE_FALSE(memorySystem.instructionCacheLine(0, 0).valid);
  REQUIRE_FALSE(memorySystem.instructionCacheLine(0, 1).valid);
}

TEST_CASE("EE instruction cache prefers invalid ways then follows LRF")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::INSTRUCTION_CACHE_ENABLE);
  constexpr std::uint32_t virtualAddress = UINT32_C(0x80000100);
  constexpr std::uint32_t physicalAddresses[] = {
    UINT32_C(0x00000100),
    UINT32_C(0x00001100),
    UINT32_C(0x00002100),
    UINT32_C(0x00003100)
  };

  for (std::size_t index = 0; index < 4; ++index)
  {
    system.eeBus().write32(
      physicalAddresses[index],
      static_cast<std::uint32_t>(index + 1));
    const EEInstructionCacheFetchResult result =
      EEMemorySystemTestAccess::fetchInstruction(
        &memorySystem,
        system.eeBus(),
        instructionTranslation(
          virtualAddress,
          physicalAddresses[index]));
    REQUIRE(
      result.source ==
      EEInstructionCacheFetchSource::Refilled);
    REQUIRE(result.way == index % 2);
    REQUIRE(
      result.instruction ==
      static_cast<std::uint32_t>(index + 1));
  }

  const EECacheLine &way0 =
    memorySystem.instructionCacheLine(4, 0);
  const EECacheLine &way1 =
    memorySystem.instructionCacheLine(4, 1);
  REQUIRE(way0.physicalTag == UINT32_C(0x2000));
  REQUIRE(way1.physicalTag == UINT32_C(0x3000));
  REQUIRE_FALSE(way0.leastRecentlyFilled);
  REQUIRE_FALSE(way1.leastRecentlyFilled);
}

TEST_CASE("EE instruction cache keeps virtual aliases incoherent")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::INSTRUCTION_CACHE_ENABLE);
  const EEAddressTranslationResult firstAlias =
    instructionTranslation(0x00400100, 0x100);
  const EEAddressTranslationResult secondAlias =
    instructionTranslation(0x00401100, 0x100);

  system.eeBus().write32(0x100, UINT32_C(0x11111111));
  REQUIRE(
    EEMemorySystemTestAccess::fetchInstruction(
      &memorySystem,
      system.eeBus(),
      firstAlias).instruction ==
    UINT32_C(0x11111111));

  system.eeBus().write32(0x100, UINT32_C(0x22222222));
  REQUIRE(
    EEMemorySystemTestAccess::fetchInstruction(
      &memorySystem,
      system.eeBus(),
      secondAlias).instruction ==
    UINT32_C(0x22222222));
  REQUIRE(
    EEMemorySystemTestAccess::fetchInstruction(
      &memorySystem,
      system.eeBus(),
      firstAlias).instruction ==
    UINT32_C(0x11111111));
}

TEST_CASE("EE data cache bypasses disabled and uncached loads")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  const EEAddressTranslationResult cached =
    dataTranslation(0x80000100, 0x100);
  const EEAddressTranslationResult uncached =
    dataTranslation(
      0xa0000100,
      0x100,
      EECacheRoute::Uncached);

  system.eeBus().write32(0x100, UINT32_C(0x11111111));
  EEDataCacheLoadResult result =
    EEMemorySystemTestAccess::loadData(
      &memorySystem,
      system.eeBus(),
      cached,
      4);
  REQUIRE(result.outcome == EEDataCacheLoadOutcome::Completed);
  REQUIRE(result.source == EEDataCacheLoadSource::Bypassed);
  REQUIRE(result.width == 4);
  REQUIRE(result.data[0] == 0x11);
  REQUIRE(result.data[3] == 0x11);

  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  system.eeBus().write32(0x100, UINT32_C(0x22222222));
  result =
    EEMemorySystemTestAccess::loadData(
      &memorySystem,
      system.eeBus(),
      uncached,
      4);
  REQUIRE(result.source == EEDataCacheLoadSource::Bypassed);
  REQUIRE(result.data[0] == 0x22);
  REQUIRE(result.data[3] == 0x22);

  const std::size_t set = (cached.virtualAddress >> 6) & 0x3f;
  REQUIRE_FALSE(memorySystem.dataCacheLine(set, 0).valid);
  REQUIRE_FALSE(memorySystem.dataCacheLine(set, 1).valid);
}

TEST_CASE("EE data cache bypasses mapped devices")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  system.eeBus().writeData32(
    EEMemoryMap::INTC_MASK,
    UINT32_C(0x00000003));

  const EEDataCacheLoadResult result =
    EEMemorySystemTestAccess::loadData(
      &memorySystem,
      system.eeBus(),
      dataTranslation(
        EEMemoryMap::KSEG0_BASE + EEMemoryMap::INTC_MASK,
        EEMemoryMap::INTC_MASK),
      4);

  REQUIRE(result.outcome == EEDataCacheLoadOutcome::Completed);
  REQUIRE(result.source == EEDataCacheLoadSource::Bypassed);
  REQUIRE(result.data[0] == 0x03);
  const std::size_t set =
    ((EEMemoryMap::KSEG0_BASE + EEMemoryMap::INTC_MASK) >> 6) &
    (EEMemorySystem::DATA_CACHE_SET_COUNT - 1);
  REQUIRE_FALSE(memorySystem.dataCacheLine(set, 0).valid);
  REQUIRE_FALSE(memorySystem.dataCacheLine(set, 1).valid);
}

TEST_CASE("EE data cache refills then returns physical-tag hits")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  const EEAddressTranslationResult translation =
    dataTranslation(0x8000012c, 0x12c);
  system.eeBus().write32(0x12c, UINT32_C(0x44332211));

  EEDataCacheLoadResult result =
    EEMemorySystemTestAccess::loadData(
      &memorySystem,
      system.eeBus(),
      translation,
      4);
  REQUIRE(result.outcome == EEDataCacheLoadOutcome::Completed);
  REQUIRE(result.source == EEDataCacheLoadSource::Refilled);
  REQUIRE(result.set == 4);
  REQUIRE(result.way == 0);
  REQUIRE(result.data[0] == 0x11);
  REQUIRE(result.data[1] == 0x22);
  REQUIRE(result.data[2] == 0x33);
  REQUIRE(result.data[3] == 0x44);

  system.eeBus().write32(0x12c, UINT32_C(0x88776655));
  result =
    EEMemorySystemTestAccess::loadData(
      &memorySystem,
      system.eeBus(),
      translation,
      4);
  REQUIRE(result.source == EEDataCacheLoadSource::Hit);
  REQUIRE(result.data[0] == 0x11);
  REQUIRE(result.data[3] == 0x44);
}

TEST_CASE("EE cache boundaries continue identically after save-state restore")
{
  SECTION("A pending data-cache refill")
  {
    NekoSystem original;
    original.eeMemorySystem().setCOP0Register(
      EECOP0Register::Config,
      EECOP0Config::DATA_CACHE_ENABLE);
    const EEAddressTranslationResult translation =
      dataTranslation(0x8000012c, 0x12c);
    original.eeBus().write32(
      0x12c,
      UINT32_C(0x44332211));

    const std::vector<std::uint8_t> checkpoint =
      original.saveState();
    NekoSystem restored;
    restored.loadState(checkpoint);

    const EEDataCacheLoadResult originalResult =
      EEMemorySystemTestAccess::loadData(
        &original.eeMemorySystem(),
        original.eeBus(),
        translation,
        4);
    const EEDataCacheLoadResult restoredResult =
      EEMemorySystemTestAccess::loadData(
        &restored.eeMemorySystem(),
        restored.eeBus(),
        translation,
        4);

    REQUIRE(
      originalResult.outcome ==
      EEDataCacheLoadOutcome::Completed);
    REQUIRE(
      restoredResult.outcome ==
      originalResult.outcome);
    REQUIRE(
      restoredResult.source ==
      originalResult.source);
    REQUIRE(restoredResult.data == originalResult.data);
    REQUIRE(restored.saveState() == original.saveState());
    REQUIRE(restored.eeStateHash() == original.eeStateHash());
  }

  SECTION("A refill that evicts a dirty line")
  {
    NekoSystem original;
    original.eeMemorySystem().setCOP0Register(
      EECOP0Register::Config,
      EECOP0Config::DATA_CACHE_ENABLE);
    constexpr std::uint32_t virtualAddress =
      UINT32_C(0x80000100);
    constexpr std::uint32_t physicalAddresses[] = {
      UINT32_C(0x00000100),
      UINT32_C(0x00001100),
      UINT32_C(0x00002100)
    };

    for (std::size_t index = 0; index < 2; ++index)
    {
      const EEDataCacheStoreResult result =
        EEMemorySystemTestAccess::storeData(
          &original.eeMemorySystem(),
          &original.eeBus(),
          dataTranslation(
            virtualAddress,
            physicalAddresses[index]),
          storeBytes(index + 1),
          1);
      REQUIRE(
        result.outcome ==
        EEDataCacheStoreOutcome::Completed);
      REQUIRE_FALSE(result.evictedDirty);
    }

    const std::vector<std::uint8_t> checkpoint =
      original.saveState();
    NekoSystem restored;
    restored.loadState(checkpoint);

    const auto completeEviction =
      [virtualAddress, &physicalAddresses](
        NekoSystem *system)
      {
        return EEMemorySystemTestAccess::storeData(
          &system->eeMemorySystem(),
          &system->eeBus(),
          dataTranslation(
            virtualAddress,
            physicalAddresses[2]),
          storeBytes(3),
          1);
      };
    const EEDataCacheStoreResult originalResult =
      completeEviction(&original);
    const EEDataCacheStoreResult restoredResult =
      completeEviction(&restored);

    REQUIRE(originalResult.evictedDirty);
    REQUIRE(
      restoredResult.outcome ==
      originalResult.outcome);
    REQUIRE(
      restoredResult.source ==
      originalResult.source);
    REQUIRE(
      restoredResult.evictedDirty ==
      originalResult.evictedDirty);
    REQUIRE(
      restored.eeBus().read32(physicalAddresses[0]) ==
      original.eeBus().read32(physicalAddresses[0]));
    REQUIRE(
      restored.eeBus().read32(physicalAddresses[0]) == 1);
    REQUIRE(restored.saveState() == original.saveState());
    REQUIRE(restored.eeStateHash() == original.eeStateHash());
  }
}

TEST_CASE("EE data cache handles every aligned width at line edges")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  for (std::uint32_t offset = 0; offset < 64; ++offset)
  {
    system.eeBus().write8(
      UINT32_C(0x200) + offset,
      static_cast<std::uint8_t>(offset));
  }

  struct LoadCase
  {
    std::uint32_t offset;
    std::size_t width;
  };
  constexpr LoadCase cases[] = {
    {63, 1},
    {62, 2},
    {60, 4},
    {56, 8},
    {48, 16}
  };
  for (const LoadCase loadCase : cases)
  {
    const EEDataCacheLoadResult result =
      EEMemorySystemTestAccess::loadData(
        &memorySystem,
        system.eeBus(),
        dataTranslation(
          UINT32_C(0x80000200) + loadCase.offset,
          UINT32_C(0x200) + loadCase.offset),
        loadCase.width);
    REQUIRE(result.outcome == EEDataCacheLoadOutcome::Completed);
    REQUIRE(result.width == loadCase.width);
    for (std::size_t index = 0;
         index < loadCase.width;
         ++index)
    {
      REQUIRE(
        result.data[index] ==
        loadCase.offset + index);
    }
  }
}

TEST_CASE("EE data cache uses virtual indices with physical tags")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  const EEAddressTranslationResult firstAlias =
    dataTranslation(0x00400100, 0x100);
  const EEAddressTranslationResult secondAlias =
    dataTranslation(0x00400900, 0x100);

  system.eeBus().write32(0x100, UINT32_C(0x11111111));
  REQUIRE(
    EEMemorySystemTestAccess::loadData(
      &memorySystem,
      system.eeBus(),
      firstAlias,
      4).data[0] == 0x11);

  system.eeBus().write32(0x100, UINT32_C(0x22222222));
  REQUIRE(
    EEMemorySystemTestAccess::loadData(
      &memorySystem,
      system.eeBus(),
      secondAlias,
      4).data[0] == 0x22);
  REQUIRE(
    EEMemorySystemTestAccess::loadData(
      &memorySystem,
      system.eeBus(),
      firstAlias,
      4).data[0] == 0x11);
}

TEST_CASE("EE data cache invalid physical loads do not mutate lines")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  const EEAddressTranslationResult translation =
    dataTranslation(
      UINT32_C(0x82000000),
      EEMemoryMap::MAIN_MEMORY_SIZE);

  const EEDataCacheLoadResult result =
    EEMemorySystemTestAccess::loadData(
      &memorySystem,
      system.eeBus(),
      translation,
      4);

  REQUIRE(
    result.outcome ==
    EEDataCacheLoadOutcome::PhysicalBusError);
  REQUIRE(result.source == EEDataCacheLoadSource::Bypassed);
  REQUIRE_FALSE(memorySystem.dataCacheLine(0, 0).valid);
  REQUIRE_FALSE(memorySystem.dataCacheLine(0, 1).valid);
}

TEST_CASE("EE data cache follows invalid-way preference and LRF")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  constexpr std::uint32_t virtualAddress = UINT32_C(0x80000100);
  constexpr std::uint32_t physicalAddresses[] = {
    UINT32_C(0x00000100),
    UINT32_C(0x00001100),
    UINT32_C(0x00002100),
    UINT32_C(0x00003100)
  };

  for (std::size_t index = 0; index < 4; ++index)
  {
    system.eeBus().write32(
      physicalAddresses[index],
      static_cast<std::uint32_t>(index + 1));
    const EEDataCacheLoadResult result =
      EEMemorySystemTestAccess::loadData(
        &memorySystem,
        system.eeBus(),
        dataTranslation(
          virtualAddress,
          physicalAddresses[index]),
        4);
    REQUIRE(result.source == EEDataCacheLoadSource::Refilled);
    REQUIRE(result.way == index % 2);
  }

  const EECacheLine &way0 =
    memorySystem.dataCacheLine(4, 0);
  const EECacheLine &way1 =
    memorySystem.dataCacheLine(4, 1);
  REQUIRE(way0.physicalTag == UINT32_C(0x2000));
  REQUIRE(way1.physicalTag == UINT32_C(0x3000));
  REQUIRE_FALSE(way0.leastRecentlyFilled);
  REQUIRE_FALSE(way1.leastRecentlyFilled);
}

TEST_CASE("EE data cache excludes a single locked replacement way")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  EECacheLine locked;
  locked.physicalTag = UINT32_C(0x00000000);
  locked.valid = true;
  locked.locked = true;
  locked.data[0] = 0x11;
  EECacheLine unlocked;
  unlocked.physicalTag = UINT32_C(0x00001000);
  unlocked.valid = true;
  unlocked.dirty = true;
  unlocked.data[0] = 0x22;
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    4,
    0,
    locked);
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    4,
    1,
    unlocked);

  EEDataCacheLoadResult load =
    EEMemorySystemTestAccess::loadData(
      &memorySystem,
      system.eeBus(),
      dataTranslation(0x80000100, 0x2100),
      1);
  REQUIRE(load.outcome == EEDataCacheLoadOutcome::Completed);
  REQUIRE(load.source == EEDataCacheLoadSource::Refilled);
  REQUIRE(load.way == 1);
  REQUIRE(memorySystem.dataCacheLine(4, 0).locked);
  REQUIRE(
    memorySystem.dataCacheLine(4, 0).physicalTag ==
    UINT32_C(0x00000000));
  std::uint8_t writtenBack = 0;
  REQUIRE(system.eeBus().readData8(0x1100, &writtenBack));
  REQUIRE(writtenBack == 0x22);

  locked.physicalTag = UINT32_C(0x00003000);
  locked.leastRecentlyFilled = true;
  unlocked = {};
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    4,
    0,
    unlocked);
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    4,
    1,
    locked);

  const EEDataCacheStoreResult store =
    EEMemorySystemTestAccess::storeData(
      &memorySystem,
      &system.eeBus(),
      dataTranslation(0x80000100, 0x5100),
      storeBytes(0x5a),
      1);
  REQUIRE(store.outcome == EEDataCacheStoreOutcome::Completed);
  REQUIRE(store.source == EEDataCacheStoreSource::Allocated);
  REQUIRE(store.way == 0);
  REQUIRE(memorySystem.dataCacheLine(4, 1).locked);
  REQUIRE(
    memorySystem.dataCacheLine(4, 1).physicalTag ==
    UINT32_C(0x00003000));
}

TEST_CASE("EE cache contents participate in hashes and save states")
{
  NekoSystem original;
  const std::uint64_t initialHash =
    original.eeCore().stateHash();
  EECacheLine instructionLine;
  instructionLine.physicalTag = UINT32_C(0x12345000);
  instructionLine.valid = true;
  instructionLine.leastRecentlyFilled = true;
  instructionLine.data[7] = 0xa5;
  EEMemorySystemTestAccess::setInstructionCacheLine(
    &original.eeMemorySystem(),
    3,
    1,
    instructionLine);
  EECacheLine dataLine;
  dataLine.physicalTag = UINT32_C(0x23456000);
  dataLine.valid = true;
  dataLine.dirty = true;
  dataLine.locked = true;
  dataLine.data[11] = 0x5a;
  EEMemorySystemTestAccess::setDataCacheLine(
    &original.eeMemorySystem(),
    5,
    0,
    dataLine);

  REQUIRE(original.eeCore().stateHash() != initialHash);
  NekoSystem restored;
  restored.loadState(original.saveState());
  REQUIRE(
    restored.eeMemorySystem().instructionCacheLine(3, 1).data[7] ==
    0xa5);
  REQUIRE(
    restored.eeMemorySystem().instructionCacheLine(3, 1).physicalTag ==
    instructionLine.physicalTag);
  REQUIRE(
    restored.eeMemorySystem().instructionCacheLine(3, 1).valid);
  REQUIRE(
    restored.eeMemorySystem()
      .instructionCacheLine(3, 1).leastRecentlyFilled);
  REQUIRE(
    restored.eeMemorySystem().dataCacheLine(5, 0).data[11] ==
    0x5a);
  REQUIRE(
    restored.eeMemorySystem().dataCacheLine(5, 0).physicalTag ==
    dataLine.physicalTag);
  REQUIRE(restored.eeMemorySystem().dataCacheLine(5, 0).valid);
  REQUIRE(restored.eeMemorySystem().dataCacheLine(5, 0).dirty);
  REQUIRE(restored.eeMemorySystem().dataCacheLine(5, 0).locked);
  REQUIRE(restored.eeCore().stateHash() == original.eeCore().stateHash());
  REQUIRE(restored.saveState() == original.saveState());
}

TEST_CASE("EE data cache stores bypass disabled and uncached routes")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  const EEAddressTranslationResult cached =
    dataTranslation(0x80000100, 0x100);
  const EEAddressTranslationResult uncached =
    dataTranslation(
      0xa0000100,
      0x100,
      EECacheRoute::Uncached);

  EEDataCacheStoreResult result =
    EEMemorySystemTestAccess::storeData(
      &memorySystem,
      &system.eeBus(),
      cached,
      storeBytes(UINT32_C(0x44332211)),
      4);
  REQUIRE(result.outcome == EEDataCacheStoreOutcome::Completed);
  REQUIRE(result.source == EEDataCacheStoreSource::Bypassed);
  REQUIRE(system.eeBus().read32(0x100) == UINT32_C(0x44332211));

  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  result =
    EEMemorySystemTestAccess::storeData(
      &memorySystem,
      &system.eeBus(),
      uncached,
      storeBytes(UINT32_C(0x88776655)),
      4);
  REQUIRE(result.outcome == EEDataCacheStoreOutcome::Completed);
  REQUIRE(result.source == EEDataCacheStoreSource::Bypassed);
  REQUIRE(system.eeBus().read32(0x100) == UINT32_C(0x88776655));

  const std::size_t set = (cached.virtualAddress >> 6) & 0x3f;
  REQUIRE_FALSE(memorySystem.dataCacheLine(set, 0).valid);
  REQUIRE_FALSE(memorySystem.dataCacheLine(set, 1).valid);
}

TEST_CASE("EE data cache write allocation preserves partial line data")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  for (std::uint32_t offset = 0; offset < 64; ++offset)
  {
    system.eeBus().write8(
      UINT32_C(0x200) + offset,
      static_cast<std::uint8_t>(offset));
  }

  const EEDataCacheStoreResult result =
    EEMemorySystemTestAccess::storeData(
      &memorySystem,
      &system.eeBus(),
      dataTranslation(0x8000023c, 0x23c),
      storeBytes(UINT32_C(0xddccbbaa)),
      4);

  REQUIRE(result.outcome == EEDataCacheStoreOutcome::Completed);
  REQUIRE(result.source == EEDataCacheStoreSource::Allocated);
  REQUIRE(result.set == 8);
  REQUIRE(result.way == 0);
  REQUIRE_FALSE(result.evictedDirty);
  const EECacheLine &line = memorySystem.dataCacheLine(8, 0);
  REQUIRE(line.valid);
  REQUIRE(line.dirty);
  REQUIRE(line.data[59] == 59);
  REQUIRE(line.data[60] == 0xaa);
  REQUIRE(line.data[61] == 0xbb);
  REQUIRE(line.data[62] == 0xcc);
  REQUIRE(line.data[63] == 0xdd);
  REQUIRE(system.eeBus().read32(0x23c) == UINT32_C(0x3f3e3d3c));
}

TEST_CASE("EE data cache stores bypass mapped devices")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  system.eeBus().writeData32(
    EEMemoryMap::INTC_MASK,
    UINT32_C(0x00000003));
  const EEAddressTranslationResult translation =
    dataTranslation(
      EEMemoryMap::KSEG0_BASE + EEMemoryMap::INTC_MASK,
      EEMemoryMap::INTC_MASK);

  const EEDataCacheStoreResult result =
    EEMemorySystemTestAccess::storeData(
      &memorySystem,
      &system.eeBus(),
      translation,
      storeBytes(UINT32_C(0x00000001)),
      4);

  REQUIRE(result.outcome == EEDataCacheStoreOutcome::Completed);
  REQUIRE(result.source == EEDataCacheStoreSource::Bypassed);
  const std::size_t set =
    (translation.virtualAddress >> 6) &
    (EEMemorySystem::DATA_CACHE_SET_COUNT - 1);
  REQUIRE_FALSE(memorySystem.dataCacheLine(set, 0).valid);
  REQUIRE_FALSE(memorySystem.dataCacheLine(set, 1).valid);
}

TEST_CASE("EE data cache store hits update every supported width")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  constexpr std::size_t widths[] = {1, 2, 4, 8, 16};
  constexpr std::uint32_t offsets[] = {63, 62, 60, 56, 48};
  const std::array<std::uint8_t, 16> data =
    storeBytes(
      UINT64_C(0x8877665544332211),
      UINT64_C(0xffeeddccbbaa0099));

  for (std::size_t index = 0; index < 5; ++index)
  {
    const EEAddressTranslationResult translation =
      dataTranslation(
        UINT32_C(0x80000300) + offsets[index],
        UINT32_C(0x300) + offsets[index]);
    REQUIRE(
      EEMemorySystemTestAccess::loadData(
        &memorySystem,
        system.eeBus(),
        translation,
        widths[index]).outcome ==
      EEDataCacheLoadOutcome::Completed);
    const EEDataCacheStoreResult result =
      EEMemorySystemTestAccess::storeData(
        &memorySystem,
        &system.eeBus(),
        translation,
        data,
        widths[index]);
    REQUIRE(result.outcome == EEDataCacheStoreOutcome::Completed);
    REQUIRE(result.source == EEDataCacheStoreSource::Hit);
    const EEDataCacheLoadResult loaded =
      EEMemorySystemTestAccess::loadData(
        &memorySystem,
        system.eeBus(),
        translation,
        widths[index]);
    for (std::size_t byte = 0; byte < widths[index]; ++byte)
    {
      REQUIRE(loaded.data[byte] == data[byte]);
    }
  }
}

TEST_CASE("EE locked data-cache store hits do not set Dirty")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  EECacheLine line;
  line.physicalTag = UINT32_C(0x00000000);
  line.valid = true;
  line.locked = true;
  line.data[0] = 0x11;
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    4,
    0,
    line);

  const EEDataCacheStoreResult result =
    EEMemorySystemTestAccess::storeData(
      &memorySystem,
      &system.eeBus(),
      dataTranslation(0x80000100, 0x100),
      storeBytes(0x5a),
      1);

  REQUIRE(result.outcome == EEDataCacheStoreOutcome::Completed);
  REQUIRE(result.source == EEDataCacheStoreSource::Hit);
  REQUIRE(result.way == 0);
  REQUIRE(memorySystem.dataCacheLine(4, 0).data[0] == 0x5a);
  REQUIRE(memorySystem.dataCacheLine(4, 0).locked);
  REQUIRE_FALSE(memorySystem.dataCacheLine(4, 0).dirty);
  std::uint8_t backingValue = 0xff;
  REQUIRE(system.eeBus().readData8(0x100, &backingValue));
  REQUIRE(backingValue == 0);

  line.data[0] = 0x5a;
  line.dirty = true;
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    4,
    0,
    line);
  REQUIRE(
    EEMemorySystemTestAccess::storeData(
      &memorySystem,
      &system.eeBus(),
      dataTranslation(0x80000100, 0x100),
      storeBytes(0xa5),
      1).outcome ==
    EEDataCacheStoreOutcome::Completed);
  REQUIRE(memorySystem.dataCacheLine(4, 0).data[0] == 0xa5);
  REQUIRE(memorySystem.dataCacheLine(4, 0).dirty);
  REQUIRE(memorySystem.dataCacheLine(4, 0).locked);
}

TEST_CASE("EE all-locked data-cache misses bypass without replacement")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  EECacheLine way0;
  way0.physicalTag = UINT32_C(0x00000000);
  way0.valid = true;
  way0.locked = true;
  way0.data[0] = 0x11;
  EECacheLine way1;
  way1.physicalTag = UINT32_C(0x00001000);
  way1.valid = true;
  way1.locked = true;
  way1.data[0] = 0x22;
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    4,
    0,
    way0);
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    4,
    1,
    way1);
  system.eeBus().write8(0x2100, 0x33);

  const EEDataCacheLoadResult load =
    EEMemorySystemTestAccess::loadData(
      &memorySystem,
      system.eeBus(),
      dataTranslation(0x80000100, 0x2100),
      1);
  REQUIRE(load.outcome == EEDataCacheLoadOutcome::Completed);
  REQUIRE(load.source == EEDataCacheLoadSource::Bypassed);
  REQUIRE(load.data[0] == 0x33);

  const EEDataCacheStoreResult store =
    EEMemorySystemTestAccess::storeData(
      &memorySystem,
      &system.eeBus(),
      dataTranslation(0x80000100, 0x2100),
      storeBytes(0x44),
      1);
  REQUIRE(store.outcome == EEDataCacheStoreOutcome::Completed);
  REQUIRE(store.source == EEDataCacheStoreSource::Bypassed);
  std::uint8_t backingValue = 0;
  REQUIRE(system.eeBus().readData8(0x2100, &backingValue));
  REQUIRE(backingValue == 0x44);
  REQUIRE(
    memorySystem.dataCacheLine(4, 0).physicalTag ==
    way0.physicalTag);
  REQUIRE(memorySystem.dataCacheLine(4, 0).data[0] == 0x11);
  REQUIRE(memorySystem.dataCacheLine(4, 0).locked);
  REQUIRE(
    memorySystem.dataCacheLine(4, 1).physicalTag ==
    way1.physicalTag);
  REQUIRE(memorySystem.dataCacheLine(4, 1).data[0] == 0x22);
  REQUIRE(memorySystem.dataCacheLine(4, 1).locked);
}

TEST_CASE("EE dirty data-cache eviction writes back before replacement")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  constexpr std::uint32_t virtualAddress = UINT32_C(0x80000100);
  constexpr std::uint32_t physicalAddresses[] = {
    UINT32_C(0x00000100),
    UINT32_C(0x00001100),
    UINT32_C(0x00002100)
  };

  for (std::size_t index = 0; index < 3; ++index)
  {
    const EEDataCacheStoreResult result =
      EEMemorySystemTestAccess::storeData(
        &memorySystem,
        &system.eeBus(),
        dataTranslation(
          virtualAddress,
          physicalAddresses[index]),
        storeBytes(index + 1),
        1);
    REQUIRE(result.outcome == EEDataCacheStoreOutcome::Completed);
    REQUIRE(result.source == EEDataCacheStoreSource::Allocated);
    REQUIRE(result.way == index % 2);
    REQUIRE(result.evictedDirty == (index == 2));
  }

  std::uint8_t value = 0;
  REQUIRE(system.eeBus().readData8(physicalAddresses[0], &value));
  REQUIRE(value == 1);
  REQUIRE(system.eeBus().readData8(physicalAddresses[1], &value));
  REQUIRE(value == 0);
  REQUIRE(system.eeBus().readData8(physicalAddresses[2], &value));
  REQUIRE(value == 0);
}

TEST_CASE("EE data-cache load refill writes back a dirty victim")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  constexpr std::uint32_t virtualAddress = UINT32_C(0x80000100);
  constexpr std::uint32_t physicalAddresses[] = {
    UINT32_C(0x00000100),
    UINT32_C(0x00001100),
    UINT32_C(0x00002100)
  };

  REQUIRE(
    EEMemorySystemTestAccess::storeData(
      &memorySystem,
      &system.eeBus(),
      dataTranslation(
        virtualAddress,
        physicalAddresses[0]),
      storeBytes(0x5a),
      1).outcome ==
    EEDataCacheStoreOutcome::Completed);
  REQUIRE(
    EEMemorySystemTestAccess::loadData(
      &memorySystem,
      system.eeBus(),
      dataTranslation(
        virtualAddress,
        physicalAddresses[1]),
      1).outcome ==
    EEDataCacheLoadOutcome::Completed);
  REQUIRE(
    EEMemorySystemTestAccess::loadData(
      &memorySystem,
      system.eeBus(),
      dataTranslation(
        virtualAddress,
        physicalAddresses[2]),
      1).outcome ==
    EEDataCacheLoadOutcome::Completed);

  std::uint8_t value = 0;
  REQUIRE(system.eeBus().readData8(physicalAddresses[0], &value));
  REQUIRE(value == 0x5a);
}

TEST_CASE("EE PREF ignores unavailable prefetch targets")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  const auto prefetch =
    [&system, &memorySystem](
      std::uint32_t virtualAddress,
      std::uint8_t hint = 0,
      EEPrivilegeMode privilege = EEPrivilegeMode::Kernel)
    {
      return EEMemorySystemTestAccess::prefetchData(
        &memorySystem,
        &system.eeBus(),
        {virtualAddress, hint, context(privilege)});
    };

  EEPrefetchResult result = prefetch(UINT32_C(0x80000100));
  REQUIRE(result.outcome == EEPrefetchOutcome::Ignored);
  REQUIRE(
    result.translation.outcome ==
    EEAddressTranslationOutcome::Translated);
  REQUIRE_FALSE(memorySystem.dataCacheLine(4, 0).valid);

  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  result = prefetch(UINT32_C(0xa0000100));
  REQUIRE(result.outcome == EEPrefetchOutcome::Ignored);
  REQUIRE(
    result.translation.cacheRoute ==
    EECacheRoute::Uncached);
  REQUIRE_FALSE(memorySystem.dataCacheLine(4, 0).valid);

  result = prefetch(
    UINT32_C(0x00010000),
    31,
    EEPrivilegeMode::User);
  REQUIRE(result.outcome == EEPrefetchOutcome::Ignored);
  REQUIRE(
    result.translation.outcome ==
    EEAddressTranslationOutcome::TLBRefillLoadOrFetch);

  result = prefetch(
    UINT32_C(0x80000100),
    0,
    EEPrivilegeMode::User);
  REQUIRE(result.outcome == EEPrefetchOutcome::Ignored);
  REQUIRE(
    result.translation.outcome ==
    EEAddressTranslationOutcome::AddressErrorLoadOrFetch);

  memorySystem.setTLBEntry(
    2,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x45678000),
      {
        UINT32_C(0x00010006) |
        (UINT32_C(7) << 3)
      },
      {UINT32_C(0x0001401e)}
    });
  result = prefetch(
    UINT32_C(0x45678100),
    0,
    EEPrivilegeMode::User);
  REQUIRE(result.outcome == EEPrefetchOutcome::Ignored);
  REQUIRE(
    result.translation.cacheRoute ==
    EECacheRoute::UncachedAccelerated);

  memorySystem.setTLBEntry(
    8,
    {
      EECOP0PageMask::SIZE_16_KIB,
      UINT32_C(0x50000000),
      {EECOP0EntryLo::SCRATCHPAD | UINT32_C(0x00000006)},
      {UINT32_C(0x0007001e)}
    });
  result = prefetch(
    UINT32_C(0x50003210),
    0,
    EEPrivilegeMode::User);
  REQUIRE(result.outcome == EEPrefetchOutcome::Ignored);
  REQUIRE(result.translation.route == EEAddressRoute::Scratchpad);

  EECacheLine locked0;
  locked0.physicalTag = UINT32_C(0x00001000);
  locked0.valid = true;
  locked0.locked = true;
  EECacheLine locked1;
  locked1.physicalTag = UINT32_C(0x00002000);
  locked1.valid = true;
  locked1.locked = true;
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    4,
    0,
    locked0);
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    4,
    1,
    locked1);
  result = prefetch(UINT32_C(0x80003100));
  REQUIRE(result.outcome == EEPrefetchOutcome::Ignored);
  REQUIRE(memorySystem.dataCacheLine(4, 0).locked);
  REQUIRE(memorySystem.dataCacheLine(4, 1).locked);

  REQUIRE_THROWS_WITH(
    prefetch(UINT32_C(0x80000100), 32),
    "EE prefetch hint is outside the instruction field.");
}

TEST_CASE("EE PREF allocates clean lines and treats reserved hints identically")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  for (std::uint32_t offset = 0; offset < 64; ++offset)
  {
    REQUIRE(
      system.eeBus().writeData8(
        UINT32_C(0x100) + offset,
        static_cast<std::uint8_t>(offset ^ 0x5a)));
  }

  EEPrefetchResult result =
    EEMemorySystemTestAccess::prefetchData(
      &memorySystem,
      &system.eeBus(),
      {
        UINT32_C(0x8000012c),
        31,
        context(EEPrivilegeMode::Kernel)
      });
  REQUIRE(result.outcome == EEPrefetchOutcome::Allocated);
  REQUIRE(result.set == 4);
  REQUIRE(result.way == 0);
  REQUIRE_FALSE(result.evictedDirty);
  const EECacheLine &allocated =
    memorySystem.dataCacheLine(4, 0);
  REQUIRE(allocated.valid);
  REQUIRE_FALSE(allocated.dirty);
  REQUIRE_FALSE(allocated.locked);
  REQUIRE(allocated.leastRecentlyFilled);
  REQUIRE(allocated.physicalTag == 0);
  for (std::size_t offset = 0; offset < allocated.data.size(); ++offset)
  {
    REQUIRE(
      allocated.data[offset] ==
      static_cast<std::uint8_t>(offset ^ 0x5a));
  }

  system.eeBus().writeData8(UINT32_C(0x12c), 0xff);
  result =
    EEMemorySystemTestAccess::prefetchData(
      &memorySystem,
      &system.eeBus(),
      {
        UINT32_C(0x8000012c),
        1,
        context(EEPrivilegeMode::Kernel)
      });
  REQUIRE(result.outcome == EEPrefetchOutcome::Hit);
  REQUIRE(result.set == 4);
  REQUIRE(result.way == 0);
  REQUIRE(memorySystem.dataCacheLine(4, 0).data[0x2c] == 0x76);
}

TEST_CASE("EE PREF writes back dirty victims atomically")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  EECacheLine dirty;
  dirty.physicalTag = UINT32_C(0x00001000);
  dirty.valid = true;
  dirty.dirty = true;
  dirty.data[0] = 0x5a;
  EECacheLine other;
  other.physicalTag = UINT32_C(0x00002000);
  other.valid = true;
  other.data[0] = 0xa5;
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    4,
    0,
    dirty);
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    4,
    1,
    other);
  system.eeBus().writeData8(UINT32_C(0x3100), 0x11);

  EEPrefetchResult result =
    EEMemorySystemTestAccess::prefetchData(
      &memorySystem,
      &system.eeBus(),
      {
        UINT32_C(0x80003100),
        0,
        context(EEPrivilegeMode::Kernel)
      });
  REQUIRE(result.outcome == EEPrefetchOutcome::Allocated);
  REQUIRE(result.evictedDirty);
  REQUIRE(result.way == 0);
  std::uint8_t writtenBack = 0;
  REQUIRE(system.eeBus().readData8(0x1100, &writtenBack));
  REQUIRE(writtenBack == 0x5a);
  REQUIRE(memorySystem.dataCacheLine(4, 0).data[0] == 0x11);
  REQUIRE_FALSE(memorySystem.dataCacheLine(4, 0).dirty);

  dirty.physicalTag = 1;
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    4,
    0,
    dirty);
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    4,
    1,
    other);
  result =
    EEMemorySystemTestAccess::prefetchData(
      &memorySystem,
      &system.eeBus(),
      {
        UINT32_C(0x80003100),
        0,
        context(EEPrivilegeMode::Kernel)
      });
  REQUIRE(
    result.outcome ==
    EEPrefetchOutcome::SuppressedTransferFailure);
  REQUIRE(
    result.transferOutcome ==
    EECacheLineTransferOutcome::InvalidLineState);
  REQUIRE(result.evictedDirty);
  REQUIRE(memorySystem.dataCacheLine(4, 0).physicalTag == 1);
  REQUIRE(memorySystem.dataCacheLine(4, 0).data[0] == 0x5a);
  REQUIRE(memorySystem.dataCacheLine(4, 0).dirty);
  REQUIRE(
    memorySystem.dataCacheLine(4, 1).physicalTag ==
    UINT32_C(0x00002000));
  REQUIRE(memorySystem.dataCacheLine(4, 1).data[0] == 0xa5);
}

TEST_CASE("EE failed dirty writeback preserves data-cache ways")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  EECacheLine invalidDirty;
  invalidDirty.data[0] = 0x5a;
  invalidDirty.physicalTag = 1;
  invalidDirty.valid = true;
  invalidDirty.dirty = true;
  EECacheLine other;
  other.data[0] = 0xa5;
  other.physicalTag = UINT32_C(0x00001000);
  other.valid = true;
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    4,
    0,
    invalidDirty);
  EEMemorySystemTestAccess::setDataCacheLine(
    &memorySystem,
    4,
    1,
    other);

  const EEDataCacheStoreResult result =
    EEMemorySystemTestAccess::storeData(
      &memorySystem,
      &system.eeBus(),
      dataTranslation(0x80000100, 0x2100),
      storeBytes(0x11),
      1);

  REQUIRE(
    result.outcome ==
    EEDataCacheStoreOutcome::PhysicalBusError);
  REQUIRE(result.source == EEDataCacheStoreSource::Allocated);
  REQUIRE(result.evictedDirty);
  const EECacheLine &preservedInvalid =
    memorySystem.dataCacheLine(4, 0);
  const EECacheLine &preservedOther =
    memorySystem.dataCacheLine(4, 1);
  REQUIRE(preservedInvalid.physicalTag == 1);
  REQUIRE(preservedInvalid.data[0] == 0x5a);
  REQUIRE(preservedInvalid.valid);
  REQUIRE(preservedInvalid.dirty);
  REQUIRE(
    preservedOther.physicalTag ==
    UINT32_C(0x00001000));
  REQUIRE(preservedOther.data[0] == 0xa5);
  REQUIRE(preservedOther.valid);
  REQUIRE_FALSE(preservedOther.dirty);
}

TEST_CASE("EE data cache preserves alias and DMA incoherence")
{
  NekoSystem system;
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(
    EECOP0Register::Config,
    EECOP0Config::DATA_CACHE_ENABLE);
  const EEAddressTranslationResult firstAlias =
    dataTranslation(0x00400100, 0x100);
  const EEAddressTranslationResult secondAlias =
    dataTranslation(0x00400900, 0x100);
  system.eeBus().write32(0x100, UINT32_C(0x11111111));
  REQUIRE(
    EEMemorySystemTestAccess::loadData(
      &memorySystem,
      system.eeBus(),
      firstAlias,
      4).data[0] == 0x11);

  REQUIRE(
    EEMemorySystemTestAccess::storeData(
      &memorySystem,
      &system.eeBus(),
      secondAlias,
      storeBytes(UINT32_C(0x22222222)),
      4).outcome ==
    EEDataCacheStoreOutcome::Completed);
  REQUIRE(system.eeBus().read32(0x100) == UINT32_C(0x11111111));
  REQUIRE(
    EEMemorySystemTestAccess::loadData(
      &memorySystem,
      system.eeBus(),
      firstAlias,
      4).data[0] == 0x11);
  REQUIRE(
    EEMemorySystemTestAccess::loadData(
      &memorySystem,
      system.eeBus(),
      secondAlias,
      4).data[0] == 0x22);

  REQUIRE(
    system.eeBus().writeDMAC128(
      0x100,
      {UINT64_C(0x3333333333333333),
       UINT64_C(0x3333333333333333)}));
  REQUIRE(
    EEMemorySystemTestAccess::loadData(
      &memorySystem,
      system.eeBus(),
      secondAlias,
      4).data[0] == 0x22);
}

TEST_CASE("EE functional scratchpad policy grants CPU and DMAC access")
{
  EEScratchpadAccessPolicy policy;

  REQUIRE(
    policy.decision(EEScratchpadAccessClient::CPU) ==
    EEScratchpadAccessDecision::Granted);
  REQUIRE(
    policy.decision(EEScratchpadAccessClient::DMAC) ==
    EEScratchpadAccessDecision::Granted);
}

TEST_CASE("EE DMAC scratchpad qword access shares CPU-visible storage")
{
  EEMemorySystem memorySystem;
  const EEQuadword cpuValue = {
    UINT64_C(0x7766554433221100),
    UINT64_C(0xffeeddccbbaa9988)
  };
  const EEQuadword dmacValue = {
    UINT64_C(0x0123456789abcdef),
    UINT64_C(0xfedcba9876543210)
  };

  REQUIRE(memorySystem.writeScratchpad128(0x100, cpuValue));
  EEQuadword loaded = {};
  REQUIRE(
    memorySystem.readScratchpadDMA128(0x100, &loaded) ==
    EEScratchpadAccessResult::Completed);
  REQUIRE(loaded.low == cpuValue.low);
  REQUIRE(loaded.high == cpuValue.high);

  REQUIRE(
    memorySystem.writeScratchpadDMA128(0x100, dmacValue) ==
    EEScratchpadAccessResult::Completed);
  REQUIRE(memorySystem.readScratchpad128(0x100, &loaded));
  REQUIRE(loaded.low == dmacValue.low);
  REQUIRE(loaded.high == dmacValue.high);
}

TEST_CASE("EE DMAC scratchpad qword access enforces physical bounds")
{
  EEMemorySystem memorySystem;
  const std::uint32_t lastAddress =
    static_cast<std::uint32_t>(
      EEMemorySystem::SCRATCHPAD_SIZE - 16);
  const EEQuadword original = {
    UINT64_C(0x7766554433221100),
    UINT64_C(0xffeeddccbbaa9988)
  };
  REQUIRE(memorySystem.writeScratchpad128(0x100, original));
  REQUIRE(
    memorySystem.writeScratchpadDMA128(lastAddress, original) ==
    EEScratchpadAccessResult::Completed);
  EEQuadword boundaryValue = {};
  REQUIRE(
    memorySystem.readScratchpadDMA128(
      lastAddress,
      &boundaryValue) ==
    EEScratchpadAccessResult::Completed);
  REQUIRE(boundaryValue.low == original.low);
  REQUIRE(boundaryValue.high == original.high);

  for (const std::uint32_t address : {
         UINT32_C(0x101),
         lastAddress + 1,
         static_cast<std::uint32_t>(
           EEMemorySystem::SCRATCHPAD_SIZE),
         UINT32_C(0x10000)})
  {
    EEQuadword loaded = {
      UINT64_C(0x5a5a5a5a5a5a5a5a),
      UINT64_C(0x5a5a5a5a5a5a5a5a)
    };
    REQUIRE(
      memorySystem.readScratchpadDMA128(address, &loaded) ==
      EEScratchpadAccessResult::InvalidAddress);
    REQUIRE(loaded.low == UINT64_C(0x5a5a5a5a5a5a5a5a));
    REQUIRE(loaded.high == UINT64_C(0x5a5a5a5a5a5a5a5a));
    REQUIRE(
      memorySystem.writeScratchpadDMA128(address, {}) ==
      EEScratchpadAccessResult::InvalidAddress);
  }

  EEQuadword loaded = {};
  REQUIRE(memorySystem.readScratchpad128(0x100, &loaded));
  REQUIRE(loaded.low == original.low);
  REQUIRE(loaded.high == original.high);
  REQUIRE(memorySystem.readScratchpad128(lastAddress, &loaded));
  REQUIRE(loaded.low == original.low);
  REQUIRE(loaded.high == original.high);
  REQUIRE_THROWS_AS(
    memorySystem.readScratchpadDMA128(0x100, nullptr),
    std::invalid_argument);
}

TEST_CASE("EE segment classification follows privilege boundaries")
{
  EEMemorySystem memorySystem;

  const struct
  {
    std::uint32_t address;
    EEPrivilegeMode privilege;
    EEAddressTranslationOutcome outcome;
  } cases[] = {
    {UINT32_C(0x00000000), EEPrivilegeMode::User,
     EEAddressTranslationOutcome::TLBLookup},
    {UINT32_C(0x7fffffff), EEPrivilegeMode::User,
     EEAddressTranslationOutcome::TLBLookup},
    {UINT32_C(0x80000000), EEPrivilegeMode::User,
     EEAddressTranslationOutcome::AddressErrorLoadOrFetch},
    {UINT32_C(0xffffffff), EEPrivilegeMode::User,
     EEAddressTranslationOutcome::AddressErrorLoadOrFetch},
    {UINT32_C(0x00000000), EEPrivilegeMode::Supervisor,
     EEAddressTranslationOutcome::TLBLookup},
    {UINT32_C(0x7fffffff), EEPrivilegeMode::Supervisor,
     EEAddressTranslationOutcome::TLBLookup},
    {UINT32_C(0x80000000), EEPrivilegeMode::Supervisor,
     EEAddressTranslationOutcome::AddressErrorLoadOrFetch},
    {UINT32_C(0xbfffffff), EEPrivilegeMode::Supervisor,
     EEAddressTranslationOutcome::AddressErrorLoadOrFetch},
    {UINT32_C(0xc0000000), EEPrivilegeMode::Supervisor,
     EEAddressTranslationOutcome::TLBLookup},
    {UINT32_C(0xdfffffff), EEPrivilegeMode::Supervisor,
     EEAddressTranslationOutcome::TLBLookup},
    {UINT32_C(0xe0000000), EEPrivilegeMode::Supervisor,
     EEAddressTranslationOutcome::AddressErrorLoadOrFetch},
    {UINT32_C(0xffffffff), EEPrivilegeMode::Supervisor,
     EEAddressTranslationOutcome::AddressErrorLoadOrFetch},
    {UINT32_C(0x00000000), EEPrivilegeMode::Kernel,
     EEAddressTranslationOutcome::TLBLookup},
    {UINT32_C(0xffffffff), EEPrivilegeMode::Kernel,
     EEAddressTranslationOutcome::TLBLookup}
  };

  for (const auto &testCase : cases)
  {
    const EEAddressTranslationResult result =
      memorySystem.classifyInstructionAddress(
        testCase.address,
        context(testCase.privilege));
    REQUIRE(result.outcome == testCase.outcome);
    REQUIRE(result.virtualAddress == testCase.address);
  }
}

TEST_CASE("EE exception levels use kernel segment privilege")
{
  EEMemorySystem memorySystem;

  for (const EEPrivilegeMode privilege : {
         EEPrivilegeMode::User,
         EEPrivilegeMode::Supervisor})
  {
    requireDirectRoute(
      memorySystem.classifyInstructionAddress(
        UINT32_C(0x81234567),
        context(privilege, true, false)),
      UINT32_C(0x81234567),
      UINT32_C(0x01234567),
      EECacheRoute::CachedNoncoherent);
    requireDirectRoute(
      memorySystem.classifyInstructionAddress(
        UINT32_C(0xa1234567),
        context(privilege, false, true)),
      UINT32_C(0xa1234567),
      UINT32_C(0x01234567),
      EECacheRoute::Uncached);
  }
}

TEST_CASE("EE exception modes preserve segment routing boundaries")
{
  EEMemorySystem memorySystem;
  const EEAddressTranslationContext exl =
    context(EEPrivilegeMode::User, true, false);
  const EEAddressTranslationContext erl =
    context(EEPrivilegeMode::User, false, true);

  requireTLBRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0x00000000),
      exl),
    UINT32_C(0x00000000));
  requireTLBRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0x7fffffff),
      exl),
    UINT32_C(0x7fffffff));
  requireDirectRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0x80000000),
      exl),
    UINT32_C(0x80000000),
    UINT32_C(0x00000000),
    EECacheRoute::CachedNoncoherent);
  requireTLBRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0xc0000000),
      exl),
    UINT32_C(0xc0000000));
  requireTLBRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0xe0000000),
      exl),
    UINT32_C(0xe0000000));

  requireDirectRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0x00000000),
      erl),
    UINT32_C(0x00000000),
    UINT32_C(0x00000000),
    EECacheRoute::Uncached);
  requireDirectRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0x7fffffff),
      erl),
    UINT32_C(0x7fffffff),
    UINT32_C(0x7fffffff),
    EECacheRoute::Uncached);
  requireDirectRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0xa0000000),
      erl),
    UINT32_C(0xa0000000),
    UINT32_C(0x00000000),
    EECacheRoute::Uncached);
  requireTLBRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0xc0000000),
      erl),
    UINT32_C(0xc0000000));
  requireTLBRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0xe0000000),
      erl),
    UINT32_C(0xe0000000));
}

TEST_CASE("EE error level makes kuseg unmapped and uncached")
{
  EEMemorySystem memorySystem;

  requireDirectRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0x01234567),
      context(EEPrivilegeMode::User, false, true)),
    UINT32_C(0x01234567),
    UINT32_C(0x01234567),
    EECacheRoute::Uncached);
}

TEST_CASE("EE kernel direct segments select physical aliases and cache routes")
{
  EEMemorySystem memorySystem;
  const EEAddressTranslationContext kernel =
    context(EEPrivilegeMode::Kernel);

  requireDirectRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0x80000000),
      kernel),
    UINT32_C(0x80000000),
    UINT32_C(0x00000000),
    EECacheRoute::CachedNoncoherent);
  requireDirectRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0x9fffffff),
      kernel),
    UINT32_C(0x9fffffff),
    UINT32_C(0x1fffffff),
    EECacheRoute::CachedNoncoherent);
  requireDirectRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0xa0000000),
      kernel),
    UINT32_C(0xa0000000),
    UINT32_C(0x00000000),
    EECacheRoute::Uncached);
  requireDirectRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0xbfffffff),
      kernel),
    UINT32_C(0xbfffffff),
    UINT32_C(0x1fffffff),
    EECacheRoute::Uncached);
  requireTLBRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0xc0000000),
      kernel),
    UINT32_C(0xc0000000));
  requireTLBRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0xffffffff),
      kernel),
    UINT32_C(0xffffffff));
}

TEST_CASE("EE data segment faults retain the access direction")
{
  EEMemorySystem memorySystem;
  const EEAddressTranslationContext user =
    context(EEPrivilegeMode::User);

  const EEAddressTranslationResult load =
    memorySystem.classifyDataAddress(
      UINT32_C(0x80000000),
      EEDataAccessDirection::Load,
      user);
  REQUIRE(
    load.outcome ==
    EEAddressTranslationOutcome::AddressErrorLoadOrFetch);
  REQUIRE(load.virtualAddress == UINT32_C(0x80000000));

  const EEAddressTranslationResult store =
    memorySystem.classifyDataAddress(
      UINT32_C(0x80000000),
      EEDataAccessDirection::Store,
      user);
  REQUIRE(
    store.outcome ==
    EEAddressTranslationOutcome::AddressErrorStore);
  REQUIRE(store.virtualAddress == UINT32_C(0x80000000));
}

TEST_CASE("EE mapped application aliases require TLB translation")
{
  EEMemorySystem memorySystem;
  const EEAddressTranslationContext user =
    context(EEPrivilegeMode::User);

  requireTLBRoute(
    memorySystem.classifyDataAddress(
      UINT32_C(0x20000000),
      EEDataAccessDirection::Load,
      user),
    UINT32_C(0x20000000));
  requireTLBRoute(
    memorySystem.classifyDataAddress(
      UINT32_C(0x30000000),
      EEDataAccessDirection::Store,
      user),
    UINT32_C(0x30000000));
}

TEST_CASE("EE cache attributes classify only supported routes")
{
  const struct
  {
    std::uint8_t attribute;
    EECacheRoute route;
  } cases[] = {
    {0, EECacheRoute::Unsupported},
    {1, EECacheRoute::Unsupported},
    {2, EECacheRoute::Uncached},
    {3, EECacheRoute::CachedNoncoherent},
    {4, EECacheRoute::Unsupported},
    {5, EECacheRoute::Unsupported},
    {6, EECacheRoute::Unsupported},
    {7, EECacheRoute::UncachedAccelerated}
  };

  for (const auto &testCase : cases)
  {
    REQUIRE(
      EEMemorySystem::cacheRoute(testCase.attribute) ==
      testCase.route);
  }
}

TEST_CASE("EE segment classification does not mutate MMU state")
{
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(EECOP0Register::Index, 9);
  memorySystem.setCOP0Register(
    EECOP0Register::EntryHi,
    UINT32_C(0x1234405a));
  const EETLBEntry entry{
    EECOP0PageMask::SIZE_16_KIB,
    UINT32_C(0x1234005a),
    {UINT32_C(0x00010007)},
    {UINT32_C(0x00020007)}
  };
  memorySystem.setTLBEntry(9, entry);

  const EEAddressTranslationResult result =
    memorySystem.classifyDataAddress(
      UINT32_C(0x81234567),
      EEDataAccessDirection::Store,
      context(EEPrivilegeMode::Kernel));

  REQUIRE(result.outcome == EEAddressTranslationOutcome::Translated);
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::Index) == 9);
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::EntryHi) ==
    UINT32_C(0x1234405a));
  REQUIRE(memorySystem.tlbEntry(9) == entry);
}

TEST_CASE("EE architectural TLB translation selects ASIDs and global entries")
{
  EEMemorySystem memorySystem;
  memorySystem.setTLBEntry(
    7,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x1234402a),
      {UINT32_C(0x0001001e)},
      {UINT32_C(0x0001401e)}
    });
  memorySystem.setCOP0Register(
    EECOP0Register::EntryHi,
    UINT32_C(0x0000002b));

  const EEAddressTranslationResult wrongASID =
    memorySystem.translateInstructionAddress(
      UINT32_C(0x12344123),
      context(EEPrivilegeMode::User));
  REQUIRE(
    wrongASID.outcome ==
    EEAddressTranslationOutcome::TLBRefillLoadOrFetch);

  memorySystem.setCOP0Register(
    EECOP0Register::EntryHi,
    UINT32_C(0x0000002a));
  const EEAddressTranslationResult matchingASID =
    memorySystem.translateInstructionAddress(
      UINT32_C(0x12344123),
      context(EEPrivilegeMode::User));
  requireDirectRoute(
    matchingASID,
    UINT32_C(0x12344123),
    UINT32_C(0x00400123),
    EECacheRoute::CachedNoncoherent);

  memorySystem.setTLBEntry(
    7,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x1234402a),
      {UINT32_C(0x0001001f)},
      {UINT32_C(0x0001401f)}
    });
  memorySystem.setCOP0Register(
    EECOP0Register::EntryHi,
    UINT32_C(0x0000002b));
  const EEAddressTranslationResult global =
    memorySystem.translateInstructionAddress(
      UINT32_C(0x12344123),
      context(EEPrivilegeMode::User));
  REQUIRE(global.outcome == EEAddressTranslationOutcome::Translated);
  REQUIRE(global.physicalAddress == UINT32_C(0x00400123));

  const auto requireASIDSensitive =
    [&memorySystem]()
    {
      for (const std::uint32_t address :
           {UINT32_C(0x12344123), UINT32_C(0x12345123)})
      {
        REQUIRE(
          memorySystem.translateInstructionAddress(
            address,
            context(EEPrivilegeMode::User)).outcome ==
          EEAddressTranslationOutcome::TLBRefillLoadOrFetch);
      }
    };
  memorySystem.setTLBEntry(
    7,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x1234402a),
      {UINT32_C(0x0001001f)},
      {UINT32_C(0x0001401e)}
    });
  requireASIDSensitive();
  memorySystem.setTLBEntry(
    7,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x1234402a),
      {UINT32_C(0x0001001e)},
      {UINT32_C(0x0001401f)}
    });
  requireASIDSensitive();
}

TEST_CASE("EE architectural TLB translation uses the lowest duplicate index")
{
  EEMemorySystem memorySystem;
  const EETLBEntry higher{
    EECOP0PageMask::SIZE_4_KIB,
    UINT32_C(0x23456044),
    {UINT32_C(0x0002401f)},
    {UINT32_C(0x0002801f)}
  };
  const EETLBEntry lower{
    EECOP0PageMask::SIZE_4_KIB,
    UINT32_C(0x23456044),
    {UINT32_C(0x0001c01f)},
    {UINT32_C(0x0002001f)}
  };
  memorySystem.setTLBEntry(20, higher);
  memorySystem.setTLBEntry(3, lower);

  const EEAddressTranslationResult result =
    memorySystem.translateDataAddress(
      UINT32_C(0x23456111),
      EEDataAccessDirection::Load,
      context(EEPrivilegeMode::User));

  REQUIRE(result.outcome == EEAddressTranslationOutcome::Translated);
  REQUIRE(result.physicalAddress == UINT32_C(0x00700111));
  REQUIRE(result.tlbIndex == 3);
}

TEST_CASE("EE architectural TLB translation covers every page size")
{
  const struct
  {
    std::uint32_t pageMask;
    std::uint32_t pageSize;
  } pageSizes[] = {
    {EECOP0PageMask::SIZE_4_KIB, UINT32_C(0x00001000)},
    {EECOP0PageMask::SIZE_16_KIB, UINT32_C(0x00004000)},
    {EECOP0PageMask::SIZE_64_KIB, UINT32_C(0x00010000)},
    {EECOP0PageMask::SIZE_256_KIB, UINT32_C(0x00040000)},
    {EECOP0PageMask::SIZE_1_MIB, UINT32_C(0x00100000)},
    {EECOP0PageMask::SIZE_4_MIB, UINT32_C(0x00400000)},
    {EECOP0PageMask::SIZE_16_MIB, UINT32_C(0x01000000)}
  };

  for (const auto &pageSize : pageSizes)
  {
    EEMemorySystem memorySystem;
    const std::uint32_t virtualBase = UINT32_C(0x40000000);
    const std::uint32_t physicalBase = UINT32_C(0x10000000);
    memorySystem.setTLBEntry(
      4,
      {
        pageSize.pageMask,
        virtualBase,
        {((physicalBase >> 12) << 6) | UINT32_C(0x1e)},
        {(((physicalBase + pageSize.pageSize) >> 12) << 6) |
         UINT32_C(0x1e)}
      });

    const std::uint32_t offset = pageSize.pageSize - 1;
    const EEAddressTranslationResult even =
      memorySystem.translateDataAddress(
        virtualBase + offset,
        EEDataAccessDirection::Load,
        context(EEPrivilegeMode::User));
    REQUIRE(even.outcome == EEAddressTranslationOutcome::Translated);
    REQUIRE(even.physicalAddress == physicalBase + offset);

    const EEAddressTranslationResult odd =
      memorySystem.translateDataAddress(
        virtualBase + pageSize.pageSize + offset,
        EEDataAccessDirection::Load,
        context(EEPrivilegeMode::User));
    REQUIRE(odd.outcome == EEAddressTranslationOutcome::Translated);
    REQUIRE(
      odd.physicalAddress ==
      physicalBase + pageSize.pageSize + offset);

    const EEAddressTranslationResult outside =
      memorySystem.translateDataAddress(
        virtualBase + pageSize.pageSize * 2,
        EEDataAccessDirection::Load,
        context(EEPrivilegeMode::User));
    REQUIRE(
      outside.outcome ==
      EEAddressTranslationOutcome::TLBRefillLoadOrFetch);
  }
}

TEST_CASE("EE architectural TLB translation reports permission faults")
{
  EEMemorySystem memorySystem;
  const std::uint32_t address = UINT32_C(0x34567000);
  memorySystem.setTLBEntry(
    5,
    {
      EECOP0PageMask::SIZE_4_KIB,
      address,
      {UINT32_C(0x00040018)},
      {UINT32_C(0x00050018)}
    });

  REQUIRE(
    memorySystem.translateDataAddress(
      address,
      EEDataAccessDirection::Load,
      context(EEPrivilegeMode::User)).outcome ==
    EEAddressTranslationOutcome::TLBInvalidLoadOrFetch);
  REQUIRE(
    memorySystem.translateDataAddress(
      address,
      EEDataAccessDirection::Store,
      context(EEPrivilegeMode::User)).outcome ==
    EEAddressTranslationOutcome::TLBInvalidStore);

  memorySystem.setTLBEntry(
    5,
    {
      EECOP0PageMask::SIZE_4_KIB,
      address,
      {UINT32_C(0x0004001a)},
      {UINT32_C(0x0005001a)}
    });
  REQUIRE(
    memorySystem.translateDataAddress(
      address,
      EEDataAccessDirection::Load,
      context(EEPrivilegeMode::User)).outcome ==
    EEAddressTranslationOutcome::Translated);
  REQUIRE(
    memorySystem.translateDataAddress(
      address,
      EEDataAccessDirection::Store,
      context(EEPrivilegeMode::User)).outcome ==
    EEAddressTranslationOutcome::TLBModified);
}

TEST_CASE("EE architectural TLB translation distinguishes refill direction")
{
  EEMemorySystem memorySystem;
  const std::uint32_t address = UINT32_C(0x76543000);

  REQUIRE(
    memorySystem.translateInstructionAddress(
      address,
      context(EEPrivilegeMode::User)).outcome ==
    EEAddressTranslationOutcome::TLBRefillLoadOrFetch);
  REQUIRE(
    memorySystem.translateDataAddress(
      address,
      EEDataAccessDirection::Load,
      context(EEPrivilegeMode::User)).outcome ==
    EEAddressTranslationOutcome::TLBRefillLoadOrFetch);
  REQUIRE(
    memorySystem.translateDataAddress(
      address,
      EEDataAccessDirection::Store,
      context(EEPrivilegeMode::User)).outcome ==
    EEAddressTranslationOutcome::TLBRefillStore);
}

TEST_CASE("EE TLB translation defers exception-register mutation")
{
  EEMemorySystem memorySystem;
  constexpr std::uint32_t address = UINT32_C(0x34567abc);
  constexpr std::uint32_t initialContext = UINT32_C(0xabd23450);
  constexpr std::uint32_t initialEntryHi = UINT32_C(0x89abc05a);
  memorySystem.setCOP0Register(
    EECOP0Register::Context,
    initialContext);
  memorySystem.setCOP0Register(
    EECOP0Register::EntryHi,
    initialEntryHi);

  REQUIRE(
    memorySystem.translateInstructionAddress(
      address,
      context(EEPrivilegeMode::User)).outcome ==
    EEAddressTranslationOutcome::TLBRefillLoadOrFetch);
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::Context) ==
    initialContext);
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::EntryHi) ==
    initialEntryHi);

  memorySystem.setTLBEntry(
    0,
    {
      EECOP0PageMask::SIZE_4_KIB,
      (address & EECOP0EntryHi::VIRTUAL_PAGE_MASK) |
        (initialEntryHi & EECOP0EntryHi::ASID_MASK),
      {UINT32_C(0x0001001a)},
      {UINT32_C(0x0001001a)}
    });
  REQUIRE(
    memorySystem.translateDataAddress(
      address,
      EEDataAccessDirection::Store,
      context(EEPrivilegeMode::User)).outcome ==
    EEAddressTranslationOutcome::TLBModified);
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::Context) ==
    initialContext);
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::EntryHi) ==
    initialEntryHi);

  memorySystem.commitTLBExceptionAddress(address);

  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::Context) ==
    ((initialContext & EECOP0Context::PTE_BASE_MASK) |
     ((address >> 9) & EECOP0Context::BAD_VPN2_MASK)));
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::EntryHi) ==
    ((address & EECOP0EntryHi::VIRTUAL_PAGE_MASK) |
     (initialEntryHi & ~EECOP0EntryHi::VIRTUAL_PAGE_MASK)));
}

TEST_CASE("EE architectural TLB translation selects mapped cache routes")
{
  const struct
  {
    std::uint8_t attribute;
    EECacheRoute route;
  } cacheRoutes[] = {
    {2, EECacheRoute::Uncached},
    {3, EECacheRoute::CachedNoncoherent},
    {7, EECacheRoute::UncachedAccelerated}
  };

  for (const auto &cacheRoute : cacheRoutes)
  {
    EEMemorySystem memorySystem;
    memorySystem.setTLBEntry(
      2,
      {
        EECOP0PageMask::SIZE_4_KIB,
        UINT32_C(0x45678000),
        {
          UINT32_C(0x00010006) |
          (static_cast<std::uint32_t>(cacheRoute.attribute) << 3)
        },
        {UINT32_C(0x0001401e)}
      });

    const EEAddressTranslationResult result =
      memorySystem.translateDataAddress(
        UINT32_C(0x45678123),
        EEDataAccessDirection::Load,
        context(EEPrivilegeMode::User));
    REQUIRE(result.outcome == EEAddressTranslationOutcome::Translated);
    REQUIRE(result.cacheAttribute == cacheRoute.attribute);
    REQUIRE(result.cacheRoute == cacheRoute.route);
  }
}

TEST_CASE("EE architectural TLB translation preserves unsupported attributes")
{
  EEMemorySystem memorySystem;
  memorySystem.setTLBEntry(
    6,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x45678000),
      {UINT32_C(0x00010006)},
      {UINT32_C(0x00014006)}
    });

  const EEAddressTranslationResult result =
    memorySystem.translateDataAddress(
      UINT32_C(0x45678123),
      EEDataAccessDirection::Load,
      context(EEPrivilegeMode::User));
  REQUIRE(
    result.outcome ==
    EEAddressTranslationOutcome::UnsupportedCacheAttribute);
  REQUIRE(result.cacheAttribute == 0);
  REQUIRE(result.cacheRoute == EECacheRoute::Unsupported);
  REQUIRE(result.physicalAddress == UINT32_C(0x00400123));
}

TEST_CASE("EE architectural TLB translation selects scratchpad")
{
  EEMemorySystem memorySystem;
  memorySystem.setTLBEntry(
    8,
    {
      EECOP0PageMask::SIZE_16_KIB,
      UINT32_C(0x50000000),
      {EECOP0EntryLo::SCRATCHPAD | UINT32_C(0x00000006)},
      {UINT32_C(0x0007001e)}
    });

  const EEAddressTranslationResult result =
    memorySystem.translateDataAddress(
      UINT32_C(0x50003210),
      EEDataAccessDirection::Load,
      context(EEPrivilegeMode::User));
  REQUIRE(result.outcome == EEAddressTranslationOutcome::Translated);
  REQUIRE(result.route == EEAddressRoute::Scratchpad);
  REQUIRE(result.physicalAddress == UINT32_C(0x00003210));
  REQUIRE(result.cacheRoute == EECacheRoute::Uncached);
}

TEST_CASE("EE instruction translation rejects scratchpad mappings")
{
  EEMemorySystem memorySystem;
  memorySystem.setTLBEntry(
    8,
    {
      EECOP0PageMask::SIZE_16_KIB,
      UINT32_C(0x50000000),
      {
        EECOP0EntryLo::SCRATCHPAD |
        UINT32_C(0x0001001e)
      },
      {UINT32_C(0x0001401e)}
    });

  const EEAddressTranslationResult result =
    memorySystem.translateInstructionAddress(
      UINT32_C(0x50003210),
      context(EEPrivilegeMode::User));
  REQUIRE(
    result.outcome ==
    EEAddressTranslationOutcome::UnsupportedScratchpadInstruction);
  REQUIRE(result.route == EEAddressRoute::Scratchpad);
  REQUIRE(result.physicalAddress == UINT32_C(0x00003210));
}

TEST_CASE("EE translation rejects non-16 KiB scratchpad mappings")
{
  EEMemorySystem memorySystem;
  memorySystem.setTLBEntry(
    9,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x51000000),
      {
        EECOP0EntryLo::SCRATCHPAD |
        UINT32_C(0x0001001e)
      },
      {UINT32_C(0x0001401e)}
    });

  const EEAddressTranslationResult result =
    memorySystem.translateDataAddress(
      UINT32_C(0x51000210),
      EEDataAccessDirection::Load,
      context(EEPrivilegeMode::User));
  REQUIRE(
    result.outcome ==
    EEAddressTranslationOutcome::UnsupportedScratchpadPageSize);
  REQUIRE(result.route == EEAddressRoute::Scratchpad);
  REQUIRE(result.physicalAddress == 0);
}

TEST_CASE("EE derived translation accelerators invalidate on TLB changes")
{
  EEMemorySystem memorySystem;
  const std::uint32_t address = UINT32_C(0x60000120);
  memorySystem.setTLBEntry(
    1,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x60000000),
      {UINT32_C(0x0000401e)},
      {UINT32_C(0x0000801e)}
    });

  REQUIRE(
    memorySystem.translateInstructionAddress(
      address,
      context(EEPrivilegeMode::User)).physicalAddress ==
    UINT32_C(0x00100120));
  REQUIRE(
    memorySystem.translateDataAddress(
      address,
      EEDataAccessDirection::Load,
      context(EEPrivilegeMode::User)).physicalAddress ==
    UINT32_C(0x00100120));

  memorySystem.setTLBEntry(
    1,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x60000000),
      {UINT32_C(0x0000c01e)},
      {UINT32_C(0x0001001e)}
    });
  REQUIRE(
    memorySystem.translateInstructionAddress(
      address,
      context(EEPrivilegeMode::User)).physicalAddress ==
    UINT32_C(0x00300120));
  REQUIRE(
    memorySystem.translateDataAddress(
      address,
      EEDataAccessDirection::Load,
      context(EEPrivilegeMode::User)).physicalAddress ==
    UINT32_C(0x00300120));
}

TEST_CASE("EE derived accelerators preserve lowest-index overlapping matches")
{
  EEMemorySystem memorySystem;
  memorySystem.setTLBEntry(
    20,
    {
      EECOP0PageMask::SIZE_16_KIB,
      UINT32_C(0x70000000),
      {UINT32_C(0x0001401f)},
      {UINT32_C(0x0001801f)}
    });
  memorySystem.setTLBEntry(
    3,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x70000000),
      {UINT32_C(0x0001c01f)},
      {UINT32_C(0x0002001f)}
    });

  const EEAddressTranslationResult largeOnly =
    memorySystem.translateInstructionAddress(
      UINT32_C(0x70002100),
      context(EEPrivilegeMode::User));
  REQUIRE(largeOnly.tlbIndex == 20);

  const EEAddressTranslationResult overlapping =
    memorySystem.translateInstructionAddress(
      UINT32_C(0x70000100),
      context(EEPrivilegeMode::User));
  REQUIRE(overlapping.tlbIndex == 3);
  REQUIRE(
    overlapping.physicalAddress ==
    UINT32_C(0x00700100));
}

TEST_CASE(
  "EE instruction and data translation remain consistent beyond accelerator capacity")
{
  EEMemorySystem memorySystem;
  constexpr std::size_t mappingCount =
    EEMemorySystem::DTLB_ENTRY_COUNT + 2;
  const EEAddressTranslationContext user =
    context(EEPrivilegeMode::User);

  for (std::size_t index = 0; index < mappingCount; ++index)
  {
    const std::uint32_t virtualBase =
      UINT32_C(0x10000000) +
      static_cast<std::uint32_t>(index) * UINT32_C(0x2000);
    const std::uint32_t physicalBase =
      UINT32_C(0x00100000) +
      static_cast<std::uint32_t>(index) * UINT32_C(0x2000);
    memorySystem.setTLBEntry(
      index,
      {
        EECOP0PageMask::SIZE_4_KIB,
        virtualBase,
        {(physicalBase >> 6) | UINT32_C(0x1f)},
        {((physicalBase + UINT32_C(0x1000)) >> 6) |
         UINT32_C(0x1f)}
      });
  }

  const auto requireMapping =
    [&memorySystem, &user](std::size_t index)
    {
      const std::uint32_t virtualAddress =
        UINT32_C(0x10000120) +
        static_cast<std::uint32_t>(index) * UINT32_C(0x2000);
      const std::uint32_t physicalAddress =
        UINT32_C(0x00100120) +
        static_cast<std::uint32_t>(index) * UINT32_C(0x2000);
      const EEAddressTranslationResult instruction =
        memorySystem.translateInstructionAddress(
          virtualAddress,
          user);
      const EEAddressTranslationResult load =
        memorySystem.translateDataAddress(
          virtualAddress,
          EEDataAccessDirection::Load,
          user);
      const EEAddressTranslationResult store =
        memorySystem.translateDataAddress(
          virtualAddress,
          EEDataAccessDirection::Store,
          user);

      for (const EEAddressTranslationResult *result :
           {&instruction, &load, &store})
      {
        REQUIRE(
          result->outcome ==
          EEAddressTranslationOutcome::Translated);
        REQUIRE(result->virtualAddress == virtualAddress);
        REQUIRE(result->physicalAddress == physicalAddress);
        REQUIRE(result->tlbIndex == index);
      }
    };

  for (std::size_t index = 0; index < mappingCount; ++index)
  {
    requireMapping(index);
  }
  for (std::size_t index = mappingCount; index-- > 0;)
  {
    requireMapping(index);
  }
}
