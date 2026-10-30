#ifndef EE_BA0_REGISTERS_HPP
#define EE_BA0_REGISTERS_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace EEBA0RegisterMap
{
  constexpr std::uint32_t TRANSFER_BEGIN = 0x10;
  constexpr std::size_t TRANSFER_HALFWORD_COUNT = 8;
  constexpr std::uint32_t TRANSFER_END =
    TRANSFER_BEGIN + TRANSFER_HALFWORD_COUNT * sizeof(std::uint16_t);
}

struct EEBA0RegistersState
{
  std::uint16_t control02 = 0;
  std::uint16_t register04 = 0;
  std::uint16_t register08 = 0;
  std::array<std::uint16_t, EEBA0RegisterMap::TRANSFER_HALFWORD_COUNT>
    transferStaging{};
};

// Provisional startup registers, not a command/status or DVE protocol.
class EEBA0Registers
{
  public:
    bool readHalfword(std::uint32_t offset, std::uint16_t &value) const noexcept;
    bool writeHalfword(std::uint32_t offset, std::uint16_t value) noexcept;
    EEBA0RegistersState state() const noexcept;

  private:
    friend class NekoSaveStateCodec;
    static bool validState(const EEBA0RegistersState &state) noexcept;
    EEBA0RegistersState registers;
};

#endif
