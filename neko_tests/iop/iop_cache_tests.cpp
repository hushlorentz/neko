#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "catch.hpp"
#include "iop_bus.hpp"
#include "iop_cache.hpp"
#include "iop_core.hpp"

struct IOPCacheTestAccess
{
  static void seed(IOPCache *cache)
  {
    for (std::size_t index = 0;
         index < cache->instructionLines.size(); ++index)
    {
      auto &line = cache->instructionLines[index];
      line.physicalTag = UINT32_C(0x1fc00000);
      line.valid = (index & 1) != 0;
      for (std::size_t word = 0; word < line.words.size(); ++word)
      {
        line.words[word] = static_cast<IOPWord>(index * 4 + word + 1);
      }
    }
    for (std::size_t index = 0; index < cache->dataLines.size(); ++index)
    {
      auto &line = cache->dataLines[index];
      line.physicalTag = UINT32_C(0x00200000);
      line.valid = (index & 1) != 0;
      line.words[0] = static_cast<IOPWord>(index + 1);
    }
  }

  static void seed(IOPCore *core)
  {
    seed(&core->caches);
  }

  static void setStatus(IOPCore *core, IOPWord status)
  {
    core->cop0.status = status;
  }

  static void setGeneralRegister(IOPCore *core, std::size_t index, IOPWord value)
  {
    core->setGeneralRegister(index, value);
  }

  static void setContinuations(IOPCore *core)
  {
    core->branch = {true, core->pc - 4, 0x200};
    core->delayedResult = {IOPDelayedResultSource::Load, 7, 0x12345678};
  }
};

static_assert(std::is_final<IOPCache>::value,
  "IOP cache ownership must remain concrete.");
static_assert(std::is_trivially_copyable<IOPCache>::value,
  "IOP cache storage must remain fixed-capacity value state.");
static_assert(std::is_trivially_copyable<IOPCacheAccessPlan>::value,
  "IOP cache routing must remain allocation-free.");
static_assert(std::is_same<
    decltype(std::declval<const IOPCore &>().cache()),
    const IOPCache &>::value,
  "Core cache inspection must not expose mutation.");
static_assert(std::is_same<
    decltype(std::declval<const IOPCache &>().instructionLine(0)),
    const IOPInstructionCacheLine &>::value,
  "Instruction-cache inspection must be read-only.");
static_assert(std::is_same<
    decltype(std::declval<const IOPCache &>().dataLine(0)),
    const IOPDataCacheLine &>::value,
  "Data-cache inspection must be read-only.");

namespace
{
  void requireClear(const IOPCache &cache)
  {
    for (std::size_t index = 0;
         index < IOPCache::INSTRUCTION_LINE_COUNT; ++index)
    {
      const auto &line = cache.instructionLine(index);
      REQUIRE_FALSE(line.valid);
      REQUIRE(line.physicalTag == 0);
      for (const auto word : line.words)
      {
        REQUIRE(word == 0);
      }
    }
    for (std::size_t index = 0;
         index < IOPCache::DATA_LINE_COUNT; ++index)
    {
      const auto &line = cache.dataLine(index);
      REQUIRE_FALSE(line.valid);
      REQUIRE(line.physicalTag == 0);
      REQUIRE(line.words[0] == 0);
    }
  }

  void requireSeeded(const IOPCache &cache)
  {
    for (std::size_t index = 0;
         index < IOPCache::INSTRUCTION_LINE_COUNT; ++index)
    {
      const auto &line = cache.instructionLine(index);
      REQUIRE(line.valid == ((index & 1) != 0));
      REQUIRE(line.physicalTag == UINT32_C(0x1fc00000));
      for (std::size_t word = 0; word < line.words.size(); ++word)
      {
        REQUIRE(line.words[word] == index * 4 + word + 1);
      }
    }
    for (std::size_t index = 0;
         index < IOPCache::DATA_LINE_COUNT; ++index)
    {
      const auto &line = cache.dataLine(index);
      REQUIRE(line.valid == ((index & 1) != 0));
      REQUIRE(line.physicalTag == UINT32_C(0x00200000));
      REQUIRE(line.words[0] == index + 1);
    }
  }

  std::shared_ptr<const BootROMImage> makeRom(std::uint8_t firstByte)
  {
    std::vector<std::uint8_t> bytes(IOPBus::ROM_SIZE, 0);
    bytes[0] = firstByte;
    return BootROMImage::create(bytes);
  }
}

TEST_CASE("IOP cache geometry keeps two distinct provisional physical banks",
  "[iop][cache]")
{
  REQUIRE(IOPCache::INSTRUCTION_SIZE == 4096);
  REQUIRE(IOPCache::DATA_SIZE == 1024);
  REQUIRE(IOPCache::INSTRUCTION_LINE_WORDS == 4);
  REQUIRE(IOPCache::DATA_LINE_WORDS == 1);
  REQUIRE(IOPCache::INSTRUCTION_LINE_COUNT *
    IOPCache::INSTRUCTION_LINE_WORDS * sizeof(IOPWord) ==
    IOPCache::INSTRUCTION_SIZE);
  REQUIRE(IOPCache::DATA_LINE_COUNT *
    IOPCache::DATA_LINE_WORDS * sizeof(IOPWord) == IOPCache::DATA_SIZE);

  IOPCache cache;
  requireClear(cache);
  IOPCacheTestAccess::seed(&cache);
  requireSeeded(cache);
  cache.reset();
  requireClear(cache);
}

TEST_CASE("IOP cache inspection checks both index boundaries",
  "[iop][cache]")
{
  IOPCache cache;
  IOPCacheTestAccess::seed(&cache);
  REQUIRE_NOTHROW(cache.instructionLine(IOPCache::INSTRUCTION_LINE_COUNT - 1));
  REQUIRE_NOTHROW(cache.dataLine(IOPCache::DATA_LINE_COUNT - 1));
  REQUIRE_THROWS_AS(cache.instructionLine(IOPCache::INSTRUCTION_LINE_COUNT),
    std::out_of_range);
  REQUIRE_THROWS_AS(cache.dataLine(IOPCache::DATA_LINE_COUNT),
    std::out_of_range);
  REQUIRE_THROWS_AS(cache.instructionLine(
    std::numeric_limits<std::size_t>::max()), std::out_of_range);
  REQUIRE_THROWS_AS(cache.dataLine(
    std::numeric_limits<std::size_t>::max()), std::out_of_range);
  requireSeeded(cache);
}

