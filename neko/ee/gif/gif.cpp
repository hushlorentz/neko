#include <stdexcept>

#include "gif.hpp"

namespace
{
  constexpr std::uint64_t GIF_NLOOP_MASK = 0x7fff;
  constexpr std::uint64_t GIF_EOP_MASK = 1ull << 15;
  constexpr std::uint64_t GIF_PRE_MASK = 1ull << 46;
  constexpr std::uint64_t GIF_PRIM_MASK = 0x07ff;
  constexpr std::uint8_t GIF_PRIM_SHIFT = 47;
  constexpr std::uint64_t GIF_FORMAT_MASK = 0x03;
  constexpr std::uint8_t GIF_FORMAT_SHIFT = 58;
  constexpr std::uint64_t GIF_NREG_MASK = 0x0f;
  constexpr std::uint8_t GIF_NREG_SHIFT = 60;
  constexpr std::uint8_t GIF_MAX_REGISTER_COUNT = 16;
  constexpr std::uint8_t GIF_DESCRIPTOR_BITS = 4;
  constexpr std::uint8_t GIF_WORD_BITS = 32;
  constexpr std::uint32_t GIF_Q_INITIAL_VALUE = 0x3f800000;
  constexpr std::uint64_t GIF_COLOR_COMPONENT_MASK = 0xff;
  constexpr std::uint64_t GIF_UV_COMPONENT_MASK = 0x7fff;
  constexpr std::uint64_t GIF_XY_COMPONENT_MASK = 0xffff;
  constexpr std::uint64_t GIF_XYZF_Z_MASK = 0xffffff;
  constexpr std::uint64_t GIF_XYZ_Z_MASK = 0xffffffff;
  constexpr std::uint64_t GIF_FOG_MASK = 0xff;
  constexpr std::uint8_t GIF_GREEN_SHIFT = 8;
  constexpr std::uint8_t GIF_BLUE_SHIFT = 16;
  constexpr std::uint8_t GIF_ALPHA_SHIFT = 24;
  constexpr std::uint8_t GIF_Q_SHIFT = 32;
  constexpr std::uint8_t GIF_V_SHIFT = 16;
  constexpr std::uint8_t GIF_Y_SHIFT = 16;
  constexpr std::uint8_t GIF_Z_SHIFT = 32;
  constexpr std::uint8_t GIF_FOG_SHIFT = 56;
  constexpr std::uint8_t GIF_XYZF_INPUT_SHIFT = 4;
  constexpr std::uint8_t GIF_ADC_WORD = 3;
  constexpr std::uint8_t GIF_ADC_BIT = 15;
  constexpr std::uint8_t GIF_AD_ADDRESS_MASK = 0xff;

  std::uint64_t makeDoubleword(
    std::uint32_t low,
    std::uint32_t high)
  {
    return
      static_cast<std::uint64_t>(low) |
      (static_cast<std::uint64_t>(high) << GIF_WORD_BITS);
  }

  GIFRegisterWrite registerWrite(
    std::uint8_t address,
    std::uint64_t data)
  {
    GIFRegisterWrite write;
    write.address = address;
    write.data = data;
    return write;
  }

  void validateTag(const GIFTag &tag)
  {
    if (tag.loopCount == 0 ||
        tag.format == GIFDataFormat::Image ||
        tag.format == GIFDataFormat::Disabled)
    {
      return;
    }

    for (std::uint8_t index = 0;
         index < tag.registerCount;
         ++index)
    {
      const std::uint8_t descriptor =
        (tag.registers >>
         (index * GIF_DESCRIPTOR_BITS)) &
        GIF_NREG_MASK;
      if (descriptor == GIFRegisterDescriptor::Reserved)
      {
        throw std::runtime_error(
          "GIF tag uses the reserved register descriptor.");
      }
    }
  }
}

GIFTag decodeGIFTag(const GIFQuadword &quadword)
{
  const std::uint64_t low =
    makeDoubleword(quadword[0], quadword[1]);
  GIFTag tag;
  tag.loopCount = low & GIF_NLOOP_MASK;
  tag.endOfPacket = (low & GIF_EOP_MASK) != 0;
  tag.primitiveEnabled = (low & GIF_PRE_MASK) != 0;
  tag.primitive =
    (low >> GIF_PRIM_SHIFT) & GIF_PRIM_MASK;
  tag.format = static_cast<GIFDataFormat>(
    (low >> GIF_FORMAT_SHIFT) & GIF_FORMAT_MASK);
  const std::uint8_t encodedRegisterCount =
    (low >> GIF_NREG_SHIFT) & GIF_NREG_MASK;
  tag.registerCount =
    encodedRegisterCount == 0
      ? GIF_MAX_REGISTER_COUNT
      : encodedRegisterCount;
  tag.registers =
    makeDoubleword(quadword[2], quadword[3]);
  return tag;
}

