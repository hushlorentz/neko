#ifndef NEKO_SYSTEM_HPP
#define NEKO_SYSTEM_HPP

#include <cstdint>
#include <vector>

#include "clock_scheduler.hpp"
#include "ee_bus.hpp"
#include "gif.hpp"
#include "gif_dmac_channel.hpp"
#include "gif_path1.hpp"
#include "gif_path3.hpp"
#include "gif_path_arbiter.hpp"
#include "gif_registers.hpp"
#include "gs.hpp"
#include "gs_display.hpp"
#include "interrupt_controller.hpp"
#include "system_interfaces.hpp"
#include "vif.hpp"
#include "vpu.hpp"

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
    bool interruptPending() const;
    MasterClockScheduler &masterClockScheduler();
    const MasterClockScheduler &masterClockScheduler() const;

  private:
    friend class NekoSaveStateCodec;

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

    void synchronizeInterrupts();
};

#endif
