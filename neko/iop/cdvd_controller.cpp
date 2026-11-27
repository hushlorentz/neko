#include "cdvd_controller.hpp"

#include <algorithm>
#include <stdexcept>

constexpr std::uint64_t CDVDController::RTC_PERIOD_SECONDS;
constexpr std::uint64_t CDVDController::RTC_PERIOD_TICKS;

namespace
{
  constexpr std::array<std::uint8_t, 4> bootRomName = {{2, 0, 0x41, 0x43}};
  constexpr std::array<std::uint8_t, 3> openConfigRequest = {{0, 1, 2}};
  struct ModelProfile
  {
    std::uint8_t id;
    std::array<std::uint8_t, 16> bytes;
  };
  // Approved synthetic diagnostic identity, not measured console NVM.
  constexpr ModelProfile modelProfile = {
    1, {{'S', 'C', 'P', 'H', '-', '7', '0', '0', '1', '2'}}
  };

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

  unsigned fromBCD(std::uint8_t value) noexcept
  {
    return (value >> 4) * 10 + (value & 15);
  }

  bool isBCDInRange(std::uint8_t value, unsigned minimum, unsigned maximum) noexcept
  {
    return (value & 15) <= 9 && (value >> 4) <= 9 &&
      fromBCD(value) >= minimum && fromBCD(value) <= maximum;
  }
}

CDVDReadResult CDVDController::readPassiveByte(
  std::uint32_t address) const noexcept
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
    default:
      return {};
  }
}

CDVDReadResult CDVDController::readByte(std::uint32_t address) noexcept
{
  const auto result = readPassiveByte(address);
  if (result.status == CDVDAccessStatus::Accepted)
  {
    return result;
  }
  if (address == 0x1f402018)
  {
    return {CDVDAccessStatus::Accepted, consumeResult()};
  }
  return {};
}

CDVDAccessStatus CDVDController::writeByte(
  std::uint32_t address, std::uint8_t value)
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
    case 0x1f402017:
      return appendSubcommandParameter(value);
    default:
      return CDVDAccessStatus::Unsupported;
  }
}

CDVDAccessStatus CDVDController::acceptNormalCommand(
  std::uint8_t opcode)
{
  if (opcode != 0 || normalPending)
  {
    return CDVDAccessStatus::Unsupported;
  }
  normalPending = true;
  recordCommand(ObservationKind::Accepted, Transport::Normal,
    SubcommandPhase::Idle, deviceCause);
  return CDVDAccessStatus::Accepted;
}

CDVDAccessStatus CDVDController::acceptSubcommand(std::uint8_t opcode)
{
  if (subcommandPhase != SubcommandPhase::Idle ||
      !isValidSubcommandRequest(opcode))
  {
    return CDVDAccessStatus::Unsupported;
  }
  captureConfigRequest(opcode);
  if (opcode == 0x17)
  {
    modelRequest = subcommandParameters[0] == 0 ?
      ModelRequest::FirstHalfV1 : ModelRequest::SecondHalfV1;
  }
  lastSubcommand = opcode;
  clearSubcommandParameters();
  subcommandPhase = SubcommandPhase::Pending;
  recordCommand(ObservationKind::Accepted, Transport::Subcommand,
    SubcommandPhase::Idle, deviceCause);
  return CDVDAccessStatus::Accepted;
}

bool CDVDController::isValidSubcommandRequest(std::uint8_t opcode) const noexcept
{
  switch (opcode)
  {
    case 3:
      return subcommandParameterCount == 1 && subcommandParameters[0] == 0;
    case 0x24:
      return subcommandParameterCount == 1 && subcommandParameters[0] == 1;
    case 0x17:
      return subcommandParameterCount == 1 &&
        (subcommandParameters[0] == 0 || subcommandParameters[0] == 8);
    case 8:
    case 0x15:
    case 0x1e:
    case 0x22:
    case 0x36:
    case 0x43:
      return subcommandParameterCount == 0;
    case 0x41:
      return subcommandParameterCount == 0 &&
        configSession == ConfigSession::Open && configNextBlock < 2;
    case 0x1a:
      return subcommandParameterCount == bootRomName.size() &&
        std::equal(bootRomName.begin(), bootRomName.end(), subcommandParameters.begin());
    case 0x40:
      return subcommandParameterCount == openConfigRequest.size() &&
        std::equal(openConfigRequest.begin(), openConfigRequest.end(),
          subcommandParameters.begin());
    default:
      return false;
  }
}

