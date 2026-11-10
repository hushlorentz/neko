#ifndef DEV9_REVISION_HPP
#define DEV9_REVISION_HPP

#include <cstdint>

namespace DEV9Revision
{
  constexpr std::uint32_t ADDRESS = 0x1f80146e;
  // Provisional absent-DEV9 profile, not a measured controller revision.
  constexpr std::uint16_t ABSENT_VALUE = 0;
}

#endif
