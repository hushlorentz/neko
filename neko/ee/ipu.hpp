#ifndef IPU_HPP
#define IPU_HPP

#include <cstdint>

class EEInterruptController;

class IPU
{
  public:
    explicit IPU(EEInterruptController &interrupts);
    std::uint32_t control() const;
    std::uint8_t bitPosition() const;
    bool writeControl(std::uint32_t value);
    bool writeCommand(std::uint32_t value);

  private:
    friend class NekoSaveStateCodec;

    EEInterruptController &interruptController;
    std::uint8_t bitPositionRegister = 0;
};

#endif
