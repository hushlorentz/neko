#ifndef SPU2_CONTROLLER_HPP
#define SPU2_CONTROLLER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "observation_order.hpp"

struct SPU2InputWriteState
{
  bool selected = false;
  bool writeOpen = false;
  bool requested = false;
  std::uint8_t fillHalf = 0;
  std::uint8_t readHalf = 0;
  std::uint16_t fillWords = 0;
};

struct SPU2InputSample
{
  bool consumed = false;
  std::uint16_t left = 0;
  std::uint16_t right = 0;
};

struct SPU2InputTick
{
  bool sampleTick = false;
  std::array<SPU2InputSample, 2> samples{};
};

// Bounded SPU2 programming and input transfers; no audio engine or volume sweeps.
class SPU2Controller final
{
  public:
    static constexpr std::size_t SOUND_RAM_SIZE = 2 * 1024 * 1024;
    static constexpr std::size_t SOUND_RAM_HALFWORD_COUNT = SOUND_RAM_SIZE / 2;
    static constexpr std::size_t SPDIF_CONFIGURATION_COUNT = 5;
    static constexpr std::size_t MASTER_VOLUME_COUNT = 4;
    static constexpr std::size_t CORE_COUNT = 2;
    static constexpr std::size_t VOICE_MIX_HALFWORD_COUNT = 16;
    static constexpr std::size_t VOICE_MODE_HALFWORD_COUNT = 8;
    static constexpr std::size_t EFFECT_INPUT_VOLUME_COUNT = 12;
    static constexpr std::size_t TRANSFER_START_HALFWORD_COUNT = 4;
    static constexpr std::uint16_t INPUT_CLOCK_PERIOD = 768;
    static constexpr std::uint16_t INPUT_SAMPLE_COUNT = 512;
    static constexpr std::uint16_t INPUT_HALF_WORD_COUNT = 256;
    SPU2Controller();
    void reset() noexcept;
    std::uint16_t readSoundRAMHalfword(std::uint32_t halfwordAddress) const;
    void writeSoundRAMHalfword(std::uint32_t halfwordAddress, std::uint16_t value);
    SPU2InputTick advanceInputClock() noexcept;
    bool inputWriteRequested(std::size_t core) const;
    SPU2InputWriteState inputWriteState(std::size_t core) const;
    bool beginInputWrite(std::size_t core);
    void cancelInputWrite(std::size_t core);
    bool writeInputWord(std::size_t core, std::uint32_t value);
    std::uint16_t readSPDIFConfiguration(std::size_t index) const;
    bool writeSPDIFConfiguration(std::size_t index, std::uint16_t value);
    static bool isFixedMasterVolumeControl(std::uint16_t value) noexcept;
    std::uint16_t readMasterVolumeControl(std::size_t index) const;
    bool writeMasterVolumeControl(std::size_t index, std::uint16_t value);
    // Retained bit0/bits14-15 and I/O mode; no FIFO, IRQ or DMA effects.
    static bool isSupportedCoreAttribute(std::uint16_t value) noexcept;
    std::uint16_t readCoreAttribute(std::size_t core) const;
    bool writeCoreAttribute(std::size_t core, std::uint16_t value);
    std::uint16_t readVoiceMix(std::size_t index) const;
    void writeVoiceMix(std::size_t index, std::uint16_t value);
    std::uint16_t readMainMix(std::size_t core) const;
    void writeMainMix(std::size_t core, std::uint16_t value);
    std::uint16_t readEffectInputVolume(std::size_t index) const;
    void writeEffectInputVolume(std::size_t index, std::uint16_t value);
    std::uint16_t readEffectEndAddressHigh(std::size_t core) const;
    void writeEffectEndAddressHigh(std::size_t core, std::uint16_t value);
    std::uint16_t readTransferStartAddress(std::size_t index) const;
    void writeTransferStartAddress(std::size_t index, std::uint16_t value);
    // Provisional synchronous PIO, only while both PCM inputs are inactive.
    bool writeTransferData(std::size_t core, std::uint16_t value);
    std::uint16_t readVoiceModeMask(std::size_t index) const;
    void writeVoiceModeMask(std::size_t index, std::uint16_t value);
    // Provisional STATX zero only with mode00 and this core's input inactive.
    bool hasInactiveCoreStatus(std::size_t core) const;
    // Provisional KOFF effect; replace before voice source/envelope or key-on.
    void keyOffUnprogrammedVoices() const noexcept;
    // Retained own-core PCM selection image, not live DMA status.
    static bool isSupportedAutoDMAWriteSelection(
      std::size_t core, std::uint16_t value) noexcept;
    std::uint16_t readAutoDMAWriteSelection(std::size_t core) const;
    bool writeAutoDMAWriteSelection(std::size_t core, std::uint16_t value);
    // Provisional 0x1f9007b8/7ba/7bc/7be and 0x1f900b60/b62 zero writes; no state or effects.
    bool writeUnknownInitialization(std::uint16_t value) const noexcept;

