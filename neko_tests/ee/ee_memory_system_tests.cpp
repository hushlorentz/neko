#include <cstdint>
#include <type_traits>

#include "catch.hpp"
#include "ee_memory_system.hpp"

static_assert(
  std::is_trivially_copyable<
    EEAddressTranslationContext>::value,
  "EE translation context must remain trivially copyable.");
static_assert(
  std::is_trivially_copyable<
    EEAddressTranslationResult>::value,
  "EE translation result must remain trivially copyable.");
static_assert(
  EEMemorySystem::ITLB_ENTRY_COUNT == 2,
  "The EE ITLB capacity must remain architectural.");
static_assert(
  EEMemorySystem::DTLB_ENTRY_COUNT == 4,
  "The EE DTLB capacity must remain architectural.");

namespace
{
  EEAddressTranslationContext context(
    EEPrivilegeMode privilege,
    bool exceptionLevel = false,
    bool errorLevel = false)
  {
    return {privilege, exceptionLevel, errorLevel};
  }

  void requireTLBRoute(
    const EEAddressTranslationResult &result,
    std::uint32_t virtualAddress)
  {
    REQUIRE(result.outcome == EEAddressTranslationOutcome::TLBLookup);
    REQUIRE(result.virtualAddress == virtualAddress);
    REQUIRE(result.cacheRoute == EECacheRoute::TLBSelected);
  }

  void requireDirectRoute(
    const EEAddressTranslationResult &result,
    std::uint32_t virtualAddress,
    std::uint32_t physicalAddress,
    EECacheRoute cacheRoute)
  {
    REQUIRE(result.outcome == EEAddressTranslationOutcome::Translated);
    REQUIRE(result.virtualAddress == virtualAddress);
    REQUIRE(result.physicalAddress == physicalAddress);
    REQUIRE(result.cacheRoute == cacheRoute);
  }
}

TEST_CASE("EE segment classification follows privilege boundaries")
{
  EEMemorySystem memorySystem;

  const struct
  {
    std::uint32_t address;
    EEPrivilegeMode privilege;
    EEAddressTranslationOutcome outcome;
  } cases[] = {
    {UINT32_C(0x00000000), EEPrivilegeMode::User,
     EEAddressTranslationOutcome::TLBLookup},
    {UINT32_C(0x7fffffff), EEPrivilegeMode::User,
     EEAddressTranslationOutcome::TLBLookup},
    {UINT32_C(0x80000000), EEPrivilegeMode::User,
     EEAddressTranslationOutcome::AddressErrorLoadOrFetch},
    {UINT32_C(0xffffffff), EEPrivilegeMode::User,
     EEAddressTranslationOutcome::AddressErrorLoadOrFetch},
    {UINT32_C(0x00000000), EEPrivilegeMode::Supervisor,
     EEAddressTranslationOutcome::TLBLookup},
    {UINT32_C(0x7fffffff), EEPrivilegeMode::Supervisor,
     EEAddressTranslationOutcome::TLBLookup},
    {UINT32_C(0x80000000), EEPrivilegeMode::Supervisor,
     EEAddressTranslationOutcome::AddressErrorLoadOrFetch},
    {UINT32_C(0xbfffffff), EEPrivilegeMode::Supervisor,
     EEAddressTranslationOutcome::AddressErrorLoadOrFetch},
    {UINT32_C(0xc0000000), EEPrivilegeMode::Supervisor,
     EEAddressTranslationOutcome::TLBLookup},
    {UINT32_C(0xdfffffff), EEPrivilegeMode::Supervisor,
     EEAddressTranslationOutcome::TLBLookup},
    {UINT32_C(0xe0000000), EEPrivilegeMode::Supervisor,
     EEAddressTranslationOutcome::AddressErrorLoadOrFetch},
    {UINT32_C(0xffffffff), EEPrivilegeMode::Supervisor,
     EEAddressTranslationOutcome::AddressErrorLoadOrFetch},
    {UINT32_C(0x00000000), EEPrivilegeMode::Kernel,
     EEAddressTranslationOutcome::TLBLookup},
    {UINT32_C(0xffffffff), EEPrivilegeMode::Kernel,
     EEAddressTranslationOutcome::TLBLookup}
  };

  for (const auto &testCase : cases)
  {
    const EEAddressTranslationResult result =
      memorySystem.classifyInstructionAddress(
        testCase.address,
        context(testCase.privilege));
    REQUIRE(result.outcome == testCase.outcome);
    REQUIRE(result.virtualAddress == testCase.address);
  }
}