TEST_CASE("IOP cache locations use physical tags without folding RAM mirrors",
  "[iop][cache]")
{
  struct BankCase
  {
    IOPCacheBank bank;
    std::size_t capacity;
    std::size_t lineWords;
  };
  const BankCase banks[] = {
    {IOPCacheBank::Instruction, IOPCache::INSTRUCTION_SIZE,
      IOPCache::INSTRUCTION_LINE_WORDS},
    {IOPCacheBank::Data, IOPCache::DATA_SIZE, IOPCache::DATA_LINE_WORDS}
  };
  for (const auto &bank : banks)
  {
    const IOPAddress lineBytes =
      static_cast<IOPAddress>(bank.lineWords * sizeof(IOPWord));
    const IOPAddress addresses[] = {
      0, 1, 3, 4, 15, 16, 0x234, 0x200234, 0x7fffff,
      0x1fc01234, 0xffffffff
    };
    for (const auto address : addresses)
    {
      const auto location = IOPCache::location(bank.bank, address);
      REQUIRE(location.lineIndex == (address % bank.capacity) / lineBytes);
      REQUIRE(location.wordIndex == (address % lineBytes) / sizeof(IOPWord));
      REQUIRE(location.byteOffset == address % sizeof(IOPWord));
      REQUIRE(location.physicalTag ==
        (address & ~static_cast<IOPAddress>(bank.capacity - 1)));
      REQUIRE(location.lineBaseAddress == (address & ~(lineBytes - 1)));
    }
    const auto first = IOPCache::location(bank.bank, 0x234);
    const auto collision = IOPCache::location(
      bank.bank, static_cast<IOPAddress>(0x234 + bank.capacity));
    const auto mirror = IOPCache::location(bank.bank, 0x200234);
    REQUIRE(first.lineIndex == collision.lineIndex);
    REQUIRE(first.physicalTag != collision.physicalTag);
    REQUIRE(first.lineIndex == mirror.lineIndex);
    REQUIRE(first.physicalTag != mirror.physicalTag);
  }
  REQUIRE_THROWS_AS(IOPCache::location(
    static_cast<IOPCacheBank>(0xff), 0), std::invalid_argument);
}

TEST_CASE("IOP cache access planning separates eligibility and unsupported modes",
  "[iop][cache]")
{
  const IOPCacheAccess accesses[] = {
    IOPCacheAccess::InstructionFetch,
    IOPCacheAccess::DataLoad,
    IOPCacheAccess::DataStore
  };
  const IOPCacheRoute routes[] = {
    IOPCacheRoute::Cached, IOPCacheRoute::Uncached
  };
  const IOPBusRegion regions[] = {
    IOPBusRegion::Unmapped, IOPBusRegion::Ram,
    IOPBusRegion::Scratchpad, IOPBusRegion::Rom
  };
  for (const auto access : accesses)
  {
    for (const auto route : routes)
    {
      for (const auto region : regions)
      {
        for (const bool isolated : {false, true})
        {
          for (const bool swapped : {false, true})
          {
            const auto plan = IOPCache::planAccess(
              access, {isolated, swapped}, route, region);
            const bool fetch = access == IOPCacheAccess::InstructionFetch;
            REQUIRE(plan.bank == ((fetch != swapped)
              ? IOPCacheBank::Instruction : IOPCacheBank::Data));
            const bool eligible = route == IOPCacheRoute::Cached &&
              (region == IOPBusRegion::Ram || region == IOPBusRegion::Rom);
            const bool unsupported =
              (!fetch && isolated) || (eligible && swapped);
            const auto expected = unsupported
              ? IOPCacheAccessDisposition::UnsupportedMode
              : eligible ? IOPCacheAccessDisposition::Cache
                         : IOPCacheAccessDisposition::Bypass;
            REQUIRE(plan.disposition == expected);
          }
        }
      }
    }
  }
  REQUIRE_THROWS_AS(IOPCache::planAccess(
    static_cast<IOPCacheAccess>(0xff), {}, IOPCacheRoute::Cached,
    IOPBusRegion::Ram), std::invalid_argument);
  REQUIRE_THROWS_AS(IOPCache::planAccess(
    IOPCacheAccess::DataLoad, {}, IOPCacheRoute::None,
    IOPBusRegion::Ram), std::invalid_argument);
  REQUIRE_THROWS_AS(IOPCache::planAccess(
    IOPCacheAccess::DataLoad, {}, static_cast<IOPCacheRoute>(0xff),
    IOPBusRegion::Ram), std::invalid_argument);
  REQUIRE_THROWS_AS(IOPCache::planAccess(
    IOPCacheAccess::DataLoad, {}, IOPCacheRoute::Cached,
    static_cast<IOPBusRegion>(0xff)), std::invalid_argument);
}

TEST_CASE("IOP bus region queries share physical decoding and validate full ranges",
  "[iop][cache][bus]")
{
  IOPBus bus;
  REQUIRE(bus.romIdentity() == nullptr);
  REQUIRE(bus.regionForRange(IOPBus::ROM_BASE, 16) == IOPBusRegion::Unmapped);
  const auto rom = makeRom(0x12);
  REQUIRE(bus.installRom(rom));
  REQUIRE(bus.romIdentity() == rom);
  struct RangeCase
  {
    IOPAddress address;
    std::size_t count;
    IOPBusRegion expected;
  };
  const RangeCase cases[] = {
    {0, 0, IOPBusRegion::Ram},
    {0, IOPBus::RAM_SIZE, IOPBusRegion::Ram},
    {0x1fffff, 1, IOPBusRegion::Ram},
    {0x1fffff, 2, IOPBusRegion::Unmapped},
    {0x200000, IOPBus::RAM_SIZE, IOPBusRegion::Ram},
    {0x7ffff0, 16, IOPBusRegion::Ram},
    {0x7ffff0, 17, IOPBusRegion::Unmapped},
    {0x800000, 1, IOPBusRegion::Unmapped},
    {IOPBus::SCRATCHPAD_BASE, IOPBus::SCRATCHPAD_SIZE,
      IOPBusRegion::Scratchpad},
    {0x1f8003fc, 4, IOPBusRegion::Scratchpad},
    {0x1f8003ff, 2, IOPBusRegion::Unmapped},
    {0x1f800400, 0, IOPBusRegion::Unmapped},
    {IOPBus::ROM_BASE, IOPBus::ROM_SIZE, IOPBusRegion::Rom},
    {0x1fffffff, 1, IOPBusRegion::Rom},
    {0x1fffffff, 2, IOPBusRegion::Unmapped},
    {0x80000000, 4, IOPBusRegion::Unmapped},
    {0xffffffff, 1, IOPBusRegion::Unmapped},
    {0xffffffff, 2, IOPBusRegion::Unmapped},
    {1, std::numeric_limits<std::size_t>::max(), IOPBusRegion::Unmapped},
    {IOPBus::ROM_BASE, std::numeric_limits<std::size_t>::max(),
      IOPBusRegion::Unmapped}
  };
  for (const auto &test : cases)
  {
    REQUIRE(bus.regionForRange(test.address, test.count) == test.expected);
  }
  REQUIRE(bus.romIdentity() == rom);
  REQUIRE(bus.read8(IOPBus::ROM_BASE).value == 0x12);
  REQUIRE_FALSE(bus.installRom(nullptr));
  REQUIRE(bus.romIdentity() == rom);
  bus.reset();
  REQUIRE(bus.romIdentity() == rom);
  const auto replacement = makeRom(0x34);
  REQUIRE(bus.installRom(replacement));
  REQUIRE(bus.romIdentity() == replacement);
  REQUIRE(bus.read8(IOPBus::ROM_BASE).value == 0x34);
  REQUIRE((*rom)[0] == 0x12);
}

TEST_CASE("IOP core cache reset clears invalid payload as well as valid entries",
  "[iop][cache][execution]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  REQUIRE(bus.write32(0x100, 0x12345678).status == IOPBusStatus::Completed);
  requireClear(core.cache());
  IOPCacheTestAccess::seed(&core);
  requireSeeded(core.cache());
  core.reset();
  requireClear(core.cache());
  REQUIRE(core.hasAttachedBus());
  REQUIRE(bus.read32(0x100).value == 0x12345678);
  IOPCacheTestAccess::seed(&core);
  core.prepareFreshExecution(0xa0000000, 0x1000, 0x2000);
  requireClear(core.cache());
  REQUIRE(core.programCounter() == 0xa0000000);
  REQUIRE(core.generalRegister(29) == 0x1000);
  REQUIRE(core.generalRegister(31) == 0x2000);
  REQUIRE(bus.read32(0x100).value == 0x12345678);
}

