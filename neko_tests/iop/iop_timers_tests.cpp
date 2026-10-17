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

namespace
{
  const IOPTimerTimingSample systemPulse = {true, false, {}};
  const IOPTimerTimingSample pixelPulse = {false, true, {}};
  const IOPTimerTimingSample bothPulses = {true, true, {}};
  const IOPTimerTimingSample noPulses = {};

  IOPTimerTimingSample withGates(
    IOPTimerTimingSample sample, IOPTimerGateLevels gates)
  {
    sample.gates = gates;
    return sample;
  }

  IOPTimerGateLevels gateLevels(std::size_t index, bool blank)
  {
    return index == 0 ?
      IOPTimerGateLevels{blank, false} : IOPTimerGateLevels{false, blank};
  }

  void requireRequest(
    const IOPTimers::InterruptRequests &requests, std::size_t index)
  {
    REQUIRE(requests.count == 1);
    REQUIRE(requests.sources[0] == IOPTimers::capabilities(index).interruptSource);
  }

  void latchRequests(
    IOPInterruptController &interrupts,
    const IOPTimers::InterruptRequests &requests)
  {
    REQUIRE(requests.count <= requests.sources.size());
    for (std::size_t index = 0; index < requests.count; ++index)
    {
      interrupts.assertSource(requests.sources[index]);
    }
  }
}

TEST_CASE("IOP timer progression selects one source and derives H blank starts")
{
  const std::array<IOPTimerTimingSample, 6> samples = {{
    systemPulse, pixelPulse, withGates(noPulses, {true, false}),
    withGates(noPulses, {true, false}), noPulses,
    withGates(bothPulses, {true, true})
  }};
  const std::array<IOPWord, 6> systemCounts = {{1, 1, 1, 1, 1, 2}};
  const std::array<IOPWord, 6> pixelCounts = {{0, 1, 1, 1, 1, 2}};
  const std::array<IOPWord, 6> blankCounts = {{0, 0, 1, 1, 1, 2}};
  for (std::size_t index = 0; index < 6; ++index)
  {
    for (IOPWord alternate : {IOPWord{0}, IOPWord{0x100}})
    {
      CAPTURE(index);
      CAPTURE(alternate);
      IOPTimers timers;
      timers.writeMode(index, alternate);
      for (std::size_t step = 0; step < samples.size(); ++step)
      {
        CAPTURE(step);
        REQUIRE(timers.advance(samples[step]).count == 0);
        const auto &expected = alternate == 0 ||
          index == 2 || index >= 4 ? systemCounts :
          (index == 0 ? pixelCounts : blankCounts);
        REQUIRE(timers.counter(index).count == expected[step]);
        REQUIRE(timers.counter(index).mode == (alternate | 0x0400));
      }
      REQUIRE(timers.gateLevels().horizontalBlank);
      REQUIRE(timers.gateLevels().verticalBlank);
    }
  }
  IOPTimers resetTimers;
  REQUIRE(resetTimers.advance(bothPulses).count == 0);
  for (std::size_t index = 0; index < 6; ++index)
  {
    REQUIRE(resetTimers.counter(index).count == 1);
    REQUIRE(resetTimers.counter(index).mode == 0);
  }
}

TEST_CASE("IOP timer progression preserves exact prescaler phase across register accesses")
{
  const std::array<std::uint16_t, 4> divisors = {{1, 8, 16, 256}};
  for (std::size_t index = 0; index < 6; ++index)
  {
    for (IOPWord small = 0; small < 2; ++small)
    {
      for (IOPWord large = 0; large < 4; ++large)
      {
        CAPTURE(index);
        CAPTURE(small);
        CAPTURE(large);
        IOPTimers timers;
        timers.writeMode(index, (small << 9) | (large << 13));
        const unsigned int divisor = index == 2 ? (small == 0 ? 1 : 8) :
          (index >= 4 ? divisors[large] : 1);
        for (unsigned int pulse = 1; pulse <= divisor * 2 + 3; ++pulse)
        {
          REQUIRE(timers.advance(systemPulse).count == 0);
          REQUIRE(timers.counter(index).count == pulse / divisor);
          REQUIRE(timers.counter(index).prescalerRemainder == pulse % divisor);
          const auto before = timers.counter(index);
          timers.writeCount(index, before.count);
          timers.writeTarget(index, before.target);
          timers.readMode(index);
          requireCounter(timers.counter(index), before);
          REQUIRE(timers.advance(pixelPulse).count == 0);
          requireCounter(timers.counter(index), before);
        }
        timers.writeMode(index, (small << 9) | (large << 13));
        REQUIRE(timers.counter(index).count == 0);
        REQUIRE(timers.counter(index).prescalerRemainder == 0);
      }
    }
  }
}

