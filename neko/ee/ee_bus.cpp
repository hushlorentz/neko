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
  std::uint32_t physical = address;
  const std::uint32_t segment = address & 0xf0000000;
  if (segment == 0x20000000 || segment == 0x30000000)
  {
    physical = address & 0x0fffffff;
  }
  else if ((address & 0xe0000000) == 0x80000000 ||
           (address & 0xe0000000) == 0xa0000000)
  {
    physical = address & EE_PHYSICAL_ADDRESS_MASK;
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

std::uint32_t EEBus::read32(std::uint32_t address) const
{
  requireAlignment(
    address,
    4,
    "EE bus 32-bit access must be naturally aligned.");
  std::uint32_t physicalAddress = 0;
  if (mainMemoryAddress(address, 4, &physicalAddress))
  {
    return
      mainMemory[physicalAddress] |
      (static_cast<std::uint32_t>(
        mainMemory[physicalAddress + 1]) << 8) |
      (static_cast<std::uint32_t>(
        mainMemory[physicalAddress + 2]) << 16) |
      (static_cast<std::uint32_t>(
        mainMemory[physicalAddress + 3]) << 24);
  }

  switch (address)
  {
    case EEMemoryMap::VIF0_STAT:
      return vifStatus(*vif0Component);
    case EEMemoryMap::VIF0_CODE:
      return vif0Component->lastCode();
    case EEMemoryMap::VIF1_STAT:
      return vifStatus(*vif1Component);
    case EEMemoryMap::VIF1_CODE:
      return vif1Component->lastCode();
    case EEMemoryMap::GIF_STAT:
      return gifRegisterFile->readStatus();
    case EEMemoryMap::GIF_P3CNT:
      return gifRegisterFile->readPath3Count();
    case EEMemoryMap::GIF_P3TAG:
      return gifRegisterFile->readPath3Tag();
    case EEMemoryMap::D2_CHCR:
      return attachedGIFDMAC().channelControl();
    case EEMemoryMap::D2_MADR:
      return attachedGIFDMAC().memoryAddress();
    case EEMemoryMap::D2_QWC:
      return attachedGIFDMAC().quadwordCount();
    case EEMemoryMap::D2_TADR:
      return attachedGIFDMAC().tagAddress();
    case EEMemoryMap::D2_ASR0:
      return attachedGIFDMAC().addressStack(0);
    case EEMemoryMap::D2_ASR1:
      return attachedGIFDMAC().addressStack(1);
    case EEMemoryMap::D_CTRL:
      return attachedGIFDMAC().globalControl();
    case EEMemoryMap::D_STAT:
      return attachedGIFDMAC().globalStatus();
    case EEMemoryMap::INTC_STAT:
      return interruptController->status();
    case EEMemoryMap::INTC_MASK:
      return interruptController->mask();
    default:
      throw std::out_of_range(
        "EE bus read from an unmapped address.");
  }
}

void EEBus::write32(
  std::uint32_t address,
  std::uint32_t value)
{
  requireAlignment(
    address,
    4,
    "EE bus 32-bit access must be naturally aligned.");
  std::uint32_t physicalAddress = 0;
  if (mainMemoryAddress(address, 4, &physicalAddress))
  {
    for (std::size_t index = 0; index < 4; ++index)
    {
      mainMemory[physicalAddress + index] =
        static_cast<std::uint8_t>(value >> (index * 8));
    }
    return;
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
      return;
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
      return;
    case EEMemoryMap::GIF_MODE:
      gifRegisterFile->writeMode(value);
      return;
    case EEMemoryMap::D2_CHCR:
      attachedGIFDMAC().writeChannelControl(value);
      return;
    case EEMemoryMap::D2_MADR:
      attachedGIFDMAC().writeMemoryAddress(value);
      return;
    case EEMemoryMap::D2_QWC:
      attachedGIFDMAC().writeQuadwordCount(value);
      return;
    case EEMemoryMap::D2_TADR:
      attachedGIFDMAC().writeTagAddress(value);
      return;
    case EEMemoryMap::D2_ASR0:
      attachedGIFDMAC().writeAddressStack(0, value);
      return;
    case EEMemoryMap::D2_ASR1:
      attachedGIFDMAC().writeAddressStack(1, value);
      return;
    case EEMemoryMap::D_CTRL:
      attachedGIFDMAC().writeGlobalControl(value);
      return;
    case EEMemoryMap::D_STAT:
      attachedGIFDMAC().writeGlobalStatus(value);
      return;
    case EEMemoryMap::INTC_STAT:
      interruptController->acknowledge(value);
      return;
    case EEMemoryMap::INTC_MASK:
      interruptController->toggleMask(value);
      return;
    default:
      throw std::out_of_range(
        "EE bus write to an unmapped address.");
  }
}

std::uint64_t EEBus::read64(std::uint32_t address)
{
  requireAlignment(
    address,
    8,
    "EE bus 64-bit access must be naturally aligned.");
  if (address == EEMemoryMap::GS_BUSDIR)
  {
    return gsComponent->hostInterfaceReversed() ? 1 : 0;
  }
  if (address == EEMemoryMap::GS_CSR)
  {
    return attachedGSDisplay().readPrivilegedRegister(
      GSDisplayPrivilegedRegister::CSR);
  }
  if (address == EEMemoryMap::GS_IMR)
  {
    return attachedGSDisplay().readPrivilegedRegister(
      GSDisplayPrivilegedRegister::IMR);
  }
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
  if (address == EEMemoryMap::GS_BUSDIR)
  {
    gsComponent->writePrivilegedRegister(
      GSPrivilegedRegisterAddress::BUSDIR,
      value);
    return;
  }
  switch (address)
  {
    case EEMemoryMap::GS_PMODE:
      attachedGSDisplay().writePrivilegedRegister(
        GSDisplayPrivilegedRegister::PMODE,
        value);
      return;
    case EEMemoryMap::GS_SMODE2:
      attachedGSDisplay().writePrivilegedRegister(
        GSDisplayPrivilegedRegister::SMODE2,
        value);
      return;
    case EEMemoryMap::GS_DISPFB1:
      attachedGSDisplay().writePrivilegedRegister(
        GSDisplayPrivilegedRegister::DISPFB1,
        value);
      return;
    case EEMemoryMap::GS_DISPLAY1:
      attachedGSDisplay().writePrivilegedRegister(
        GSDisplayPrivilegedRegister::DISPLAY1,
        value);
      return;
    case EEMemoryMap::GS_DISPFB2:
      attachedGSDisplay().writePrivilegedRegister(
        GSDisplayPrivilegedRegister::DISPFB2,
        value);
      return;
    case EEMemoryMap::GS_DISPLAY2:
      attachedGSDisplay().writePrivilegedRegister(
        GSDisplayPrivilegedRegister::DISPLAY2,
        value);
      return;
    case EEMemoryMap::GS_BGCOLOR:
      attachedGSDisplay().writePrivilegedRegister(
        GSDisplayPrivilegedRegister::BGCOLOR,
        value);
      return;
    case EEMemoryMap::GS_CSR:
      attachedGSDisplay().writePrivilegedRegister(
        GSDisplayPrivilegedRegister::CSR,
        value);
      return;
    case EEMemoryMap::GS_IMR:
      attachedGSDisplay().writePrivilegedRegister(
        GSDisplayPrivilegedRegister::IMR,
        value);
      return;
    default:
      break;
  }
  write32(address, static_cast<std::uint32_t>(value));
  write32(address + 4, static_cast<std::uint32_t>(value >> 32));
}

GIFQuadword EEBus::readQuadword(std::uint32_t address) const
{
  requireAlignment(
    address,
    16,
    "EE bus quadword access must be naturally aligned.");
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
