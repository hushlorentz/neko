#include "ee_memory_system.hpp"

#include <stdexcept>

#include "ee_bus.hpp"

namespace
{
  bool supportedPageMask(std::uint32_t value)
  {
    switch (value)
    {
      case EECOP0PageMask::SIZE_4_KIB:
      case EECOP0PageMask::SIZE_16_KIB:
      case EECOP0PageMask::SIZE_64_KIB:
      case EECOP0PageMask::SIZE_256_KIB:
      case EECOP0PageMask::SIZE_1_MIB:
      case EECOP0PageMask::SIZE_4_MIB:
      case EECOP0PageMask::SIZE_16_MIB:
        return true;
    }
    return false;
  }

  EETLBEntry canonicalTLBEntry(
    std::uint32_t pageMask,
    std::uint32_t entryHi,
    std::uint32_t entryLo0,
    std::uint32_t entryLo1)
  {
    const std::uint32_t canonicalPageMask =
      pageMask & EECOP0PageMask::IMPLEMENTED_MASK;
    if (!supportedPageMask(canonicalPageMask))
    {
      throw std::invalid_argument(
        "EE TLB PageMask encoding is unsupported.");
    }
    const std::uint32_t physicalFrameIgnoredMask =
      canonicalPageMask >> 7;
    std::uint32_t evenPage =
      entryLo0 &
      EECOP0EntryLo::ENTRY_LO_0_IMPLEMENTED_MASK &
      ~physicalFrameIgnoredMask;
    std::uint32_t oddPage =
      entryLo1 &
      EECOP0EntryLo::ENTRY_LO_1_IMPLEMENTED_MASK &
      ~physicalFrameIgnoredMask;
    const bool global =
      (evenPage & EECOP0EntryLo::GLOBAL) != 0 &&
      (oddPage & EECOP0EntryLo::GLOBAL) != 0;
    evenPage &= ~EECOP0EntryLo::GLOBAL;
    oddPage &= ~EECOP0EntryLo::GLOBAL;
    if (global)
    {
      evenPage |= EECOP0EntryLo::GLOBAL;
      oddPage |= EECOP0EntryLo::GLOBAL;
    }
    return {
      canonicalPageMask,
      entryHi &
        EECOP0EntryHi::IMPLEMENTED_MASK &
        ~canonicalPageMask,
      {evenPage},
      {oddPage}
    };
  }

  bool scratchpadRange(
    std::uint32_t offset,
    std::size_t width)
  {
    return
      offset < EEMemorySystem::SCRATCHPAD_SIZE &&
      width <= EEMemorySystem::SCRATCHPAD_SIZE - offset;
  }

  std::uint8_t scratchpadByte(
    const std::array<
      EEQuadword,
      EEMemorySystem::SCRATCHPAD_QWORD_COUNT> &scratchpad,
    std::uint32_t offset)
  {
    const EEQuadword &quadword = scratchpad[offset / 16];
    const std::size_t byteIndex = offset % 16;
    const std::uint64_t half =
      byteIndex < 8 ? quadword.low : quadword.high;
    return static_cast<std::uint8_t>(
      half >> ((byteIndex % 8) * 8));
  }

  void setScratchpadByte(
    std::array<
      EEQuadword,
      EEMemorySystem::SCRATCHPAD_QWORD_COUNT> *scratchpad,
    std::uint32_t offset,
    std::uint8_t value)
  {
    EEQuadword &quadword = (*scratchpad)[offset / 16];
    const std::size_t byteIndex = offset % 16;
    std::uint64_t &half =
      byteIndex < 8 ? quadword.low : quadword.high;
    const std::size_t shift = (byteIndex % 8) * 8;
    half =
      (half & ~(UINT64_C(0xff) << shift)) |
      (static_cast<std::uint64_t>(value) << shift);
  }

  template<typename Value>
  bool readScratchpadValue(
    const std::array<
      EEQuadword,
      EEMemorySystem::SCRATCHPAD_QWORD_COUNT> &scratchpad,
    std::uint32_t offset,
    Value *value)
  {
    if (value == nullptr)
    {
      throw std::invalid_argument(
        "EE scratchpad load requires an output value.");
    }
    if (!scratchpadRange(offset, sizeof(Value)))
    {
      return false;
    }
    Value result = 0;
    for (std::size_t index = 0; index < sizeof(Value); ++index)
    {
      result |=
        static_cast<Value>(
          scratchpadByte(scratchpad, offset + index)) <<
        (index * 8);
    }
    *value = result;
    return true;
  }

