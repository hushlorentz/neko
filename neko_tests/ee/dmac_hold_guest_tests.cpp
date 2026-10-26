#include <array>
#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "iop/iop_trace_test_utils.hpp"

namespace
{
  constexpr std::uint32_t RESULTS = 0x3000;
  constexpr std::uint32_t HELD = 0x81234567;
  constexpr std::uint32_t RELEASED = HELD & ~DMACHold::CPND;
  constexpr std::uint64_t BUDGET = 256;
  enum class Finish { Resume, Abort };
  enum class Source { Normal, Chain };
  struct Endpoint
  {
    unsigned id;
    std::uint32_t chcr;
  };
  constexpr std::array<Endpoint, 6> ENDPOINTS = {{
    {1, 0x10009000}, {2, 0x1000a000}, {5, 0x1000c000},
    {6, 0x1000c400}, {8, 0x1000d000}, {9, 0x1000d400}
  }};

  std::uint32_t pendingStatus(const Endpoint &endpoint)
  {
    const auto other = endpoint.id == 9 ? 8 : 9;
    return (1u << other) | (1u << (other + 16));
  }

  void prepare(NekoSystem &system, const Endpoint &endpoint, Finish finish,
    Source source = Source::Normal, unsigned grants = 1)
  {
    const auto direction = endpoint.id == 5 ? 0u : 1u;
    const auto mode = source == Source::Chain ? 4u : 0u;
    // Original uncached guest: arm while held, allow one (or two) atomic
    // grants, re-hold, then resume or clear only STR in the observed CHCR.
    std::vector<std::uint32_t> guest = {
      0x3c01b000, 0x3421f500, // r1: hold register neighborhood
      0x3c02a000, 0x34423000, // r2: result markers
      0x3c03b000, 0x34630000 | (endpoint.chcr & 0xffff),
      0x3c048123, 0x34844567, // r4: retained held word
      0x3c058122, 0x34a54567, // r5: same reserved bits, CPND clear
      0x34060100 | direction | mode,
      0x34074000,
      0x34080000 | (source == Source::Chain ? 0u : 3u),
      0x3c0cb000, 0x358ce000, // r12: D_CTRL / D_STAT
      0x340d0001, 0xad8d0000, // DMAE
      0xac240090,             // hold
      source == Source::Chain ? 0xac670030u : 0xac670010u,
      0xac680020, 0xac660000, // QWC, STR
      0x8c290020, 0, 0xac490004, // raw D_ENABLER readback
      0x340a0001, 0xac4a0000, // checkpoint 1
      0, 0, 0, 0,
      0xac250090              // release
    };
    guest.insert(guest.end(), grants - 1, 0);
    guest.insert(guest.end(), {
      0xac240090,             // re-hold before the next grant
      0x8c690020, 0, 0xac490008, // interrupted QWC
      0x8c690010, 0, 0xac49000c, // interrupted MADR
      0x340a0002, 0xac4a0000 // checkpoint 2
    });
    guest.insert(guest.end(), 32, 0);
    if (finish == Finish::Abort)
    {
      guest.insert(guest.end(), {
        0x3c0dffff, 0x35adfeff, // mask retaining all fields except STR
        0x8c6b0000, 0,
        0x016d5824, 0xac6b0000 // CHCR &= ~STR while held
      });
    }
    guest.push_back(0xac250090);
    guest.insert(guest.end(), 8, 0);
    guest.insert(guest.end(), {
      0x8c690000, 0, 0xac490010, // final CHCR
      0x8d890010, 0, 0xac490014, // final D_STAT
      0x340a0003, 0xac4a0000, // checkpoint 3
      0x1000ffff, 0          // bounded host run ends in this guest loop
    });
    for (std::size_t index = 0; index < guest.size(); ++index)
    {
      system.eeBus().write32(0x1000 + static_cast<std::uint32_t>(4 * index),
        guest[index]);
    }
    if (source == Source::Chain)
    {
      REQUIRE(system.eeBus().writeQuadword(0x4000, {{0x70000003, 0, 0, 0}}));
    }
    if (endpoint.id == 5)
    {
      for (unsigned index = 0; index < 3; ++index)
      {
        REQUIRE(system.sif().pushSIF0Prologue({{0, 0, 0, 0}}) ==
          SIFTransferOutcome::Completed);
      }
    }
    const auto other = endpoint.id == 9 ? DMACStatus::CHANNEL_8 :
      DMACStatus::CHANNEL_9;
    system.dmacController().signalChannelCompletion(other);
    system.dmacController().writeStatus(other << 16);
    system.eeCore().setCOP0Register(EECOP0Register::Status, 0);
    system.eeCore().startExecution(0xa0001000);
  }

  void reach(NekoSystem &system, std::uint32_t phase)
  {
    for (std::uint64_t elapsed = 0;
         elapsed < BUDGET && system.eeBus().read32(RESULTS) != phase; ++elapsed)
    {
      system.clockMasterCycle();
    }
    REQUIRE(system.eeBus().read32(RESULTS) == phase);
    REQUIRE(system.eeCore().pendingException() == EEException::None);
  }