TEST_CASE("IOP core preserves cache contents across ordinary execution boundaries",
  "[iop][cache][execution]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  REQUIRE(bus.write32(0, 0).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(4, 0x0000000c).status == IOPBusStatus::Completed);
  IOPCacheTestAccess::seed(&core);
  core.startExecution(0xa0000000);
  requireSeeded(core.cache());
  core.haltExecution();
  core.resumeExecution();
  requireSeeded(core.cache());
  core.stepInstruction();
  REQUIRE(core.retiredInstructions() == 1);
  requireSeeded(core.cache());
  core.stepInstruction();
  REQUIRE(core.exception().kind == IOPException::SystemCall);
  requireSeeded(core.cache());
  bus.reset();
  requireSeeded(core.cache());
}

TEST_CASE("IOP core bus changes clear cache state but same-bus attachment preserves it",
  "[iop][cache][execution]")
{
  IOPBus bus;
  IOPBus other;
  IOPCore core;
  core.attachBus(&bus);
  IOPCacheTestAccess::seed(&core);
  core.attachBus(&bus);
  requireSeeded(core.cache());
  core.attachBus(&other);
  requireClear(core.cache());
  IOPCacheTestAccess::seed(&core);
  core.attachBus(nullptr);
  requireClear(core.cache());
  REQUIRE_FALSE(core.hasAttachedBus());
  IOPCacheTestAccess::seed(&core);
  core.attachBus(&bus);
  requireClear(core.cache());
}

TEST_CASE("IOP core detects ROM reconfiguration before every guest access family",
  "[iop][cache][execution]")
{
  for (unsigned access = 0; access < 9; ++access)
  {
    IOPBus bus;
    IOPCore core;
    const auto first = makeRom(0x12);
    const auto second = makeRom(0x34);
    REQUIRE(bus.installRom(first));
    core.attachBus(&bus);
    core.startExecution(0xbfc00000);
    IOPCacheTestAccess::seed(&core);
    REQUIRE(bus.installRom(first));
    core.attachBus(&bus);
    requireSeeded(core.cache());
    REQUIRE(bus.installRom(second));
    requireSeeded(core.cache());
    switch (access)
    {
      case 0:
        REQUIRE(core.fetchInstruction().value == 0x34);
        break;
      case 1:
        REQUIRE(core.readData8(0xbfc00000).value == 0x34);
        break;
      case 2:
        REQUIRE(core.readData16(0xbfc00000).value == 0x34);
        break;
      case 3:
        REQUIRE(core.readData32(0xbfc00000).value == 0x34);
        break;
      case 4:
        REQUIRE(core.writeData8(0x100, 0x56).outcome ==
          IOPAccessOutcome::Completed);
        break;
      case 5:
        REQUIRE(core.writeData16(0x100, 0x5678).outcome ==
          IOPAccessOutcome::Completed);
        break;
      case 6:
        REQUIRE(core.writeData32(0x100, 0x56789abc).outcome ==
          IOPAccessOutcome::Completed);
        break;
      case 7:
        core.attachBus(&bus);
        break;
      case 8:
        REQUIRE(bus.write32(0, 0xa8000001).status == IOPBusStatus::Completed);
        core.startExecution(0xa0000000);
        core.stepInstruction(); // SWL r0, 1(r0).
        REQUIRE(core.retiredInstructions() == 1);
        break;
    }
    requireClear(core.cache());
    IOPCacheTestAccess::seed(&core);
    REQUIRE(core.readData32(0xbfc00000).value == 0x34);
    requireSeeded(core.cache());
  }
}

TEST_CASE("IOP uncached data and fetch ignore seeded cache entries",
  "[iop][cache][execution]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  REQUIRE(bus.write32(0x100, 0x11223344).status == IOPBusStatus::Completed);
  IOPCacheTestAccess::seed(&core);
  core.startExecution(0xa0000100);
  REQUIRE(core.fetchInstruction().value == 0x11223344);
  REQUIRE(core.readData8(0xa0000100).value == 0x44);
  REQUIRE(core.readData16(0xa0000100).value == 0x3344);
  REQUIRE(core.readData32(0xa0000100).value == 0x11223344);
  REQUIRE(core.writeData32(0x80000100, 0xaabbccdd).outcome ==
    IOPAccessOutcome::Completed);
  REQUIRE(bus.read32(0x100).value == 0xaabbccdd);
  requireSeeded(core.cache());
}

TEST_CASE("IOP cached data reads fill one word and extract every little endian lane",
  "[iop][cache][data-load]")
{
  for (const unsigned width : {1u, 2u, 4u})
  {
    for (IOPAddress offset = 0; offset < 4; offset += width)
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHE_PROBE_RESULT);
      REQUIRE(bus.write32(0x120, 0x89abcdef).status == IOPBusStatus::Completed);
      const auto first = width == 1 ? core.readData8(0x80000120 + offset)
        : width == 2 ? core.readData16(0x80000120 + offset)
                     : core.readData32(0x80000120);
      const IOPWord mask = width == 1 ? 0xff : width == 2 ? 0xffff : UINT32_MAX;
      REQUIRE(first.outcome == IOPAccessOutcome::Completed);
      REQUIRE(first.exception == IOPException::None);
      REQUIRE(first.virtualAddress == 0x80000120 + offset);
      REQUIRE(first.physicalAddress == 0x120 + offset);
      REQUIRE(first.cacheRoute == IOPCacheRoute::Cached);
      REQUIRE(first.value == ((0x89abcdefu >> (8 * offset)) & mask));
      const auto location = IOPCache::location(IOPCacheBank::Data, 0x120);
      const auto &line = core.cache().dataLine(location.lineIndex);
      REQUIRE(line.valid);
      REQUIRE(line.physicalTag == location.physicalTag);
      REQUIRE(line.words[0] == 0x89abcdef);
      REQUIRE_FALSE(core.cache().dataLine(location.lineIndex + 1).valid);
      REQUIRE(bus.write32(0x120, 0x12345678).status == IOPBusStatus::Completed);
      REQUIRE(core.readData8(0x120).value == 0xef);
      REQUIRE(core.readData8(0x121).value == 0xcd);
      REQUIRE(core.readData8(0x122).value == 0xab);
      REQUIRE(core.readData8(0x123).value == 0x89);
      REQUIRE(core.readData16(0x120).value == 0xcdef);
      REQUIRE(core.readData16(0x122).value == 0x89ab);
      REQUIRE(core.readData32(0x120).value == 0x89abcdef);
      REQUIRE(core.status() == IOPCOP0Status::CACHE_PROBE_RESULT);
      REQUIRE(core.elapsedCycles() == 0);
      REQUIRE(core.retiredInstructions() == 0);
      for (std::size_t index = 0; index < IOPCache::INSTRUCTION_LINE_COUNT; ++index)
      {
        REQUIRE_FALSE(core.cache().instructionLine(index).valid);
      }
    }
  }
}

