#ifndef NEKO_SYSTEM_HPP
#define NEKO_SYSTEM_HPP

#include <cstdint>
#include <vector>

#include "clock_scheduler.hpp"
#include "ee_bus.hpp"
#include "ee_core.hpp"
#include "gif.hpp"
#include "gif_dmac_channel.hpp"
#include "gif_path1.hpp"
#include "gif_path3.hpp"
#include "gif_path_arbiter.hpp"
#include "gif_registers.hpp"
#include "gs.hpp"
#include "gs_display.hpp"
#include "interrupt_controller.hpp"
#include "regression_trace.hpp"
#include "system_interfaces.hpp"
#include "vif.hpp"
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

class NekoSystem
{
  public:
    static constexpr std::uint64_t EE_CLOCK_HZ = 294912000;
    static constexpr std::uint64_t VU_CLOCK_HZ = 147456000;
    static constexpr std::uint64_t VU_CLOCK_PERIOD =
      EE_CLOCK_HZ / VU_CLOCK_HZ;

    NekoSystem();

    NekoSystem(const NekoSystem &) = delete;
    NekoSystem &operator=(const NekoSystem &) = delete;
    NekoSystem(NekoSystem &&) = delete;
    NekoSystem &operator=(NekoSystem &&) = delete;

    void reset();
    void setInput(const NekoInputState &input);
    const NekoInputState &input() const;
    NekoFrameResult runFrame();
    GSPresentation videoOutput() const;
    NekoAudioFrame audioOutput() const;
    std::vector<std::uint8_t> saveState() const;
    void loadState(const std::vector<std::uint8_t> &state);
    void startTrace();
    void stopTrace();
    void clearTrace();
    bool traceEnabled() const;
    const std::vector<NekoTraceEvent> &trace() const;
    std::uint64_t traceHash() const;

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
    GIFDMACChannel &gifDMAC();
    const GIFDMACChannel &gifDMAC() const;
    GSDisplay &gsDisplay();
    const GSDisplay &gsDisplay() const;
    EEBus &eeBus();
    const EEBus &eeBus() const;
    EEInterruptController &interruptController();
    const EEInterruptController &interruptController() const;
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
    MasterClockScheduler masterClock;
    EEBus eeBusComponent;
    GIFDMACChannel gifDMACComponent;
    GSDisplay gsDisplayComponent;
    NekoInputState inputState;
    bool collectingTrace = false;
    std::vector<NekoTraceEvent> traceEvents;

    void synchronizeInterrupts();
    EEExecutionResult makeEEExecutionResult(
      std::uint64_t masterCycles,
      std::uint64_t startingEECycles,
      std::uint64_t instructions,
      bool cycleLimitReached) const;
    void recordCycleTrace(
      std::uint64_t cycle,
      std::uint64_t vu0Cycles,
      std::uint64_t vu1Cycles,
      std::uint64_t vif0Words,
      std::uint64_t vif1Words,
      std::uint64_t gifQuadwords,
      std::uint64_t dmacQuadwords,
      std::uint32_t dmacControl,
      std::uint32_t interruptStatus,
      std::uint64_t pixels,
      std::uint64_t presentationBoundary);
    void appendTrace(
      std::uint64_t cycle,
      NekoTraceSubsystem subsystem,
      NekoTraceEventType type,
      std::uint64_t value0,
      std::uint64_t value1 = 0,
      std::uint64_t value2 = 0);
};

#endif