  template<typename Value>
  bool writeScratchpadValue(
    std::array<
      EEQuadword,
      EEMemorySystem::SCRATCHPAD_QWORD_COUNT> *scratchpad,
    std::uint32_t offset,
    Value value)
  {
    if (!scratchpadRange(offset, sizeof(Value)))
    {
      return false;
    }
    for (std::size_t index = 0; index < sizeof(Value); ++index)
    {
      setScratchpadByte(
        scratchpad,
        offset + index,
        static_cast<std::uint8_t>(value >> (index * 8)));
    }
    return true;
  }

  void storeCacheQuadword(
    EECacheLine *line,
    std::uint8_t quadwordIndex,
    const EEQuadword &value)
  {
    const std::size_t offset = quadwordIndex * 16;
    for (std::size_t index = 0; index < 8; ++index)
    {
      line->data[offset + index] =
        static_cast<std::uint8_t>(value.low >> (index * 8));
      line->data[offset + 8 + index] =
        static_cast<std::uint8_t>(value.high >> (index * 8));
    }
  }

  EEQuadword loadCacheQuadword(
    const EECacheLine &line,
    std::uint8_t quadwordIndex)
  {
    const std::size_t offset = quadwordIndex * 16;
    EEQuadword value = {};
    for (std::size_t index = 0; index < 8; ++index)
    {
      value.low |=
        static_cast<std::uint64_t>(
          line.data[offset + index]) <<
        (index * 8);
      value.high |=
        static_cast<std::uint64_t>(
          line.data[offset + 8 + index]) <<
        (index * 8);
    }
    return value;
  }
}

bool EETLBPage::valid() const
{
  return (value & EECOP0EntryLo::VALID) != 0;
}

bool EETLBPage::dirty() const
{
  return (value & EECOP0EntryLo::DIRTY) != 0;
}

bool EETLBPage::scratchpad() const
{
  return (value & EECOP0EntryLo::SCRATCHPAD) != 0;
}

bool operator==(const EETLBPage &left, const EETLBPage &right)
{
  return left.value == right.value;
}

bool EETLBEntry::global() const
{
  return
    (evenPage.value & EECOP0EntryLo::GLOBAL) != 0 &&
    (oddPage.value & EECOP0EntryLo::GLOBAL) != 0;
}

bool EETLBEntry::matches(
  std::uint32_t virtualAddress,
  std::uint8_t asid) const
{
  const std::uint32_t virtualPageComparisonMask =
    ~(pageMask | UINT32_C(0x1fff));
  return
    (virtualAddress & virtualPageComparisonMask) ==
      (entryHi & virtualPageComparisonMask) &&
    (global() ||
     (entryHi & EECOP0EntryHi::ASID_MASK) == asid);
}

const EETLBPage &EETLBEntry::pageForAddress(
  std::uint32_t virtualAddress) const
{
  const std::uint32_t oddPageBit =
    (pageMask + UINT32_C(0x2000)) >> 1;
  return (virtualAddress & oddPageBit) == 0
    ? evenPage
    : oddPage;
}

bool operator==(const EETLBEntry &left, const EETLBEntry &right)
{
  return
    left.pageMask == right.pageMask &&
    left.entryHi == right.entryHi &&
    left.evenPage == right.evenPage &&
    left.oddPage == right.oddPage;
}

EECacheRoute EEMemorySystem::cacheRoute(std::uint8_t attribute)
{
  switch (attribute)
  {
    case 2:
      return EECacheRoute::Uncached;
    case 3:
      return EECacheRoute::CachedNoncoherent;
    case 7:
      return EECacheRoute::UncachedAccelerated;
    default:
      return EECacheRoute::Unsupported;
  }
}

EEAddressTranslationResult
EEMemorySystem::classifyInstructionAddress(
  std::uint32_t virtualAddress,
  const EEAddressTranslationContext &context) const
{
  return classifyAddress(virtualAddress, false, context);
}

EEAddressTranslationResult EEMemorySystem::classifyDataAddress(
  std::uint32_t virtualAddress,
  EEDataAccessDirection direction,
  const EEAddressTranslationContext &context) const
{
  return classifyAddress(
    virtualAddress,
    direction == EEDataAccessDirection::Store,
    context);
}

EEAddressTranslationResult
EEMemorySystem::translateInstructionAddress(
  std::uint32_t virtualAddress,
  const EEAddressTranslationContext &context) const
{
  const EEAddressTranslationResult classification =
    classifyInstructionAddress(virtualAddress, context);
  return classification.outcome == EEAddressTranslationOutcome::TLBLookup
    ? translateMappedAddress(virtualAddress, false, true)
    : classification;
}

