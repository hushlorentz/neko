#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "clock_scheduler.hpp"

namespace
{
  class RecordingComponent : public ClockedComponent
  {
    public:
      RecordingComponent(
        std::uint8_t identifier,
        std::vector<std::uint8_t> *events) :
        identifier(identifier),
        events(events)
      {
      }

      bool clockActive() const override
      {
        return active;
      }

      void clock() override
      {
        events->push_back(identifier);
        ++clockCount;
      }

      std::uint8_t identifier;
      std::vector<std::uint8_t> *events;
      bool active = true;
      std::uint64_t clockCount = 0;
  };
}

TEST_CASE("Master Clock Scheduler Tests")
{
  SECTION("Periods and phases use integer master cycles")
  {
    std::vector<std::uint8_t> events;
    RecordingComponent everyCycle(1, &events);
    RecordingComponent evenCycles(2, &events);
    RecordingComponent oddCycles(3, &events);
    MasterClockScheduler scheduler;
    scheduler.registerComponent(everyCycle, 1);
    scheduler.registerComponent(evenCycles, 2);
    scheduler.registerComponent(oddCycles, 2, 1);

    REQUIRE(scheduler.run(4) == 4);

    REQUIRE(
      events ==
      std::vector<std::uint8_t>{1, 2, 1, 3, 1, 2, 1, 3});
    REQUIRE(everyCycle.clockCount == 4);
    REQUIRE(evenCycles.clockCount == 2);
    REQUIRE(oddCycles.clockCount == 2);
    REQUIRE(scheduler.currentCycle() == 4);
  }

  SECTION("Same-cycle components run in registration order")
  {
    std::vector<std::uint8_t> events;
    RecordingComponent first(1, &events);
    RecordingComponent second(2, &events);
    MasterClockScheduler scheduler;
    scheduler.registerComponent(first, 1);
    scheduler.registerComponent(second, 1);

    scheduler.clock();

    REQUIRE(events == std::vector<std::uint8_t>{1, 2});
  }

  SECTION("Inactive components retain the global clock phase")
  {
    std::vector<std::uint8_t> events;
    RecordingComponent component(1, &events);
    MasterClockScheduler scheduler;
    scheduler.registerComponent(component, 2);
    component.active = false;
    scheduler.run(2);
    component.active = true;

    scheduler.clock();

    REQUIRE(component.clockCount == 1);
    REQUIRE(scheduler.currentCycle() == 3);
  }

  SECTION("Invalid registrations are rejected")
  {
    std::vector<std::uint8_t> events;
    RecordingComponent component(1, &events);
    MasterClockScheduler scheduler;

    REQUIRE_THROWS_WITH(
      scheduler.registerComponent(component, 0),
      "A scheduled clock period must be nonzero.");
    REQUIRE_THROWS_WITH(
      scheduler.registerComponent(component, 2, 2),
      "A scheduled clock phase must be less than its period.");
    scheduler.registerComponent(component, 1);
    REQUIRE_THROWS_WITH(
      scheduler.registerComponent(component, 1),
      "A component cannot be registered more than once.");
  }
}
