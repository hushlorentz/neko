#ifndef STARTUP_DIAGNOSTICS_HPP
#define STARTUP_DIAGNOSTICS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
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
};

struct StartupDMACChannelSummary
{
  std::uint8_t channel = 0;
  std::uint32_t control = 0;
  std::uint32_t memoryAddress = 0;
  std::uint32_t quadwordCount = 0;
  // Absent on channel 8 (zero here, JSON null).
  std::uint32_t tagAddress = 0;
  // Only channels 8/9; zero here and JSON null on channels 1/2.
  std::uint32_t scratchpadAddress = 0;
  bool clockEligible = false;
  // Retained last-attempt flag for channels 1/2, not current sink readiness.
  bool lastTransferStalled = false;
};

struct StartupDMACSummary
{
  std::uint32_t control = 0;
  std::uint32_t status = 0;
  std::uint32_t interleaveSize = 0;
  bool interruptPending = false;
  std::array<StartupDMACChannelSummary, 4> channels = {};
};

struct StartupEEInterruptSummary
{
  std::uint32_t status = 0;
  std::uint32_t mask = 0;
  bool pending = false;
};

struct StartupIOPDMASummary
{
  std::array<IOPWord, 2> priority = {};
  std::array<IOPWord, 2> interruptStatus = {};
  bool interruptPending = false;
  IOPWord control = 0;
  IOPWord memoryAddress = 0;
  IOPWord blockControl = 0;
  IOPWord remainingWords = 0;
  bool terminal = false;
  bool enabled = false;
  // Side-effect-free next grant, not a retained previous-attempt result.
  IOPDMABeatResult nextBeat;
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

// Call between completed master cycles. Uses const owner inspection only;
// neither CPU/device work nor tracing is advanced, enabled, or consumed.
StartupSummary inspectStartup(const NekoSystem &system);

// Opt-in wrappers preserve the existing runner policy and admission errors.
StartupRunSummary runDualCPUWithStartupSummary(
  NekoSystem &system, std::uint64_t maxMasterCycles);
StartupRunSummary runDualCPUBIOSWithStartupSummary(
  NekoSystem &system, const std::vector<std::uint8_t> &image,
  std::uint64_t maxMasterCycles);

void writeStartupSummaryJsonLine(
  std::ostream &output, const StartupSummary &summary);
void writeStartupRunSummaryJsonLine(
  std::ostream &output, const StartupRunSummary &summary);
void writeStartupTraceJsonLines(
  std::ostream &output, const NekoSystem &system);

#endif