EEAddressTranslationResult EEMemorySystem::translateDataAddress(
  std::uint32_t virtualAddress,
  EEDataAccessDirection direction,
  const EEAddressTranslationContext &context) const
{
  const bool store = direction == EEDataAccessDirection::Store;
  const EEAddressTranslationResult classification =
    classifyDataAddress(virtualAddress, direction, context);
  return classification.outcome == EEAddressTranslationOutcome::TLBLookup
    ? translateMappedAddress(virtualAddress, store, false)
    : classification;
}

bool EEMemorySystem::readScratchpad8(
  std::uint32_t offset,
  std::uint8_t *value) const
{
  if (!scratchpadAccessGranted(EEScratchpadAccessClient::CPU))
  {
    return false;
  }
  return readScratchpadValue(scratchpad, offset, value);
}

bool EEMemorySystem::writeScratchpad8(
  std::uint32_t offset,
  std::uint8_t value)
{
  if (!scratchpadAccessGranted(EEScratchpadAccessClient::CPU))
  {
    return false;
  }
  return writeScratchpadValue(&scratchpad, offset, value);
}

bool EEMemorySystem::readScratchpad16(
  std::uint32_t offset,
  std::uint16_t *value) const
{
  if (!scratchpadAccessGranted(EEScratchpadAccessClient::CPU))
  {
    return false;
  }
  return readScratchpadValue(scratchpad, offset, value);
}

bool EEMemorySystem::writeScratchpad16(
  std::uint32_t offset,
  std::uint16_t value)
{
  if (!scratchpadAccessGranted(EEScratchpadAccessClient::CPU))
  {
    return false;
  }
  return writeScratchpadValue(&scratchpad, offset, value);
}

bool EEMemorySystem::readScratchpad32(
  std::uint32_t offset,
  std::uint32_t *value) const
{
  if (!scratchpadAccessGranted(EEScratchpadAccessClient::CPU))
  {
    return false;
  }
  return readScratchpadValue(scratchpad, offset, value);
}

bool EEMemorySystem::writeScratchpad32(
  std::uint32_t offset,
  std::uint32_t value)
{
  if (!scratchpadAccessGranted(EEScratchpadAccessClient::CPU))
  {
    return false;
  }
  return writeScratchpadValue(&scratchpad, offset, value);
}

bool EEMemorySystem::readScratchpad64(
  std::uint32_t offset,
  std::uint64_t *value) const
{
  if (!scratchpadAccessGranted(EEScratchpadAccessClient::CPU))
  {
    return false;
  }
  return readScratchpadValue(scratchpad, offset, value);
}

bool EEMemorySystem::writeScratchpad64(
  std::uint32_t offset,
  std::uint64_t value)
{
  if (!scratchpadAccessGranted(EEScratchpadAccessClient::CPU))
  {
    return false;
  }
  return writeScratchpadValue(&scratchpad, offset, value);
}

bool EEMemorySystem::readScratchpad128(
  std::uint32_t offset,
  EEQuadword *value) const
{
  if (value == nullptr)
  {
    throw std::invalid_argument(
      "EE scratchpad quadword load requires an output value.");
  }
  if (!scratchpadAccessGranted(EEScratchpadAccessClient::CPU))
  {
    return false;
  }
  if (!scratchpadRange(offset, 16))
  {
    return false;
  }
  EEQuadword result = {};
  readScratchpadValue(scratchpad, offset, &result.low);
  readScratchpadValue(scratchpad, offset + 8, &result.high);
  *value = result;
  return true;
}

bool EEMemorySystem::writeScratchpad128(
  std::uint32_t offset,
  const EEQuadword &value)
{
  if (!scratchpadAccessGranted(EEScratchpadAccessClient::CPU))
  {
    return false;
  }
  if (!scratchpadRange(offset, 16))
  {
    return false;
  }
  writeScratchpadValue(&scratchpad, offset, value.low);
  writeScratchpadValue(&scratchpad, offset + 8, value.high);
  return true;
}

EEScratchpadAccessResult EEMemorySystem::readScratchpadDMA128(
  std::uint32_t physicalAddress,
  EEQuadword *value) const
{
  if (value == nullptr)
  {
    throw std::invalid_argument(
      "EE DMAC scratchpad load requires an output value.");
  }
  if ((physicalAddress & 0x0f) != 0 ||
      !scratchpadRange(physicalAddress, 16))
  {
    return EEScratchpadAccessResult::InvalidAddress;
  }
  if (!scratchpadAccessGranted(EEScratchpadAccessClient::DMAC))
  {
    return EEScratchpadAccessResult::Wait;
  }
  EEQuadword result = {};
  readScratchpadValue(scratchpad, physicalAddress, &result.low);
  readScratchpadValue(
    scratchpad,
    physicalAddress + 8,
    &result.high);
  *value = result;
  return EEScratchpadAccessResult::Completed;
}

