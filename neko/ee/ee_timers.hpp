#ifndef EE_TIMERS_HPP
#define EE_TIMERS_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace EETimerRegisterMap
{
  constexpr std::uint32_t BASE = 0x10000000;
  constexpr std::uint32_t STRIDE = 0x800;
  constexpr std::uint32_t COUNT = 0;
  constexpr std::uint32_t MODE = 0x10;
  constexpr std::uint32_t COMP = 0x20;
  constexpr std::uint32_t HOLD = 0x30;
}

namespace EETimerMode
{
  constexpr std::uint16_t CLOCK = 0x003;
  constexpr std::uint16_t GATE = 0x004;
  constexpr std::uint16_t VBLANK_GATE = 0x008;
  constexpr std::uint16_t GATE_MODE = 0x030;
  constexpr std::uint16_t ZERO_RETURN = 0x040;
  constexpr std::uint16_t COUNT_ENABLE = 0x080;
  constexpr std::uint16_t COMPARE_ENABLE = 0x100;
  constexpr std::uint16_t OVERFLOW_ENABLE = 0x200;
  constexpr std::uint16_t EQUAL_FLAG = 0x400;
  constexpr std::uint16_t OVERFLOW_FLAG = 0x800;
  constexpr std::uint16_t CONTROLS = 0x3ff;
  constexpr std::uint16_t FLAGS = 0xc00;
}

struct EETimerGateLevels
{
  bool hBlank = false;
  bool vBlank = false;
};

class EETimers
{
  public:
    static constexpr std::size_t TIMER_COUNT = 4;

    static bool isRegisterAddress(std::uint32_t address);
    std::uint32_t readRegister(std::uint32_t address) const;
    void writeRegister(std::uint32_t address, std::uint32_t value);
    // One completed master cycle; returned bits identify new timer IRQ requests.
    std::uint8_t advance(
      std::uint64_t masterCycle, const EETimerGateLevels &levels);
    void captureSBUS() noexcept;
    std::uint64_t stateHash(std::uint64_t masterCycle) const;

  private:
    friend class NekoSaveStateCodec;

    struct Timer
    {
      std::uint16_t count = 0;
      std::uint16_t mode = 0;
      std::uint16_t compare = 0;
      std::uint16_t hold = 0;
    };

    static bool decode(
      std::uint32_t address, std::size_t *index, std::uint32_t *offset);
    static bool increment(Timer *timer);
    static std::uint64_t clockPeriod(unsigned clock);

    std::array<Timer, TIMER_COUNT> timers{};
    EETimerGateLevels previousLevels;
};

#endif
