#include "catch.hpp"
#include "iop/iop_trace_test_utils.hpp"
#include "startup_diagnostics.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace
{
  void startLoops(NekoSystem &system)
  {
    system.eeBus().write32(0, 0x1000ffff); // beq zero, zero, -1
    REQUIRE(system.iopBus().write32(0, 0x1000ffff).status ==
      IOPBusStatus::Completed);
    system.eeCore().startExecution(0xa0000000);
    system.iopCore().startExecution(0xa0000000);
  }
}

TEST_CASE("Bounded traces retain only the ordered prefix without changing execution",
  "[bounded-trace]")
{
  NekoSystem reference;
  startLoops(reference);
  reference.startTrace();
  REQUIRE(reference.runDualCPU(65).masterCycles == 65);
  const std::uint64_t total = reference.trace().size();
  REQUIRE(total > 1);
  REQUIRE_FALSE(reference.traceTruncated());
  REQUIRE(reference.traceEventLimit() ==
    std::numeric_limits<std::uint64_t>::max());

  NekoSystem plain;
  startLoops(plain);
  plain.runDualCPU(65);
  IOPTraceTest::requireMachine(plain, reference);

  for (const std::uint64_t limit :
       {UINT64_C(0), UINT64_C(1), total - 1, total, total + 1})
  {
    CAPTURE(limit);
    NekoSystem bounded;
    startLoops(bounded);
    bounded.startTrace(limit);
    REQUIRE(bounded.traceEventLimit() == limit);
    REQUIRE_FALSE(bounded.traceTruncated());
    REQUIRE(bounded.runDualCPU(65).masterCycles == 65);
    REQUIRE(bounded.traceEnabled());
    REQUIRE(bounded.traceTruncated() == (limit < total));
    const auto captured = std::min(limit, total);
    const IOPTraceTest::Events prefix(
      reference.trace().begin(), reference.trace().begin() + captured);
    IOPTraceTest::requireEvents(bounded.trace(), prefix);
    IOPTraceTest::requireMachine(bounded, reference);
  }
}

TEST_CASE("Bounded trace lifecycle resets loss metadata without changing machine state",
  "[bounded-trace]")
{
  NekoSystem system;
  startLoops(system);
  system.startTrace(1);
  system.runDualCPU(1);
  REQUIRE(system.trace().size() == 1);
  REQUIRE(system.traceTruncated());
  const auto firstEvent = system.trace();
  system.stopTrace();
  system.runDualCPU(8);
  REQUIRE_FALSE(system.traceEnabled());
  REQUIRE(system.traceTruncated());
  REQUIRE(system.traceEventLimit() == 1);
  IOPTraceTest::requireEvents(system.trace(), firstEvent);

  system.clearTrace();
  REQUIRE_FALSE(system.traceEnabled());
  REQUIRE(system.trace().empty());
  REQUIRE_FALSE(system.traceTruncated());
  REQUIRE(system.traceEventLimit() == 1);
  system.startTrace(2);
  system.runDualCPU(1);
  REQUIRE(system.trace().size() == 2);
  REQUIRE(system.traceTruncated());
  system.clearTrace();
  REQUIRE(system.traceEnabled());
  REQUIRE(system.trace().empty());
  REQUIRE_FALSE(system.traceTruncated());
  REQUIRE(system.traceEventLimit() == 2);
  system.runDualCPU(8);
  REQUIRE(system.trace().size() == 2);
  REQUIRE(system.traceTruncated());

  system.startTrace();
  REQUIRE(system.trace().empty());
  REQUIRE_FALSE(system.traceTruncated());
  REQUIRE(system.traceEventLimit() ==
    std::numeric_limits<std::uint64_t>::max());
  system.runDualCPU(8);
  REQUIRE(system.trace().size() > 2);
  REQUIRE_FALSE(system.traceTruncated());
  system.reset();
  REQUIRE_FALSE(system.traceEnabled());
  REQUIRE(system.trace().empty());
  REQUIRE_FALSE(system.traceTruncated());
  REQUIRE(system.traceEventLimit() == 0);
}

TEST_CASE("Saturated trace prefixes preserve failure stops and recorder streams",
  "[bounded-trace][startup-failure]")
{
  for (const bool iopFailure : {false, true})
  {
    CAPTURE(iopFailure);
    const auto prepare = [&](NekoSystem &system)
    {
      startLoops(system);
      if (iopFailure)
      {
        REQUIRE(system.iopBus().write32(0, 0).status == IOPBusStatus::Completed);
        REQUIRE(system.iopBus().write32(0x80, 0x8c020001).status ==
          IOPBusStatus::Completed);
      }
      else
      {
        system.eeBus().write32(0, 0);
        system.eeBus().write32(0x80, 0x8c020001);
      }
    };
    NekoSystem reference;
    prepare(reference);
    reference.startTrace(4096, {}, NekoTraceStopPolicy::FirstFailure);
    const auto result = reference.runDualCPU(512);
    REQUIRE_FALSE(result.cycleLimitReached);
    REQUIRE_FALSE(reference.traceTruncated());
    const auto failure = firstStartupFailure(reference.trace());
    REQUIRE(failure.found);
    REQUIRE(failure.event.subsystem ==
      (iopFailure ? NekoTraceSubsystem::IOP : NekoTraceSubsystem::EE));
    const auto state = reference.saveState();

    for (const std::uint64_t limit : {UINT64_C(0), UINT64_C(1)})
    {
      CAPTURE(limit);
      NekoSystem bounded;
      prepare(bounded);
      bounded.startTrace(limit, {}, NekoTraceStopPolicy::FirstFailure);
      REQUIRE(bounded.runDualCPU(512).masterCycles == result.masterCycles);
      REQUIRE(bounded.traceTruncated());
      const IOPTraceTest::Events prefix(
        reference.trace().begin(), reference.trace().begin() + limit);
      IOPTraceTest::requireEvents(bounded.trace(), prefix);
      REQUIRE(bounded.saveState() == state);

      NekoSystem recorded;
      prepare(recorded);
      auto capture = std::make_shared<StartupFailureCapture>();
      recorded.startTrace(limit, capture, NekoTraceStopPolicy::FirstFailure);
      REQUIRE(recorded.runDualCPU(512).masterCycles == result.masterCycles);
      REQUIRE(recorded.traceTruncated());
      IOPTraceTest::requireEvents(recorded.trace(), prefix);
      IOPTraceTest::requireEvents(capture->events(), reference.trace());
      REQUIRE(capture->observedEvents() == reference.trace().size());
      REQUIRE(recorded.saveState() == state);
    }
  }
}

