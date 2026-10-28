#ifndef REGRESSION_TRACE_HPP
#define REGRESSION_TRACE_HPP

#include <array>
#include <cstdint>
#include <vector>

#include "gs_display.hpp"

enum class NekoTraceSubsystem : std::uint8_t
{
  VU0,
  VU1,
  VIF0,
  VIF1,
  GIF,
  GIFDMAC,
  GS,
  InterruptController,
  Display,
  Input,
  EE,
  IOP = 11,
  IOPTimers = 12,
  IOPInterruptController = 13,
  SIF = 14,
  CDVD = 15
};

enum class NekoTraceEventType : std::uint8_t
{
  Progress,
  StateChanged,
  TransferCompleted,
  InterruptChanged,
  PresentationBoundary,
  InstructionIssued,
  BranchScheduled,
  MemoryAccess,
  ExceptionEntered,
  InterruptDelivered,
  StateSnapshot,
  COP1LoadInterlock,
  COP1ResourceInterlock,
  COP1DividerHazard,
  COP1StageTransition,
  COP1Retired,
  InstructionRetired = 16,
  TimerEvent = 17,
  RegisterWrite = 18,
  SignalRequested = 19,
  DMABeat = 20,
  FaultContext = 21,
  // EE: raw byte, physical TXFIFO address, zero, zero; precedes EE observations.
  SerialTransmitCompleted = 22,
  CommandAccepted = 23,
  CommandCompleted = 24
};

namespace NekoCDVDTrace
{
  enum class Transport : std::uint8_t { Normal = 0, Subcommand = 1 };
  enum class Phase : std::uint8_t { Idle = 0, Pending = 1, Result = 2 };

  struct Command
  {
    Transport transport = Transport::Normal;
    std::uint8_t opcode = 0;
    Phase beforePhase = Phase::Idle;
    Phase afterPhase = Phase::Idle;
    std::uint8_t beforeCause = 0;
    std::uint8_t afterCause = 0;
    std::uint64_t rtcTicks = 0;
    std::array<std::uint8_t, 8> result = {};
  };
}

namespace NekoEETraceFault
{
  constexpr std::uint64_t INSTRUCTION_AVAILABLE = UINT64_C(1);
  constexpr std::uint64_t DELAY_SLOT = UINT64_C(1) << 1;
  constexpr std::uint64_t HOST_REJECTION = UINT64_C(1) << 2;
  constexpr std::uint8_t STOP_REASON_SHIFT = 8;
}

namespace NekoSIFTrace
{
  enum class Side : std::uint8_t { EE = 0, IOP = 1 };
  enum class Register : std::uint8_t
  {
    MSCOM = 0, SMCOM = 1, MSFLAG = 2, SMFLAG = 3, CTRL = 4, IOPDoorbell = 5
  };
  enum class Outcome : std::uint8_t { Completed = 0, UnsupportedControl = 1 };
  enum class Signal : std::uint8_t { RequestIOP = 1, RequestEE = 2 };
  constexpr std::uint8_t SIDE_SHIFT = 8;
  constexpr std::uint8_t OUTCOME_SHIFT = 16;
}

namespace NekoSIFDMATrace
{
  enum class Endpoint : std::uint8_t { EE = 0, IOP = 1, EESIF0 = 2, IOPSIF0 = 3 };
  enum class Kind : std::uint8_t
  {
    SourceTag = 0, Header = 1, Payload = 2, Completion = 3, DestinationTag = 4
  };
  enum class Outcome : std::uint8_t
  {
    Accepted = 0, Wait = 1, Completed = 2, Rejected = 3
  };
  enum class Failure : std::uint8_t
  {
    None = 0, SourceBus = 1, UnalignedHeader = 2, DestinationWrite = 3,
    DestinationBus = 4, DescriptorRead = 5, SourceRead = 6
  };
  constexpr std::uint8_t KIND_SHIFT = 8;
  constexpr std::uint8_t OUTCOME_SHIFT = 16;
  constexpr std::uint8_t FAILURE_SHIFT = 24;
}

namespace NekoIOPTraceInstruction
{
  constexpr std::uint64_t DELAY_SLOT = UINT64_C(1);
}

namespace NekoIOPTraceState
{
  enum class Execution : std::uint8_t
  {
    Halted = 0,
    Running = 1
  };

  enum class StopReason : std::uint8_t
  {
    None = 0,
    HostHalt = 1,
    UndefinedOperation = 2,
    ExecutionException = 3,
    StoreFailure = 4
  };
}

