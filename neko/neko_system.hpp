#ifndef NEKO_SYSTEM_HPP
#define NEKO_SYSTEM_HPP

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "clock_scheduler.hpp"
#include "clock_rates.hpp"
#include "cdvd_controller.hpp"
#include "dmac_controller.hpp"
#include "sif1_dmac_channel.hpp"
#include "sif0_dmac_channel.hpp"
#include "ee_bus.hpp"
#include "ee_core.hpp"
#include "ee_elf_loader.hpp"
#include "ee_rdram_initialization.hpp"
#include "ee_serial.hpp"
#include "ee_timers.hpp"
#include "gif.hpp"
#include "gif_dmac_channel.hpp"
#include "gif_path1.hpp"
#include "gif_path3.hpp"
#include "gif_path_arbiter.hpp"
#include "gif_registers.hpp"
#include "gs.hpp"
#include "gs_display.hpp"
#include "interrupt_controller.hpp"
#include "iop_core.hpp"
#include "iop_dma_controller.hpp"
#include "iop_sif1_dma_channel.hpp"
#include "iop_sif0_dma_channel.hpp"
#include "iop_timer_timing.hpp"
#include "regression_trace.hpp"
#include "scratchpad_dmac_channel.hpp"
#include "sif_interrupt_router.hpp"
#include "system_interfaces.hpp"
#include "vif.hpp"
#include "vif1_dmac_channel.hpp"
#include "vpu.hpp"

struct EEExecutionResult
{
  std::uint64_t masterCycles = 0;
  std::uint64_t eeCycles = 0;
  std::uint64_t instructions = 0;
  bool cycleLimitReached = false;
  EEExecutionState state = EEExecutionState::Halted;
  EEStopReason stopReason = EEStopReason::None;
  std::uint32_t programCounter = 0;
  EEException pendingException = EEException::None;
  std::uint32_t exceptionAddress = 0;
};

struct IOPExecutionResult
{
  std::uint64_t iopCycles = 0;
  std::uint64_t instructions = 0;
  IOPExecutionState state = IOPExecutionState::Halted;
  IOPStopReason stopReason = IOPStopReason::None;
  std::uint32_t programCounter = 0;
  IOPExceptionRecord exception;
  IOPStoreFailure storeFailure;
};

struct DualCPUExecutionResult
{
  std::uint64_t masterCycles = 0;
  bool cycleLimitReached = false;
  // Per-call progress. The EE budget fields describe the shared run boundary.
  EEExecutionResult ee;
  IOPExecutionResult iop;
};

namespace EEGuestRuntime
{
  constexpr std::uint32_t STACK_POINTER =
    EEMemoryMap::MAIN_MEMORY_SIZE;
  constexpr std::uint32_t RETURN_ADDRESS =
    UINT32_C(0xfffffffc);
  constexpr std::size_t EXIT_CODE_REGISTER = 2;
}

enum class EEGuestOutcome : std::uint8_t
{
  Completed,
  GuestReportedFailure,
  CycleLimitReached,
  Exception,
  Stopped
};

struct EEGuestExecutionResult
{
  EEELFLoadResult load;
  EEExecutionResult execution;
  EEGuestOutcome outcome = EEGuestOutcome::Stopped;
  std::uint32_t exitCode = 0;
};

class NekoSystem
{
  public:
    static constexpr std::uint64_t EE_CLOCK_HZ = PS2Clock::MASTER_HZ;
    static constexpr std::uint64_t VU_CLOCK_HZ = 147456000;
    static constexpr std::uint64_t VU_CLOCK_PERIOD =
      EE_CLOCK_HZ / VU_CLOCK_HZ;

    NekoSystem();

    NekoSystem(const NekoSystem &) = delete;
    NekoSystem &operator=(const NekoSystem &) = delete;
    NekoSystem(NekoSystem &&) = delete;
    NekoSystem &operator=(NekoSystem &&) = delete;

    // Between completed master cycles only. Clears mutable state and tracing,
    // preserves external ROM identities, and leaves both CPUs halted.
    void reset();
    // Requires both CPUs halted; installs shared bytes without resetting or starting.
    void installBootROM(const std::vector<std::uint8_t> &image);
    void setInput(const NekoInputState &input);
    const NekoInputState &input() const;
    NekoFrameResult runFrame();
    GSPresentation videoOutput() const;
    NekoAudioFrame audioOutput() const;
    std::vector<std::uint8_t> saveState() const;
    void loadState(const std::vector<std::uint8_t> &state);
    EEELFLoadResult loadELF(
      const std::vector<std::uint8_t> &image);
    EEGuestExecutionResult runELF(
      const std::vector<std::uint8_t> &image,
      std::uint64_t maxMasterCycles);
    EEExecutionResult runBIOS(
      const std::vector<std::uint8_t> &image,
      std::uint64_t maxMasterCycles);
    // Requires both CPUs halted; validates, cold-resets, and releases both at c=0.
    DualCPUExecutionResult runDualCPUBIOS(
      const std::vector<std::uint8_t> &image,
      std::uint64_t maxMasterCycles);
    // Continues existing state, including guest exception handlers. Either CPU
    // halted takes precedence over the budget; returning does not halt its peer.
    DualCPUExecutionResult runDualCPU(
      std::uint64_t maxMasterCycles);
    void startTrace();
    // Observer receives all events before prefix limiting; stop/reset detach it.
    void startTrace(std::uint64_t eventLimit,
      std::shared_ptr<NekoTraceObserver> observer = {});
    void stopTrace();
    void clearTrace();
    bool traceEnabled() const;
    std::uint64_t traceEventLimit() const;
    bool traceTruncated() const;
    const std::vector<NekoTraceEvent> &trace() const;
    std::uint64_t traceHash() const;
    std::uint64_t eeStateHash() const;
    // Hashes IOP state and its scheduling phase, not the complete machine.
    std::uint64_t iopStateHash() const;
    // Shared interconnect state only; destination INTC latches remain separate.
    std::uint64_t sifStateHash() const;
    SIF &sif();
    const SIF &sif() const;