EEScratchpadAccessResult EEMemorySystem::writeScratchpadDMA128(
  std::uint32_t physicalAddress,
  const EEQuadword &value)
{
  if ((physicalAddress & 0x0f) != 0 ||
      !scratchpadRange(physicalAddress, 16))
  {
    return EEScratchpadAccessResult::InvalidAddress;
  }
  if (!scratchpadAccessGranted(EEScratchpadAccessClient::DMAC))
  {
    return EEScratchpadAccessResult::Wait;
  }
  writeScratchpadValue(&scratchpad, physicalAddress, value.low);
  writeScratchpadValue(
    &scratchpad,
    physicalAddress + 8,
    value.high);
  return EEScratchpadAccessResult::Completed;
}

bool EEMemorySystem::scratchpadAccessGranted(
  EEScratchpadAccessClient client) const
{
  return
    scratchpadAccessPolicy.decision(client) ==
    EEScratchpadAccessDecision::Granted;
}

EEAddressTranslationResult EEMemorySystem::classifyAddress(
  std::uint32_t virtualAddress,
  bool store,
  const EEAddressTranslationContext &context)
{
  const EEPrivilegeMode privilege =
    context.exceptionLevel || context.errorLevel
      ? EEPrivilegeMode::Kernel
      : context.privilege;
  const auto addressError = [&]() {
    return EEAddressTranslationResult{
      store
        ? EEAddressTranslationOutcome::AddressErrorStore
        : EEAddressTranslationOutcome::AddressErrorLoadOrFetch,
      virtualAddress,
      0,
      EECacheRoute::Unsupported,
      0,
      EEAddressRoute::MainBus,
      0xff
    };
  };
  const auto tlbLookup = [&]() {
    return EEAddressTranslationResult{
      EEAddressTranslationOutcome::TLBLookup,
      virtualAddress,
      0,
      EECacheRoute::TLBSelected,
      0,
      EEAddressRoute::MainBus,
      0xff
    };
  };

  if (virtualAddress < UINT32_C(0x80000000))
  {
    if (context.errorLevel)
    {
      return {
        EEAddressTranslationOutcome::Translated,
        virtualAddress,
        virtualAddress,
        EECacheRoute::Uncached,
        2,
        EEAddressRoute::MainBus,
        0xff
      };
    }
    return tlbLookup();
  }
  if (virtualAddress < UINT32_C(0xc0000000))
  {
    if (privilege != EEPrivilegeMode::Kernel)
    {
      return addressError();
    }
    const bool cached =
      virtualAddress < UINT32_C(0xa0000000);
    return {
      EEAddressTranslationOutcome::Translated,
      virtualAddress,
      virtualAddress & UINT32_C(0x1fffffff),
      cached
        ? EECacheRoute::CachedNoncoherent
        : EECacheRoute::Uncached,
      static_cast<std::uint8_t>(cached ? 3 : 2),
      EEAddressRoute::MainBus,
      0xff
    };
  }
  if (virtualAddress < UINT32_C(0xe0000000))
  {
    return privilege == EEPrivilegeMode::User
      ? addressError()
      : tlbLookup();
  }
  return privilege == EEPrivilegeMode::Kernel
    ? tlbLookup()
    : addressError();
}

