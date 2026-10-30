#ifndef EE_BUS_HPP
#define EE_BUS_HPP

#include <cstdint>
#include <vector>

#include "boot_rom.hpp"
#include "ee_types.hpp"
#include "gif.hpp"

class EEInterruptController;
class EETimers;
class EESerial;
class EERDRAMInitialization;
class DMACController;
class GIFRegisters;
class GIFDMACChannel;
class SIF1DMACChannel;
class SIF0DMACChannel;
class GIFPath3Transfer;
class GS;
class GSDisplay;
class ScratchpadDMACChannel;
class SIF;
class SIFSignalReceiver;
class NekoSystem;
class IOPBus;
class VIF;
class VIF1DMACChannel;
class VPU;

namespace EEMemoryMap
{
  constexpr std::uint32_t MAIN_MEMORY_SIZE =
    32 * 1024 * 1024;
  constexpr std::uint32_t BOOT_ROM_BASE =
    UINT32_C(0x1fc00000);
  constexpr std::uint32_t BOOT_ROM_SIZE =
    4 * 1024 * 1024;
  constexpr std::uint32_t BOOT_ROM_END =
    BOOT_ROM_BASE + BOOT_ROM_SIZE;
  constexpr std::uint32_t VU0_DATA_MEMORY_BASE =
    UINT32_C(0x11004000);
  constexpr std::uint32_t VU0_DATA_MEMORY_END =
    UINT32_C(0x11008000);
  constexpr std::uint32_t VU1_DATA_MEMORY_BASE =
    UINT32_C(0x1100c000);
  constexpr std::uint32_t VU1_DATA_MEMORY_END =
    UINT32_C(0x11010000);
  constexpr std::uint32_t KSEG0_BASE = UINT32_C(0x80000000);
  constexpr std::uint32_t KSEG1_BASE = UINT32_C(0xa0000000);
  constexpr std::uint32_t KSEG2_BASE = UINT32_C(0xc0000000);

  constexpr std::uint32_t GIF_BASE = 0x10003000;
  constexpr std::uint32_t GIF_CTRL = GIF_BASE;
  constexpr std::uint32_t SIF_BASE = 0x1000f200;
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

  constexpr std::uint32_t D1_CHCR = 0x10009000;
  constexpr std::uint32_t D1_MADR = 0x10009010;
  constexpr std::uint32_t D1_QWC = 0x10009020;
  constexpr std::uint32_t D1_TADR = 0x10009030;
  constexpr std::uint32_t D1_ASR0 = 0x10009040;
  constexpr std::uint32_t D1_ASR1 = 0x10009050;
  constexpr std::uint32_t D2_CHCR = 0x1000a000;
  constexpr std::uint32_t D6_CHCR = 0x1000c400;
  constexpr std::uint32_t D5_CHCR = 0x1000c000;
  constexpr std::uint32_t D5_MADR = 0x1000c010;
  constexpr std::uint32_t D5_QWC = 0x1000c020;
  constexpr std::uint32_t D6_MADR = 0x1000c410;
  constexpr std::uint32_t D6_QWC = 0x1000c420;
  constexpr std::uint32_t D6_TADR = 0x1000c430;
  constexpr std::uint32_t D2_MADR = 0x1000a010;
  constexpr std::uint32_t D2_QWC = 0x1000a020;
  constexpr std::uint32_t D2_TADR = 0x1000a030;
  constexpr std::uint32_t D2_ASR0 = 0x1000a040;
  constexpr std::uint32_t D2_ASR1 = 0x1000a050;
  constexpr std::uint32_t D8_CHCR = 0x1000d000;
  constexpr std::uint32_t D8_MADR = 0x1000d010;
  constexpr std::uint32_t D8_QWC = 0x1000d020;
  constexpr std::uint32_t D8_SADR = 0x1000d080;
  constexpr std::uint32_t D9_CHCR = 0x1000d400;
  constexpr std::uint32_t D9_MADR = 0x1000d410;
  constexpr std::uint32_t D9_QWC = 0x1000d420;
  constexpr std::uint32_t D9_TADR = 0x1000d430;
  constexpr std::uint32_t D9_SADR = 0x1000d480;
  constexpr std::uint32_t D_CTRL = 0x1000e000;
  constexpr std::uint32_t D_STAT = 0x1000e010;
  constexpr std::uint32_t D_PCR = 0x1000e020;
  constexpr std::uint32_t D_SQWC = 0x1000e030;
  constexpr std::uint32_t D_STADR = 0x1000e060;

