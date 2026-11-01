#ifndef IPU_HPP
#define IPU_HPP

#include <cstdint>

class EEInterruptController;

class IPU
{
  public:
    explicit IPU(EEInterruptController &interrupts);
    std::uint32_t control() const;
    bool writeControl(std::uint32_t value);

  private:
    EEInterruptController &interruptController;
};

#endif