EEAddressTranslationResult EEMemorySystem::translateMappedAddress(
  std::uint32_t virtualAddress,
  bool store,
  bool instruction) const
{
  const std::size_t index =
    matchingTLBEntry(virtualAddress, instruction);
  if (index == tlbEntries.size())
  {
    return {
      store
        ? EEAddressTranslationOutcome::TLBRefillStore
        : EEAddressTranslationOutcome::TLBRefillLoadOrFetch,
      virtualAddress,
      0,
      EECacheRoute::TLBSelected,
      0,
      EEAddressRoute::MainBus,
      0xff
    };
  }

  const EETLBEntry &entry = tlbEntries[index];
  const EETLBPage &page = entry.pageForAddress(virtualAddress);
  const std::uint8_t resultIndex =
    static_cast<std::uint8_t>(index);
  if (!page.valid())
  {
    return {
      store
        ? EEAddressTranslationOutcome::TLBInvalidStore
        : EEAddressTranslationOutcome::TLBInvalidLoadOrFetch,
      virtualAddress,
      0,
      EECacheRoute::TLBSelected,
      0,
      EEAddressRoute::MainBus,
      resultIndex
    };
  }
  if (store && !page.dirty())
  {
    return {
      EEAddressTranslationOutcome::TLBModified,
      virtualAddress,
      0,
      EECacheRoute::TLBSelected,
      0,
      EEAddressRoute::MainBus,
      resultIndex
    };
  }

  const std::uint8_t cacheAttribute =
    static_cast<std::uint8_t>(
      (page.value & EECOP0EntryLo::CACHE_MODE_MASK) >> 3);
  if (page.scratchpad())
  {
    if (entry.pageMask != EECOP0PageMask::SIZE_16_KIB)
    {
      return {
        EEAddressTranslationOutcome::UnsupportedScratchpadPageSize,
        virtualAddress,
        0,
        EECacheRoute::Uncached,
        cacheAttribute,
        EEAddressRoute::Scratchpad,
        resultIndex
      };
    }
    return {
      instruction
        ? EEAddressTranslationOutcome::
            UnsupportedScratchpadInstruction
        : EEAddressTranslationOutcome::Translated,
      virtualAddress,
      virtualAddress & UINT32_C(0x00003fff),
      EECacheRoute::Uncached,
      cacheAttribute,
      EEAddressRoute::Scratchpad,
      resultIndex
    };
  }

  const std::uint32_t pageSize =
    (entry.pageMask + UINT32_C(0x2000)) >> 1;
  const std::uint32_t pageOffsetMask = pageSize - 1;
  const std::uint32_t physicalAddress =
    ((page.value & EECOP0EntryLo::PHYSICAL_FRAME_MASK) << 6) |
    (virtualAddress & pageOffsetMask);
  const EECacheRoute route = cacheRoute(cacheAttribute);
  return {
    route == EECacheRoute::Unsupported
      ? EEAddressTranslationOutcome::UnsupportedCacheAttribute
      : EEAddressTranslationOutcome::Translated,
    virtualAddress,
    physicalAddress,
    route,
    cacheAttribute,
    EEAddressRoute::MainBus,
    resultIndex
  };
}

std::size_t EEMemorySystem::matchingTLBEntry(
  std::uint32_t virtualAddress,
  bool instruction) const
{
  const std::uint8_t asid =
    static_cast<std::uint8_t>(
      cop0EntryHi & EECOP0EntryHi::ASID_MASK);
  auto findAccelerated =
    [&](auto &accelerator) -> std::size_t {
      for (TLBAcceleratorEntry &cached : accelerator)
      {
        if (cached.valid &&
            tlbEntries[cached.tlbIndex].matches(
              virtualAddress,
              asid))
        {
          for (std::size_t index = 0;
               index < cached.tlbIndex;
               ++index)
          {
            if (tlbEntries[index].matches(virtualAddress, asid))
            {
              cached.tlbIndex =
                static_cast<std::uint8_t>(index);
              break;
            }
          }
          return cached.tlbIndex;
        }
      }
      return tlbEntries.size();
    };
  const std::size_t accelerated =
    instruction ? findAccelerated(itlb) : findAccelerated(dtlb);
  if (accelerated != tlbEntries.size())
  {
    return accelerated;
  }

  std::size_t matched = tlbEntries.size();
  for (std::size_t index = 0;
       index < tlbEntries.size();
       ++index)
  {
    if (tlbEntries[index].matches(virtualAddress, asid))
    {
      matched = index;
      break;
    }
  }
  if (matched == tlbEntries.size())
  {
    return matched;
  }

  if (instruction)
  {
    itlb[nextITLBReplacement] = {
      true,
      static_cast<std::uint8_t>(matched)
    };
    nextITLBReplacement =
      (nextITLBReplacement + 1) % itlb.size();
  }
  else
  {
    dtlb[nextDTLBReplacement] = {
      true,
      static_cast<std::uint8_t>(matched)
    };
    nextDTLBReplacement =
      (nextDTLBReplacement + 1) % dtlb.size();
  }
  return matched;
}

void EEMemorySystem::invalidateTLBAccelerators()
{
  itlb.fill({});
  dtlb.fill({});
  nextITLBReplacement = 0;
  nextDTLBReplacement = 0;
}

void EEMemorySystem::reset()
{
  cop0Index = 0;
  cop0Random = EECOP0Random::RESET;
  cop0EntryLo0 = 0;
  cop0EntryLo1 = 0;
  cop0Context = 0;
  cop0PageMask = EECOP0PageMask::SIZE_4_KIB;
  cop0Wired = 0;
  cop0EntryHi = 0;
  cop0Config = EECOP0Config::RESET;
  cop0TagLo = 0;
  cop0TagHi = 0;
  tlbEntries.fill({});
  instructionCache.fill({});
  dataCache.fill({});
  scratchpad.fill({});
  invalidateTLBAccelerators();
}

