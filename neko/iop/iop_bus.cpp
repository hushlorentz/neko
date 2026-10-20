#include "iop_bus.hpp"
#include "iop_interrupt_controller.hpp"
#include "iop_dma_controller.hpp"
#include "iop_sif1_dma_channel.hpp"
#include "iop_sif0_dma_channel.hpp"
#include "iop_timers.hpp"
#include "sif.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <utility>

namespace
{
  constexpr std::array<IOPAddress, IOPTimers::COUNTER_COUNT> timerBases = {{
    0x1f801100, 0x1f801110, 0x1f801120,
    0x1f801480, 0x1f801490, 0x1f8014a0
  }};
  constexpr std::array<IOPAddress, 2> dmaBankBases = {{
    0x1f8010f0, 0x1f801570
  }};
  constexpr IOPAddress sif1DMABase = 0x1f801530;
  constexpr IOPAddress sif0DMABase = 0x1f801520;

  bool isAligned(IOPAddress address, std::size_t width)
  {
    return (address & (width - 1)) == 0;
  }
}

constexpr std::size_t IOPBus::RAM_SIZE;
constexpr std::size_t IOPBus::RAM_MIRROR_COUNT;
constexpr std::size_t IOPBus::SCRATCHPAD_SIZE;
constexpr std::size_t IOPBus::ROM_SIZE;
constexpr IOPAddress IOPBus::RAM_BASE;
constexpr IOPAddress IOPBus::SCRATCHPAD_BASE;
constexpr IOPAddress IOPBus::ROM_BASE;
constexpr IOPAddress IOPBus::INTC_STAT;
constexpr IOPAddress IOPBus::INTC_MASK;
constexpr IOPAddress IOPBus::INTC_CTRL;
constexpr IOPAddress IOPBus::SIF_BASE;
constexpr IOPAddress IOPBus::SIF_DOORBELL;

IOPBus::IOPBus() : ram(RAM_SIZE, 0)
{
}

void IOPBus::reset()
{
  std::fill(ram.begin(), ram.end(), static_cast<std::uint8_t>(0));
  scratchpad.fill(0);
}

void IOPBus::attachInterruptController(IOPInterruptController *interrupts)
{
  interruptController = interrupts;
}

void IOPBus::attachTimers(IOPTimers *attachedTimers)
{
  timers = attachedTimers;
}

void IOPBus::attachDMA(IOPDMAController *controller, IOPSIF1DMAChannel *sif1,
  IOPSIF0DMAChannel *sif0)
{
  if (controller == nullptr || sif1 == nullptr)
  {
    throw std::invalid_argument("IOP bus requires non-null DMA owners.");
  }
  if (dmaController != nullptr)
  {
    throw std::logic_error("IOP bus DMA is already attached.");
  }
  dmaController = controller;
  sif1DMA = sif1;
  sif0DMA = sif0;
}

bool IOPBus::interruptControllerPending() const
{
  return interruptController != nullptr && interruptController->interruptPending();
}

void IOPBus::attachSIF(SIF *sif, SIFSignalReceiver *signals)
{
  if (sif == nullptr || signals == nullptr)
  {
    throw std::invalid_argument(
      "IOP bus requires a non-null SIF owner and signal receiver.");
  }
  if (sifComponent != nullptr)
  {
    throw std::logic_error("IOP bus SIF is already attached.");
  }
  sifComponent = sif;
  sifSignals = signals;
}

bool IOPBus::installRom(
  std::shared_ptr<const BootROMImage> image)
{
  if (image == nullptr || image->size() != ROM_SIZE)
  {
    return false;
  }

  rom = std::move(image);
  return true;
}

const std::shared_ptr<const BootROMImage> &IOPBus::romIdentity() const
{
  return rom;
}

IOPBusRegion IOPBus::regionForRange(
  IOPAddress address,
  std::size_t count) const
{
  return locate(address, count).region;
}

