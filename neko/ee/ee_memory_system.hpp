#ifndef EE_MEMORY_SYSTEM_HPP
#define EE_MEMORY_SYSTEM_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "ee_cop0.hpp"
#include "ee_types.hpp"

struct EETLBPage
{
  std::uint32_t value = 0;

  bool valid() const;
  bool dirty() const;
  bool scratchpad() const;
};

bool operator==(const EETLBPage &left, const EETLBPage &right);

struct EETLBEntry
{
  std::uint32_t pageMask = EECOP0PageMask::SIZE_4_KIB;
  std::uint32_t entryHi = 0;
  EETLBPage evenPage;
  EETLBPage oddPage;

  bool global() const;
  bool matches(
    std::uint32_t virtualAddress,
    std::uint8_t asid) const;
  const EETLBPage &pageForAddress(
    std::uint32_t virtualAddress) const;
};

bool operator==(const EETLBEntry &left, const EETLBEntry &right);

enum class EEPrivilegeMode : std::uint8_t
{
  Kernel,
  Supervisor,
  User
};

struct EEAddressTranslationContext
{
  EEPrivilegeMode privilege = EEPrivilegeMode::Kernel;
  bool exceptionLevel = false;
  bool errorLevel = false;
};

enum class EEDataAccessDirection : std::uint8_t
{
  Load,
  Store
};

enum class EEAddressTranslationOutcome : std::uint8_t
{
  Translated,
  TLBLookup,
  AddressErrorLoadOrFetch,
  AddressErrorStore,
  TLBRefillLoadOrFetch,
  TLBRefillStore,
  TLBInvalidLoadOrFetch,
  TLBInvalidStore,
  TLBModified,
  UnsupportedScratchpadInstruction,
  UnsupportedScratchpadPageSize,
  UnsupportedCacheAttribute
};

enum class EECacheRoute : std::uint8_t
{
  Uncached,
  CachedNoncoherent,
  UncachedAccelerated,
  TLBSelected,
  Unsupported
};

enum class EEAddressRoute : std::uint8_t
{
  MainBus,
  Scratchpad
};

enum class EEScratchpadAccessClient : std::uint8_t
{
  CPU,
  DMAC
};

enum class EEScratchpadAccessDecision : std::uint8_t
{
  Granted,
  Wait
};

enum class EEScratchpadAccessResult : std::uint8_t
{
  Completed,
  Wait,
  InvalidAddress
};

class EEScratchpadAccessPolicy final
{
  public:
    constexpr EEScratchpadAccessDecision decision(
      EEScratchpadAccessClient) const
    {
      return EEScratchpadAccessDecision::Granted;
    }
};

struct EEAddressTranslationResult
{
  EEAddressTranslationOutcome outcome =
    EEAddressTranslationOutcome::TLBLookup;
  std::uint32_t virtualAddress = 0;
  std::uint32_t physicalAddress = 0;
  EECacheRoute cacheRoute = EECacheRoute::TLBSelected;
  std::uint8_t cacheAttribute = 0;
  EEAddressRoute route = EEAddressRoute::MainBus;
  std::uint8_t tlbIndex = 0xff;
};

struct EECacheLine
{
  static constexpr std::size_t DATA_SIZE = 64;

  std::array<std::uint8_t, DATA_SIZE> data = {};
  std::uint32_t physicalTag = 0;
  bool valid = false;
  bool dirty = false;
  bool leastRecentlyFilled = false;
  bool locked = false;
};

class EEMemorySystem final
{
  public:
    static constexpr std::size_t TLB_ENTRY_COUNT = 48;
    static constexpr std::size_t ITLB_ENTRY_COUNT = 2;
    static constexpr std::size_t DTLB_ENTRY_COUNT = 4;
    static constexpr std::size_t CACHE_LINE_SIZE =
      EECacheLine::DATA_SIZE;
    static constexpr std::size_t CACHE_WAY_COUNT = 2;
    static constexpr std::size_t INSTRUCTION_CACHE_SET_COUNT = 128;
    static constexpr std::size_t DATA_CACHE_SET_COUNT = 64;
    static constexpr std::size_t SCRATCHPAD_SIZE = 16 * 1024;
    static constexpr std::size_t SCRATCHPAD_QWORD_COUNT =
      SCRATCHPAD_SIZE / 16;