  void requireHeld(NekoSystem &system, const Endpoint &endpoint,
    std::uint32_t count, std::uint32_t address)
  {
    REQUIRE(system.dmacController().holdControl() == HELD);
    REQUIRE((system.eeBus().read32(endpoint.chcr) & DMACChannelControl::START) != 0);
    REQUIRE(system.eeBus().read32(endpoint.chcr + 0x20) == count);
    REQUIRE(system.eeBus().read32(endpoint.chcr + 0x10) == address);
    REQUIRE(system.dmacController().status() == pendingStatus(endpoint));
    REQUIRE(system.dmacController().interruptPending());
  }

  void requireFinished(NekoSystem &system, const Endpoint &endpoint,
    Finish finish)
  {
    const auto aborted = finish == Finish::Abort;
    REQUIRE(system.eeBus().read32(RESULTS) == 3);
    REQUIRE(system.dmacController().holdControl() == RELEASED);
    REQUIRE((system.eeBus().read32(RESULTS + 0x10) &
      DMACChannelControl::START) == 0);
    REQUIRE(system.eeBus().read32(endpoint.chcr + 0x20) == (aborted ? 2 : 0));
    REQUIRE(system.eeBus().read32(endpoint.chcr + 0x10) ==
      (aborted ? 0x4010 : 0x4030));
    REQUIRE(system.eeBus().read32(RESULTS + 0x14) ==
      (pendingStatus(endpoint) | (aborted ? 0 : 1u << endpoint.id)));
    if (endpoint.id == 5 || endpoint.id == 6)
    {
      const auto direction = endpoint.id == 5 ? SIFDirection::SIF0 :
        SIFDirection::SIF1;
      const auto expected = endpoint.id == 5 ? (aborted ? 8 : 0) :
        (aborted ? 4 : 12);
      REQUIRE(system.sif().wordCount(direction) == expected);
    }
  }

  void finishRun(NekoSystem &system)
  {
    const auto cycle = system.masterClockScheduler().currentCycle();
    REQUIRE(cycle < BUDGET);
    system.runMasterCycles(BUDGET - cycle);
    REQUIRE(system.eeBus().read32(RESULTS) == 3);
    REQUIRE(system.eeCore().pendingException() == EEException::None);
  }

  void requireHoldObservations(const NekoSystem &system)
  {
    constexpr std::array<std::uint32_t, 4> values = {
      HELD, RELEASED, HELD, RELEASED
    };
    std::size_t writes = 0;
    std::size_t reads = 0;
    for (const auto &event : system.trace())
    {
      if (event.subsystem != NekoTraceSubsystem::EE ||
          event.type != NekoTraceEventType::MemoryAccess ||
          (event.value0 != 0xb000f590 && event.value0 != 0xb000f520))
      {
        continue;
      }
      const bool store = event.value0 == 0xb000f590;
      if (store)
      {
        REQUIRE(writes < values.size());
        REQUIRE(event.value1 == values[writes++]);
      }
      else
      {
        ++reads;
        REQUIRE(event.value1 == HELD);
      }
      REQUIRE(event.value2 == 0);
      REQUIRE(event.value3 == nekoPackEEMemoryTraceMetadata({
        store ? 0x1000f590u : 0x1000f520u, 4,
        store ? NekoEETraceMemory::AccessKind::DataStore :
          NekoEETraceMemory::AccessKind::DataLoad,
        NekoEETraceMemory::TranslationOutcome::Translated,
        NekoEETraceMemory::CacheRoute::Uncached, 2, false, true,
        NekoEETraceMemory::CacheAccess::Bypassed,
        NekoEETraceMemory::TransferOutcome::Completed,
        NekoEETraceMemory::FailurePhase::None
      }));
    }
    REQUIRE(writes == values.size());
    REQUIRE(reads == 1);
    REQUIRE_FALSE(system.traceTruncated());
  }
}

TEST_CASE("EE guest hold stores bracket one grant on every implemented DMA channel",
  "[ee][dma][hold][guest]")
{
  for (const auto endpoint : ENDPOINTS)
  {
    for (const auto finish : {Finish::Resume, Finish::Abort})
    {
      CAPTURE(endpoint.id);
      CAPTURE(finish == Finish::Abort);
      NekoSystem system;
      prepare(system, endpoint, finish);
      system.startTrace();
      reach(system, 1);
      requireHeld(system, endpoint, 3, 0x4000);
      REQUIRE(system.eeBus().read32(RESULTS + 4) == HELD);
      reach(system, 2);
      requireHeld(system, endpoint, 2, 0x4010);
      REQUIRE(system.eeBus().read32(RESULTS + 8) == 2);
      REQUIRE(system.eeBus().read32(RESULTS + 12) == 0x4010);
      reach(system, 3);
      requireFinished(system, endpoint, finish);
      finishRun(system);
      requireFinished(system, endpoint, finish);
      requireHoldObservations(system);
    }
  }
}

