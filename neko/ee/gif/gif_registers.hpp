#ifndef GIF_REGISTERS_HPP
#define GIF_REGISTERS_HPP

#include <cstdint>

#include "gif_path_arbiter.hpp"

namespace GIFMode
{
  constexpr std::uint32_t M3R = 1u << 0;
  constexpr std::uint32_t IMT = 1u << 2;
}

namespace GIFStatus
{
  constexpr std::uint32_t M3R = 1u << 0;
  constexpr std::uint32_t M3P = 1u << 1;
  constexpr std::uint32_t IMT = 1u << 2;
  constexpr std::uint32_t IP3 = 1u << 5;
  constexpr std::uint32_t P3Q = 1u << 6;
  constexpr std::uint32_t P2Q = 1u << 7;
  constexpr std::uint32_t P1Q = 1u << 8;
  constexpr std::uint32_t OPH = 1u << 9;
  constexpr std::uint8_t APATH_SHIFT = 10;
  constexpr std::uint32_t APATH_MASK = 0x03u << APATH_SHIFT;
}

class GIFRegisters
{
  public:
    explicit GIFRegisters(GIFPathArbiter *arbiter);

    void writeMode(std::uint32_t value);
    std::uint32_t readStatus() const;
    std::uint32_t readPath3Count() const;
    std::uint32_t readPath3Tag() const;

  private:
    GIFPathArbiter *gifPathArbiter;
};

#endif