IOPBus::Location IOPBus::locate(IOPAddress address, std::size_t count) const
{
  const std::uint64_t first = address;
  if (count > UINT64_C(0x100000000) - first)
  {
    return {};
  }
  const std::uint64_t last = first + (count == 0 ? 0 : count - 1);

  const std::uint64_t ramEnd =
    static_cast<std::uint64_t>(RAM_SIZE) * RAM_MIRROR_COUNT;
  if (first < ramEnd)
  {
    if (last / RAM_SIZE != first / RAM_SIZE)
    {
      return {};
    }
    return {IOPBusRegion::Ram, static_cast<std::size_t>(first % RAM_SIZE)};
  }

  if (first >= SCRATCHPAD_BASE &&
      last < static_cast<std::uint64_t>(SCRATCHPAD_BASE) + SCRATCHPAD_SIZE)
  {
    return {IOPBusRegion::Scratchpad,
      static_cast<std::size_t>(first - SCRATCHPAD_BASE)};
  }

  if (rom != nullptr && first >= ROM_BASE &&
      last < static_cast<std::uint64_t>(ROM_BASE) + ROM_SIZE)
  {
    return {IOPBusRegion::Rom, static_cast<std::size_t>(first - ROM_BASE)};
  }

  if (first >= INTC_STAT &&
      last < static_cast<std::uint64_t>(INTC_CTRL) + sizeof(IOPWord))
  {
    return {IOPBusRegion::Mmio, static_cast<std::size_t>(first - INTC_STAT),
      Device::InterruptController};
  }

  if (first >= SIF_BASE &&
      last < static_cast<std::uint64_t>(SIF_BASE) + SIFRegisterMap::APERTURE_SIZE)
  {
    return {IOPBusRegion::Mmio, static_cast<std::size_t>(first - SIF_BASE),
      Device::SIFRegisters};
  }
  if (first >= SIF_DOORBELL &&
      last < static_cast<std::uint64_t>(SIF_DOORBELL) + sizeof(IOPWord))
  {
    return {IOPBusRegion::Mmio, static_cast<std::size_t>(first - SIF_DOORBELL),
      Device::SIFDoorbell};
  }

  for (std::size_t index = 0; index < timerBases.size(); ++index)
  {
    if (first >= timerBases[index] &&
        last < static_cast<std::uint64_t>(timerBases[index]) + 12)
    {
      return {IOPBusRegion::Mmio,
        static_cast<std::size_t>(first - timerBases[index]), Device::Timer, index};
    }
  }

  for (std::size_t index = 0; index < dmaBankBases.size(); ++index)
  {
    if (first >= dmaBankBases[index] &&
        last < static_cast<std::uint64_t>(dmaBankBases[index]) + 8)
    {
      return {IOPBusRegion::Mmio,
        static_cast<std::size_t>(first - dmaBankBases[index]),
        Device::DMAController, index};
    }
  }
  if (first >= sif0DMABase &&
      last < static_cast<std::uint64_t>(sif0DMABase) + 16)
  {
    return {IOPBusRegion::Mmio,
      static_cast<std::size_t>(first - sif0DMABase), Device::SIF0DMA};
  }
  if (first >= sif1DMABase &&
      last < static_cast<std::uint64_t>(sif1DMABase) + 12)
  {
    return {IOPBusRegion::Mmio,
      static_cast<std::size_t>(first - sif1DMABase), Device::SIF1DMA};
  }

  return {};
}

const std::uint8_t *IOPBus::readableBytes(const Location &location) const
{
  switch (location.region)
  {
    case IOPBusRegion::Ram:
      return ram.data() + location.offset;
    case IOPBusRegion::Scratchpad:
      return scratchpad.data() + location.offset;
    case IOPBusRegion::Rom:
      return rom->data() + location.offset;
    case IOPBusRegion::Unmapped:
    case IOPBusRegion::Mmio:
      break;
  }
  return nullptr;
}

std::uint8_t *IOPBus::writableBytes(const Location &location)
{
  switch (location.region)
  {
    case IOPBusRegion::Ram:
      return ram.data() + location.offset;
    case IOPBusRegion::Scratchpad:
      return scratchpad.data() + location.offset;
    case IOPBusRegion::Rom:
    case IOPBusRegion::Unmapped:
    case IOPBusRegion::Mmio:
      break;
  }
  return nullptr;
}

IOPBusReadResult IOPBus::readScalar(IOPAddress address, std::size_t width)
{
  IOPBusReadResult result;
  if (!isAligned(address, width))
  {
    result.status = IOPBusStatus::Misaligned;
    return result;
  }

  const Location location = locate(address, width);
  if (location.region == IOPBusRegion::Unmapped)
  {
    return result;
  }
  if (location.region == IOPBusRegion::Mmio)
  {
    return readDeviceRegister(location, width);
  }

  const std::uint8_t *bytes = readableBytes(location);
  for (std::size_t index = 0; index < width; ++index)
  {
    result.value |= static_cast<IOPWord>(bytes[index]) << (8 * index);
  }
  result.status = IOPBusStatus::Completed;
  return result;
}