const EETLBEntry &EEMemorySystem::tlbEntry(
  std::size_t index) const
{
  if (index >= tlbEntries.size())
  {
    throw std::out_of_range("EE TLB entry index is out of range.");
  }
  return tlbEntries[index];
}

void EEMemorySystem::setTLBEntry(
  std::size_t index,
  const EETLBEntry &entry)
{
  if (index >= tlbEntries.size())
  {
    throw std::out_of_range("EE TLB entry index is out of range.");
  }
  tlbEntries[index] = canonicalTLBEntry(
    entry.pageMask,
    entry.entryHi,
    entry.evenPage.value,
    entry.oddPage.value);
  invalidateTLBAccelerators();
}

EETLBEntry EEMemorySystem::currentTLBEntry() const
{
  return canonicalTLBEntry(
    cop0PageMask,
    cop0EntryHi,
    cop0EntryLo0,
    cop0EntryLo1);
}

void EEMemorySystem::readIndexedTLBEntry()
{
  const std::uint32_t index =
    cop0Index & EECOP0Index::INDEX_MASK;
  if (index >= tlbEntries.size())
  {
    return;
  }
  const EETLBEntry &entry = tlbEntries[index];
  cop0PageMask = entry.pageMask;
  cop0EntryHi = entry.entryHi;
  cop0EntryLo0 = entry.evenPage.value;
  cop0EntryLo1 = entry.oddPage.value;
}

void EEMemorySystem::writeTLBEntry(std::uint32_t index)
{
  if (index >= tlbEntries.size())
  {
    return;
  }
  tlbEntries[index] = currentTLBEntry();
  invalidateTLBAccelerators();
}

void EEMemorySystem::writeIndexedTLBEntry()
{
  writeTLBEntry(cop0Index & EECOP0Index::INDEX_MASK);
}

void EEMemorySystem::writeRandomTLBEntry()
{
  writeTLBEntry(cop0Random);
}

void EEMemorySystem::probeTLB()
{
  const std::uint8_t asid =
    static_cast<std::uint8_t>(
      cop0EntryHi & EECOP0EntryHi::ASID_MASK);
  const std::uint32_t virtualAddress =
    cop0EntryHi & EECOP0EntryHi::VIRTUAL_PAGE_MASK;
  for (std::size_t index = 0;
       index < tlbEntries.size();
       ++index)
  {
    if (tlbEntries[index].matches(virtualAddress, asid))
    {
      cop0Index = static_cast<std::uint32_t>(index);
      return;
    }
  }
  cop0Index = EECOP0Index::PROBE_FAILURE;
}

void EEMemorySystem::retireInstruction()
{
  if (cop0Random <= cop0Wired)
  {
    cop0Random = EECOP0Random::RESET;
    return;
  }
  --cop0Random;
}

bool EEMemorySystem::replacementStateValid() const
{
  return
    cop0Wired <= EECOP0Wired::MAXIMUM &&
    cop0Random >= cop0Wired &&
    cop0Random <= EECOP0Random::MAXIMUM;
}

const EECacheLine &EEMemorySystem::instructionCacheLine(
  std::size_t set,
  std::size_t way) const
{
  if (set >= instructionCache.size() ||
      way >= instructionCache[set].size())
  {
    throw std::out_of_range(
      "EE instruction-cache line index is out of range.");
  }
  return instructionCache[set][way];
}

const EECacheLine &EEMemorySystem::dataCacheLine(
  std::size_t set,
  std::size_t way) const
{
  if (set >= dataCache.size() ||
      way >= dataCache[set].size())
  {
    throw std::out_of_range(
      "EE data-cache line index is out of range.");
  }
  return dataCache[set][way];
}

std::array<std::uint8_t, 4>
EEMemorySystem::cacheLineRefillOrder(
  std::uint32_t physicalAddress)
{
  const std::uint8_t first =
    static_cast<std::uint8_t>((physicalAddress >> 4) & 3);
  return {
    first,
    static_cast<std::uint8_t>((first + 1) & 3),
    static_cast<std::uint8_t>((first + 2) & 3),
    static_cast<std::uint8_t>((first + 3) & 3)
  };
}