TEST_CASE("EE exception levels use kernel segment privilege")
{
  EEMemorySystem memorySystem;

  for (const EEPrivilegeMode privilege : {
         EEPrivilegeMode::User,
         EEPrivilegeMode::Supervisor})
  {
    requireDirectRoute(
      memorySystem.classifyInstructionAddress(
        UINT32_C(0x81234567),
        context(privilege, true, false)),
      UINT32_C(0x81234567),
      UINT32_C(0x01234567),
      EECacheRoute::CachedNoncoherent);
    requireDirectRoute(
      memorySystem.classifyInstructionAddress(
        UINT32_C(0xa1234567),
        context(privilege, false, true)),
      UINT32_C(0xa1234567),
      UINT32_C(0x01234567),
      EECacheRoute::Uncached);
  }
}

TEST_CASE("EE exception modes preserve segment routing boundaries")
{
  EEMemorySystem memorySystem;
  const EEAddressTranslationContext exl =
    context(EEPrivilegeMode::User, true, false);
  const EEAddressTranslationContext erl =
    context(EEPrivilegeMode::User, false, true);

  requireTLBRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0x00000000),
      exl),
    UINT32_C(0x00000000));
  requireTLBRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0x7fffffff),
      exl),
    UINT32_C(0x7fffffff));
  requireDirectRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0x80000000),
      exl),
    UINT32_C(0x80000000),
    UINT32_C(0x00000000),
    EECacheRoute::CachedNoncoherent);
  requireTLBRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0xc0000000),
      exl),
    UINT32_C(0xc0000000));
  requireTLBRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0xe0000000),
      exl),
    UINT32_C(0xe0000000));

  requireDirectRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0x00000000),
      erl),
    UINT32_C(0x00000000),
    UINT32_C(0x00000000),
    EECacheRoute::Uncached);
  requireDirectRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0x7fffffff),
      erl),
    UINT32_C(0x7fffffff),
    UINT32_C(0x7fffffff),
    EECacheRoute::Uncached);
  requireDirectRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0xa0000000),
      erl),
    UINT32_C(0xa0000000),
    UINT32_C(0x00000000),
    EECacheRoute::Uncached);
  requireTLBRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0xc0000000),
      erl),
    UINT32_C(0xc0000000));
  requireTLBRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0xe0000000),
      erl),
    UINT32_C(0xe0000000));
}

TEST_CASE("EE error level makes kuseg unmapped and uncached")
{
  EEMemorySystem memorySystem;

  requireDirectRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0x01234567),
      context(EEPrivilegeMode::User, false, true)),
    UINT32_C(0x01234567),
    UINT32_C(0x01234567),
    EECacheRoute::Uncached);
}

TEST_CASE("EE kernel direct segments select physical aliases and cache routes")
{
  EEMemorySystem memorySystem;
  const EEAddressTranslationContext kernel =
    context(EEPrivilegeMode::Kernel);

  requireDirectRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0x80000000),
      kernel),
    UINT32_C(0x80000000),
    UINT32_C(0x00000000),
    EECacheRoute::CachedNoncoherent);
  requireDirectRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0x9fffffff),
      kernel),
    UINT32_C(0x9fffffff),
    UINT32_C(0x1fffffff),
    EECacheRoute::CachedNoncoherent);
  requireDirectRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0xa0000000),
      kernel),
    UINT32_C(0xa0000000),
    UINT32_C(0x00000000),
    EECacheRoute::Uncached);
  requireDirectRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0xbfffffff),
      kernel),
    UINT32_C(0xbfffffff),
    UINT32_C(0x1fffffff),
    EECacheRoute::Uncached);
  requireTLBRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0xc0000000),
      kernel),
    UINT32_C(0xc0000000));
  requireTLBRoute(
    memorySystem.classifyInstructionAddress(
      UINT32_C(0xffffffff),
      kernel),
    UINT32_C(0xffffffff));
}