TEST_CASE("IOP data aliases share physical tags without folding RAM mirror storage",
  "[iop][cache][data-load]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  REQUIRE(bus.write32(0x124, 0x11223344).status == IOPBusStatus::Completed);
  REQUIRE(core.readData32(0x80000124).value == 0x11223344);
  const std::uint8_t bytes[] = {0x88, 0x77, 0x66, 0x55};
  REQUIRE(bus.loadPhysical(0x124, bytes, sizeof(bytes)) == IOPBusStatus::Completed);
  REQUIRE(core.readData32(0x124).value == 0x11223344);
  REQUIRE(core.readData32(0xa0000124).value == 0x55667788);
  REQUIRE(core.readData32(0x80000124).value == 0x11223344);
  REQUIRE(core.readData32(0x80200124).value == 0x55667788);
  const auto location = IOPCache::location(IOPCacheBank::Data, 0x200124);
  REQUIRE(core.cache().dataLine(location.lineIndex).physicalTag ==
    location.physicalTag);
  REQUIRE(bus.write32(0x124, 0xaabbccdd).status == IOPBusStatus::Completed);
  REQUIRE(core.readData32(0x124).value == 0xaabbccdd);
}

TEST_CASE("IOP data misses replace only the indexed word without adjacent prefetch",
  "[iop][cache][data-load]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  const IOPAddress collision = 0x120 + IOPCache::DATA_SIZE;
  REQUIRE(bus.write32(0x120, 0x11223344).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x124, 0x55667788).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(collision, 0xaabbccdd).status == IOPBusStatus::Completed);
  REQUIRE(core.readData32(0x120).value == 0x11223344);
  REQUIRE(bus.write32(0x124, 0x12345678).status == IOPBusStatus::Completed);
  REQUIRE(core.readData32(0x124).value == 0x12345678);
  REQUIRE(core.readData32(collision).value == 0xaabbccdd);
  REQUIRE(bus.write32(0x124, 0x87654321).status == IOPBusStatus::Completed);
  REQUIRE(core.readData32(0x124).value == 0x12345678);
  REQUIRE(bus.write32(0x120, 0xdeadbeef).status == IOPBusStatus::Completed);
  REQUIRE(core.readData32(0x120).value == 0xdeadbeef);
}

TEST_CASE("IOP instruction and data cache banks retain independent snapshots",
  "[iop][cache][data-load]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  core.startExecution(0x120);
  REQUIRE(bus.write32(0x120, 0x11223344).status == IOPBusStatus::Completed);
  REQUIRE(core.fetchInstruction().value == 0x11223344);
  REQUIRE(bus.write32(0x120, 0x55667788).status == IOPBusStatus::Completed);
  REQUIRE(core.readData32(0x120).value == 0x55667788);
  REQUIRE(bus.write32(0x120, 0xaabbccdd).status == IOPBusStatus::Completed);
  REQUIRE(core.fetchInstruction().value == 0x11223344);
  REQUIRE(core.readData32(0x120).value == 0x55667788);
  REQUIRE(core.readData32(0xa0000120).value == 0xaabbccdd);
  core.reset();
  REQUIRE(core.readData32(0x120).value == 0xaabbccdd);
}

TEST_CASE("IOP scratchpad data loads bypass both cache banks at every width",
  "[iop][cache][data-load]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCacheTestAccess::seed(&core);
  for (const IOPAddress address : {0x1f800100u, 0x9f800100u, 0xbf800100u})
  {
    REQUIRE(bus.write32(0x1f800100, 0x11223344).status == IOPBusStatus::Completed);
    REQUIRE(core.readData32(address).value == 0x11223344);
    REQUIRE(bus.write32(0x1f800100, 0xaabbccdd).status == IOPBusStatus::Completed);
    REQUIRE(core.readData8(address + 3).value == 0xaa);
    REQUIRE(core.readData16(address + 2).value == 0xaabb);
    REQUIRE(core.readData32(address).value == 0xaabbccdd);
    requireSeeded(core.cache());
  }
}

TEST_CASE("IOP cached ROM data hits reconcile image identity before returning data",
  "[iop][cache][data-load]")
{
  IOPBus bus;
  IOPCore core;
  REQUIRE(bus.installRom(makeRom(0x12)));
  core.attachBus(&bus);
  REQUIRE(core.readData8(0x9fc00000).value == 0x12);
  const auto location = IOPCache::location(IOPCacheBank::Data, IOPBus::ROM_BASE);
  REQUIRE(core.cache().dataLine(location.lineIndex).valid);
  REQUIRE(bus.installRom(makeRom(0x34)));
  REQUIRE(core.readData8(0x1fc00000).value == 0x34);
  REQUIRE(core.cache().dataLine(location.lineIndex).valid);
  REQUIRE(core.cache().dataLine(location.lineIndex).words[0] == 0x34);
}

TEST_CASE("IOP cached data reads reach the last byte of each RAM alias and ROM",
  "[iop][cache][data-load]")
{
  IOPBus bus;
  IOPCore core;
  std::vector<std::uint8_t> bytes(IOPBus::ROM_SIZE, 0x89);
  REQUIRE(bus.installRom(BootROMImage::create(bytes)));
  core.attachBus(&bus);
  for (const IOPAddress physical :
       {0x001ffffcu, 0x003ffffcu, 0x005ffffcu, 0x007ffffcu, 0x1ffffffcu})
  {
    const bool rom = physical >= IOPBus::ROM_BASE;
    if (!rom)
    {
      REQUIRE(bus.write32(physical, 0x89abcdef).status == IOPBusStatus::Completed);
    }
    REQUIRE(core.readData8(physical + 3).value == 0x89);
    REQUIRE(core.readData16(physical + 2).value == (rom ? 0x8989u : 0x89abu));
    REQUIRE(core.readData32(physical).value == (rom ? 0x89898989u : 0x89abcdefu));
    const auto location = IOPCache::location(IOPCacheBank::Data, physical);
    REQUIRE(core.cache().dataLine(location.lineIndex).valid);
    REQUIRE(core.cache().dataLine(location.lineIndex).physicalTag ==
      location.physicalTag);
  }
}

TEST_CASE("IOP rejected data loads preserve cache banks and probe state",
  "[iop][cache][data-load]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCacheTestAccess::seed(&core);
  IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHE_PROBE_RESULT);
  REQUIRE(core.readData16(0x101).outcome == IOPAccessOutcome::Misaligned);
  REQUIRE(core.readData32(0x102).outcome == IOPAccessOutcome::Misaligned);
  for (const IOPAddress address : {0x00800000u, 0x1f800400u, 0x1fc00000u})
  {
    const auto read = core.readData32(address);
    REQUIRE(read.outcome == IOPAccessOutcome::Unmapped);
    REQUIRE(read.exception == IOPException::DataBusError);
    REQUIRE(read.physicalAddress == address);
  }
  REQUIRE(core.status() == IOPCOP0Status::CACHE_PROBE_RESULT);
  IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHE_PROBE_RESULT |
    IOPCOP0Status::CURRENT_USER_MODE);
  REQUIRE(core.readData32(0x80000100).outcome == IOPAccessOutcome::ProtectionFailure);
  requireSeeded(core.cache());
}

