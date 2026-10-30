#ifndef EE_BA0_REGISTERS_HPP
#define EE_BA0_REGISTERS_HPP

#include <cstdint>

struct EEBA0RegistersState
{
  std::uint16_t control02 = 0;
  std::uint16_t register04 = 0;
  std::uint16_t register08 = 0;
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
