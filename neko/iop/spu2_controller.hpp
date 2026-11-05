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
    static constexpr std::size_t CORE_COUNT = 2;
    void reset() noexcept;
    std::uint16_t readSPDIFConfiguration(std::size_t index) const;
    void writeSPDIFConfiguration(std::size_t index, std::uint16_t value);
    static bool isFixedMasterVolumeControl(std::uint16_t value) noexcept;
    std::uint16_t readMasterVolumeControl(std::size_t index) const;
    bool writeMasterVolumeControl(std::size_t index, std::uint16_t value);
    // Provisional retained bit15-only ATTR images; no engine/reset effects.
    static bool isSupportedCoreAttribute(std::uint16_t value) noexcept;
    std::uint16_t readCoreAttribute(std::size_t core) const;
    bool writeCoreAttribute(std::size_t core, std::uint16_t value);
    // Provisional quiescent STATX projection; replace when transfers are modeled.
    std::uint16_t readUnprogrammedCoreStatus() const noexcept;
    // Provisional disabled Auto DMA initialization; no transfer effects.
    bool writeUnprogrammedAutoDMAControl(std::uint16_t value) const noexcept;
    // Provisional 0x1f900b60/0x1f900b62 zero initialization; no state or effects.
    bool writeUnknownInitialization(std::uint16_t value) const noexcept;

  private:
    friend class NekoSaveStateCodec;
    // MODE, MEDIA, COPY PROTECTION, unknown 0x7ca; not a full register bank.
    std::array<std::uint16_t, SPDIF_CONFIGURATION_COUNT> spdifConfiguration{};
    // Core 0 left/right, core 1 left/right; programmed controls, not current gain.
    std::array<std::uint16_t, MASTER_VOLUME_COUNT> masterVolumeControls{};
    std::array<std::uint16_t, CORE_COUNT> coreAttributes{};
};

#endif
