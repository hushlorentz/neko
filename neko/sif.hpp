#ifndef SIF_HPP
#define SIF_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "observation_order.hpp"

using SIFQuadword = std::array<std::uint32_t, 4>;

enum class SIFRegister : std::uint8_t
{
  MSCOM,
  SMCOM,
  MSFLAG,
  SMFLAG,
  CTRL
};

namespace SIFRegisterMap
{
  constexpr std::uint32_t APERTURE_SIZE = 0x80;
  bool decodeOffset(std::uint32_t offset, SIFRegister &reg);
}

enum class SIFSignal : std::uint8_t
{
  None,
  RequestIOP,
  RequestEE
};

class SIFSignalReceiver
{
  public:
    virtual ~SIFSignalReceiver() = default;
    virtual void requestSIFSignal(SIFSignal signal) noexcept = 0;
};

enum class SIFWriteOutcome : std::uint8_t
{
  Completed,
  UnsupportedControl
};

struct SIFWriteResult
{
  SIFWriteOutcome outcome = SIFWriteOutcome::Completed;
  SIFSignal signal = SIFSignal::None;
};

enum class SIFDirection : std::uint8_t
{
  SIF0,
  SIF1
};

enum class SIFEEDMAEvent : std::uint8_t
{
  Start,
  Complete,
  BusError,
  Abort
};

enum class SIFTransferOutcome : std::uint8_t
{
  Completed,
  Wait
};

enum class SIFPayloadBoundary : std::uint8_t
{
  Continuing,
  Final
};

class SIF final
{
  public:
    static constexpr std::size_t WORD_CAPACITY = 32;
    static constexpr std::uint32_t CONTROL_STATE_MASK = 0x61f0;

    SIF();
    void reset();

    std::uint32_t readEE(SIFRegister reg) const;
    std::uint32_t readIOP(SIFRegister reg) const;
    SIFWriteResult writeEE(SIFRegister reg, std::uint32_t value);
    SIFWriteResult writeIOP(SIFRegister reg, std::uint32_t value);
    std::uint32_t iopDoorbell() const;
    SIFWriteResult writeIOPDoorbell(std::uint32_t value);
    std::uint32_t control() const;
    void applyEEDMAEvent(SIFDirection direction, SIFEEDMAEvent event);

    SIFTransferOutcome pushSIF1Quadword(const SIFQuadword &words);
    SIFTransferOutcome pushSIF0Prologue(const SIFQuadword &words);
    SIFTransferOutcome pushSIF0PayloadWord(
      std::uint32_t word, SIFPayloadBoundary boundary);

    // Count is 1..4. Wait preserves output and all transport/packing state.
    SIFTransferOutcome peekWords(
      SIFDirection direction, SIFQuadword &output, std::size_t count) const;
    SIFTransferOutcome consumeWords(SIFDirection direction, std::size_t count);
    std::size_t wordCount(SIFDirection direction) const;
    std::size_t freeWordCount() const;
    std::uint32_t wordAt(SIFDirection direction, std::size_t index) const;
    const SIFQuadword &sif0PackingLatch() const;
    std::size_t sif0PackingCursor() const;

  private:
    friend class NekoSystem;
    friend class NekoSaveStateCodec;

    enum class ObservationPort : std::uint8_t { EE, IOP, IOPDoorbell };
    struct Observation
    {
      ObservationPort port;
      SIFRegister reg;
      std::uint32_t value;
      std::uint32_t before;
      std::uint32_t after;
      SIFWriteResult result;
      std::size_t sequence;
    };

    // At most two EE instructions and one IOP instruction write in a cycle.
    static constexpr std::size_t OBSERVATION_CAPACITY = 3;
    std::array<Observation, OBSERVATION_CAPACITY> observations = {};
    std::size_t observationCount = 0;
    NekoObservationOrder *observationOrder = nullptr;
    void enableObservations(NekoObservationOrder *order);
    void disableObservations();
    void recordWrite(ObservationPort port, SIFRegister reg, std::uint32_t value,
      std::uint32_t before, std::uint32_t after, SIFWriteResult result);
    SIFWriteResult mutateEE(SIFRegister reg, std::uint32_t value);
    SIFWriteResult mutateIOP(SIFRegister reg, std::uint32_t value);

    static constexpr std::uint8_t END = WORD_CAPACITY;

    struct Stream
    {
      std::uint8_t head = END;
      std::uint8_t tail = END;
      std::uint8_t count = 0;
    };

    static std::size_t directionIndex(SIFDirection direction);
    static void validateBeatSize(std::size_t count);
    std::uint32_t readRegister(SIFRegister reg) const;
    SIFTransferOutcome pushWords(
      SIFDirection direction, const SIFQuadword &words, std::size_t count);

    std::uint32_t mscom = 0;
    std::uint32_t smcom = 0;
    std::uint32_t msflag = 0;
    std::uint32_t smflag = 0;
    std::uint32_t controlRegister = 0;
    std::uint32_t iopDoorbellRegister = 0;
    std::array<std::uint32_t, WORD_CAPACITY> storage = {};
    std::array<std::uint8_t, WORD_CAPACITY> next = {};
    std::array<Stream, 2> streams = {};
    std::uint8_t freeHead = END;
    SIFQuadword packingLatch = {};
    std::uint8_t packingCursor = 0;
};

#endif
