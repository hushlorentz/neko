#include "regression_trace.hpp"
#include "ee/ee_core.hpp"

#include <stdexcept>

namespace
{
  constexpr std::uint64_t FNV_OFFSET_BASIS =
    UINT64_C(14695981039346656037);
  constexpr std::uint64_t FNV_PRIME =
    UINT64_C(1099511628211);

  void hashU64(std::uint64_t *hash, std::uint64_t value)
  {
    for (std::uint8_t index = 0; index < 8; ++index)
    {
      *hash ^= static_cast<std::uint8_t>(value >> (index * 8));
      *hash *= FNV_PRIME;
    }
  }
}

NekoTraceFailureKind nekoTraceFailureKind(const NekoTraceEvent &event) noexcept
{
  using Kind = NekoTraceFailureKind;
  if (event.type == NekoTraceEventType::MemoryAccess)
  {
    if (event.subsystem == NekoTraceSubsystem::IOP &&
        (event.value3 & NekoIOPTraceMemory::OUTCOME_MASK) != 0)
    {
      return Kind::MemoryAccess;
    }
    if (event.subsystem == NekoTraceSubsystem::EE)
    {
      const auto translation = (event.value3 &
        NekoEETraceMemory::TRANSLATION_OUTCOME_MASK) >>
          NekoEETraceMemory::TRANSLATION_OUTCOME_SHIFT;
      const auto transfer = (event.value3 &
        NekoEETraceMemory::TRANSFER_OUTCOME_MASK) >>
          NekoEETraceMemory::TRANSFER_OUTCOME_SHIFT;
      if (translation >= static_cast<unsigned>(
            NekoEETraceMemory::TranslationOutcome::AddressErrorLoadOrFetch) ||
          transfer == static_cast<unsigned>(
            NekoEETraceMemory::TransferOutcome::PhysicalBusError))
      {
        return Kind::MemoryAccess;
      }
    }
  }
  if (event.subsystem == NekoTraceSubsystem::EE &&
      ((event.type == NekoTraceEventType::FaultContext &&
        (event.value3 & NekoEETraceFault::HOST_REJECTION) != 0) ||
       (event.type == NekoTraceEventType::ExceptionEntered &&
        (event.value0 == static_cast<unsigned>(EEException::ReservedInstruction) ||
         event.value0 == static_cast<unsigned>(EEException::CoprocessorUnusable)))))
  {
    return Kind::InstructionRejection;
  }
  if (event.subsystem == NekoTraceSubsystem::IOP &&
      ((event.type == NekoTraceEventType::StateChanged &&
        event.value2 == static_cast<unsigned>(NekoIOPTraceState::StopReason::UndefinedOperation)) ||
       (event.type == NekoTraceEventType::ExceptionEntered &&
        (event.value0 == 10 || event.value0 == 11))))
  {
    return Kind::InstructionRejection;
  }
  if (event.subsystem == NekoTraceSubsystem::SIF &&
      event.type == NekoTraceEventType::DMABeat &&
      ((event.value0 >> NekoSIFDMATrace::OUTCOME_SHIFT) & 255) ==
        static_cast<unsigned>(NekoSIFDMATrace::Outcome::Rejected))
  {
    return Kind::DMARejection;
  }
  if (event.subsystem == NekoTraceSubsystem::SPU2 &&
      event.type == NekoTraceEventType::DMABeat &&
      ((event.value0 >> NekoSPUTrace::OUTCOME_SHIFT) & 255) ==
        static_cast<unsigned>(NekoSPUTrace::Outcome::Rejected))
  {
    return Kind::DMARejection;
  }
  if (event.subsystem == NekoTraceSubsystem::SIO2 &&
      event.type == NekoTraceEventType::DMABeat &&
      ((event.value0 >> NekoSIO2Trace::OUTCOME_SHIFT) & 255) ==
        static_cast<unsigned>(NekoSPUTrace::Outcome::Rejected))
  {
    return Kind::DMARejection;
  }
  return Kind::None;
}

namespace
{
  void validateSPUIdentity(std::uint8_t channel, std::uint8_t core)
  {
    if (core > 1 || channel != (core == 0 ? 4 : 7))
    {
      throw std::invalid_argument("Unknown SPU trace channel/core identity.");
    }
  }

  std::uint64_t packSPUInput(const NekoSPUTrace::InputState &state) noexcept
  {
    return (state.writeOpen ? 1u : 0u) | (state.selected ? 2u : 0u) |
      (state.requested ? 4u : 0u) | (std::uint64_t{state.fillHalf} << 8) |
      (std::uint64_t{state.readHalf} << 16) |
      (std::uint64_t{state.fillWords} << 32);
  }

