#ifndef SPU2_CONTROLLER_HPP
#define SPU2_CONTROLLER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

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

// Bounded SPU2 programming; no audio engine or volume sweeps.
class SPU2Controller final
{
  public:
    static constexpr std::size_t SOUND_RAM_SIZE = 2 * 1024 * 1024;
    static constexpr std::size_t SOUND_RAM_HALFWORD_COUNT = SOUND_RAM_SIZE / 2;
    static constexpr std::size_t SPDIF_CONFIGURATION_COUNT = 5;
    static constexpr std::size_t MASTER_VOLUME_COUNT = 4;
    static constexpr std::size_t CORE_COUNT = 2;
    static constexpr std::size_t VOICE_MIX_HALFWORD_COUNT = 16;
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
    bool beginInputWrite(std::size_t core);
    void cancelInputWrite(std::size_t core);
    bool writeInputWord(std::size_t core, std::uint32_t value);
    std::uint16_t readSPDIFConfiguration(std::size_t index) const;
    bool writeSPDIFConfiguration(std::size_t index, std::uint16_t value);
    static bool isFixedMasterVolumeControl(std::uint16_t value) noexcept;
    std::uint16_t readMasterVolumeControl(std::size_t index) const;
    bool writeMasterVolumeControl(std::size_t index, std::uint16_t value);
    // Retained bit0/bits14-15 ATTR; changes reject while this input is selected.
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
    // STATX zero is supported only before the core's first input selection.
    bool hasQuiescentCoreStatus(std::size_t core) const;
    // Provisional KOFF effect; replace before voice programming or key-on.
    void keyOffUnprogrammedVoices() const noexcept;
    // Own-core PCM input selection; ADMAS readback remains unsupported.
    static bool isSupportedAutoDMAWriteSelection(
      std::size_t core, std::uint16_t value) noexcept;
    std::uint16_t readAutoDMAWriteSelection(std::size_t core) const;
    bool writeAutoDMAWriteSelection(std::size_t core, std::uint16_t value);
    // Provisional 0x1f900b60/0x1f900b62 zero initialization; no state or effects.
    bool writeUnknownInitialization(std::uint16_t value) const noexcept;

  private:
    friend class NekoSaveStateCodec;
    friend class IOPDMAController;
    struct InputState
    {
      bool everSelected = false;
      bool writeOpen = false;
      std::uint8_t fillHalf = 0;
      std::uint16_t fillWords = 0;
    };
    bool hasSupportedInputProfile(std::size_t core) const noexcept;
    bool hasValidInputState() const noexcept;
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
    // Per core: raw TSA high/low images; no decoded address or active cursor.
    std::array<std::uint16_t, TRANSFER_START_HALFWORD_COUNT> transferStartAddress{};
    std::array<std::uint16_t, CORE_COUNT> autoDMAWriteSelection{};
};

#endif
