#ifndef CLOCK_RATES_HPP
#define CLOCK_RATES_HPP

#include <cstdint>

namespace PS2Clock
{
  constexpr std::uint64_t MASTER_HZ = 294912000;
  constexpr std::uint64_t IOP_PIXEL_HZ = 13500000;
  constexpr std::uint16_t IOP_PIXEL_INCREMENT = 375;
  constexpr std::uint16_t IOP_PIXEL_PERIOD = 8192;

  static_assert(MASTER_HZ * IOP_PIXEL_INCREMENT ==
    IOP_PIXEL_HZ * IOP_PIXEL_PERIOD, "IOP pixel ratio must be exact.");
}

#endif