std::size_t CDVDController::subcommandResultSize(std::uint8_t opcode) noexcept
{
  switch (opcode)
  {
    case 3:
      return 4;
    case 8:
      return 8;
    case 0x17:
      return 9;
    case 0x1e:
      return 5;
    case 0x15:
    case 0x1a:
    case 0x24:
    case 0x40:
    case 0x43:
      return 1;
    case 0x41:
      return 16;
    case 0x22:
      return 10;
    case 0x36:
      return 15;
    default:
      return 0;
  }
}

CDVDController::SubcommandResult CDVDController::versionResult() noexcept
{
  return {{3, 6, 2, 0}};
}

CDVDController::SubcommandResult CDVDController::forbidDVDPResult() noexcept
{
  // Approved compatibility reply, not a DVD-read permission transition.
  return {{5}};
}

CDVDController::SubcommandResult CDVDController::bootCertifyResult() noexcept
{
  // Approved compatibility acknowledgement, not actual certification.
  return {{1}};
}

CDVDController::SubcommandResult CDVDController::wakeUpTimeResult() noexcept
{
  // Approved opaque compatibility data, not a disabled timer or hardware default.
  return {};
}

CDVDController::SubcommandResult CDVDController::remote2ReadResult() noexcept
{
  // Approved opaque compatibility reply, not measured remote state.
  return {{0, 0x14}};
}

CDVDController::SubcommandResult CDVDController::remoteBypassResult() noexcept
{
  // Approved opaque compatibility reply, not a SIRCS-to-SIO2 mode transition.
  return {};
}

CDVDController::SubcommandResult CDVDController::openConfigResult() noexcept
{
  // Opaque diagnostic-session acknowledgement, not physical-console success.
  return {};
}

CDVDController::SubcommandResult CDVDController::configPacket(
  std::uint8_t block) noexcept
{
  // Approved opaque diagnostic input, not measured or default console settings.
  constexpr std::array<SubcommandResult, 2> packets = {{
    {{0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
      0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x58}},
    {{0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
      0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x48}}
  }};
  return packets[block];
}

void CDVDController::captureConfigRequest(std::uint8_t opcode) noexcept
{
  switch (opcode)
  {
    case 0x40:
      configRequest = ConfigRequest::OpenV1;
      break;
    case 0x41:
      configRequest = ConfigRequest::ReadV1;
      configRequestBlock = configNextBlock;
      break;
    case 0x43:
      configRequest = ConfigRequest::Close;
      break;
    default:
      break;
  }
}

void CDVDController::completeConfigRequest() noexcept
{
  switch (configRequest)
  {
    case ConfigRequest::OpenV1:
      configSession = ConfigSession::Open;
      configProfile = 1;
      configNextBlock = 0;
      break;
    case ConfigRequest::ReadV1:
      configNextBlock = static_cast<std::uint8_t>(configRequestBlock + 1);
      break;
    case ConfigRequest::Close:
      configSession = ConfigSession::Closed;
      configProfile = 0;
      configNextBlock = 0;
      break;
    case ConfigRequest::None:
      break;
  }
  configRequest = ConfigRequest::None;
  configRequestBlock = 0;
}

bool CDVDController::isValidConfigState() const noexcept
{
  const bool closed = configSession == ConfigSession::Closed;
  const bool open = configSession == ConfigSession::Open;
  if ((!closed && !open) ||
      (closed && (configProfile != 0 || configNextBlock != 0)) ||
      (open && (configProfile != 1 || configNextBlock > 2)))
  {
    return false;
  }
  if (subcommandPhase == SubcommandPhase::Pending)
  {
    switch (lastSubcommand)
    {
      case 0x40:
        return configRequest == ConfigRequest::OpenV1 && configRequestBlock == 0;
      case 0x41:
        return configRequest == ConfigRequest::ReadV1 && open &&
          configNextBlock < 2 && configRequestBlock == configNextBlock;
      case 0x43:
        return configRequest == ConfigRequest::Close && configRequestBlock == 0;
      default:
        return configRequest == ConfigRequest::None && configRequestBlock == 0;
    }
  }
  if (configRequest != ConfigRequest::None || configRequestBlock != 0)
  {
    return false;
  }
  switch (lastSubcommand)
  {
    case 0:
      return subcommandPhase == SubcommandPhase::Idle && closed;
    case 0x40:
      return open && configNextBlock == 0;
    case 0x41:
      return open && configNextBlock >= 1;
    case 0x43:
      return closed;
    default:
      return true;
  }
}

