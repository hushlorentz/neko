#ifndef VIF_H
#define VIF_H

#include <cstdint>

#include "vif_command.hpp"

class VIF
{
  public:
    explicit VIF(VIFType type);

    VIFType unitType() const;
    VIFCommand processCode(std::uint32_t code);

    std::uint16_t cycle() const;
    std::uint8_t cycleLength() const;
    std::uint8_t writeLength() const;
    std::uint8_t mode() const;
    std::uint16_t itops() const;
    std::uint16_t base() const;
    std::uint16_t offset() const;
    std::uint16_t tops() const;
    std::uint16_t mark() const;
    bool doubleBufferFlag() const;
    bool path3Masked() const;
    bool markDetected() const;
    std::uint32_t lastCode() const;

  private:
    VIFType type;
    std::uint16_t cycleRegister = 0;
    std::uint8_t modeRegister = 0;
    std::uint16_t itopsRegister = 0;
    std::uint16_t baseRegister = 0;
    std::uint16_t offsetRegister = 0;
    std::uint16_t topsRegister = 0;
    std::uint16_t markRegister = 0;
    bool dbf = false;
    bool path3Mask = false;
    bool markFlag = false;
    std::uint32_t codeRegister = 0;
};

#endif
