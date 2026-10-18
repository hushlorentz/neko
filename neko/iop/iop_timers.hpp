#ifndef IOP_TIMERS_HPP
#define IOP_TIMERS_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "iop_interrupt_controller.hpp"
#include "iop_observation.hpp"
#include "iop_types.hpp"

namespace IOPTimerMode
{
  constexpr IOPWord GATE_ENABLE = 0x0001;
  constexpr IOPWord GATE_MODE_MASK = 0x0006;
  constexpr IOPWord RESET_ON_TARGET = 0x0008;
  constexpr IOPWord INTERRUPT_ON_TARGET = 0x0010;
  constexpr IOPWord INTERRUPT_ON_OVERFLOW = 0x0020;
  constexpr IOPWord REPEAT_INTERRUPT = 0x0040;
  constexpr IOPWord TOGGLE_INTERRUPT = 0x0080;
  constexpr IOPWord ALTERNATE_SOURCE = 0x0100;
  constexpr IOPWord DIVIDE_BY_8 = 0x0200;
  constexpr IOPWord REQUEST_IDLE = 0x0400;
  constexpr IOPWord TARGET_REACHED = 0x0800;
  constexpr IOPWord OVERFLOW_REACHED = 0x1000;
  constexpr IOPWord PRESCALER_MASK = 0x6000;
  constexpr IOPWord WRITABLE_MASK = 0x63ff;
  constexpr IOPWord EVENT_MASK = TARGET_REACHED | OVERFLOW_REACHED;
}

enum class IOPTimerSource : std::uint8_t
{
  System,
  Pixel,
  HBlank
};

enum class IOPTimerGate : std::uint8_t
{
  None,
  HBlank,
  VBlank
};

enum class IOPTimerPrescalerField : std::uint8_t
{
  None,
  Bit9,
  Bits13And14
};

enum class IOPTimerGateMode : std::uint8_t
{
  FreeRunning,
  CountOutsideBlank,
  ClearOnBlankEnd,
  CountInBlank,
  StartOnBlankEnd,
  Stopped
};

struct IOPTimerCapabilities
{
  std::uint8_t counterBits = 16;
  IOPTimerSource alternateSource = IOPTimerSource::System;
  IOPTimerGate gate = IOPTimerGate::None;
  IOPTimerPrescalerField prescaler = IOPTimerPrescalerField::None;
  IOPInterruptSource interruptSource = IOPInterruptSource::Timer0;
};

struct IOPTimerConfiguration
{
  IOPTimerSource source = IOPTimerSource::System;
  IOPTimerGateMode gateMode = IOPTimerGateMode::FreeRunning;
  std::uint16_t divisor = 1;
};

struct IOPTimerCounterState
{
  IOPWord count = 0;
  IOPWord mode = 0;
  IOPWord target = 0;
  std::uint16_t prescalerRemainder = 0;
  bool gateWaiting = false;
  bool oneShotConsumed = false;
  bool pulseReleasePending = false;
};

struct IOPTimerGateLevels
{
  bool horizontalBlank = false;
  bool verticalBlank = false;
};

struct IOPTimerTimingSample
{
  bool systemClockPulse = false;
  bool pixelClockPulse = false;
  // Complete levels for this sample; H-blank starts also supply its clock.
  IOPTimerGateLevels gates;
};

class IOPTimers final
{
  public:
    static constexpr std::size_t COUNTER_COUNT = 6;

    struct InterruptRequests
    {
      // Only the first count entries are valid, ordered by timer index.
      std::array<IOPInterruptSource, COUNTER_COUNT> sources = {};
      std::size_t count = 0;
    };

    void reset();
    void writeCount(std::size_t index, IOPWord value);
    void writeTarget(std::size_t index, IOPWord value);
    void writeMode(std::size_t index, IOPWord value);
    IOPWord readMode(std::size_t index);
    InterruptRequests advance(const IOPTimerTimingSample &sample);

    // Inspection does not perform the guest mode-register acknowledgement.
    const IOPTimerCounterState &counter(std::size_t index) const;
    const IOPTimerGateLevels &gateLevels() const;
    IOPTimerConfiguration configuration(std::size_t index) const;
    static const IOPTimerCapabilities &capabilities(std::size_t index);

  private:
    friend class NekoSystem;
    friend class NekoSaveStateCodec;
    friend struct IOPTimersTestAccess;
    friend struct IOPDeviceObservationTestAccess;

    enum class Register : std::uint8_t
    {
      None,
      Count,
      Mode,
      Target
    };

    enum EventFlag : std::uint8_t
    {
      TargetReached = 1 << 0,
      OverflowReached = 1 << 1,
      InterruptRequested = 1 << 2,
      PulseReleased = 1 << 3,
      GateChanged = 1 << 4,
      ModeAcknowledged = 1 << 5,
      RegisterWritten = 1 << 6
    };

    struct Observation
    {
      std::size_t index = 0;
      IOPTimerCounterState before;
      IOPTimerCounterState after;
      IOPTimerTimingSample sample;
      IOPTimerGateLevels gatesBefore;
      Register registerKind = Register::None;
      std::uint8_t flags = 0;
      std::size_t sequence = 0;
    };

    // Six older pulse releases, six counter/gate events, CPU fetch + data MMIO.
    static constexpr std::size_t OBSERVATION_CAPACITY = 2 * COUNTER_COUNT + 2;
    std::array<Observation, OBSERVATION_CAPACITY> observations = {};
    std::size_t observationCount = 0;
    bool observationsEnabled = false;
    IOPObservationOrder *observationOrder = nullptr;

    void enableObservations(IOPObservationOrder *order = nullptr);
    void disableObservations();
    void clearObservations();
    void recordObservation(const Observation &observation);
    void recordRegisterAccess(
      std::size_t index, const IOPTimerCounterState &before,
      Register registerKind, EventFlag flag);

    std::array<IOPTimerCounterState, COUNTER_COUNT> counters = {};
    IOPTimerGateLevels blanking;
};

#endif
