#ifndef IOP_CACHE_HPP
#define IOP_CACHE_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "iop_bus.hpp"
#include "iop_types.hpp"

enum class IOPCacheBank : std::uint8_t
{
  Instruction,
  Data
};

enum class IOPCacheAccess : std::uint8_t
{
  InstructionFetch,
  DataLoad,
  DataStore
};

struct IOPCacheControl
{
  bool isolated = false;
  bool swapped = false;
};

enum class IOPCacheAccessDisposition : std::uint8_t
{
  Bypass,
  Cache,
  Isolated
};

struct IOPCacheAccessPlan
{
  IOPCacheAccessDisposition disposition =
    IOPCacheAccessDisposition::Bypass;
  IOPCacheBank bank = IOPCacheBank::Data;
};

struct IOPCacheLocation
{
  std::size_t lineIndex = 0;
  std::size_t wordIndex = 0;
  std::size_t byteOffset = 0;
  IOPAddress physicalTag = 0;
  IOPAddress lineBaseAddress = 0;
};

enum class IOPCacheProbe : std::uint8_t
{
  Hit,
  Miss
};

struct IOPCacheProbeResult
{
  IOPWord value = 0;
  IOPCacheProbe probe = IOPCacheProbe::Miss;
};

template<std::size_t WordCount>
struct IOPCacheLine
{
  std::array<IOPWord, WordCount> words = {};
  IOPAddress physicalTag = 0;
  bool valid = false;
};

using IOPInstructionCacheLine = IOPCacheLine<4>;
using IOPDataCacheLine = IOPCacheLine<1>;

class IOPCache final
{
  public:
    // Provisional retail geometry; see PROJECT.md's IOP cache contract.
    static constexpr std::size_t INSTRUCTION_SIZE = 4 * 1024;
    static constexpr std::size_t DATA_SIZE = 1024;
    static constexpr std::size_t INSTRUCTION_LINE_WORDS = 4;
    static constexpr std::size_t DATA_LINE_WORDS = 1;
    static constexpr std::size_t INSTRUCTION_LINE_COUNT =
      INSTRUCTION_SIZE / (INSTRUCTION_LINE_WORDS * sizeof(IOPWord));
    static constexpr std::size_t DATA_LINE_COUNT =
      DATA_SIZE / (DATA_LINE_WORDS * sizeof(IOPWord));

    void reset();
    const IOPInstructionCacheLine &instructionLine(std::size_t index) const;
    const IOPDataCacheLine &dataLine(std::size_t index) const;

    // The core admits aligned physical words before these boundaries.
    IOPBusReadResult readWord(
      IOPCacheBank bank,
      IOPBus &bus,
      IOPAddress physicalAddress);
    IOPCacheProbeResult readIsolatedWord(
      IOPCacheBank bank,
      IOPAddress physicalAddress) const;
    // Commit only after physical completion; value is already lane-positioned.
    void commitStore(
      IOPCacheBank bank,
      IOPAddress physicalAddress,
      IOPWord value,
      std::uint8_t byteEnable);
    // Indexed stores write one full word or invalidate on a partial mask.
    void commitIndexedStore(
      IOPCacheBank bank,
      IOPAddress physicalAddress,
      IOPWord value,
      std::uint8_t byteEnable);

    static IOPCacheLocation location(
      IOPCacheBank bank,
      IOPAddress physicalAddress);
    // Planning only: no memory access or guest exception selection.
    static IOPCacheAccessPlan planAccess(
      IOPCacheAccess access,
      IOPCacheControl control,
      IOPCacheRoute route,
      IOPBusRegion region);

  private:
    friend class NekoSaveStateCodec;
    friend struct IOPCacheTestAccess;

    std::array<IOPInstructionCacheLine, INSTRUCTION_LINE_COUNT>
      instructionLines = {};
    std::array<IOPDataCacheLine, DATA_LINE_COUNT> dataLines = {};
};

#endif
