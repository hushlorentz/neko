#ifndef STARTUP_DIAGNOSTICS_HPP
#define STARTUP_DIAGNOSTICS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "neko_system.hpp"

struct StartupEESummary
{
  std::uint64_t cycles = 0;
  std::uint32_t programCounter = 0;
  EEExecutionState state = EEExecutionState::Halted;
  EEStopReason stopReason = EEStopReason::None;
  std::uint32_t rejectedInstruction = 0;
  // Retained exception metadata, not a claim of an exception in this cycle.
  EEException pendingException = EEException::None;
  std::uint32_t exceptionAddress = 0;
  std::uint32_t status = 0;
  std::uint32_t cause = 0;
  std::uint32_t epc = 0;
  std::uint32_t badVirtualAddress = 0;
};

struct StartupIOPSummary
{
  std::uint64_t cycles = 0;
  std::uint64_t retiredInstructions = 0;
  std::uint32_t programCounter = 0;
  IOPExecutionState state = IOPExecutionState::Halted;
  IOPStopReason stopReason = IOPStopReason::None;
  // Entry event from the last IOP step; COP0 fields retain handler context.
  IOPExceptionRecord exception;
  IOPStoreFailure storeFailure;
  std::uint32_t status = 0;
  std::uint32_t cause = 0;
  std::uint32_t epc = 0;
  std::uint32_t badVirtualAddress = 0;
};

struct StartupSIFSummary
{
  std::uint32_t mscom = 0;
  std::uint32_t smcom = 0;
  std::uint32_t msflag = 0;
  std::uint32_t smflag = 0;
  std::uint32_t control = 0;
  std::uint32_t iopDoorbell = 0;
  std::size_t sif0Words = 0;
  std::size_t sif1Words = 0;
  std::size_t freeWords = 0;
  std::size_t packingCursor = 0;
  SIFQuadword packingLatch = {};
};

struct StartupEESIFSummary
{
  bool active = false;
  bool enabled = false;
  EESIFDMABeatKind nextBeatKind = EESIFDMABeatKind::Completion;
  DMACChannelContinuation continuation;
};

struct StartupDMACChannelSummary
{
  std::uint8_t channel = 0;
  std::uint32_t control = 0;
  std::uint32_t memoryAddress = 0;
  std::uint32_t quadwordCount = 0;
  // Absent on channels 5/8 (zero here, JSON null).
  std::uint32_t tagAddress = 0;
  // Only channels 8/9; zero here and JSON null elsewhere.
  std::uint32_t scratchpadAddress = 0;
  bool clockEligible = false;
  // Retained last-attempt flag for channels 1/2, not current sink readiness.
  bool lastTransferStalled = false;
  // Only channels 5/6; not serialized on other channels.
  StartupEESIFSummary sif;
};

struct StartupDMACSummary
{
  std::uint32_t control = 0;
  std::uint32_t status = 0;
  std::uint32_t interleaveSize = 0;
  bool interruptPending = false;
  std::uint32_t priorityControl = 0;
  std::uint32_t stallAddress = 0;
  std::array<SIFDirection, 2> sifGrantOrder = {{SIFDirection::SIF0, SIFDirection::SIF1}};
  // Preserve existing indices and append the SIF endpoints.
  std::array<StartupDMACChannelSummary, 6> channels = {{{1}, {2}, {8}, {9}, {5}, {6}}};
};

struct StartupEEInterruptSummary
{
  std::uint32_t status = 0;
  std::uint32_t mask = 0;
  bool pending = false;
};

struct StartupIOPDMAChannelSummary
{
  explicit StartupIOPDMAChannelSummary(IOPDMAChannel channel) :
    nextBeat{IOPDMABeatOutcome::Wait, channel}
  {
  }

  IOPWord control = 0;
  IOPWord memoryAddress = 0;
  IOPWord blockControl = 0;
  // Only channel 9 (zero here and JSON null on channel 10).
  IOPWord tagAddress = 0;
  IOPWord remainingWords = 0;
  bool terminal = false;
  bool active = false;
  bool enabled = false;
  std::uint8_t priority = 0;
  // Side-effect-free next grant, not a retained previous-attempt result.
  IOPDMABeatResult nextBeat;
};