std::uint8_t CDVDController::modelProfileId() noexcept
{
  return modelProfile.id;
}

bool CDVDController::isValidModelState() const noexcept
{
  if (lastSubcommand == 0x17 && subcommandPhase != SubcommandPhase::Idle)
  {
    return modelRequest == ModelRequest::FirstHalfV1 ||
      modelRequest == ModelRequest::SecondHalfV1;
  }
  return modelRequest == ModelRequest::None;
}

CDVDController::SubcommandResult CDVDController::modelResult(ModelRequest request)
{
  std::size_t offset;
  switch (request)
  {
    case ModelRequest::FirstHalfV1:
      offset = 0;
      break;
    case ModelRequest::SecondHalfV1:
      offset = 8;
      break;
    default:
      throw std::logic_error("CD/DVD model reply has no captured request.");
  }
  SubcommandResult snapshot = {};
  std::copy_n(modelProfile.bytes.begin() + offset, 8, snapshot.begin() + 1);
  return snapshot;
}

CDVDController::SubcommandResult CDVDController::regionResult() noexcept
{
  // Approved compatibility profile, not measured SCPH-70012 NVM contents.
  return {{0, 8, 0, 0x41, 0x41, 0x65, 0x6e, 0x67, 0x41, 0x55, 0, 0, 0, 0, 0}};
}

CDVDController::SubcommandResult CDVDController::subcommandResult() const
{
  switch (lastSubcommand)
  {
    case 3:
      return versionResult();
    case 8:
      return clockResult();
    case 0x15:
      return forbidDVDPResult();
    case 0x17:
      return modelResult(modelRequest);
    case 0x1a:
      return bootCertifyResult();
    case 0x1e:
      return remote2ReadResult();
    case 0x22:
      return wakeUpTimeResult();
    case 0x24:
      return remoteBypassResult();
    case 0x36:
      return regionResult();
    case 0x40:
    case 0x43:
      return openConfigResult();
    case 0x41:
      return configPacket(configRequestBlock);
    default:
      throw std::logic_error("CD/DVD pending subcommand is unsupported.");
  }
}

bool CDVDController::isValidSubcommandResult(std::uint8_t opcode,
  const SubcommandResult &snapshot) noexcept
{
  switch (opcode)
  {
    case 3:
      return snapshot == versionResult();
    case 8:
      return isValidClockResult(snapshot);
    case 0x15:
      return snapshot == forbidDVDPResult();
    case 0x17:
      return isValidModelState() && snapshot == modelResult(modelRequest);
    case 0x1a:
      return snapshot == bootCertifyResult();
    case 0x1e:
      return snapshot == remote2ReadResult();
    case 0x22:
      return snapshot == wakeUpTimeResult();
    case 0x24:
      return snapshot == remoteBypassResult();
    case 0x36:
      return snapshot == regionResult();
    case 0x40:
    case 0x43:
      return snapshot == openConfigResult();
    case 0x41:
      return configSession == ConfigSession::Open &&
        configNextBlock >= 1 && configNextBlock <= 2 &&
        snapshot == configPacket(static_cast<std::uint8_t>(configNextBlock - 1));
    default:
      return false;
  }
}

CDVDAccessStatus CDVDController::appendSubcommandParameter(
  std::uint8_t value) noexcept
{
  if (subcommandPhase != SubcommandPhase::Idle ||
      subcommandParameterCount >= subcommandParameters.size())
  {
    return CDVDAccessStatus::Unsupported;
  }
  subcommandParameters[subcommandParameterCount++] = value;
  return CDVDAccessStatus::Accepted;
}

void CDVDController::clearSubcommandParameters() noexcept
{
  subcommandParameters.fill(0);
  subcommandParameterCount = 0;
}

