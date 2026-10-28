#ifndef CDVD_CONTROLLER_HPP
#define CDVD_CONTROLLER_HPP

#include <array>
#include <cstdint>

#include "clock_rates.hpp"

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
      std::uint32_t address, std::uint8_t value) noexcept;
    void advanceMasterTick() noexcept;
    // Called before the next IOP instruction, never after its accepting store.
    CDVDNormalCompletion completeNormalCommand() noexcept;
    bool completeSubcommand() noexcept;
    void reset() noexcept;
    std::uint64_t stateHash() const;

  private:
    friend class NekoSaveStateCodec;
    friend struct CDVDControllerTestAccess;

    enum class SubcommandPhase : std::uint8_t
    {
      Idle,
      Pending,
      Result
    };

    static constexpr std::uint64_t RTC_PERIOD_SECONDS = UINT64_C(36525) * 86400;
    static_assert(RTC_PERIOD_SECONDS <= UINT64_MAX / PS2Clock::MASTER_HZ,
      "The 2000-2099 RTC master-tick period must fit in uint64.");
    static constexpr std::uint64_t RTC_PERIOD_TICKS =
      RTC_PERIOD_SECONDS * PS2Clock::MASTER_HZ;

    CDVDAccessStatus acceptNormalCommand(std::uint8_t opcode) noexcept;
    CDVDAccessStatus acceptSubcommand(std::uint8_t opcode) noexcept;
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
