#ifndef CDVD_CONTROLLER_HPP
#define CDVD_CONTROLLER_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "clock_rates.hpp"
#include "observation_order.hpp"

enum class CDVDAccessStatus : std::uint8_t
{
  Accepted,
  Unsupported
};

struct CDVDReadResult
{
  CDVDAccessStatus status = CDVDAccessStatus::Unsupported;
  std::uint8_t value = 0;
};

struct CDVDNormalCompletion
{
  bool completed = false;
  std::uint8_t newCauses = 0;
};

// Provisional no-media N-NOP/ReadClock profile, not a general CD/DVD drive.
class CDVDController final
{
  public:
    CDVDReadResult readByte(std::uint32_t address) noexcept;
    CDVDAccessStatus writeByte(
      std::uint32_t address, std::uint8_t value);
    void advanceMasterTick() noexcept;
    // Called before the next IOP instruction, never after its accepting store.
    CDVDNormalCompletion completeNormalCommand();
    bool completeSubcommand();
    void reset() noexcept;
    std::uint64_t stateHash() const;

  private:
    friend class NekoSaveStateCodec;
    friend class NekoSystem;
    friend struct CDVDControllerTestAccess;
    friend struct CDVDObservationTestAccess;

    enum class SubcommandPhase : std::uint8_t
    {
      Idle,
      Pending,
      Result
    };

    enum class ObservationKind : std::uint8_t { Accepted, Completed };
    enum class Transport : std::uint8_t { Normal, Subcommand };
    struct Observation
    {
      ObservationKind kind = ObservationKind::Accepted;
      Transport transport = Transport::Normal;
      std::uint8_t opcode = 0;
      SubcommandPhase beforePhase = SubcommandPhase::Idle;
      SubcommandPhase afterPhase = SubcommandPhase::Idle;
      std::uint8_t beforeCause = 0;
      std::uint8_t afterCause = 0;
      std::uint64_t rtcTicks = 0;
      std::array<std::uint8_t, 8> result = {};
      std::size_t sequence = 0;
    };

    // Two completions plus one command accepted by the IOP instruction.
    static constexpr std::size_t OBSERVATION_CAPACITY = 3;
    std::array<Observation, OBSERVATION_CAPACITY> observations = {};
    std::size_t observationCount = 0;
    bool observationsEnabled = false;
    NekoObservationOrder *observationOrder = nullptr;

    void enableObservations(NekoObservationOrder *order = nullptr) noexcept;
    void disableObservations() noexcept;
    void clearObservations() noexcept;
    void recordCommand(ObservationKind kind, Transport transport,
      SubcommandPhase beforePhase, std::uint8_t beforeCause);

    static constexpr std::uint64_t RTC_PERIOD_SECONDS = UINT64_C(36525) * 86400;
    static_assert(RTC_PERIOD_SECONDS <= UINT64_MAX / PS2Clock::MASTER_HZ,
      "The 2000-2099 RTC master-tick period must fit in uint64.");
    static constexpr std::uint64_t RTC_PERIOD_TICKS =
      RTC_PERIOD_SECONDS * PS2Clock::MASTER_HZ;

    CDVDAccessStatus acceptNormalCommand(std::uint8_t opcode);
    CDVDAccessStatus acceptSubcommand(std::uint8_t opcode);
    std::uint8_t consumeResult() noexcept;
    std::array<std::uint8_t, 8> clockResult() const noexcept;
    static bool isValidClockResult(
      const std::array<std::uint8_t, 8> &snapshot) noexcept;
    void clearResult() noexcept;

    std::uint64_t rtcTicks = 0;
    bool normalPending = false;
    std::uint8_t deviceCause = 0;
    std::uint8_t lastSubcommand = 0;
    SubcommandPhase subcommandPhase = SubcommandPhase::Idle;
    std::array<std::uint8_t, 8> result = {};
    std::uint8_t resultCursor = 0;
};

#endif