TEST_CASE("IOP timer progression observes target and natural overflow after increments")
{
  for (std::size_t index = 0; index < 6; ++index)
  {
    const IOPWord maximum = index < 3 ? 0xffff : 0xffffffff;
    for (IOPWord target : {IOPWord{0}, IOPWord{1}, maximum})
    {
      for (IOPWord resetOnTarget : {IOPWord{0}, IOPWord{8}})
      {
        CAPTURE(index);
        CAPTURE(target);
        CAPTURE(resetOnTarget);
        IOPTimers timers;
        timers.writeMode(index, resetOnTarget);
        timers.writeTarget(index, target);
        timers.writeCount(index, target == 0 ? maximum : target - 1);
        REQUIRE(timers.advance(noPulses).count == 0);
        REQUIRE((timers.counter(index).mode & 0x1800) == 0);
        REQUIRE(timers.advance(systemPulse).count == 0);
        REQUIRE(timers.counter(index).count ==
          (resetOnTarget == 0 ? target : 0));
        REQUIRE((timers.counter(index).mode & 0x1800) ==
          (target == 0 ? 0x1800 : 0x0800));
        REQUIRE_FALSE(timers.counter(index).oneShotConsumed);
        REQUIRE_FALSE(timers.counter(index).pulseReleasePending);
        timers.readMode(index);
        REQUIRE(timers.advance(systemPulse).count == 0);
        const IOPWord nextFlags = target == 1 && resetOnTarget != 0 ?
          0x0800 : (target == maximum && resetOnTarget == 0 ? 0x1000 : 0);
        REQUIRE((timers.counter(index).mode & 0x1800) == nextFlags);
      }
    }
    IOPTimers timers;
    timers.writeTarget(index, 7);
    timers.writeCount(index, 7);
    REQUIRE(timers.advance(systemPulse).count == 0);
    REQUIRE(timers.counter(index).count == 8);
    REQUIRE((timers.counter(index).mode & 0x1800) == 0);
    timers.writeTarget(index, 3);
    REQUIRE(timers.advance(systemPulse).count == 0);
    REQUIRE((timers.counter(index).mode & 0x1800) == 0);
    timers.writeCount(index, maximum);
    REQUIRE(timers.advance(systemPulse).count == 0);
    REQUIRE((timers.counter(index).mode & 0x1800) == 0x1000);
    for (unsigned int pulse = 0; pulse < 3; ++pulse)
    {
      timers.advance(systemPulse);
    }
    REQUIRE(timers.counter(index).count == 3);
    REQUIRE((timers.counter(index).mode & 0x1800) == 0x1800);
  }
}

TEST_CASE("IOP timer progression qualifies and coalesces one shot repeat pulse and toggle requests")
{
  for (std::size_t index = 0; index < 6; ++index)
  {
    const IOPWord maximum = index < 3 ? 0xffff : 0xffffffff;
    for (IOPWord enables = 0; enables < 4; ++enables)
    {
      for (IOPWord repeat = 0; repeat < 2; ++repeat)
      {
        for (IOPWord toggle = 0; toggle < 2; ++toggle)
        {
          for (unsigned int kind = 0; kind < 3; ++kind)
          {
            CAPTURE(index);
            CAPTURE(enables);
            CAPTURE(repeat);
            CAPTURE(toggle);
            CAPTURE(kind);
            IOPTimers timers;
            timers.writeMode(index, (enables << 4) | (repeat << 6) |
              (toggle << 7));
            timers.writeTarget(index, kind == 0 ? 1 : (kind == 1 ? 2 : 0));
            const bool qualifies =
              ((kind != 1) && (enables & 1) != 0) ||
              ((kind != 0) && (enables & 2) != 0);
            for (unsigned int event = 0; event < 3; ++event)
            {
              timers.writeCount(index, kind == 0 ? 0 : maximum);
              const auto requests = timers.advance(systemPulse);
              const bool accepted = qualifies && (repeat != 0 || event == 0);
              const bool emits = accepted &&
                (toggle == 0 || event % 2 == 0);
              if (emits)
              {
                requireRequest(requests, index);
              }
              else
              {
                REQUIRE(requests.count == 0);
              }
              const bool idle = toggle == 0 ? !accepted :
                (!qualifies || (repeat != 0 && event % 2 != 0));
              REQUIRE(((timers.counter(index).mode & 0x0400) != 0) == idle);
              REQUIRE(timers.counter(index).pulseReleasePending ==
                (accepted && toggle == 0));
              REQUIRE(timers.counter(index).oneShotConsumed ==
                (qualifies && repeat == 0));
              REQUIRE((timers.counter(index).mode & 0x1800) ==
                (kind == 0 ? 0x0800 : (kind == 1 ? 0x1000 : 0x1800)));
            }
          }
        }
      }
    }
  }
}

