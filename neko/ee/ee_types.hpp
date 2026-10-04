#ifndef EE_TYPES_HPP
#define EE_TYPES_HPP

#include <cstdint>

struct EEQuadword
{
  std::uint64_t low = 0;
  std::uint64_t high = 0;
};

#endif