TEST_CASE("EE data segment faults retain the access direction")
{
  EEMemorySystem memorySystem;
  const EEAddressTranslationContext user =
    context(EEPrivilegeMode::User);

  const EEAddressTranslationResult load =
    memorySystem.classifyDataAddress(
      UINT32_C(0x80000000),
      EEDataAccessDirection::Load,
      user);
  REQUIRE(
    load.outcome ==
    EEAddressTranslationOutcome::AddressErrorLoadOrFetch);
  REQUIRE(load.virtualAddress == UINT32_C(0x80000000));

  const EEAddressTranslationResult store =
    memorySystem.classifyDataAddress(
      UINT32_C(0x80000000),
      EEDataAccessDirection::Store,
      user);
  REQUIRE(
    store.outcome ==
    EEAddressTranslationOutcome::AddressErrorStore);
  REQUIRE(store.virtualAddress == UINT32_C(0x80000000));
}

TEST_CASE("EE mapped application aliases require TLB translation")
{
  EEMemorySystem memorySystem;
  const EEAddressTranslationContext user =
    context(EEPrivilegeMode::User);

  requireTLBRoute(
    memorySystem.classifyDataAddress(
      UINT32_C(0x20000000),
      EEDataAccessDirection::Load,
      user),
    UINT32_C(0x20000000));
  requireTLBRoute(
    memorySystem.classifyDataAddress(
      UINT32_C(0x30000000),
      EEDataAccessDirection::Store,
      user),
    UINT32_C(0x30000000));
}

TEST_CASE("EE cache attributes classify only supported routes")
{
  const struct
  {
    std::uint8_t attribute;
    EECacheRoute route;
  } cases[] = {
    {0, EECacheRoute::Unsupported},
    {1, EECacheRoute::Unsupported},
    {2, EECacheRoute::Uncached},
    {3, EECacheRoute::CachedNoncoherent},
    {4, EECacheRoute::Unsupported},
    {5, EECacheRoute::Unsupported},
    {6, EECacheRoute::Unsupported},
    {7, EECacheRoute::UncachedAccelerated}
  };

  for (const auto &testCase : cases)
  {
    REQUIRE(
      EEMemorySystem::cacheRoute(testCase.attribute) ==
      testCase.route);
  }
}

TEST_CASE("EE segment classification does not mutate MMU state")
{
  EEMemorySystem memorySystem;
  memorySystem.setCOP0Register(EECOP0Register::Index, 9);
  memorySystem.setCOP0Register(
    EECOP0Register::EntryHi,
    UINT32_C(0x1234405a));
  const EETLBEntry entry{
    EECOP0PageMask::SIZE_16_KIB,
    UINT32_C(0x1234005a),
    {UINT32_C(0x00010007)},
    {UINT32_C(0x00020007)}
  };
  memorySystem.setTLBEntry(9, entry);

  const EEAddressTranslationResult result =
    memorySystem.classifyDataAddress(
      UINT32_C(0x81234567),
      EEDataAccessDirection::Store,
      context(EEPrivilegeMode::Kernel));

  REQUIRE(result.outcome == EEAddressTranslationOutcome::Translated);
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::Index) == 9);
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::EntryHi) ==
    UINT32_C(0x1234405a));
  REQUIRE(memorySystem.tlbEntry(9) == entry);
}