IOPBusWriteResult IOPBus::writeScalar(
  IOPAddress address,
  std::size_t width,
  IOPWord value)
{
  IOPBusWriteResult result;
  if (!isAligned(address, width))
  {
    result.status = IOPBusStatus::Misaligned;
    return result;
  }

  const Location location = locate(address, width);
  if (location.region == IOPBusRegion::Unmapped)
  {
    return result;
  }
  if (location.region == IOPBusRegion::Rom)
  {
    result.status = IOPBusStatus::ReadOnly;
    return result;
  }
  if (location.region == IOPBusRegion::Mmio)
  {
    return writeDeviceRegister(location, width, value);
  }

  std::uint8_t *bytes = writableBytes(location);
  for (std::size_t index = 0; index < width; ++index)
  {
    bytes[index] = static_cast<std::uint8_t>(value >> (8 * index));
  }
  result.status = IOPBusStatus::Completed;
  return result;
}

IOPBusReadResult IOPBus::read8(IOPAddress address)
{
  return readScalar(address, 1);
}

IOPBusReadResult IOPBus::read16(IOPAddress address)
{
  return readScalar(address, 2);
}

IOPBusReadResult IOPBus::read32(IOPAddress address)
{
  return readScalar(address, 4);
}

IOPBusWriteResult IOPBus::write8(IOPAddress address, std::uint8_t value)
{
  return writeScalar(address, 1, value);
}

IOPBusWriteResult IOPBus::write16(IOPAddress address, std::uint16_t value)
{
  return writeScalar(address, 2, value);
}

IOPBusWriteResult IOPBus::write32(IOPAddress address, IOPWord value)
{
  return writeScalar(address, 4, value);
}

IOPBusWriteResult IOPBus::writeMasked32(
  IOPAddress address,
  IOPWord value,
  std::uint8_t byteEnable)
{
  if (byteEnable == 0 || (byteEnable & 0xf0) != 0)
  {
    throw std::invalid_argument(
      "IOP word write requires a nonzero four-bit byte enable.");
  }
  if (!isAligned(address, sizeof(IOPWord)))
  {
    return {IOPBusStatus::Misaligned};
  }
  const Location location = locate(address, sizeof(IOPWord));
  if (location.region == IOPBusRegion::Unmapped)
  {
    return {IOPBusStatus::Unmapped};
  }
  if (location.region == IOPBusRegion::Rom)
  {
    return {IOPBusStatus::ReadOnly};
  }
  if (location.region == IOPBusRegion::Mmio)
  {
    return byteEnable == 0xf
      ? writeDeviceRegister(location, sizeof(IOPWord), value)
      : IOPBusWriteResult{IOPBusStatus::Unmapped};
  }

  std::uint8_t *bytes = writableBytes(location);
  for (std::uint8_t lane = 0; lane < sizeof(IOPWord); ++lane)
  {
    if ((byteEnable & (1 << lane)) != 0)
    {
      bytes[lane] = static_cast<std::uint8_t>(value >> (8 * lane));
    }
  }
  return {IOPBusStatus::Completed};
}

IOPBusReadResult IOPBus::readDeviceRegister(
  const Location &location,
  std::size_t width)
{
  switch (location.device)
  {
    case Device::InterruptController:
      return width == sizeof(IOPWord)
        ? readInterruptRegister(location.offset) : IOPBusReadResult{};
    case Device::DMAController:
    case Device::SIF1DMA:
    case Device::SIF0DMA:
      return width == sizeof(IOPWord) ? readDMARegister(location)
        : IOPBusReadResult{};
    case Device::Timer:
      return readTimerRegister(location, width);
    case Device::SIFRegisters:
    case Device::SIFDoorbell:
      return readSIFRegister(location, width);
    case Device::None:
      return {};
  }
  return {};
}

IOPBusWriteResult IOPBus::writeDeviceRegister(
  const Location &location,
  std::size_t width,
  IOPWord value)
{
  switch (location.device)
  {
    case Device::InterruptController:
      return width == sizeof(IOPWord)
        ? writeInterruptRegister(location.offset, value) : IOPBusWriteResult{};
    case Device::DMAController:
    case Device::SIF1DMA:
    case Device::SIF0DMA:
      return width == sizeof(IOPWord) ? writeDMARegister(location, value)
        : IOPBusWriteResult{};
    case Device::Timer:
      return writeTimerRegister(location, width, value);
    case Device::SIFRegisters:
    case Device::SIFDoorbell:
      return writeSIFRegister(location, width, value);
    case Device::None:
      return {};
  }
  return {};
}