EECacheLineFillResult EEMemorySystem::fillCacheLine(
  const EEBus &bus,
  std::uint32_t physicalAddress) const
{
  EECacheLineFillResult result;
  result.lineBaseAddress =
    physicalAddress &
    ~static_cast<std::uint32_t>(CACHE_LINE_SIZE - 1);
  result.firstQuadword =
    static_cast<std::uint8_t>((physicalAddress >> 4) & 3);
  if (!bus.isMainMemoryRange(
        result.lineBaseAddress,
        CACHE_LINE_SIZE))
  {
    return result;
  }

  EECacheLine candidate;
  for (const std::uint8_t quadwordIndex :
       cacheLineRefillOrder(physicalAddress))
  {
    EEQuadword value = {};
    if (!bus.readData128(
          result.lineBaseAddress + quadwordIndex * 16,
          &value))
    {
      return result;
    }
    storeCacheQuadword(&candidate, quadwordIndex, value);
    ++result.quadwordsTransferred;
  }

  candidate.physicalTag =
    result.lineBaseAddress & EECacheLine::PHYSICAL_TAG_MASK;
  candidate.valid = true;
  result.outcome = EECacheLineTransferOutcome::Completed;
  result.line = candidate;
  return result;
}

EECacheLineTransferResult EEMemorySystem::writeBackDataCacheLine(
  EEBus *bus,
  std::size_t set,
  const EECacheLine &line) const
{
  if (bus == nullptr)
  {
    throw std::invalid_argument(
      "EE cache writeback requires a physical bus.");
  }
  if (set >= DATA_CACHE_SET_COUNT)
  {
    throw std::out_of_range(
      "EE data-cache writeback set is out of range.");
  }

  EECacheLineTransferResult result;
  result.outcome = EECacheLineTransferOutcome::Completed;
  result.lineBaseAddress =
    line.physicalTag |
    static_cast<std::uint32_t>(set * CACHE_LINE_SIZE);
  if (!line.valid || !line.dirty)
  {
    return result;
  }
  if ((line.physicalTag & ~EECacheLine::PHYSICAL_TAG_MASK) != 0)
  {
    result.outcome = EECacheLineTransferOutcome::InvalidLineState;
    return result;
  }
  if (!bus->isMainMemoryRange(
        result.lineBaseAddress,
        CACHE_LINE_SIZE))
  {
    result.outcome = EECacheLineTransferOutcome::PhysicalBusError;
    return result;
  }

  for (std::uint8_t quadwordIndex = 0;
       quadwordIndex < 4;
       ++quadwordIndex)
  {
    if (!bus->writeData128(
          result.lineBaseAddress + quadwordIndex * 16,
          loadCacheQuadword(line, quadwordIndex)))
    {
      result.outcome =
        EECacheLineTransferOutcome::PhysicalBusError;
      return result;
    }
    ++result.quadwordsTransferred;
  }
  return result;
}

std::uint32_t EEMemorySystem::cop0Register(
  EECOP0Register registerIndex) const
{
  switch (registerIndex)
  {
    case EECOP0Register::Index:
      return cop0Index;
    case EECOP0Register::Random:
      return cop0Random;
    case EECOP0Register::EntryLo0:
      return cop0EntryLo0;
    case EECOP0Register::EntryLo1:
      return cop0EntryLo1;
    case EECOP0Register::Context:
      return cop0Context;
    case EECOP0Register::PageMask:
      return cop0PageMask;
    case EECOP0Register::Wired:
      return cop0Wired;
    case EECOP0Register::EntryHi:
      return cop0EntryHi;
    case EECOP0Register::Config:
      return cop0Config;
    case EECOP0Register::TagLo:
      return cop0TagLo;
    case EECOP0Register::TagHi:
      return cop0TagHi;
    default:
      throw std::out_of_range(
        "EE memory-system COP0 register is not owned.");
  }
}

