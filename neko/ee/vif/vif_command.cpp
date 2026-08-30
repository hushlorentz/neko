#include <stdexcept>

#include "vif_command.hpp"

namespace
{
  constexpr std::uint8_t VIF_COMMAND_MASK = 0x7f;
  constexpr std::uint8_t VIF_UNPACK_COMMAND_MASK = 0x60;
  constexpr std::uint8_t VIF_UNPACK_FORMAT_MASK = 0x0f;
  constexpr std::uint16_t VIF_ZERO_COUNT = 256;

  bool vif1Only(VIFCommandKind kind)
  {
    switch (kind)
    {
      case VIFCommandKind::OFFSET:
      case VIFCommandKind::BASE:
      case VIFCommandKind::MSKPATH3:
      case VIFCommandKind::FLUSH:
      case VIFCommandKind::FLUSHA:
      case VIFCommandKind::MSCALF:
      case VIFCommandKind::DIRECT:
      case VIFCommandKind::DIRECTHL:
        return true;
      default:
        return false;
    }
  }

  VIFUnpackFormat unpackFormat(std::uint8_t encoding)
  {
    using namespace VIFUnpackEncoding;
    switch (encoding)
    {
      case S_32: return VIFUnpackFormat::S_32;
      case S_16: return VIFUnpackFormat::S_16;
      case S_8: return VIFUnpackFormat::S_8;
      case V2_32: return VIFUnpackFormat::V2_32;
      case V2_16: return VIFUnpackFormat::V2_16;
      case V2_8: return VIFUnpackFormat::V2_8;
      case V3_32: return VIFUnpackFormat::V3_32;
      case V3_16: return VIFUnpackFormat::V3_16;
      case V3_8: return VIFUnpackFormat::V3_8;
      case V4_32: return VIFUnpackFormat::V4_32;
      case V4_16: return VIFUnpackFormat::V4_16;
      case V4_8: return VIFUnpackFormat::V4_8;
      case V4_5: return VIFUnpackFormat::V4_5;
      default:
        throw std::runtime_error("Unsupported VIF UNPACK format.");
    }
  }

  VIFCommandKind commandKind(std::uint8_t command)
  {
    using namespace VIFCommandEncoding;
    switch (command)
    {
      case NOP: return VIFCommandKind::NOP;
      case STCYCL: return VIFCommandKind::STCYCL;
      case OFFSET: return VIFCommandKind::OFFSET;
      case BASE: return VIFCommandKind::BASE;
      case ITOP: return VIFCommandKind::ITOP;
      case STMOD: return VIFCommandKind::STMOD;
      case MSKPATH3: return VIFCommandKind::MSKPATH3;
      case MARK: return VIFCommandKind::MARK;
      case FLUSHE: return VIFCommandKind::FLUSHE;
      case FLUSH: return VIFCommandKind::FLUSH;
      case FLUSHA: return VIFCommandKind::FLUSHA;
      case MSCAL: return VIFCommandKind::MSCAL;
      case MSCALF: return VIFCommandKind::MSCALF;
      case MSCNT: return VIFCommandKind::MSCNT;
      case STMASK: return VIFCommandKind::STMASK;
      case STROW: return VIFCommandKind::STROW;
      case STCOL: return VIFCommandKind::STCOL;
      case MPG: return VIFCommandKind::MPG;
      case DIRECT: return VIFCommandKind::DIRECT;
      case DIRECTHL: return VIFCommandKind::DIRECTHL;
      default:
        throw std::runtime_error("Unsupported VIF command.");
    }
  }
}

VIFCommand decodeVIFCommand(std::uint32_t code, VIFType type)
{
  VIFCommand decoded;
  decoded.raw = code;
  decoded.immediate = code & 0xffff;
  decoded.encodedCount = (code >> 16) & 0xff;
  decoded.count =
    decoded.encodedCount == 0 ? VIF_ZERO_COUNT : decoded.encodedCount;
  const std::uint8_t encodedCommand = (code >> 24) & 0xff;
  decoded.interrupt =
    (encodedCommand & VIFCommandEncoding::Interrupt) != 0;
  decoded.command = encodedCommand & VIF_COMMAND_MASK;

  if ((decoded.command & VIF_UNPACK_COMMAND_MASK) ==
      VIFCommandEncoding::UNPACK)
  {
    decoded.kind = VIFCommandKind::UNPACK;
    decoded.unpackFormat = unpackFormat(
      decoded.command & VIF_UNPACK_FORMAT_MASK);
    decoded.masked =
      (decoded.command & VIFCommandEncoding::UNPACKMask) != 0;
    decoded.address =
      decoded.immediate & VIFImmediateEncoding::AddressMask;
    decoded.unsignedData =
      (decoded.immediate & VIFImmediateEncoding::UNPACKUnsigned) != 0;
    decoded.addTops =
      type == VIFType::VIF1 &&
      (decoded.immediate & VIFImmediateEncoding::UNPACKAddTOPS) != 0;
  }
  else
  {
    decoded.kind = commandKind(decoded.command);
  }

  if (type == VIFType::VIF0 && vif1Only(decoded.kind))
  {
    throw std::runtime_error("VIF command is only supported on VIF1.");
  }

  if (decoded.kind == VIFCommandKind::STMOD &&
      (decoded.immediate & 0x3) == 0x3)
  {
    throw std::runtime_error("Unsupported VIF addition mode.");
  }

  return decoded;
}
