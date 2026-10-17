#include "iop_cache.hpp"

#include <stdexcept>

constexpr std::size_t IOPCache::INSTRUCTION_SIZE;
constexpr std::size_t IOPCache::DATA_SIZE;
constexpr std::size_t IOPCache::INSTRUCTION_LINE_WORDS;
constexpr std::size_t IOPCache::DATA_LINE_WORDS;
constexpr std::size_t IOPCache::INSTRUCTION_LINE_COUNT;
constexpr std::size_t IOPCache::DATA_LINE_COUNT;

static_assert((IOPCache::INSTRUCTION_SIZE &
  (IOPCache::INSTRUCTION_SIZE - 1)) == 0,
  "IOP instruction-cache geometry must be a power of two.");
static_assert((IOPCache::DATA_SIZE & (IOPCache::DATA_SIZE - 1)) == 0,
  "IOP data-cache geometry must be a power of two.");

void IOPCache::reset()
{
  instructionLines = {};
  dataLines = {};
}

const IOPInstructionCacheLine &IOPCache::instructionLine(
  std::size_t index) const
{
  return instructionLines.at(index);
}

const IOPDataCacheLine &IOPCache::dataLine(std::size_t index) const
{
  return dataLines.at(index);
}

IOPCacheLocation IOPCache::location(
  IOPCacheBank bank,
  IOPAddress physicalAddress)
{
  std::size_t capacity;
  std::size_t lineWords;
  switch (bank)
  {
    case IOPCacheBank::Instruction:
      capacity = INSTRUCTION_SIZE;
      lineWords = INSTRUCTION_LINE_WORDS;
      break;
    case IOPCacheBank::Data:
      capacity = DATA_SIZE;
      lineWords = DATA_LINE_WORDS;
      break;
    default:
      throw std::invalid_argument("Unknown IOP cache bank.");
  }
  const IOPAddress lineBytes =
    static_cast<IOPAddress>(lineWords * sizeof(IOPWord));
  const IOPAddress indexMask = static_cast<IOPAddress>(capacity - 1);
  return {
    (physicalAddress & indexMask) / lineBytes,
    (physicalAddress & (lineBytes - 1)) / sizeof(IOPWord),
    physicalAddress & (sizeof(IOPWord) - 1),
    physicalAddress & ~indexMask,
    physicalAddress & ~(lineBytes - 1)
  };
}

IOPCacheAccessPlan IOPCache::planAccess(
  IOPCacheAccess access,
  IOPCacheControl control,
  IOPCacheRoute route,
  IOPBusRegion region)
{
  switch (access)
  {
    case IOPCacheAccess::InstructionFetch:
    case IOPCacheAccess::DataLoad:
    case IOPCacheAccess::DataStore:
      break;
    default:
      throw std::invalid_argument("Unknown IOP cache access.");
  }
  switch (route)
  {
    case IOPCacheRoute::Cached:
    case IOPCacheRoute::Uncached:
      break;
    case IOPCacheRoute::None:
    default:
      throw std::invalid_argument("IOP cache access requires a translated route.");
  }
  switch (region)
  {
    case IOPBusRegion::Unmapped:
    case IOPBusRegion::Ram:
    case IOPBusRegion::Scratchpad:
    case IOPBusRegion::Rom:
      break;
    default:
      throw std::invalid_argument("Unknown IOP physical region.");
  }

  const bool fetch = access == IOPCacheAccess::InstructionFetch;
  const IOPCacheBank bank = (fetch != control.swapped)
    ? IOPCacheBank::Instruction : IOPCacheBank::Data;
  const bool eligible = route == IOPCacheRoute::Cached &&
    (region == IOPBusRegion::Ram || region == IOPBusRegion::Rom);
  if ((!fetch && control.isolated) || (eligible && control.swapped))
  {
    return {IOPCacheAccessDisposition::UnsupportedMode, bank};
  }
  return {
    eligible ? IOPCacheAccessDisposition::Cache
             : IOPCacheAccessDisposition::Bypass,
    bank
  };
}