namespace NekoIOPTraceMemory
{
  constexpr std::uint64_t WIDTH_MASK = UINT64_C(0xff);
  constexpr std::uint64_t WRITE = UINT64_C(1) << 8;
  constexpr std::uint64_t SUCCEEDED = UINT64_C(1) << 9;
  constexpr std::uint8_t ACCESS_KIND_SHIFT = 10;
  constexpr std::uint64_t ACCESS_KIND_MASK =
    UINT64_C(0x3) << ACCESS_KIND_SHIFT;
  constexpr std::uint8_t OUTCOME_SHIFT = 12;
  constexpr std::uint64_t OUTCOME_MASK =
    UINT64_C(0x7) << OUTCOME_SHIFT;
  constexpr std::uint8_t CACHE_ROUTE_SHIFT = 15;
  constexpr std::uint64_t CACHE_ROUTE_MASK =
    UINT64_C(0x3) << CACHE_ROUTE_SHIFT;
  constexpr std::uint64_t PHYSICAL_ADDRESS_VALID = UINT64_C(1) << 17;
  constexpr std::uint8_t BYTE_ENABLE_SHIFT = 18;
  constexpr std::uint64_t BYTE_ENABLE_MASK =
    UINT64_C(0xf) << BYTE_ENABLE_SHIFT;
  constexpr std::uint8_t PHYSICAL_ADDRESS_SHIFT = 32;

  enum class AccessKind : std::uint8_t
  {
    InstructionFetch = 0,
    DataLoad = 1,
    DataStore = 2
  };

  enum class Outcome : std::uint8_t
  {
    Completed = 0,
    ProtectionFailure = 1,
    Misaligned = 2,
    ReadOnly = 3,
    Unmapped = 4
  };

  enum class CacheRoute : std::uint8_t
  {
    None = 0,
    Cached = 1,
    Uncached = 2
  };

  struct Metadata
  {
    std::uint32_t physicalAddress = 0;
    std::uint8_t width = 0;
    AccessKind accessKind = AccessKind::InstructionFetch;
    Outcome outcome = Outcome::Unmapped;
    CacheRoute cacheRoute = CacheRoute::None;
    bool physicalAddressValid = false;
    // Byte positions within the containing word; encoder clears these for reads.
    std::uint8_t byteEnable = 0;
  };
}

namespace NekoIOPTraceTimer
{
  constexpr std::uint64_t COUNT_MASK = UINT64_C(0xffffffff);
  constexpr std::uint8_t MODE_SHIFT = 32;
  constexpr std::uint64_t MODE_MASK = UINT64_C(0xffff) << MODE_SHIFT;
  constexpr std::uint8_t PRESCALER_SHIFT = 48;
  constexpr std::uint64_t PRESCALER_MASK =
    UINT64_C(0xff) << PRESCALER_SHIFT;
  constexpr std::uint64_t GATE_WAITING = UINT64_C(1) << 56;
  constexpr std::uint64_t ONE_SHOT_CONSUMED = UINT64_C(1) << 57;
  constexpr std::uint64_t PULSE_RELEASE_PENDING = UINT64_C(1) << 58;

  constexpr std::uint64_t INDEX_MASK = UINT64_C(0xff);
  constexpr std::uint64_t TARGET_REACHED = UINT64_C(1) << 8;
  constexpr std::uint64_t OVERFLOW_REACHED = UINT64_C(1) << 9;
  constexpr std::uint64_t INTERRUPT_REQUESTED = UINT64_C(1) << 10;
  constexpr std::uint64_t PULSE_RELEASED = UINT64_C(1) << 11;
  constexpr std::uint64_t GATE_CHANGED = UINT64_C(1) << 12;
  constexpr std::uint64_t MODE_ACKNOWLEDGED = UINT64_C(1) << 13;
  constexpr std::uint64_t REGISTER_WRITTEN = UINT64_C(1) << 14;
  constexpr std::uint64_t SYSTEM_PULSE = UINT64_C(1) << 16;
  constexpr std::uint64_t PIXEL_PULSE = UINT64_C(1) << 17;
  constexpr std::uint64_t HBLANK_BEFORE = UINT64_C(1) << 18;
  constexpr std::uint64_t HBLANK_AFTER = UINT64_C(1) << 19;
  constexpr std::uint64_t VBLANK_BEFORE = UINT64_C(1) << 20;
  constexpr std::uint64_t VBLANK_AFTER = UINT64_C(1) << 21;
  constexpr std::uint8_t REGISTER_SHIFT = 22;
  constexpr std::uint64_t REGISTER_MASK = UINT64_C(0x3) << REGISTER_SHIFT;

