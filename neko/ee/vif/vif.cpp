#include <stdexcept>

#include "vif.hpp"

namespace
{
  constexpr std::uint16_t VIF_STMOD_MASK = 0x0003;
}

VIF::VIF(VIFType type) : type(type)
{
}

VIFType VIF::unitType() const
{
  return type;
}

VIFCommand VIF::processCode(std::uint32_t code)
{
  const VIFCommand command = decodeVIFCommand(code, type);
  codeRegister = code;

  switch (command.kind)
  {
    case VIFCommandKind::NOP:
      break;
    case VIFCommandKind::STCYCL:
      cycleRegister = command.immediate;
      break;
    case VIFCommandKind::OFFSET:
      offsetRegister =
        command.immediate & VIFImmediateEncoding::AddressMask;
      dbf = false;
      topsRegister = baseRegister;
      break;
    case VIFCommandKind::BASE:
      baseRegister =
        command.immediate & VIFImmediateEncoding::AddressMask;
      break;
    case VIFCommandKind::ITOP:
      itopsRegister =
        command.immediate & VIFImmediateEncoding::AddressMask;
      break;
    case VIFCommandKind::STMOD:
      modeRegister = command.immediate & VIF_STMOD_MASK;
      break;
    case VIFCommandKind::MSKPATH3:
      path3Mask =
        (command.immediate & VIFImmediateEncoding::MSKPATH3Mask) != 0;
      break;
    case VIFCommandKind::MARK:
      markRegister = command.immediate;
      markFlag = true;
      break;
    default:
      throw std::runtime_error(
        "VIF command execution requires its owning subsystem.");
  }

  return command;
}

std::uint16_t VIF::cycle() const
{
  return cycleRegister;
}

std::uint8_t VIF::cycleLength() const
{
  return cycleRegister & 0xff;
}

std::uint8_t VIF::writeLength() const
{
  return cycleRegister >> 8;
}

std::uint8_t VIF::mode() const
{
  return modeRegister;
}

std::uint16_t VIF::itops() const
{
  return itopsRegister;
}

std::uint16_t VIF::base() const
{
  return baseRegister;
}

std::uint16_t VIF::offset() const
{
  return offsetRegister;
}

std::uint16_t VIF::tops() const
{
  return topsRegister;
}

std::uint16_t VIF::mark() const
{
  return markRegister;
}

bool VIF::doubleBufferFlag() const
{
  return dbf;
}

bool VIF::path3Masked() const
{
  return path3Mask;
}

bool VIF::markDetected() const
{
  return markFlag;
}

std::uint32_t VIF::lastCode() const
{
  return codeRegister;
}
