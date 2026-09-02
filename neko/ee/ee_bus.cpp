#include <stdexcept>

#include "ee_bus.hpp"
#include "gif_registers.hpp"
#include "gif_dmac_channel.hpp"
#include "gif_path3.hpp"
#include "gs.hpp"
#include "gs_display.hpp"
#include "interrupt_controller.hpp"
#include "vif.hpp"

namespace
{
  constexpr std::uint32_t EE_PHYSICAL_ADDRESS_MASK =
    0x1fffffff;
  constexpr std::uint32_t VIF_STALL_CANCEL = 1u << 3;

  std::uint32_t directMappedPhysicalAddress(
    std::uint32_t address)
  {
    if (address >= EEMemoryMap::KSEG0_BASE &&
        address < EEMemoryMap::KSEG2_BASE)
    {
      return address & EE_PHYSICAL_ADDRESS_MASK;
    }
    return address;
  }

  void requireAlignment(
    std::uint32_t address,
    std::uint32_t alignment,
    const char *message)
  {
    if (address % alignment != 0)
    {
      throw std::invalid_argument(message);
    }
  }
}

EEBus::EEBus(
  VIF *vif0,
  VIF *vif1,
  GIFRegisters *gifRegisters,
  GIFPath3Transfer *gifPath3,
  GS *gs,
  EEInterruptController *interrupts) :
  vif0Component(vif0),
  vif1Component(vif1),
  gifRegisterFile(gifRegisters),
  gifPath3Transfer(gifPath3),
  gsComponent(gs),
  interruptController(interrupts),
  mainMemory(EEMemoryMap::MAIN_MEMORY_SIZE, 0)
{
  if (vif0Component == nullptr ||
      vif1Component == nullptr ||
      gifRegisterFile == nullptr ||
      gifPath3Transfer == nullptr ||
      gsComponent == nullptr ||
      interruptController == nullptr)
  {
    throw std::invalid_argument(
      "EE bus requires non-null system components.");
  }
}

bool EEBus::mainMemoryAddress(
  std::uint32_t address,
  std::size_t width,
  std::uint32_t *physicalAddress) const
{
  std::uint32_t physical =
    directMappedPhysicalAddress(address);
  const std::uint32_t segment = address & 0xf0000000;
  if (segment == 0x20000000 || segment == 0x30000000)
  {
    physical = address & 0x0fffffff;
  }
  if (physical >= mainMemory.size() ||
      width > mainMemory.size() - physical)
  {
    return false;
  }
  *physicalAddress = physical;
  return true;
}

void EEBus::attachGIFDMACChannel(GIFDMACChannel *gifDMAC)
{
  if (gifDMAC == nullptr)
  {
    throw std::invalid_argument(
      "EE bus requires a non-null GIF DMAC channel.");
  }
  if (gifDMACChannel != nullptr)
  {
    throw std::logic_error(
      "EE bus GIF DMAC channel is already attached.");
  }
  gifDMACChannel = gifDMAC;
}

void EEBus::attachGSDisplay(GSDisplay *gsDisplay)
{
  if (gsDisplay == nullptr)
  {
    throw std::invalid_argument(
      "EE bus requires a non-null GS display.");
  }
  if (gsDisplayCircuit != nullptr)
  {
    throw std::logic_error(
      "EE bus GS display is already attached.");
  }
  gsDisplayCircuit = gsDisplay;
}

std::uint32_t EEBus::vifStatus(const VIF &vif) const
{
  std::uint32_t status = 0;
  if (vif.awaitingPayload())
  {
    status |= EEVIFStatus::WAITING_FOR_PAYLOAD;
  }
  if (vif.markDetected())
  {
    status |= EEVIFStatus::MARK;
  }
  if (vif.doubleBufferFlag())
  {
    status |= EEVIFStatus::DOUBLE_BUFFER;
  }
  if (vif.interruptPending())
  {
    status |=
      EEVIFStatus::INTERRUPT_STALL |
      EEVIFStatus::INTERRUPT;
  }
  return status;
}

GIFDMACChannel &EEBus::attachedGIFDMAC() const
{
  if (gifDMACChannel == nullptr)
  {
    throw std::logic_error(
      "EE bus GIF DMAC channel is not attached.");
  }
  return *gifDMACChannel;
}

GSDisplay &EEBus::attachedGSDisplay() const
{
  if (gsDisplayCircuit == nullptr)
  {
    throw std::logic_error(
      "EE bus GS display is not attached.");
  }
  return *gsDisplayCircuit;
}

