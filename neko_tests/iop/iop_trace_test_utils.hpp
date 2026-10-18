#ifndef IOP_TRACE_TEST_UTILS_HPP
#define IOP_TRACE_TEST_UTILS_HPP

#include <vector>

#include "catch.hpp"
#include "neko_system.hpp"

namespace IOPTraceTest
{
  using Events = std::vector<NekoTraceEvent>;

  inline void requireEvents(const Events &actual, const Events &expected)
  {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
      CAPTURE(index);
      REQUIRE(actual[index].masterCycle == expected[index].masterCycle);
      REQUIRE(actual[index].subsystem == expected[index].subsystem);
      REQUIRE(actual[index].type == expected[index].type);
      REQUIRE(actual[index].value0 == expected[index].value0);
      REQUIRE(actual[index].value1 == expected[index].value1);
      REQUIRE(actual[index].value2 == expected[index].value2);
      REQUIRE(actual[index].value3 == expected[index].value3);
    }
    REQUIRE(nekoTraceHash(actual) == nekoTraceHash(expected));
  }

  inline Events subsystemEvents(
    const NekoSystem &system, NekoTraceSubsystem subsystem)
  {
    Events events;
    for (const auto &event : system.trace())
    {
      if (event.subsystem == subsystem)
      {
        events.push_back(event);
      }
    }
    return events;
  }

  inline void requireMachine(const NekoSystem &actual, const NekoSystem &expected)
  {
    REQUIRE(actual.masterClockScheduler().currentCycle() ==
      expected.masterClockScheduler().currentCycle());
    REQUIRE(actual.iopCore().elapsedCycles() == expected.iopCore().elapsedCycles());
    REQUIRE(actual.iopCore().retiredInstructions() ==
      expected.iopCore().retiredInstructions());
    REQUIRE(actual.iopCore().programCounter() == expected.iopCore().programCounter());
    REQUIRE(actual.iopCore().executionState() == expected.iopCore().executionState());
    REQUIRE(actual.iopCore().stopReason() == expected.iopCore().stopReason());
    REQUIRE(actual.iopStateHash() == expected.iopStateHash());
    REQUIRE(actual.eeStateHash() == expected.eeStateHash());
    const bool identicalState = actual.saveState() == expected.saveState();
    REQUIRE(identicalState);
  }
}

#endif
