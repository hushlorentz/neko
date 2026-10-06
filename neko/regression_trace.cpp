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

std::uint64_t nekoPackEEMemoryTraceMetadata(
  const NekoEETraceMemory::Metadata &metadata)
{
  using namespace NekoEETraceMemory;
  return
    metadata.width |
    (metadata.accessKind == AccessKind::DataStore ? WRITE : 0) |
    (metadata.transferOutcome == TransferOutcome::Completed
      ? SUCCEEDED
      : 0) |
    (static_cast<std::uint64_t>(metadata.accessKind) <<
      ACCESS_KIND_SHIFT) |
    (static_cast<std::uint64_t>(
      metadata.translationOutcome) <<
      TRANSLATION_OUTCOME_SHIFT) |
    (static_cast<std::uint64_t>(metadata.cacheRoute) <<
      CACHE_ROUTE_SHIFT) |
    (static_cast<std::uint64_t>(metadata.cacheAccess) <<
      CACHE_ACCESS_SHIFT) |
    (static_cast<std::uint64_t>(
      metadata.transferOutcome) <<
      TRANSFER_OUTCOME_SHIFT) |
    (metadata.scratchpadRoute ? SCRATCHPAD_ROUTE : 0) |
    (metadata.physicalAddressValid
      ? PHYSICAL_ADDRESS_VALID
      : 0) |
    (static_cast<std::uint64_t>(
      metadata.cacheAttribute & 0x7) <<
      CACHE_ATTRIBUTE_SHIFT) |
    (static_cast<std::uint64_t>(metadata.failurePhase) <<
      FAILURE_PHASE_SHIFT) |
    (static_cast<std::uint64_t>(metadata.physicalAddress) <<
      PHYSICAL_ADDRESS_SHIFT);
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
    hashU64(&hash, event.value3);
  }
  return hash;
}