TEST_CASE("IOP cached scalar load hits and misses retain signed extension and load delay",
  "[iop][cache][data-load][execution]")
{
  struct LoadCase
  {
    IOPWord opcode;
    IOPAddress offset;
    IOPWord expected;
  };
  const LoadCase cases[] = {
    {0x20, 0, 0xfffffffe}, {0x20, 1, 0xffffff80},
    {0x20, 2, 0xffffffab}, {0x20, 3, 0xffffff89},
    {0x24, 0, 0xfe}, {0x24, 1, 0x80}, {0x24, 2, 0xab}, {0x24, 3, 0x89},
    {0x21, 0, 0xffff80fe}, {0x21, 2, 0xffff89ab},
    {0x25, 0, 0x80fe}, {0x25, 2, 0x89ab}, {0x23, 0, 0x89ab80fe}
  };
  for (const bool hit : {false, true})
  {
    for (const auto &test : cases)
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      REQUIRE(bus.write32(0x200, 0x89ab80fe).status == IOPBusStatus::Completed);
      if (hit)
      {
        REQUIRE(core.readData32(0x200).value == 0x89ab80fe);
        REQUIRE(bus.write32(0x200, 0x12345678).status == IOPBusStatus::Completed);
      }
      const IOPWord load = (test.opcode << 26) | (2u << 16) | (0x200 + test.offset);
      REQUIRE(bus.write32(0x100, load).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(0x104, 0x00401821).status == IOPBusStatus::Completed);
      IOPCacheTestAccess::setGeneralRegister(&core, 2, 0x55);
      core.startExecution(0x100);
      core.stepInstruction();
      REQUIRE(core.generalRegister(2) == 0x55);
      REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::Load);
      REQUIRE(core.delayedResultContinuation().value == test.expected);
      core.stepInstruction();
      REQUIRE(core.generalRegister(3) == 0x55);
      REQUIRE(core.generalRegister(2) == test.expected);
      REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
      REQUIRE(core.retiredInstructions() == 2);
      REQUIRE(core.elapsedCycles() == 2);
    }
  }
}

TEST_CASE("IOP cached merge pairs preserve every offset and both instruction orders",
  "[iop][cache][data-load][merge][execution]")
{
  for (const bool hit : {false, true})
  {
    for (const bool leftFirst : {false, true})
    {
      for (IOPAddress offset = 0; offset < 4; ++offset)
      {
        IOPBus bus;
        IOPCore core;
        core.attachBus(&bus);
        REQUIRE(bus.write32(0x200, 0x44332211).status == IOPBusStatus::Completed);
        REQUIRE(bus.write32(0x204, 0x88776655).status == IOPBusStatus::Completed);
        if (hit)
        {
          REQUIRE(core.readData32(0x200).value == 0x44332211);
          REQUIRE(core.readData32(0x204).value == 0x88776655);
          REQUIRE(bus.write32(0x200, 0xaabbccdd).status == IOPBusStatus::Completed);
          REQUIRE(bus.write32(0x204, 0xeeff0011).status == IOPBusStatus::Completed);
        }
        const IOPWord left = 0x88020203 + offset; // LWL r2, 0x203+offset(r0).
        const IOPWord right = 0x98020200 + offset;
        REQUIRE(bus.write32(0x100, leftFirst ? left : right).status ==
          IOPBusStatus::Completed);
        REQUIRE(bus.write32(0x104, leftFirst ? right : left).status ==
          IOPBusStatus::Completed);
        core.startExecution(0x100);
        core.stepInstruction();
        core.haltExecution();
        core.resumeExecution();
        core.stepInstruction();
        core.stepInstruction();
        REQUIRE(core.generalRegister(2) ==
          static_cast<IOPWord>(UINT64_C(0x8877665544332211) >> (offset * 8)));
        REQUIRE(core.exception().kind == IOPException::None);
        REQUIRE(core.retiredInstructions() == 3);
        REQUIRE(core.elapsedCycles() == 3);
      }
    }
  }
}

TEST_CASE("IOP loads into register zero still populate the data cache",
  "[iop][cache][data-load][execution]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  REQUIRE(bus.write32(0x200, 0x11223344).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x100, 0x8c000200).status == IOPBusStatus::Completed);
  core.startExecution(0x100);
  core.stepInstruction();
  REQUIRE(core.generalRegister(0) == 0);
  REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
  REQUIRE(bus.write32(0x200, 0x55667788).status == IOPBusStatus::Completed);
  REQUIRE(core.readData32(0x200).value == 0x11223344);
  REQUIRE(core.retiredInstructions() == 1);
}

TEST_CASE("IOP failed data fills retain invalid prior payload and reject unaligned words",
  "[iop][cache][data-load]")
{
  IOPBus bus;
  IOPCache cache;
  IOPCacheTestAccess::seed(&cache);
  const auto location = IOPCache::location(IOPCacheBank::Data, 0x00800004);
  const auto before = cache.dataLine(location.lineIndex);
  REQUIRE(before.valid);
  REQUIRE(cache.readDataWord(bus, 0x00800004).status == IOPBusStatus::Unmapped);
  const auto &after = cache.dataLine(location.lineIndex);
  REQUIRE_FALSE(after.valid);
  REQUIRE(after.physicalTag == before.physicalTag);
  REQUIRE(after.words == before.words);
  REQUIRE(cache.readDataWord(bus, 5).status == IOPBusStatus::Misaligned);
  REQUIRE_FALSE(after.valid);
  REQUIRE(after.physicalTag == before.physicalTag);
  REQUIRE(after.words == before.words);
  REQUIRE(bus.write32(4, 0x11223344).status == IOPBusStatus::Completed);
  REQUIRE(cache.readDataWord(bus, 4).value == 0x11223344);
  REQUIRE(after.valid);
  REQUIRE(after.physicalTag == 0);
  REQUIRE(after.words[0] == 0x11223344);
}

TEST_CASE("IOP data faults retain cached older results and branch delay provenance",
  "[iop][cache][data-load][execution]")
{
  for (const bool delaySlot : {false, true})
  {
    for (const IOPWord opcode : {0x23u, 0x22u, 0x26u})
    {
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      IOPCacheTestAccess::setStatus(&core,
        core.status() | IOPCOP0Status::CACHE_PROBE_RESULT);
      IOPCacheTestAccess::setGeneralRegister(&core, 1, 0x00800001);
      REQUIRE(bus.write32(0x200, 0x11223344).status == IOPBusStatus::Completed);
      REQUIRE(core.readData32(0x200).value == 0x11223344);
      REQUIRE(bus.write32(0x200, 0x55667788).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(0x100, delaySlot ? 0x10000002 : 0x8c020200).status ==
        IOPBusStatus::Completed);
      const IOPWord fault = (opcode << 26) | (1u << 21) | (3u << 16);
      REQUIRE(bus.write32(0x104, fault).status == IOPBusStatus::Completed);
      core.startExecution(0x100);
      core.stepInstruction();
      core.stepInstruction();
      const bool alignedLoad = opcode == 0x23;
      REQUIRE(core.exception().kind == (alignedLoad
        ? IOPException::AddressErrorLoadOrFetch : IOPException::DataBusError));
      REQUIRE(core.exception().badVirtualAddress == 0x00800001);
      REQUIRE(core.badVirtualAddress() == (alignedLoad ? 0x00800001 : 0));
      REQUIRE(core.exception().inBranchDelaySlot == delaySlot);
      REQUIRE(core.exceptionProgramCounter() == (delaySlot ? 0x100 : 0x104));
      REQUIRE(core.generalRegister(2) == (delaySlot ? 0 : 0x11223344));
      REQUIRE(core.generalRegister(3) == 0);
      REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
      REQUIRE_FALSE(core.branchContinuation().active);
      REQUIRE((core.status() & IOPCOP0Status::CACHE_PROBE_RESULT) != 0);
      REQUIRE(core.retiredInstructions() == 1);
      REQUIRE(core.elapsedCycles() == 2);
      REQUIRE(core.readData32(0x200).value == 0x11223344);
    }
  }
}

