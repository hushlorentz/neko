#ifndef EE_MEMORY_SYSTEM_HPP
#define EE_MEMORY_SYSTEM_HPP

#include <cstdint>

#include "ee_cop0.hpp"

class EEMemorySystem final
{
  public:
    void reset();
    std::uint32_t cop0Register(
      EECOP0Register registerIndex) const;
    void setCOP0Register(
      EECOP0Register registerIndex,
      std::uint32_t value);
    EECOP0WriteResult writeCOP0Register(
      EECOP0Register registerIndex,
      std::uint32_t value);

  private:
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
};

#endif
