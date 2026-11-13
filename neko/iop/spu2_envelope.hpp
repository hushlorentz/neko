#ifndef SPU2_ENVELOPE_HPP
#define SPU2_ENVELOPE_HPP

#include <cstdint>

namespace SPU2Envelope
{
  enum class Phase : std::uint8_t
  {
    Stopped, Attack, Decay, Sustain, Release
  };

  // Caller-owned runtime value; programmed ADSR words stay with SPU2Controller.
  struct State
  {
    Phase phase = Phase::Stopped;
    std::uint16_t level = 0;
    std::uint32_t counter = 0;
  };

  void keyOn(State &state) noexcept;
  void keyOff(State &state) noexcept;
  void parametersWritten(State &state) noexcept;
  // One sample tick. The caller applies the resulting level to this tick's source.
  void advance(State &state, std::uint16_t adsr1, std::uint16_t adsr2) noexcept;
}

#endif
