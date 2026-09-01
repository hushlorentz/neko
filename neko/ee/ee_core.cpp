#include "ee_core.hpp"

#include <stdexcept>

#include "ee_bus.hpp"

constexpr std::size_t EECore::GENERAL_REGISTER_COUNT;

void EECore::reset()
{
  generalRegisters.fill({});
  pc = 0;
  hiRegister = 0;
  loRegister = 0;
  hi1Register = 0;
  lo1Register = 0;
  saRegister = 0;
  exception = EEException::None;
  faultAddress = 0;
}

void EECore::attachBus(EEBus *newBus)
{
  if (newBus == nullptr)
  {
    throw std::invalid_argument(
      "EE Core requires a non-null bus.");
  }
  if (bus != nullptr)
  {
    throw std::logic_error(
      "EE Core bus is already attached.");
  }
  bus = newBus;
}

EEInstructionFetchResult EECore::fetchInstruction()
{
  const std::uint32_t address = pc;
  if (exceptionPending())
  {
    return {false, address, 0};
  }
  if ((address & 3) != 0)
  {
    return raiseFetchException(
      EEException::AddressErrorLoadOrFetch,
      address);
  }

  std::uint32_t instruction = 0;
  if (!attachedBus().readInstruction32(
        address,
        &instruction))
  {
    return raiseFetchException(
      EEException::InstructionBusError,
      address);
  }

  pc += 4;
  return {true, address, instruction};
}

const EERegister128 &EECore::generalRegister(
  std::size_t index) const
{
  requireGeneralRegisterIndex(index);
  return generalRegisters[index];
}

void EECore::setGeneralRegister(
  std::size_t index,
  const EERegister128 &value)
{
  requireGeneralRegisterIndex(index);
  if (index != 0)
  {
    generalRegisters[index] = value;
  }
}

std::uint32_t EECore::programCounter() const
{
  return pc;
}

void EECore::setProgramCounter(std::uint32_t value)
{
  pc = value;
}

std::uint64_t EECore::hi() const
{
  return hiRegister;
}

void EECore::setHI(std::uint64_t value)
{
  hiRegister = value;
}

std::uint64_t EECore::lo() const
{
  return loRegister;
}

void EECore::setLO(std::uint64_t value)
{
  loRegister = value;
}

std::uint64_t EECore::hi1() const
{
  return hi1Register;
}

void EECore::setHI1(std::uint64_t value)
{
  hi1Register = value;
}

std::uint64_t EECore::lo1() const
{
  return lo1Register;
}

void EECore::setLO1(std::uint64_t value)
{
  lo1Register = value;
}

std::uint32_t EECore::shiftAmount() const
{
  return saRegister;
}

void EECore::setShiftAmount(std::uint32_t value)
{
  saRegister = value;
}

bool EECore::exceptionPending() const
{
  return exception != EEException::None;
}

EEException EECore::pendingException() const
{
  return exception;
}

std::uint32_t EECore::exceptionAddress() const
{
  return faultAddress;
}

void EECore::clearPendingException()
{
  exception = EEException::None;
  faultAddress = 0;
}

void EECore::requireGeneralRegisterIndex(
  std::size_t index)
{
  if (index >= GENERAL_REGISTER_COUNT)
  {
    throw std::out_of_range(
      "EE general-purpose register index is out of range.");
  }
}

EEBus &EECore::attachedBus() const
{
  if (bus == nullptr)
  {
    throw std::logic_error(
      "EE Core bus is not attached.");
  }
  return *bus;
}

EEInstructionFetchResult EECore::raiseFetchException(
  EEException type,
  std::uint32_t address)
{
  exception = type;
  faultAddress = address;
  return {false, address, 0};
}
