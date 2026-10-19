#include <cstdint>
#include <stdexcept>

#include "catch.hpp"
#include "neko_system.hpp"

namespace
{
  constexpr std::uint32_t RESERVED = 0x1000f500;
  constexpr std::uint32_t UNCACHED = 0xb000f500;

  void seed(NekoSystem &system)
  {
    system.eeBus().write32(0x2000, 0xabcdef01);
    system.eeBus().write32(EEMemoryMap::D_CTRL, 1);
    system.eeBus().write32(EEMemoryMap::D_SQWC, 0x00030002);
    system.eeBus().write32(EEMemoryMap::D2_MADR, 0x2000);
    system.eeBus().write32(EEMemoryMap::D2_QWC, 2);
    system.dmacController().signalChannelCompletion(DMACStatus::CHANNEL_2);
    system.dmacController().writeStatus(DMACStatus::CHANNEL_2_MASK);
    REQUIRE(system.eeBus().writeData32(EEMemoryMap::SIF_BASE, 0x87654321));
    REQUIRE(system.iopBus().write32(IOPBus::SIF_BASE + 0x10, 0xfedcba98).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.eeBus().writeData32(EEMemoryMap::SIF_BASE + 0x20, 0x80000001));
    REQUIRE(system.iopBus().write32(IOPBus::SIF_BASE + 0x30, 0x40000001).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.eeBus().writeData32(EEMemoryMap::SIF_BASE + 0x40, 0x40100));
    REQUIRE(system.iopBus().write32(IOPBus::SIF_DOORBELL, 2).status ==
      IOPBusStatus::Completed);
    REQUIRE(system.sif().pushSIF0Prologue({{1, 2, 3, 4}}) ==
      SIFTransferOutcome::Completed);
    REQUIRE(system.sif().pushSIF0PayloadWord(5, SIFPayloadBoundary::Continuing) ==
      SIFTransferOutcome::Completed);
    REQUIRE(system.sif().pushSIF1Quadword({{6, 7, 8, 9}}) ==
      SIFTransferOutcome::Completed);
    system.eeCore().startExecution(0xa0000000);
    system.iopCore().startExecution(0xa0000000);
  }

  EEExecutionResult execute(
    NekoSystem &system, std::uint8_t opcode, std::uint32_t value,
    std::uint16_t lane = 0)
  {
    const std::uint32_t instruction =
      (std::uint32_t(opcode) << 26) | (1u << 21) | (2u << 16) | lane;
    system.eeBus().write32(0, instruction);
    system.eeCore().setGeneralRegister(1, {UNCACHED, 0});
    system.eeCore().setGeneralRegister(2, {value, 0xfeedface});
    system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
    system.eeCore().startExecution(0xa0000000);
    system.startTrace();
    return system.stepEEInstruction(16);
  }

  void requireWordStoreObservation(
    const NekoSystem &system, std::uint32_t value, bool completed)
  {
    std::size_t stores = 0;
    for (const auto &event : system.trace())
    {
      REQUIRE(event.subsystem != NekoTraceSubsystem::SIF);
      if (event.subsystem != NekoTraceSubsystem::EE ||
          event.type != NekoTraceEventType::MemoryAccess ||
          event.value0 != UNCACHED)
      {
        continue;
      }
      ++stores;
      REQUIRE(event.value1 == value);
      REQUIRE(event.value2 == 0);
      REQUIRE(event.value3 == nekoPackEEMemoryTraceMetadata({
        RESERVED, 4,
        NekoEETraceMemory::AccessKind::DataStore,
        NekoEETraceMemory::TranslationOutcome::Translated,
        NekoEETraceMemory::CacheRoute::Uncached, 2, false, true,
        NekoEETraceMemory::CacheAccess::Bypassed,
        completed ? NekoEETraceMemory::TransferOutcome::Completed
                  : NekoEETraceMemory::TransferOutcome::PhysicalBusError,
        completed ? NekoEETraceMemory::FailurePhase::None
                  : NekoEETraceMemory::FailurePhase::Direct
      }));
    }
    REQUIRE(stores == 1);
  }
}

TEST_CASE("EE reserved startup word accepts stateless full-word writes",
  "[ee][bus][reserved]")
{
  NekoSystem system;
  seed(system);
  REQUIRE(EEMemoryMap::RESERVED_F500 == RESERVED);
  const auto before = system.saveState();
  const auto eeHash = system.eeStateHash();
  const auto iopHash = system.iopStateHash();
  const auto sifHash = system.sifStateHash();
  system.startTrace();
  for (const auto value : {0u, 0xffffffffu, 0xaaaaaaaau, 0x55555555u, 0xffffffffu})
  {
    CAPTURE(value);
    REQUIRE(system.eeBus().writeData32(RESERVED, value));
    REQUIRE_NOTHROW(system.eeBus().write32(RESERVED, value));
    REQUIRE_NOTHROW(system.eeBus().write32(UNCACHED, value));
  }
  REQUIRE(system.eeStateHash() == eeHash);
  REQUIRE(system.iopStateHash() == iopHash);
  REQUIRE(system.sifStateHash() == sifHash);
  const bool unchanged = system.saveState() == before;
  REQUIRE(unchanged);
  REQUIRE(system.trace().empty());
}

