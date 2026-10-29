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
#include "iop_interrupt_controller.hpp"
#include "iop_timers.hpp"

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

  static IOPMemoryWriteResult writeStore(
    IOPCore *core, const IOPStoreRequest &request)
  {
    return core->writeStore(request);
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
  void requireUnchangedCache(
    const IOPCache &cache,
    const IOPCache &before,
    std::size_t changedDataIndex = IOPCache::DATA_LINE_COUNT,
    std::size_t changedInstructionIndex = IOPCache::INSTRUCTION_LINE_COUNT)
  {
    for (std::size_t index = 0;
         index < IOPCache::INSTRUCTION_LINE_COUNT; ++index)
    {
      if (index == changedInstructionIndex)
      {
        continue;
      }
      const auto &line = cache.instructionLine(index);
      const auto &old = before.instructionLine(index);
      REQUIRE(line.valid == old.valid);
      REQUIRE(line.physicalTag == old.physicalTag);
      REQUIRE(line.words == old.words);
    }
    for (std::size_t index = 0; index < IOPCache::DATA_LINE_COUNT; ++index)
    {
      if (index == changedDataIndex)
      {
        continue;
      }
      const auto &line = cache.dataLine(index);
      const auto &old = before.dataLine(index);
      REQUIRE(line.valid == old.valid);
      REQUIRE(line.physicalTag == old.physicalTag);
      REQUIRE(line.words == old.words);
    }
  }

  template<std::size_t WordCount>
  void requireIndexedStore(
    const IOPCacheLine<WordCount> &line,
    const IOPCacheLine<WordCount> &before,
    const IOPCacheLocation &location,
    IOPWord value,
    std::uint8_t byteEnable)
  {
    REQUIRE(line.valid == (byteEnable == 0xf));
    REQUIRE(line.physicalTag == (byteEnable == 0xf
      ? location.physicalTag : before.physicalTag));
    for (std::size_t word = 0; word < WordCount; ++word)
    {
      REQUIRE(line.words[word] == (byteEnable == 0xf && word == location.wordIndex
        ? value : before.words[word]));
    }
  }

  void requireInvalidatedGroup(
    const IOPCache &cache,
    const IOPCache &before,
    IOPWord profile,
    std::size_t groupOffset)
  {
    for (std::size_t index = 0; index < IOPCache::INSTRUCTION_LINE_COUNT; ++index)
    {
      const auto &line = cache.instructionLine(index);
      const auto &old = before.instructionLine(index);
      REQUIRE(line.valid == ((profile == 0xc04 || profile == 0xcc4) &&
        index == (groupOffset & 0xfff) / 16 ? false : old.valid));
      REQUIRE(line.physicalTag == old.physicalTag);
      REQUIRE(line.words == old.words);
    }
    for (std::size_t index = 0; index < IOPCache::DATA_LINE_COUNT; ++index)
    {
      const auto &line = cache.dataLine(index);
      const auto &old = before.dataLine(index);
      REQUIRE(line.valid == ((profile == 0xc4 || profile == 0xcc4) &&
        index >= (groupOffset & 0x3ff) / 4 &&
        index < (groupOffset & 0x3ff) / 4 + 4
          ? false : old.valid));
      REQUIRE(line.physicalTag == old.physicalTag);
      REQUIRE(line.words == old.words);
    }
  }

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

  void requireSameCoreState(const IOPCore &actual, const IOPCore &expected)
  {
    for (std::size_t index = 0; index < IOPCore::GENERAL_REGISTER_COUNT; ++index)
    {
      CAPTURE(index);
      REQUIRE(actual.generalRegister(index) == expected.generalRegister(index));
    }
    REQUIRE(actual.programCounter() == expected.programCounter());
    REQUIRE(actual.hi() == expected.hi());
    REQUIRE(actual.lo() == expected.lo());
    REQUIRE(actual.status() == expected.status());
    REQUIRE(actual.cause() == expected.cause());
    REQUIRE(actual.badVirtualAddress() == expected.badVirtualAddress());
    REQUIRE(actual.exceptionProgramCounter() == expected.exceptionProgramCounter());
    REQUIRE(actual.processorRevision() == expected.processorRevision());
    REQUIRE(actual.executionState() == expected.executionState());
    REQUIRE(actual.stopReason() == expected.stopReason());
    REQUIRE(actual.elapsedCycles() == expected.elapsedCycles());
    REQUIRE(actual.retiredInstructions() == expected.retiredInstructions());
    const auto &branch = actual.branchContinuation();
    const auto &expectedBranch = expected.branchContinuation();
    REQUIRE(branch.active == expectedBranch.active);
    REQUIRE(branch.instructionAddress == expectedBranch.instructionAddress);
    REQUIRE(branch.targetAddress == expectedBranch.targetAddress);
    const auto &delayed = actual.delayedResultContinuation();
    const auto &expectedDelayed = expected.delayedResultContinuation();
    REQUIRE(delayed.source == expectedDelayed.source);
    REQUIRE(delayed.destination == expectedDelayed.destination);
    REQUIRE(delayed.value == expectedDelayed.value);
    REQUIRE(actual.exception().kind == expected.exception().kind);
    REQUIRE(actual.exception().badVirtualAddress ==
      expected.exception().badVirtualAddress);
    REQUIRE(actual.exception().inBranchDelaySlot ==
      expected.exception().inBranchDelaySlot);
    REQUIRE(actual.exception().coprocessor == expected.exception().coprocessor);
    const auto &failure = actual.storeFailure();
    const auto &expectedFailure = expected.storeFailure();
    REQUIRE(failure.width == expectedFailure.width);
    REQUIRE(failure.value == expectedFailure.value);
    REQUIRE(failure.mergeByteEnable == expectedFailure.mergeByteEnable);
    REQUIRE(failure.access.outcome == expectedFailure.access.outcome);
    REQUIRE(failure.access.exception == expectedFailure.access.exception);
    REQUIRE(failure.access.virtualAddress == expectedFailure.access.virtualAddress);
    REQUIRE(failure.access.physicalAddress == expectedFailure.access.physicalAddress);
    REQUIRE(failure.access.cacheRoute == expectedFailure.access.cacheRoute);
    requireUnchangedCache(actual.cache(), expected.cache());
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

TEST_CASE("IOP timer MMIO bypass stays live across aliases and cache bank swapping",
  "[iop][cache][timer]")
{
  for (const bool swapped : {false, true})
  {
    for (std::size_t index = 0; index < 6; ++index)
    {
      const IOPAddress base = index < 3 ?
        0x1f801100 + static_cast<IOPAddress>(index * 16) :
        0x1f801480 + static_cast<IOPAddress>((index - 3) * 16);
      for (IOPAddress alias : {0u, 0x80000000u, 0xa0000000u})
      {
        CAPTURE(index);
        CAPTURE(alias);
        CAPTURE(swapped);
        IOPBus bus;
        IOPTimers timers;
        bus.attachTimers(&timers);
        timers.writeMode(index, 0x10);
        timers.writeTarget(index, 1);
        REQUIRE(timers.advance({true, false, {}}).count == 1);
        IOPCore core;
        core.attachBus(&bus);
        IOPCacheTestAccess::seed(&core);
        IOPCacheTestAccess::setStatus(&core,
          swapped ? IOPCOP0Status::CACHES_SWAPPED : 0);
        const IOPCache before = core.cache();
        const IOPAddress address = (base | alias) + 4;
        REQUIRE(core.readData8(address).outcome == IOPAccessOutcome::Unmapped);
        REQUIRE(core.readData16(address + 2).outcome == IOPAccessOutcome::Unmapped);
        REQUIRE(core.writeData8(address, 0).outcome == IOPAccessOutcome::Unmapped);
        REQUIRE(core.writeData32(address + 1, 0).outcome ==
          IOPAccessOutcome::Misaligned);
        REQUIRE(timers.counter(index).mode == 0x0810);
        REQUIRE(core.writeData32(base | alias, 0x12345678).outcome ==
          IOPAccessOutcome::Completed);
        REQUIRE(timers.counter(index).mode == 0x0810);
        const auto mode = core.readData16(address);
        REQUIRE(mode.outcome == IOPAccessOutcome::Completed);
        REQUIRE(mode.physicalAddress == base + 4);
        REQUIRE(mode.value == 0x0810);
        REQUIRE(timers.counter(index).mode == 0x0410);
        REQUIRE(core.readData32(address).value == 0x0410);
        REQUIRE(core.writeData16(address, 0).outcome == IOPAccessOutcome::Completed);
        REQUIRE(timers.counter(index).count == 0);
        requireUnchangedCache(core.cache(), before);
      }
    }
  }
}

TEST_CASE("IOP timer MMIO cache isolation cannot acknowledge or create later device hits",
  "[iop][cache][timer]")
{
  for (const bool swapped : {false, true})
  {
    for (IOPAddress mode : {0x1f801104u, 0x9f801104u, 0xbf801104u,
         0x1f801484u, 0x9f801484u, 0xbf801484u})
    {
      CAPTURE(mode);
      CAPTURE(swapped);
      const std::size_t index = (mode & 0xfff) == 0x104 ? 0 : 3;
      IOPBus bus;
      IOPTimers timers;
      bus.attachTimers(&timers);
      timers.writeMode(index, 0x10);
      timers.writeTarget(index, 1);
      timers.advance({true, false, {}});
      IOPCore core;
      core.attachBus(&bus);
      const IOPWord swap = swapped ? IOPCOP0Status::CACHES_SWAPPED : 0;
      IOPCacheTestAccess::setStatus(&core, swap | IOPCOP0Status::CACHE_ISOLATED);
      REQUIRE(core.writeData32(mode, 0x11223344).outcome == IOPAccessOutcome::Completed);
      REQUIRE(core.readData32(mode).value == 0x11223344);
      REQUIRE(timers.counter(index).mode == 0x0810);
      const IOPCache before = core.cache();
      IOPCacheTestAccess::setStatus(&core, swap);
      REQUIRE(core.readData32(mode).value == 0x0810);
      REQUIRE(core.readData32(mode).value == 0x0410);
      requireUnchangedCache(core.cache(), before);
    }
  }
}

TEST_CASE("IOP timer MMIO protection and instruction fetch precede data cache policy",
  "[iop][cache][timer]")
{
  for (const bool swapped : {false, true})
  {
    for (IOPAddress address : {0x9f801104u, 0xbf801104u, 0x9f801484u, 0xbf801484u})
    {
      const std::size_t index = (address & 0xfff) == 0x104 ? 0 : 3;
      IOPBus bus;
      IOPTimers timers;
      bus.attachTimers(&timers);
      timers.writeMode(index, 0x10);
      timers.writeTarget(index, 1);
      timers.advance({true, false, {}});
      IOPCore core;
      core.attachBus(&bus);
      core.startExecution(address);
      IOPCacheTestAccess::seed(&core);
      const IOPCache before = core.cache();
      const IOPWord swap = swapped ? IOPCOP0Status::CACHES_SWAPPED : 0;
      IOPCacheTestAccess::setStatus(&core, swap | IOPCOP0Status::CURRENT_USER_MODE);
      REQUIRE(core.fetchInstruction().outcome == IOPAccessOutcome::ProtectionFailure);
      REQUIRE(core.readData32(address).outcome == IOPAccessOutcome::ProtectionFailure);
      REQUIRE(core.writeData16(address, 0).outcome == IOPAccessOutcome::ProtectionFailure);
      REQUIRE(timers.counter(index).mode == 0x0810);
      IOPCacheTestAccess::setStatus(&core, swap | IOPCOP0Status::CACHE_ISOLATED);
      REQUIRE(core.fetchInstruction().value == 0x0810);
      REQUIRE(core.fetchInstruction().value == 0x0410);
      requireUnchangedCache(core.cache(), before);
    }
  }
}

TEST_CASE("IOP cache bypass keeps INTC traffic live across aliases and bank swapping",
  "[iop][cache][intc]")
{
  for (const bool swapped : {false, true})
  {
    for (IOPAddress controlAddress : {0x1f801078u, 0x9f801078u, 0xbf801078u})
    {
      CAPTURE(swapped);
      CAPTURE(controlAddress);
      IOPBus bus;
      IOPInterruptController interrupts;
      bus.attachInterruptController(&interrupts);
      interrupts.assertSource(IOPInterruptSource::Timer0);
      interrupts.writeMask(0x10);
      interrupts.writeControl(1);
      IOPCore core;
      core.attachBus(&bus);
      IOPCacheTestAccess::seed(&core);
      IOPCacheTestAccess::setStatus(&core,
        swapped ? IOPCOP0Status::CACHES_SWAPPED : 0);
      const IOPCache before = core.cache();

      const auto status = core.readData32(controlAddress - 8);
      REQUIRE(status.outcome == IOPAccessOutcome::Completed);
      REQUIRE(status.value == 0x10);
      REQUIRE(core.writeData32(controlAddress - 4, 0xffffffff).outcome ==
        IOPAccessOutcome::Completed);
      REQUIRE(interrupts.mask() == 0x03ffffff);
      REQUIRE(core.writeData32(controlAddress - 8, 0).outcome ==
        IOPAccessOutcome::Completed);
      REQUIRE(interrupts.status() == 0);

      const auto byte = core.readData8(controlAddress);
      REQUIRE(byte.outcome == IOPAccessOutcome::Unmapped);
      REQUIRE(byte.exception == IOPException::DataBusError);
      REQUIRE(core.readData16(controlAddress + 2).outcome == IOPAccessOutcome::Unmapped);
      REQUIRE(core.writeData8(controlAddress, 0).outcome == IOPAccessOutcome::Unmapped);
      REQUIRE(core.writeData16(controlAddress, 0).outcome == IOPAccessOutcome::Unmapped);
      REQUIRE(core.readData32(controlAddress + 1).outcome == IOPAccessOutcome::Misaligned);
      REQUIRE(core.writeData32(controlAddress + 1, 0).outcome ==
        IOPAccessOutcome::Misaligned);
      REQUIRE(interrupts.control() == 1);
      const auto control = core.readData32(controlAddress);
      REQUIRE(control.outcome == IOPAccessOutcome::Completed);
      REQUIRE(control.physicalAddress == 0x1f801078);
      REQUIRE(control.value == 1);
      REQUIRE(interrupts.control() == 0);
      REQUIRE(core.readData32(controlAddress).value == 0);
      requireUnchangedCache(core.cache(), before);
    }
  }
}

TEST_CASE("IOP cache isolation prevents INTC side effects and cannot create MMIO cache hits",
  "[iop][cache][intc]")
{
  for (const bool swapped : {false, true})
  {
    for (IOPAddress address : {0x1f801078u, 0x9f801078u, 0xbf801078u})
    {
      CAPTURE(swapped);
      CAPTURE(address);
      IOPBus bus;
      IOPInterruptController interrupts;
      bus.attachInterruptController(&interrupts);
      interrupts.writeControl(1);
      IOPCore core;
      core.attachBus(&bus);
      const IOPWord swap = swapped ? IOPCOP0Status::CACHES_SWAPPED : 0;
      IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHE_ISOLATED | swap);
      REQUIRE(core.writeData32(address, 0x11223344).outcome ==
        IOPAccessOutcome::Completed);
      REQUIRE(core.readData32(address).value == 0x11223344);
      REQUIRE(interrupts.control() == 1);
      const IOPCache before = core.cache();

      IOPCacheTestAccess::setStatus(&core, swap);
      const auto read = core.readData32(address);
      REQUIRE(read.outcome == IOPAccessOutcome::Completed);
      REQUIRE(read.value == 1);
      REQUIRE(interrupts.control() == 0);
      REQUIRE(core.writeData32(address, 1).outcome == IOPAccessOutcome::Completed);
      REQUIRE(interrupts.control() == 1);
      requireUnchangedCache(core.cache(), before);
    }
  }
}

TEST_CASE("IOP INTC fetch routing preserves protection before device side effects",
  "[iop][cache][intc]")
{
  for (const bool swapped : {false, true})
  {
    for (IOPAddress address : {0x9f801078u, 0xbf801078u})
    {
      CAPTURE(swapped);
      CAPTURE(address);
      IOPBus bus;
      IOPInterruptController interrupts;
      bus.attachInterruptController(&interrupts);
      interrupts.writeControl(1);
      IOPCore core;
      core.attachBus(&bus);
      core.startExecution(address);
      IOPCacheTestAccess::seed(&core);
      const IOPCache before = core.cache();
      const IOPWord swap = swapped ? IOPCOP0Status::CACHES_SWAPPED : 0;
      IOPCacheTestAccess::setStatus(&core, swap | IOPCOP0Status::CURRENT_USER_MODE);
      REQUIRE(core.fetchInstruction().outcome == IOPAccessOutcome::ProtectionFailure);
      REQUIRE(core.readData32(address).outcome == IOPAccessOutcome::ProtectionFailure);
      REQUIRE(core.writeData32(address, 0).outcome == IOPAccessOutcome::ProtectionFailure);
      REQUIRE(interrupts.control() == 1);
      requireUnchangedCache(core.cache(), before);

      IOPCacheTestAccess::setStatus(&core, swap | IOPCOP0Status::CACHE_ISOLATED);
      const auto fetch = core.fetchInstruction();
      REQUIRE(fetch.outcome == IOPAccessOutcome::Completed);
      REQUIRE(fetch.value == 1);
      REQUIRE(interrupts.control() == 0);
      REQUIRE(core.fetchInstruction().value == 0);
      requireUnchangedCache(core.cache(), before);
    }
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

TEST_CASE("IOP cache access planning prioritizes isolation and selects physical banks",
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
    IOPBusRegion::Scratchpad, IOPBusRegion::Rom, IOPBusRegion::Mmio
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
            const auto expected = !fetch && isolated
              ? IOPCacheAccessDisposition::Isolated
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

TEST_CASE("IOP core reconciles ROM reconfiguration through every admitted access family",
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
    if (access == 6)
    {
      const auto location = IOPCache::location(IOPCacheBank::Data, 0x100);
      const auto &line = core.cache().dataLine(location.lineIndex);
      REQUIRE(line.valid);
      REQUIRE(line.physicalTag == location.physicalTag);
      REQUIRE(line.words[0] == 0x56789abc);
      requireUnchangedCache(core.cache(), IOPCache{}, location.lineIndex);
    }
    else
    {
      requireClear(core.cache());
    }
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
  REQUIRE(core.writeData32(0xa0000100, 0xaabbccdd).outcome ==
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

TEST_CASE("IOP cached scalar stores write through with word allocation and partial hit merges",
  "[iop][cache][data-store]")
{
  for (unsigned initial = 0; initial < 3; ++initial)
  {
    for (const unsigned width : {1u, 2u, 4u})
    {
      for (IOPAddress offset = 0; offset < 4; offset += width)
      {
        CAPTURE(initial);
        CAPTURE(width);
        CAPTURE(offset);
        IOPBus bus;
        IOPCore core;
        core.attachBus(&bus);
        IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHE_PROBE_RESULT);
        REQUIRE(bus.write32(0x200, 0x11223344).status == IOPBusStatus::Completed);
        if (initial != 0)
        {
          const IOPAddress fill = initial == 1 ? 0x200 : 0x600;
          REQUIRE(bus.write32(fill, 0x11223344).status == IOPBusStatus::Completed);
          REQUIRE(core.readData32(fill).value == 0x11223344);
        }
        REQUIRE(core.readData32(0x204).outcome == IOPAccessOutcome::Completed);
        core.startExecution(0x200);
        REQUIRE(core.fetchInstruction().value == 0x11223344);
        REQUIRE(bus.write32(0x200, 0xaabbccdd).status == IOPBusStatus::Completed);
        const auto before = core.cache();
        const IOPAddress address = 0x80000200 + offset;
        const auto write = width == 1 ? core.writeData8(address, 0x78)
          : width == 2 ? core.writeData16(address, 0x5678)
                       : core.writeData32(address, 0x12345678);
        const IOPWord mask = width == 1 ? 0xff : width == 2 ? 0xffff : UINT32_MAX;
        const IOPWord laneMask = mask << (8 * offset);
        const IOPWord value = (0x12345678 & mask) << (8 * offset);
        REQUIRE(write.outcome == IOPAccessOutcome::Completed);
        REQUIRE(write.exception == IOPException::None);
        REQUIRE(write.virtualAddress == address);
        REQUIRE(write.physicalAddress == 0x200 + offset);
        REQUIRE(write.cacheRoute == IOPCacheRoute::Cached);
        REQUIRE(bus.read32(0x200).value == ((0xaabbccdd & ~laneMask) | value));
        const auto location = IOPCache::location(IOPCacheBank::Data, 0x200);
        const auto &line = core.cache().dataLine(location.lineIndex);
        if (width == 4 || initial == 1)
        {
          REQUIRE(line.valid);
          REQUIRE(line.physicalTag == location.physicalTag);
          REQUIRE(line.words[0] == ((0x11223344 & ~laneMask) | value));
          requireUnchangedCache(core.cache(), before, location.lineIndex);
        }
        else
        {
          requireUnchangedCache(core.cache(), before);
        }
        REQUIRE(core.status() == IOPCOP0Status::CACHE_PROBE_RESULT);
        REQUIRE(core.elapsedCycles() == 0);
        REQUIRE(core.retiredInstructions() == 0);
        REQUIRE(core.fetchInstruction().value == 0x11223344);
      }
    }
  }
}

TEST_CASE("IOP cached merge stores use effective byte lanes for allocation and stale hit updates",
  "[iop][cache][data-store][merge][execution]")
{
  for (unsigned initial = 0; initial < 3; ++initial)
  {
    for (const bool left : {false, true})
    {
      for (IOPAddress offset = 0; offset < 4; ++offset)
      {
        CAPTURE(initial);
        CAPTURE(left);
        CAPTURE(offset);
        IOPBus bus;
        IOPCore core;
        core.attachBus(&bus);
        REQUIRE(bus.write32(0x200, 0x11223344).status == IOPBusStatus::Completed);
        if (initial != 0)
        {
          const IOPAddress fill = initial == 1 ? 0x200 : 0x600;
          REQUIRE(bus.write32(fill, 0x11223344).status == IOPBusStatus::Completed);
          REQUIRE(core.readData32(fill).value == 0x11223344);
        }
        REQUIRE(bus.write32(0x200, 0xaabbccdd).status == IOPBusStatus::Completed);
        const IOPWord instruction = ((left ? 0x2au : 0x2eu) << 26) |
          (1u << 21) | (2u << 16) | offset;
        REQUIRE(bus.write32(0x100, instruction).status == IOPBusStatus::Completed);
        IOPCacheTestAccess::setGeneralRegister(&core, 1, 0x80000200);
        IOPCacheTestAccess::setGeneralRegister(&core, 2, 0x87654321);
        core.startExecution(0xa0000100);
        IOPCacheTestAccess::setContinuations(&core);
        IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHE_PROBE_RESULT);
        const auto before = core.cache();
        core.stepInstruction();
        IOPWord expectedMemory = 0xaabbccdd;
        IOPWord expectedCache = 0x11223344;
        for (unsigned lane = 0; lane < 4; ++lane)
        {
          if (left ? lane <= offset : lane >= offset)
          {
            const unsigned source = left ? 3 - offset + lane : lane - offset;
            const IOPWord byte = (0x87654321u >> (source * 8)) & 0xff;
            const IOPWord laneMask = 0xffu << (lane * 8);
            expectedMemory = (expectedMemory & ~laneMask) | (byte << (lane * 8));
            expectedCache = (expectedCache & ~laneMask) | (byte << (lane * 8));
          }
        }
        REQUIRE(bus.read32(0x200).value == expectedMemory);
        const auto location = IOPCache::location(IOPCacheBank::Data, 0x200);
        if (initial == 1 || (left ? offset == 3 : offset == 0))
        {
          const auto &line = core.cache().dataLine(location.lineIndex);
          REQUIRE(line.valid);
          REQUIRE(line.physicalTag == location.physicalTag);
          REQUIRE(line.words[0] == expectedCache);
          requireUnchangedCache(core.cache(), before, location.lineIndex);
        }
        else
        {
          requireUnchangedCache(core.cache(), before);
        }
        REQUIRE(core.exception().kind == IOPException::None);
        REQUIRE(core.storeFailure().width == IOPStoreWidth::None);
        REQUIRE(core.programCounter() == 0x200);
        REQUIRE_FALSE(core.branchContinuation().active);
        REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
        REQUIRE(core.generalRegister(7) == 0x12345678);
        REQUIRE(core.status() == IOPCOP0Status::CACHE_PROBE_RESULT);
        REQUIRE(core.retiredInstructions() == 1);
        REQUIRE(core.elapsedCycles() == 1);
      }
    }
  }
}

TEST_CASE("IOP bypass stores leave both cache banks untouched at every width and merge mask",
  "[iop][cache][data-store]")
{
  for (const IOPAddress address :
       {0xa0000200u, 0x1f800200u, 0x9f800200u, 0xbf800200u})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    IOPCacheTestAccess::seed(&core);
    const auto before = core.cache();
    for (const unsigned width : {1u, 2u, 4u})
    {
      for (IOPAddress offset = 0; offset < 4; offset += width)
      {
        const auto write = width == 1 ? core.writeData8(address + offset, 0x78)
          : width == 2 ? core.writeData16(address + offset, 0x5678)
                       : core.writeData32(address, 0x12345678);
        REQUIRE(write.outcome == IOPAccessOutcome::Completed);
        requireUnchangedCache(core.cache(), before);
      }
    }
    for (const bool left : {false, true})
    {
      for (IOPAddress offset = 0; offset < 4; ++offset)
      {
        const std::uint8_t mask = left ? 0xf >> (3 - offset)
          : (0xf << offset) & 0xf;
        REQUIRE(IOPCacheTestAccess::writeStore(&core,
          {IOPStoreOperation::MaskedWord, address + offset, 0x87654321, mask})
          .outcome == IOPAccessOutcome::Completed);
        requireUnchangedCache(core.cache(), before);
      }
    }
  }
}

TEST_CASE("IOP data store commits validate inputs and preserve every unselected byte",
  "[iop][cache][data-store]")
{
  for (const IOPAddress address : {0x200000u, 0x4u, 0x200004u})
  {
    for (std::uint8_t byteEnable = 1; byteEnable <= 0xf; ++byteEnable)
    {
      IOPCache cache;
      IOPCacheTestAccess::seed(&cache);
      const auto before = cache;
      const auto location = IOPCache::location(IOPCacheBank::Data, address);
      const auto &old = before.dataLine(location.lineIndex);
      cache.commitStore(IOPCacheBank::Data, address, 0x89abcdef, byteEnable);
      const bool update = byteEnable == 0xf ||
        (old.valid && old.physicalTag == location.physicalTag);
      if (update)
      {
        const auto &line = cache.dataLine(location.lineIndex);
        REQUIRE(line.valid);
        REQUIRE(line.physicalTag == location.physicalTag);
        for (unsigned lane = 0; lane < 4; ++lane)
        {
          const IOPWord expected = (byteEnable & (1u << lane)) != 0
            ? 0x89abcdef : old.words[0];
          REQUIRE(((line.words[0] >> (lane * 8)) & 0xff) ==
            ((expected >> (lane * 8)) & 0xff));
        }
        requireUnchangedCache(cache, before, location.lineIndex);
      }
      else
      {
        requireUnchangedCache(cache, before);
      }
    }
  }
  IOPCache cache;
  IOPCacheTestAccess::seed(&cache);
  const auto before = cache;
  for (const std::uint8_t mask : {0, 0x10, 0xff})
  {
    REQUIRE_THROWS_AS(cache.commitStore(IOPCacheBank::Data, 4, 0x12345678, mask),
      std::invalid_argument);
    requireUnchangedCache(cache, before);
  }
  for (const IOPAddress address : {1u, 2u, 3u, 0xffffffffu})
  {
    REQUIRE_THROWS_AS(cache.commitStore(IOPCacheBank::Data, address, 0x12345678, 0xf),
      std::invalid_argument);
    requireUnchangedCache(cache, before);
  }
}

TEST_CASE("IOP stores preserve stale external writes and distinguish physical RAM mirror tags",
  "[iop][cache][data-store]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  REQUIRE(core.writeData32(0x80000200, 0x11223344).outcome == IOPAccessOutcome::Completed);
  const auto location = IOPCache::location(IOPCacheBank::Data, 0x200);
  REQUIRE(core.cache().dataLine(location.lineIndex).valid);
  const std::uint8_t bytes[] = {0xdd, 0xcc, 0xbb, 0xaa};
  REQUIRE(bus.loadPhysical(0x200, bytes, sizeof(bytes)) == IOPBusStatus::Completed);
  REQUIRE(core.readData32(0x200).value == 0x11223344);
  REQUIRE(core.writeData8(0x80200201, 0x55).outcome == IOPAccessOutcome::Completed);
  REQUIRE(core.cache().dataLine(location.lineIndex).physicalTag == 0);
  REQUIRE(core.readData32(0x80000200).value == 0x11223344);
  REQUIRE(bus.read32(0x200).value == 0xaabb55dd);
  REQUIRE(core.writeData32(0x80200200, 0x55667788).outcome == IOPAccessOutcome::Completed);
  REQUIRE(core.cache().dataLine(location.lineIndex).physicalTag == 0x200000);
  REQUIRE(core.writeData32(0xa0000200, 0xdeadbeef).outcome == IOPAccessOutcome::Completed);
  REQUIRE(core.readData32(0x80200200).value == 0x55667788);
  REQUIRE(core.readData32(0xa0200200).value == 0xdeadbeef);
  REQUIRE(core.readData32(0x80000200).value == 0xdeadbeef);
}

TEST_CASE("IOP rejected stores preserve caches and defer pending ROM reconciliation",
  "[iop][cache][data-store][admission]")
{
  for (const auto operation : {IOPStoreOperation::Byte, IOPStoreOperation::Halfword,
       IOPStoreOperation::Word, IOPStoreOperation::MaskedWord})
  {
    for (unsigned rejection = 0; rejection < 4; ++rejection)
    {
      CAPTURE(static_cast<unsigned>(operation));
      CAPTURE(rejection);
      IOPBus bus;
      IOPCore core;
      auto rom = makeRom(0x12);
      const std::weak_ptr<const BootROMImage> oldImage = rom;
      REQUIRE(bus.installRom(rom));
      core.attachBus(&bus);
      rom.reset();
      IOPCacheTestAccess::seed(&core);
      IOPCacheTestAccess::setContinuations(&core);
      IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHE_PROBE_RESULT |
        (rejection == 3 ? IOPCOP0Status::CURRENT_USER_MODE : 0));
      REQUIRE(bus.installRom(makeRom(0x34)));
      const IOPAddress address = rejection == 0 ? 0x9fc00000
        : rejection == 1 ? 0x00800000
        : rejection == 2 ? 0x1f800400 : 0x80000200;
      const auto write = IOPCacheTestAccess::writeStore(&core,
        {operation, address + (operation == IOPStoreOperation::MaskedWord ? 1 : 0),
          0x12345678, 3});
      REQUIRE(write.outcome == (rejection == 0 ? IOPAccessOutcome::ReadOnly
        : rejection < 3 ? IOPAccessOutcome::Unmapped
        : IOPAccessOutcome::ProtectionFailure));
      REQUIRE_FALSE(oldImage.expired());
      requireSeeded(core.cache());
      REQUIRE((core.status() & IOPCOP0Status::CACHE_PROBE_RESULT) != 0);
      REQUIRE(core.branchContinuation().active);
      REQUIRE(core.delayedResultContinuation().value == 0x12345678);
      REQUIRE(bus.read32(IOPBus::ROM_BASE).value == 0x34);
      REQUIRE(bus.read32(0x200).value == 0);
      IOPCacheTestAccess::setStatus(&core, 0);
      REQUIRE(core.writeData32(0x80000200, 0x89abcdef).outcome == IOPAccessOutcome::Completed);
      REQUIRE(oldImage.expired());
      const auto location = IOPCache::location(IOPCacheBank::Data, 0x200);
      REQUIRE(core.cache().dataLine(location.lineIndex).valid);
      REQUIRE(core.cache().dataLine(location.lineIndex).words[0] == 0x89abcdef);
      REQUIRE_FALSE(core.cache().instructionLine(1).valid);
      REQUIRE_FALSE(core.cache().dataLine(1).valid);
    }
  }
}

TEST_CASE("IOP rejected store hits and misaligned accesses preserve resident snapshots",
  "[iop][cache][data-store][admission]")
{
  IOPBus bus;
  IOPCore core;
  REQUIRE(bus.installRom(makeRom(0x12)));
  core.attachBus(&bus);
  core.startExecution(0x9fc00000);
  REQUIRE(core.fetchInstruction().value == 0x12);
  REQUIRE(core.readData32(0x9fc00000).value == 0x12);
  REQUIRE(core.readData32(0x200).outcome == IOPAccessOutcome::Completed);
  IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHE_PROBE_RESULT);
  const auto before = core.cache();
  for (const auto operation :
       {IOPStoreOperation::Byte, IOPStoreOperation::Halfword, IOPStoreOperation::Word})
  {
    REQUIRE(IOPCacheTestAccess::writeStore(
      &core, {operation, 0x9fc00000, 0x89abcdef, 0}).outcome ==
      IOPAccessOutcome::ReadOnly);
    requireUnchangedCache(core.cache(), before);
  }
  for (std::uint8_t mask = 1; mask <= 0xf; ++mask)
  {
    const auto write = IOPCacheTestAccess::writeStore(&core,
      {IOPStoreOperation::MaskedWord, 0x9fc00001, 0x89abcdef, mask});
    REQUIRE(write.outcome == IOPAccessOutcome::ReadOnly);
    REQUIRE(write.virtualAddress == 0x9fc00001);
    REQUIRE(write.physicalAddress == 0x1fc00001);
    requireUnchangedCache(core.cache(), before);
  }
  REQUIRE(core.writeData16(0x201, 0x1234).outcome == IOPAccessOutcome::Misaligned);
  REQUIRE(core.writeData32(0x202, 0x12345678).outcome == IOPAccessOutcome::Misaligned);
  requireUnchangedCache(core.cache(), before);
  REQUIRE(bus.read32(0x200).value == 0);
  REQUIRE(bus.read32(IOPBus::ROM_BASE).value == 0x12);
  REQUIRE(core.status() == IOPCOP0Status::CACHE_PROBE_RESULT);
}

TEST_CASE("IOP rejected executed stores preserve both banks and pending retirement state",
  "[iop][cache][data-store][execution]")
{
  for (const IOPWord opcode : {0x28u, 0x29u, 0x2bu, 0x2au, 0x2eu})
  {
    for (const IOPAddress address : {0x9fc00000u, 0x00800000u})
    {
      IOPBus bus;
      IOPCore core;
      REQUIRE(bus.installRom(makeRom(0x12)));
      core.attachBus(&bus);
      const IOPAddress offset = opcode == 0x2a || opcode == 0x2e ? 1 : 0;
      REQUIRE(bus.write32(0x100, (opcode << 26) | (1u << 21) |
        (2u << 16) | offset).status == IOPBusStatus::Completed);
      IOPCacheTestAccess::setGeneralRegister(&core, 1, address);
      IOPCacheTestAccess::setGeneralRegister(&core, 2, 0x89abcdef);
      core.startExecution(0xa0000100);
      IOPCacheTestAccess::seed(&core);
      IOPCacheTestAccess::setContinuations(&core);
      IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHE_PROBE_RESULT);
      core.stepInstruction();
      REQUIRE(core.stopReason() == IOPStopReason::StoreFailure);
      REQUIRE(core.executionState() == IOPExecutionState::Halted);
      REQUIRE(core.exception().kind == IOPException::None);
      REQUIRE(core.storeFailure().access.virtualAddress == address + offset);
      REQUIRE(core.storeFailure().access.physicalAddress ==
        (address & 0x1fffffff) + offset);
      REQUIRE(core.storeFailure().access.cacheRoute == IOPCacheRoute::Cached);
      REQUIRE(core.storeFailure().access.outcome == (address == 0x9fc00000
        ? IOPAccessOutcome::ReadOnly : IOPAccessOutcome::Unmapped));
      REQUIRE(core.programCounter() == 0xa0000100);
      REQUIRE(core.branchContinuation().active);
      REQUIRE(core.branchContinuation().instructionAddress == 0xa00000fc);
      REQUIRE(core.branchContinuation().targetAddress == 0x200);
      REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::Load);
      REQUIRE(core.delayedResultContinuation().destination == 7);
      REQUIRE(core.delayedResultContinuation().value == 0x12345678);
      REQUIRE(core.generalRegister(7) == 0);
      REQUIRE(core.status() == IOPCOP0Status::CACHE_PROBE_RESULT);
      REQUIRE(core.retiredInstructions() == 0);
      REQUIRE(core.elapsedCycles() == 1);
      requireSeeded(core.cache());
    }
  }
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
  REQUIRE(cache.readWord(IOPCacheBank::Data, bus, 0x00800004).status == IOPBusStatus::Unmapped);
  const auto &after = cache.dataLine(location.lineIndex);
  REQUIRE_FALSE(after.valid);
  REQUIRE(after.physicalTag == before.physicalTag);
  REQUIRE(after.words == before.words);
  REQUIRE(cache.readWord(IOPCacheBank::Data, bus, 5).status == IOPBusStatus::Misaligned);
  REQUIRE_FALSE(after.valid);
  REQUIRE(after.physicalTag == before.physicalTag);
  REQUIRE(after.words == before.words);
  REQUIRE(bus.write32(4, 0x11223344).status == IOPBusStatus::Completed);
  REQUIRE(cache.readWord(IOPCacheBank::Data, bus, 4).value == 0x11223344);
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
      const bool written = index >= 0x120 / sizeof(IOPWord) &&
        index < 0x130 / sizeof(IOPWord);
      const auto &data = core.cache().dataLine(index);
      REQUIRE(data.valid == written);
      REQUIRE(data.physicalTag == 0);
      REQUIRE(data.words[0] == (written
        ? 0x56780000 + index - 0x120 / sizeof(IOPWord) : 0));
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

