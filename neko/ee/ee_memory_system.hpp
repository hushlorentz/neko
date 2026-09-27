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

class EEMemorySystem final
{
  public:
    static constexpr std::size_t TLB_ENTRY_COUNT = 48;

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
};

#endif