TEST_CASE("Clearing saturated traces restores hash-bearing component suffixes",
  "[bounded-trace]")
{
  NekoSystem reference;
  NekoSystem bounded;
  for (auto *system : {&reference, &bounded})
  {
    startLoops(*system);
    system->gsDisplay().configureTiming({4, 6, {1, 2, 1}});
  }
  reference.startTrace();
  bounded.startTrace(16);
  reference.runDualCPU(63);
  bounded.runDualCPU(63);
  REQUIRE(bounded.traceTruncated());
  const IOPTraceTest::Events prefix(
    reference.trace().begin(), reference.trace().begin() + 16);
  IOPTraceTest::requireEvents(bounded.trace(), prefix);

  reference.clearTrace();
  bounded.clearTrace();
  REQUIRE_FALSE(bounded.traceTruncated());
  reference.runDualCPU(1);
  bounded.runDualCPU(1);
  REQUIRE_FALSE(bounded.traceTruncated());
  IOPTraceTest::requireEvents(bounded.trace(), reference.trace());
  REQUIRE(std::any_of(bounded.trace().begin(), bounded.trace().end(),
    [](const NekoTraceEvent &event)
    {
      return event.type == NekoTraceEventType::StateSnapshot;
    }));
  REQUIRE(std::any_of(bounded.trace().begin(), bounded.trace().end(),
    [](const NekoTraceEvent &event)
    {
      return event.type == NekoTraceEventType::PresentationBoundary;
    }));
  IOPTraceTest::requireMachine(bounded, reference);
}

TEST_CASE("Loading canonical state preserves destination bounded trace controls and history",
  "[bounded-trace]")
{
  NekoSystem source;
  startLoops(source);
  source.startTrace(0);
  source.runDualCPU(9);
  REQUIRE(source.traceTruncated());
  const auto state = source.saveState();

  NekoSystem destination;
  startLoops(destination);
  destination.startTrace(1);
  destination.runDualCPU(1);
  const auto history = destination.trace();
  REQUIRE(destination.traceTruncated());
  destination.loadState(state);
  REQUIRE(destination.traceEnabled());
  REQUIRE(destination.traceEventLimit() == 1);
  REQUIRE(destination.traceTruncated());
  IOPTraceTest::requireEvents(destination.trace(), history);
  IOPTraceTest::requireMachine(destination, source);
  source.runDualCPU(24);
  destination.runDualCPU(24);
  REQUIRE(destination.trace().size() == 1);
  IOPTraceTest::requireMachine(destination, source);

  NekoSystem disabled;
  disabled.loadState(state);
  REQUIRE_FALSE(disabled.traceEnabled());
  REQUIRE(disabled.traceEventLimit() == 0);
  REQUIRE_FALSE(disabled.traceTruncated());
  REQUIRE(disabled.trace().empty());
}

TEST_CASE("Startup trace export is observational and independent of output formatting",
  "[bounded-trace]")
{
  class GroupedNumbers : public std::numpunct<char>
  {
    char do_thousands_sep() const override { return '_'; }
    std::string do_grouping() const override { return "\3"; }
  };
  NekoSystem system;
  startLoops(system);
  system.startTrace(2);
  system.runDualCPU(1);
  const auto state = system.saveState();
  const auto history = system.trace();

  std::ostringstream plain;
  writeStartupTraceJsonLines(plain, system);
  REQUIRE(plain.str().find(
    "{\"type\":\"startup_trace_summary\",\"event_limit\":2,"
    "\"captured_events\":2,\"truncated\":true}\n") == 0);
  std::ostringstream decorated;
  const auto locale = std::locale(std::locale::classic(), new GroupedNumbers);
  decorated.imbue(locale);
  decorated << std::hex << std::showbase << std::setfill('*');
  decorated.width(12);
  const auto flags = decorated.flags();
  writeStartupTraceJsonLines(decorated, system);
  REQUIRE(decorated.str() == plain.str());
  REQUIRE(decorated.getloc() == locale);
  REQUIRE(decorated.flags() == flags);
  REQUIRE(decorated.width() == 12);
  REQUIRE(decorated.fill() == '*');
  const bool unchanged = system.saveState() == state;
  REQUIRE(unchanged);
  REQUIRE(system.traceEventLimit() == 2);
  REQUIRE(system.traceTruncated());
  REQUIRE(system.traceEnabled());
  IOPTraceTest::requireEvents(system.trace(), history);
}
