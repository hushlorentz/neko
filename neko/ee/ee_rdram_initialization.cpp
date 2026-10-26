#include "ee_rdram_initialization.hpp"

#include <stdexcept>

namespace
{
  constexpr std::uint32_t COMMAND_START = UINT32_C(0x80000000);
  constexpr std::uint32_t COMMAND_FIELDS = UINT32_C(0x8fff03ff);
  constexpr std::uint32_t BROADCAST = 0x20;
  constexpr std::uint32_t RETAIL_INITIALIZATION = UINT32_C(0x800003a0);
  constexpr std::uint8_t RETAIL_DEVICE_COUNT = 2;

  enum class Command
  {
    Unsupported,
    Initialize,
    WriteInitialization,
    Enumerate,
    ReadConfigA,
    ReadConfigB,
    ReadDeviceID
  };

  Command decodeCommand(std::uint32_t value) noexcept
  {
    if ((value & COMMAND_START) == 0 || (value & ~COMMAND_FIELDS) != 0)
    {
      return Command::Unsupported;
    }
    if (value == RETAIL_INITIALIZATION)
    {
      return Command::Initialize;
    }
    const auto operation = (value >> 6) & 0xf;
    const auto subaddress = (value >> 16) & 0xfff;
    if (operation == 1 && subaddress == 0x21)
    {
      return Command::WriteInitialization;
    }
    if (operation != 0 || (value & BROADCAST) != 0)
    {
      return Command::Unsupported;
    }
    switch (subaddress)
    {
      case 0x21:
        return Command::Enumerate;
      case 0x23:
        return Command::ReadConfigA;
      case 0x24:
        return Command::ReadConfigB;
      case 0x40:
        return Command::ReadDeviceID;
      default:
        return Command::Unsupported;
    }
  }
}

std::uint32_t EERDRAMInitialization::readStatus() const noexcept
{
  return 0;
}

bool EERDRAMInitialization::writeCommand(std::uint32_t value) noexcept
{
  const auto command = decodeCommand(value);
  if (command == Command::Unsupported ||
      (command == Command::Initialize && registers.inputData != 0))
  {
    return false;
  }
  if (command == Command::WriteInitialization &&
      (registers.inputData & 0x80) == 0)
  {
    registers.enumeratedDevices = 0;
  }
  registers.command = value & ~COMMAND_START;
  return true;
}

void EERDRAMInitialization::writeData(std::uint32_t value) noexcept
{
  registers.inputData = value;
}

bool EERDRAMInitialization::readData(std::uint32_t *value)
{
  if (value == nullptr)
  {
    throw std::invalid_argument(
      "EE RDRAM initialization read requires an output value.");
  }
  switch (decodeCommand(registers.command | COMMAND_START))
  {
    case Command::Enumerate:
      if (registers.enumeratedDevices < RETAIL_DEVICE_COUNT)
      {
        ++registers.enumeratedDevices;
        *value = 0x1f;
      }
      else
      {
        *value = 0;
      }
      return true;
    case Command::ReadConfigA:
      *value = 0x0d0d;
      return true;
    case Command::ReadConfigB:
      *value = 0x0090;
      return true;
    case Command::ReadDeviceID:
      *value = registers.command & 0x1f;
      return true;
    default:
      return false;
  }
}

void EERDRAMInitialization::reset() noexcept
{
  registers = {};
}

EERDRAMInitializationState EERDRAMInitialization::state() const noexcept
{
  return registers;
}