TEST_CASE("EE guest held tags and payloads restore resume abort and trace modes",
  "[ee][dma][hold][guest][save-state][trace]")
{
  const auto endpoint = ENDPOINTS[3];
  for (const auto source : {Source::Normal, Source::Chain})
  {
    for (const auto finish : {Finish::Resume, Finish::Abort})
    {
      CAPTURE(source == Source::Chain);
      CAPTURE(finish == Finish::Abort);
      NekoSystem original;
      prepare(original, endpoint, finish, source);
      const auto data = source == Source::Chain ? 0x4010u : 0x4000u;
      for (std::uint32_t index = 0; index < 12; ++index)
      {
        original.eeBus().write32(data + 4 * index, index + 1);
      }
      reach(original, 2);
      requireHeld(original, endpoint, source == Source::Chain ? 3 : 2, 0x4010);
      const auto checkpoint = original.saveState();
      NekoSystem restored;
      NekoSystem untraced;
      restored.loadState(checkpoint);
      untraced.loadState(checkpoint);
      original.startTrace();
      restored.startTrace();
      for (auto *system : {&original, &restored, &untraced})
      {
        finishRun(*system);
        REQUIRE_FALSE(system->sif1DMAC().active());
        const auto aborted = finish == Finish::Abort;
        REQUIRE(system->sif1DMAC().quadwordCount() ==
          (aborted ? (source == Source::Chain ? 3 : 2) : 0));
        REQUIRE(system->sif1DMAC().memoryAddress() ==
          (aborted ? 0x4010 : (source == Source::Chain ? 0x4040 : 0x4030)));
        const auto words = aborted ? (source == Source::Chain ? 0u : 4u) : 12u;
        REQUIRE(system->sif().wordCount(SIFDirection::SIF1) == words);
        for (std::size_t index = 0; index < words; ++index)
        {
          REQUIRE(system->sif().wordAt(SIFDirection::SIF1, index) == index + 1);
        }
        REQUIRE(system->dmacController().status() ==
          (pendingStatus(endpoint) | (aborted ? 0 : DMACStatus::CHANNEL_6)));
      }
      REQUIRE_FALSE(original.trace().empty());
      REQUIRE(untraced.trace().empty());
      IOPTraceTest::requireEvents(restored.trace(), original.trace());
      IOPTraceTest::requireMachine(restored, original);
      IOPTraceTest::requireMachine(untraced, original);
    }
  }
}

TEST_CASE("EE guest hold preserves partial SIF packets while the IOP drains",
  "[ee][dma][hold][guest][sif1][save-state][trace]")
{
  const auto endpoint = ENDPOINTS[3];
  NekoSystem original;
  prepare(original, endpoint, Finish::Resume, Source::Normal, 2);
  REQUIRE(original.eeBus().writeQuadword(0x4000, {{0x80002000, 8, 0, 0}}));
  REQUIRE(original.eeBus().writeQuadword(0x4010, {{1, 2, 3, 4}}));
  REQUIRE(original.eeBus().writeQuadword(0x4020, {{5, 6, 7, 8}}));
  REQUIRE(original.iopSIF1DMAC().writeChannelControl(0x01000400) ==
    IOPDMAWriteOutcome::Completed);
  original.iopDMAController().writePriority(IOPDMABank::Secondary, 0x8000);
  reach(original, 2);
  requireHeld(original, endpoint, 1, 0x4020);
  REQUIRE(original.iopSIF1DMAC().active());
  REQUIRE(original.iopSIF1DMAC().remainingWords() == 8);
  for (unsigned elapsed = 0;
       elapsed < 16 && original.iopSIF1DMAC().remainingWords() == 8; ++elapsed)
  {
    original.clockMasterCycle();
  }
  requireHeld(original, endpoint, 1, 0x4020);
  const auto remaining = original.iopSIF1DMAC().remainingWords();
  REQUIRE(remaining > 4);
  REQUIRE(remaining < 8);
  REQUIRE(original.sif().wordCount(SIFDirection::SIF1) == remaining - 4);
  const auto checkpoint = original.saveState();
  NekoSystem restored;
  NekoSystem untraced;
  restored.loadState(checkpoint);
  untraced.loadState(checkpoint);
  original.startTrace();
  restored.startTrace();
  for (auto *system : {&original, &restored, &untraced})
  {
    finishRun(*system);
    REQUIRE_FALSE(system->sif1DMAC().active());
    REQUIRE_FALSE(system->iopSIF1DMAC().active());
    REQUIRE(system->sif().wordCount(SIFDirection::SIF1) == 0);
    REQUIRE(system->iopDMAController().interruptStatus(IOPDMABank::Secondary) ==
      0x08000000);
    for (std::uint32_t index = 0; index < 8; ++index)
    {
      const auto result = system->iopBus().read32(0x2000 + 4 * index);
      REQUIRE(result.status == IOPBusStatus::Completed);
      REQUIRE(result.value == index + 1);
    }
  }
  IOPTraceTest::requireEvents(restored.trace(), original.trace());
  IOPTraceTest::requireMachine(restored, original);
  IOPTraceTest::requireMachine(untraced, original);
}