    IOPCore &iopCore();
    const IOPCore &iopCore() const;
    IOPBus &iopBus();
    const IOPBus &iopBus() const;
    IOPInterruptController &iopInterruptController();
    const IOPInterruptController &iopInterruptController() const;
    IOPDMAController &iopDMAController();
    const IOPDMAController &iopDMAController() const;
    IOPSIF1DMAChannel &iopSIF1DMAC();
    const IOPSIF1DMAChannel &iopSIF1DMAC() const;
    IOPSIF0DMAChannel &iopSIF0DMAC();
    const IOPSIF0DMAChannel &iopSIF0DMAC() const;
    IOPTimers &iopTimers();
    const IOPTimers &iopTimers() const;
    CDVDController &cdvdController();
    const CDVDController &cdvdController() const;
    IOPTimerTiming &iopTimerTiming();
    const IOPTimerTiming &iopTimerTiming() const;

    EECore &eeCore();
    const EECore &eeCore() const;
    VPU &vu0();
    const VPU &vu0() const;
    VPU &vu1();
    const VPU &vu1() const;
    VIF &vif0();
    const VIF &vif0() const;
    VIF &vif1();
    const VIF &vif1() const;
    GIFDecoder &gifDecoder();
    const GIFDecoder &gifDecoder() const;
    GIFPathArbiter &gifPathArbiter();
    const GIFPathArbiter &gifPathArbiter() const;
    GIFPath1Transfer &gifPath1();
    const GIFPath1Transfer &gifPath1() const;
    GIFPath3Transfer &gifPath3();
    const GIFPath3Transfer &gifPath3() const;
    GS &gs();
    const GS &gs() const;
    GIFRegisters &gifRegisters();
    const GIFRegisters &gifRegisters() const;
    DMACController &dmacController();
    const DMACController &dmacController() const;
    GIFDMACChannel &gifDMAC();
    const GIFDMACChannel &gifDMAC() const;
    VIF1DMACChannel &vif1DMAC();
    const VIF1DMACChannel &vif1DMAC() const;
    SIF1DMACChannel &sif1DMAC();
    const SIF1DMACChannel &sif1DMAC() const;
    SIF0DMACChannel &sif0DMAC();
    const SIF0DMACChannel &sif0DMAC() const;
    ScratchpadDMACChannel &fromScratchpadDMAC();
    const ScratchpadDMACChannel &fromScratchpadDMAC() const;
    ScratchpadDMACChannel &toScratchpadDMAC();
    const ScratchpadDMACChannel &toScratchpadDMAC() const;
    GSDisplay &gsDisplay();
    const GSDisplay &gsDisplay() const;
    EEBus &eeBus();
    const EEBus &eeBus() const;
    EEMemorySystem &eeMemorySystem();
    const EEMemorySystem &eeMemorySystem() const;
    EEInterruptController &interruptController();
    const EEInterruptController &interruptController() const;
    EETimers &eeTimers();
    const EETimers &eeTimers() const;
    void clockMasterCycle();
    std::uint64_t runMasterCycles(std::uint64_t cycles);
    EEExecutionResult stepEEInstruction(
      std::uint64_t maxMasterCycles);
    EEExecutionResult runEE(
      std::uint64_t maxMasterCycles);
    bool interruptPending() const;
    MasterClockScheduler &masterClockScheduler();
    const MasterClockScheduler &masterClockScheduler() const;

  private:
    friend class NekoSaveStateCodec;

