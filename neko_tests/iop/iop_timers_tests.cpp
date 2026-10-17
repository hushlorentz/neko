#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "catch.hpp"
#include "iop_timers.hpp"

struct IOPTimersTestAccess
{
  static void seed(IOPTimers &timers, IOPWord mode = 0x7a35)
  {
    timers.blanking = {true, true};
    for (std::size_t index = 0; index < 6; ++index)
    {
      auto &state = timers.counters[index];
      state.count = static_cast<IOPWord>(0x1200 + index);
      state.mode = mode;
      state.target = static_cast<IOPWord>(0x3400 + index);
      state.prescalerRemainder =
        index == 2 ? 7 : (index >= 4 ? 255 : 0);
      state.gateWaiting = index == 0 || index == 1 || index == 3;
      state.oneShotConsumed = true;
      state.pulseReleasePending = true;
    }
  }
};

static_assert(std::is_final<IOPTimers>::value,
  "IOP timer ownership must remain concrete.");
static_assert(std::is_trivially_copyable<IOPTimers>::value,
  "IOP timers must remain fixed-capacity value state.");
static_assert(std::is_same<
    decltype(std::declval<const IOPTimers &>().counter(0)),
    const IOPTimerCounterState &>::value,
  "Counter inspection must not expose mutation.");
static_assert(std::is_same<
    decltype(IOPTimers::capabilities(0)),
    const IOPTimerCapabilities &>::value,
  "Timer capabilities must have one immutable authority.");
static_assert(std::is_same<
    decltype(std::declval<const IOPTimers &>().gateLevels()),
    const IOPTimerGateLevels &>::value,
  "Gate inspection must not expose mutation.");

namespace
{
  void requireCounter(
    const IOPTimerCounterState &actual,
    const IOPTimerCounterState &expected)
  {
    REQUIRE(actual.count == expected.count);
    REQUIRE(actual.mode == expected.mode);
    REQUIRE(actual.target == expected.target);
    REQUIRE(actual.prescalerRemainder == expected.prescalerRemainder);
    REQUIRE(actual.gateWaiting == expected.gateWaiting);
    REQUIRE(actual.oneShotConsumed == expected.oneShotConsumed);
    REQUIRE(actual.pulseReleasePending == expected.pulseReleasePending);
  }

  void requireTimers(const IOPTimers &actual, const IOPTimers &expected)
  {
    REQUIRE(actual.gateLevels().horizontalBlank ==
      expected.gateLevels().horizontalBlank);
    REQUIRE(actual.gateLevels().verticalBlank ==
      expected.gateLevels().verticalBlank);
    for (std::size_t index = 0; index < 6; ++index)
    {
      CAPTURE(index);
      requireCounter(actual.counter(index), expected.counter(index));
    }
  }

  void requireOtherCounters(
    const IOPTimers &actual, const IOPTimers &before, std::size_t changed)
  {
    REQUIRE(actual.gateLevels().horizontalBlank ==
      before.gateLevels().horizontalBlank);
    REQUIRE(actual.gateLevels().verticalBlank ==
      before.gateLevels().verticalBlank);
    for (std::size_t index = 0; index < 6; ++index)
    {
      if (index != changed)
      {
        requireCounter(actual.counter(index), before.counter(index));
      }
    }
  }
}

TEST_CASE("IOP timer owner pins the six hardware capability profiles")
{
  REQUIRE(IOPTimers::COUNTER_COUNT == 6);
  const std::array<std::uint8_t, 6> widths = {{16, 16, 16, 32, 32, 32}};
  const std::array<IOPTimerSource, 6> alternatives = {{
    IOPTimerSource::Pixel, IOPTimerSource::HBlank, IOPTimerSource::System,
    IOPTimerSource::HBlank, IOPTimerSource::System, IOPTimerSource::System
  }};
  const std::array<IOPTimerGate, 6> gates = {{
    IOPTimerGate::HBlank, IOPTimerGate::VBlank, IOPTimerGate::None,
    IOPTimerGate::VBlank, IOPTimerGate::None, IOPTimerGate::None
  }};
  const std::array<IOPTimerPrescalerField, 6> prescalers = {{
    IOPTimerPrescalerField::None, IOPTimerPrescalerField::None,
    IOPTimerPrescalerField::Bit9, IOPTimerPrescalerField::None,
    IOPTimerPrescalerField::Bits13And14, IOPTimerPrescalerField::Bits13And14
  }};
  const std::array<IOPInterruptSource, 6> interrupts = {{
    IOPInterruptSource::Timer0, IOPInterruptSource::Timer1,
    IOPInterruptSource::Timer2, IOPInterruptSource::Timer3,
    IOPInterruptSource::Timer4, IOPInterruptSource::Timer5
  }};
  for (std::size_t index = 0; index < 6; ++index)
  {
    CAPTURE(index);
    const auto &capability = IOPTimers::capabilities(index);
    REQUIRE(capability.counterBits == widths[index]);
    REQUIRE(capability.alternateSource == alternatives[index]);
    REQUIRE(capability.gate == gates[index]);
    REQUIRE(capability.prescaler == prescalers[index]);
    REQUIRE(capability.interruptSource == interrupts[index]);
    REQUIRE(&capability == &IOPTimers::capabilities(index));
  }
}

