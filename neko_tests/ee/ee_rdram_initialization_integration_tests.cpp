#include <cstdint>
#include <stdexcept>

#include "catch.hpp"
#include "neko_system.hpp"
#include "save_state_mutation.hpp"

namespace
{
  constexpr std::uint32_t COMMAND = 0x1000f430;
  constexpr std::uint32_t DATA = 0x1000f440;
  constexpr std::uint32_t ENUMERATE = 0x80210000;
  constexpr std::uint32_t REINITIALIZE = 0x80210040;

  EEExecutionResult mergeStore(
    NekoSystem &system, std::uint32_t address, std::uint8_t opcode,
    std::uint16_t lane, std::uint32_t value)
  {
    system.eeBus().write32(0,
      (std::uint32_t(opcode) << 26) | (1u << 21) | (2u << 16) | lane);
    system.eeCore().setGeneralRegister(1, {address | 0xa0000000, 0});
    system.eeCore().setGeneralRegister(2, {value, 0});
    system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
    system.eeCore().startExecution(0xa0000000);
    system.startTrace(100);
    return system.stepEEInstruction(16);
  }
}

TEST_CASE("EE RDRAM initialization maps only physical word endpoints and host aliases",
  "[ee][bus][rdram-init-integration]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  std::uint32_t value = 0x12345678;
  REQUIRE(bus.readData32(COMMAND, &value));
  REQUIRE(value == 0);
  value = 0x12345678;
  REQUIRE_FALSE(bus.readData32(DATA, &value));
  REQUIRE(value == 0x12345678);
  for (const auto alias : {0u, 0x80000000u, 0xa0000000u})
  {
    CAPTURE(alias);
    REQUIRE_NOTHROW(bus.write32(DATA | alias, 0));
    REQUIRE_NOTHROW(bus.write32(COMMAND | alias, 0x800003a0));
    REQUIRE(bus.read32(COMMAND | alias) == 0);
    REQUIRE_THROWS_AS(bus.read32(DATA | alias), std::out_of_range);
    REQUIRE_NOTHROW(bus.write32(COMMAND | alias, 0x80230000));
    REQUIRE(bus.read32(DATA | alias) == 0x0d0d);
  }
  REQUIRE(bus.writeData32(COMMAND, 0x80240000));
  REQUIRE(bus.readData32(DATA, &value));
  REQUIRE(value == 0x0090);
  REQUIRE(bus.writeData32(COMMAND, 0x8040001b));
  REQUIRE(bus.readData32(DATA, &value));
  REQUIRE(value == 0x1b);
  const auto hash = system.eeStateHash();
  for (const auto address : {COMMAND | 0x80000000u, COMMAND | 0xa0000000u,
         DATA | 0x80000000u, DATA | 0xa0000000u,
         COMMAND - 4, COMMAND + 4, COMMAND + 8, COMMAND + 12,
         DATA + 4, DATA + 8, DATA + 12})
  {
    CAPTURE(address);
    value = 0x12345678;
    REQUIRE_FALSE(bus.readData32(address, &value));
    REQUIRE(value == 0x12345678);
    REQUIRE_FALSE(bus.writeData32(address, ENUMERATE));
  }
  REQUIRE_FALSE(bus.writeData32(COMMAND, 0));
  REQUIRE_FALSE(bus.writeData32(COMMAND, 0x80210400));
  REQUIRE_THROWS_AS(bus.write32(COMMAND, 0), std::out_of_range);
  REQUIRE(system.eeStateHash() == hash);
}

