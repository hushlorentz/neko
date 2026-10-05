#ifndef EE_MEMORY_SYSTEM_HPP
#define EE_MEMORY_SYSTEM_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "ee_cop0.hpp"
#include "ee_types.hpp"

class EEBus;
struct EEMemorySystemTestAccess;

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
  static constexpr std::uint32_t PHYSICAL_TAG_MASK =
    UINT32_C(0xfffff000);

  std::array<std::uint8_t, DATA_SIZE> data = {};
  std::uint32_t physicalTag = 0;
  bool valid = false;
  bool dirty = false;
  bool leastRecentlyFilled = false;
  bool locked = false;
};

enum class EECacheLineTransferOutcome : std::uint8_t
{
  Completed,
  PhysicalBusError,
  InvalidLineState
};

struct EECacheLineTransferResult
{
  EECacheLineTransferOutcome outcome =
    EECacheLineTransferOutcome::PhysicalBusError;
  std::uint32_t lineBaseAddress = 0;
  std::uint8_t firstQuadword = 0;
  std::uint8_t quadwordsTransferred = 0;
};

struct EECacheLineFillResult
{
  EECacheLineTransferOutcome outcome =
    EECacheLineTransferOutcome::PhysicalBusError;
  EECacheLine line;
  std::uint32_t lineBaseAddress = 0;
  std::uint8_t firstQuadword = 0;
  std::uint8_t quadwordsTransferred = 0;
};

enum class EEInstructionCacheFetchOutcome : std::uint8_t
{
  Completed,
  PhysicalBusError
};

enum class EEInstructionCacheFetchSource : std::uint8_t
{
  Bypassed,
  Hit,
  Refilled
};

struct EEInstructionCacheFetchResult
{
  EEInstructionCacheFetchOutcome outcome =
    EEInstructionCacheFetchOutcome::PhysicalBusError;
  EEInstructionCacheFetchSource source =
    EEInstructionCacheFetchSource::Bypassed;
  std::uint32_t instruction = 0;
  std::uint8_t set = 0xff;
  std::uint8_t way = 0xff;
};

enum class EEDataCacheLoadOutcome : std::uint8_t
{
  Completed,
  PhysicalBusError
};

enum class EEDataCacheLoadSource : std::uint8_t
{
  Bypassed,
  Hit,
  Refilled
};

struct EEDataCacheLoadResult
{
  EEDataCacheLoadOutcome outcome =
    EEDataCacheLoadOutcome::PhysicalBusError;
  EEDataCacheLoadSource source =
    EEDataCacheLoadSource::Bypassed;
  std::array<std::uint8_t, 16> data = {};
  std::uint8_t width = 0;
  std::uint8_t set = 0xff;
  std::uint8_t way = 0xff;
};

enum class EEDataCacheStoreOutcome : std::uint8_t
{
  Completed,
  Stalled,
  PhysicalBusError
};

enum class EEDataCacheStoreSource : std::uint8_t
{
  Bypassed,
  Hit,
  Allocated
};

struct EEDataCacheStoreResult
{
  EEDataCacheStoreOutcome outcome =
    EEDataCacheStoreOutcome::PhysicalBusError;
  EEDataCacheStoreSource source =
    EEDataCacheStoreSource::Bypassed;
  bool evictedDirty = false;
  std::uint8_t set = 0xff;
  std::uint8_t way = 0xff;
};

enum class EECacheOperation : std::uint8_t
{
  InstructionIndexLoadTag = 0x00,
  InstructionIndexLoadData = 0x01,
  InstructionIndexStoreTag = 0x04,
  InstructionIndexStoreData = 0x05,
  InstructionIndexInvalidate = 0x07,
  InstructionHitInvalidate = 0x0b,
  InstructionFill = 0x0e,
  DataIndexLoadTag = 0x10,
  DataIndexLoadData = 0x11,
  DataIndexStoreTag = 0x12,
  DataIndexStoreData = 0x13,
  DataIndexWriteBackInvalidate = 0x14,
  DataIndexInvalidate = 0x16,
  DataHitWriteBackInvalidate = 0x18,
  DataHitInvalidate = 0x1a,
  DataHitWriteBack = 0x1c
};

struct EECacheMaintenanceRequest
{
  EECacheOperation operation =
    EECacheOperation::InstructionIndexLoadTag;
  std::uint32_t virtualAddress = 0;
  EEAddressTranslationContext translationContext;
};

enum class EECacheMaintenanceOutcome : std::uint8_t
{
  Completed,
  AddressTranslationFailure,
  PhysicalBusError,
  UnsupportedOperation
};

struct EECacheMaintenanceResult
{
  EECacheMaintenanceOutcome outcome =
    EECacheMaintenanceOutcome::UnsupportedOperation;
  EEAddressTranslationResult translation;
  bool cacheHitStatusValid = false;
  bool cacheHit = false;
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
    friend struct EEMemorySystemTestAccess;

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
    static std::array<std::uint8_t, 4> cacheLineRefillOrder(
      std::uint32_t physicalAddress);
    EECacheLineFillResult fillCacheLine(
      const EEBus &bus,
      std::uint32_t physicalAddress) const;
    EECacheLineTransferResult writeBackDataCacheLine(
      EEBus *bus,
      std::size_t set,
      const EECacheLine &line) const;
    EEInstructionCacheFetchResult fetchInstruction(
      const EEBus &bus,
      const EEAddressTranslationResult &translation);
    EEDataCacheLoadResult loadData(
      EEBus &bus,
      const EEAddressTranslationResult &translation,
      std::size_t width);
    EEDataCacheStoreResult storeData(
      EEBus *bus,
      const EEAddressTranslationResult &translation,
      const std::array<std::uint8_t, 16> &data,
      std::size_t width);
    EECacheMaintenanceResult maintainCache(
      EEBus *bus,
      const EECacheMaintenanceRequest &request);
    EECacheMaintenanceResult maintainInstructionCacheIndex(
      const EECacheMaintenanceRequest &request);
    EECacheMaintenanceResult maintainInstructionCacheAddressed(
      EEBus *bus,
      const EECacheMaintenanceRequest &request);
    EECacheMaintenanceResult maintainDataCacheIndex(
      EEBus *bus,
      const EECacheMaintenanceRequest &request);
    std::size_t instructionCacheVictim(std::size_t set) const;
    bool dataCacheVictim(
      std::size_t set,
      std::size_t *way) const;

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