TEST_CASE("IOP zero-tag instruction flush exposes replaced code without changing payload",
  "[iop][cache][tag-control]")
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
  core.startExecution(0x80000100);
  core.stepInstruction();
  REQUIRE(core.generalRegister(1) == 1);
  const auto before = core.cache();
  const auto index = IOPCache::location(IOPCacheBank::Instruction, 0x100).lineIndex;
  REQUIRE(bus.write32(IOPBus::CACHE_INIT_CONTROL, 0xc04).status ==
    IOPBusStatus::Completed);
  requireUnchangedCache(core.cache(), before);
  IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHE_ISOLATED);
  REQUIRE(core.writeData32(0x104, 0).outcome == IOPAccessOutcome::Completed);
  const auto &line = core.cache().instructionLine(index);
  REQUIRE_FALSE(line.valid);
  REQUIRE(line.words == before.instructionLine(index).words);
  REQUIRE(line.physicalTag == before.instructionLine(index).physicalTag);
  requireUnchangedCache(core.cache(), before, IOPCache::DATA_LINE_COUNT, index);
  REQUIRE(bus.read32(0x100).value == 0x24010002);
  IOPCacheTestAccess::setStatus(&core, 0);
  core.startExecution(0x80000100);
  core.stepInstruction();
  REQUIRE(core.generalRegister(1) == 2);
}