void CDVDController::advanceMasterTick() noexcept
{
  ++rtcTicks;
  if (rtcTicks == RTC_PERIOD_TICKS)
  {
    rtcTicks = 0;
  }
}

CDVDNormalCompletion CDVDController::completeNormalCommand()
{
  if (!normalPending)
  {
    return {};
  }
  normalPending = false;
  const auto beforeCause = deviceCause;
  const auto newCauses = static_cast<std::uint8_t>(1 & ~deviceCause);
  deviceCause |= 1;
  recordCommand(ObservationKind::Completed, Transport::Normal,
    SubcommandPhase::Pending, beforeCause);
  return {true, newCauses};
}

bool CDVDController::completeSubcommand()
{
  if (subcommandPhase != SubcommandPhase::Pending)
  {
    return false;
  }
  result = subcommandResult();
  completeConfigRequest();
  subcommandPhase = SubcommandPhase::Result;
  recordCommand(ObservationKind::Completed, Transport::Subcommand,
    SubcommandPhase::Pending, deviceCause);
  return true;
}

std::uint8_t CDVDController::consumeResult() noexcept
{
  if (subcommandPhase != SubcommandPhase::Result)
  {
    return 0;
  }
  const auto byte = result[resultCursor++];
  if (resultCursor == subcommandResultSize(lastSubcommand))
  {
    clearResult();
    modelRequest = ModelRequest::None;
    subcommandPhase = SubcommandPhase::Idle;
  }
  return byte;
}

CDVDController::SubcommandResult CDVDController::clockResult() const noexcept
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

bool CDVDController::isValidClockResult(
  const SubcommandResult &snapshot) noexcept
{
  return snapshot[0] == 0 && snapshot[4] == 0 &&
    isBCDInRange(snapshot[1], 0, 59) &&
    isBCDInRange(snapshot[2], 0, 59) &&
    isBCDInRange(snapshot[3], 0, 23) &&
    isBCDInRange(snapshot[5], 1, 31) &&
    isBCDInRange(snapshot[6], 1, 12) &&
    isBCDInRange(snapshot[7], 0, 99) &&
    fromBCD(snapshot[5]) <=
      daysInMonth(2000 + fromBCD(snapshot[7]), fromBCD(snapshot[6])) &&
    std::all_of(snapshot.begin() + 8, snapshot.end(),
      [](std::uint8_t byte) { return byte == 0; });
}

void CDVDController::reset() noexcept
{
  clearObservations();
  rtcTicks = 0;
  normalPending = false;
  deviceCause = 0;
  lastSubcommand = 0;
  subcommandPhase = SubcommandPhase::Idle;
  clearResult();
  clearSubcommandParameters();
  configSession = ConfigSession::Closed;
  configProfile = 0;
  configNextBlock = 0;
  configRequest = ConfigRequest::None;
  configRequestBlock = 0;
  modelRequest = ModelRequest::None;
}

void CDVDController::enableObservations(NekoObservationOrder *order) noexcept
{
  clearObservations();
  observationOrder = order;
  observationsEnabled = true;
}

void CDVDController::disableObservations() noexcept
{
  clearObservations();
  observationsEnabled = false;
  observationOrder = nullptr;
}

void CDVDController::clearObservations() noexcept
{
  observationCount = 0;
}

void CDVDController::recordCommand(ObservationKind kind, Transport transport,
  SubcommandPhase beforePhase, std::uint8_t beforeCause)
{
  if (!observationsEnabled)
  {
    return;
  }
  if (observationCount >= observations.size())
  {
    throw std::logic_error("CD/DVD observation capacity exceeded.");
  }
  auto &event = observations[observationCount];
  event = {};
  event.kind = kind;
  event.transport = transport;
  event.opcode = transport == Transport::Normal ? 0 : lastSubcommand;
  event.beforePhase = beforePhase;
  event.afterPhase = transport == Transport::Normal
    ? (normalPending ? SubcommandPhase::Pending : SubcommandPhase::Idle)
    : subcommandPhase;
  event.beforeCause = beforeCause;
  event.afterCause = deviceCause;
  event.rtcTicks = rtcTicks;
  if (kind == ObservationKind::Completed && transport == Transport::Subcommand)
  {
    event.result = result;
  }
  event.sequence =
    observationOrder != nullptr ? observationOrder->next++ : observationCount;
  ++observationCount;
}