TEST_CASE("IOP timer progression consumes one shot across both event kinds until acknowledgement")
{
  for (std::size_t index = 0; index < 6; ++index)
  {
    const IOPWord maximum = index < 3 ? 0xffff : 0xffffffff;
    for (IOPWord toggle : {IOPWord{0}, IOPWord{0x80}})
    {
      for (unsigned int first = 0; first < 2; ++first)
      {
        CAPTURE(index);
        CAPTURE(toggle);
        CAPTURE(first);
        IOPTimers timers;
        timers.writeMode(index, 0x30 | toggle);
        timers.writeTarget(index, first == 0 ? 1 : 2);
        timers.writeCount(index, first == 0 ? 0 : maximum);
        requireRequest(timers.advance(systemPulse), index);
        timers.writeTarget(index, first == 0 ? 2 : 1);
        timers.writeCount(index, first == 0 ? maximum : 0);
        REQUIRE(timers.advance(systemPulse).count == 0);
        REQUIRE((timers.counter(index).mode & 0x1800) == 0x1800);
        REQUIRE(timers.counter(index).oneShotConsumed);
        timers.readMode(index);
        timers.writeCount(index, first == 0 ? maximum : 0);
        requireRequest(timers.advance(systemPulse), index);
        timers.writeMode(index, 0x30 | toggle);
        timers.writeTarget(index, 1);
        requireRequest(timers.advance(systemPulse), index);
      }
    }
    for (IOPWord enable : {IOPWord{0x10}, IOPWord{0x20}})
    {
      IOPTimers timers;
      timers.writeMode(index, enable);
      timers.writeTarget(index, enable == 0x10 ? 2 : 1);
      timers.writeCount(index, enable == 0x10 ? maximum : 0);
      REQUIRE(timers.advance(systemPulse).count == 0);
      REQUIRE_FALSE(timers.counter(index).oneShotConsumed);
      timers.writeTarget(index, enable == 0x10 ? 1 : 2);
      timers.writeCount(index, enable == 0x10 ? 0 : maximum);
      requireRequest(timers.advance(systemPulse), index);
    }
  }
}

TEST_CASE("IOP timer progression qualifies events only at prescaled increments")
{
  const std::array<unsigned int, 4> divisors = {{1, 8, 16, 256}};
  for (std::size_t index : {std::size_t{2}, std::size_t{4}, std::size_t{5}})
  {
    for (IOPWord selector = 0; selector < (index == 2 ? 2 : 4); ++selector)
    {
      for (IOPWord target : {IOPWord{0}, IOPWord{1}})
      {
        CAPTURE(index);
        CAPTURE(selector);
        CAPTURE(target);
        IOPTimers timers;
        timers.writeMode(index, 0x78 | (selector << (index == 2 ? 9 : 13)));
        timers.writeTarget(index, target);
        const IOPWord initial = target == 0 ?
          (index < 3 ? 0xffff : 0xffffffff) : 0;
        timers.writeCount(index, initial);
        for (unsigned int pulse = 1; pulse < divisors[selector]; ++pulse)
        {
          REQUIRE(timers.advance(systemPulse).count == 0);
          REQUIRE(timers.counter(index).count == initial);
          REQUIRE(timers.counter(index).prescalerRemainder == pulse);
          REQUIRE((timers.counter(index).mode & 0x1800) == 0);
        }
        requireRequest(timers.advance(systemPulse), index);
        REQUIRE(timers.counter(index).count == 0);
        REQUIRE(timers.counter(index).prescalerRemainder == 0);
        REQUIRE((timers.counter(index).mode & 0x1800) ==
          (target == 0 ? 0x1800 : 0x0800));
        timers.readMode(index);
        const auto next = timers.advance(systemPulse);
        REQUIRE(next.count == (target == 1 && selector == 0 ? 1 : 0));
        REQUIRE((timers.counter(index).mode & 0x1800) ==
          (target == 1 && selector == 0 ? 0x0800 : 0));
      }
    }
  }
}

