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
  return Kind::None;
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

std::uint64_t nekoPackCDVDTraceResult(
  const std::array<std::uint8_t, 8> &result) noexcept
{
  std::uint64_t packed = 0;
  for (std::size_t index = 0; index < result.size(); ++index)
  {
    packed |= static_cast<std::uint64_t>(result[index]) << (index * 8);
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
  for (std::size_t index = 0; index < command.result.size(); ++index)
  {
    command.result[index] = static_cast<std::uint8_t>(event.value2 >> (index * 8));
  }
  return command;
}