TEST_CASE("IOP zero-tag data flush invalidates all four words of a maintenance group",
  "[iop][cache][tag-control]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  for (IOPAddress address = 0x100; address < 0x120; address += 4)
  {
    REQUIRE(bus.write32(address, address).status == IOPBusStatus::Completed);
    REQUIRE(core.readData32(address).value == address);
    REQUIRE(bus.write32(address, address + 1).status == IOPBusStatus::Completed);
  }
  const auto before = core.cache();
  REQUIRE(bus.write32(IOPBus::CACHE_INIT_CONTROL, 0xc4).status ==
    IOPBusStatus::Completed);
  IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHE_ISOLATED);
  REQUIRE(core.writeData32(0x10c, 0).outcome == IOPAccessOutcome::Completed);
  for (IOPAddress address = 0x100; address < 0x120; address += 4)
  {
    const auto index = IOPCache::location(IOPCacheBank::Data, address).lineIndex;
    const auto &line = core.cache().dataLine(index);
    REQUIRE(line.valid == (address >= 0x110));
    REQUIRE(line.words == before.dataLine(index).words);
    REQUIRE(line.physicalTag == before.dataLine(index).physicalTag);
    REQUIRE(bus.read32(address).value == address + 1);
  }
  IOPCacheTestAccess::setStatus(&core, 0);
  for (IOPAddress address = 0x100; address < 0x120; address += 4)
  {
    REQUIRE(core.readData32(address).value == address + (address < 0x110 ? 1 : 0));
  }
}

