#ifndef SPU2_CONTROLLER_HPP
#define SPU2_CONTROLLER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "observation_order.hpp"
#include "spu2_envelope.hpp"

enum class SPU2AccessStatus : std::uint8_t
{
  Accepted,
  Unsupported
};

struct SPU2ReadResult
{
  SPU2AccessStatus status = SPU2AccessStatus::Unsupported;
  std::uint16_t value = 0;
};

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

struct SPU2StereoSample
{
  std::int16_t left = 0;
  std::int16_t right = 0;
};

struct SPU2SampleTick
{
  bool sampleTick = false;
  std::array<SPU2InputSample, 2> samples{};
  std::array<SPU2StereoSample, 2> outputs{};
};

// Bounded SPU2 programming, waveform sources and input transfers; no host audio.
class SPU2Controller final
{
  public:
    static constexpr std::size_t SOUND_RAM_SIZE = 2 * 1024 * 1024;
    static constexpr std::size_t SOUND_RAM_HALFWORD_COUNT = SOUND_RAM_SIZE / 2;
    static constexpr std::size_t SPDIF_CONFIGURATION_COUNT = 5;
    static constexpr std::size_t MASTER_VOLUME_COUNT = 4;
    static constexpr std::size_t CORE_COUNT = 2;
    static constexpr std::size_t VOICE_COUNT_PER_CORE = 24;
    static constexpr std::size_t VOICE_VOLUME_COUNT = CORE_COUNT * VOICE_COUNT_PER_CORE * 2;
    static constexpr std::size_t VOICE_PITCH_COUNT = CORE_COUNT * VOICE_COUNT_PER_CORE;
    static constexpr std::size_t VOICE_ADSR_COUNT = CORE_COUNT * VOICE_COUNT_PER_CORE * 2;
    static constexpr std::size_t VOICE_START_ADDRESS_HALFWORD_COUNT =
      CORE_COUNT * VOICE_COUNT_PER_CORE * 2;
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
    SPU2SampleTick advanceSampleClock() noexcept;
    bool inputWriteRequested(std::size_t core) const;
    SPU2InputWriteState inputWriteState(std::size_t core) const;
    bool beginInputWrite(std::size_t core);
    void cancelInputWrite(std::size_t core);
    bool writeInputWord(std::size_t core, std::uint32_t value);
    std::uint16_t readSPDIFConfiguration(std::size_t index) const;
    bool writeSPDIFConfiguration(std::size_t index, std::uint16_t value);
    static bool isFixedVolumeControl(std::uint16_t value) noexcept;
    std::uint16_t readMasterVolumeControl(std::size_t index) const;
    bool writeMasterVolumeControl(std::size_t index, std::uint16_t value);
    std::uint16_t readVoiceVolumeControl(std::size_t index) const;
    bool writeVoiceVolumeControl(std::size_t index, std::uint16_t value);
    std::uint16_t readVoicePitchControl(std::size_t index) const;
    void writeVoicePitchControl(std::size_t index, std::uint16_t value);
    std::uint16_t readVoiceADSRControl(std::size_t index) const;
    void writeVoiceADSRControl(std::size_t index, std::uint16_t value);
    std::uint16_t readVoiceStartAddress(std::size_t index) const;
    bool writeVoiceStartAddress(std::size_t index, std::uint16_t value);
    std::uint16_t readVoiceLoopAddress(std::size_t index) const;
    void writeVoiceLoopAddressHalfword(std::size_t index, std::uint16_t value);
    SPU2ReadResult readVoiceNextAddress(std::size_t index) const;
    std::uint16_t readVoiceEnvelope(std::size_t index) const;
    std::uint16_t readVoiceEndMask(std::size_t index) const;
    std::uint16_t readKeyOn(std::size_t index) const;
    bool writeKeyOn(std::size_t index, std::uint16_t value);
    std::uint16_t readKeyOff(std::size_t index) const;
    void writeKeyOff(std::size_t index, std::uint16_t value);
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
    bool writeVoiceModeMask(std::size_t index, std::uint16_t value);
    // Provisional idle STATX: mode00 zero; mode01 ready only with both inputs inactive.
    SPU2ReadResult readCoreStatus(std::size_t core) const;
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
    friend struct SPU2VoiceTestAccess;
    struct VoiceState
    {
      SPU2Envelope::State envelope;
      std::uint8_t age = 0;
      std::uint16_t fraction = 0;
      std::int16_t history1 = 0;
      std::int16_t history2 = 0;
      std::uint32_t nextAddress = 0;
      std::uint32_t loopAddress = 0;
      bool loopOverride = false;
      std::uint16_t header = 0;
      bool headerValid = false;
      std::uint8_t fifoHead = 0;
      std::uint8_t fifoCount = 0;
      std::array<std::int16_t, 32> fifo{};
    };
    bool startVoice(std::size_t index) noexcept;
    void stopVoice(VoiceState &voice) noexcept;
    void writeVoiceLoopAddress(std::size_t index, std::uint32_t address) noexcept;
    void refillVoice(std::size_t index) noexcept;
    std::int16_t advanceVoice(std::size_t index) noexcept;
    std::array<std::int16_t, 4> renderVoiceMix(std::size_t core) noexcept;
    SPU2StereoSample renderCoreOutput(std::size_t core,
      const std::array<std::int16_t, 4> &mix, const SPU2InputSample &input) const noexcept;
    void consumeCommands(std::size_t core) noexcept;
    bool isVoiceActive(std::size_t index) const noexcept;
    bool hasActiveVoices() const noexcept;
    bool hasSupportedVoiceMode(std::size_t index) const noexcept;
    bool hasValidVoiceState() const noexcept;
    bool hasValidCommandState() const noexcept;
    struct Observation
    {
      enum class Kind : std::uint8_t
      {
        Input, KeyOffConsumed, KeyOnConsumed, KeyOn, KeyOff,
        Endpoint, EnvelopePhase, EnvelopeZero
      };
      Kind kind = Kind::Input;
      std::uint8_t core = 0;
      std::uint8_t voice = 0;
      std::uint32_t mask = 0;
      SPU2Envelope::Phase beforePhase = SPU2Envelope::Phase::Stopped;
      SPU2Envelope::Phase afterPhase = SPU2Envelope::Phase::Stopped;
      std::uint16_t beforeLevel = 0;
      std::uint16_t afterLevel = 0;
      std::uint16_t samplePosition = 0;
      SPU2InputWriteState before;
      SPU2InputWriteState after;
      std::size_t sequence = 0;
    };
    // Per voice: KOFF, KON, endpoint, envelope transition; four commands and
    // eight existing PCM/DMA/CPU input transitions per IOP pulse.
    std::array<Observation, 4 * CORE_COUNT * VOICE_COUNT_PER_CORE + 4 + 8> observations{};
    std::size_t observationCount = 0;
    bool observationsEnabled = false;
    NekoObservationOrder *observationOrder = nullptr;
    void enableObservations(NekoObservationOrder *order) noexcept;
    void disableObservations() noexcept;
    void clearObservations() noexcept;
    Observation &appendObservation(Observation::Kind kind, std::size_t core) noexcept;
    void recordCommand(Observation::Kind kind, std::size_t core, std::uint32_t mask) noexcept;
    void recordVoice(Observation::Kind kind, std::size_t index,
      const SPU2Envelope::State &before) noexcept;
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
    bool hasInactiveInputs() const;
    static std::uint16_t voiceStartAddressMask(std::size_t index) noexcept;
    std::vector<std::uint8_t> soundRAM;
    std::uint16_t inputClockPhase = 0;
    std::uint16_t inputReadPosition = 0;
    std::array<InputState, CORE_COUNT> inputStates{};
    // MODE, MEDIA, COPY PROTECTION, unknown 0x7ca, OUTPUT.
    std::array<std::uint16_t, SPDIF_CONFIGURATION_COUNT> spdifConfiguration{};
    // Core 0 left/right, core 1 left/right; programmed controls, not current gain.
    std::array<std::uint16_t, MASTER_VOLUME_COUNT> masterVolumeControls{};
    // Per core, voices 0-23 left/right; programmed controls, not current gain.
    std::array<std::uint16_t, VOICE_VOLUME_COUNT> voiceVolumeControls{};
    // Per core, voices 0-23; full raw pitch images, not effective sample steps.
    std::array<std::uint16_t, VOICE_PITCH_COUNT> voicePitchControls{};
    // Per core, voices 0-23 ADSR1/2; raw images, not live envelope state.
    std::array<std::uint16_t, VOICE_ADSR_COUNT> voiceADSRControls{};
    // Per core, voices 0-23 high/low; normalized SSA, not a live decoder cursor.
    std::array<std::uint16_t, VOICE_START_ADDRESS_HALFWORD_COUNT> voiceStartAddresses{};
    std::array<VoiceState, VOICE_PITCH_COUNT> voices{};
    std::array<std::uint32_t, CORE_COUNT> voiceEndMasks{{0xffffff, 0xffffff}};
    std::array<std::uint16_t, CORE_COUNT * 2> keyOnImages{};
    std::array<std::uint16_t, CORE_COUNT * 2> keyOffImages{};
    std::array<std::uint32_t, CORE_COUNT> pendingKeyOn{};
    std::array<std::uint32_t, CORE_COUNT> pendingKeyOff{};
    std::array<std::uint16_t, CORE_COUNT> coreAttributes{};
    // Per core: VMIXL, VMIXEL, VMIXR, VMIXER; voices 0-15 then 16-23.
    // Raw halfwords, including unused upper bits; gates decode only voices 0-23.
    std::array<std::uint16_t, VOICE_MIX_HALFWORD_COUNT> voiceMix{};
    // Raw MMIX images; core0 external and unsupported wet gates have no output source.
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