TEST_CASE("EE architectural TLB translation selects ASIDs and global entries")
{
  EEMemorySystem memorySystem;
  memorySystem.setTLBEntry(
    7,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x1234402a),
      {UINT32_C(0x0001001e)},
      {UINT32_C(0x0001401e)}
    });
  memorySystem.setCOP0Register(
    EECOP0Register::EntryHi,
    UINT32_C(0x0000002b));

  const EEAddressTranslationResult wrongASID =
    memorySystem.translateInstructionAddress(
      UINT32_C(0x12344123),
      context(EEPrivilegeMode::User));
  REQUIRE(
    wrongASID.outcome ==
    EEAddressTranslationOutcome::TLBRefillLoadOrFetch);

  memorySystem.setCOP0Register(
    EECOP0Register::EntryHi,
    UINT32_C(0x0000002a));
  const EEAddressTranslationResult matchingASID =
    memorySystem.translateInstructionAddress(
      UINT32_C(0x12344123),
      context(EEPrivilegeMode::User));
  requireDirectRoute(
    matchingASID,
    UINT32_C(0x12344123),
    UINT32_C(0x00400123),
    EECacheRoute::CachedNoncoherent);

  memorySystem.setTLBEntry(
    7,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x1234402a),
      {UINT32_C(0x0001001f)},
      {UINT32_C(0x0001401f)}
    });
  memorySystem.setCOP0Register(
    EECOP0Register::EntryHi,
    UINT32_C(0x0000002b));
  const EEAddressTranslationResult global =
    memorySystem.translateInstructionAddress(
      UINT32_C(0x12344123),
      context(EEPrivilegeMode::User));
  REQUIRE(global.outcome == EEAddressTranslationOutcome::Translated);
  REQUIRE(global.physicalAddress == UINT32_C(0x00400123));

  const auto requireASIDSensitive =
    [&memorySystem]()
    {
      for (const std::uint32_t address :
           {UINT32_C(0x12344123), UINT32_C(0x12345123)})
      {
        REQUIRE(
          memorySystem.translateInstructionAddress(
            address,
            context(EEPrivilegeMode::User)).outcome ==
          EEAddressTranslationOutcome::TLBRefillLoadOrFetch);
      }
    };
  memorySystem.setTLBEntry(
    7,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x1234402a),
      {UINT32_C(0x0001001f)},
      {UINT32_C(0x0001401e)}
    });
  requireASIDSensitive();
  memorySystem.setTLBEntry(
    7,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x1234402a),
      {UINT32_C(0x0001001e)},
      {UINT32_C(0x0001401f)}
    });
  requireASIDSensitive();
}

TEST_CASE("EE architectural TLB translation uses the lowest duplicate index")
{
  EEMemorySystem memorySystem;
  const EETLBEntry higher{
    EECOP0PageMask::SIZE_4_KIB,
    UINT32_C(0x23456044),
    {UINT32_C(0x0002401f)},
    {UINT32_C(0x0002801f)}
  };
  const EETLBEntry lower{
    EECOP0PageMask::SIZE_4_KIB,
    UINT32_C(0x23456044),
    {UINT32_C(0x0001c01f)},
    {UINT32_C(0x0002001f)}
  };
  memorySystem.setTLBEntry(20, higher);
  memorySystem.setTLBEntry(3, lower);

  const EEAddressTranslationResult result =
    memorySystem.translateDataAddress(
      UINT32_C(0x23456111),
      EEDataAccessDirection::Load,
      context(EEPrivilegeMode::User));

  REQUIRE(result.outcome == EEAddressTranslationOutcome::Translated);
  REQUIRE(result.physicalAddress == UINT32_C(0x00700111));
  REQUIRE(result.tlbIndex == 3);
}