TEST_CASE("IOP tag planning gates exact profiles on isolation without affecting fetch",
  "[iop][cache][tag-control]")
{
  for (const IOPWord bcc : {0u, 3u, 0xc00u, 0xcc0u, 4u, 0xc04u, 0xc4u,
       0xcc4u, 0xc05u, 0xc06u, 0x80000c04u, 0xffffffffu})
  {
    for (const bool isolated : {false, true})
    {
      for (const bool swapped : {false, true})
      {
        for (const auto access : {IOPCacheAccess::InstructionFetch,
             IOPCacheAccess::DataLoad, IOPCacheAccess::DataStore})
        {
          for (const auto route : {IOPCacheRoute::Cached, IOPCacheRoute::Uncached})
          {
            for (const auto region : {IOPBusRegion::Ram, IOPBusRegion::Rom,
                 IOPBusRegion::Scratchpad, IOPBusRegion::Mmio, IOPBusRegion::Unmapped})
            {
              const auto ordinary = IOPCache::planAccess(
                access, {isolated, swapped}, route, region);
              const auto plan = IOPCache::planAccess(
                access, {isolated, swapped, bcc}, route, region);
              if (!isolated || (bcc & 4) == 0 ||
                  access == IOPCacheAccess::InstructionFetch)
              {
                REQUIRE(plan.disposition == ordinary.disposition);
                REQUIRE(plan.bank == ordinary.bank);
              }
              else if (!swapped && access == IOPCacheAccess::DataStore &&
                  (bcc == 0xc04 || bcc == 0xc4))
              {
                REQUIRE(plan.disposition == IOPCacheAccessDisposition::InvalidateTags);
                REQUIRE(plan.bank == (bcc == 0xc04
                  ? IOPCacheBank::Instruction : IOPCacheBank::Data));
              }
              else if (!swapped && access == IOPCacheAccess::DataStore &&
                  bcc == 0xcc4)
              {
                REQUIRE(plan.disposition ==
                  IOPCacheAccessDisposition::InvalidateBothTags);
              }
              else
              {
                REQUIRE(plan.disposition == IOPCacheAccessDisposition::Unsupported);
              }
            }
          }
        }
      }
    }
  }
}

TEST_CASE("IOP zero-tag groups use every word position and index without tag matching",
  "[iop][cache][tag-control]")
{
  for (const IOPWord profile : {0xc04u, 0xc4u, 0xcc4u})
  {
    CAPTURE(profile);
    const IOPAddress capacity = profile == 0xc4 ? 1024 : 4096;
    for (const IOPAddress base : {0u, 0x80000000u, 0xa0200000u, 0x1f800000u,
         0x9fc00000u, 0x00800000u, 0xfffff000u})
    {
      for (const IOPAddress group : {0u, 0x100u, capacity - 16,
           capacity, capacity + 0x110})
      {
        for (IOPAddress word = 0; word < 16; word += 4)
        {
          for (const bool masked : {false, true})
          {
            IOPBus bus;
            IOPCore core;
            core.attachBus(&bus);
            IOPCacheTestAccess::seed(&core);
            const auto before = core.cache();
            REQUIRE(bus.write32(group + word, 0x12345678).status ==
              IOPBusStatus::Completed);
            REQUIRE(bus.write32(IOPBus::SCRATCHPAD_BASE + (group + word) % 1024,
              0x89abcdef).status == IOPBusStatus::Completed);
            REQUIRE(bus.write32(IOPBus::CACHE_INIT_CONTROL, profile).status ==
              IOPBusStatus::Completed);
            IOPCacheTestAccess::setStatus(&core,
              IOPCOP0Status::CACHE_ISOLATED | IOPCOP0Status::CACHE_PROBE_RESULT);
            const auto address = base + group + word;
            const auto write = masked ? IOPCacheTestAccess::writeStore(&core,
              {IOPStoreOperation::MaskedWord, address + 3, 0, 0xf})
              : core.writeData32(address, 0);
            REQUIRE(write.outcome == IOPAccessOutcome::Completed);
            REQUIRE(write.exception == IOPException::None);
            requireInvalidatedGroup(core.cache(), before, profile, group);
            REQUIRE(core.status() ==
              (IOPCOP0Status::CACHE_ISOLATED | IOPCOP0Status::CACHE_PROBE_RESULT));
            REQUIRE(bus.read32(group + word).value == 0x12345678);
            REQUIRE(bus.read32(IOPBus::SCRATCHPAD_BASE + (group + word) % 1024).value ==
              0x89abcdef);
          }
        }
      }
    }
  }
}

TEST_CASE("IOP unsupported tag accesses preserve caches CM devices and pending ROM identity",
  "[iop][cache][tag-control]")
{
  for (const IOPWord bcc : {4u, 0xc04u, 0xc4u, 0xcc4u, 0xc05u, 0xc06u,
       0xcc5u, 0xcc6u, 0x80000c04u, 0x80000cc4u, 0xffffffffu})
  {
    for (const bool swapped : {false, true})
    {
      CAPTURE(bcc);
      CAPTURE(swapped);
      const bool supported = !swapped &&
        (bcc == 0xc04 || bcc == 0xc4 || bcc == 0xcc4);
      IOPBus bus;
      IOPCore core;
      IOPInterruptController interrupts;
      bus.attachInterruptController(&interrupts);
      interrupts.writeControl(1);
      auto image = makeRom(0x12);
      const std::weak_ptr<const BootROMImage> previous = image;
      REQUIRE(bus.installRom(image));
      core.attachBus(&bus);
      image.reset();
      IOPCacheTestAccess::seed(&core);
      const auto before = core.cache();
      const IOPWord status = IOPCOP0Status::CACHE_ISOLATED |
        IOPCOP0Status::CACHE_PROBE_RESULT |
        (swapped ? IOPCOP0Status::CACHES_SWAPPED : 0);
      IOPCacheTestAccess::setStatus(&core, status);
      REQUIRE(bus.write32(IOPBus::CACHE_INIT_CONTROL, bcc).status ==
        IOPBusStatus::Completed);
      REQUIRE(bus.write32(0x100, 0x12345678).status == IOPBusStatus::Completed);
      REQUIRE(bus.installRom(makeRom(0x34)));
      for (const IOPAddress address : {0x100u, 0xa0000100u, 0x9fc00000u,
           IOPBus::INTC_CTRL, IOPBus::CACHE_INIT_CONTROL, 0x00800000u})
      {
        for (const unsigned width : {1u, 2u, 4u})
        {
          const auto read = width == 1 ? core.readData8(address)
            : width == 2 ? core.readData16(address) : core.readData32(address);
          REQUIRE(read.outcome == IOPAccessOutcome::Unmapped);
          REQUIRE(read.exception == IOPException::DataBusError);
          REQUIRE(read.virtualAddress == address);
          REQUIRE(read.physicalAddress == core.classifyAddress(address).physicalAddress);
        }
        for (const auto operation : {IOPStoreOperation::Byte,
             IOPStoreOperation::Halfword, IOPStoreOperation::Word,
             IOPStoreOperation::MaskedWord})
        {
          if (supported && operation != IOPStoreOperation::Word)
          {
            continue;
          }
          const auto write = IOPCacheTestAccess::writeStore(&core,
            {operation, address, operation == IOPStoreOperation::Word ? 1u : 0u, 3});
          REQUIRE(write.outcome == IOPAccessOutcome::Unmapped);
          REQUIRE(write.exception == IOPException::None);
        }
      }
      for (std::uint8_t mask = 1; mask <= 0xf; ++mask)
      {
        if (supported && mask != 0xf)
        {
          continue;
        }
        const auto write = IOPCacheTestAccess::writeStore(&core,
          {IOPStoreOperation::MaskedWord, 0x103, mask == 0xf ? 1u : 0u, mask});
        REQUIRE(write.outcome == IOPAccessOutcome::Unmapped);
        REQUIRE(write.exception == IOPException::None);
      }
      for (const std::uint8_t mask : {0u, 0x10u, 0x1fu, 0xffu})
      {
        REQUIRE(IOPCacheTestAccess::writeStore(&core,
          {IOPStoreOperation::MaskedWord, 0x103, 0, mask}).outcome ==
          IOPAccessOutcome::Unmapped);
      }
      if (!supported)
      {
        REQUIRE(core.writeData32(0x100, 0).outcome == IOPAccessOutcome::Unmapped);
        REQUIRE(IOPCacheTestAccess::writeStore(&core,
          {IOPStoreOperation::MaskedWord, 0x101, 0, 0xf}).outcome ==
          IOPAccessOutcome::Unmapped);
      }
      requireUnchangedCache(core.cache(), before);
      REQUIRE(core.status() == status);
      REQUIRE_FALSE(previous.expired());
      REQUIRE(interrupts.control() == 1);
      REQUIRE(bus.read32(0x100).value == 0x12345678);
      REQUIRE(bus.cacheControl() == bcc);
      IOPCacheTestAccess::setStatus(&core, 0);
      REQUIRE(core.readData32(0xa0000100).value == 0x12345678);
      REQUIRE(previous.expired());
      requireClear(core.cache());
    }
  }
}

TEST_CASE("IOP accepted tag stores do not write control words or consume MMIO",
  "[iop][cache][tag-control]")
{
  for (const IOPWord profile : {0xc04u, 0xc4u, 0xcc4u})
  {
    IOPBus bus;
    IOPCore core;
    IOPInterruptController interrupts;
    bus.attachInterruptController(&interrupts);
    interrupts.writeControl(1);
    core.attachBus(&bus);
    IOPCacheTestAccess::seed(&core);
    IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHE_ISOLATED);
    REQUIRE(bus.write32(IOPBus::CACHE_INIT_CONTROL, profile).status ==
      IOPBusStatus::Completed);
    for (const auto address : {IOPBus::INTC_CTRL, IOPBus::CACHE_INIT_CONTROL})
    {
      const auto before = core.cache();
      REQUIRE(core.writeData32(address, 0).outcome == IOPAccessOutcome::Completed);
      requireInvalidatedGroup(core.cache(), before, profile,
        address == IOPBus::INTC_CTRL ? 0x70 : 0x130);
      REQUIRE(bus.cacheControl() == profile);
      REQUIRE(interrupts.control() == 1);
    }
  }
}

TEST_CASE("IOP tag partial stores invalidate groups without writing payload or devices",
  "[iop][cache][tag-control]")
{
  for (const IOPWord profile : {0xc04u, 0xc4u, 0xcc4u})
  {
    for (const IOPAddress address : {0x110u, 0xa0000110u, 0xbf802070u,
         IOPBus::CACHE_INIT_CONTROL, IOPBus::INTC_CTRL,
         0x80000ff0u, 0x80001010u})
    {
      for (const IOPWord value : {0u, 0x89abcdefu})
      {
        std::vector<IOPStoreRequest> requests;
        for (IOPAddress offset = 0; offset < 4; ++offset)
        {
          requests.push_back({IOPStoreOperation::Byte, address + offset, value, 0});
        }
        for (IOPAddress offset = 0; offset < 4; offset += 2)
        {
          requests.push_back({IOPStoreOperation::Halfword, address + offset, value, 0});
        }
        for (std::uint8_t mask = 1; mask < 0xf; ++mask)
        {
          requests.push_back({IOPStoreOperation::MaskedWord, address + 3, value, mask});
        }
        for (const auto &request : requests)
        {
          CAPTURE(profile);
          CAPTURE(request.virtualAddress);
          CAPTURE(request.byteEnable);
          CAPTURE(value);
          IOPBus bus;
          IOPCore core;
          IOPInterruptController interrupts;
          bus.attachInterruptController(&interrupts);
          interrupts.writeControl(1);
          core.attachBus(&bus);
          IOPCacheTestAccess::seed(&core);
          const auto before = core.cache();
          for (const IOPAddress physical : {0x110u, 0xff0u, 0x1010u})
          {
            REQUIRE(bus.write32(physical, 0x12345678).status == IOPBusStatus::Completed);
          }
          REQUIRE(bus.write32(IOPBus::CACHE_INIT_CONTROL, profile).status ==
            IOPBusStatus::Completed);
          const auto status =
            IOPCOP0Status::CACHE_ISOLATED | IOPCOP0Status::CACHE_PROBE_RESULT;
          IOPCacheTestAccess::setStatus(&core, status);
          const auto write = IOPCacheTestAccess::writeStore(&core, request);
          REQUIRE(write.outcome == IOPAccessOutcome::Completed);
          REQUIRE(write.exception == IOPException::None);
          requireInvalidatedGroup(core.cache(), before, profile, address & ~IOPAddress(15));
          REQUIRE(core.status() == status);
          REQUIRE(bus.cacheControl() == profile);
          REQUIRE(interrupts.control() == 1);
          for (const IOPAddress physical : {0x110u, 0xff0u, 0x1010u})
          {
            REQUIRE(bus.read32(physical).value == 0x12345678);
          }
        }
      }
    }
  }
}

TEST_CASE("IOP guest merge stores invalidate tag groups for zero words or partial transfers",
  "[iop][cache][tag-control]")
{
  for (const IOPWord profile : {0xc04u, 0xc4u, 0xcc4u})
  {
    for (const bool left : {false, true})
    {
      for (const bool complete : {false, true})
      {
        IOPBus bus;
        IOPCore core;
        core.attachBus(&bus);
        const IOPWord offset = complete ? (left ? 3 : 0) : 1;
        REQUIRE(bus.write32(0x200, (left ? 0xa8220000 : 0xb8220000) | offset).status ==
          IOPBusStatus::Completed);
        REQUIRE(bus.write32(0x100, 0x12345678).status == IOPBusStatus::Completed);
        REQUIRE(bus.write32(IOPBus::CACHE_INIT_CONTROL, profile).status ==
          IOPBusStatus::Completed);
        core.startExecution(0xa0000200);
        IOPCacheTestAccess::seed(&core);
        IOPCacheTestAccess::setGeneralRegister(&core, 1, 0x80000100);
        IOPCacheTestAccess::setGeneralRegister(&core, 2, complete ? 0 : 0x89abcdef);
        IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHE_ISOLATED);
        const auto before = core.cache();
        core.stepInstruction();
        REQUIRE(core.exception().kind == IOPException::None);
        REQUIRE(core.retiredInstructions() == 1);
        REQUIRE(core.stopReason() == IOPStopReason::None);
        requireInvalidatedGroup(core.cache(), before, profile, 0x100);
        REQUIRE(bus.read32(0x100).value == 0x12345678);
      }
    }
  }
}