IOPBusReadResult IOPBus::readDMARegister(const Location &location)
{
  if (location.device == Device::SIF0DMA && sif0DMA != nullptr)
  {
    switch (location.offset)
    {
      case 0:
        return {IOPBusStatus::Completed, sif0DMA->memoryAddress()};
      case 4:
        return {IOPBusStatus::Completed, sif0DMA->blockControl()};
      case 8:
        return {IOPBusStatus::Completed, sif0DMA->channelControl()};
      case 12:
        return {IOPBusStatus::Completed, sif0DMA->tagAddress()};
    }
  }
  if (location.device == Device::DMAController && dmaController != nullptr)
  {
    const auto bank = static_cast<IOPDMABank>(location.deviceIndex);
    switch (location.offset)
    {
      case 0:
        return {IOPBusStatus::Completed, dmaController->priority(bank)};
      case 4:
        return {IOPBusStatus::Completed, dmaController->interruptStatus(bank)};
    }
  }
  if (location.device == Device::SIF1DMA && sif1DMA != nullptr)
  {
    switch (location.offset)
    {
      case 0:
        return {IOPBusStatus::Completed, sif1DMA->memoryAddress()};
      case 4:
        return {IOPBusStatus::Completed, sif1DMA->blockControl()};
      case 8:
        return {IOPBusStatus::Completed, sif1DMA->channelControl()};
    }
  }
  return {};
}

IOPBusWriteResult IOPBus::writeDMARegister(
  const Location &location, IOPWord value)
{
  if (location.device == Device::SIF0DMA && sif0DMA != nullptr)
  {
    IOPDMAWriteOutcome result;
    switch (location.offset)
    {
      case 0:
        result = sif0DMA->writeMemoryAddress(value);
        break;
      case 4:
        result = sif0DMA->writeBlockControl(value);
        break;
      case 8:
        result = sif0DMA->writeChannelControl(value);
        break;
      case 12:
        result = sif0DMA->writeTagAddress(value);
        break;
      default:
        return {};
    }
    return result == IOPDMAWriteOutcome::Completed ?
      IOPBusWriteResult{IOPBusStatus::Completed} : IOPBusWriteResult{};
  }
  if (location.device == Device::DMAController && dmaController != nullptr)
  {
    const auto bank = static_cast<IOPDMABank>(location.deviceIndex);
    switch (location.offset)
    {
      case 0:
        dmaController->writePriority(bank, value);
        return {IOPBusStatus::Completed};
      case 4:
        dmaController->writeInterrupt(bank, value);
        return {IOPBusStatus::Completed};
    }
  }
  if (location.device == Device::SIF1DMA && sif1DMA != nullptr)
  {
    IOPDMAWriteOutcome result;
    switch (location.offset)
    {
      case 0:
        result = sif1DMA->writeMemoryAddress(value);
        break;
      case 4:
        result = sif1DMA->writeBlockControl(value);
        break;
      case 8:
        result = sif1DMA->writeChannelControl(value);
        break;
      default:
        return {};
    }
    if (result == IOPDMAWriteOutcome::Completed)
    {
      return {IOPBusStatus::Completed};
    }
  }
  return {};
}

IOPBusReadResult IOPBus::readSIFRegister(
  const Location &location,
  std::size_t width)
{
  if (sifComponent == nullptr || width != sizeof(IOPWord))
  {
    return {};
  }
  if (location.device == Device::SIFDoorbell)
  {
    return {IOPBusStatus::Completed, sifComponent->iopDoorbell()};
  }
  SIFRegister reg;
  if (!SIFRegisterMap::decodeOffset(
        static_cast<std::uint32_t>(location.offset), reg))
  {
    return {};
  }
  return {IOPBusStatus::Completed, sifComponent->readIOP(reg)};
}

IOPBusWriteResult IOPBus::writeSIFRegister(
  const Location &location,
  std::size_t width,
  IOPWord value)
{
  if (sifComponent == nullptr || width != sizeof(IOPWord))
  {
    return {};
  }
  SIFWriteResult result;
  if (location.device == Device::SIFDoorbell)
  {
    result = sifComponent->writeIOPDoorbell(value);
  }
  else
  {
    SIFRegister reg;
    if (!SIFRegisterMap::decodeOffset(
          static_cast<std::uint32_t>(location.offset), reg))
    {
      return {};
    }
    result = sifComponent->writeIOP(reg, value);
  }
  if (result.outcome != SIFWriteOutcome::Completed)
  {
    return {};
  }
  if (result.signal != SIFSignal::None)
  {
    sifSignals->requestSIFSignal(result.signal);
  }
  return {IOPBusStatus::Completed};
}

