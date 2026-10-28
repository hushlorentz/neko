#ifndef EE_RDRAM_INITIALIZATION_HPP
#define EE_RDRAM_INITIALIZATION_HPP

#include <array>
#include <cstddef>
#include <cstdint>

struct EERDRAMInitializationState
{
  static constexpr std::size_t MCH_WINDOW_WORD_COUNT = 64;

  std::uint32_t command = 0;
  std::uint32_t inputData = 0;
  std::uint8_t enumeratedDevices = 0;
  std::array<std::uint32_t, MCH_WINDOW_WORD_COUNT> mchWindowWords{};
};

// Provisional synchronous retail initialization, not a DRAM timing model.
class EERDRAMInitialization
{
  public:
    std::uint32_t readStatus() const noexcept;
    bool writeCommand(std::uint32_t value) noexcept;
    void writeData(std::uint32_t value) noexcept;
    bool readData(std::uint32_t *value);
    bool readMCHWindowWord(
      std::size_t wordIndex, std::uint32_t *value) const;
    bool writeMCHWindowWord(
      std::size_t wordIndex, std::uint32_t value) noexcept;
    void reset() noexcept;
    // Diagnostic state, not command/data MMIO readback.
    EERDRAMInitializationState state() const noexcept;
    std::uint64_t stateHash() const;

  private:
    friend class NekoSaveStateCodec;
    static bool validState(const EERDRAMInitializationState &state) noexcept;
    EERDRAMInitializationState registers;
};

#endif