bool EEBus::readInstruction32(
  std::uint32_t address,
  std::uint32_t *instruction) const
{
  if (instruction == nullptr)
  {
    throw std::invalid_argument(
      "EE instruction fetch requires an output value.");
  }
  requireAlignment(
    address,
    4,
    "EE instruction fetch must be word-aligned.");

  std::uint32_t physicalAddress = 0;
  if (!mainMemoryAddress(address, 4, &physicalAddress))
  {
    return false;
  }

  *instruction =
    mainMemory[physicalAddress] |
    (static_cast<std::uint32_t>(
      mainMemory[physicalAddress + 1]) << 8) |
    (static_cast<std::uint32_t>(
      mainMemory[physicalAddress + 2]) << 16) |
    (static_cast<std::uint32_t>(
      mainMemory[physicalAddress + 3]) << 24);
  return true;
}

bool EEBus::readData8(
  std::uint32_t address,
  std::uint8_t *value) const
{
  if (value == nullptr)
  {
    throw std::invalid_argument(
      "EE byte load requires an output value.");
  }
  std::uint32_t physicalAddress = 0;
  if (!mainMemoryAddress(address, 1, &physicalAddress))
  {
    return false;
  }
  *value = mainMemory[physicalAddress];
  return true;
}

bool EEBus::writeData8(
  std::uint32_t address,
  std::uint8_t value)
{
  std::uint32_t physicalAddress = 0;
  if (!mainMemoryAddress(address, 1, &physicalAddress))
  {
    return false;
  }
  mainMemory[physicalAddress] = value;
  return true;
}

bool EEBus::readData16(
  std::uint32_t address,
  std::uint16_t *value) const
{
  if (value == nullptr)
  {
    throw std::invalid_argument(
      "EE halfword load requires an output value.");
  }
  requireAlignment(
    address,
    2,
    "EE halfword load must be naturally aligned.");
  std::uint32_t physicalAddress = 0;
  if (!mainMemoryAddress(address, 2, &physicalAddress))
  {
    return false;
  }
  *value =
    mainMemory[physicalAddress] |
    (static_cast<std::uint16_t>(
      mainMemory[physicalAddress + 1]) << 8);
  return true;
}

bool EEBus::writeData16(
  std::uint32_t address,
  std::uint16_t value)
{
  requireAlignment(
    address,
    2,
    "EE halfword store must be naturally aligned.");
  std::uint32_t physicalAddress = 0;
  if (!mainMemoryAddress(address, 2, &physicalAddress))
  {
    return false;
  }
  mainMemory[physicalAddress] =
    static_cast<std::uint8_t>(value);
  mainMemory[physicalAddress + 1] =
    static_cast<std::uint8_t>(value >> 8);
  return true;
}

bool EEBus::readData32(
  std::uint32_t address,
  std::uint32_t *value) const
{
  if (value == nullptr)
  {
    throw std::invalid_argument(
      "EE word load requires an output value.");
  }
  requireAlignment(
    address,
    4,
    "EE word load must be naturally aligned.");
  return readMapped32(address, value);
}

bool EEBus::writeData32(
  std::uint32_t address,
  std::uint32_t value)
{
  requireAlignment(
    address,
    4,
    "EE word store must be naturally aligned.");
  return writeMapped32(address, value);
}

bool EEBus::readData64(
  std::uint32_t address,
  std::uint64_t *value) const
{
  if (value == nullptr)
  {
    throw std::invalid_argument(
      "EE doubleword load requires an output value.");
  }
  requireAlignment(
    address,
    8,
    "EE doubleword load must be naturally aligned.");
  if (readMapped64(address, value))
  {
    return true;
  }
  return false;
}

bool EEBus::writeData64(
  std::uint32_t address,
  std::uint64_t value)
{
  requireAlignment(
    address,
    8,
    "EE doubleword store must be naturally aligned.");
  return writeMapped64(address, value);
}

bool EEBus::readData128(
  std::uint32_t address,
  EEQuadword *value) const
{
  if (value == nullptr)
  {
    throw std::invalid_argument(
      "EE quadword load requires an output value.");
  }
  requireAlignment(
    address,
    16,
    "EE quadword load must be naturally aligned.");
  std::uint32_t physicalAddress = 0;
  if (!mainMemoryAddress(address, 16, &physicalAddress))
  {
    return false;
  }
  value->low = 0;
  value->high = 0;
  for (std::size_t index = 0; index < 8; ++index)
  {
    value->low |=
      static_cast<std::uint64_t>(
        mainMemory[physicalAddress + index]) <<
      (index * 8);
    value->high |=
      static_cast<std::uint64_t>(
        mainMemory[physicalAddress + 8 + index]) <<
      (index * 8);
  }
  return true;
}