    static EECacheRoute cacheRoute(std::uint8_t attribute);
    EEAddressTranslationResult classifyInstructionAddress(
      std::uint32_t virtualAddress,
      const EEAddressTranslationContext &context) const;
    EEAddressTranslationResult classifyDataAddress(
      std::uint32_t virtualAddress,
      EEDataAccessDirection direction,
      const EEAddressTranslationContext &context) const;
    EEAddressTranslationResult translateInstructionAddress(
      std::uint32_t virtualAddress,
      const EEAddressTranslationContext &context) const;
    EEAddressTranslationResult translateDataAddress(
      std::uint32_t virtualAddress,
      EEDataAccessDirection direction,
      const EEAddressTranslationContext &context) const;
    bool readScratchpad8(
      std::uint32_t offset,
      std::uint8_t *value) const;
    bool writeScratchpad8(
      std::uint32_t offset,
      std::uint8_t value);
    bool readScratchpad16(
      std::uint32_t offset,
      std::uint16_t *value) const;
    bool writeScratchpad16(
      std::uint32_t offset,
      std::uint16_t value);
    bool readScratchpad32(
      std::uint32_t offset,
      std::uint32_t *value) const;
    bool writeScratchpad32(
      std::uint32_t offset,
      std::uint32_t value);
    bool readScratchpad64(
      std::uint32_t offset,
      std::uint64_t *value) const;
    bool writeScratchpad64(
      std::uint32_t offset,
      std::uint64_t value);
    bool readScratchpad128(
      std::uint32_t offset,
      EEQuadword *value) const;
    bool writeScratchpad128(
      std::uint32_t offset,
      const EEQuadword &value);
    EEScratchpadAccessResult readScratchpadDMA128(
      std::uint32_t physicalAddress,
      EEQuadword *value) const;
    EEScratchpadAccessResult writeScratchpadDMA128(
      std::uint32_t physicalAddress,
      const EEQuadword &value);
    void reset();
    std::uint32_t cop0Register(
      EECOP0Register registerIndex) const;
    void setCOP0Register(
      EECOP0Register registerIndex,
      std::uint32_t value);
    void commitTLBExceptionAddress(std::uint32_t virtualAddress);
    EECOP0WriteResult writeCOP0Register(
      EECOP0Register registerIndex,
      std::uint32_t value);
    const EETLBEntry &tlbEntry(std::size_t index) const;
    void setTLBEntry(
      std::size_t index,
      const EETLBEntry &entry);
    void readIndexedTLBEntry();
    void writeIndexedTLBEntry();
    void writeRandomTLBEntry();
    void probeTLB();
    void retireInstruction();
    bool replacementStateValid() const;
    const EECacheLine &instructionCacheLine(
      std::size_t set,
      std::size_t way) const;
    const EECacheLine &dataCacheLine(
      std::size_t set,
      std::size_t way) const;

  private:
    friend class EECore;
    friend class NekoSaveStateCodec;

    struct TLBAcceleratorEntry
    {
      bool valid = false;
      std::uint8_t tlbIndex = 0;
    };

    static EEAddressTranslationResult classifyAddress(
      std::uint32_t virtualAddress,
      bool store,
      const EEAddressTranslationContext &context);
    EEAddressTranslationResult translateMappedAddress(
      std::uint32_t virtualAddress,
      bool store,
      bool instruction) const;
    std::size_t matchingTLBEntry(
      std::uint32_t virtualAddress,
      bool instruction) const;
    void invalidateTLBAccelerators();
    EETLBEntry currentTLBEntry() const;
    void writeTLBEntry(std::uint32_t index);
    bool scratchpadAccessGranted(
      EEScratchpadAccessClient client) const;

    std::uint32_t cop0Index = 0;
    std::uint32_t cop0Random = EECOP0Random::RESET;
    std::uint32_t cop0EntryLo0 = 0;
    std::uint32_t cop0EntryLo1 = 0;
    std::uint32_t cop0Context = 0;
    std::uint32_t cop0PageMask = EECOP0PageMask::SIZE_4_KIB;
    std::uint32_t cop0Wired = 0;
    std::uint32_t cop0EntryHi = 0;
    std::uint32_t cop0Config = EECOP0Config::RESET;
    std::uint32_t cop0TagLo = 0;
    std::uint32_t cop0TagHi = 0;
    std::array<EETLBEntry, TLB_ENTRY_COUNT> tlbEntries = {};
    std::array<
      std::array<EECacheLine, CACHE_WAY_COUNT>,
      INSTRUCTION_CACHE_SET_COUNT> instructionCache = {};
    std::array<
      std::array<EECacheLine, CACHE_WAY_COUNT>,
      DATA_CACHE_SET_COUNT> dataCache = {};
    std::array<EEQuadword, SCRATCHPAD_QWORD_COUNT> scratchpad = {};
    EEScratchpadAccessPolicy scratchpadAccessPolicy;
    mutable std::array<TLBAcceleratorEntry, ITLB_ENTRY_COUNT> itlb = {};
    mutable std::array<TLBAcceleratorEntry, DTLB_ENTRY_COUNT> dtlb = {};
    mutable std::size_t nextITLBReplacement = 0;
    mutable std::size_t nextDTLBReplacement = 0;
};

#endif