TEST_CASE("EE RDRAM initialization rejects non-word and backing accesses without consuming data",
  "[ee][bus][rdram-init-integration]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  REQUIRE(bus.writeData32(COMMAND, ENUMERATE));
  const auto hash = system.eeStateHash();
  std::uint8_t byte = 0x12;
  std::uint16_t half = 0x1234;
  std::uint32_t word = 0x12345678;
  std::uint64_t wide = UINT64_C(0x123456789abcdef0);
  EEQuadword quad = {wide, wide};
  for (const auto address : {COMMAND, DATA})
  {
    CAPTURE(address);
    for (unsigned lane = 0; lane < 4; ++lane)
    {
      REQUIRE_FALSE(bus.readData8(address + lane, &byte));
      REQUIRE_FALSE(bus.writeData8(address + lane, 0));
    }
    for (const auto lane : {0u, 2u})
    {
      REQUIRE_FALSE(bus.readData16(address + lane, &half));
      REQUIRE_FALSE(bus.writeData16(address + lane, 0));
    }
    REQUIRE_FALSE(bus.readData64(address, &wide));
    REQUIRE_FALSE(bus.writeData64(address, 0));
    REQUIRE_FALSE(bus.readData128(address, &quad));
    REQUIRE_FALSE(bus.writeData128(address, {0, 0}));
    REQUIRE_FALSE(bus.readInstruction32(address, &word));
    REQUIRE_FALSE(bus.isCacheFillRange(address, 4));
    REQUIRE_FALSE(bus.readDMAC128(address, &quad));
    REQUIRE_FALSE(bus.writeDMAC128(address, {0, 0}));
    for (const auto alias : {0u, 0x80000000u, 0xa0000000u})
    {
      REQUIRE_THROWS_AS(bus.read64(address | alias), std::out_of_range);
      REQUIRE_THROWS_AS(bus.write64(address | alias, 0), std::out_of_range);
      REQUIRE_THROWS_AS(bus.readQuadword(address | alias), std::out_of_range);
      REQUIRE_THROWS_AS(bus.writeQuadword(address | alias, {{0, 0, 0, 0}}),
        std::out_of_range);
    }
    REQUIRE_THROWS_AS(bus.readData32(address + 1, &word), std::invalid_argument);
    REQUIRE_THROWS_AS(bus.writeData32(address + 1, 0), std::invalid_argument);
    REQUIRE_THROWS_AS(bus.readData32(address, nullptr), std::invalid_argument);
  }
  REQUIRE(byte == 0x12);
  REQUIRE(half == 0x1234);
  REQUIRE(word == 0x12345678);
  REQUIRE(wide == UINT64_C(0x123456789abcdef0));
  REQUIRE(quad.low == wide);
  REQUIRE(quad.high == wide);
  REQUIRE(system.eeStateHash() == hash);
  REQUIRE(bus.read32(DATA) == 0x1f);
  REQUIRE(bus.read32(DATA) == 0x1f);
  REQUIRE(bus.read32(DATA) == 0);
}

TEST_CASE("EE RDRAM initialization full-mask guest merge stores never read the endpoint",
  "[ee][bus][guest][trace][rdram-init-integration]")
{
  for (const auto address : {COMMAND, DATA})
  {
    for (const std::uint8_t opcode : {0x2a, 0x2e})
    {
      for (std::uint16_t lane = 0; lane < 4; ++lane)
      {
        CAPTURE(address);
        CAPTURE(opcode);
        CAPTURE(lane);
        NekoSystem system;
        REQUIRE(system.eeBus().writeData32(COMMAND, ENUMERATE));
        const bool full = lane == (opcode == 0x2a ? 3 : 0);
        const auto result = mergeStore(system, address, opcode, lane, ENUMERATE);
        REQUIRE(result.pendingException ==
          (full ? EEException::None : EEException::DataBusErrorStore));
        REQUIRE_FALSE(result.cycleLimitReached);
        if (!full)
        {
          REQUIRE(result.exceptionAddress == (address | 0xa0000000) + lane);
          REQUIRE(system.eeCore().cop0Register(EECOP0Register::EPC) == 0xa0000000);
        }
        std::size_t accesses = 0;
        for (const auto &event : system.trace())
        {
          if (event.subsystem == NekoTraceSubsystem::EE &&
              event.type == NekoTraceEventType::MemoryAccess &&
              event.value0 == (address | 0xa0000000))
          {
            ++accesses;
            REQUIRE(event.value1 == ENUMERATE);
            REQUIRE(event.value2 == 0);
            REQUIRE(event.value3 == nekoPackEEMemoryTraceMetadata({
              address, 4, NekoEETraceMemory::AccessKind::DataStore,
              NekoEETraceMemory::TranslationOutcome::Translated,
              NekoEETraceMemory::CacheRoute::Uncached, 2, false, true,
              NekoEETraceMemory::CacheAccess::Bypassed,
              full ? NekoEETraceMemory::TransferOutcome::Completed
                   : NekoEETraceMemory::TransferOutcome::PhysicalBusError,
              full ? NekoEETraceMemory::FailurePhase::None
                   : NekoEETraceMemory::FailurePhase::Direct
            }));
          }
        }
        REQUIRE(accesses == 1);
        REQUIRE_FALSE(system.traceTruncated());
        REQUIRE(system.eeBus().read32(DATA) == 0x1f);
        REQUIRE(system.eeBus().read32(DATA) == 0x1f);
        REQUIRE(system.eeBus().read32(DATA) == 0);
      }
    }
  }
}