TEST_CASE("EE architectural TLB translation covers every page size")
{
  const struct
  {
    std::uint32_t pageMask;
    std::uint32_t pageSize;
  } pageSizes[] = {
    {EECOP0PageMask::SIZE_4_KIB, UINT32_C(0x00001000)},
    {EECOP0PageMask::SIZE_16_KIB, UINT32_C(0x00004000)},
    {EECOP0PageMask::SIZE_64_KIB, UINT32_C(0x00010000)},
    {EECOP0PageMask::SIZE_256_KIB, UINT32_C(0x00040000)},
    {EECOP0PageMask::SIZE_1_MIB, UINT32_C(0x00100000)},
    {EECOP0PageMask::SIZE_4_MIB, UINT32_C(0x00400000)},
    {EECOP0PageMask::SIZE_16_MIB, UINT32_C(0x01000000)}
  };

  for (const auto &pageSize : pageSizes)
  {
    EEMemorySystem memorySystem;
    const std::uint32_t virtualBase = UINT32_C(0x40000000);
    const std::uint32_t physicalBase = UINT32_C(0x10000000);
    memorySystem.setTLBEntry(
      4,
      {
        pageSize.pageMask,
        virtualBase,
        {((physicalBase >> 12) << 6) | UINT32_C(0x1e)},
        {(((physicalBase + pageSize.pageSize) >> 12) << 6) |
         UINT32_C(0x1e)}
      });

    const std::uint32_t offset = pageSize.pageSize - 1;
    const EEAddressTranslationResult even =
      memorySystem.translateDataAddress(
        virtualBase + offset,
        EEDataAccessDirection::Load,
        context(EEPrivilegeMode::User));
    REQUIRE(even.outcome == EEAddressTranslationOutcome::Translated);
    REQUIRE(even.physicalAddress == physicalBase + offset);

    const EEAddressTranslationResult odd =
      memorySystem.translateDataAddress(
        virtualBase + pageSize.pageSize + offset,
        EEDataAccessDirection::Load,
        context(EEPrivilegeMode::User));
    REQUIRE(odd.outcome == EEAddressTranslationOutcome::Translated);
    REQUIRE(
      odd.physicalAddress ==
      physicalBase + pageSize.pageSize + offset);

    const EEAddressTranslationResult outside =
      memorySystem.translateDataAddress(
        virtualBase + pageSize.pageSize * 2,
        EEDataAccessDirection::Load,
        context(EEPrivilegeMode::User));
    REQUIRE(
      outside.outcome ==
      EEAddressTranslationOutcome::TLBRefillLoadOrFetch);
  }
}

TEST_CASE("EE architectural TLB translation reports permission faults")
{
  EEMemorySystem memorySystem;
  const std::uint32_t address = UINT32_C(0x34567000);
  memorySystem.setTLBEntry(
    5,
    {
      EECOP0PageMask::SIZE_4_KIB,
      address,
      {UINT32_C(0x00040018)},
      {UINT32_C(0x00050018)}
    });

  REQUIRE(
    memorySystem.translateDataAddress(
      address,
      EEDataAccessDirection::Load,
      context(EEPrivilegeMode::User)).outcome ==
    EEAddressTranslationOutcome::TLBInvalidLoadOrFetch);
  REQUIRE(
    memorySystem.translateDataAddress(
      address,
      EEDataAccessDirection::Store,
      context(EEPrivilegeMode::User)).outcome ==
    EEAddressTranslationOutcome::TLBInvalidStore);

  memorySystem.setTLBEntry(
    5,
    {
      EECOP0PageMask::SIZE_4_KIB,
      address,
      {UINT32_C(0x0004001a)},
      {UINT32_C(0x0005001a)}
    });
  REQUIRE(
    memorySystem.translateDataAddress(
      address,
      EEDataAccessDirection::Load,
      context(EEPrivilegeMode::User)).outcome ==
    EEAddressTranslationOutcome::Translated);
  REQUIRE(
    memorySystem.translateDataAddress(
      address,
      EEDataAccessDirection::Store,
      context(EEPrivilegeMode::User)).outcome ==
    EEAddressTranslationOutcome::TLBModified);
}

TEST_CASE("EE architectural TLB translation distinguishes refill direction")
{
  EEMemorySystem memorySystem;
  const std::uint32_t address = UINT32_C(0x76543000);

  REQUIRE(
    memorySystem.translateInstructionAddress(
      address,
      context(EEPrivilegeMode::User)).outcome ==
    EEAddressTranslationOutcome::TLBRefillLoadOrFetch);
  REQUIRE(
    memorySystem.translateDataAddress(
      address,
      EEDataAccessDirection::Load,
      context(EEPrivilegeMode::User)).outcome ==
    EEAddressTranslationOutcome::TLBRefillLoadOrFetch);
  REQUIRE(
    memorySystem.translateDataAddress(
      address,
      EEDataAccessDirection::Store,
      context(EEPrivilegeMode::User)).outcome ==
    EEAddressTranslationOutcome::TLBRefillStore);
}