GIFDecodeResult GIFDecoder::ingestQuadword(
  const GIFQuadword &quadword)
{
  GIFDecodeResult result;
  if (waitingForTag)
  {
    beginPrimitive(decodeGIFTag(quadword), &result);
    dispatchWrites(result);
    return result;
  }

  switch (tag.format)
  {
    case GIFDataFormat::Packed:
      consumePacked(quadword, &result);
      break;
    case GIFDataFormat::RegisterList:
      consumeRegisterList(quadword, &result);
      break;
    case GIFDataFormat::Image:
    case GIFDataFormat::Disabled:
      consumeImage(quadword, &result);
      break;
  }

  --remainingQuadwords;
  if (remainingQuadwords == 0)
  {
    completePrimitive(&result);
  }
  dispatchWrites(result);
  return result;
}

void GIFDecoder::attachRegisterWriteHandler(
  GIFRegisterWriteHandler *attachedHandler)
{
  if (attachedHandler == nullptr)
  {
    throw std::invalid_argument(
      "Cannot attach a null GIF register-write handler.");
  }
  if (packetInProgress())
  {
    throw std::runtime_error(
      "Cannot attach a GIF register-write handler during a packet.");
  }

  writeHandler = attachedHandler;
}

void GIFDecoder::beginPrimitive(
  const GIFTag &newTag,
  GIFDecodeResult *result)
{
  validateTag(newTag);
  tag = newTag;
  qValue = GIF_Q_INITIAL_VALUE;
  currentLoop = 0;
  currentRegister = 0;
  activePacket = true;
  result->tagDecoded = true;
  result->tag = tag;

  if (tag.loopCount == 0)
  {
    completePrimitive(result);
    return;
  }

  switch (tag.format)
  {
    case GIFDataFormat::Packed:
      remainingQuadwords =
        static_cast<std::uint32_t>(tag.loopCount) *
        tag.registerCount;
      if (tag.primitiveEnabled)
      {
        result->writes.push_back(registerWrite(
          GIFRegisterAddress::PRIM,
          tag.primitive));
      }
      break;
    case GIFDataFormat::RegisterList:
      remainingRegisterValues =
        static_cast<std::uint32_t>(tag.loopCount) *
        tag.registerCount;
      remainingQuadwords =
        (remainingRegisterValues + 1) / 2;
      break;
    case GIFDataFormat::Image:
    case GIFDataFormat::Disabled:
      remainingQuadwords = tag.loopCount;
      break;
  }

  waitingForTag = false;
}

void GIFDecoder::consumePacked(
  const GIFQuadword &quadword,
  GIFDecodeResult *result)
{
  const std::uint8_t packedDescriptor = descriptor();
  const std::uint64_t lower =
    makeDoubleword(quadword[0], quadword[1]);

  switch (packedDescriptor)
  {
    case GIFRegisterDescriptor::PRIM:
      result->writes.push_back(registerWrite(
        GIFRegisterAddress::PRIM,
        quadword[0] & GIF_PRIM_MASK));
      break;
    case GIFRegisterDescriptor::RGBAQ:
    {
      const std::uint64_t packed =
        (quadword[0] & GIF_COLOR_COMPONENT_MASK) |
        ((quadword[1] & GIF_COLOR_COMPONENT_MASK) <<
         GIF_GREEN_SHIFT) |
        ((quadword[2] & GIF_COLOR_COMPONENT_MASK) <<
         GIF_BLUE_SHIFT) |
        ((quadword[3] & GIF_COLOR_COMPONENT_MASK) <<
         GIF_ALPHA_SHIFT) |
        (static_cast<std::uint64_t>(qValue) << GIF_Q_SHIFT);
      result->writes.push_back(registerWrite(
        GIFRegisterAddress::RGBAQ,
        packed));
      break;
    }
    case GIFRegisterDescriptor::ST:
      result->writes.push_back(registerWrite(
        GIFRegisterAddress::ST,
        lower));
      qValue = quadword[2];
      break;
    case GIFRegisterDescriptor::UV:
    {
      const std::uint64_t packed =
        (quadword[0] & GIF_UV_COMPONENT_MASK) |
        ((quadword[1] & GIF_UV_COMPONENT_MASK) <<
         GIF_V_SHIFT);
      result->writes.push_back(registerWrite(
        GIFRegisterAddress::UV,
        packed));
      break;
    }
    case GIFRegisterDescriptor::XYZF2:
    {
      const bool adc =
        ((quadword[GIF_ADC_WORD] >> GIF_ADC_BIT) & 1) != 0;
      const std::uint64_t packed =
        (quadword[0] & GIF_XY_COMPONENT_MASK) |
        ((quadword[1] & GIF_XY_COMPONENT_MASK) <<
         GIF_Y_SHIFT) |
        (((quadword[2] >> GIF_XYZF_INPUT_SHIFT) &
          GIF_XYZF_Z_MASK) << GIF_Z_SHIFT) |
        (((quadword[3] >> GIF_XYZF_INPUT_SHIFT) &
          GIF_FOG_MASK) << GIF_FOG_SHIFT);
      result->writes.push_back(registerWrite(
        adc
          ? GIFRegisterAddress::XYZF3
          : GIFRegisterAddress::XYZF2,
        packed));
      break;
    }
    case GIFRegisterDescriptor::XYZ2:
    {
      const bool adc =
        ((quadword[GIF_ADC_WORD] >> GIF_ADC_BIT) & 1) != 0;
      const std::uint64_t packed =
        (quadword[0] & GIF_XY_COMPONENT_MASK) |
        ((quadword[1] & GIF_XY_COMPONENT_MASK) <<
         GIF_Y_SHIFT) |
        ((quadword[2] & GIF_XYZ_Z_MASK) << GIF_Z_SHIFT);
      result->writes.push_back(registerWrite(
        adc
          ? GIFRegisterAddress::XYZ3
          : GIFRegisterAddress::XYZ2,
        packed));
      break;
    }
    case GIFRegisterDescriptor::FOG:
      result->writes.push_back(registerWrite(
        GIFRegisterAddress::FOG,
        ((quadword[3] >> GIF_XYZF_INPUT_SHIFT) &
         GIF_FOG_MASK) << GIF_FOG_SHIFT));
      break;
    case GIFRegisterDescriptor::AD:
      result->writes.push_back(registerWrite(
        quadword[2] & GIF_AD_ADDRESS_MASK,
        lower));
      break;
    case GIFRegisterDescriptor::NOP:
      break;
    default:
      result->writes.push_back(registerWrite(
        packedDescriptor,
        lower));
      break;
  }

  advanceDescriptor();
}

