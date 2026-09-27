#include "ee_memory_system.hpp"

#include <stdexcept>

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