TEST_CASE("EE TLB translation defers exception-register mutation")
{
  EEMemorySystem memorySystem;
  constexpr std::uint32_t address = UINT32_C(0x34567abc);
  constexpr std::uint32_t initialContext = UINT32_C(0xabd23450);
  constexpr std::uint32_t initialEntryHi = UINT32_C(0x89abc05a);
  memorySystem.setCOP0Register(
    EECOP0Register::Context,
    initialContext);
  memorySystem.setCOP0Register(
    EECOP0Register::EntryHi,
    initialEntryHi);

  REQUIRE(
    memorySystem.translateInstructionAddress(
      address,
      context(EEPrivilegeMode::User)).outcome ==
    EEAddressTranslationOutcome::TLBRefillLoadOrFetch);
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::Context) ==
    initialContext);
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::EntryHi) ==
    initialEntryHi);

  memorySystem.setTLBEntry(
    0,
    {
      EECOP0PageMask::SIZE_4_KIB,
      (address & EECOP0EntryHi::VIRTUAL_PAGE_MASK) |
        (initialEntryHi & EECOP0EntryHi::ASID_MASK),
      {UINT32_C(0x0001001a)},
      {UINT32_C(0x0001001a)}
    });
  REQUIRE(
    memorySystem.translateDataAddress(
      address,
      EEDataAccessDirection::Store,
      context(EEPrivilegeMode::User)).outcome ==
    EEAddressTranslationOutcome::TLBModified);
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::Context) ==
    initialContext);
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::EntryHi) ==
    initialEntryHi);

  memorySystem.commitTLBExceptionAddress(address);

  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::Context) ==
    ((initialContext & EECOP0Context::PTE_BASE_MASK) |
     ((address >> 9) & EECOP0Context::BAD_VPN2_MASK)));
  REQUIRE(
    memorySystem.cop0Register(EECOP0Register::EntryHi) ==
    ((address & EECOP0EntryHi::VIRTUAL_PAGE_MASK) |
     (initialEntryHi & ~EECOP0EntryHi::VIRTUAL_PAGE_MASK)));
}

TEST_CASE("EE architectural TLB translation selects mapped cache routes")
{
  const struct
  {
    std::uint8_t attribute;
    EECacheRoute route;
  } cacheRoutes[] = {
    {2, EECacheRoute::Uncached},
    {3, EECacheRoute::CachedNoncoherent},
    {7, EECacheRoute::UncachedAccelerated}
  };

  for (const auto &cacheRoute : cacheRoutes)
  {
    EEMemorySystem memorySystem;
    memorySystem.setTLBEntry(
      2,
      {
        EECOP0PageMask::SIZE_4_KIB,
        UINT32_C(0x45678000),
        {
          UINT32_C(0x00010006) |
          (static_cast<std::uint32_t>(cacheRoute.attribute) << 3)
        },
        {UINT32_C(0x0001401e)}
      });

    const EEAddressTranslationResult result =
      memorySystem.translateDataAddress(
        UINT32_C(0x45678123),
        EEDataAccessDirection::Load,
        context(EEPrivilegeMode::User));
    REQUIRE(result.outcome == EEAddressTranslationOutcome::Translated);
    REQUIRE(result.cacheAttribute == cacheRoute.attribute);
    REQUIRE(result.cacheRoute == cacheRoute.route);
  }
}

