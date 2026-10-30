#include "ee_ba0_registers.hpp"

bool EEBA0Registers::readHalfword(
  std::uint32_t offset, std::uint16_t &value) const noexcept
{
  switch (offset)
  {
    case 0x04:
      value = registers.register04;
      return true;
    case 0x08:
      value = registers.register08;
      return true;
    default:
      return false;
  }
}

bool EEBA0Registers::writeHalfword(
  std::uint32_t offset, std::uint16_t value) noexcept
{
  switch (offset)
  {
    case 0x02:
      if (value != 0x42)
      {
        return false;
      }
      registers.control02 = value;
      return true;
    case 0x04:
      registers.register04 = value;
      return true;
    case 0x06:
      return value == 3;
    case 0x08:
      registers.register08 = value;
      return true;
    default:
      return false;
  }
}

EEBA0RegistersState EEBA0Registers::state() const noexcept
{
  return registers;
}

bool EEBA0Registers::validState(const EEBA0RegistersState &state) noexcept
{
  return state.control02 == 0 || state.control02 == 0x42;
}