TEST_CASE("IOP timer owner starts at deterministic zero state")
{
  const IOPTimers timers;
  REQUIRE_FALSE(timers.gateLevels().horizontalBlank);
  REQUIRE_FALSE(timers.gateLevels().verticalBlank);
  for (std::size_t index = 0; index < 6; ++index)
  {
    CAPTURE(index);
    requireCounter(timers.counter(index), {});
    const auto configuration = timers.configuration(index);
    REQUIRE(configuration.source == IOPTimerSource::System);
    REQUIRE(configuration.gateMode == IOPTimerGateMode::FreeRunning);
    REQUIRE(configuration.divisor == 1);
    requireCounter(timers.counter(index), {});
  }
}

TEST_CASE("IOP timer owner truncates count and target without other effects")
{
  const std::array<IOPWord, 6> values = {{
    0, 0xffff, 0x10000, 0x12345678, 0x80000000, 0xffffffff
  }};
  for (std::size_t index = 0; index < 6; ++index)
  {
    for (IOPWord value : values)
    {
      CAPTURE(index);
      CAPTURE(value);
      IOPTimers timers;
      IOPTimersTestAccess::seed(timers);
      const IOPTimers before = timers;
      const IOPWord expectedValue = index < 3 ? value & 0xffff : value;

      auto expected = before.counter(index);
      expected.count = expectedValue;
      timers.writeCount(index, value);
      requireCounter(timers.counter(index), expected);
      requireOtherCounters(timers, before, index);

      expected.target = expectedValue;
      timers.writeTarget(index, value);
      requireCounter(timers.counter(index), expected);
      requireOtherCounters(timers, before, index);
    }
  }
}

TEST_CASE("IOP timer owner masks every mode bit and fully rearms on writes")
{
  REQUIRE(IOPTimerMode::WRITABLE_MASK == 0x63ff);
  REQUIRE(IOPTimerMode::REQUEST_IDLE == 0x0400);
  REQUIRE(IOPTimerMode::TARGET_REACHED == 0x0800);
  REQUIRE(IOPTimerMode::OVERFLOW_REACHED == 0x1000);
  for (std::size_t index = 0; index < 6; ++index)
  {
    for (unsigned int bit = 0; bit < 32; ++bit)
    {
      CAPTURE(index);
      CAPTURE(bit);
      IOPTimers timers;
      IOPTimersTestAccess::seed(timers);
      const IOPTimers before = timers;
      const IOPWord value = UINT32_C(1) << bit;
      auto expected = IOPTimerCounterState{};
      expected.mode = (value & 0x63ff) | 0x0400;
      expected.target = before.counter(index).target;

      timers.writeMode(index, value);
      requireCounter(timers.counter(index), expected);
      requireOtherCounters(timers, before, index);
    }
    IOPTimers timers;
    IOPTimersTestAccess::seed(timers);
    const IOPTimers before = timers;
    timers.writeMode(index, 0xffffffff);
    auto expected = IOPTimerCounterState{};
    expected.mode = 0x67ff;
    expected.target = before.counter(index).target;
    expected.gateWaiting = index == 0 || index == 1 || index == 3;
    requireCounter(timers.counter(index), expected);
    requireOtherCounters(timers, before, index);
  }
}

