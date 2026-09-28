#ifndef EE_MEMORY_SYSTEM_HPP
#define EE_MEMORY_SYSTEM_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "ee_cop0.hpp"

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

class EEMemorySystem final
{
  public:
    static constexpr std::size_t TLB_ENTRY_COUNT = 48;
    static constexpr std::size_t ITLB_ENTRY_COUNT = 2;
    static constexpr std::size_t DTLB_ENTRY_COUNT = 4;

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
      const EEAddressTranslationContext &context);
    EEAddressTranslationResult translateDataAddress(
      std::uint32_t virtualAddress,
      EEDataAccessDirection direction,
      const EEAddressTranslationContext &context);
    void reset();
    std::uint32_t cop0Register(
      EECOP0Register registerIndex) const;
    void setCOP0Register(
      EECOP0Register registerIndex,
      std::uint32_t value);
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

  private:
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
      bool instruction);
    std::size_t matchingTLBEntry(
      std::uint32_t virtualAddress,
      bool instruction);
    void invalidateTLBAccelerators();
    EETLBEntry currentTLBEntry() const;
    void writeTLBEntry(std::uint32_t index);

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
    std::array<TLBAcceleratorEntry, ITLB_ENTRY_COUNT> itlb = {};
    std::array<TLBAcceleratorEntry, DTLB_ENTRY_COUNT> dtlb = {};
    std::size_t nextITLBReplacement = 0;
    std::size_t nextDTLBReplacement = 0;
};

#endif