TEST_CASE("EE RDRAM initialization hashes every mutable field without peer effects",
  "[ee][rdram-init-integration][save-state]")
{
  NekoSystem system;
  auto &bus = system.eeBus();
  const auto initial = system.eeStateHash();
  const auto iop = system.iopStateHash();
  const auto sif = system.sifStateHash();
  REQUIRE(bus.writeData32(DATA, 0xabcdef01));
  REQUIRE(system.eeStateHash() != initial);
  REQUIRE(bus.writeData32(DATA, 0));
  REQUIRE(system.eeStateHash() == initial);
  REQUIRE(bus.writeData32(COMMAND, ENUMERATE));
  const auto selected = system.eeStateHash();
  REQUIRE(selected != initial);
  REQUIRE(bus.read32(COMMAND) == 0);
  REQUIRE(system.eeStateHash() == selected);
  REQUIRE(bus.read32(DATA) == 0x1f);
  const auto first = system.eeStateHash();
  REQUIRE(first != selected);
  REQUIRE(bus.read32(DATA) == 0x1f);
  const auto second = system.eeStateHash();
  REQUIRE(second != first);
  REQUIRE(bus.read32(DATA) == 0);
  REQUIRE(system.eeStateHash() == second);
  REQUIRE(bus.writeData32(COMMAND, 0x80230000));
  REQUIRE(system.eeStateHash() != second);
  REQUIRE(system.iopStateHash() == iop);
  REQUIRE(system.sifStateHash() == sif);
  REQUIRE(system.masterClockScheduler().currentCycle() == 0);
}

TEST_CASE("EE RDRAM initialization restores passive input and every enumeration continuation",
  "[ee][rdram-init-integration][save-state]")
{
  for (unsigned cursor = 0; cursor <= 2; ++cursor)
  {
    CAPTURE(cursor);
    NekoSystem original;
    REQUIRE(original.eeBus().writeData32(COMMAND, ENUMERATE));
    for (unsigned read = 0; read < cursor; ++read)
    {
      REQUIRE(original.eeBus().read32(DATA) == 0x1f);
    }
    REQUIRE(original.eeBus().writeData32(COMMAND, 0x800003a0));
    REQUIRE(original.eeBus().writeData32(DATA, 0x80 | cursor));
    const auto saved = original.saveState();
    const auto hash = original.eeStateHash();
    NekoSystem restored;
    REQUIRE(restored.eeBus().writeData32(COMMAND, 0x80240000));
    restored.loadState(saved);
    REQUIRE(restored.saveState() == saved);
    REQUIRE(restored.eeStateHash() == hash);
    REQUIRE_THROWS_AS(restored.eeBus().read32(DATA), std::out_of_range);
    for (NekoSystem *machine : {&original, &restored})
    {
      REQUIRE(machine->eeBus().writeData32(COMMAND, REINITIALIZE));
      REQUIRE(machine->eeBus().writeData32(COMMAND, ENUMERATE));
      REQUIRE(machine->eeBus().read32(DATA) == (cursor < 2 ? 0x1fu : 0u));
    }
    REQUIRE(restored.eeStateHash() == original.eeStateHash());
    const auto sourceHash = original.eeStateHash();
    REQUIRE(restored.eeBus().writeData32(DATA, 0));
    REQUIRE(restored.eeBus().writeData32(COMMAND, REINITIALIZE));
    REQUIRE(restored.eeBus().writeData32(COMMAND, ENUMERATE));
    REQUIRE(restored.eeBus().read32(DATA) == 0x1f);
    REQUIRE(original.eeStateHash() == sourceHash);
  }
}