  NekoSPUTrace::InputState decodeSPUInput(std::uint64_t value)
  {
    NekoSPUTrace::InputState state;
    state.writeOpen = (value & 1) != 0;
    state.selected = (value & 2) != 0;
    state.requested = (value & 4) != 0;
    state.fillHalf = static_cast<std::uint8_t>(value >> 8);
    state.readHalf = static_cast<std::uint8_t>(value >> 16);
    state.fillWords = static_cast<std::uint16_t>(value >> 32);
    if (state.fillHalf > 1 || state.readHalf > 1 || state.fillWords > 256)
    {
      throw std::invalid_argument("Unknown SPU input trace buffer position.");
    }
    return state;
  }
}

NekoTraceEvent nekoSPUDMABeatEvent(const NekoSPUTrace::Beat &beat) noexcept
{
  return {0, NekoTraceSubsystem::SPU2, NekoTraceEventType::DMABeat,
    beat.channel | (std::uint64_t{beat.core} << 8) |
      (static_cast<std::uint64_t>(beat.outcome) << 16) |
      (static_cast<std::uint64_t>(beat.busStatus) << 24),
    beat.sourceAddress,
    beat.remainingBefore | (std::uint64_t{beat.remainingAfter} << 32),
    beat.targetHalf | (std::uint64_t{beat.targetWord} << 8)};
}

NekoSPUTrace::Beat nekoDecodeSPUDMABeat(const NekoTraceEvent &event)
{
  if (event.subsystem != NekoTraceSubsystem::SPU2 ||
      event.type != NekoTraceEventType::DMABeat)
  {
    throw std::invalid_argument("Not a SPU DMA beat trace event.");
  }
  NekoSPUTrace::Beat beat;
  beat.channel = static_cast<std::uint8_t>(event.value0);
  beat.core = static_cast<std::uint8_t>(event.value0 >> 8);
  validateSPUIdentity(beat.channel, beat.core);
  beat.outcome = static_cast<NekoSPUTrace::Outcome>((event.value0 >> 16) & 255);
  beat.busStatus = static_cast<NekoSPUTrace::BusStatus>((event.value0 >> 24) & 255);
  beat.sourceAddress = static_cast<std::uint32_t>(event.value1);
  beat.remainingBefore = static_cast<std::uint32_t>(event.value2);
  beat.remainingAfter = static_cast<std::uint32_t>(event.value2 >> 32);
  beat.targetHalf = static_cast<std::uint8_t>(event.value3);
  beat.targetWord = static_cast<std::uint16_t>(event.value3 >> 8);
  if (static_cast<unsigned>(beat.outcome) > 3 ||
      static_cast<unsigned>(beat.busStatus) > 3 || beat.targetHalf > 1 ||
      beat.targetWord > 256 ||
      ((beat.outcome == NekoSPUTrace::Outcome::Rejected) ==
        (beat.busStatus == NekoSPUTrace::BusStatus::None)))
  {
    throw std::invalid_argument("Unknown SPU DMA beat trace metadata.");
  }
  return beat;
}

NekoTraceEvent nekoSIO2DMABeatEvent(const NekoSIO2Trace::Beat &beat) noexcept
{
  return {0, NekoTraceSubsystem::SIO2, NekoTraceEventType::DMABeat,
    beat.channel |
      (static_cast<std::uint64_t>(beat.outcome) << NekoSIO2Trace::OUTCOME_SHIFT) |
      (static_cast<std::uint64_t>(beat.busStatus) << NekoSIO2Trace::BUS_STATUS_SHIFT),
    beat.sourceAddress | (std::uint64_t{beat.sourceValue} << 32),
    beat.remainingBefore | (std::uint64_t{beat.remainingAfter} << 8),
    beat.stagedBefore | (std::uint64_t{beat.stagedAfter} << 16)};
}