  constexpr std::uint32_t INTC_STAT = 0x1000f000;
  constexpr std::uint32_t INTC_MASK = 0x1000f010;
  constexpr std::uint32_t SIO_LCR = 0x1000f100;
  constexpr std::uint32_t SIO_IER = 0x1000f120;
  constexpr std::uint32_t SIO_ISR = 0x1000f130;
  constexpr std::uint32_t SIO_FCR = 0x1000f140;
  constexpr std::uint32_t SIO_BGR = 0x1000f150;
  constexpr std::uint32_t SIO_TXFIFO = 0x1000f180;
  constexpr std::uint32_t MCH_WINDOW_BASE = 0x1000f400;
  constexpr std::uint32_t MCH_WINDOW_END = 0x1000f500;
  constexpr std::uint32_t MCH_RICM = 0x1000f430;
  constexpr std::uint32_t MCH_DRD = 0x1000f440;
  constexpr std::uint32_t RESERVED_F500 = 0x1000f500;
  constexpr std::uint32_t D_ENABLER = 0x1000f520;
  constexpr std::uint32_t D_ENABLEW = 0x1000f590;
  constexpr std::uint32_t BA0_REGISTER_04 = 0x1a000004;
  constexpr std::uint32_t BA0_INIT_06 = 0x1a000006;
  constexpr std::uint32_t BA0_REGISTER_08 = 0x1a000008;
  constexpr std::uint32_t IOP_BOOT_PROBE = 0x1f803204;
  constexpr std::uint32_t IOP_RAM_BASE = 0x1c000000;
  constexpr std::uint32_t IOP_RAM_END = 0x1c200000;
  constexpr std::uint32_t IOP_DEV9_REVISION = 0x1f80146e;
  constexpr std::uint32_t IOP_DEV9_INIT_1470 = 0x1f801470;
  constexpr std::uint32_t IOP_DEV9_INIT_1472 = 0x1f801472;

  constexpr std::uint32_t GS_BUSDIR = 0x12001040;
  constexpr std::uint32_t GS_PMODE = 0x12000000;
  constexpr std::uint32_t GS_SMODE1 = 0x12000010;
  constexpr std::uint32_t GS_SMODE2 = 0x12000020;
  constexpr std::uint32_t GS_SRFSH = 0x12000030;
  constexpr std::uint32_t GS_SYNCH1 = 0x12000040;
  constexpr std::uint32_t GS_SYNCH2 = 0x12000050;
  constexpr std::uint32_t GS_SYNCV = 0x12000060;
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

enum class EEDataWriteResult : std::uint8_t
{
  Completed,
  Stalled,
  Failed
};

enum class EEWordMergeStorePolicy : std::uint8_t
{
  ReadModifyWrite,
  FullWordOnly
};

enum class EEScratchpadDMACMode : std::uint8_t
{
  Normal,
  Interleave
};

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
    void attachDMACController(DMACController *dmac);
    void attachGIFDMACChannel(GIFDMACChannel *gifDMAC);
    void attachVIF1DMACChannel(VIF1DMACChannel *vif1DMAC);
    void attachSIF1DMACChannel(SIF1DMACChannel *sif1DMAC);
    void attachSIF0DMACChannel(SIF0DMACChannel *sif0DMAC);
    void attachFromScratchpadDMACChannel(
      ScratchpadDMACChannel *fromScratchpadDMAC);
    void attachToScratchpadDMACChannel(
      ScratchpadDMACChannel *toScratchpadDMAC);
    void attachVectorUnits(VPU *vu0, VPU *vu1);
    void attachGSDisplay(GSDisplay *gsDisplay);
    void attachTimers(EETimers *timers);
    void attachSerial(EESerial *serial);
    void attachRDRAMInitialization(EERDRAMInitialization *initialization);
    // The caller keeps the sole IOP RAM owner alive while attached.
    void attachIOPRAM(IOPBus *bus);
    // The caller keeps both the shared owner and signal receiver alive.
    void attachSIF(SIF *sif, SIFSignalReceiver *signals);
    void installBootROM(
      const std::vector<std::uint8_t> &image);
    void installBootROM(
      const std::shared_ptr<const BootROMImage> &image);
    const std::shared_ptr<const BootROMImage> &bootROMIdentity() const;
    bool isMainMemoryRange(
      std::uint32_t address,
      std::size_t width) const;
    // Physical, side-effect-free backing eligible for cache refill.
    bool isCacheFillRange(
      std::uint32_t physicalAddress,
      std::size_t width) const;