TEST_CASE("IOP timer progression gates all external profiles before coincident pulses")
{
  const std::array<std::array<IOPWord, 7>, 4> counts = {{
    {{1, 1, 1, 2, 3, 3, 4}},
    {{1, 2, 3, 1, 2, 3, 1}},
    {{0, 1, 2, 0, 0, 1, 0}},
    {{0, 0, 0, 1, 2, 3, 4}}
  }};
  const std::array<bool, 7> levels = {{false, true, true, false, false, true, false}};
  for (std::size_t index : {std::size_t{0}, std::size_t{1}, std::size_t{3}})
  {
    for (IOPWord selector = 0; selector < 4; ++selector)
    {
      CAPTURE(index);
      CAPTURE(selector);
      IOPTimers timers;
      timers.writeMode(index, 1 | (selector << 1));
      for (std::size_t step = 0; step < levels.size(); ++step)
      {
        CAPTURE(step);
        REQUIRE(timers.advance(withGates(
          systemPulse, gateLevels(index, levels[step]))).count == 0);
        REQUIRE(timers.counter(index).count == counts[selector][step]);
        REQUIRE(timers.counter(index).gateWaiting ==
          ((selector == 2 && step == 0) || (selector == 3 && step < 3)));
      }
    }
  }
}

TEST_CASE("IOP timer progression waits for fresh edges after writes during blank")
{
  for (std::size_t index : {std::size_t{0}, std::size_t{1}, std::size_t{3}})
  {
    for (IOPWord mode : {IOPWord{5}, IOPWord{7}})
    {
      CAPTURE(index);
      CAPTURE(mode);
      IOPTimers timers;
      timers.advance(withGates(noPulses, gateLevels(index, true)));
      timers.writeMode(index, mode);
      REQUIRE(timers.advance(withGates(
        systemPulse, gateLevels(index, true))).count == 0);
      REQUIRE(timers.counter(index).count == 0);
      REQUIRE(timers.counter(index).gateWaiting);
      const IOPTimerGateLevels unrelated = {true, true};
      REQUIRE(timers.advance(withGates(systemPulse, unrelated)).count == 0);
      REQUIRE(timers.counter(index).count == 0);
      REQUIRE(timers.counter(index).gateWaiting);
      if (mode == 7)
      {
        timers.writeCount(index, 9);
      }
      timers.advance(withGates(systemPulse, gateLevels(index, false)));
      REQUIRE(timers.counter(index).count == (mode == 7 ? 10 : 0));
      timers.advance(withGates(systemPulse, gateLevels(index, true)));
      REQUIRE(timers.counter(index).count == (mode == 7 ? 11 : 1));
      REQUIRE_FALSE(timers.counter(index).gateWaiting);
      timers.writeCount(index, 9);
      REQUIRE(timers.counter(index).count == 9);
      timers.advance(withGates(noPulses, gateLevels(index, false)));
      REQUIRE(timers.counter(index).count == (mode == 5 ? 0 : 9));
      timers.writeCount(index, 11);
      REQUIRE(timers.counter(index).count == 11);
      timers.advance(withGates(noPulses, gateLevels(index, false)));
      REQUIRE(timers.counter(index).count == (mode == 5 ? 0 : 11));
    }
  }
}