struct StartupIOPDMASummary
{
  std::array<IOPWord, 2> priority = {};
  std::array<IOPWord, 2> interruptStatus = {};
  bool interruptPending = false;
  std::array<IOPDMAChannel, 2> sifGrantOrder = {{IOPDMAChannel::SIF0, IOPDMAChannel::SIF1}};
  IOPDMAChannel sifTieBreakChannel = IOPDMAChannel::SIF0;
  StartupIOPDMAChannelSummary sif0{IOPDMAChannel::SIF0};
  StartupIOPDMAChannelSummary sif1{IOPDMAChannel::SIF1};
};

struct StartupIOPInterruptSummary
{
  std::uint32_t status = 0;
  std::uint32_t mask = 0;
  std::uint32_t control = 0;
  bool pending = false;
};

struct StartupSummary
{
  std::uint64_t masterCycle = 0;
  StartupEESummary ee;
  StartupIOPSummary iop;
  StartupSIFSummary sif;
  StartupDMACSummary dmac;
  StartupIOPDMASummary iopDMA;
  StartupEEInterruptSummary eeInterrupts;
  StartupIOPInterruptSummary iopInterrupts;
};

struct StartupRunSummary
{
  DualCPUExecutionResult execution;
  StartupSummary startup;
};

enum class StartupExecutionBoundary { CompleteCycle, IncompleteCycle };

struct StartupRuntimeError
{
  std::string message;
  StartupSummary startup;
};

enum class StartupRunOutcome { Completed, RuntimeError };

struct StartupRunEvidence
{
  StartupRunOutcome outcome = StartupRunOutcome::Completed;
  // Only the member selected by outcome is valid.
  StartupRunSummary completed;
  StartupRuntimeError error;
};

struct StartupInstructionContext
{
  bool available = false;
  std::uint32_t programCounter = 0;
  bool instructionAvailable = false;
  std::uint32_t instruction = 0;
  bool delaySlotAvailable = false;
  bool delaySlot = false;
  bool branchAddressAvailable = false;
  std::uint32_t branchAddress = 0;
};

enum class StartupFailureKind { MemoryAccess, InstructionRejection, DMARejection };

struct StartupFailure
{
  bool found = false;
  StartupFailureKind kind = StartupFailureKind::MemoryAccess;
  std::size_t eventIndex = 0;
  NekoTraceEvent event;
  StartupInstructionContext instruction;
  bool exceptionAvailable = false;
  NekoTraceEvent exception;
};

// Call between completed master cycles. Uses const owner inspection only;
// neither CPU/device work nor tracing is advanced, enabled, or consumed.
// Also safe after synchronous execution has unwound; that snapshot is not
// a completed-cycle boundary and must be labelled as such by its caller.
StartupSummary inspectStartup(const NekoSystem &system);

// Opt-in wrappers preserve the existing runner policy and admission errors.
StartupRunSummary runDualCPUWithStartupSummary(
  NekoSystem &system, std::uint64_t maxMasterCycles);
StartupRunSummary runDualCPUBIOSWithStartupSummary(
  NekoSystem &system, const std::vector<std::uint8_t> &image,
  std::uint64_t maxMasterCycles);

// Admission/reset remains the caller's responsibility. Runtime errors retain
// available owner state, never a fabricated successful execution result.
StartupRunEvidence runDualCPUWithStartupEvidence(
  NekoSystem &system, std::uint64_t maxMasterCycles);
StartupFailure firstStartupFailure(
  const std::vector<NekoTraceEvent> &trace);

void writeStartupSummaryJsonLine(
  std::ostream &output, const StartupSummary &summary);
void writeStartupRunSummaryJsonLine(
  std::ostream &output, const StartupRunSummary &summary);
void writeStartupTraceJsonLines(
  std::ostream &output, const NekoSystem &system);
void writeStartupRuntimeErrorJsonLine(
  std::ostream &output, const StartupRuntimeError &error);
void writeStartupFirstFailureJsonLine(
  std::ostream &output, const NekoSystem &system,
  StartupExecutionBoundary boundary = StartupExecutionBoundary::CompleteCycle);

#endif
