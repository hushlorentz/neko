#ifndef SPU2_CONTROLLER_HPP
#define SPU2_CONTROLLER_HPP

#include <array>
#include <cstddef>
#include <cstdint>

// Bounded SPU2 programming; no audio engine or volume sweeps.
class SPU2Controller final
{
  public:
    static constexpr std::size_t SPDIF_CONFIGURATION_COUNT = 4;
    static constexpr std::size_t MASTER_VOLUME_COUNT = 4;
    void reset() noexcept;
    std::uint16_t readSPDIFConfiguration(std::size_t index) const;
    void writeSPDIFConfiguration(std::size_t index, std::uint16_t value);
    static bool isFixedMasterVolumeControl(std::uint16_t value) noexcept;
    std::uint16_t readMasterVolumeControl(std::size_t index) const;
    bool writeMasterVolumeControl(std::size_t index, std::uint16_t value);
    // Provisional exact 0x1f900b60 zero initialization; no retained state or effects.
    bool writeUnknownB60Initialization(std::uint16_t value) const noexcept;

  private:
    friend class NekoSaveStateCodec;
    // MODE, MEDIA, COPY PROTECTION, unknown 0x7ca; not a full register bank.
    std::array<std::uint16_t, SPDIF_CONFIGURATION_COUNT> spdifConfiguration{};
    // Core 0 left/right, core 1 left/right; programmed controls, not current gain.
    std::array<std::uint16_t, MASTER_VOLUME_COUNT> masterVolumeControls{};
};

#endif
