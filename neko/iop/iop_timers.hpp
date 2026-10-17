#ifndef IOP_TIMERS_HPP
#define IOP_TIMERS_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "iop_interrupt_controller.hpp"
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

class IOPTimers final
{
  public:
    static constexpr std::size_t COUNTER_COUNT = 6;

    void reset();
    void writeCount(std::size_t index, IOPWord value);
    void writeTarget(std::size_t index, IOPWord value);
    void writeMode(std::size_t index, IOPWord value);
    IOPWord readMode(std::size_t index);

    // Inspection does not perform the guest mode-register acknowledgement.
    const IOPTimerCounterState &counter(std::size_t index) const;
    const IOPTimerGateLevels &gateLevels() const;
    IOPTimerConfiguration configuration(std::size_t index) const;
    static const IOPTimerCapabilities &capabilities(std::size_t index);

  private:
    friend struct IOPTimersTestAccess;

    std::array<IOPTimerCounterState, COUNTER_COUNT> counters = {};
    IOPTimerGateLevels blanking;
};

#endif
