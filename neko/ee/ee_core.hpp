#ifndef EE_CORE_HPP
#define EE_CORE_HPP

#include <array>
#include <cstddef>
#include <cstdint>

class EEBus;

struct EERegister128
{
  std::uint64_t low = 0;
  std::uint64_t high = 0;
};

constexpr bool operator==(
  const EERegister128 &left,
  const EERegister128 &right)
{
  return
    left.low == right.low &&
    left.high == right.high;
}

constexpr bool operator!=(
  const EERegister128 &left,
  const EERegister128 &right)
{
  return !(left == right);
}

enum class EEException : std::uint8_t
{
  None,
  AddressErrorLoadOrFetch,
  InstructionBusError
};

struct EEInstructionFetchResult
{
  bool succeeded = false;
  std::uint32_t address = 0;
  std::uint32_t instruction = 0;
};

class EECore
{
  public:
    static constexpr std::size_t GENERAL_REGISTER_COUNT = 32;

    void reset();
    void attachBus(EEBus *bus);
    EEInstructionFetchResult fetchInstruction();

    const EERegister128 &generalRegister(
      std::size_t index) const;
    void setGeneralRegister(
      std::size_t index,
      const EERegister128 &value);

    std::uint32_t programCounter() const;
    void setProgramCounter(std::uint32_t value);

    std::uint64_t hi() const;
    void setHI(std::uint64_t value);
    std::uint64_t lo() const;
    void setLO(std::uint64_t value);
    std::uint64_t hi1() const;
    void setHI1(std::uint64_t value);
    std::uint64_t lo1() const;
    void setLO1(std::uint64_t value);

    std::uint32_t shiftAmount() const;
    void setShiftAmount(std::uint32_t value);

    bool exceptionPending() const;
    EEException pendingException() const;
    std::uint32_t exceptionAddress() const;
    void clearPendingException();

  private:
    friend class NekoSaveStateCodec;

    std::array<EERegister128, GENERAL_REGISTER_COUNT>
      generalRegisters = {};
    std::uint32_t pc = 0;
    std::uint64_t hiRegister = 0;
    std::uint64_t loRegister = 0;
    std::uint64_t hi1Register = 0;
    std::uint64_t lo1Register = 0;
    std::uint32_t saRegister = 0;
    EEBus *bus = nullptr;
    EEException exception = EEException::None;
    std::uint32_t faultAddress = 0;

    static void requireGeneralRegisterIndex(
      std::size_t index);
    EEBus &attachedBus() const;
    EEInstructionFetchResult raiseFetchException(
      EEException type,
      std::uint32_t address);
};

#endif
