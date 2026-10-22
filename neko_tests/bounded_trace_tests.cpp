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
