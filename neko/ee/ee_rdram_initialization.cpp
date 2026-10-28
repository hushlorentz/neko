#include "ee_rdram_initialization.hpp"

#include <stdexcept>

namespace
{
  constexpr std::uint32_t COMMAND_START = UINT32_C(0x80000000);
  constexpr std::uint32_t RETAIL_INITIALIZATION = UINT32_C(0x800003a0);
  constexpr std::uint32_t SA4C_WRITE = UINT32_C(0x804c0060);
  constexpr std::uint32_t SA4D_WRITE = UINT32_C(0x804d0060);
  constexpr std::uint8_t RETAIL_DEVICE_COUNT = 2;

  enum class Command
  {
    Unsupported,
    Initialize,
    WriteSA4C,
    WriteSA4D,
    WriteInitialization,
    WriteOther,
    Enumerate,
    ReadConfigA,
    ReadConfigB,
    ReadDeviceID
  };

  Command decodeCommand(std::uint32_t value) noexcept
  {
    if ((value & COMMAND_START) == 0)
    {
      return Command::Unsupported;
    }
    if (value == RETAIL_INITIALIZATION)
    {
      return Command::Initialize;
    }
    if (value == SA4D_WRITE)
    {
      return Command::WriteSA4D;
    }
    if (value == SA4C_WRITE)
    {
      return Command::WriteSA4C;
    }
    const auto operation = (value >> 6) & 0xf;
    const auto subaddress = (value >> 16) & 0xfff;
    if (operation == 1 && subaddress == 0x21)
    {
      return Command::WriteInitialization;
    }
    if (operation == 1 && subaddress != 0x4c && subaddress != 0x4d)
    {
      return Command::WriteOther;
    }
    if (operation != 0)
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
      ((command == Command::Initialize || command == Command::WriteSA4D) &&
       registers.inputData != 0) ||
      (command == Command::WriteSA4C && registers.inputData != 0x27))
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

bool EERDRAMInitialization::validState(
  const EERDRAMInitializationState &state) noexcept
{
  if ((state.command & COMMAND_START) != 0 ||
      state.enumeratedDevices > RETAIL_DEVICE_COUNT)
  {
    return false;
  }
  if (state.command == 0)
  {
    return state.enumeratedDevices == 0;
  }
  // Input may have changed since acceptance; do not replay its old preconditions.
  return decodeCommand(state.command | COMMAND_START) != Command::Unsupported;
}
