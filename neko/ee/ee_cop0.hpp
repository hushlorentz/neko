#ifndef EE_COP0_HPP
#define EE_COP0_HPP

#include <cstdint>

enum class EECOP0Register : std::uint8_t
{
  Index = 0,
  Random = 1,
  EntryLo0 = 2,
  EntryLo1 = 3,
  Context = 4,
  PageMask = 5,
  Wired = 6,
  BadVAddr = 8,
  Count = 9,
  EntryHi = 10,
  Compare = 11,
  Status = 12,
  Cause = 13,
  EPC = 14,
  PRId = 15,
  Config = 16,
  TagLo = 28,
  TagHi = 29,
  ErrorEPC = 30
};

namespace EECOP0Index
{
  constexpr std::uint32_t INDEX_MASK = UINT32_C(0x0000003f);
  constexpr std::uint32_t PROBE_FAILURE = UINT32_C(1) << 31;
  constexpr std::uint32_t IMPLEMENTED_MASK =
    PROBE_FAILURE | INDEX_MASK;
}

namespace EECOP0Random
{
  constexpr std::uint32_t RESET = 47;
  constexpr std::uint32_t MAXIMUM = 47;
}

namespace EECOP0EntryLo
{
  constexpr std::uint32_t GLOBAL = UINT32_C(1);
  constexpr std::uint32_t VALID = UINT32_C(1) << 1;
  constexpr std::uint32_t DIRTY = UINT32_C(1) << 2;
  constexpr std::uint32_t CACHE_MODE_MASK = UINT32_C(0x7) << 3;
  constexpr std::uint32_t PHYSICAL_FRAME_MASK =
    UINT32_C(0x000fffff) << 6;
  constexpr std::uint32_t SCRATCHPAD = UINT32_C(1) << 31;
  constexpr std::uint32_t ENTRY_LO_0_IMPLEMENTED_MASK =
    UINT32_C(0x83ffffff);
  constexpr std::uint32_t ENTRY_LO_1_IMPLEMENTED_MASK =
    UINT32_C(0x03ffffff);
}

namespace EECOP0Context
{
  constexpr std::uint32_t BAD_VPN2_MASK =
    UINT32_C(0x007ffff0);
  constexpr std::uint32_t PTE_BASE_MASK =
    UINT32_C(0xff800000);
  constexpr std::uint32_t IMPLEMENTED_MASK =
    PTE_BASE_MASK | BAD_VPN2_MASK;
}

namespace EECOP0PageMask
{
  constexpr std::uint32_t IMPLEMENTED_MASK =
    UINT32_C(0x01ffe000);
  constexpr std::uint32_t SIZE_4_KIB = UINT32_C(0x00000000);
  constexpr std::uint32_t SIZE_16_KIB = UINT32_C(0x00006000);
  constexpr std::uint32_t SIZE_64_KIB = UINT32_C(0x0001e000);
  constexpr std::uint32_t SIZE_256_KIB = UINT32_C(0x0007e000);
  constexpr std::uint32_t SIZE_1_MIB = UINT32_C(0x001fe000);
  constexpr std::uint32_t SIZE_4_MIB = UINT32_C(0x007fe000);
  constexpr std::uint32_t SIZE_16_MIB = UINT32_C(0x01ffe000);
}

namespace EECOP0Wired
{
  constexpr std::uint32_t MAXIMUM = 47;
}

namespace EECOP0EntryHi
{
  constexpr std::uint32_t ASID_MASK = UINT32_C(0xff);
  constexpr std::uint32_t VIRTUAL_PAGE_MASK =
    UINT32_C(0xffffe000);
  constexpr std::uint32_t IMPLEMENTED_MASK =
    UINT32_C(0xffffe0ff);
}

namespace EECOP0Status
{
  constexpr std::uint32_t IMPLEMENTED_MASK =
    UINT32_C(0xf0c79c1f);
  constexpr std::uint32_t SOFTWARE_WRITABLE_MASK =
    UINT32_C(0xf0c39c1f);
  constexpr std::uint32_t RESET = UINT32_C(0x70400004);
  constexpr std::uint32_t INTERRUPT_ENABLE = UINT32_C(1);
  constexpr std::uint32_t EXCEPTION_LEVEL = UINT32_C(1) << 1;
  constexpr std::uint32_t ERROR_LEVEL = UINT32_C(1) << 2;
  constexpr std::uint32_t PRIVILEGE_MASK = UINT32_C(0x3) << 3;
  constexpr std::uint32_t SUPERVISOR_MODE = UINT32_C(1) << 3;
  constexpr std::uint32_t USER_MODE = UINT32_C(2) << 3;
  constexpr std::uint32_t INTC_MASK = UINT32_C(1) << 10;
  constexpr std::uint32_t DMAC_MASK = UINT32_C(1) << 11;
  constexpr std::uint32_t MASTER_INTERRUPT_ENABLE =
    UINT32_C(1) << 16;
  constexpr std::uint32_t CACHE_HIT = UINT32_C(1) << 18;
  constexpr std::uint32_t BOOTSTRAP_EXCEPTION_VECTOR =
    UINT32_C(1) << 22;
  constexpr std::uint32_t COP0_USABLE = UINT32_C(1) << 28;
  constexpr std::uint32_t COP1_USABLE = UINT32_C(1) << 29;
}

namespace EECOP0Cause
{
  constexpr std::uint32_t EXCEPTION_CODE_MASK =
    UINT32_C(0x1f) << 2;
  constexpr std::uint32_t INTC_PENDING = UINT32_C(1) << 10;
  constexpr std::uint32_t DMAC_PENDING = UINT32_C(1) << 11;
  constexpr std::uint32_t BRANCH_DELAY = UINT32_C(1) << 31;
  constexpr std::uint32_t COPROCESSOR_ERROR_MASK =
    UINT32_C(0x3) << 28;
  constexpr std::uint32_t COPROCESSOR_1 =
    UINT32_C(1) << 28;
}

namespace EECOP0PRId
{
  constexpr std::uint32_t VALUE = UINT32_C(0x00002e20);
}

namespace EECOP0Config
{
  constexpr std::uint32_t FIXED = UINT32_C(0x00000440);
  constexpr std::uint32_t DATA_CACHE_ENABLE =
    UINT32_C(1) << 16;
  constexpr std::uint32_t INSTRUCTION_CACHE_ENABLE =
    UINT32_C(1) << 17;
  constexpr std::uint32_t WRITABLE_MASK =
    DATA_CACHE_ENABLE | INSTRUCTION_CACHE_ENABLE;
  constexpr std::uint32_t RESET = FIXED;
}

namespace EECOP0TagLo
{
  constexpr std::uint32_t IMPLEMENTED_MASK =
    UINT32_C(0xfffff078);
  constexpr std::uint32_t DIRTY = UINT32_C(1) << 6;
  constexpr std::uint32_t VALID = UINT32_C(1) << 5;
  constexpr std::uint32_t LEAST_RECENTLY_FILLED =
    UINT32_C(1) << 4;
  constexpr std::uint32_t LOCK = UINT32_C(1) << 3;
}

enum class EECOP0WriteResult : std::uint8_t
{
  Succeeded,
  UnsupportedValue
};

#endif