  enum class Register : std::uint8_t
  {
    None = 0,
    Count = 1,
    Mode = 2,
    Target = 3
  };

  struct CounterSnapshot
  {
    std::uint32_t count = 0;
    std::uint16_t mode = 0;
    // Largest supported divisor is 256, so its remainder fits in eight bits.
    std::uint8_t prescalerRemainder = 0;
    bool gateWaiting = false;
    bool oneShotConsumed = false;
    bool pulseReleasePending = false;
  };
}

namespace NekoEETraceBranch
{
  constexpr std::uint64_t TAKEN = UINT64_C(1);
  constexpr std::uint64_t LIKELY = UINT64_C(1) << 1;
}

namespace NekoEETraceMemory
{
  constexpr std::uint64_t WIDTH_MASK = UINT64_C(0xff);
  constexpr std::uint64_t WRITE = UINT64_C(1) << 8;
  constexpr std::uint64_t SUCCEEDED = UINT64_C(1) << 9;
  constexpr std::uint64_t LEGACY_MASK =
    WIDTH_MASK | WRITE | SUCCEEDED;
  constexpr std::uint8_t ACCESS_KIND_SHIFT = 10;
  constexpr std::uint64_t ACCESS_KIND_MASK =
    UINT64_C(0x3) << ACCESS_KIND_SHIFT;
  constexpr std::uint8_t TRANSLATION_OUTCOME_SHIFT = 12;
  constexpr std::uint64_t TRANSLATION_OUTCOME_MASK =
    UINT64_C(0xf) << TRANSLATION_OUTCOME_SHIFT;
  constexpr std::uint8_t CACHE_ROUTE_SHIFT = 16;
  constexpr std::uint64_t CACHE_ROUTE_MASK =
    UINT64_C(0x7) << CACHE_ROUTE_SHIFT;
  constexpr std::uint8_t CACHE_ACCESS_SHIFT = 19;
  constexpr std::uint64_t CACHE_ACCESS_MASK =
    UINT64_C(0x7) << CACHE_ACCESS_SHIFT;
  constexpr std::uint8_t TRANSFER_OUTCOME_SHIFT = 22;
  constexpr std::uint64_t TRANSFER_OUTCOME_MASK =
    UINT64_C(0x7) << TRANSFER_OUTCOME_SHIFT;
  constexpr std::uint64_t SCRATCHPAD_ROUTE =
    UINT64_C(1) << 25;
  constexpr std::uint64_t PHYSICAL_ADDRESS_VALID =
    UINT64_C(1) << 26;
  constexpr std::uint8_t CACHE_ATTRIBUTE_SHIFT = 27;
  constexpr std::uint64_t CACHE_ATTRIBUTE_MASK =
    UINT64_C(0x7) << CACHE_ATTRIBUTE_SHIFT;
  constexpr std::uint8_t FAILURE_PHASE_SHIFT = 30;
  constexpr std::uint64_t FAILURE_PHASE_MASK =
    UINT64_C(0x3) << FAILURE_PHASE_SHIFT;
  constexpr std::uint8_t PHYSICAL_ADDRESS_SHIFT = 32;

  enum class AccessKind : std::uint8_t
  {
    InstructionFetch,
    DataLoad,
    DataStore
  };

  enum class TranslationOutcome : std::uint8_t
  {
    Translated,
    TLBLookup,
    AddressErrorLoadOrFetch,
    AddressErrorStore,
    TLBRefillLoadOrFetch,
    TLBRefillStore,
    TLBInvalidLoadOrFetch,
    TLBInvalidStore,
    TLBModified,
    UnsupportedScratchpadInstruction,
    UnsupportedScratchpadPageSize,
    UnsupportedCacheAttribute
  };

  enum class CacheRoute : std::uint8_t
  {
    Uncached,
    CachedNoncoherent,
    UncachedAccelerated,
    TLBSelected,
    Unsupported
  };

  enum class CacheAccess : std::uint8_t
  {
    NotAccessed,
    Bypassed,
    Hit,
    Miss,
    Refilled,
    Allocated
  };

  enum class TransferOutcome : std::uint8_t
  {
    NotAttempted,
    Completed,
    Stalled,
    PhysicalBusError,
    DeviceNotReady
  };

  enum class FailurePhase : std::uint8_t
  {
    None,
    Direct,
    Refill,
    Writeback
  };

