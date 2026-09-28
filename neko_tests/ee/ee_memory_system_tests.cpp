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
      memorySystem.translateInstructionAddress(
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
      memorySystem.translateInstructionAddress(
        UINT32_C(0x81234567),
        context(privilege, true, false)),
      UINT32_C(0x81234567),
      UINT32_C(0x01234567),
      EECacheRoute::CachedNoncoherent);
    requireDirectRoute(
      memorySystem.translateInstructionAddress(
        UINT32_C(0xa1234567),
        context(privilege, false, true)),
      UINT32_C(0xa1234567),
      UINT32_C(0x01234567),
      EECacheRoute::Uncached);
  }
}

TEST_CASE("EE kernel direct segments select physical aliases and cache routes")
{
  EEMemorySystem memorySystem;
  const EEAddressTranslationContext kernel =
    context(EEPrivilegeMode::Kernel);

  requireDirectRoute(
    memorySystem.translateInstructionAddress(
      UINT32_C(0x80000000),
      kernel),
    UINT32_C(0x80000000),
    UINT32_C(0x00000000),
    EECacheRoute::CachedNoncoherent);
  requireDirectRoute(
    memorySystem.translateInstructionAddress(
      UINT32_C(0x9fffffff),
      kernel),
    UINT32_C(0x9fffffff),
    UINT32_C(0x1fffffff),
    EECacheRoute::CachedNoncoherent);
  requireDirectRoute(
    memorySystem.translateInstructionAddress(
      UINT32_C(0xa0000000),
      kernel),
    UINT32_C(0xa0000000),
    UINT32_C(0x00000000),
    EECacheRoute::Uncached);
  requireDirectRoute(
    memorySystem.translateInstructionAddress(
      UINT32_C(0xbfffffff),
      kernel),
    UINT32_C(0xbfffffff),
    UINT32_C(0x1fffffff),
    EECacheRoute::Uncached);
  requireTLBRoute(
    memorySystem.translateInstructionAddress(
      UINT32_C(0xc0000000),
      kernel),
    UINT32_C(0xc0000000));
  requireTLBRoute(
    memorySystem.translateInstructionAddress(
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
    memorySystem.translateDataAddress(
      UINT32_C(0x80000000),
      EEDataAccessDirection::Load,
      user);
  REQUIRE(
    load.outcome ==
    EEAddressTranslationOutcome::AddressErrorLoadOrFetch);
  REQUIRE(load.virtualAddress == UINT32_C(0x80000000));

  const EEAddressTranslationResult store =
    memorySystem.translateDataAddress(
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
    memorySystem.translateDataAddress(
      UINT32_C(0x20000000),
      EEDataAccessDirection::Load,
      user),
    UINT32_C(0x20000000));
  requireTLBRoute(
    memorySystem.translateDataAddress(
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
    memorySystem.translateDataAddress(
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
