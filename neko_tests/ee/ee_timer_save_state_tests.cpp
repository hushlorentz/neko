#include <cstdint>
#include <stdexcept>
#include <string>

#include "catch.hpp"
#include "neko_system.hpp"
#include "save_state_mutation.hpp"

namespace
{
  constexpr std::uint32_t timerBase(unsigned index)
  {
    return 0x10000000u + index * 0x800u;
  }
}

TEST_CASE("EE timer registers participate in canonical EE identity",
  "[ee][ee-timer][save-state]")
{
  NekoSystem system;
  const auto initial = system.eeStateHash();
  const auto iopInitial = system.iopStateHash();
  for (unsigned index = 0; index < 4; ++index)
  {
    for (std::uint32_t offset : {0u, 0x10u, 0x20u, 0x30u})
    {
      if (offset == 0x30 && index >= 2)
      {
        continue;
      }
      for (unsigned bit = 0; bit < (offset == 0x10 ? 10u : 16u); ++bit)
      {
        CAPTURE(index);
        CAPTURE(offset);
        CAPTURE(bit);
        REQUIRE(system.eeBus().writeData32(timerBase(index) + offset, 1u << bit));
        REQUIRE(system.eeStateHash() != initial);
        REQUIRE(system.eeBus().writeData32(timerBase(index) + offset, 0));
        REQUIRE(system.eeStateHash() == initial);
      }
    }
  }
  REQUIRE(system.iopStateHash() == iopInitial);
}

TEST_CASE("EE timers restore nonzero clock and blank phases across trace modes",
  "[ee][ee-timer][save-state]")
{
  NekoSystem system;
  system.gsDisplay().configureTiming({11, 19, {3, 7, 1}});
  const std::uint32_t modes[] = {0x3c1, 0x382, 0x383, 0x3bc};
  for (unsigned index = 0; index < 4; ++index)
  {
    REQUIRE(system.eeBus().writeData32(timerBase(index), 0xffff - index));
    REQUIRE(system.eeBus().writeData32(timerBase(index) + 0x20, 6 + index));
    REQUIRE(system.eeBus().writeData32(timerBase(index) + 0x10, modes[index]));
  }
  system.runMasterCycles(33);
  REQUIRE(system.gsDisplay().inHorizontalBlank());
  REQUIRE(system.gsDisplay().inVerticalBlank());
  REQUIRE(system.iopBus().write32(IOPBus::SIF_DOORBELL, 2).status ==
    IOPBusStatus::Completed);
  system.interruptController().acknowledge(UINT32_MAX);
  const auto saved = system.saveState();
  const auto savedHash = system.eeStateHash();
  NekoSystem restored;
  restored.loadState(saved);
  REQUIRE(restored.eeStateHash() == savedHash);
  REQUIRE(restored.saveState() == saved);
  REQUIRE(restored.masterClockScheduler().currentCycle() == 33);
  system.startTrace(10000);
  restored.startTrace(10000);
  system.runMasterCycles(571);
  restored.runMasterCycles(571);
  REQUIRE_FALSE(system.traceTruncated());
  REQUIRE_FALSE(restored.traceTruncated());
  REQUIRE(system.traceHash() == restored.traceHash());
  REQUIRE(system.eeStateHash() == restored.eeStateHash());
  REQUIRE(system.iopStateHash() == restored.iopStateHash());
  const auto ending = system.saveState();
  REQUIRE(restored.saveState() == ending);
  restored.stopTrace();
  restored.loadState(saved);
  REQUIRE_FALSE(restored.traceEnabled());
  restored.runMasterCycles(571);
  REQUIRE(restored.saveState() == ending);
  restored.reset();
  NekoSystem fresh;
  REQUIRE(restored.saveState() == fresh.saveState());
  REQUIRE(restored.eeStateHash() == fresh.eeStateHash());
}

TEST_CASE("EE timer malformed continuation rejects atomically",
  "[ee][ee-timer][save-state]")
{
  NekoSystem system;
  REQUIRE(system.eeBus().writeData32(timerBase(0) + 0x10, 0x181));
  system.runMasterCycles(37);
  const auto original = system.saveState();
  const auto hash = system.eeStateHash();
  const char *fields[] = {
    "system.eeTimers.timers[0].mode",
    "system.eeTimers.timers[3].mode",
    "system.eeTimers.previousHBlank",
    "system.eeTimers.previousVBlank"
  };
  for (const char *field : fields)
  {
    CAPTURE(field);
    SaveStateMutation invalid(original);
    const bool mode = std::string(field).find(".mode") != std::string::npos;
    invalid.writeScalar(field, mode ? 0x1000 : 2);
    invalid.refreshChecksum();
    REQUIRE_THROWS_AS(system.loadState(invalid), std::invalid_argument);
    REQUIRE(system.eeStateHash() == hash);
    REQUIRE(system.saveState() == original);
  }
}

TEST_CASE("EE timer identity includes only relevant active bus clock phase",
  "[ee][ee-timer][save-state]")
{
  EETimers timers;
  REQUIRE(timers.stateHash(1) == timers.stateHash(2));
  timers.writeRegister(timerBase(0) + 0x10, 0x81);
  REQUIRE(timers.stateHash(1) != timers.stateHash(2));
  REQUIRE(timers.stateHash(1) == timers.stateHash(33));
  timers.writeRegister(timerBase(1) + 0x10, 0x82);
  REQUIRE(timers.stateHash(1) != timers.stateHash(33));
  REQUIRE(timers.stateHash(1) == timers.stateHash(513));
  timers.writeRegister(timerBase(0) + 0x10, 0);
  timers.writeRegister(timerBase(1) + 0x10, 0x83);
  REQUIRE(timers.stateHash(1) == timers.stateHash(2));
}