TEST_CASE("EE reserved startup word has no readback or wider access",
  "[ee][bus][reserved]")
{
  NekoSystem system;
  seed(system);
  auto &bus = system.eeBus();
  const auto before = system.saveState();
  REQUIRE(bus.writeData32(RESERVED, 0xffffffff));
  std::uint8_t byte = 0x12;
  std::uint16_t half = 0x1234;
  std::uint32_t word = 0x12345678;
  std::uint64_t wide = UINT64_C(0x123456789abcdef0);
  EEQuadword quad = {wide, wide};
  for (std::uint32_t lane = 0; lane < 4; ++lane)
  {
    REQUIRE_FALSE(bus.readData8(RESERVED + lane, &byte));
    REQUIRE_FALSE(bus.writeData8(RESERVED + lane, 0xff));
    REQUIRE_THROWS_AS(bus.write8(RESERVED + lane, 0xff), std::out_of_range);
  }
  for (std::uint32_t lane : {0u, 2u})
  {
    REQUIRE_FALSE(bus.readData16(RESERVED + lane, &half));
    REQUIRE_FALSE(bus.writeData16(RESERVED + lane, 0xffff));
  }
  REQUIRE_FALSE(bus.readData32(RESERVED, &word));
  REQUIRE_FALSE(bus.readInstruction32(RESERVED, &word));
  REQUIRE_FALSE(bus.readData64(RESERVED, &wide));
  REQUIRE_FALSE(bus.writeData64(RESERVED, UINT64_MAX));
  REQUIRE_FALSE(bus.readData128(RESERVED, &quad));
  REQUIRE_FALSE(bus.writeData128(RESERVED, {UINT64_MAX, UINT64_MAX}));
  REQUIRE_FALSE(bus.readDMAC128(RESERVED, &quad));
  REQUIRE_FALSE(bus.writeDMAC128(RESERVED, {UINT64_MAX, UINT64_MAX}));
  REQUIRE(byte == 0x12);
  REQUIRE(half == 0x1234);
  REQUIRE(word == 0x12345678);
  REQUIRE(wide == UINT64_C(0x123456789abcdef0));
  REQUIRE(quad.low == wide);
  REQUIRE(quad.high == wide);

  for (auto address : {RESERVED, UNCACHED})
  {
    REQUIRE_THROWS_AS(bus.read32(address), std::out_of_range);
    REQUIRE_THROWS_AS(bus.read64(address), std::out_of_range);
    REQUIRE_THROWS_AS(bus.readQuadword(address), std::out_of_range);
    REQUIRE_THROWS_AS(bus.writeQuadword(address, {{0xffffffff, 0, 0, 0}}),
      std::out_of_range);
    // The width guard must reject before the host's sequential word fallback.
    REQUIRE_THROWS_WITH(bus.write64(address, UINT64_MAX),
      "EE bus address supports full 32-bit accesses only.");
  }
  REQUIRE_THROWS_AS(bus.writeData32(RESERVED + 1, 0), std::invalid_argument);
  REQUIRE_THROWS_AS(bus.readData32(RESERVED + 1, &word), std::invalid_argument);
  REQUIRE_FALSE(bus.writeData32(UNCACHED, 0));
  REQUIRE_FALSE(bus.readData32(UNCACHED, &word));
  const bool unchanged = system.saveState() == before;
  REQUIRE(unchanged);
}

TEST_CASE("EE reserved startup admission does not map neighboring words or IOP views",
  "[ee][bus][reserved]")
{
  NekoSystem system;
  seed(system);
  const auto before = system.saveState();
  for (std::uint32_t address : {0x1000f4fcu, 0x1000f504u, 0x1000f510u,
         0x1000f520u, 0x1000f590u, 0x1000f5fcu, 0x1000f600u})
  {
    CAPTURE(address);
    std::uint32_t word = 0x12345678;
    REQUIRE_FALSE(system.eeBus().readData32(address, &word));
    REQUIRE_FALSE(system.eeBus().writeData32(address, 0xffffffff));
    REQUIRE_THROWS_AS(system.eeBus().read32(address), std::out_of_range);
    REQUIRE_THROWS_AS(system.eeBus().write32(address, 0xffffffff), std::out_of_range);
    REQUIRE(word == 0x12345678);
  }
  for (auto address : {RESERVED, UNCACHED})
  {
    REQUIRE(system.iopBus().read32(address).status == IOPBusStatus::Unmapped);
    REQUIRE(system.iopBus().write32(address, 0xffffffff).status ==
      IOPBusStatus::Unmapped);
    REQUIRE(system.iopBus().writeMasked32(address, 0xffffffff, 0xf).status ==
      IOPBusStatus::Unmapped);
  }
  const bool unchanged = system.saveState() == before;
  REQUIRE(unchanged);
}