bool EEBus::writeData128(
  std::uint32_t address,
  const EEQuadword &value)
{
  requireAlignment(
    address,
    16,
    "EE quadword store must be naturally aligned.");
  address = directMappedPhysicalAddress(address);
  if (address == EEMemoryMap::VIF0_FIFO ||
      address == EEMemoryMap::VIF1_FIFO ||
      address == EEMemoryMap::GIF_FIFO)
  {
    return writeQuadword(
      address,
      GIFQuadword{{
        static_cast<std::uint32_t>(value.low),
        static_cast<std::uint32_t>(value.low >> 32),
        static_cast<std::uint32_t>(value.high),
        static_cast<std::uint32_t>(value.high >> 32)
      }});
  }
  std::uint32_t physicalAddress = 0;
  if (!mainMemoryAddress(address, 16, &physicalAddress))
  {
    return false;
  }
  for (std::size_t index = 0; index < 8; ++index)
  {
    mainMemory[physicalAddress + index] =
      static_cast<std::uint8_t>(value.low >> (index * 8));
    mainMemory[physicalAddress + 8 + index] =
      static_cast<std::uint8_t>(value.high >> (index * 8));
  }
  return true;
}

bool EEBus::readMapped32(
  std::uint32_t address,
  std::uint32_t *value) const
{
  address = directMappedPhysicalAddress(address);
  std::uint32_t physicalAddress = 0;
  if (mainMemoryAddress(address, 4, &physicalAddress))
  {
    *value =
      mainMemory[physicalAddress] |
      (static_cast<std::uint32_t>(
        mainMemory[physicalAddress + 1]) << 8) |
      (static_cast<std::uint32_t>(
        mainMemory[physicalAddress + 2]) << 16) |
      (static_cast<std::uint32_t>(
        mainMemory[physicalAddress + 3]) << 24);
    return true;
  }

  switch (address)
  {
    case EEMemoryMap::VIF0_STAT:
      *value = vifStatus(*vif0Component);
      return true;
    case EEMemoryMap::VIF0_CODE:
      *value = vif0Component->lastCode();
      return true;
    case EEMemoryMap::VIF1_STAT:
      *value = vifStatus(*vif1Component);
      return true;
    case EEMemoryMap::VIF1_CODE:
      *value = vif1Component->lastCode();
      return true;
    case EEMemoryMap::GIF_STAT:
      *value = gifRegisterFile->readStatus();
      return true;
    case EEMemoryMap::GIF_P3CNT:
      *value = gifRegisterFile->readPath3Count();
      return true;
    case EEMemoryMap::GIF_P3TAG:
      *value = gifRegisterFile->readPath3Tag();
      return true;
    case EEMemoryMap::D2_CHCR:
      *value = attachedGIFDMAC().channelControl();
      return true;
    case EEMemoryMap::D2_MADR:
      *value = attachedGIFDMAC().memoryAddress();
      return true;
    case EEMemoryMap::D2_QWC:
      *value = attachedGIFDMAC().quadwordCount();
      return true;
    case EEMemoryMap::D2_TADR:
      *value = attachedGIFDMAC().tagAddress();
      return true;
    case EEMemoryMap::D2_ASR0:
      *value = attachedGIFDMAC().addressStack(0);
      return true;
    case EEMemoryMap::D2_ASR1:
      *value = attachedGIFDMAC().addressStack(1);
      return true;
    case EEMemoryMap::D_CTRL:
      *value = attachedGIFDMAC().globalControl();
      return true;
    case EEMemoryMap::D_STAT:
      *value = attachedGIFDMAC().globalStatus();
      return true;
    case EEMemoryMap::INTC_STAT:
      *value = interruptController->status();
      return true;
    case EEMemoryMap::INTC_MASK:
      *value = interruptController->mask();
      return true;
    default:
      return false;
  }
}