TEST_CASE("IOP active tag accesses retain virtual alignment and protection precedence",
  "[iop][cache][tag-control]")
{
  IOPBus bus;
  IOPCore core;
  core.attachBus(&bus);
  IOPCacheTestAccess::seed(&core);
  const auto before = core.cache();
  REQUIRE(bus.write32(IOPBus::CACHE_INIT_CONTROL, 0xcc4).status ==
    IOPBusStatus::Completed);
  IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHE_ISOLATED);
  REQUIRE(core.readData32(0x101).outcome == IOPAccessOutcome::Misaligned);
  REQUIRE(core.readData16(0x101).exception == IOPException::AddressErrorLoadOrFetch);
  REQUIRE(core.writeData32(0x101, 0).outcome == IOPAccessOutcome::Misaligned);
  REQUIRE(core.writeData16(0x101, 0).exception == IOPException::AddressErrorStore);
  IOPCacheTestAccess::setStatus(&core,
    IOPCOP0Status::CACHE_ISOLATED | IOPCOP0Status::CURRENT_USER_MODE);
  REQUIRE(core.readData32(0x80000101).outcome == IOPAccessOutcome::ProtectionFailure);
  REQUIRE(core.writeData32(0x80000101, 0).outcome == IOPAccessOutcome::ProtectionFailure);
  REQUIRE(IOPCacheTestAccess::writeStore(&core,
    {IOPStoreOperation::MaskedWord, 0x80000101, 0, 0xf}).exception ==
    IOPException::AddressErrorStore);
  requireUnchangedCache(core.cache(), before);
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
  REQUIRE(cache.readWord(IOPCacheBank::Instruction, bus, unmapped).status == IOPBusStatus::Unmapped);
  const auto &after = cache.instructionLine(location.lineIndex);
  REQUIRE_FALSE(after.valid);
  REQUIRE(after.physicalTag == before.physicalTag);
  REQUIRE(after.words == before.words);
  REQUIRE(cache.readWord(IOPCacheBank::Instruction, bus, unmapped).status == IOPBusStatus::Unmapped);
  REQUIRE_FALSE(after.valid);
  REQUIRE(after.physicalTag == before.physicalTag);
  REQUIRE(after.words == before.words);
  REQUIRE(bus.write32(0x1c, 0x11223344).status == IOPBusStatus::Completed);
  REQUIRE(cache.readWord(IOPCacheBank::Instruction, bus, 0x1c).value == 0x11223344);
  REQUIRE(after.valid);
  REQUIRE(after.physicalTag == 0);
  REQUIRE(after.words[0] == 0);
  REQUIRE(after.words[3] == 0x11223344);
  const auto filled = after;
  REQUIRE(cache.readWord(IOPCacheBank::Instruction, bus, 0x1d).status == IOPBusStatus::Misaligned);
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

TEST_CASE("IOP isolated reads expose indexed bytes and probe physical tags on every route",
  "[iop][cache][isolation]")
{
  for (const bool swapped : {false, true})
  {
    for (const bool oldMiss : {false, true})
    {
      IOPBus bus;
      IOPCore core;
      REQUIRE(bus.installRom(makeRom(0x12)));
      core.attachBus(&bus);
      IOPCacheTestAccess::seed(&core);
      const auto before = core.cache();
      const auto bank = swapped ? IOPCacheBank::Instruction : IOPCacheBank::Data;
      const IOPWord mode = IOPCOP0Status::CACHE_ISOLATED |
        (swapped ? IOPCOP0Status::CACHES_SWAPPED : 0);
      for (const IOPAddress address : {0x104u, 0x80000104u, 0xa0000104u,
           0x00200104u, 0x1f800104u, 0x9fc00010u, 0x00800104u, 0xfffffffcu})
      {
        const auto physical = core.classifyAddress(address).physicalAddress;
        const auto location = IOPCache::location(bank, physical);
        const IOPWord word = swapped
          ? before.instructionLine(location.lineIndex).words[location.wordIndex]
          : before.dataLine(location.lineIndex).words[0];
        const bool valid = swapped
          ? before.instructionLine(location.lineIndex).valid
          : before.dataLine(location.lineIndex).valid;
        const auto tag = swapped
          ? before.instructionLine(location.lineIndex).physicalTag
          : before.dataLine(location.lineIndex).physicalTag;
        const bool miss = !valid || tag != location.physicalTag;
        for (const unsigned width : {1u, 2u, 4u})
        {
          for (IOPAddress offset = 0; offset < 4; offset += width)
          {
            IOPCacheTestAccess::setStatus(&core, mode |
              (oldMiss ? IOPCOP0Status::CACHE_PROBE_RESULT : 0));
            const auto read = width == 1 ? core.readData8(address + offset)
              : width == 2 ? core.readData16(address + offset)
                           : core.readData32(address);
            const IOPWord mask = width == 1 ? 0xff : width == 2 ? 0xffff : UINT32_MAX;
            REQUIRE(read.outcome == IOPAccessOutcome::Completed);
            REQUIRE(read.exception == IOPException::None);
            REQUIRE(read.virtualAddress == address + offset);
            REQUIRE(read.physicalAddress == physical + offset);
            REQUIRE(read.value == ((word >> (offset * 8)) & mask));
            REQUIRE(core.status() == (mode |
              (miss ? IOPCOP0Status::CACHE_PROBE_RESULT : 0)));
          }
        }
      }
      requireUnchangedCache(core.cache(), before);
      REQUIRE(bus.read32(0x104).value == 0);
      REQUIRE(bus.read32(0x1f800104).value == 0);
      REQUIRE(core.elapsedCycles() == 0);
      REQUIRE(core.retiredInstructions() == 0);
    }
  }
}

TEST_CASE("IOP isolated stores update only the addressed word or invalidate without bus access",
  "[iop][cache][isolation]")
{
  for (const bool swapped : {false, true})
  {
    for (const IOPAddress address : {0x104u, 0x80000108u, 0xa020010cu,
         0x1f800104u, 0x9fc00014u, 0x00800104u, 0xfffffffcu})
    {
      for (std::uint8_t mask = 1; mask <= 0xf; ++mask)
      {
        IOPBus bus;
        IOPCore core;
        REQUIRE(bus.installRom(makeRom(0x12)));
        core.attachBus(&bus);
        IOPCacheTestAccess::seed(&core);
        const IOPWord mode = IOPCOP0Status::CACHE_ISOLATED |
          IOPCOP0Status::CACHE_PROBE_RESULT |
          (swapped ? IOPCOP0Status::CACHES_SWAPPED : 0);
        IOPCacheTestAccess::setStatus(&core, mode);
        const auto before = core.cache();
        const auto bank = swapped ? IOPCacheBank::Instruction : IOPCacheBank::Data;
        const auto physical = core.classifyAddress(address).physicalAddress;
        const auto location = IOPCache::location(bank, physical);
        const auto write = IOPCacheTestAccess::writeStore(&core,
          {IOPStoreOperation::MaskedWord, address + 1, 0x89abcdef, mask});
        REQUIRE(write.outcome == IOPAccessOutcome::Completed);
        REQUIRE(write.exception == IOPException::None);
        REQUIRE(write.virtualAddress == address + 1);
        REQUIRE(write.physicalAddress == physical + 1);
        if (swapped)
        {
          requireIndexedStore(core.cache().instructionLine(location.lineIndex),
            before.instructionLine(location.lineIndex), location, 0x89abcdef, mask);
          requireUnchangedCache(core.cache(), before,
            IOPCache::DATA_LINE_COUNT, location.lineIndex);
        }
        else
        {
          requireIndexedStore(core.cache().dataLine(location.lineIndex),
            before.dataLine(location.lineIndex), location, 0x89abcdef, mask);
          requireUnchangedCache(core.cache(), before, location.lineIndex);
        }
        REQUIRE(core.status() == mode);
        REQUIRE(bus.read32(0x104).value == 0);
        REQUIRE(bus.read32(0x20010c).value == 0);
        REQUIRE(bus.read32(0x1f800104).value == 0);
        REQUIRE(bus.read32(IOPBus::ROM_BASE).value == 0x12);
        REQUIRE(core.elapsedCycles() == 0);
        REQUIRE(core.retiredInstructions() == 0);
      }
    }
  }
}

TEST_CASE("IOP scalar cache-management stores normalize lanes without merging partial payload",
  "[iop][cache][isolation]")
{
  struct ModeCase
  {
    bool isolated;
    bool swapped;
  };
  const ModeCase modes[] = {{true, false}, {true, true}, {false, true}};
  for (const auto mode : modes)
  {
    for (const unsigned width : {1u, 2u, 4u})
    {
      for (IOPAddress offset = 0; offset < 4; offset += width)
      {
        IOPBus bus;
        IOPCore core;
        core.attachBus(&bus);
        IOPCacheTestAccess::seed(&core);
        IOPCacheTestAccess::setStatus(&core,
          (mode.isolated ? IOPCOP0Status::CACHE_ISOLATED : 0) |
          (mode.swapped ? IOPCOP0Status::CACHES_SWAPPED : 0));
        const auto before = core.cache();
        const auto write = width == 1 ? core.writeData8(0x80000114 + offset, 0x78)
          : width == 2 ? core.writeData16(0x80000114 + offset, 0x5678)
                       : core.writeData32(0x80000114, 0x12345678);
        REQUIRE(write.outcome == IOPAccessOutcome::Completed);
        const auto bank = mode.swapped ? IOPCacheBank::Instruction : IOPCacheBank::Data;
        const auto location = IOPCache::location(bank, 0x114);
        const std::uint8_t mask = width == 4 ? 0xf : 1;
        if (mode.swapped)
        {
          requireIndexedStore(core.cache().instructionLine(location.lineIndex),
            before.instructionLine(location.lineIndex), location, 0x12345678, mask);
          requireUnchangedCache(core.cache(), before,
            IOPCache::DATA_LINE_COUNT, location.lineIndex);
        }
        else
        {
          requireIndexedStore(core.cache().dataLine(location.lineIndex),
            before.dataLine(location.lineIndex), location, 0x12345678, mask);
          requireUnchangedCache(core.cache(), before, location.lineIndex);
        }
        const IOPWord valueMask = width == 1 ? 0xff : width == 2 ? 0xffff : UINT32_MAX;
        REQUIRE(bus.read32(0x114).value == (mode.isolated
          ? 0 : (0x12345678 & valueMask) << (offset * 8)));
      }
    }
  }
}

TEST_CASE("IOP isolation supports marker sizing and invalidation keeps payload observable",
  "[iop][cache][isolation]")
{
  for (const bool swapped : {false, true})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    const IOPWord mode = IOPCOP0Status::CACHE_ISOLATED |
      (swapped ? IOPCOP0Status::CACHES_SWAPPED : 0);
    const IOPAddress capacity = swapped ? IOPCache::INSTRUCTION_SIZE
      : IOPCache::DATA_SIZE;
    IOPCacheTestAccess::setStatus(&core, mode);
    REQUIRE(core.writeData32(0x80000000, 0xa5a5a5a5).outcome ==
      IOPAccessOutcome::Completed);
    REQUIRE(core.readData32(0x80000000 + capacity / 2).value == 0);
    REQUIRE(core.readData32(0x80000000 + capacity).value == 0xa5a5a5a5);
    REQUIRE(core.status() == (mode | IOPCOP0Status::CACHE_PROBE_RESULT));
    REQUIRE(core.readData32(0xa0000000).value == 0xa5a5a5a5);
    REQUIRE(core.status() == mode);
    REQUIRE(core.writeData8(0x80000000 + capacity, 0).outcome ==
      IOPAccessOutcome::Completed);
    REQUIRE(core.readData32(0x80000000).value == 0xa5a5a5a5);
    REQUIRE(core.status() == (mode | IOPCOP0Status::CACHE_PROBE_RESULT));
    REQUIRE(bus.read32(0).value == 0);
  }
}

TEST_CASE("IOP swapped fetch and normal data loads use the selected physical bank geometry",
  "[iop][cache][isolation][swap]")
{
  for (const bool fetch : {false, true})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    for (IOPAddress word = 0; word < 4; ++word)
    {
      REQUIRE(bus.write32(0x120 + word * 4, 0x12340000 + word).status ==
        IOPBusStatus::Completed);
    }
    const IOPWord mode = IOPCOP0Status::CACHES_SWAPPED |
      IOPCOP0Status::CACHE_PROBE_RESULT |
      (fetch ? IOPCOP0Status::CACHE_ISOLATED : 0);
    IOPCacheTestAccess::setStatus(&core, mode);
    core.startExecution(0x80000124);
    const auto read = fetch ? core.fetchInstruction() : core.readData16(0x80000124);
    REQUIRE(read.outcome == IOPAccessOutcome::Completed);
    REQUIRE(read.value == (fetch ? 0x12340001u : 1u));
    const auto bank = fetch ? IOPCacheBank::Data : IOPCacheBank::Instruction;
    const auto location = IOPCache::location(bank, 0x124);
    if (fetch)
    {
      REQUIRE(core.cache().dataLine(location.lineIndex).valid);
      REQUIRE(core.cache().dataLine(location.lineIndex).words[0] == 0x12340001);
      REQUIRE_FALSE(core.cache().dataLine(location.lineIndex - 1).valid);
      REQUIRE_FALSE(core.cache().instructionLine(0x120 / 16).valid);
    }
    else
    {
      REQUIRE(core.cache().instructionLine(location.lineIndex).valid);
      for (IOPAddress word = 0; word < 4; ++word)
      {
        REQUIRE(core.cache().instructionLine(location.lineIndex).words[word] ==
          0x12340000 + word);
      }
      REQUIRE_FALSE(core.cache().dataLine(0x124 / 4).valid);
    }
    const auto filled = core.cache();
    REQUIRE(bus.write32(0x124, 0x55667788).status == IOPBusStatus::Completed);
    REQUIRE((fetch ? core.fetchInstruction() : core.readData32(0x80000124)).value ==
      0x12340001);
    REQUIRE(core.status() == mode);
    IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHE_PROBE_RESULT);
    REQUIRE((fetch ? core.readData32(0x80000124) : core.fetchInstruction()).value ==
      0x12340001);
    requireUnchangedCache(core.cache(), filled);
    REQUIRE(core.status() == IOPCOP0Status::CACHE_PROBE_RESULT);
  }
}