TEST_CASE("EE reserved startup guest word stores retire with existing memory observations",
  "[ee][bus][reserved][guest]")
{
  for (const auto value : {0u, 0xffffffffu, 0xaaaaaaaau, 0x55555555u})
  {
    CAPTURE(value);
    NekoSystem system;
    seed(system);
    const auto sifHash = system.sifStateHash();
    const auto result = execute(system, 0x2b, value);
    REQUIRE(result.instructions != 0);
    REQUIRE_FALSE(result.cycleLimitReached);
    REQUIRE(result.masterCycles != 0);
    REQUIRE(result.pendingException == EEException::None);
    REQUIRE(result.state == EEExecutionState::Running);
    REQUIRE(system.iopCore().executionState() == IOPExecutionState::Running);
    REQUIRE(system.eeCore().generalRegister(2).low == value);
    REQUIRE(system.eeCore().generalRegister(2).high == 0xfeedface);
    REQUIRE(system.sifStateHash() == sifHash);
    REQUIRE(system.dmacController().control() == 1);
    REQUIRE(system.dmacController().interleaveSize() == 0x00030002);
    REQUIRE(system.dmacController().status() ==
      (DMACStatus::CHANNEL_2 | DMACStatus::CHANNEL_2_MASK));
    REQUIRE(system.gifDMAC().memoryAddress() == 0x2000);
    REQUIRE(system.gifDMAC().quadwordCount() == 2);
    REQUIRE(system.eeBus().read32(0x2000) == 0xabcdef01);
    REQUIRE(system.interruptController().status() == 2);
    REQUIRE(system.iopInterruptController().status() == 2);
    requireWordStoreObservation(system, value, true);
  }
}

TEST_CASE("EE reserved startup guest merge stores admit only full-word lanes",
  "[ee][bus][reserved][guest]")
{
  for (std::uint8_t opcode : {0x2a, 0x2e})
  {
    for (std::uint16_t lane = 0; lane < 4; ++lane)
    {
      CAPTURE(opcode);
      CAPTURE(lane);
      NekoSystem system;
      const auto sifHash = system.sifStateHash();
      const bool full = lane == (opcode == 0x2a ? 3 : 0);
      const auto result = execute(system, opcode, 0x89abcdef, lane);
      REQUIRE(result.pendingException ==
        (full ? EEException::None : EEException::DataBusErrorStore));
      REQUIRE_FALSE(result.cycleLimitReached);
      if (full)
      {
        REQUIRE(result.instructions != 0);
      }
      else
      {
        REQUIRE(result.exceptionAddress == UNCACHED + lane);
        REQUIRE(system.eeCore().cop0Register(EECOP0Register::EPC) == 0xa0000000);
      }
      REQUIRE(system.sifStateHash() == sifHash);
      REQUIRE(system.interruptController().status() == 0);
      REQUIRE(system.iopInterruptController().status() == 0);
      requireWordStoreObservation(system, 0x89abcdef, full);
    }
  }
}

TEST_CASE("EE reserved startup guest reads and non-word stores retain precise bus errors",
  "[ee][bus][reserved][guest]")
{
  for (std::uint8_t opcode : {0x20, 0x21, 0x23, 0x37, 0x1e, 0x22, 0x26,
         0x28, 0x29, 0x3f, 0x1f, 0x2c, 0x2d})
  {
    CAPTURE(opcode);
    NekoSystem system;
    const bool store = opcode == 0x28 || opcode == 0x29 || opcode == 0x3f ||
      opcode == 0x1f || opcode == 0x2c || opcode == 0x2d;
    const auto result = execute(system, opcode, 0x89abcdef);
    REQUIRE(result.pendingException ==
      (store ? EEException::DataBusErrorStore : EEException::DataBusErrorLoad));
    REQUIRE_FALSE(result.cycleLimitReached);
    REQUIRE(result.exceptionAddress == UNCACHED);
    REQUIRE(system.eeCore().cop0Register(EECOP0Register::EPC) == 0xa0000000);
    REQUIRE(system.eeCore().generalRegister(2).low == 0x89abcdef);
    REQUIRE(system.eeCore().generalRegister(2).high == 0xfeedface);
  }
}

TEST_CASE("EE reserved startup word cannot become an instruction source",
  "[ee][bus][reserved][guest]")
{
  NekoSystem system;
  system.eeCore().startExecution(UNCACHED);
  const auto result = system.stepEEInstruction(16);
  REQUIRE(result.pendingException == EEException::InstructionBusError);
  REQUIRE(result.exceptionAddress == UNCACHED);
  REQUIRE(result.instructions == 0);
  REQUIRE_FALSE(result.cycleLimitReached);
}
