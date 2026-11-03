#ifndef SIO2_CONTROLLER_HPP
#define SIO2_CONTROLLER_HPP

#include <cstdint>

enum class SIO2AccessStatus : std::uint8_t
{
  Accepted,
  Unsupported
};

// Provisional initialization-only profile, without transport or active reset.
class SIO2Controller final
{
  public:
    void reset() noexcept;
    std::uint32_t readControl() const noexcept;
    SIO2AccessStatus writeStartupControl(std::uint32_t value) noexcept;

  private:
    friend class NekoSaveStateCodec;
    std::uint32_t control = 0;
};

#endif