void EEMemorySystem::setCOP0Register(
  EECOP0Register registerIndex,
  std::uint32_t value)
{
  switch (registerIndex)
  {
    case EECOP0Register::Index:
      cop0Index = value & EECOP0Index::IMPLEMENTED_MASK;
      return;
    case EECOP0Register::Random:
      if (value > EECOP0Random::MAXIMUM)
      {
        throw std::invalid_argument(
          "EE COP0 Random index is outside the 48-entry TLB.");
      }
      cop0Random = value;
      return;
    case EECOP0Register::EntryLo0:
      cop0EntryLo0 =
        value & EECOP0EntryLo::ENTRY_LO_0_IMPLEMENTED_MASK;
      return;
    case EECOP0Register::EntryLo1:
      cop0EntryLo1 =
        value & EECOP0EntryLo::ENTRY_LO_1_IMPLEMENTED_MASK;
      return;
    case EECOP0Register::Context:
      cop0Context = value & EECOP0Context::IMPLEMENTED_MASK;
      return;
    case EECOP0Register::PageMask:
    {
      const std::uint32_t canonicalValue =
        value & EECOP0PageMask::IMPLEMENTED_MASK;
      if (!supportedPageMask(canonicalValue))
      {
        throw std::invalid_argument(
          "EE COP0 PageMask encoding is unsupported.");
      }
      cop0PageMask = canonicalValue;
      return;
    }
    case EECOP0Register::Wired:
      if (value > EECOP0Wired::MAXIMUM)
      {
        throw std::invalid_argument(
          "EE COP0 Wired index is outside the 48-entry TLB.");
      }
      cop0Wired = value;
      return;
    case EECOP0Register::EntryHi:
      cop0EntryHi = value & EECOP0EntryHi::IMPLEMENTED_MASK;
      return;
    case EECOP0Register::Config:
      cop0Config =
        EECOP0Config::FIXED |
        (value & EECOP0Config::WRITABLE_MASK);
      return;
    case EECOP0Register::TagLo:
      cop0TagLo = value & EECOP0TagLo::IMPLEMENTED_MASK;
      return;
    case EECOP0Register::TagHi:
      cop0TagHi = value;
      return;
    default:
      throw std::out_of_range(
        "EE memory-system COP0 register is not owned.");
  }
}

void EEMemorySystem::commitTLBExceptionAddress(
  std::uint32_t virtualAddress)
{
  cop0Context =
    (cop0Context & ~EECOP0Context::BAD_VPN2_MASK) |
    ((virtualAddress >> 9) & EECOP0Context::BAD_VPN2_MASK);
  cop0EntryHi =
    (cop0EntryHi & ~EECOP0EntryHi::VIRTUAL_PAGE_MASK) |
    (virtualAddress & EECOP0EntryHi::VIRTUAL_PAGE_MASK);
}

EECOP0WriteResult EEMemorySystem::writeCOP0Register(
  EECOP0Register registerIndex,
  std::uint32_t value)
{
  switch (registerIndex)
  {
    case EECOP0Register::Index:
      cop0Index =
        (cop0Index & EECOP0Index::PROBE_FAILURE) |
        (value & EECOP0Index::INDEX_MASK);
      return EECOP0WriteResult::Succeeded;
    case EECOP0Register::Random:
      return EECOP0WriteResult::Succeeded;
    case EECOP0Register::EntryLo0:
      cop0EntryLo0 =
        value & EECOP0EntryLo::ENTRY_LO_0_IMPLEMENTED_MASK;
      return EECOP0WriteResult::Succeeded;
    case EECOP0Register::EntryLo1:
      cop0EntryLo1 =
        value & EECOP0EntryLo::ENTRY_LO_1_IMPLEMENTED_MASK;
      return EECOP0WriteResult::Succeeded;
    case EECOP0Register::Context:
      cop0Context =
        (cop0Context & EECOP0Context::BAD_VPN2_MASK) |
        (value & EECOP0Context::PTE_BASE_MASK);
      return EECOP0WriteResult::Succeeded;
    case EECOP0Register::PageMask:
    {
      const std::uint32_t canonicalValue =
        value & EECOP0PageMask::IMPLEMENTED_MASK;
      if (!supportedPageMask(canonicalValue))
      {
        return EECOP0WriteResult::UnsupportedValue;
      }
      cop0PageMask = canonicalValue;
      return EECOP0WriteResult::Succeeded;
    }
    case EECOP0Register::Wired:
      if (value > EECOP0Wired::MAXIMUM)
      {
        return EECOP0WriteResult::UnsupportedValue;
      }
      cop0Wired = value;
      cop0Random = EECOP0Random::RESET;
      return EECOP0WriteResult::Succeeded;
    case EECOP0Register::EntryHi:
      cop0EntryHi = value & EECOP0EntryHi::IMPLEMENTED_MASK;
      return EECOP0WriteResult::Succeeded;
    case EECOP0Register::Config:
      cop0Config =
        EECOP0Config::FIXED |
        (value & EECOP0Config::WRITABLE_MASK);
      return EECOP0WriteResult::Succeeded;
    case EECOP0Register::TagLo:
      cop0TagLo = value & EECOP0TagLo::IMPLEMENTED_MASK;
      return EECOP0WriteResult::Succeeded;
    case EECOP0Register::TagHi:
      cop0TagHi = value;
      return EECOP0WriteResult::Succeeded;
    default:
      throw std::out_of_range(
        "EE memory-system COP0 register is not owned.");
  }
}