    EECore eeCoreComponent;
    VPU vu0Component;
    VPU vu1Component;
    VIF vif0Component;
    VIF vif1Component;
    GS gsComponent;
    GIFDecoder gifDecoderComponent;
    GIFPathArbiter gifPathArbiterComponent;
    GIFPath1Transfer gifPath1Component;
    GIFPath3Transfer gifPath3Component;
    GIFRegisters gifRegisterFile;
    EEInterruptController interruptControllerComponent;
    EETimers eeTimersComponent;
    EESerial eeSerialComponent;
    EERDRAMInitialization eeRDRAMInitializationComponent;
    MasterClockScheduler masterClock;
    EEBus eeBusComponent;
    DMACController dmacControllerComponent;
    GIFDMACChannel gifDMACComponent;
    VIF1DMACChannel vif1DMACComponent;
    ScratchpadDMACChannel fromScratchpadDMACComponent;
    ScratchpadDMACChannel toScratchpadDMACComponent;
    GSDisplay gsDisplayComponent;
    CDVDController cdvdControllerComponent;
    IOPBus iopBusComponent;
    IOPCore iopCoreComponent;
    IOPInterruptController iopInterruptControllerComponent;
    IOPDMAController iopDMAControllerComponent;
    IOPSIF1DMAChannel iopSIF1DMAComponent;
    IOPSIF0DMAChannel iopSIF0DMAComponent;
    IOPTimers iopTimersComponent;
    IOPTimerTiming iopTimerTimingComponent;
    SIF sifComponent;
    SIF1DMACChannel sif1DMACComponent;
    SIF0DMACChannel sif0DMACComponent;
    SIFInterruptRouter sifInterruptRouterComponent;
    NekoInputState inputState;
    bool collectingTrace = false;
    std::vector<NekoTraceEvent> traceEvents;
    std::shared_ptr<NekoTraceObserver> traceObserver;
    std::uint64_t maximumTraceEvents = 0;
    bool truncatedTrace = false;
    std::uint64_t lastTracedEEStateHash = 0;

    struct CycleObservationSnapshot
    {
      std::uint64_t vu0Cycles;
      std::uint64_t vu1Cycles;
      std::uint64_t vif0Words;
      std::uint64_t vif1Words;
      std::uint64_t gifQuadwords;
      std::uint64_t dmacQuadwords;
      std::uint32_t dmacControl;
      std::uint32_t interruptStatus;
      std::uint64_t pixels;
      std::uint64_t presentationBoundary;
    };

    struct SIFDMAObservation
    {
      NekoTraceEvent event;
      std::size_t sequence = 0;
    };

    struct SIFDMAObservations
    {
      std::array<SIFDMAObservation, 4> entries = {};
      std::size_t count = 0;
    };

    void clockEESIF(NekoObservationOrder &order, SIFDMAObservations &events);
    EESIFDMABeatOutcome clockEESIFChannel(SIFDirection direction,
      NekoObservationOrder &order, SIFDMAObservations &events);
    IOPDMABeatResult clockIOPSIF(NekoObservationOrder &order, SIFDMAObservations &events);
    IOPDMABeatResult clockIOPSIFChannel(IOPDMAChannel channel,
      NekoObservationOrder &order, SIFDMAObservations &events);
    void latchComponentInterrupts();
    void publishEEInterruptLines();
    void advanceIOPTimers(bool systemClockPulse);
    void advanceEETimers(std::uint64_t masterCycle);
    CycleObservationSnapshot
      captureCycleObservation() const;
    EEExecutionResult makeEEExecutionResult(
      std::uint64_t masterCycles,
      std::uint64_t startingEECycles,
      std::uint64_t instructions,
      bool cycleLimitReached) const;
    void publishCycleTrace(
      std::uint64_t cycle,
      const CycleObservationSnapshot &beforeCycle,
      const SIFDMAObservations &dma,
      const EESerialCompletion &serial);
    void publishIOPAndSIFTrace(
      std::uint64_t cycle, const SIFDMAObservations &dma);
    void publishCDVDTrace(
      std::uint64_t cycle, const CDVDController::Observation &event);
    void publishSIFTrace(std::uint64_t cycle, const SIF::Observation &event);
    void publishIOPCPUTrace(std::uint64_t cycle, const IOPCore::StepEvent &event);
    void publishIOPTimerTrace(
      std::uint64_t cycle, const IOPTimers::Observation &event);
    void publishIOPInterruptTrace(
      std::uint64_t cycle, const IOPInterruptController::Observation &event);
    static NekoEETraceMemory::AccessKind
      traceMemoryAccessKind(EECore::MemoryAccessKind kind);
    static NekoEETraceMemory::TranslationOutcome
      traceMemoryTranslationOutcome(
        EEAddressTranslationOutcome outcome);
    static NekoEETraceMemory::CacheRoute
      traceMemoryCacheRoute(EECacheRoute route);
    static NekoEETraceMemory::CacheAccess
      traceMemoryCacheAccess(
        EECore::MemoryCacheAccess access);
    static NekoEETraceMemory::TransferOutcome
      traceMemoryTransferOutcome(
        EECore::MemoryTransferOutcome outcome);
    static NekoEETraceMemory::FailurePhase
      traceMemoryFailurePhase(
        EEMemoryTransferFailurePhase phase);
    void appendTrace(
      std::uint64_t cycle,
      NekoTraceSubsystem subsystem,
      NekoTraceEventType type,
      std::uint64_t value0,
      std::uint64_t value1 = 0,
      std::uint64_t value2 = 0,
      std::uint64_t value3 = 0);
};

#endif