TEST_CASE("IOP timer progression stops local counters without accumulating prescaler phase")
{
  for (std::size_t index : {std::size_t{2}, std::size_t{4}, std::size_t{5}})
  {
    for (IOPWord gate = 0; gate < 8; ++gate)
    {
      CAPTURE(index);
      CAPTURE(gate);
      IOPTimers timers;
      timers.writeMode(index, gate | (index == 2 ? 0x0200 : 0x2000));
      const bool stopped = gate == 1 || gate == 5;
      for (unsigned int pulse = 0; pulse < 19; ++pulse)
      {
        timers.advance(withGates(bothPulses, {pulse % 2 != 0, pulse % 3 != 0}));
      }
      REQUIRE(timers.counter(index).count == (stopped ? 0 : 2));
      REQUIRE(timers.counter(index).prescalerRemainder == (stopped ? 0 : 3));
    }
  }
}

TEST_CASE("IOP timer progression resolves H blank source and V blank gates in one sample")
{
  for (std::size_t index : {std::size_t{1}, std::size_t{3}})
  {
    for (IOPWord selector = 0; selector < 4; ++selector)
    {
      CAPTURE(index);
      CAPTURE(selector);
      IOPTimers timers;
      timers.writeMode(index, 0x0101 | (selector << 1));
      timers.writeTarget(index, 0);
      REQUIRE(timers.advance(withGates(bothPulses, {true, true})).count == 0);
      REQUIRE(timers.counter(index).count ==
        (selector == 1 || selector == 2 ? 1 : 0));
      timers.advance(withGates(noPulses, {false, true}));
      REQUIRE(timers.advance(withGates(bothPulses, {true, false})).count == 0);
      REQUIRE(timers.counter(index).count == (selector == 2 ? 0 : 1));
      REQUIRE((timers.counter(index).mode & 0x1800) == 0);
    }
  }
}

TEST_CASE("IOP timer progression gate clears neither fire events nor rearm requests")
{
  for (std::size_t index : {std::size_t{0}, std::size_t{1}, std::size_t{3}})
  {
    for (IOPWord mode : {IOPWord{0x33}, IOPWord{0x35}})
    {
      CAPTURE(index);
      CAPTURE(mode);
      IOPTimers timers;
      timers.writeMode(index, mode);
      timers.writeTarget(index, 1);
      requireRequest(timers.advance(withGates(
        systemPulse, gateLevels(index, mode == 0x35))), index);
      timers.writeTarget(index, 0);
      timers.writeCount(index, index < 3 ? 0xffff : 0xffffffff);
      const auto before = timers.counter(index);
      REQUIRE(timers.advance(withGates(
        noPulses, gateLevels(index, true))).count == 0);
      requireCounter(timers.counter(index), before);
      REQUIRE(timers.advance(noPulses).count == 0);
      auto expected = before;
      expected.count = 0;
      requireCounter(timers.counter(index), expected);
    }
  }
}

TEST_CASE("IOP timer progression releases older pulses on system samples even while gated")
{
  for (std::size_t index : {std::size_t{0}, std::size_t{1}, std::size_t{3}})
  {
    CAPTURE(index);
    IOPTimers timers;
    timers.writeMode(index, 0x0159);
    timers.writeTarget(index, 1);
    const auto event = index == 0 ? pixelPulse :
      withGates(noPulses, {true, false});
    requireRequest(timers.advance(event), index);
    REQUIRE((timers.counter(index).mode & 0x0400) == 0);
    REQUIRE(timers.counter(index).pulseReleasePending);
    const auto paused = withGates(noPulses, {true, true});
    REQUIRE(timers.advance(paused).count == 0);
    REQUIRE(timers.advance(paused).count == 0);
    REQUIRE((timers.counter(index).mode & 0x0400) == 0);
    REQUIRE(timers.counter(index).pulseReleasePending);
    REQUIRE(timers.advance(withGates(systemPulse, {true, true})).count == 0);
    REQUIRE((timers.counter(index).mode & 0x0400) != 0);
    REQUIRE_FALSE(timers.counter(index).pulseReleasePending);
  }
  IOPTimers timers;
  timers.writeMode(0, 0x0158);
  timers.writeTarget(0, 1);
  requireRequest(timers.advance(pixelPulse), 0);
  requireRequest(timers.advance(bothPulses), 0);
  REQUIRE((timers.counter(0).mode & 0x0400) == 0);
  REQUIRE(timers.counter(0).pulseReleasePending);
  REQUIRE(timers.advance(systemPulse).count == 0);
  REQUIRE((timers.counter(0).mode & 0x0400) != 0);
}

