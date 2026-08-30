#ifndef VIF_COMMAND_H
#define VIF_COMMAND_H

#include <cstdint>

enum class VIFType : std::uint8_t
{
  VIF0,
  VIF1
};

namespace VIFCommandEncoding
{
  constexpr std::uint8_t Interrupt = 0x80;
  constexpr std::uint8_t NOP = 0x00;
  constexpr std::uint8_t STCYCL = 0x01;
  constexpr std::uint8_t OFFSET = 0x02;
  constexpr std::uint8_t BASE = 0x03;
  constexpr std::uint8_t ITOP = 0x04;
  constexpr std::uint8_t STMOD = 0x05;
  constexpr std::uint8_t MSKPATH3 = 0x06;
  constexpr std::uint8_t MARK = 0x07;
  constexpr std::uint8_t FLUSHE = 0x10;
  constexpr std::uint8_t FLUSH = 0x11;
  constexpr std::uint8_t FLUSHA = 0x13;
  constexpr std::uint8_t MSCAL = 0x14;
  constexpr std::uint8_t MSCALF = 0x15;
  constexpr std::uint8_t MSCNT = 0x17;
  constexpr std::uint8_t STMASK = 0x20;
  constexpr std::uint8_t STROW = 0x30;
  constexpr std::uint8_t STCOL = 0x31;
  constexpr std::uint8_t MPG = 0x4a;
  constexpr std::uint8_t DIRECT = 0x50;
  constexpr std::uint8_t DIRECTHL = 0x51;
  constexpr std::uint8_t UNPACK = 0x60;
  constexpr std::uint8_t UNPACKMask = 0x10;
}

namespace VIFImmediateEncoding
{
  constexpr std::uint16_t AddressMask = 0x03ff;
  constexpr std::uint16_t MSKPATH3Mask = 0x8000;
  constexpr std::uint16_t UNPACKUnsigned = 0x4000;
  constexpr std::uint16_t UNPACKAddTOPS = 0x8000;
}

namespace VIFUnpackEncoding
{
  constexpr std::uint8_t S_32 = 0x0;
  constexpr std::uint8_t S_16 = 0x1;
  constexpr std::uint8_t S_8 = 0x2;
  constexpr std::uint8_t V2_32 = 0x4;
  constexpr std::uint8_t V2_16 = 0x5;
  constexpr std::uint8_t V2_8 = 0x6;
  constexpr std::uint8_t V3_32 = 0x8;
  constexpr std::uint8_t V3_16 = 0x9;
  constexpr std::uint8_t V3_8 = 0xa;
  constexpr std::uint8_t V4_32 = 0xc;
  constexpr std::uint8_t V4_16 = 0xd;
  constexpr std::uint8_t V4_8 = 0xe;
  constexpr std::uint8_t V4_5 = 0xf;
}

enum class VIFCommandKind : std::uint8_t
{
  NOP,
  STCYCL,
  OFFSET,
  BASE,
  ITOP,
  STMOD,
  MSKPATH3,
  MARK,
  FLUSHE,
  FLUSH,
  FLUSHA,
  MSCAL,
  MSCALF,
  MSCNT,
  STMASK,
  STROW,
  STCOL,
  MPG,
  DIRECT,
  DIRECTHL,
  UNPACK
};

enum class VIFUnpackFormat : std::uint8_t
{
  None,
  S_32,
  S_16,
  S_8,
  V2_32,
  V2_16,
  V2_8,
  V3_32,
  V3_16,
  V3_8,
  V4_32,
  V4_16,
  V4_8,
  V4_5
};

struct VIFCommand
{
  VIFCommandKind kind = VIFCommandKind::NOP;
  VIFUnpackFormat unpackFormat = VIFUnpackFormat::None;
  std::uint32_t raw = 0;
  std::uint16_t immediate = 0;
  std::uint16_t count = 0;
  std::uint16_t address = 0;
  std::uint8_t encodedCount = 0;
  std::uint8_t command = 0;
  bool interrupt = false;
  bool masked = false;
  bool unsignedData = false;
  bool addTops = false;
};

VIFCommand decodeVIFCommand(std::uint32_t code, VIFType type);

#endif