TEST_CASE("IOP ordinary swapped stores write through before indexed I bank updates",
  "[iop][cache][isolation][swap]")
{
  for (std::uint8_t mask = 1; mask <= 0xf; ++mask)
  {
    for (const bool rejected : {false, true})
    {
      IOPBus bus;
      IOPCore core;
      REQUIRE(bus.installRom(makeRom(0x12)));
      core.attachBus(&bus);
      IOPCacheTestAccess::seed(&core);
      IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHES_SWAPPED |
        IOPCOP0Status::CACHE_PROBE_RESULT);
      const IOPAddress address = rejected ? 0x9fc00014 : 0x80000114;
      const auto before = core.cache();
      const auto write = IOPCacheTestAccess::writeStore(&core,
        {IOPStoreOperation::MaskedWord, address + 1, 0x89abcdef, mask});
      REQUIRE(write.outcome == (rejected ? IOPAccessOutcome::ReadOnly
        : IOPAccessOutcome::Completed));
      if (rejected)
      {
        requireUnchangedCache(core.cache(), before);
      }
      else
      {
        const auto location = IOPCache::location(IOPCacheBank::Instruction, 0x114);
        requireIndexedStore(core.cache().instructionLine(location.lineIndex),
          before.instructionLine(location.lineIndex), location, 0x89abcdef, mask);
        requireUnchangedCache(core.cache(), before,
          IOPCache::DATA_LINE_COUNT, location.lineIndex);
        for (unsigned lane = 0; lane < 4; ++lane)
        {
          REQUIRE(bus.read8(0x114 + lane).value == ((mask & (1u << lane)) != 0
            ? (0x89abcdefu >> (lane * 8)) & 0xff : 0));
        }
      }
      REQUIRE(core.status() ==
        (IOPCOP0Status::CACHES_SWAPPED | IOPCOP0Status::CACHE_PROBE_RESULT));
    }
  }
}

TEST_CASE("IOP isolated scalar loads publish probe results before delayed GPR writeback",
  "[iop][cache][isolation][execution]")
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
  for (const bool swapped : {false, true})
  {
    for (const bool miss : {false, true})
    {
      for (const unsigned destination : {0u, 2u})
      {
        for (const auto &test : cases)
        {
          IOPBus bus;
          IOPCore core;
          core.attachBus(&bus);
          const IOPWord mode = IOPCOP0Status::CACHE_ISOLATED |
            (swapped ? IOPCOP0Status::CACHES_SWAPPED : 0);
          IOPCacheTestAccess::setStatus(&core, mode);
          REQUIRE(core.writeData32(0x00800200, 0x89ab80fe).outcome ==
            IOPAccessOutcome::Completed);
          const IOPAddress capacity = swapped ? IOPCache::INSTRUCTION_SIZE
            : IOPCache::DATA_SIZE;
          IOPCacheTestAccess::setGeneralRegister(
            &core, 1, 0x00800200 + (miss ? capacity : 0));
          IOPCacheTestAccess::setGeneralRegister(&core, 2, 0x55);
          REQUIRE(bus.write32(0x100, (test.opcode << 26) | (1u << 21) |
            (destination << 16) | test.offset).status == IOPBusStatus::Completed);
          REQUIRE(bus.write32(0x104, 0x40046000).status == IOPBusStatus::Completed);
          IOPCacheTestAccess::setStatus(&core, mode |
            (miss ? 0 : IOPCOP0Status::CACHE_PROBE_RESULT));
          const auto before = core.cache();
          core.startExecution(0xa0000100);
          core.stepInstruction();
          const IOPWord status = mode | (miss ? IOPCOP0Status::CACHE_PROBE_RESULT : 0);
          REQUIRE(core.status() == status);
          REQUIRE(core.generalRegister(2) == 0x55);
          REQUIRE(core.generalRegister(0) == 0);
          core.haltExecution();
          core.resumeExecution();
          core.stepInstruction(); // MFC0 r4, Status samples the completed probe.
          REQUIRE(core.generalRegister(2) == (destination == 0 ? 0x55 : test.expected));
          REQUIRE(core.generalRegister(4) == 0);
          REQUIRE(core.delayedResultContinuation().value ==
            (status | IOPCOP0Status::TLB_SHUTDOWN));
          core.stepInstruction();
          REQUIRE(core.generalRegister(4) == (status | IOPCOP0Status::TLB_SHUTDOWN));
          REQUIRE(core.status() == status);
          REQUIRE(core.retiredInstructions() == 3);
          REQUIRE(core.elapsedCycles() == 3);
          REQUIRE(core.exception().kind == IOPException::None);
          requireUnchangedCache(core.cache(), before);
        }
      }
    }
  }
}

TEST_CASE("IOP isolated merge loads preserve every offset and instruction order",
  "[iop][cache][isolation][merge][execution]")
{
  for (const bool swapped : {false, true})
  {
    for (const bool leftFirst : {false, true})
    {
      for (IOPAddress offset = 0; offset < 4; ++offset)
      {
        IOPBus bus;
        IOPCore core;
        core.attachBus(&bus);
        const IOPWord mode = IOPCOP0Status::CACHE_ISOLATED |
          (swapped ? IOPCOP0Status::CACHES_SWAPPED : 0);
        IOPCacheTestAccess::setStatus(&core, mode);
        REQUIRE(core.writeData32(0x00800200, 0x44332211).outcome ==
          IOPAccessOutcome::Completed);
        REQUIRE(core.writeData32(0x00800204, 0x88776655).outcome ==
          IOPAccessOutcome::Completed);
        IOPCacheTestAccess::setGeneralRegister(&core, 1, 0x00800200 + offset);
        const IOPWord left = 0x88220003;
        const IOPWord right = 0x98220000;
        REQUIRE(bus.write32(0x100, leftFirst ? left : right).status ==
          IOPBusStatus::Completed);
        REQUIRE(bus.write32(0x104, leftFirst ? right : left).status ==
          IOPBusStatus::Completed);
        const auto before = core.cache();
        core.startExecution(0xa0000100);
        core.stepInstruction();
        core.haltExecution();
        core.resumeExecution();
        core.stepInstruction();
        core.stepInstruction();
        REQUIRE(core.generalRegister(2) ==
          static_cast<IOPWord>(UINT64_C(0x8877665544332211) >> (offset * 8)));
        REQUIRE(core.status() == mode);
        REQUIRE(core.retiredInstructions() == 3);
        REQUIRE(core.elapsedCycles() == 3);
        REQUIRE(core.exception().kind == IOPException::None);
        requireUnchangedCache(core.cache(), before);
      }
    }
  }
}

TEST_CASE("IOP MTC0 isolation and swap maintenance restore external visibility without snooping",
  "[iop][cache][isolation][execution]")
{
  for (const bool swapped : {false, true})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    REQUIRE(bus.write32(0x200, 0x24030001).status == IOPBusStatus::Completed);
    core.startExecution(0x80000200);
    REQUIRE(core.fetchInstruction().value == 0x24030001);
    REQUIRE(core.readData32(0x80000200).value == 0x24030001);
    REQUIRE(bus.write32(0x200, 0x24030002).status == IOPBusStatus::Completed);
    const IOPWord mode = IOPCOP0Status::CACHE_ISOLATED |
      (swapped ? IOPCOP0Status::CACHES_SWAPPED : 0);
    IOPCacheTestAccess::setGeneralRegister(&core, 1, mode);
    IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHE_PROBE_RESULT);
    const IOPWord program[] = {
      0x40816000, // MTC0 r1, Status.
      0xa0000200, // SB r0, 0x200(r0) invalidates only the selected bank.
      0x40806000, // MTC0 r0, Status.
      0x8c020200, // LW r2, 0x200(r0).
      0
    };
    for (IOPAddress index = 0; index < sizeof(program) / sizeof(program[0]); ++index)
    {
      REQUIRE(bus.write32(0x100 + index * 4, program[index]).status ==
        IOPBusStatus::Completed);
    }
    core.startExecution(0xa0000100);
    core.stepInstruction();
    REQUIRE((core.status() & mode) == mode);
    REQUIRE(core.cache().instructionLine(0x200 / 16).valid);
    REQUIRE(core.cache().dataLine(0x200 / 4).valid);
    core.stepInstruction();
    REQUIRE(core.cache().instructionLine(0x200 / 16).valid == !swapped);
    REQUIRE(core.cache().dataLine(0x200 / 4).valid == swapped);
    core.stepInstruction();
    core.stepInstruction();
    core.stepInstruction();
    REQUIRE(core.generalRegister(2) == (swapped ? 0x24030001u : 0x24030002u));
    REQUIRE(bus.read32(0x200).value == 0x24030002);
    core.startExecution(0x80000200);
    REQUIRE(core.fetchInstruction().value == (swapped ? 0x24030002u : 0x24030001u));
    REQUIRE(core.status() ==
      (IOPCOP0Status::TLB_SHUTDOWN | IOPCOP0Status::CACHE_PROBE_RESULT));
    REQUIRE(core.exception().kind == IOPException::None);
    REQUIRE(core.retiredInstructions() == 5);
    REQUIRE(core.elapsedCycles() == 5);
  }
}

TEST_CASE("IOP isolated load faults preserve probe state and publish older delayed results",
  "[iop][cache][isolation][execution]")
{
  for (const bool swapped : {false, true})
  {
    IOPBus bus;
    IOPCore core;
    core.attachBus(&bus);
    const IOPWord mode = IOPCOP0Status::CACHE_ISOLATED |
      (swapped ? IOPCOP0Status::CACHES_SWAPPED : 0);
    IOPCacheTestAccess::setStatus(&core, mode);
    REQUIRE(core.writeData32(0x00800200, 0x89abcdef).outcome ==
      IOPAccessOutcome::Completed);
    const IOPAddress capacity = swapped ? IOPCache::INSTRUCTION_SIZE
      : IOPCache::DATA_SIZE;
    IOPCacheTestAccess::setGeneralRegister(&core, 1, 0x00800200 + capacity);
    REQUIRE(bus.write32(0x100, 0x8c220000).status == IOPBusStatus::Completed);
    REQUIRE(bus.write32(0x104, 0x8c230001).status == IOPBusStatus::Completed);
    const auto before = core.cache();
    core.startExecution(0xa0000100);
    core.stepInstruction();
    REQUIRE(core.status() == (mode | IOPCOP0Status::CACHE_PROBE_RESULT));
    core.stepInstruction();
    REQUIRE(core.exception().kind == IOPException::AddressErrorLoadOrFetch);
    REQUIRE(core.exception().badVirtualAddress == 0x00800201 + capacity);
    REQUIRE(core.exceptionProgramCounter() == 0xa0000104);
    REQUIRE(core.generalRegister(2) == 0x89abcdef);
    REQUIRE(core.generalRegister(3) == 0);
    REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
    REQUIRE(core.status() == (mode | IOPCOP0Status::CACHE_PROBE_RESULT));
    REQUIRE(core.retiredInstructions() == 1);
    REQUIRE(core.elapsedCycles() == 2);
    requireUnchangedCache(core.cache(), before);
  }
}

TEST_CASE("IOP indexed cache boundaries reject malformed requests before mutation",
  "[iop][cache][isolation]")
{
  IOPBus bus;
  IOPCache cache;
  IOPCacheTestAccess::seed(&cache);
  const auto before = cache;
  for (const auto bank : {IOPCacheBank::Instruction, IOPCacheBank::Data})
  {
    for (const std::uint8_t mask : {0, 0x10, 0xff})
    {
      REQUIRE_THROWS_AS(cache.commitIndexedStore(bank, 0x114, 0x89abcdef, mask),
        std::invalid_argument);
    }
    for (const IOPAddress address : {1u, 2u, 3u, 0xffffffffu})
    {
      REQUIRE_THROWS_AS(cache.readIsolatedWord(bank, address), std::invalid_argument);
      REQUIRE_THROWS_AS(cache.commitIndexedStore(bank, address, 0x89abcdef, 0xf),
        std::invalid_argument);
    }
  }
  const auto invalidBank = static_cast<IOPCacheBank>(0xff);
  REQUIRE_THROWS_AS(cache.readWord(invalidBank, bus, 4), std::invalid_argument);
  REQUIRE_THROWS_AS(cache.readIsolatedWord(invalidBank, 4), std::invalid_argument);
  REQUIRE_THROWS_AS(cache.commitStore(invalidBank, 4, 0x89abcdef, 0xf),
    std::invalid_argument);
  REQUIRE_THROWS_AS(cache.commitIndexedStore(invalidBank, 4, 0x89abcdef, 0xf),
    std::invalid_argument);
  requireUnchangedCache(cache, before);
}

TEST_CASE("IOP isolated and swapped access families reconcile ROM configuration",
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
      REQUIRE(outcome == IOPAccessOutcome::Completed);
      REQUIRE(exception == IOPException::None);
      REQUIRE(core.status() == status);
      const IOPWord expectedMemory = isolated || access < 4 ? 0x11223344
        : access == 4 ? 0x11223355 : access == 5 ? 0x11225566 : 0x55667788;
      REQUIRE(bus.read32(0x100).value == expectedMemory);
      REQUIRE(oldImage.expired());
      REQUIRE_FALSE(core.cache().instructionLine(1).valid);
      REQUIRE_FALSE(core.cache().dataLine(1).valid);
      REQUIRE(core.elapsedCycles() == 0);
      REQUIRE(core.retiredInstructions() == 0);
      REQUIRE(core.executionState() == IOPExecutionState::Running);
    }
  }
}