NekoSIO2Trace::Beat nekoDecodeSIO2DMABeat(const NekoTraceEvent &event)
{
  if (event.subsystem != NekoTraceSubsystem::SIO2 ||
      event.type != NekoTraceEventType::DMABeat)
  {
    throw std::invalid_argument("Not a SIO2 DMA beat trace event.");
  }
  const auto channel = event.value0 & 255;
  const auto outcome = (event.value0 >> NekoSIO2Trace::OUTCOME_SHIFT) & 255;
  const auto status = (event.value0 >> NekoSIO2Trace::BUS_STATUS_SHIFT) & 255;
  const auto remainingBefore = event.value2 & 255;
  const auto remainingAfter = (event.value2 >> 8) & 255;
  const auto stagedBefore = event.value3 & 65535;
  const auto stagedAfter = (event.value3 >> 16) & 65535;
  if ((channel != 11 && channel != 12) || outcome > 3 || status > 3 ||
      remainingBefore > 36 || remainingAfter > 36 ||
      stagedBefore > 144 || stagedAfter > 144 ||
      ((outcome == static_cast<unsigned>(NekoSPUTrace::Outcome::Rejected)) ==
        (status == static_cast<unsigned>(NekoSPUTrace::BusStatus::None))))
  {
    throw std::invalid_argument("Unknown SIO2 DMA beat trace metadata.");
  }
  return {static_cast<std::uint8_t>(channel),
    static_cast<NekoSPUTrace::Outcome>(outcome),
    static_cast<NekoSPUTrace::BusStatus>(status),
    static_cast<std::uint32_t>(event.value1),
    static_cast<std::uint32_t>(event.value1 >> 32),
    static_cast<std::uint8_t>(remainingBefore),
    static_cast<std::uint8_t>(remainingAfter),
    static_cast<std::uint16_t>(stagedBefore),
    static_cast<std::uint16_t>(stagedAfter)};
}

NekoTraceEvent nekoSPUInputEvent(
  const NekoSPUTrace::InputTransition &transition) noexcept
{
  return {0, NekoTraceSubsystem::SPU2, NekoTraceEventType::StateChanged,
    transition.channel | (std::uint64_t{transition.core} << 8),
    packSPUInput(transition.before), packSPUInput(transition.after), 0};
}

NekoSPUTrace::InputTransition nekoDecodeSPUInput(const NekoTraceEvent &event)
{
  if (event.subsystem != NekoTraceSubsystem::SPU2 ||
      event.type != NekoTraceEventType::StateChanged)
  {
    throw std::invalid_argument("Not a SPU input transition trace event.");
  }
  NekoSPUTrace::InputTransition transition;
  transition.channel = static_cast<std::uint8_t>(event.value0);
  transition.core = static_cast<std::uint8_t>(event.value0 >> 8);
  validateSPUIdentity(transition.channel, transition.core);
  transition.before = decodeSPUInput(event.value1);
  transition.after = decodeSPUInput(event.value2);
  return transition;
}

std::uint64_t nekoFrameHash(
  const GSPresentation &presentation)
{
  std::uint64_t hash = FNV_OFFSET_BASIS;
  hashU64(&hash, presentation.width);
  hashU64(&hash, presentation.height);
  hashU64(&hash, presentation.rgba.size());
  for (const std::uint8_t value : presentation.rgba)
  {
    hash ^= value;
    hash *= FNV_PRIME;
  }
  return hash;
}

std::uint64_t nekoPackEEMemoryTraceMetadata(
  const NekoEETraceMemory::Metadata &metadata)
{
  using namespace NekoEETraceMemory;
  return
    metadata.width |
    (metadata.accessKind == AccessKind::DataStore ? WRITE : 0) |
    (metadata.transferOutcome == TransferOutcome::Completed
      ? SUCCEEDED
      : 0) |
    (static_cast<std::uint64_t>(metadata.accessKind) <<
      ACCESS_KIND_SHIFT) |
    (static_cast<std::uint64_t>(
      metadata.translationOutcome) <<
      TRANSLATION_OUTCOME_SHIFT) |
    (static_cast<std::uint64_t>(metadata.cacheRoute) <<
      CACHE_ROUTE_SHIFT) |
    (static_cast<std::uint64_t>(metadata.cacheAccess) <<
      CACHE_ACCESS_SHIFT) |
    (static_cast<std::uint64_t>(
      metadata.transferOutcome) <<
      TRANSFER_OUTCOME_SHIFT) |
    (metadata.scratchpadRoute ? SCRATCHPAD_ROUTE : 0) |
    (metadata.physicalAddressValid
      ? PHYSICAL_ADDRESS_VALID
      : 0) |
    (static_cast<std::uint64_t>(
      metadata.cacheAttribute & 0x7) <<
      CACHE_ATTRIBUTE_SHIFT) |
    (static_cast<std::uint64_t>(metadata.failurePhase) <<
      FAILURE_PHASE_SHIFT) |
    (static_cast<std::uint64_t>(metadata.physicalAddress) <<
      PHYSICAL_ADDRESS_SHIFT);
}

