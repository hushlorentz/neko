#ifndef INTERRUPT_CONTROLLER_HPP
#define INTERRUPT_CONTROLLER_HPP

#include <cstdint>

namespace EEInterruptSource
{
  constexpr std::uint8_t GS = 0;
  constexpr std::uint8_t VBLANK_START = 2;
  constexpr std::uint8_t VBLANK_END = 3;
  constexpr std::uint8_t VIF0 = 4;
  constexpr std::uint8_t VIF1 = 5;
  constexpr std::uint8_t VU0 = 6;
  constexpr std::uint8_t VU1 = 7;

  constexpr std::uint32_t mask(std::uint8_t source)
  {
    return UINT32_C(1) << source;
  }
}

class EEInterruptController
{
  public:
    void setSource(std::uint8_t source, bool pending);
    void acknowledge(std::uint32_t sources);
    void toggleMask(std::uint32_t sources);
    std::uint32_t status() const;
    std::uint32_t mask() const;
    bool interruptPending() const;

  private:
    std::uint32_t statusRegister = 0;
    std::uint32_t maskRegister = 0;
};

#endif
