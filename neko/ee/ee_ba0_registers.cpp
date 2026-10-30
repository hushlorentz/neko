#include "ee_ba0_registers.hpp"

namespace
{
  bool transferHalfwordOffset(std::uint32_t offset) noexcept
  {
    return offset >= EEBA0RegisterMap::TRANSFER_BEGIN &&
      offset < EEBA0RegisterMap::TRANSFER_END && offset % 2 == 0;
  }

  bool transferControl(std::uint16_t value) noexcept
  {
    return value == 0x42 || value == 0x43;
  }

  bool legacyDeviceReadValue(
    std::uint8_t selector, std::uint8_t &value) noexcept
  {
    // Selected compatibility identity, not a measured console profile.
    if (selector == 0x7e)
    {
      value = 0x1c;
      return true;
    }
    return false;
  }
}

bool EEBA0Registers::readHalfword(
  std::uint32_t offset, std::uint16_t &value) const noexcept
{
  if (transferHalfwordOffset(offset))
  {
    value = registers.transferStaging[
      (offset - EEBA0RegisterMap::TRANSFER_BEGIN) / sizeof(std::uint16_t)];
    return true;
  }
  switch (offset)
  {
    case 0x04:
      value = registers.register04;
      return true;
    case 0x06:
      value = registers.status06;
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
  if (transferHalfwordOffset(offset))
  {
    registers.transferStaging[
      (offset - EEBA0RegisterMap::TRANSFER_BEGIN) / sizeof(std::uint16_t)] = value;
    return true;
  }
  switch (offset)
  {
    case 0x00:
      return submitCommand(value);
    case 0x02:
      if (!transferControl(value))
      {
        return false;
      }
      registers.control02 = value;
      return true;
    case 0x04:
      registers.register04 = value;
      return true;
    case 0x06:
      return acknowledgeStatus(value);
    case 0x08:
      registers.register08 = value;
      return true;
    default:
      return false;
  }
}

bool EEBA0Registers::submitCommand(std::uint16_t command) noexcept
{
  if (command != 0x81)
  {
    return false;
  }
  switch (registers.control02)
  {
    case 0x42:
      return submitSelectorWrite();
    case 0x43:
      return submitDeviceRead();
    default:
      return false;
  }
}

bool EEBA0Registers::submitSelectorWrite() noexcept
{
  const auto selector = registers.transferStaging[0];
  if (selector > 0xff)
  {
    return false;
  }
  registers.selector = static_cast<std::uint8_t>(selector);
  registers.selectorValid = true;
  registers.status06 = 2;
  return true;
}

bool EEBA0Registers::submitDeviceRead() noexcept
{
  if (!registers.selectorValid)
  {
    return false;
  }
  std::uint8_t value = 0;
  if (!legacyDeviceReadValue(registers.selector, value))
  {
    return false;
  }
  registers.transferStaging[0] = value;
  registers.status06 = 2;
  return true;
}

bool EEBA0Registers::acknowledgeStatus(std::uint16_t value) noexcept
{
  if (value != 3)
  {
    return false;
  }
  registers.status06 = 0;
  return true;
}

EEBA0RegistersState EEBA0Registers::state() const noexcept
{
  return registers;
}

bool EEBA0Registers::validState(const EEBA0RegistersState &state) noexcept
{
  if (state.control02 != 0 && !transferControl(state.control02))
  {
    return false;
  }
  if (!state.selectorValid)
  {
    return state.selector == 0 && state.status06 == 0;
  }
  return state.control02 != 0 &&
    (state.status06 == 0 || state.status06 == 2);
}
