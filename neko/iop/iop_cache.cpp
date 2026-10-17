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

namespace
{
  void validateStore(IOPAddress physicalAddress, std::uint8_t byteEnable)
  {
    if ((physicalAddress & (sizeof(IOPWord) - 1)) != 0 ||
        byteEnable == 0 || (byteEnable & 0xf0) != 0)
    {
      throw std::invalid_argument(
        "IOP cache store requires an aligned word and nonzero four-bit byte enable.");
    }
  }

  template<std::size_t WordCount>
  IOPCacheProbeResult probeLine(
    const IOPCacheLocation &address,
    const IOPCacheLine<WordCount> &line)
  {
    return {line.words[address.wordIndex],
      line.valid && line.physicalTag == address.physicalTag
        ? IOPCacheProbe::Hit : IOPCacheProbe::Miss};
  }

  template<std::size_t WordCount>
  void storeIndexedLine(
    const IOPCacheLocation &address,
    IOPCacheLine<WordCount> &line,
    IOPWord value,
    std::uint8_t byteEnable)
  {
    if (byteEnable == 0xf)
    {
      line.words[address.wordIndex] = value;
      line.physicalTag = address.physicalTag;
      line.valid = true;
    }
    else
    {
      line.valid = false;
    }
  }

  template<std::size_t WordCount>
  IOPBusReadResult readLine(
    IOPBus &bus,
    IOPAddress physicalAddress,
    const IOPCacheLocation &address,
    IOPCacheLine<WordCount> &line)
  {
    if ((physicalAddress & (sizeof(IOPWord) - 1)) != 0)
    {
      return {IOPBusStatus::Misaligned, 0};
    }
    if (line.valid && line.physicalTag == address.physicalTag)
    {
      return {IOPBusStatus::Completed, line.words[address.wordIndex]};
    }

    line.valid = false;
    IOPCacheLine<WordCount> candidate;
    candidate.physicalTag = address.physicalTag;
    for (std::size_t word = 0; word < candidate.words.size(); ++word)
    {
      const auto read = bus.read32(address.lineBaseAddress +
        static_cast<IOPAddress>(word * sizeof(IOPWord)));
      if (read.status != IOPBusStatus::Completed)
      {
        return read;
      }
      candidate.words[word] = read.value;
    }
    candidate.valid = true;
    line = candidate;
    return {IOPBusStatus::Completed, line.words[address.wordIndex]};
  }
}

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

IOPBusReadResult IOPCache::readWord(
  IOPCacheBank bank,
  IOPBus &bus,
  IOPAddress physicalAddress)
{
  const auto address = location(bank, physicalAddress);
  if (bank == IOPCacheBank::Instruction)
  {
    return readLine(bus, physicalAddress, address, instructionLines[address.lineIndex]);
  }
  return readLine(bus, physicalAddress, address, dataLines[address.lineIndex]);
}

IOPCacheProbeResult IOPCache::readIsolatedWord(
  IOPCacheBank bank,
  IOPAddress physicalAddress) const
{
  if ((physicalAddress & (sizeof(IOPWord) - 1)) != 0)
  {
    throw std::invalid_argument("IOP cache probe requires an aligned word.");
  }
  const auto address = location(bank, physicalAddress);
  if (bank == IOPCacheBank::Instruction)
  {
    return probeLine(address, instructionLines[address.lineIndex]);
  }
  return probeLine(address, dataLines[address.lineIndex]);
}

void IOPCache::commitIndexedStore(
  IOPCacheBank bank,
  IOPAddress physicalAddress,
  IOPWord value,
  std::uint8_t byteEnable)
{
  validateStore(physicalAddress, byteEnable);
  const auto address = location(bank, physicalAddress);
  if (bank == IOPCacheBank::Instruction)
  {
    storeIndexedLine(address, instructionLines[address.lineIndex], value, byteEnable);
  }
  else
  {
    storeIndexedLine(address, dataLines[address.lineIndex], value, byteEnable);
  }
}

void IOPCache::commitStore(
  IOPCacheBank bank,
  IOPAddress physicalAddress,
  IOPWord value,
  std::uint8_t byteEnable)
{
  if (bank == IOPCacheBank::Instruction)
  {
    commitIndexedStore(bank, physicalAddress, value, byteEnable);
    return;
  }
  validateStore(physicalAddress, byteEnable);
  const auto address = location(bank, physicalAddress);
  auto &line = dataLines[address.lineIndex];
  if (byteEnable == 0xf)
  {
    line.words[0] = value;
    line.physicalTag = address.physicalTag;
    line.valid = true;
    return;
  }
  if (!line.valid || line.physicalTag != address.physicalTag)
  {
    return;
  }
  IOPWord mask = 0;
  for (unsigned lane = 0; lane < sizeof(IOPWord); ++lane)
  {
    if ((byteEnable & (1u << lane)) != 0)
    {
      mask |= IOPWord(0xff) << (8 * lane);
    }
  }
  line.words[0] = (line.words[0] & ~mask) | (value & mask);
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
  if (!fetch && control.isolated)
  {
    return {IOPCacheAccessDisposition::Isolated, bank};
  }
  return {
    eligible ? IOPCacheAccessDisposition::Cache
             : IOPCacheAccessDisposition::Bypass,
    bank
  };
}
