#ifndef EE_BUS_HPP
#define EE_BUS_HPP

#include <cstdint>
#include <vector>

#include "gif.hpp"

class EEInterruptController;
class GIFRegisters;
class GIFDMACChannel;
class GIFPath3Transfer;
class GS;
class GSDisplay;
class VIF;

namespace EEMemoryMap
{
  constexpr std::uint32_t MAIN_MEMORY_SIZE =
    32 * 1024 * 1024;

  constexpr std::uint32_t GIF_BASE = 0x10003000;
  constexpr std::uint32_t GIF_MODE = GIF_BASE + 0x10;
  constexpr std::uint32_t GIF_STAT = GIF_BASE + 0x20;
  constexpr std::uint32_t GIF_P3CNT = GIF_BASE + 0x90;
  constexpr std::uint32_t GIF_P3TAG = GIF_BASE + 0xa0;

  constexpr std::uint32_t VIF0_BASE = 0x10003800;
  constexpr std::uint32_t VIF1_BASE = 0x10003c00;
  constexpr std::uint32_t VIF0_STAT = VIF0_BASE;
  constexpr std::uint32_t VIF0_FBRST = VIF0_BASE + 0x10;
  constexpr std::uint32_t VIF0_CODE = VIF0_BASE + 0x80;
  constexpr std::uint32_t VIF1_STAT = VIF1_BASE;
  constexpr std::uint32_t VIF1_FBRST = VIF1_BASE + 0x10;
  constexpr std::uint32_t VIF1_CODE = VIF1_BASE + 0x80;

  constexpr std::uint32_t VIF0_FIFO = 0x10004000;
  constexpr std::uint32_t VIF1_FIFO = 0x10005000;
  constexpr std::uint32_t GIF_FIFO = 0x10006000;

  constexpr std::uint32_t D2_CHCR = 0x1000a000;
  constexpr std::uint32_t D2_MADR = 0x1000a010;
  constexpr std::uint32_t D2_QWC = 0x1000a020;
  constexpr std::uint32_t D2_TADR = 0x1000a030;
  constexpr std::uint32_t D2_ASR0 = 0x1000a040;
  constexpr std::uint32_t D2_ASR1 = 0x1000a050;
  constexpr std::uint32_t D_CTRL = 0x1000e000;
  constexpr std::uint32_t D_STAT = 0x1000e010;

  constexpr std::uint32_t INTC_STAT = 0x1000f000;
  constexpr std::uint32_t INTC_MASK = 0x1000f010;

  constexpr std::uint32_t GS_BUSDIR = 0x12001040;
  constexpr std::uint32_t GS_PMODE = 0x12000000;
  constexpr std::uint32_t GS_SMODE2 = 0x12000020;
  constexpr std::uint32_t GS_DISPFB1 = 0x12000070;
  constexpr std::uint32_t GS_DISPLAY1 = 0x12000080;
  constexpr std::uint32_t GS_DISPFB2 = 0x12000090;
  constexpr std::uint32_t GS_DISPLAY2 = 0x120000a0;
  constexpr std::uint32_t GS_BGCOLOR = 0x120000e0;
  constexpr std::uint32_t GS_CSR = 0x12001000;
  constexpr std::uint32_t GS_IMR = 0x12001010;
}

namespace EEVIFStatus
{
  constexpr std::uint32_t WAITING_FOR_PAYLOAD = 1u;
  constexpr std::uint32_t MARK = 1u << 6;
  constexpr std::uint32_t DOUBLE_BUFFER = 1u << 7;
  constexpr std::uint32_t INTERRUPT_STALL = 1u << 10;
  constexpr std::uint32_t INTERRUPT = 1u << 11;
}

class EEBus
{
  public:
    EEBus(
      VIF *vif0,
      VIF *vif1,
      GIFRegisters *gifRegisters,
      GIFPath3Transfer *gifPath3,
      GS *gs,
      EEInterruptController *interrupts);
    void attachGIFDMACChannel(GIFDMACChannel *gifDMAC);
    void attachGSDisplay(GSDisplay *gsDisplay);

    std::uint32_t read32(std::uint32_t address) const;
    void write32(
      std::uint32_t address,
      std::uint32_t value);
    std::uint64_t read64(std::uint32_t address);
    void write64(
      std::uint32_t address,
      std::uint64_t value);
    GIFQuadword readQuadword(std::uint32_t address) const;
    bool writeQuadword(
      std::uint32_t address,
      const GIFQuadword &value);

  private:
    friend class NekoSaveStateCodec;

    bool mainMemoryAddress(
      std::uint32_t address,
      std::size_t width,
      std::uint32_t *physicalAddress) const;
    std::uint32_t vifStatus(const VIF &vif) const;
    GIFDMACChannel &attachedGIFDMAC() const;
    GSDisplay &attachedGSDisplay() const;

    VIF *vif0Component;
    VIF *vif1Component;
    GIFRegisters *gifRegisterFile;
    GIFPath3Transfer *gifPath3Transfer;
    GS *gsComponent;
    EEInterruptController *interruptController;
    GIFDMACChannel *gifDMACChannel = nullptr;
    GSDisplay *gsDisplayCircuit = nullptr;
    std::vector<std::uint8_t> mainMemory;
};

#endif