TEST_CASE("IOP isolated and swapped execution retires through existing continuation boundaries",
  "[iop][cache][fetch][admission][execution]")
{
  // Fetch, all implemented load families, and every physical store boundary.
  const IOPWord instructions[] = {
    0, 0x80000100, 0x84000100, 0x8c000100, 0x88000101, 0x98000101,
    0xa0000100, 0xa4000100, 0xac000100, 0xa8000101, 0xb8000101
  };
  for (unsigned mode = 1; mode < 4; ++mode)
  {
    for (const auto instruction : instructions)
    {
      const bool isolated = (mode & 1) != 0;
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      REQUIRE(bus.write32(0, instruction).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(0x100, 0x11223344).status == IOPBusStatus::Completed);
      core.startExecution(instruction == 0 ? 0x80000000 : 0xa0000000);
      IOPCacheTestAccess::seed(&core);
      IOPCacheTestAccess::setContinuations(&core);
      const IOPWord status = IOPCOP0Status::CACHE_PROBE_RESULT |
        (isolated ? IOPCOP0Status::CACHE_ISOLATED : 0) |
        ((mode & 2) != 0 ? IOPCOP0Status::CACHES_SWAPPED : 0);
      IOPCacheTestAccess::setStatus(&core, status);
      core.stepInstruction();
      REQUIRE(core.executionState() == IOPExecutionState::Running);
      REQUIRE(core.stopReason() == IOPStopReason::None);
      REQUIRE(core.exception().kind == IOPException::None);
      REQUIRE(core.storeFailure().width == IOPStoreWidth::None);
      REQUIRE(core.programCounter() == 0x200);
      REQUIRE(core.status() == status);
      REQUIRE(core.cause() == 0);
      REQUIRE(core.exceptionProgramCounter() == 0);
      REQUIRE(core.generalRegister(7) == 0x12345678);
      REQUIRE_FALSE(core.branchContinuation().active);
      REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
      REQUIRE(core.retiredInstructions() == 1);
      REQUIRE(core.elapsedCycles() == 1);
      const IOPWord opcode = instruction >> 26;
      const IOPWord expectedMemory = isolated || opcode < 0x28 ? 0x11223344
        : opcode == 0x28 ? 0x11223300
        : opcode == 0x29 || opcode == 0x2a ? 0x11220000
        : opcode == 0x2e ? 0x44 : 0;
      REQUIRE(bus.read32(0x100).value == expectedMemory);
    }
  }
}

TEST_CASE("IOP isolation permits byte accesses at uncached and non-memory boundaries",
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
    const auto before = core.cache();
    REQUIRE(core.readData8(address).outcome == IOPAccessOutcome::Completed);
    REQUIRE(core.writeData8(address, 0x55).outcome ==
      IOPAccessOutcome::Completed);
    const auto location = IOPCache::location(IOPCacheBank::Data,
      core.classifyAddress(address).physicalAddress);
    requireIndexedStore(core.cache().dataLine(location.lineIndex),
      before.dataLine(location.lineIndex), location, 0x55, 1);
    requireUnchangedCache(core.cache(), before, location.lineIndex);
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
  REQUIRE(core.status() == (mode | IOPCOP0Status::CURRENT_USER_MODE));
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

TEST_CASE("IOP cache invalidation through aliases and RAM mirrors maintains only the selected bank",
  "[iop][cache][conformance]")
{
  for (const bool swapped : {false, true})
  {
    for (IOPAddress word = 0; word < IOPCache::INSTRUCTION_LINE_WORDS; ++word)
    {
      const IOPAddress physical = 0x220 + word * 4;
      for (const IOPAddress alias :
           {physical, physical | 0x80000000u, physical | 0xa0000000u,
            physical + static_cast<IOPAddress>(IOPBus::RAM_SIZE)})
      {
        CAPTURE(swapped);
        CAPTURE(word);
        CAPTURE(alias);
        IOPBus bus;
        IOPCore core;
        core.attachBus(&bus);
        REQUIRE(bus.write32(physical, 0x11223344).status == IOPBusStatus::Completed);
        core.startExecution(physical | 0x80000000u);
        REQUIRE(core.fetchInstruction().value == 0x11223344);
        REQUIRE(core.readData32(physical).value == 0x11223344);
        const std::uint8_t replacement[] = {0x88, 0x77, 0x66, 0x55};
        REQUIRE(bus.loadPhysical(physical, replacement, sizeof(replacement)) ==
          IOPBusStatus::Completed);
        const auto before = core.cache();
        const auto bank = swapped ? IOPCacheBank::Instruction : IOPCacheBank::Data;
        const auto location = IOPCache::location(bank, physical);
        IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHE_ISOLATED |
          IOPCOP0Status::CACHE_PROBE_RESULT |
          (swapped ? IOPCOP0Status::CACHES_SWAPPED : 0));
        REQUIRE(core.writeData8(alias, 0).outcome == IOPAccessOutcome::Completed);
        if (swapped)
        {
          requireIndexedStore(core.cache().instructionLine(location.lineIndex),
            before.instructionLine(location.lineIndex), location, 0, 1);
          requireUnchangedCache(core.cache(), before, IOPCache::DATA_LINE_COUNT,
            location.lineIndex);
        }
        else
        {
          requireIndexedStore(core.cache().dataLine(location.lineIndex),
            before.dataLine(location.lineIndex), location, 0, 1);
          requireUnchangedCache(core.cache(), before, location.lineIndex);
        }
        REQUIRE(bus.read32(physical).value == 0x55667788);
        IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::CACHE_PROBE_RESULT);
        REQUIRE(core.fetchInstruction().value == (swapped ? 0x55667788u : 0x11223344u));
        REQUIRE(core.readData32(physical).value == (swapped ? 0x11223344u : 0x55667788u));
        REQUIRE(core.status() == IOPCOP0Status::CACHE_PROBE_RESULT);
        REQUIRE(core.elapsedCycles() == 0);
        REQUIRE(core.retiredInstructions() == 0);
      }
    }
  }
}

TEST_CASE("IOP cached continuation outcomes repeat identically across host pause and resume",
  "[iop][cache][conformance]")
{
  const auto rom = makeRom(0x12);
  for (unsigned mode = 0; mode < 4; ++mode)
  {
    const bool isolated = (mode & 1) != 0;
    const IOPWord controls = (isolated ? IOPCOP0Status::CACHE_ISOLATED : 0) |
      ((mode & 2) != 0 ? IOPCOP0Status::CACHES_SWAPPED : 0);
    for (const bool firstIsBranch : {false, true})
    {
      for (unsigned endpoint = 0; endpoint < 3; ++endpoint)
      {
        IOPBus baselineBus;
        IOPBus pausedBus;
        REQUIRE(baselineBus.installRom(rom));
        REQUIRE(pausedBus.installRom(rom));
        IOPCore baseline;
        IOPCore paused;
        baseline.attachBus(&baselineBus);
        paused.attachBus(&pausedBus);
        IOPCache repeatedCache;
        for (unsigned repetition = 0; repetition < 3; ++repetition)
        {
          CAPTURE(mode);
          CAPTURE(firstIsBranch);
          CAPTURE(endpoint);
          CAPTURE(repetition);
          const IOPAddress destination = endpoint == 0 ? 0x80000240
            : endpoint == 1 ? 0x9fc00040 : 0x80000201;
          const auto setup = [&](IOPCore &core, IOPBus &bus)
          {
            bus.reset();
            core.prepareFreshExecution(0x80000100, 0, 0);
            requireClear(core.cache());
            REQUIRE_FALSE(core.branchContinuation().active);
            REQUIRE(core.delayedResultContinuation().source ==
              IOPDelayedResultSource::None);
            REQUIRE(core.elapsedCycles() == 0);
            REQUIRE(core.retiredInstructions() == 0);
            REQUIRE(bus.write32(0x100, firstIsBranch ? 0x10000003 : 0x8c070200).status ==
              IOPBusStatus::Completed);
            REQUIRE(bus.write32(0x104, 0xac220000).status == IOPBusStatus::Completed);
            REQUIRE(bus.write32(0x200, 0x11223344).status == IOPBusStatus::Completed);
            IOPCacheTestAccess::setStatus(&core, IOPCOP0Status::TLB_SHUTDOWN |
              controls | IOPCOP0Status::CACHE_PROBE_RESULT);
            if (isolated)
            {
              REQUIRE(core.writeData32(0x200, 0x11223344).outcome ==
                IOPAccessOutcome::Completed);
            }
            else
            {
              REQUIRE(core.readData32(0x200).value == 0x11223344);
            }
            REQUIRE(bus.write32(0x200, 0x55667788).status == IOPBusStatus::Completed);
            IOPCacheTestAccess::setGeneralRegister(&core, 1, destination);
            IOPCacheTestAccess::setGeneralRegister(&core, 2, 0x89abcdef);
            IOPCacheTestAccess::setGeneralRegister(&core, 7, 0x77);
            core.startExecution(0x80000100);
            core.stepInstruction();
            REQUIRE(core.programCounter() == 0x80000104);
            REQUIRE(core.branchContinuation().active == firstIsBranch);
            REQUIRE(core.delayedResultContinuation().source ==
              (firstIsBranch ? IOPDelayedResultSource::None : IOPDelayedResultSource::Load));
            REQUIRE(core.generalRegister(7) == 0x77);
            REQUIRE(core.fetchInstruction().value == 0xac220000);
          };
          setup(baseline, baselineBus);
          setup(paused, pausedBus);
          requireSameCoreState(paused, baseline);
          const auto beforeStore = baseline.cache();
          paused.haltExecution();
          REQUIRE(paused.stopReason() == IOPStopReason::HostHalt);
          REQUIRE_THROWS_AS(paused.stepInstruction(), std::logic_error);
          REQUIRE(paused.elapsedCycles() == 1);
          requireUnchangedCache(paused.cache(), beforeStore);
          paused.resumeExecution();
          requireSameCoreState(paused, baseline);
          baseline.stepInstruction();
          paused.stepInstruction();
          requireSameCoreState(paused, baseline);
          const bool rejected = endpoint == 1 && !isolated;
          const bool faulted = endpoint == 2;
          REQUIRE(baseline.elapsedCycles() == 2);
          REQUIRE(baseline.retiredInstructions() == (rejected || faulted ? 1 : 2));
          REQUIRE(baseline.status() == (IOPCOP0Status::TLB_SHUTDOWN | controls |
            (firstIsBranch || !isolated ? IOPCOP0Status::CACHE_PROBE_RESULT : 0)));
          REQUIRE(baseline.generalRegister(7) ==
            (firstIsBranch || rejected ? 0x77u : 0x11223344u));
          REQUIRE(baseline.branchContinuation().active == (firstIsBranch && rejected));
          REQUIRE(baseline.delayedResultContinuation().source ==
            (!firstIsBranch && rejected ? IOPDelayedResultSource::Load
                                       : IOPDelayedResultSource::None));
          REQUIRE(baseline.executionState() == (rejected ? IOPExecutionState::Halted
                                                       : IOPExecutionState::Running));
          REQUIRE(baseline.stopReason() == (rejected ? IOPStopReason::StoreFailure
                                                   : IOPStopReason::None));
          REQUIRE(baseline.exception().kind == (faulted ? IOPException::AddressErrorStore
                                                       : IOPException::None));
          REQUIRE(baseline.exception().inBranchDelaySlot == (faulted && firstIsBranch));
          REQUIRE(baseline.programCounter() == (faulted ? IOPExceptionVector::GENERAL
            : rejected ? 0x80000104u : firstIsBranch ? 0x80000110u : 0x80000108u));
          if (faulted)
          {
            REQUIRE(baseline.exceptionProgramCounter() ==
              (firstIsBranch ? 0x80000100u : 0x80000104u));
            REQUIRE(baseline.badVirtualAddress() == destination);
          }
          if (rejected)
          {
            REQUIRE(baseline.storeFailure().width == IOPStoreWidth::Word);
            REQUIRE(baseline.storeFailure().value == 0x89abcdef);
            REQUIRE(baseline.storeFailure().access.outcome == IOPAccessOutcome::ReadOnly);
            REQUIRE(baseline.storeFailure().access.virtualAddress == destination);
            REQUIRE_THROWS_AS(baseline.resumeExecution(), std::logic_error);
            REQUIRE_THROWS_AS(paused.resumeExecution(), std::logic_error);
            requireSameCoreState(paused, baseline);
            REQUIRE(baseline.executionState() == IOPExecutionState::Halted);
          }
          else
          {
            REQUIRE(baseline.storeFailure().width == IOPStoreWidth::None);
          }
          if (rejected || faulted)
          {
            requireUnchangedCache(baseline.cache(), beforeStore);
          }
          else
          {
            const auto bank = (mode & 2) != 0 ? IOPCacheBank::Instruction
                                            : IOPCacheBank::Data;
            const auto location = IOPCache::location(bank, destination & 0x1fffffff);
            if (bank == IOPCacheBank::Instruction)
            {
              requireIndexedStore(baseline.cache().instructionLine(location.lineIndex),
                beforeStore.instructionLine(location.lineIndex), location, 0x89abcdef, 0xf);
              requireUnchangedCache(baseline.cache(), beforeStore,
                IOPCache::DATA_LINE_COUNT, location.lineIndex);
            }
            else
            {
              requireIndexedStore(baseline.cache().dataLine(location.lineIndex),
                beforeStore.dataLine(location.lineIndex), location, 0x89abcdef, 0xf);
              requireUnchangedCache(baseline.cache(), beforeStore, location.lineIndex);
            }
          }
          REQUIRE(baselineBus.read32(0x240).value ==
            (endpoint == 0 && !isolated ? 0x89abcdefu : 0));
          REQUIRE(baselineBus.read32(0x200).value == 0x55667788);
          REQUIRE(baselineBus.read32(IOPBus::ROM_BASE + 0x40).value == 0);
          std::vector<std::uint8_t> baselineMemory(0x250);
          std::vector<std::uint8_t> pausedMemory(baselineMemory.size());
          REQUIRE(baselineBus.inspectPhysical(0, baselineMemory.data(),
            baselineMemory.size()) == IOPBusStatus::Completed);
          REQUIRE(pausedBus.inspectPhysical(0, pausedMemory.data(),
            pausedMemory.size()) == IOPBusStatus::Completed);
          REQUIRE(pausedMemory == baselineMemory);
          if (repetition != 0)
          {
            requireUnchangedCache(baseline.cache(), repeatedCache);
          }
          repeatedCache = baseline.cache();
        }
      }
    }
  }
}