void GIFDecoder::consumeRegisterList(
  const GIFQuadword &quadword,
  GIFDecodeResult *result)
{
  consumeRegisterListValue(
    makeDoubleword(quadword[0], quadword[1]),
    result);
  if (remainingRegisterValues != 0)
  {
    consumeRegisterListValue(
      makeDoubleword(quadword[2], quadword[3]),
      result);
  }
}

void GIFDecoder::consumeRegisterListValue(
  std::uint64_t value,
  GIFDecodeResult *result)
{
  const std::uint8_t registerDescriptor = descriptor();
  if (registerDescriptor != GIFRegisterDescriptor::AD &&
      registerDescriptor != GIFRegisterDescriptor::NOP)
  {
    result->writes.push_back(registerWrite(
      registerDescriptor,
      value));
  }
  --remainingRegisterValues;
  advanceDescriptor();
}

void GIFDecoder::consumeImage(
  const GIFQuadword &quadword,
  GIFDecodeResult *result)
{
  result->writes.push_back(registerWrite(
    GIFRegisterAddress::HWREG,
    makeDoubleword(quadword[0], quadword[1])));
  result->writes.push_back(registerWrite(
    GIFRegisterAddress::HWREG,
    makeDoubleword(quadword[2], quadword[3])));
}

void GIFDecoder::advanceDescriptor()
{
  ++currentRegister;
  if (currentRegister == tag.registerCount)
  {
    currentRegister = 0;
    ++currentLoop;
  }
}

void GIFDecoder::completePrimitive(GIFDecodeResult *result)
{
  waitingForTag = true;
  remainingQuadwords = 0;
  remainingRegisterValues = 0;
  result->primitiveComplete = true;
  result->packetComplete = tag.endOfPacket;
  if (tag.endOfPacket)
  {
    activePacket = false;
  }
}

void GIFDecoder::dispatchWrites(const GIFDecodeResult &result)
{
  if (writeHandler == nullptr)
  {
    return;
  }

  for (const GIFRegisterWrite &write : result.writes)
  {
    writeHandler->writeRegister(write.address, write.data);
  }
}

std::uint8_t GIFDecoder::descriptor() const
{
  return
    (tag.registers >>
     (currentRegister * GIF_DESCRIPTOR_BITS)) &
    GIF_NREG_MASK;
}

bool GIFDecoder::awaitingTag() const
{
  return waitingForTag;
}

bool GIFDecoder::packetInProgress() const
{
  return activePacket;
}

std::uint32_t GIFDecoder::quadwordsRemaining() const
{
  return remainingQuadwords;
}

std::uint16_t GIFDecoder::loopIndex() const
{
  return currentLoop;
}

std::uint8_t GIFDecoder::registerIndex() const
{
  return currentRegister;
}

const GIFTag &GIFDecoder::currentTag() const
{
  return tag;
}
