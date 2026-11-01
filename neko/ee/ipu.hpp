#ifndef IPU_HPP
#define IPU_HPP

#include <array>
#include <cstdint>
#include "ee_types.hpp"

class EEInterruptController;

class IPU
{
  public:
    explicit IPU(EEInterruptController &interrupts);
    std::uint32_t control() const;
    std::uint8_t bitPosition() const;
    std::uint32_t bitPositionStatus() const;
    bool inputWriteReady() const;
    bool submitInput(const EEQuadword &value);
    std::uint64_t stateHash() const;
    bool writeControl(std::uint32_t value);
    bool writeCommand(std::uint32_t value);

  private:
    friend class NekoSaveStateCodec;

    void clearInput();

    EEInterruptController &interruptController;
    std::uint8_t bitPositionRegister = 0;
    std::uint8_t inputFIFOCount = 0;
    std::array<EEQuadword, 8> inputFIFO{};
};

#endif
