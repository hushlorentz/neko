#include "regression_trace.hpp"

namespace
{
  constexpr std::uint64_t FNV_OFFSET_BASIS =
    UINT64_C(14695981039346656037);
  constexpr std::uint64_t FNV_PRIME =
    UINT64_C(1099511628211);

  void hashU64(std::uint64_t *hash, std::uint64_t value)
  {
    for (std::uint8_t index = 0; index < 8; ++index)
    {
      *hash ^= static_cast<std::uint8_t>(value >> (index * 8));
      *hash *= FNV_PRIME;
    }
  }
}

std::uint64_t nekoFrameHash(
  const GSPresentation &presentation)
{
  std::uint64_t hash = FNV_OFFSET_BASIS;
  hashU64(&hash, presentation.width);
  hashU64(&hash, presentation.height);
  hashU64(&hash, presentation.rgba.size());
  for (const std::uint8_t value : presentation.rgba)
  {
    hash ^= value;
    hash *= FNV_PRIME;
  }
  return hash;
}

std::uint64_t nekoTraceHash(
  const std::vector<NekoTraceEvent> &events)
{
  std::uint64_t hash = FNV_OFFSET_BASIS;
  hashU64(&hash, events.size());
  for (const NekoTraceEvent &event : events)
  {
    hashU64(&hash, event.masterCycle);
    hashU64(&hash, static_cast<std::uint8_t>(event.subsystem));
    hashU64(&hash, static_cast<std::uint8_t>(event.type));
    hashU64(&hash, event.value0);
    hashU64(&hash, event.value1);
    hashU64(&hash, event.value2);
  }
  return hash;
}