IOPBusReadResult IOPBus::readTimerRegister(
  const Location &location,
  std::size_t width)
{
  if (timers == nullptr || (width != 2 && width != 4))
  {
    return {};
  }

  const std::size_t registerOffset = location.offset & ~std::size_t{3};
  const std::size_t halfOffset = location.offset & 3;
  if (halfOffset != 0 &&
      (registerOffset == 4 ||
       IOPTimers::capabilities(location.deviceIndex).counterBits != 32))
  {
    return {};
  }

  IOPWord value = 0;
  switch (registerOffset)
  {
    case 0:
      value = timers->counter(location.deviceIndex).count;
      break;
    case 4:
      value = timers->readMode(location.deviceIndex);
      break;
    case 8:
      value = timers->counter(location.deviceIndex).target;
      break;
    default:
      return {};
  }
  if (width == 2)
  {
    value = (value >> (8 * halfOffset)) & 0xffff;
  }
  return {IOPBusStatus::Completed, value};
}

IOPBusWriteResult IOPBus::writeTimerRegister(
  const Location &location,
  std::size_t width,
  IOPWord value)
{
  if (timers == nullptr || (width != 2 && width != 4))
  {
    return {};
  }
  if (width == 2 && location.offset != 4 &&
      IOPTimers::capabilities(location.deviceIndex).counterBits != 16)
  {
    return {};
  }

  switch (location.offset)
  {
    case 0:
      timers->writeCount(location.deviceIndex, value);
      break;
    case 4:
      timers->writeMode(location.deviceIndex, value);
      break;
    case 8:
      timers->writeTarget(location.deviceIndex, value);
      break;
    default:
      return {};
  }
  return {IOPBusStatus::Completed};
}

IOPBusReadResult IOPBus::readInterruptRegister(std::size_t offset)
{
  if (interruptController == nullptr)
  {
    return {};
  }
  switch (offset)
  {
    case 0:
      return {IOPBusStatus::Completed, interruptController->status()};
    case 4:
      return {IOPBusStatus::Completed, interruptController->mask()};
    case 8:
      return {IOPBusStatus::Completed, interruptController->readControl()};
  }
  return {};
}

IOPBusWriteResult IOPBus::writeInterruptRegister(
  std::size_t offset,
  IOPWord value)
{
  if (interruptController == nullptr)
  {
    return {};
  }
  switch (offset)
  {
    case 0:
      interruptController->writeStatus(value);
      break;
    case 4:
      interruptController->writeMask(value);
      break;
    case 8:
      interruptController->writeControl(value);
      break;
    default:
      return {};
  }
  return {IOPBusStatus::Completed};
}

IOPBusStatus IOPBus::dmaWriteStatus(IOPAddress address) const
{
  if ((address & 3) != 0)
  {
    return IOPBusStatus::Misaligned;
  }
  const auto region = locate(address, sizeof(IOPWord)).region;
  if (region == IOPBusRegion::Ram)
  {
    return IOPBusStatus::Completed;
  }
  return region == IOPBusRegion::Rom ?
    IOPBusStatus::ReadOnly : IOPBusStatus::Unmapped;
}

IOPBusWriteResult IOPBus::writeDMAC32(IOPAddress address, IOPWord value)
{
  const auto status = dmaWriteStatus(address);
  if (status != IOPBusStatus::Completed)
  {
    return {status};
  }
  auto *bytes = writableBytes(locate(address, sizeof(IOPWord)));
  for (std::size_t byte = 0; byte < sizeof(IOPWord); ++byte)
  {
    bytes[byte] = static_cast<std::uint8_t>(value >> (8 * byte));
  }
  return {IOPBusStatus::Completed};
}

IOPBusStatus IOPBus::loadPhysical(
  IOPAddress address,
  const std::uint8_t *bytes,
  std::size_t count)
{
  assert(count == 0 || bytes != nullptr);
  const Location location = locate(address, count);
  if (location.region == IOPBusRegion::Unmapped ||
      location.region == IOPBusRegion::Mmio)
  {
    return IOPBusStatus::Unmapped;
  }
  if (location.region == IOPBusRegion::Rom)
  {
    return IOPBusStatus::ReadOnly;
  }

  std::copy(bytes, bytes + count, writableBytes(location));
  return IOPBusStatus::Completed;
}

IOPBusStatus IOPBus::inspectPhysical(
  IOPAddress address,
  std::uint8_t *bytes,
  std::size_t count) const
{
  assert(count == 0 || bytes != nullptr);
  const Location location = locate(address, count);
  if (location.region == IOPBusRegion::Unmapped ||
      location.region == IOPBusRegion::Mmio)
  {
    return IOPBusStatus::Unmapped;
  }

  const std::uint8_t *source = readableBytes(location);
  std::copy(source, source + count, bytes);
  return IOPBusStatus::Completed;
}