  private:
    friend class NekoSaveStateCodec;
    friend class IOPDMAController;
    friend class NekoSystem;
    friend struct SPU2ObservationTestAccess;
    struct Observation
    {
      std::uint8_t core = 0;
      SPU2InputWriteState before;
      SPU2InputWriteState after;
      std::size_t sequence = 0;
    };
    // Two sample switches, full-half + completion, and one CPU selection/open.
    std::array<Observation, 8> observations{};
    std::size_t observationCount = 0;
    bool observationsEnabled = false;
    NekoObservationOrder *observationOrder = nullptr;
    void enableObservations(NekoObservationOrder *order) noexcept;
    void disableObservations() noexcept;
    void clearObservations() noexcept;
    void recordInputState(std::size_t core, const SPU2InputWriteState &before) noexcept;
    struct InputState
    {
      bool everSelected = false;
      bool writeOpen = false;
      std::uint8_t fillHalf = 0;
      std::uint16_t fillWords = 0;
    };
    bool hasSupportedInputProfile(std::size_t core) const noexcept;
    bool hasValidInputState() const noexcept;
    bool hasInactiveInput(std::size_t core) const;
    std::vector<std::uint8_t> soundRAM;
    std::uint16_t inputClockPhase = 0;
    std::uint16_t inputReadPosition = 0;
    std::array<InputState, CORE_COUNT> inputStates{};
    // MODE, MEDIA, COPY PROTECTION, unknown 0x7ca, OUTPUT.
    std::array<std::uint16_t, SPDIF_CONFIGURATION_COUNT> spdifConfiguration{};
    // Core 0 left/right, core 1 left/right; programmed controls, not current gain.
    std::array<std::uint16_t, MASTER_VOLUME_COUNT> masterVolumeControls{};
    std::array<std::uint16_t, CORE_COUNT> coreAttributes{};
    // Per core: VMIXL, VMIXEL, VMIXR, VMIXER; voices 0-15 then 16-23.
    // Passive raw halfwords, including unused upper bits; no live voice gates.
    std::array<std::uint16_t, VOICE_MIX_HALFWORD_COUNT> voiceMix{};
    // Passive raw MMIX images, including unused bits; no live source gates.
    std::array<std::uint16_t, CORE_COUNT> mainMix{};
    // Per core: EVOLL/R, AVOLL/R, BVOLL/R; core0 AVOL has no input function.
    std::array<std::uint16_t, EFFECT_INPUT_VOLUME_COUNT> effectInputVolumes{};
    // Passive raw EEA high images; no decoded address or effect work area.
    std::array<std::uint16_t, CORE_COUNT> effectEndAddressHigh{};
    // Per core: raw programmed TSA high/low images, independent of PIO progress.
    std::array<std::uint16_t, TRANSFER_START_HALFWORD_COUNT> transferStartAddress{};
    std::array<std::uint32_t, CORE_COUNT> transferCursor{};
    // Per core: raw PMON low/high, NON low/high; no voice or noise execution.
    std::array<std::uint16_t, VOICE_MODE_HALFWORD_COUNT> voiceModeMasks{};
    std::array<std::uint16_t, CORE_COUNT> autoDMAWriteSelection{};
};

#endif