TEST_CASE("IOP ROM reconfiguration preserves pending CPU continuations",
  "[iop][cache][execution]")
{
  for (const bool pendingLoad : {false, true})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    REQUIRE(bus.write32(0, pendingLoad ? 0x8c010100 : 0x10000002).status ==
      IOPBusStatus::Completed);
    REQUIRE(bus.write32(0x100, 0x12345678).status == IOPBusStatus::Completed);
    core.startExecution(0xa0000000);
    core.stepInstruction();
    REQUIRE(core.programCounter() == 0xa0000004);
    REQUIRE(core.delayedResultContinuation().source ==
      (pendingLoad ? IOPDelayedResultSource::Load
                   : IOPDelayedResultSource::None));
    REQUIRE(core.branchContinuation().active == !pendingLoad);
    const auto branch = core.branchContinuation();
    const auto delayedResult = core.delayedResultContinuation();
    IOPCacheTestAccess::seed(&core);
    REQUIRE(bus.installRom(makeRom(0x12)));
    REQUIRE(core.fetchInstruction().outcome == IOPAccessOutcome::Completed);
    requireClear(core.cache());
    REQUIRE(core.programCounter() == 0xa0000004);
    REQUIRE(core.generalRegister(1) == 0);
    REQUIRE(core.retiredInstructions() == 1);
    REQUIRE(core.elapsedCycles() == 1);
    REQUIRE(core.branchContinuation().active == branch.active);
    REQUIRE(core.branchContinuation().instructionAddress == branch.instructionAddress);
    REQUIRE(core.branchContinuation().targetAddress == branch.targetAddress);
    REQUIRE(core.delayedResultContinuation().source == delayedResult.source);
    REQUIRE(core.delayedResultContinuation().destination == delayedResult.destination);
    REQUIRE(core.delayedResultContinuation().value == delayedResult.value);
    core.stepInstruction();
    REQUIRE(core.retiredInstructions() == 2);
    REQUIRE(core.generalRegister(1) == (pendingLoad ? 0x12345678u : 0u));
    REQUIRE(core.programCounter() == (pendingLoad ? 0xa0000008u : 0xa000000cu));
  }
}

TEST_CASE("IOP ROM identity tracking retains the old image until rebinding",
  "[iop][cache][execution]")
{
  IOPBus bus;
  IOPCore core;
  auto rom = makeRom(0x12);
  const std::weak_ptr<const BootROMImage> oldImage = rom;
  REQUIRE(bus.installRom(rom));
  core.attachBus(&bus);
  rom.reset();
  REQUIRE(bus.installRom(makeRom(0x34)));
  REQUIRE_FALSE(oldImage.expired());
  REQUIRE(core.readData8(IOPBus::ROM_BASE).value == 0x34);
  REQUIRE(oldImage.expired());
}

TEST_CASE("IOP cached fetch fills a complete line and preserves stale words",
  "[iop][cache][fetch]")
{
  for (IOPAddress missedWord = 0; missedWord < 4; ++missedWord)
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    for (IOPAddress word = 0; word < 8; ++word)
    {
      REQUIRE(bus.write32(0x120 + word * 4, 0x12340000 + word).status ==
        IOPBusStatus::Completed);
    }
    core.startExecution(0x80000120 + missedWord * 4);
    const auto first = core.fetchInstruction();
    REQUIRE(first.outcome == IOPAccessOutcome::Completed);
    REQUIRE(first.exception == IOPException::None);
    REQUIRE(first.virtualAddress == 0x80000120 + missedWord * 4);
    REQUIRE(first.physicalAddress == 0x120 + missedWord * 4);
    REQUIRE(first.cacheRoute == IOPCacheRoute::Cached);
    REQUIRE(first.value == 0x12340000 + missedWord);
    const auto location = IOPCache::location(IOPCacheBank::Instruction, 0x120);
    const auto &line = core.cache().instructionLine(location.lineIndex);
    REQUIRE(line.valid);
    REQUIRE(line.physicalTag == location.physicalTag);
    for (IOPAddress word = 0; word < 4; ++word)
    {
      REQUIRE(line.words[word] == 0x12340000 + word);
      REQUIRE(core.writeData32(0x120 + word * 4, 0x56780000 + word).outcome ==
        IOPAccessOutcome::Completed);
      core.startExecution(0x80000120 + word * 4);
      REQUIRE(core.fetchInstruction().value == 0x12340000 + word);
      core.startExecution(0xa0000120 + word * 4);
      REQUIRE(core.fetchInstruction().value == 0x56780000 + word);
    }
    const auto next = IOPCache::location(IOPCacheBank::Instruction, 0x130);
    REQUIRE_FALSE(core.cache().instructionLine(next.lineIndex).valid);
    REQUIRE(core.elapsedCycles() == 0);
    REQUIRE(core.retiredInstructions() == 0);
    for (std::size_t index = 0; index < IOPCache::DATA_LINE_COUNT; ++index)
    {
      REQUIRE_FALSE(core.cache().dataLine(index).valid);
    }
  }
}

TEST_CASE("IOP fetch shares translated aliases but not physical RAM mirrors",
  "[iop][cache][fetch]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  REQUIRE(bus.write32(0x124, 0x11112222).status == IOPBusStatus::Completed);
  core.startExecution(0x80000124);
  REQUIRE(core.fetchInstruction().value == 0x11112222);
  const std::uint8_t replacement[] = {0x44, 0x44, 0x33, 0x33};
  REQUIRE(bus.loadPhysical(0x124, replacement, sizeof(replacement)) ==
    IOPBusStatus::Completed);
  core.startExecution(0x124);
  REQUIRE(core.fetchInstruction().value == 0x11112222);
  core.startExecution(0xa0000124);
  REQUIRE(core.fetchInstruction().value == 0x33334444);
  core.startExecution(0x80000124);
  REQUIRE(core.fetchInstruction().value == 0x11112222);
  core.startExecution(0x80200124);
  REQUIRE(core.fetchInstruction().value == 0x33334444);
  const auto location = IOPCache::location(IOPCacheBank::Instruction, 0x200124);
  REQUIRE(core.cache().instructionLine(location.lineIndex).physicalTag ==
    location.physicalTag);
  REQUIRE(bus.write32(0x124, 0x55556666).status == IOPBusStatus::Completed);
  core.startExecution(0x80000124);
  REQUIRE(core.fetchInstruction().value == 0x55556666);
}

TEST_CASE("IOP instruction cache collisions replace only the indexed line",
  "[iop][cache][fetch]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  const IOPAddress collision = 0x120 + IOPCache::INSTRUCTION_SIZE;
  REQUIRE(bus.write32(0x120, 0x11223344).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(0x130, 0x55667788).status == IOPBusStatus::Completed);
  REQUIRE(bus.write32(collision, 0xaabbccdd).status == IOPBusStatus::Completed);
  core.startExecution(0x130);
  REQUIRE(core.fetchInstruction().value == 0x55667788);
  core.startExecution(0x120);
  REQUIRE(core.fetchInstruction().value == 0x11223344);
  REQUIRE(bus.write32(0x120, 0x12345678).status == IOPBusStatus::Completed);
  core.startExecution(collision);
  REQUIRE(core.fetchInstruction().value == 0xaabbccdd);
  core.startExecution(0x130);
  REQUIRE(core.fetchInstruction().value == 0x55667788);
  core.startExecution(0x120);
  REQUIRE(core.fetchInstruction().value == 0x12345678);
}