TEST_CASE("IOP timer progression can repeat external pulse requests before release")
{
  IOPTimers timers;
  timers.writeMode(1, 0x0158);
  timers.writeTarget(1, 1);
  for (unsigned int event = 0; event < 3; ++event)
  {
    REQUIRE(timers.advance(noPulses).count == 0);
    requireRequest(timers.advance(withGates(noPulses, {true, false})), 1);
    REQUIRE((timers.counter(1).mode & 0x0400) == 0);
    REQUIRE(timers.counter(1).pulseReleasePending);
  }
  const auto oldMode = timers.readMode(1);
  REQUIRE((oldMode & 0x0400) == 0);
  REQUIRE((timers.counter(1).mode & 0x0400) != 0);
  REQUIRE_FALSE(timers.counter(1).pulseReleasePending);
  REQUIRE(timers.advance(withGates(systemPulse, {true, false})).count == 0);
}

TEST_CASE("IOP timer progression returns all simultaneous causes independently of INTC")
{
  IOPTimers timers;
  IOPInterruptController interrupts;
  for (std::size_t index = 0; index < 6; ++index)
  {
    timers.writeMode(index, 0x10);
    timers.writeTarget(index, 1);
  }
  const auto requests = timers.advance(bothPulses);
  REQUIRE(requests.count == 6);
  IOPWord expectedStatus = 0;
  for (std::size_t index = 0; index < 6; ++index)
  {
    REQUIRE(requests.sources[index] == IOPTimers::capabilities(index).interruptSource);
    expectedStatus |= UINT32_C(1) <<
      static_cast<unsigned int>(requests.sources[index]);
  }
  latchRequests(interrupts, requests);
  REQUIRE(interrupts.status() == expectedStatus);
  REQUIRE_FALSE(interrupts.interruptPending());
  interrupts.writeMask(expectedStatus);
  interrupts.writeControl(1);
  REQUIRE(interrupts.interruptPending());
  interrupts.writeStatus(0);
  latchRequests(interrupts, timers.advance(noPulses));
  REQUIRE(interrupts.status() == 0);
  latchRequests(interrupts, requests);
  for (std::size_t index = 0; index < 6; ++index)
  {
    timers.readMode(index);
    timers.writeMode(index, 0);
  }
  timers.reset();
  REQUIRE(interrupts.status() == expectedStatus);
  REQUIRE(interrupts.interruptPending());
  interrupts.reset();
  REQUIRE(timers.advance(systemPulse).count == 0);
  REQUIRE(interrupts.status() == 0);
}

TEST_CASE("IOP timer progression repeats deterministically and resets active continuation")
{
  IOPTimers timers;
  timers.writeMode(0, 0x0158);
  timers.writeTarget(0, 1);
  timers.writeMode(1, 7);
  timers.writeMode(4, 0x6000);
  requireRequest(timers.advance(pixelPulse), 0);
  timers.advance(systemPulse);
  IOPTimers copy = timers;
  const std::array<IOPTimerTimingSample, 5> samples = {{
    withGates(bothPulses, {true, true}),
    withGates(pixelPulse, {true, true}),
    withGates(noPulses, {false, true}),
    bothPulses, systemPulse
  }};
  for (unsigned int iteration = 0; iteration < 60; ++iteration)
  {
    for (const auto &sample : samples)
    {
      const auto first = timers.advance(sample);
      const auto second = copy.advance(sample);
      REQUIRE(first.count == second.count);
      for (std::size_t index = 0; index < first.count; ++index)
      {
        REQUIRE(first.sources[index] == second.sources[index]);
      }
      requireTimers(timers, copy);
    }
  }
  timers.advance(pixelPulse);
  timers.reset();
  requireTimers(timers, IOPTimers{});
  REQUIRE(timers.advance(withGates(bothPulses, {true, true})).count == 0);
  for (std::size_t index = 0; index < 6; ++index)
  {
    REQUIRE(timers.counter(index).count == 1);
    REQUIRE(timers.counter(index).mode == 0);
  }
}