TEST_CASE("EE RDRAM initialization restores each retained command kind without replay",
  "[ee][rdram-init-integration][save-state]")
{
  for (const auto command : {0u, 0x800003a0u, REINITIALIZE | 0x3fu,
         ENUMERATE | 0x1fu, 0x8023001fu, 0x8024001fu, 0x8040001fu})
  {
    CAPTURE(command);
    NekoSystem original;
    if (command != 0)
    {
      REQUIRE(original.eeBus().writeData32(COMMAND, command));
    }
    REQUIRE(original.eeBus().writeData32(DATA, UINT32_MAX));
    const auto saved = original.saveState();
    NekoSystem restored;
    restored.loadState(saved);
    REQUIRE(restored.saveState() == saved);
    REQUIRE(restored.eeStateHash() == original.eeStateHash());
    std::uint32_t expected = 0x12345678;
    std::uint32_t actual = expected;
    REQUIRE(restored.eeBus().readData32(DATA, &actual) ==
      original.eeBus().readData32(DATA, &expected));
    REQUIRE(actual == expected);
  }
}

TEST_CASE("EE RDRAM initialization malformed continuations reject transactionally",
  "[ee][rdram-init-integration][save-state]")
{
  NekoSystem destination;
  REQUIRE(destination.eeBus().writeData32(COMMAND, ENUMERATE));
  REQUIRE(destination.eeBus().read32(DATA) == 0x1f);
  const auto original = destination.saveState();
  const auto hash = destination.eeStateHash();
  struct Corruption
  {
    const char *path;
    std::uint32_t value;
  };
  for (const auto mutation : {
         Corruption{"system.eeRDRAMInitialization.command", 0},
         Corruption{"system.eeRDRAMInitialization.command", ENUMERATE},
         Corruption{"system.eeRDRAMInitialization.command", 0x00210400},
         Corruption{"system.eeRDRAMInitialization.command", 0x00218000},
         Corruption{"system.eeRDRAMInitialization.command", 0x10210000},
         Corruption{"system.eeRDRAMInitialization.command", 0x00210020},
         Corruption{"system.eeRDRAMInitialization.command", 0x00220000},
         Corruption{"system.eeRDRAMInitialization.command", 0x00210080},
         Corruption{"system.eeRDRAMInitialization.command", 0x000003a1},
         Corruption{"system.eeRDRAMInitialization.enumeratedDevices", 3},
         Corruption{"system.eeRDRAMInitialization.enumeratedDevices", 0xff},
         Corruption{"container.version", 53}})
  {
    CAPTURE(mutation.path);
    CAPTURE(mutation.value);
    SaveStateMutation invalid(original);
    invalid.writeScalar(mutation.path, mutation.value);
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(destination.loadState(invalid), std::invalid_argument);
    REQUIRE(destination.eeStateHash() == hash);
    REQUIRE(destination.saveState() == original);
  }
  REQUIRE(destination.eeBus().read32(DATA) == 0x1f);
  REQUIRE(destination.eeBus().read32(DATA) == 0);
}

TEST_CASE("EE RDRAM initialization cold reset clears state and reattaches the bus",
  "[ee][rdram-init-integration][save-state]")
{
  NekoSystem system;
  REQUIRE(system.eeBus().writeData32(COMMAND, ENUMERATE));
  REQUIRE(system.eeBus().read32(DATA) == 0x1f);
  REQUIRE(system.eeBus().writeData32(DATA, UINT32_MAX));
  system.reset();
  NekoSystem fresh;
  REQUIRE(system.eeStateHash() == fresh.eeStateHash());
  REQUIRE(system.saveState() == fresh.saveState());
  REQUIRE(system.eeBus().read32(COMMAND) == 0);
  REQUIRE_THROWS_AS(system.eeBus().read32(DATA), std::out_of_range);
  REQUIRE(system.eeBus().writeData32(COMMAND, ENUMERATE));
  REQUIRE(system.eeBus().read32(DATA) == 0x1f);
  REQUIRE(system.eeBus().read32(DATA) == 0x1f);
  REQUIRE(system.eeBus().read32(DATA) == 0);
  REQUIRE_THROWS_AS(fresh.eeBus().read32(DATA), std::out_of_range);
}