TEST_CASE("IOP scratchpad and uncached fetch never mutate either bank",
  "[iop][cache][fetch]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCacheTestAccess::seed(&core);
  for (const IOPAddress address : {0xa0000000u, 0x1f800000u, 0x9f800000u})
  {
    const IOPAddress physical = address & 0x1fffffff;
    core.startExecution(address);
    REQUIRE(bus.write32(physical, 0x11223344).status == IOPBusStatus::Completed);
    REQUIRE(core.fetchInstruction().value == 0x11223344);
    REQUIRE(bus.write32(physical, 0xaabbccdd).status == IOPBusStatus::Completed);
    REQUIRE(core.fetchInstruction().value == 0xaabbccdd);
    requireSeeded(core.cache());
  }
}

TEST_CASE("IOP ROM identity is reconciled before a cached fetch hit",
  "[iop][cache][fetch]")
{
  IOPBus bus;
  IOPCore core;
  REQUIRE(bus.installRom(makeRom(0x12)));
  core.attachBus(&bus);
  core.startExecution(0x9fc00000);
  REQUIRE(core.fetchInstruction().value == 0x12);
  const auto location = IOPCache::location(
    IOPCacheBank::Instruction, IOPBus::ROM_BASE);
  REQUIRE(core.cache().instructionLine(location.lineIndex).valid);
  REQUIRE(bus.installRom(makeRom(0x34)));
  REQUIRE(core.fetchInstruction().value == 0x34);
  REQUIRE(core.cache().instructionLine(location.lineIndex).valid);
  REQUIRE(core.cache().instructionLine(location.lineIndex).words[0] == 0x34);
  core.reset();
  requireClear(core.cache());
}

TEST_CASE("IOP fetch ignores isolation and preserves cache probe state",
  "[iop][cache][fetch]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  const IOPWord status =
    IOPCOP0Status::CACHE_ISOLATED | IOPCOP0Status::CACHE_PROBE_RESULT;
  IOPCacheTestAccess::setStatus(&core, status);
  core.startExecution(0x80000100);
  REQUIRE(bus.write32(0x100, 0x11223344).status == IOPBusStatus::Completed);
  REQUIRE(core.fetchInstruction().value == 0x11223344);
  REQUIRE(bus.write32(0x100, 0xaabbccdd).status == IOPBusStatus::Completed);
  REQUIRE(core.fetchInstruction().value == 0x11223344);
  REQUIRE(core.status() == status);
  core.startExecution(0xa0000100);
  REQUIRE(core.fetchInstruction().value == 0xaabbccdd);
  REQUIRE(core.status() == status);
}

TEST_CASE("IOP execution sees stale cached code until an instruction line is evicted",
  "[iop][cache][fetch][execution]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  REQUIRE(bus.write32(0x100, 0x24010001).status == IOPBusStatus::Completed);
  core.startExecution(0x80000100);
  core.stepInstruction();
  REQUIRE(core.generalRegister(1) == 1);
  REQUIRE(core.writeData32(0x100, 0x24010002).outcome ==
    IOPAccessOutcome::Completed);
  core.startExecution(0x100);
  core.stepInstruction();
  REQUIRE(core.generalRegister(1) == 1);
  core.startExecution(0xa0000100);
  core.stepInstruction();
  REQUIRE(core.generalRegister(1) == 2);
  core.startExecution(0x100 + IOPCache::INSTRUCTION_SIZE);
  REQUIRE(core.fetchInstruction().outcome == IOPAccessOutcome::Completed);
  core.startExecution(0x100);
  core.stepInstruction();
  REQUIRE(core.generalRegister(1) == 2);
  REQUIRE(core.elapsedCycles() == 4);
  REQUIRE(core.retiredInstructions() == 4);
}

TEST_CASE("IOP failed instruction fills invalidate without replacing prior payload",
  "[iop][cache][fetch]")
{
  IOPBus bus;
  IOPCache cache;
  IOPCacheTestAccess::seed(&cache);
  const IOPAddress unmapped = 0x0080001c;
  const auto location = IOPCache::location(IOPCacheBank::Instruction, unmapped);
  const auto before = cache.instructionLine(location.lineIndex);
  REQUIRE(before.valid);
  REQUIRE(cache.readInstruction(bus, unmapped).status == IOPBusStatus::Unmapped);
  const auto &after = cache.instructionLine(location.lineIndex);
  REQUIRE_FALSE(after.valid);
  REQUIRE(after.physicalTag == before.physicalTag);
  REQUIRE(after.words == before.words);
  REQUIRE(cache.readInstruction(bus, unmapped).status == IOPBusStatus::Unmapped);
  REQUIRE_FALSE(after.valid);
  REQUIRE(after.physicalTag == before.physicalTag);
  REQUIRE(after.words == before.words);
  REQUIRE(bus.write32(0x1c, 0x11223344).status == IOPBusStatus::Completed);
  REQUIRE(cache.readInstruction(bus, 0x1c).value == 0x11223344);
  REQUIRE(after.valid);
  REQUIRE(after.physicalTag == 0);
  REQUIRE(after.words[0] == 0);
  REQUIRE(after.words[3] == 0x11223344);
  const auto filled = after;
  REQUIRE(cache.readInstruction(bus, 0x1d).status == IOPBusStatus::Misaligned);
  REQUIRE(after.valid == filled.valid);
  REQUIRE(after.physicalTag == filled.physicalTag);
  REQUIRE(after.words == filled.words);
}

TEST_CASE("IOP instruction fills stay within the final line of each physical region",
  "[iop][cache][fetch]")
{
  for (const IOPAddress physical :
       {0x001ffffcu, 0x003ffffcu, 0x005ffffcu, 0x007ffffcu, 0x1ffffffcu})
  {
    IOPBus bus;
    IOPCore core;
    const bool rom = physical >= IOPBus::ROM_BASE;
    if (rom)
    {
      std::vector<std::uint8_t> bytes(IOPBus::ROM_SIZE, 0x12);
      REQUIRE(bus.installRom(BootROMImage::create(bytes)));
    }
    else
    {
      REQUIRE(bus.write32(physical, 0x12345678).status == IOPBusStatus::Completed);
    }
    core.attachBus(&bus);
    core.startExecution(physical | 0x80000000);
    const auto read = core.fetchInstruction();
    REQUIRE(read.outcome == IOPAccessOutcome::Completed);
    REQUIRE(read.value == (rom ? 0x12121212u : 0x12345678u));
    const auto location = IOPCache::location(IOPCacheBank::Instruction, physical);
    const auto &line = core.cache().instructionLine(location.lineIndex);
    REQUIRE(line.valid);
    REQUIRE(line.physicalTag == location.physicalTag);
    REQUIRE(line.words[3] == read.value);
  }
}

