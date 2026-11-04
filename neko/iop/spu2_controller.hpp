#ifndef SPU2_CONTROLLER_HPP
#define SPU2_CONTROLLER_HPP

#include <array>
#include <cstddef>
#include <cstdint>

// Provisional retained global S/PDIF configuration; no audio engine yet.
class SPU2Controller final
{
  public:
    static constexpr std::size_t SPDIF_CONFIGURATION_COUNT = 4;
    void reset() noexcept;
    std::uint16_t readSPDIFConfiguration(std::size_t index) const;
    void writeSPDIFConfiguration(std::size_t index, std::uint16_t value);

  private:
    friend class NekoSaveStateCodec;
    // MODE, MEDIA, COPY PROTECTION, unknown 0x7ca; not a full register bank.
    std::array<std::uint16_t, SPDIF_CONFIGURATION_COUNT> spdifConfiguration{};
};

#endif