TEST_CASE("EE architectural TLB translation preserves unsupported attributes")
{
  EEMemorySystem memorySystem;
  memorySystem.setTLBEntry(
    6,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x45678000),
      {UINT32_C(0x00010006)},
      {UINT32_C(0x00014006)}
    });

  const EEAddressTranslationResult result =
    memorySystem.translateDataAddress(
      UINT32_C(0x45678123),
      EEDataAccessDirection::Load,
      context(EEPrivilegeMode::User));
  REQUIRE(
    result.outcome ==
    EEAddressTranslationOutcome::UnsupportedCacheAttribute);
  REQUIRE(result.cacheAttribute == 0);
  REQUIRE(result.cacheRoute == EECacheRoute::Unsupported);
  REQUIRE(result.physicalAddress == UINT32_C(0x00400123));
}

TEST_CASE("EE architectural TLB translation selects scratchpad")
{
  EEMemorySystem memorySystem;
  memorySystem.setTLBEntry(
    8,
    {
      EECOP0PageMask::SIZE_16_KIB,
      UINT32_C(0x50000000),
      {EECOP0EntryLo::SCRATCHPAD | UINT32_C(0x00000006)},
      {UINT32_C(0x0007001e)}
    });

  const EEAddressTranslationResult result =
    memorySystem.translateDataAddress(
      UINT32_C(0x50003210),
      EEDataAccessDirection::Load,
      context(EEPrivilegeMode::User));
  REQUIRE(result.outcome == EEAddressTranslationOutcome::Translated);
  REQUIRE(result.route == EEAddressRoute::Scratchpad);
  REQUIRE(result.physicalAddress == UINT32_C(0x00003210));
  REQUIRE(result.cacheRoute == EECacheRoute::Uncached);
}

TEST_CASE("EE instruction translation rejects scratchpad mappings")
{
  EEMemorySystem memorySystem;
  memorySystem.setTLBEntry(
    8,
    {
      EECOP0PageMask::SIZE_16_KIB,
      UINT32_C(0x50000000),
      {
        EECOP0EntryLo::SCRATCHPAD |
        UINT32_C(0x0001001e)
      },
      {UINT32_C(0x0001401e)}
    });

  const EEAddressTranslationResult result =
    memorySystem.translateInstructionAddress(
      UINT32_C(0x50003210),
      context(EEPrivilegeMode::User));
  REQUIRE(
    result.outcome ==
    EEAddressTranslationOutcome::UnsupportedScratchpadInstruction);
  REQUIRE(result.route == EEAddressRoute::Scratchpad);
  REQUIRE(result.physicalAddress == UINT32_C(0x00003210));
}

TEST_CASE("EE translation rejects non-16 KiB scratchpad mappings")
{
  EEMemorySystem memorySystem;
  memorySystem.setTLBEntry(
    9,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x51000000),
      {
        EECOP0EntryLo::SCRATCHPAD |
        UINT32_C(0x0001001e)
      },
      {UINT32_C(0x0001401e)}
    });

  const EEAddressTranslationResult result =
    memorySystem.translateDataAddress(
      UINT32_C(0x51000210),
      EEDataAccessDirection::Load,
      context(EEPrivilegeMode::User));
  REQUIRE(
    result.outcome ==
    EEAddressTranslationOutcome::UnsupportedScratchpadPageSize);
  REQUIRE(result.route == EEAddressRoute::Scratchpad);
  REQUIRE(result.physicalAddress == 0);
}

TEST_CASE("EE derived translation accelerators invalidate on TLB changes")
{
  EEMemorySystem memorySystem;
  const std::uint32_t address = UINT32_C(0x60000120);
  memorySystem.setTLBEntry(
    1,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x60000000),
      {UINT32_C(0x0000401e)},
      {UINT32_C(0x0000801e)}
    });

  REQUIRE(
    memorySystem.translateInstructionAddress(
      address,
      context(EEPrivilegeMode::User)).physicalAddress ==
    UINT32_C(0x00100120));
  REQUIRE(
    memorySystem.translateDataAddress(
      address,
      EEDataAccessDirection::Load,
      context(EEPrivilegeMode::User)).physicalAddress ==
    UINT32_C(0x00100120));

  memorySystem.setTLBEntry(
    1,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x60000000),
      {UINT32_C(0x0000c01e)},
      {UINT32_C(0x0001001e)}
    });
  REQUIRE(
    memorySystem.translateInstructionAddress(
      address,
      context(EEPrivilegeMode::User)).physicalAddress ==
    UINT32_C(0x00300120));
  REQUIRE(
    memorySystem.translateDataAddress(
      address,
      EEDataAccessDirection::Load,
      context(EEPrivilegeMode::User)).physicalAddress ==
    UINT32_C(0x00300120));
}