TEST_CASE("IOP INTC handler execution repeats identically across host pauses and cache bank swapping",
  "[iop][cache][interrupt][intc][conformance]")
{
  for (const bool swapped : {false, true})
  {
    for (const bool firstIsBranch : {false, true})
    {
      IOPBus baselineBus;
      IOPBus pausedBus;
      IOPInterruptController baselineInterrupts;
      IOPInterruptController pausedInterrupts;
      baselineBus.attachInterruptController(&baselineInterrupts);
      pausedBus.attachInterruptController(&pausedInterrupts);
      IOPCore baseline;
      IOPCore paused;
      baseline.attachBus(&baselineBus);
      paused.attachBus(&pausedBus);
      IOPCore repeatedResult;
      std::vector<std::uint8_t> baselineMemory(0x250);
      std::vector<std::uint8_t> pausedMemory(baselineMemory.size());
      for (unsigned repetition = 0; repetition < 3; ++repetition)
      {
        CAPTURE(swapped);
        CAPTURE(firstIsBranch);
        CAPTURE(repetition);
        const auto setup = [&](IOPCore &core, IOPBus &bus,
                               IOPInterruptController &interrupts)
        {
          interrupts.reset();
          bus.reset();
          core.prepareFreshExecution(0x80000100, 0, 0);
          requireClear(core.cache());
          interrupts.writeMask(0x02000010);
          interrupts.writeControl(1);
          IOPCacheTestAccess::setStatus(&core, 0x00200401 |
            (swapped ? IOPCOP0Status::CACHES_SWAPPED : 0));
          IOPCacheTestAccess::setGeneralRegister(&core, 1, IOPBus::INTC_CTRL);
          IOPCacheTestAccess::setGeneralRegister(&core, 2, IOPBus::INTC_STAT);
          IOPCacheTestAccess::setGeneralRegister(&core, 3, 0x80000240);
          IOPCacheTestAccess::setGeneralRegister(&core, 7, 0x77);
          const IOPWord program[] = {
            firstIsBranch ? 0x10000007u : 0x8c270000u,
            0xac670000,
            0x08000048,
            0
          };
          const IOPWord handler[] = {
            0xac400000,
            0x401a7000,
            0,
            0x03400008,
            0x42000010
          };
          for (std::size_t index = 0; index < sizeof(program) / sizeof(program[0]); ++index)
          {
            REQUIRE(bus.write32(0x100 + 4 * index, program[index]).status ==
              IOPBusStatus::Completed);
          }
          for (std::size_t index = 0; index < sizeof(handler) / sizeof(handler[0]); ++index)
          {
            REQUIRE(bus.write32(0x80 + 4 * index, handler[index]).status ==
              IOPBusStatus::Completed);
          }
          REQUIRE(bus.write32(0x120, 0x24080009).status == IOPBusStatus::Completed);
          core.startExecution(0x80000100);
        };
        setup(baseline, baselineBus, baselineInterrupts);
        setup(paused, pausedBus, pausedInterrupts);
        for (unsigned step = 0; step < 12; ++step)
        {
          CAPTURE(step);
          const bool pause = repetition == 0 || (repetition == 1 && step % 2 != 0) ||
            step == 1 || step == 6;
          if (pause)
          {
            paused.haltExecution();
          }
          if (step == 1)
          {
            REQUIRE(baseline.branchContinuation().active == firstIsBranch);
            REQUIRE(baseline.delayedResultContinuation().source ==
              (firstIsBranch ? IOPDelayedResultSource::None : IOPDelayedResultSource::Load));
            REQUIRE(paused.cause() == 0);
            for (IOPInterruptController *interrupts :
                 {&baselineInterrupts, &pausedInterrupts})
            {
              interrupts->assertSource(IOPInterruptSource::Timer0);
              interrupts->assertSource(IOPInterruptSource::FireWireDMA);
              interrupts->assertSource(IOPInterruptSource::Timer0);
              interrupts->writeControl(1);
            }
          }
          if (pause)
          {
            REQUIRE_THROWS_AS(paused.stepInstruction(), std::logic_error);
            REQUIRE(paused.elapsedCycles() == baseline.elapsedCycles());
            REQUIRE(paused.retiredInstructions() == baseline.retiredInstructions());
            paused.resumeExecution();
          }
          requireSameCoreState(paused, baseline);
          baseline.stepInstruction();
          paused.stepInstruction();
          requireSameCoreState(paused, baseline);
          REQUIRE(pausedInterrupts.status() == baselineInterrupts.status());
          REQUIRE(pausedInterrupts.mask() == baselineInterrupts.mask());
          REQUIRE(pausedInterrupts.control() == baselineInterrupts.control());
          REQUIRE(pausedInterrupts.interruptPending() == baselineInterrupts.interruptPending());
          REQUIRE(baselineBus.inspectPhysical(0, baselineMemory.data(),
            baselineMemory.size()) == IOPBusStatus::Completed);
          REQUIRE(pausedBus.inspectPhysical(0, pausedMemory.data(),
            pausedMemory.size()) == IOPBusStatus::Completed);
          REQUIRE(pausedMemory == baselineMemory);
          if (step == 1)
          {
            REQUIRE(baseline.exception().kind == IOPException::Interrupt);
            REQUIRE(baseline.exception().inBranchDelaySlot == firstIsBranch);
            REQUIRE(baseline.exceptionProgramCounter() ==
              (firstIsBranch ? 0x80000100 : 0x80000104));
            REQUIRE(baselineInterrupts.status() == 0x02000010);
          }
          else
          {
            REQUIRE(baseline.exception().kind == IOPException::None);
          }
        }
        REQUIRE(baseline.programCounter() == (firstIsBranch ? 0x8000012c : 0x80000128));
        REQUIRE(baseline.generalRegister(7) == (firstIsBranch ? 0x77 : 1));
        REQUIRE(baseline.generalRegister(8) == 9);
        REQUIRE(baselineBus.read32(0x240).value == (firstIsBranch ? 0x77 : 1));
        REQUIRE(baselineInterrupts.status() == 0);
        REQUIRE(baselineInterrupts.mask() == 0x02000010);
        REQUIRE(baselineInterrupts.control() == 1);
        REQUIRE(baseline.cause() == (firstIsBranch ? IOPCOP0Cause::BRANCH_DELAY : 0));
        REQUIRE(baseline.elapsedCycles() == 12);
        REQUIRE(baseline.retiredInstructions() == 11);
        if (repetition != 0)
        {
          requireSameCoreState(baseline, repeatedResult);
        }
        // Inspection-only snapshot; the independently owned devices are not copied.
        repeatedResult = baseline;
      }
    }
  }
}

TEST_CASE("IOP restart discards real continuations but only fresh execution and reset clear caches",
  "[iop][cache][conformance]")
{
  for (unsigned mode = 0; mode < 4; ++mode)
  {
    for (const bool firstIsBranch : {false, true})
    {
      for (const bool freshExecution : {false, true})
      {
        CAPTURE(mode);
        CAPTURE(firstIsBranch);
        CAPTURE(freshExecution);
        IOPBus bus;
        IOPCore core;
        core.attachBus(&bus);
        REQUIRE(bus.write32(0x100, firstIsBranch ? 0x10000003 : 0x8c070200).status ==
          IOPBusStatus::Completed);
        REQUIRE(bus.write32(0x200, 0x11223344).status == IOPBusStatus::Completed);
        IOPCacheTestAccess::seed(&core);
        const IOPWord controls = ((mode & 1) != 0 ? IOPCOP0Status::CACHE_ISOLATED : 0) |
          ((mode & 2) != 0 ? IOPCOP0Status::CACHES_SWAPPED : 0);
        IOPCacheTestAccess::setStatus(&core, controls);
        for (unsigned restart = 0; restart < 2; ++restart)
        {
          core.startExecution(0xa0000100);
          core.stepInstruction();
          REQUIRE(core.branchContinuation().active == firstIsBranch);
          REQUIRE(core.delayedResultContinuation().source ==
            (firstIsBranch ? IOPDelayedResultSource::None : IOPDelayedResultSource::Load));
          const auto caches = core.cache();
          core.haltExecution();
          const auto status = core.status();
          core.startExecution(0xa0000100);
          REQUIRE_FALSE(core.branchContinuation().active);
          REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
          REQUIRE(core.generalRegister(7) == 0);
          REQUIRE(core.status() == status);
          REQUIRE(core.elapsedCycles() == restart + 1);
          REQUIRE(core.retiredInstructions() == restart + 1);
          requireUnchangedCache(core.cache(), caches);
        }
        core.stepInstruction();
        REQUIRE(core.branchContinuation().active == firstIsBranch);
        REQUIRE(core.delayedResultContinuation().source ==
          (firstIsBranch ? IOPDelayedResultSource::None : IOPDelayedResultSource::Load));
        if (freshExecution)
        {
          core.prepareFreshExecution(0x80000300, 0x1230, 0x4560);
        }
        else
        {
          core.reset();
        }
        requireClear(core.cache());
        REQUIRE(core.programCounter() == (freshExecution ? 0x80000300u : IOPReset::VECTOR));
        REQUIRE(core.generalRegister(29) == (freshExecution ? 0x1230 : 0));
        REQUIRE(core.generalRegister(31) == (freshExecution ? 0x4560 : 0));
        REQUIRE(core.generalRegister(7) == 0);
        REQUIRE_FALSE(core.branchContinuation().active);
        REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
        REQUIRE(core.status() ==
          (IOPCOP0Status::BOOT_EXCEPTION_VECTORS | IOPCOP0Status::TLB_SHUTDOWN));
        REQUIRE(core.executionState() == IOPExecutionState::Halted);
        REQUIRE(core.stopReason() == IOPStopReason::None);
        REQUIRE(core.elapsedCycles() == 0);
        REQUIRE(core.retiredInstructions() == 0);
        REQUIRE(bus.read32(0x200).value == 0x11223344);
      }
    }
  }
}

TEST_CASE("IOP exception entry and JR RFE return preserve cache controls and stale payloads",
  "[iop][cache][conformance]")
{
  for (unsigned mode = 0; mode < 4; ++mode)
  {
    for (const bool firstIsBranch : {false, true})
    {
      CAPTURE(mode);
      CAPTURE(firstIsBranch);
      IOPBus bus;
      IOPCore core;
      core.attachBus(&bus);
      REQUIRE(bus.write32(0x200, 0x11223344).status == IOPBusStatus::Completed);
      core.startExecution(0x80000200);
      REQUIRE(core.fetchInstruction().value == 0x11223344);
      REQUIRE(core.readData32(0x200).value == 0x11223344);
      REQUIRE(bus.write32(0x200, 0x55667788).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(0x100, firstIsBranch ? 0x10000003 : 0x8c070200).status ==
        IOPBusStatus::Completed);
      REQUIRE(bus.write32(0x104, 0x0000000c).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(0x80, 0x00200008).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(0x84, 0x42000010).status == IOPBusStatus::Completed);
      REQUIRE(bus.write32(0x300, 0).status == IOPBusStatus::Completed);
      const IOPWord controls = ((mode & 1) != 0 ? IOPCOP0Status::CACHE_ISOLATED : 0) |
        ((mode & 2) != 0 ? IOPCOP0Status::CACHES_SWAPPED : 0);
      const IOPWord initialStatus = IOPCOP0Status::TLB_SHUTDOWN | controls |
        IOPCOP0Status::CACHE_PROBE_RESULT | 0x15;
      IOPCacheTestAccess::setStatus(&core, initialStatus);
      IOPCacheTestAccess::setGeneralRegister(&core, 1, 0x80000300);
      IOPCacheTestAccess::setGeneralRegister(&core, 7, 0x77);
      core.startExecution(0xa0000100);
      core.stepInstruction();
      REQUIRE(core.branchContinuation().active == firstIsBranch);
      REQUIRE(core.delayedResultContinuation().source ==
        (firstIsBranch ? IOPDelayedResultSource::None : IOPDelayedResultSource::Load));
      const auto statusBeforeException = core.status();
      const auto beforeException = core.cache();
      core.stepInstruction();
      REQUIRE(core.exception().kind == IOPException::SystemCall);
      REQUIRE(core.exception().inBranchDelaySlot == firstIsBranch);
      REQUIRE(core.exceptionProgramCounter() == (firstIsBranch ? 0xa0000100u : 0xa0000104u));
      REQUIRE(core.cause() == ((firstIsBranch ? IOPCOP0Cause::BRANCH_DELAY : 0) | (8u << 2)));
      REQUIRE(core.status() == ((statusBeforeException & ~IOPWord(0x3f)) | 0x14));
      REQUIRE(core.programCounter() == IOPExceptionVector::GENERAL);
      REQUIRE(core.generalRegister(7) == (firstIsBranch ? 0x77u : 0x11223344u));
      REQUIRE_FALSE(core.branchContinuation().active);
      REQUIRE(core.delayedResultContinuation().source == IOPDelayedResultSource::None);
      requireUnchangedCache(core.cache(), beforeException);
      REQUIRE(core.fetchInstruction().value == 0x00200008);
      const auto beforeJR = core.cache();
      core.stepInstruction();
      REQUIRE(core.branchContinuation().active);
      REQUIRE(core.branchContinuation().targetAddress == 0x80000300);
      requireUnchangedCache(core.cache(), beforeJR);
      REQUIRE(core.fetchInstruction().value == 0x42000010);
      const auto beforeRFE = core.cache();
      core.haltExecution();
      core.resumeExecution();
      core.stepInstruction();
      REQUIRE(core.programCounter() == 0x80000300);
      REQUIRE(core.status() == statusBeforeException);
      REQUIRE_FALSE(core.branchContinuation().active);
      requireUnchangedCache(core.cache(), beforeRFE);
      core.stepInstruction();
      REQUIRE(core.exception().kind == IOPException::None);
      REQUIRE(core.elapsedCycles() == 5);
      REQUIRE(core.retiredInstructions() == 4);
      IOPCacheTestAccess::setStatus(&core, 0);
      core.startExecution(0x80000200);
      REQUIRE(core.fetchInstruction().value == 0x11223344);
      REQUIRE(core.readData32(0x200).value == 0x11223344);
      REQUIRE(bus.read32(0x200).value == 0x55667788);
    }
  }
}