  struct Metadata
  {
    std::uint32_t physicalAddress = 0;
    std::uint8_t width = 0;
    AccessKind accessKind = AccessKind::InstructionFetch;
    TranslationOutcome translationOutcome =
      TranslationOutcome::TLBLookup;
    CacheRoute cacheRoute = CacheRoute::TLBSelected;
    std::uint8_t cacheAttribute = 0;
    bool scratchpadRoute = false;
    bool physicalAddressValid = false;
    CacheAccess cacheAccess = CacheAccess::NotAccessed;
    TransferOutcome transferOutcome =
      TransferOutcome::NotAttempted;
    FailurePhase failurePhase = FailurePhase::None;
  };
}

namespace NekoEETraceCOP1Interlock
{
  constexpr std::uint64_t READ = UINT64_C(1);
  constexpr std::uint64_t WRITE = UINT64_C(1) << 1;
}

namespace NekoEETraceCOP1Resource
{
  constexpr std::uint64_t ACCUMULATOR = 32;
  constexpr std::uint64_t FCR31 = 33;
  constexpr std::uint64_t CONDITION = 34;
  constexpr std::uint64_t GPR = 35;
  constexpr std::uint64_t MEMORY_EXCEPTION = 36;
}

namespace NekoEETraceCOP1DividerHazard
{
  constexpr std::uint64_t BRANCH_DELAY_SLOT = UINT64_C(1);
  constexpr std::uint64_t AFTER_BRANCH_DELAY_SLOT =
    UINT64_C(1) << 1;
  constexpr std::uint64_t AFTER_BRANCH_TARGET =
    UINT64_C(1) << 2;
}

namespace NekoEETraceCOP1Stage
{
  constexpr std::uint64_t R = 0;
  constexpr std::uint64_t T = 1;
  constexpr std::uint64_t X = 2;
  constexpr std::uint64_t Y = 3;
  constexpr std::uint64_t Z = 4;
  constexpr std::uint64_t S1 = 5;
  constexpr std::uint64_t S2 = 6;
  constexpr std::uint64_t NONE = UINT64_C(0xff);
  constexpr std::uint64_t FROM_MASK = UINT64_C(0xff);
  constexpr std::uint64_t TO_SHIFT = 8;
  constexpr std::uint64_t REMAINING_CYCLES_SHIFT = 16;
}

namespace NekoEETraceCOP1Result
{
  constexpr std::uint64_t DESTINATION_FPR = UINT64_C(1);
  constexpr std::uint64_t DESTINATION_ACCUMULATOR =
    UINT64_C(1) << 1;
  constexpr std::uint64_t DESTINATION_FCR31 =
    UINT64_C(1) << 2;
  constexpr std::uint64_t DESTINATION_CONDITION =
    UINT64_C(1) << 3;
  constexpr std::uint64_t DESTINATION_GPR =
    UINT64_C(1) << 4;
  constexpr std::uint64_t DESTINATION_MEMORY =
    UINT64_C(1) << 5;
  constexpr std::uint64_t DESTINATION_MASK_SHIFT = 32;
  constexpr std::uint64_t FPR_REGISTER_SHIFT = 40;
  constexpr std::uint64_t GPR_REGISTER_SHIFT = 48;
  constexpr std::uint64_t AFFECTED_FLAGS_MASK = UINT64_C(0xff);
  constexpr std::uint64_t RAISED_FLAGS_SHIFT = 8;
  constexpr std::uint64_t RAISED_STICKY_FLAGS_SHIFT = 16;
  constexpr std::uint64_t CONDITION = UINT64_C(1) << 24;
}

struct NekoTraceEvent
{
  std::uint64_t masterCycle = 0;
  NekoTraceSubsystem subsystem = NekoTraceSubsystem::VU0;
  NekoTraceEventType type = NekoTraceEventType::Progress;
  std::uint64_t value0 = 0;
  std::uint64_t value1 = 0;
  std::uint64_t value2 = 0;
  std::uint64_t value3 = 0;
};

// Passive diagnostic sink. Must not reenter or mutate the emitting system.
class NekoTraceObserver
{
  public:
    virtual ~NekoTraceObserver() = default;
    virtual void observe(const NekoTraceEvent &event) noexcept = 0;
};

// EE/FaultContext: originating instruction PC, raw opcode, branch PC (only
// when DELAY_SLOT), flags plus EEStopReason << 8. Emitted immediately before
// ExceptionEntered, or alone for a host instruction rejection. Fetch failures
// have no available opcode. Delay context comes from the transition request,
// not retained COP0 BD/EPC (which can belong to an earlier nested exception).