TEST_CASE("EE derived accelerators preserve lowest-index overlapping matches")
{
  EEMemorySystem memorySystem;
  memorySystem.setTLBEntry(
    20,
    {
      EECOP0PageMask::SIZE_16_KIB,
      UINT32_C(0x70000000),
      {UINT32_C(0x0001401f)},
      {UINT32_C(0x0001801f)}
    });
  memorySystem.setTLBEntry(
    3,
    {
      EECOP0PageMask::SIZE_4_KIB,
      UINT32_C(0x70000000),
      {UINT32_C(0x0001c01f)},
      {UINT32_C(0x0002001f)}
    });

  const EEAddressTranslationResult largeOnly =
    memorySystem.translateInstructionAddress(
      UINT32_C(0x70002100),
      context(EEPrivilegeMode::User));
  REQUIRE(largeOnly.tlbIndex == 20);

  const EEAddressTranslationResult overlapping =
    memorySystem.translateInstructionAddress(
      UINT32_C(0x70000100),
      context(EEPrivilegeMode::User));
  REQUIRE(overlapping.tlbIndex == 3);
  REQUIRE(
    overlapping.physicalAddress ==
    UINT32_C(0x00700100));
}

TEST_CASE(
  "EE instruction and data translation remain consistent beyond accelerator capacity")
{
  EEMemorySystem memorySystem;
  constexpr std::size_t mappingCount =
    EEMemorySystem::DTLB_ENTRY_COUNT + 2;
  const EEAddressTranslationContext user =
    context(EEPrivilegeMode::User);

  for (std::size_t index = 0; index < mappingCount; ++index)
  {
    const std::uint32_t virtualBase =
      UINT32_C(0x10000000) +
      static_cast<std::uint32_t>(index) * UINT32_C(0x2000);
    const std::uint32_t physicalBase =
      UINT32_C(0x00100000) +
      static_cast<std::uint32_t>(index) * UINT32_C(0x2000);
    memorySystem.setTLBEntry(
      index,
      {
        EECOP0PageMask::SIZE_4_KIB,
        virtualBase,
        {(physicalBase >> 6) | UINT32_C(0x1f)},
        {((physicalBase + UINT32_C(0x1000)) >> 6) |
         UINT32_C(0x1f)}
      });
  }

  const auto requireMapping =
    [&memorySystem, &user](std::size_t index)
    {
      const std::uint32_t virtualAddress =
        UINT32_C(0x10000120) +
        static_cast<std::uint32_t>(index) * UINT32_C(0x2000);
      const std::uint32_t physicalAddress =
        UINT32_C(0x00100120) +
        static_cast<std::uint32_t>(index) * UINT32_C(0x2000);
      const EEAddressTranslationResult instruction =
        memorySystem.translateInstructionAddress(
          virtualAddress,
          user);
      const EEAddressTranslationResult load =
        memorySystem.translateDataAddress(
          virtualAddress,
          EEDataAccessDirection::Load,
          user);
      const EEAddressTranslationResult store =
        memorySystem.translateDataAddress(
          virtualAddress,
          EEDataAccessDirection::Store,
          user);

      for (const EEAddressTranslationResult *result :
           {&instruction, &load, &store})
      {
        REQUIRE(
          result->outcome ==
          EEAddressTranslationOutcome::Translated);
        REQUIRE(result->virtualAddress == virtualAddress);
        REQUIRE(result->physicalAddress == physicalAddress);
        REQUIRE(result->tlbIndex == index);
      }
    };

  for (std::size_t index = 0; index < mappingCount; ++index)
  {
    requireMapping(index);
  }
  for (std::size_t index = mappingCount; index-- > 0;)
  {
    requireMapping(index);
  }
}