std::uint64_t nekoPackIOPMemoryTraceMetadata(
  const NekoIOPTraceMemory::Metadata &metadata)
{
  using namespace NekoIOPTraceMemory;
  return
    metadata.width |
    (metadata.accessKind == AccessKind::DataStore ? WRITE : 0) |
    (metadata.outcome == Outcome::Completed ? SUCCEEDED : 0) |
    (static_cast<std::uint64_t>(metadata.accessKind) << ACCESS_KIND_SHIFT) |
    (static_cast<std::uint64_t>(metadata.outcome) << OUTCOME_SHIFT) |
    (static_cast<std::uint64_t>(metadata.cacheRoute) << CACHE_ROUTE_SHIFT) |
    (metadata.physicalAddressValid ? PHYSICAL_ADDRESS_VALID : 0) |
    (metadata.accessKind == AccessKind::DataStore
      ? static_cast<std::uint64_t>(metadata.byteEnable & 0xf) << BYTE_ENABLE_SHIFT
      : 0) |
    (metadata.physicalAddressValid
      ? static_cast<std::uint64_t>(metadata.physicalAddress) <<
          PHYSICAL_ADDRESS_SHIFT
      : 0);
}

std::uint64_t nekoPackIOPTimerTraceSnapshot(
  const NekoIOPTraceTimer::CounterSnapshot &snapshot)
{
  using namespace NekoIOPTraceTimer;
  return
    snapshot.count |
    (static_cast<std::uint64_t>(snapshot.mode) << MODE_SHIFT) |
    (static_cast<std::uint64_t>(snapshot.prescalerRemainder) << PRESCALER_SHIFT) |
    (snapshot.gateWaiting ? GATE_WAITING : 0) |
    (snapshot.oneShotConsumed ? ONE_SHOT_CONSUMED : 0) |
    (snapshot.pulseReleasePending ? PULSE_RELEASE_PENDING : 0);
}

std::uint64_t nekoTraceHash(
  const std::vector<NekoTraceEvent> &events)
{
  std::uint64_t hash = FNV_OFFSET_BASIS;
  hashU64(&hash, events.size());
  for (const NekoTraceEvent &event : events)
  {
    hashU64(&hash, event.masterCycle);
    hashU64(&hash, static_cast<std::uint8_t>(event.subsystem));
    hashU64(&hash, static_cast<std::uint8_t>(event.type));
    hashU64(&hash, event.value0);
    hashU64(&hash, event.value1);
    hashU64(&hash, event.value2);
    hashU64(&hash, event.value3);
  }
  return hash;
}

std::uint64_t nekoPackCDVDTraceMetadata(
  const NekoCDVDTrace::Command &command) noexcept
{
  return static_cast<std::uint64_t>(command.transport) |
    (static_cast<std::uint64_t>(command.opcode) << 8) |
    (static_cast<std::uint64_t>(command.beforePhase) << 16) |
    (static_cast<std::uint64_t>(command.afterPhase) << 24) |
    (static_cast<std::uint64_t>(command.beforeCause) << 32) |
    (static_cast<std::uint64_t>(command.afterCause) << 40);
}

std::array<std::uint64_t, 2> nekoPackCDVDTraceResult(
  const std::array<std::uint8_t, 16> &result) noexcept
{
  std::array<std::uint64_t, 2> packed = {};
  for (std::size_t index = 0; index < result.size(); ++index)
  {
    packed[index / 8] |= static_cast<std::uint64_t>(result[index]) << ((index % 8) * 8);
  }
  return packed;
}

NekoCDVDTrace::Command nekoDecodeCDVDTrace(const NekoTraceEvent &event)
{
  if (event.subsystem != NekoTraceSubsystem::CDVD ||
      (event.type != NekoTraceEventType::CommandAccepted &&
       event.type != NekoTraceEventType::CommandCompleted))
  {
    throw std::invalid_argument("Not a CD/DVD command trace event.");
  }
  NekoCDVDTrace::Command command;
  command.transport =
    static_cast<NekoCDVDTrace::Transport>(event.value0 & 0xff);
  command.opcode = static_cast<std::uint8_t>(event.value0 >> 8);
  command.beforePhase =
    static_cast<NekoCDVDTrace::Phase>((event.value0 >> 16) & 0xff);
  command.afterPhase =
    static_cast<NekoCDVDTrace::Phase>((event.value0 >> 24) & 0xff);
  command.beforeCause = static_cast<std::uint8_t>(event.value0 >> 32);
  command.afterCause = static_cast<std::uint8_t>(event.value0 >> 40);
  command.rtcTicks = event.value1;
  const std::array<std::uint64_t, 2> result = {{event.value2, event.value3}};
  for (std::size_t index = 0; index < command.result.size(); ++index)
  {
    command.result[index] =
      static_cast<std::uint8_t>(result[index / 8] >> ((index % 8) * 8));
  }
  return command;
}
