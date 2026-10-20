#ifndef IOP_BUS_HPP
#define IOP_BUS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "boot_rom.hpp"
#include "iop_types.hpp"

class IOPInterruptController;
class IOPDMAController;
class IOPSIF1DMAChannel;
class IOPSIF0DMAChannel;
class IOPTimers;
class SIF;
class SIFSignalReceiver;

enum class IOPBusStatus : std::uint8_t
{
  Completed,
  Misaligned,
  ReadOnly,
  Unmapped
};

enum class IOPBusRegion : std::uint8_t
{
  Unmapped,
  Ram,
  Scratchpad,
  Rom,
  Mmio
};

struct IOPBusReadResult
{
  IOPBusStatus status = IOPBusStatus::Unmapped;
  IOPWord value = 0;
};

struct IOPBusWriteResult
{
  IOPBusStatus status = IOPBusStatus::Unmapped;
};

// Physical-address boundary for the IOP. Guest accessors and host
// load/inspect helpers are separate; neither selects CPU exceptions.
class IOPBus final
{
  public:
    static constexpr std::size_t RAM_SIZE = 2 * 1024 * 1024;
    static constexpr std::size_t RAM_MIRROR_COUNT = 4;
    static constexpr std::size_t SCRATCHPAD_SIZE = 1024;
    static constexpr std::size_t ROM_SIZE = BootROMImage::SIZE;
    static constexpr IOPAddress RAM_BASE = 0x00000000;
    static constexpr IOPAddress SCRATCHPAD_BASE = 0x1f800000;
    static constexpr IOPAddress ROM_BASE = 0x1fc00000;
    static constexpr IOPAddress INTC_STAT = 0x1f801070;
    static constexpr IOPAddress INTC_MASK = 0x1f801074;
    static constexpr IOPAddress INTC_CTRL = 0x1f801078;
    static constexpr IOPAddress SIF_BASE = 0x1d000000;
    static constexpr IOPAddress SIF_DOORBELL = 0x1f801450;

    IOPBus();

    // Clears RAM and scratchpad, not attached devices or ROM configuration.
    void reset();
    // The caller owns the controller and must keep it alive while attached.
    void attachInterruptController(IOPInterruptController *interrupts);
    // The caller owns the timers and must keep them alive while attached.
    void attachTimers(IOPTimers *timers);
    // The caller keeps the attached DMA owners alive.
    void attachDMA(IOPDMAController *controller, IOPSIF1DMAChannel *sif1,
      IOPSIF0DMAChannel *sif0 = nullptr);
    // The caller keeps both the shared owner and signal receiver alive.
    void attachSIF(SIF *sif, SIFSignalReceiver *signals);
    // Side-effect-free aggregate level; an absent controller drives no level.
    bool interruptControllerPending() const;

    // The shared image owns an immutable copy of its source bytes.
    bool installRom(
      std::shared_ptr<const BootROMImage> image);
    const std::shared_ptr<const BootROMImage> &romIdentity() const;
    // A zero-length range queries the addressed byte, like host inspection.
    IOPBusRegion regionForRange(IOPAddress address, std::size_t count) const;

    // Guest reads may consume device state. INTC supports word access only.
    IOPBusReadResult read8(IOPAddress address);
    IOPBusReadResult read16(IOPAddress address);
    IOPBusReadResult read32(IOPAddress address);
    IOPBusWriteResult write8(IOPAddress address, std::uint8_t value);
    IOPBusWriteResult write16(IOPAddress address, std::uint16_t value);
    IOPBusWriteResult write32(IOPAddress address, IOPWord value);
    // Devices reject partial masks with Unmapped, without a device read.
    IOPBusWriteResult writeMasked32(
      IOPAddress address,
      IOPWord value,
      std::uint8_t byteEnable);

    // DMA sees physical RAM aliases only, without device or CPU cache access.
    IOPBusReadResult readDMAC32(IOPAddress address) const;
    IOPBusStatus dmaWriteStatus(IOPAddress address) const;
    IOPBusWriteResult writeDMAC32(IOPAddress address, IOPWord value);

    // Host-only physical access. A range must lie wholly inside one RAM
    // alias, the scratchpad, or (inspection only) the installed ROM.
    IOPBusStatus loadPhysical(
      IOPAddress address,
      const std::uint8_t *bytes,
      std::size_t count);
    IOPBusStatus inspectPhysical(
      IOPAddress address,
      std::uint8_t *bytes,
      std::size_t count) const;

  private:
    friend class NekoSaveStateCodec;
    friend class IOPDMAController;
    enum class Device : std::uint8_t
    {
      None,
      InterruptController,
      DMAController,
      SIF1DMA,
      SIF0DMA,
      Timer,
      SIFRegisters,
      SIFDoorbell
    };

    struct Location
    {
      IOPBusRegion region = IOPBusRegion::Unmapped;
      std::size_t offset = 0;
      Device device = Device::None;
      std::size_t deviceIndex = 0;
    };

    Location locate(IOPAddress address, std::size_t count) const;
    const std::uint8_t *readableBytes(const Location &location) const;
    std::uint8_t *writableBytes(const Location &location);
    IOPBusReadResult readScalar(
      IOPAddress address,
      std::size_t width);
    IOPBusWriteResult writeScalar(
      IOPAddress address,
      std::size_t width,
      IOPWord value);
    IOPBusReadResult readDeviceRegister(
      const Location &location,
      std::size_t width);
    IOPBusWriteResult writeDeviceRegister(
      const Location &location,
      std::size_t width,
      IOPWord value);
    IOPBusReadResult readTimerRegister(
      const Location &location,
      std::size_t width);
    IOPBusWriteResult writeTimerRegister(
      const Location &location,
      std::size_t width,
      IOPWord value);
    IOPBusReadResult readDMARegister(const Location &location);
    IOPBusWriteResult writeDMARegister(const Location &location, IOPWord value);
    IOPBusReadResult readSIFRegister(
      const Location &location,
      std::size_t width);
    IOPBusWriteResult writeSIFRegister(
      const Location &location,
      std::size_t width,
      IOPWord value);
    IOPBusReadResult readInterruptRegister(std::size_t offset);
    IOPBusWriteResult writeInterruptRegister(
      std::size_t offset,
      IOPWord value);

    std::vector<std::uint8_t> ram;
    std::array<std::uint8_t, SCRATCHPAD_SIZE> scratchpad = {};
    std::shared_ptr<const BootROMImage> rom;
    IOPInterruptController *interruptController = nullptr;
    IOPTimers *timers = nullptr;
    IOPDMAController *dmaController = nullptr;
    IOPSIF1DMAChannel *sif1DMA = nullptr;
    IOPSIF0DMAChannel *sif0DMA = nullptr;
    SIF *sifComponent = nullptr;
    SIFSignalReceiver *sifSignals = nullptr;
};

#endif