bool EEBus::writeMapped32(
  std::uint32_t address,
  std::uint32_t value)
{
  address = directMappedPhysicalAddress(address);
  std::uint32_t physicalAddress = 0;
  if (mainMemoryAddress(address, 4, &physicalAddress))
  {
    for (std::size_t index = 0; index < 4; ++index)
    {
      mainMemory[physicalAddress + index] =
        static_cast<std::uint8_t>(value >> (index * 8));
    }
    return true;
  }

  switch (address)
  {
    case EEMemoryMap::VIF0_FBRST:
      if ((value & ~VIF_STALL_CANCEL) != 0)
      {
        throw std::invalid_argument(
          "Unsupported VIF0 FBRST operation.");
      }
      if ((value & VIF_STALL_CANCEL) != 0)
      {
        vif0Component->clearInterrupt();
      }
      return true;
    case EEMemoryMap::VIF1_FBRST:
      if ((value & ~VIF_STALL_CANCEL) != 0)
      {
        throw std::invalid_argument(
          "Unsupported VIF1 FBRST operation.");
      }
      if ((value & VIF_STALL_CANCEL) != 0)
      {
        vif1Component->clearInterrupt();
      }
      return true;
    case EEMemoryMap::GIF_MODE:
      gifRegisterFile->writeMode(value);
      return true;
    case EEMemoryMap::D2_CHCR:
      attachedGIFDMAC().writeChannelControl(value);
      return true;
    case EEMemoryMap::D2_MADR:
      attachedGIFDMAC().writeMemoryAddress(value);
      return true;
    case EEMemoryMap::D2_QWC:
      attachedGIFDMAC().writeQuadwordCount(value);
      return true;
    case EEMemoryMap::D2_TADR:
      attachedGIFDMAC().writeTagAddress(value);
      return true;
    case EEMemoryMap::D2_ASR0:
      attachedGIFDMAC().writeAddressStack(0, value);
      return true;
    case EEMemoryMap::D2_ASR1:
      attachedGIFDMAC().writeAddressStack(1, value);
      return true;
    case EEMemoryMap::D_CTRL:
      attachedGIFDMAC().writeGlobalControl(value);
      return true;
    case EEMemoryMap::D_STAT:
      attachedGIFDMAC().writeGlobalStatus(value);
      return true;
    case EEMemoryMap::INTC_STAT:
      interruptController->acknowledge(value);
      return true;
    case EEMemoryMap::INTC_MASK:
      interruptController->toggleMask(value);
      return true;
    default:
      return false;
  }
}

std::uint32_t EEBus::read32(std::uint32_t address) const
{
  requireAlignment(
    address,
    4,
    "EE bus 32-bit access must be naturally aligned.");
  std::uint32_t value = 0;
  if (readMapped32(address, &value))
  {
    return value;
  }
  throw std::out_of_range(
    "EE bus read from an unmapped address.");
}

void EEBus::write32(
  std::uint32_t address,
  std::uint32_t value)
{
  requireAlignment(
    address,
    4,
    "EE bus 32-bit access must be naturally aligned.");
  if (writeMapped32(address, value))
  {
    return;
  }
  throw std::out_of_range(
    "EE bus write to an unmapped address.");
}

bool EEBus::readMapped64(
  std::uint32_t address,
  std::uint64_t *value) const
{
  address = directMappedPhysicalAddress(address);
  std::uint32_t physicalAddress = 0;
  if (mainMemoryAddress(address, 8, &physicalAddress))
  {
    *value = 0;
    for (std::size_t index = 0; index < 8; ++index)
    {
      *value |=
        static_cast<std::uint64_t>(
          mainMemory[physicalAddress + index]) <<
        (index * 8);
    }
    return true;
  }
  if (address == EEMemoryMap::GS_BUSDIR)
  {
    *value = gsComponent->hostInterfaceReversed() ? 1 : 0;
    return true;
  }
  if (address == EEMemoryMap::GS_CSR)
  {
    *value = attachedGSDisplay().readPrivilegedRegister(
      GSDisplayPrivilegedRegister::CSR);
    return true;
  }
  if (address == EEMemoryMap::GS_IMR)
  {
    *value = attachedGSDisplay().readPrivilegedRegister(
      GSDisplayPrivilegedRegister::IMR);
    return true;
  }
  return false;
}