// IOP payloads (value0, value1, value2, value3). Producers zero currently
// unassigned words/bits; consumers ignore them to permit future extensions.
// InstructionIssued: instruction PC, raw opcode, reserved, instruction flags.
//   Describes a successfully fetched attempt, including invalid/deferred opcodes.
// InstructionRetired: instruction PC, next PC, retired total, IOP cycle total.
// BranchScheduled: branch PC, selected target, taken (bit 0), reserved.
// MemoryAccess: virtual address, raw transferred value, reserved, memory metadata.
//   Merge accesses use the aligned containing word and actual store byte enables.
//   Failed reads carry zero value; failed stores retain the attempted value.
// ExceptionEntered: architectural ExcCode, EPC, selected vector, committed Cause.
// InterruptDelivered: interrupted PC, pre-entry Status, committed Cause, vector.
// StateChanged: old execution state, new execution state, stop reason, current PC.
// IOPInterruptController/InterruptChanged: I_STAT, I_MASK, I_CTRL, aggregate pending.
// IOPTimers/TimerEvent: index/event/input/register metadata, before/after counter
//   snapshots, targets (before in bits 31:0, after in bits 63:32). Snapshot mode
//   is raw mode, not the guest read value with its synthesized request-idle bit.
// Producers map private types to these public encodings; NekoSystem supplies
// the post-clock master-cycle timestamp and preserves IOP mutation order.
// IOP records follow existing non-display cycle records and precede the final
// PresentationBoundary marker. Collection covers scheduled cycles, not direct
// host setup or execution-control calls between cycles.
//
// SIF/RegisterWrite: register | side << 8 | outcome << 16, submitted word,
//   canonical word before, canonical word after (no synthesized read bits).
//   Includes accepted no-ops and unsupported CTRL commands; rejected widths or
//   addresses remain CPU memory failures, not SIF register operations.
// SIF/SignalRequested: signal, originating register metadata, submitted word, 0.
//   Emitted for each request, even if destination INTC status already coalesced.
// SIF records share mutation ordering with IOP CPU/device records. Collection
//   observes scheduled writes only; inspection and register polling add no noise.
// SIF/DMABeat: endpoint | kind << 8 | outcome << 16 | failure << 24,
//   attempted address (including the EE SPR selector), remaining count
//   (before in bits 31:0, after in bits 63:32), directional queue count (same pair).
//   Remaining count is EE QWC or IOP packet words. EE source-tag grants do not
//   enqueue tags; IOP source-tag grants enqueue the full EE prologue.
//   Completion is endpoint-local, not peer drain.
//   At most two EE and two IOP attempts per cycle, only for eligible grants;
//   a waiting candidate may be bypassed, but only one transition per side accepts.
//   waits are per grant, not per host read or disabled/idle channel.
//   A waiting header uses current IOP MADR until a complete header is available.
//   Rejected IOP grants publish the executed cycle prefix before throwing,
//   without admitting the IOP instruction. Records precede completion-produced
//   IOP INTC changes and same-pulse IOP instruction/interrupt observations.
//
// CDVD/CommandAccepted and CommandCompleted:
//   value0 bytes 0..5: transport (N=0/S=1), opcode, before/after phase
//   (Idle=0/Pending=1/Result=2), before/after device cause; bits 63:48 zero.
//   value1: current RTC master ticks in the selected 100-year cycle.
//   value2: eight result bytes, byte i at bits 8*i, only for S completion;
//   zero for N commands and acceptance. value3 is reserved and zero.
//   Normal commands use Idle/Pending phases independently of the S channel.
//   Acceptance is ordered inside the successful byte write, before its CPU
//   memory record. Completion precedes its resulting INTC/CPU records.
//   No events for rejected commands, polling, result bytes or acknowledgements.

std::uint64_t nekoFrameHash(const GSPresentation &presentation);
std::uint64_t nekoTraceHash(
  const std::vector<NekoTraceEvent> &events);
std::uint64_t nekoPackEEMemoryTraceMetadata(
  const NekoEETraceMemory::Metadata &metadata);
std::uint64_t nekoPackIOPMemoryTraceMetadata(
  const NekoIOPTraceMemory::Metadata &metadata);
std::uint64_t nekoPackIOPTimerTraceSnapshot(
  const NekoIOPTraceTimer::CounterSnapshot &snapshot);
std::uint64_t nekoPackCDVDTraceMetadata(
  const NekoCDVDTrace::Command &command) noexcept;
std::uint64_t nekoPackCDVDTraceResult(
  const std::array<std::uint8_t, 8> &result) noexcept;
NekoCDVDTrace::Command nekoDecodeCDVDTrace(const NekoTraceEvent &event);

#endif