TEST_CASE("IOP cache mode guards precede ROM synchronization and direct access",
  "[iop][cache][fetch][admission]")
{
  for (const bool isolated : {false, true})
  {
    for (unsigned access = 0; access < 7; ++access)
    {
      if (isolated && access == 0)
      {
        continue; // Isolation does not affect fetch.
      }
      IOPBus bus;
      IOPCore core;
      auto rom = makeRom(0x12);
      const std::weak_ptr<const BootROMImage> oldImage = rom;
      REQUIRE(bus.installRom(rom));
      core.attachBus(&bus);
      rom.reset();
      core.startExecution(0x80000100);
      IOPCacheTestAccess::seed(&core);
      const IOPWord status = IOPCOP0Status::CACHE_PROBE_RESULT |
        (isolated ? IOPCOP0Status::CACHE_ISOLATED
                  : IOPCOP0Status::CACHES_SWAPPED);
      IOPCacheTestAccess::setStatus(&core, status);
      REQUIRE(bus.write32(0x100, 0x11223344).status == IOPBusStatus::Completed);
      REQUIRE(bus.installRom(makeRom(0x34)));
      IOPAccessOutcome outcome = IOPAccessOutcome::Completed;
      IOPException exception = IOPException::InstructionBusError;
      switch (access)
      {
        case 0:
        case 1:
        case 2:
        case 3:
        {
          const auto read = access == 0 ? core.fetchInstruction()
            : access == 1 ? core.readData8(0x100)
            : access == 2 ? core.readData16(0x100)
                          : core.readData32(0x100);
          outcome = read.outcome;
          exception = read.exception;
          break;
        }
        default:
        {
          const auto write = access == 4 ? core.writeData8(0x100, 0x55)
            : access == 5 ? core.writeData16(0x100, 0x5566)
                          : core.writeData32(0x100, 0x55667788);
          outcome = write.outcome;
          exception = write.exception;
          break;
        }
      }
      REQUIRE(outcome == IOPAccessOutcome::UnsupportedCacheMode);
      REQUIRE(exception == IOPException::None);
      REQUIRE(core.status() == status);
      REQUIRE(bus.read32(0x100).value == 0x11223344);
      REQUIRE_FALSE(oldImage.expired());
      requireSeeded(core.cache());
      REQUIRE(core.elapsedCycles() == 0);
      REQUIRE(core.retiredInstructions() == 0);
      REQUIRE(core.executionState() == IOPExecutionState::Running);
    }
  }
}

TEST_CASE("IOP unsupported cache modes stop without exceptions or retirement effects",
  "[iop][cache][fetch][admission][execution]")
{
  // Fetch, all implemented load families, and every physical store boundary.
  const IOPWord instructions[] = {
    0, 0x80000100, 0x84000100, 0x8c000100, 0x88000101, 0x98000101,
    0xa0000100, 0xa4000100, 0xac000100, 0xa8000101, 0xb8000101
  };
  for (const bool isolated : {false, true})
  {
    for (const auto instruction : instructions)
    {
      if (isolated && instruction == 0)
      {
        continue;
      }
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      REQUIRE(bus.write32(0, instruction).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(0x100, 0x11223344).status == IOPBusStatus::Completed);
      core.startExecution(instruction == 0 ? 0x80000000 : 0xa0000000);
      IOPCacheTestAccess::seed(&core);
      IOPCacheTestAccess::setContinuations(&core);
      const IOPWord status = IOPCOP0Status::CACHE_PROBE_RESULT |
        (isolated ? IOPCOP0Status::CACHE_ISOLATED
                  : IOPCOP0Status::CACHES_SWAPPED);
      IOPCacheTestAccess::setStatus(&core, status);
      const auto pc = core.programCounter();
      const auto branch = core.branchContinuation();
      const auto delayed = core.delayedResultContinuation();
      core.stepInstruction();
      REQUIRE(core.executionState() == IOPExecutionState::Halted);
      REQUIRE(core.stopReason() == IOPStopReason::UnsupportedCacheMode);
      REQUIRE(core.exception().kind == IOPException::None);
      REQUIRE(core.storeFailure().width == IOPStoreWidth::None);
      REQUIRE(core.programCounter() == pc);
      REQUIRE(core.status() == status);
      REQUIRE(core.cause() == 0);
      REQUIRE(core.exceptionProgramCounter() == 0);
      REQUIRE(core.generalRegister(7) == 0);
      REQUIRE(core.branchContinuation().active == branch.active);
      REQUIRE(core.branchContinuation().instructionAddress == branch.instructionAddress);
      REQUIRE(core.branchContinuation().targetAddress == branch.targetAddress);
      REQUIRE(core.delayedResultContinuation().source == delayed.source);
      REQUIRE(core.delayedResultContinuation().destination == delayed.destination);
      REQUIRE(core.delayedResultContinuation().value == delayed.value);
      REQUIRE(core.retiredInstructions() == 0);
      REQUIRE(core.elapsedCycles() == 1);
      REQUIRE(bus.read32(0x100).value == 0x11223344);
      requireSeeded(core.cache());
    }
  }
}

TEST_CASE("IOP isolation guards cover uncached and non-memory data targets",
  "[iop][cache][fetch][admission]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHE_ISOLATED);
  IOPCacheTestAccess::seed(&core);
  for (const IOPAddress address :
       {0xa0000100u, 0x1f800000u, 0xbfc00000u, 0x00800000u, 0xffffffffu})
  {
    REQUIRE(core.readData8(address).outcome == IOPAccessOutcome::UnsupportedCacheMode);
    REQUIRE(core.writeData8(address, 0x55).outcome ==
      IOPAccessOutcome::UnsupportedCacheMode);
    requireSeeded(core.cache());
  }
}

TEST_CASE("IOP protection and alignment faults precede cache mode admission",
  "[iop][cache][fetch][admission]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  const IOPWord mode = IOPCOP0Status::CACHE_ISOLATED |
    IOPCOP0Status::CACHES_SWAPPED | IOPCOP0Status::CACHE_PROBE_RESULT;
  IOPCacheTestAccess::setStatus(&core, mode);
  IOPCacheTestAccess::seed(&core);
  core.startExecution(0x101);
  REQUIRE(core.fetchInstruction().outcome == IOPAccessOutcome::Misaligned);
  REQUIRE(core.readData32(0x101).outcome == IOPAccessOutcome::Misaligned);
  REQUIRE(core.writeData32(0x101, 0).outcome == IOPAccessOutcome::Misaligned);
  IOPCacheTestAccess::setStatus(&core, mode | IOPCOP0Status::CURRENT_USER_MODE);
  core.startExecution(0x80000100);
  REQUIRE(core.fetchInstruction().outcome == IOPAccessOutcome::ProtectionFailure);
  REQUIRE(core.readData32(0x80000100).outcome == IOPAccessOutcome::ProtectionFailure);
  REQUIRE(core.writeData32(0x80000100, 0).outcome == IOPAccessOutcome::ProtectionFailure);
  requireSeeded(core.cache());
}

TEST_CASE("IOP swapped bypass fetch preserves banks and ordinary bus fault selection",
  "[iop][cache][fetch][admission]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCacheTestAccess::seed(&core);
  IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHES_SWAPPED);
  for (const IOPAddress address : {0xa0000100u, 0x1f800000u})
  {
    const auto physical = address & 0x1fffffff;
    REQUIRE(bus.write32(physical, 0x11223344).status == IOPBusStatus::Completed);
    core.startExecution(address);
    REQUIRE(core.fetchInstruction().value == 0x11223344);
    REQUIRE(core.readData32(address).value == 0x11223344);
    REQUIRE(core.writeData32(address, 0x55667788).outcome ==
      IOPAccessOutcome::Completed);
    requireSeeded(core.cache());
  }
  core.startExecution(0x00800000);
  const auto fetch = core.fetchInstruction();
  REQUIRE(fetch.outcome == IOPAccessOutcome::Unmapped);
  REQUIRE(fetch.exception == IOPException::InstructionBusError);
  const auto load = core.readData32(0x00800000);
  REQUIRE(load.outcome == IOPAccessOutcome::Unmapped);
  REQUIRE(load.exception == IOPException::DataBusError);
  requireSeeded(core.cache());
}
