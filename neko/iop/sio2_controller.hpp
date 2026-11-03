#ifndef SIO2_CONTROLLER_HPP
#define SIO2_CONTROLLER_HPP

#include <cstdint>

enum class SIO2AccessStatus : std::uint8_t
{
  Accepted,
  Unsupported
};

// Provisional passive control profile, without transport or active reset.
class SIO2Controller final
{
  public:
    void reset() noexcept;
    std::uint32_t readControl() const noexcept;
    SIO2AccessStatus writeControl(std::uint32_t value) noexcept;

  private:
    friend class NekoSaveStateCodec;
    static bool supportsControl(std::uint32_t value) noexcept;
    std::uint32_t control = 0;
};

#endif
