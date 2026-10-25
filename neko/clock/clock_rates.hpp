#ifndef CLOCK_RATES_HPP
#define CLOCK_RATES_HPP

#include <cstdint>

namespace PS2Clock
{
  constexpr std::uint64_t MASTER_HZ = 294912000;
  constexpr std::uint64_t EE_BUS_HZ = MASTER_HZ / 2;
  constexpr std::uint64_t EE_BUS_CLOCK_PERIOD = MASTER_HZ / EE_BUS_HZ;
  constexpr std::uint64_t IOP_HZ = 36864000;
  constexpr std::uint64_t IOP_CLOCK_PERIOD = MASTER_HZ / IOP_HZ;
  constexpr std::uint64_t IOP_CLOCK_PHASE = 0;
  constexpr std::uint64_t IOP_PIXEL_HZ = 13500000;
  constexpr std::uint16_t IOP_PIXEL_INCREMENT = 375;
  constexpr std::uint16_t IOP_PIXEL_PERIOD = 8192;

  static_assert(MASTER_HZ * IOP_PIXEL_INCREMENT ==
    IOP_PIXEL_HZ * IOP_PIXEL_PERIOD, "IOP pixel ratio must be exact.");
  static_assert(IOP_CLOCK_PERIOD == 8 &&
    MASTER_HZ == IOP_HZ * IOP_CLOCK_PERIOD,
    "IOP system clock ratio must be exactly eight.");
}

#endif