bool EEBus::writeMapped64(
  std::uint32_t address,
  std::uint64_t value)
{
  address = directMappedPhysicalAddress(address);
  std::uint32_t physicalAddress = 0;
  if (mainMemoryAddress(address, 8, &physicalAddress))
  {
    for (std::size_t index = 0; index < 8; ++index)
    {
      mainMemory[physicalAddress + index] =
        static_cast<std::uint8_t>(value >> (index * 8));
    }
    return true;
  }
  if (address == EEMemoryMap::GS_BUSDIR)
  {
    gsComponent->writePrivilegedRegister(
      GSPrivilegedRegisterAddress::BUSDIR,
      value);
    return true;
  }
  switch (address)
  {
    case EEMemoryMap::GS_PMODE:
      attachedGSDisplay().writePrivilegedRegister(
        GSDisplayPrivilegedRegister::PMODE,
        value);
      return true;
    case EEMemoryMap::GS_SMODE2:
      attachedGSDisplay().writePrivilegedRegister(
        GSDisplayPrivilegedRegister::SMODE2,
        value);
      return true;
    case EEMemoryMap::GS_DISPFB1:
      attachedGSDisplay().writePrivilegedRegister(
        GSDisplayPrivilegedRegister::DISPFB1,
        value);
      return true;
    case EEMemoryMap::GS_DISPLAY1:
      attachedGSDisplay().writePrivilegedRegister(
        GSDisplayPrivilegedRegister::DISPLAY1,
        value);
      return true;
    case EEMemoryMap::GS_DISPFB2:
      attachedGSDisplay().writePrivilegedRegister(
        GSDisplayPrivilegedRegister::DISPFB2,
        value);
      return true;
    case EEMemoryMap::GS_DISPLAY2:
      attachedGSDisplay().writePrivilegedRegister(
        GSDisplayPrivilegedRegister::DISPLAY2,
        value);
      return true;
    case EEMemoryMap::GS_BGCOLOR:
      attachedGSDisplay().writePrivilegedRegister(
        GSDisplayPrivilegedRegister::BGCOLOR,
        value);
      return true;
    case EEMemoryMap::GS_CSR:
      attachedGSDisplay().writePrivilegedRegister(
        GSDisplayPrivilegedRegister::CSR,
        value);
      return true;
    case EEMemoryMap::GS_IMR:
      attachedGSDisplay().writePrivilegedRegister(
        GSDisplayPrivilegedRegister::IMR,
        value);
      return true;
    default:
      return false;
  }
}

std::uint64_t EEBus::read64(std::uint32_t address)
{
  requireAlignment(
    address,
    8,
    "EE bus 64-bit access must be naturally aligned.");
  std::uint64_t value = 0;
  if (readMapped64(address, &value))
  {
    return value;
  }
  address = directMappedPhysicalAddress(address);
  return
    read32(address) |
    (static_cast<std::uint64_t>(read32(address + 4)) << 32);
}

void EEBus::write64(
  std::uint32_t address,
  std::uint64_t value)
{
  requireAlignment(
    address,
    8,
    "EE bus 64-bit access must be naturally aligned.");
  if (writeMapped64(address, value))
  {
    return;
  }
  address = directMappedPhysicalAddress(address);
  write32(address, static_cast<std::uint32_t>(value));
  write32(address + 4, static_cast<std::uint32_t>(value >> 32));
}

GIFQuadword EEBus::readQuadword(std::uint32_t address) const
{
  requireAlignment(
    address,
    16,
    "EE bus quadword access must be naturally aligned.");
  address = directMappedPhysicalAddress(address);
  GIFQuadword value = {};
  for (std::size_t index = 0; index < value.size(); ++index)
  {
    value[index] = read32(
      address + static_cast<std::uint32_t>(index * 4));
  }
  return value;
}

bool EEBus::writeQuadword(
  std::uint32_t address,
  const GIFQuadword &value)
{
  requireAlignment(
    address,
    16,
    "EE bus quadword access must be naturally aligned.");
  address = directMappedPhysicalAddress(address);
  if (address == EEMemoryMap::VIF0_FIFO ||
      address == EEMemoryMap::VIF1_FIFO)
  {
    VIF *vif = address == EEMemoryMap::VIF0_FIFO
      ? vif0Component
      : vif1Component;
    if (vif->interruptPending())
    {
      return false;
    }
    for (const std::uint32_t word : value)
    {
      if (vif->ingestWord(word).stalled)
      {
        return false;
      }
    }
    return true;
  }
  if (address == EEMemoryMap::GIF_FIFO)
  {
    const GIFPath3SubmissionResult result =
      gifPath3Transfer->submitQuadwords(&value, 1);
    return result.transferredQuadwords == 1;
  }

  std::uint32_t physicalAddress = 0;
  if (!mainMemoryAddress(address, 16, &physicalAddress))
  {
    throw std::out_of_range(
      "EE bus quadword write to an unmapped address.");
  }
  for (std::size_t index = 0; index < value.size(); ++index)
  {
    write32(
      address + static_cast<std::uint32_t>(index * 4),
      value[index]);
  }
  return true;
}
