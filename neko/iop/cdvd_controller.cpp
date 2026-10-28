#include "cdvd_controller.hpp"

constexpr std::uint64_t CDVDController::RTC_PERIOD_SECONDS;
constexpr std::uint64_t CDVDController::RTC_PERIOD_TICKS;

namespace
{
  bool isLeapYear(unsigned year) noexcept
  {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  }

  unsigned daysInMonth(unsigned year, unsigned month) noexcept
  {
    constexpr unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return days[month - 1] + (month == 2 && isLeapYear(year) ? 1 : 0);
  }

  std::uint8_t bcd(unsigned value) noexcept
  {
    return static_cast<std::uint8_t>((value / 10) * 16 + value % 10);
  }
}

CDVDReadResult CDVDController::readByte(std::uint32_t address) noexcept
{
  switch (address)
  {
    case 0x1f402004:
    case 0x1f402006:
    case 0x1f40200a:
    case 0x1f40200f:
      return {CDVDAccessStatus::Accepted, 0};
    case 0x1f402005:
      return {CDVDAccessStatus::Accepted,
        static_cast<std::uint8_t>(normalPending ? 0x80 : 0x40)};
    case 0x1f402008:
      return {CDVDAccessStatus::Accepted, deviceCause};
    case 0x1f402016:
      return {CDVDAccessStatus::Accepted, lastSubcommand};
    case 0x1f402017:
      return {CDVDAccessStatus::Accepted,
        static_cast<std::uint8_t>(subcommandPhase == SubcommandPhase::Pending ?
          0xc0 : (subcommandPhase == SubcommandPhase::Result ? 0 : 0x40))};
    case 0x1f402018:
      return {CDVDAccessStatus::Accepted, consumeResult()};
    default:
      return {};
  }
}

CDVDAccessStatus CDVDController::writeByte(
  std::uint32_t address, std::uint8_t value) noexcept
{
  switch (address)
  {
    case 0x1f402004:
      return acceptNormalCommand(value);
    case 0x1f402008:
      deviceCause = static_cast<std::uint8_t>(deviceCause & ~value);
      return CDVDAccessStatus::Accepted;
    case 0x1f402016:
      return acceptSubcommand(value);
    default:
      return CDVDAccessStatus::Unsupported;
  }
}

CDVDAccessStatus CDVDController::acceptNormalCommand(
  std::uint8_t opcode) noexcept
{
  if (opcode != 0 || normalPending)
  {
    return CDVDAccessStatus::Unsupported;
  }
  normalPending = true;
  return CDVDAccessStatus::Accepted;
}

CDVDAccessStatus CDVDController::acceptSubcommand(std::uint8_t opcode) noexcept
{
  if (opcode != 8 || subcommandPhase != SubcommandPhase::Idle)
  {
    return CDVDAccessStatus::Unsupported;
  }
  lastSubcommand = opcode;
  subcommandPhase = SubcommandPhase::Pending;
  return CDVDAccessStatus::Accepted;
}

void CDVDController::advanceMasterTick() noexcept
{
  ++rtcTicks;
  if (rtcTicks == RTC_PERIOD_TICKS)
  {
    rtcTicks = 0;
  }
}

CDVDNormalCompletion CDVDController::completeNormalCommand() noexcept
{
  if (!normalPending)
  {
    return {};
  }
  normalPending = false;
  const auto newCauses = static_cast<std::uint8_t>(1 & ~deviceCause);
  deviceCause |= 1;
  return {true, newCauses};
}

bool CDVDController::completeSubcommand() noexcept
{
  if (subcommandPhase != SubcommandPhase::Pending)
  {
    return false;
  }
  result = clockResult();
  subcommandPhase = SubcommandPhase::Result;
  return true;
}

std::uint8_t CDVDController::consumeResult() noexcept
{
  if (subcommandPhase != SubcommandPhase::Result)
  {
    return 0;
  }
  const auto byte = result[resultCursor++];
  if (resultCursor == result.size())
  {
    clearResult();
    subcommandPhase = SubcommandPhase::Idle;
  }
  return byte;
}

std::array<std::uint8_t, 8> CDVDController::clockResult() const noexcept
{
  auto seconds = rtcTicks / PS2Clock::MASTER_HZ;
  const auto second = static_cast<unsigned>(seconds % 60);
  seconds /= 60;
  const auto minute = static_cast<unsigned>(seconds % 60);
  seconds /= 60;
  const auto hour = static_cast<unsigned>(seconds % 24);
  auto day = static_cast<unsigned>(seconds / 24);
  unsigned year = 2000;
  for (;;)
  {
    const unsigned days = isLeapYear(year) ? 366 : 365;
    if (day < days)
    {
      break;
    }
    day -= days;
    ++year;
  }
  unsigned month = 1;
  while (day >= daysInMonth(year, month))
  {
    day -= daysInMonth(year, month);
    ++month;
  }
  return {{0, bcd(second), bcd(minute), bcd(hour), 0,
    bcd(day + 1), bcd(month), bcd(year - 2000)}};
}

void CDVDController::clearResult() noexcept
{
  result = {};
  resultCursor = 0;
}

void CDVDController::reset() noexcept
{
  rtcTicks = 0;
  normalPending = false;
  deviceCause = 0;
  lastSubcommand = 0;
  subcommandPhase = SubcommandPhase::Idle;
  clearResult();
}