TEST_CASE("IOP timer owner decodes all gate source and prescaler selectors")
{
  const std::array<IOPTimerGateMode, 8> externalGates = {{
    IOPTimerGateMode::FreeRunning, IOPTimerGateMode::CountOutsideBlank,
    IOPTimerGateMode::FreeRunning, IOPTimerGateMode::ClearOnBlankEnd,
    IOPTimerGateMode::FreeRunning, IOPTimerGateMode::CountInBlank,
    IOPTimerGateMode::FreeRunning, IOPTimerGateMode::StartOnBlankEnd
  }};
  const std::array<IOPTimerGateMode, 8> localGates = {{
    IOPTimerGateMode::FreeRunning, IOPTimerGateMode::Stopped,
    IOPTimerGateMode::FreeRunning, IOPTimerGateMode::FreeRunning,
    IOPTimerGateMode::FreeRunning, IOPTimerGateMode::Stopped,
    IOPTimerGateMode::FreeRunning, IOPTimerGateMode::FreeRunning
  }};
  const std::array<IOPTimerSource, 6> alternatives = {{
    IOPTimerSource::Pixel, IOPTimerSource::HBlank, IOPTimerSource::System,
    IOPTimerSource::HBlank, IOPTimerSource::System, IOPTimerSource::System
  }};
  const std::array<std::uint16_t, 4> divisors = {{1, 8, 16, 256}};
  for (std::size_t index = 0; index < 6; ++index)
  {
    const bool externalGate = index == 0 || index == 1 || index == 3;
    for (IOPWord gate = 0; gate < 8; ++gate)
    {
      for (IOPWord source = 0; source < 2; ++source)
      {
        for (IOPWord smallPrescale = 0; smallPrescale < 2; ++smallPrescale)
        {
          for (IOPWord largePrescale = 0; largePrescale < 4; ++largePrescale)
          {
            CAPTURE(index);
            CAPTURE(gate);
            CAPTURE(source);
            CAPTURE(smallPrescale);
            CAPTURE(largePrescale);
            IOPTimers timers;
            timers.writeMode(index, gate | (source << 8) |
              (smallPrescale << 9) | (largePrescale << 13));
            const auto configuration = timers.configuration(index);
            REQUIRE(configuration.source ==
              (source == 0 ? IOPTimerSource::System : alternatives[index]));
            REQUIRE(configuration.gateMode ==
              (externalGate ? externalGates[gate] : localGates[gate]));
            const std::uint16_t expectedDivisor = index == 2 ?
              (smallPrescale == 0 ? 1 : 8) :
              (index >= 4 ? divisors[largePrescale] : 1);
            REQUIRE(configuration.divisor == expectedDivisor);
            REQUIRE(timers.counter(index).gateWaiting ==
              (externalGate && (gate == 5 || gate == 7)));
          }
        }
      }
    }
  }
}

TEST_CASE("IOP timer owner mode reads return old status and only acknowledge IRQ state")
{
  const std::array<IOPWord, 4> flags = {{0, 0x0800, 0x1000, 0x1800}};
  for (std::size_t index = 0; index < 6; ++index)
  {
    for (IOPWord events : flags)
    {
      for (IOPWord idle : {IOPWord{0}, IOPWord{0x0400}})
      {
        CAPTURE(index);
        CAPTURE(events);
        CAPTURE(idle);
        IOPTimers timers;
        IOPTimersTestAccess::seed(timers, 0x6235 | events | idle);
        const IOPTimers before = timers;
        auto expected = before.counter(index);
        expected.mode = 0x6635;
        expected.oneShotConsumed = false;
        expected.pulseReleasePending = false;
        const IOPTimers &inspection = timers;
        requireCounter(inspection.counter(index), before.counter(index));
        inspection.configuration(index);
        requireTimers(timers, before);

        REQUIRE(timers.readMode(index) == (0x6235 | events | idle));
        requireCounter(timers.counter(index), expected);
        requireOtherCounters(timers, before, index);
        REQUIRE(timers.readMode(index) == 0x6635);
        requireCounter(timers.counter(index), expected);
      }
    }
  }
}

TEST_CASE("IOP timer owner reset clears canonical continuation state independently")
{
  IOPTimers timers;
  IOPTimers other;
  IOPTimersTestAccess::seed(timers);
  IOPTimersTestAccess::seed(other);
  const IOPTimers before = other;
  timers.reset();
  requireTimers(timers, IOPTimers{});
  requireTimers(other, before);
  for (std::size_t index = 0; index < 6; ++index)
  {
    REQUIRE(timers.readMode(index) == 0);
    REQUIRE(timers.counter(index).mode == 0x0400);
  }
  timers.reset();
  timers.reset();
  requireTimers(timers, IOPTimers{});
  other.writeCount(0, 0xffffffff);
  other.writeTarget(5, 0xffffffff);
  other.writeMode(3, 7);
  requireTimers(timers, IOPTimers{});
}

TEST_CASE("IOP timer owner rejects invalid indices before mutation")
{
  const std::array<std::size_t, 5> invalid = {{
    6, 7, 31, 255, std::numeric_limits<std::size_t>::max()
  }};
  IOPTimers timers;
  IOPTimersTestAccess::seed(timers);
  const IOPTimers before = timers;
  for (std::size_t index : invalid)
  {
    CAPTURE(index);
    REQUIRE_THROWS_AS(IOPTimers::capabilities(index), std::out_of_range);
    REQUIRE_THROWS_AS(timers.counter(index), std::out_of_range);
    REQUIRE_THROWS_AS(timers.configuration(index), std::out_of_range);
    REQUIRE_THROWS_AS(timers.writeCount(index, 0), std::out_of_range);
    REQUIRE_THROWS_AS(timers.writeTarget(index, 0), std::out_of_range);
    REQUIRE_THROWS_AS(timers.writeMode(index, 0), std::out_of_range);
    REQUIRE_THROWS_AS(timers.readMode(index), std::out_of_range);
    requireTimers(timers, before);
  }
}