    bool readInstruction32(
      std::uint32_t address,
      std::uint32_t *instruction) const;
    bool readData8(
      std::uint32_t address,
      std::uint8_t *value) const;
    bool writeData8(
      std::uint32_t address,
      std::uint8_t value);
    bool readData16(
      std::uint32_t address,
      std::uint16_t *value) const;
    bool writeData16(
      std::uint32_t address,
      std::uint16_t value);
    bool readData32(
      std::uint32_t address,
      std::uint32_t *value) const;
    bool writeData32(
      std::uint32_t address,
      std::uint32_t value);
    // Physical byte-enable policy, not a probe for a mapped register.
    EEWordMergeStorePolicy wordMergeStorePolicy(std::uint32_t address) const;
    bool readData64(
      std::uint32_t address,
      std::uint64_t *value) const;
    bool writeData64(
      std::uint32_t address,
      std::uint64_t value);
    bool readData128(
      std::uint32_t address,
      EEQuadword *value) const;
    bool writeData128(
      std::uint32_t address,
      const EEQuadword &value);
    bool readDMAC128(
      std::uint32_t physicalAddress,
      EEQuadword *value) const;
    bool writeDMAC128(
      std::uint32_t physicalAddress,
      const EEQuadword &value);
    bool readScratchpadDMAC128(
      std::uint32_t physicalAddress,
      EEScratchpadDMACMode mode,
      EEQuadword *value) const;
    bool writeScratchpadDMAC128(
      std::uint32_t physicalAddress,
      EEScratchpadDMACMode mode,
      const EEQuadword &value);
    EEDataWriteResult writeGuestData128(
      std::uint32_t address,
      const EEQuadword &value);
    bool guestData128WriteReady(
      std::uint32_t address) const;
    void advanceGuestFIFOs();
    void write8(
      std::uint32_t address,
      std::uint8_t value);
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
    friend class NekoSystem;

    bool mainMemoryAddress(
      std::uint32_t address,
      std::size_t width,
      std::uint32_t *physicalAddress) const;
    bool iopRAMAddress(std::uint32_t address, std::size_t width) const;
    bool readIOPRAM(std::uint32_t address, std::size_t width,
      EEQuadword *value) const;
    bool writeIOPRAM(std::uint32_t address, std::size_t width,
      const EEQuadword &value);
    bool readBacking128(std::uint32_t address, EEQuadword *value) const;
    bool bootROMAddress(
      std::uint32_t address,
      std::size_t width,
      std::uint32_t *offset) const;
    bool readMapped32(
      std::uint32_t address,
      std::uint32_t *value) const;
    bool writeMapped32(
      std::uint32_t address,
      std::uint32_t value,
      bool checkedGuestAccess);
    bool readMapped64(
      std::uint32_t address,
      std::uint64_t *value) const;
    bool writeMapped64(
      std::uint32_t address,
      std::uint64_t value,
      bool checkedGuestAccess);
    std::uint32_t vifStatus(const VIF &vif) const;
    DMACController &attachedDMACController() const;
    GIFDMACChannel &attachedGIFDMAC() const;
    VIF1DMACChannel &attachedVIF1DMAC() const;
    SIF1DMACChannel &attachedSIF1DMAC() const;
    SIF0DMACChannel &attachedSIF0DMAC() const;
    ScratchpadDMACChannel &attachedFromScratchpadDMAC() const;
    ScratchpadDMACChannel &attachedToScratchpadDMAC() const;
    bool vuDataMemoryAddress(
      std::uint32_t address,
      VPU **vpu,
      std::size_t *quadwordIndex) const;
    GSDisplay &attachedGSDisplay() const;
    EESerial &attachedSerial() const;
    EERDRAMInitialization &attachedRDRAMInitialization() const;

    VIF *vif0Component;
    VIF *vif1Component;
    GIFRegisters *gifRegisterFile;
    GIFPath3Transfer *gifPath3Transfer;
    GS *gsComponent;
    EEInterruptController *interruptController;
    DMACController *dmacController = nullptr;
    GIFDMACChannel *gifDMACChannel = nullptr;
    VIF1DMACChannel *vif1DMACChannel = nullptr;
    SIF1DMACChannel *sif1DMACChannel = nullptr;
    SIF0DMACChannel *sif0DMACChannel = nullptr;
    ScratchpadDMACChannel *fromScratchpadDMACChannel = nullptr;
    ScratchpadDMACChannel *toScratchpadDMACChannel = nullptr;
    VPU *vu0Component = nullptr;
    VPU *vu1Component = nullptr;
    GSDisplay *gsDisplayCircuit = nullptr;
    EETimers *timersComponent = nullptr;
    EESerial *serialComponent = nullptr;
    EERDRAMInitialization *rdramInitializationComponent = nullptr;
    SIF *sifComponent = nullptr;
    SIFSignalReceiver *sifSignals = nullptr;
    IOPBus *iopRAMBus = nullptr;
    std::vector<std::uint8_t> mainMemory;
    std::shared_ptr<const BootROMImage> bootROM;
    // Provisional retained halfwords; BA0 protocol and bit meanings are unknown.
    std::uint16_t ba0Register04 = 0;
    std::uint16_t ba0Register08 = 0;
};

#endif
