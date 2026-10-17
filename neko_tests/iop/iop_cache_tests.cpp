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

TEST_CASE("IOP cache storage does not route guest traffic through seeded entries",
  "[iop][cache][execution]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  REQUIRE(bus.write32(0x100, 0x11223344).status == IOPBusStatus::Completed);
  IOPCacheTestAccess::seed(&core);
  core.startExecution(0x80000100);
  REQUIRE(core.fetchInstruction().value == 0x11223344);
  REQUIRE(core.readData32(0x100).value == 0x11223344);
  REQUIRE(core.readData32(0x80000100).value == 0x11223344);
  REQUIRE(core.readData32(0xa0000100).value == 0x11223344);
  REQUIRE(core.writeData32(0x80000100, 0xaabbccdd).outcome ==
    IOPAccessOutcome::Completed);
  REQUIRE(bus.read32(0x100).value == 0xaabbccdd);
  requireSeeded(core.cache());
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
