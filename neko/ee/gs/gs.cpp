#include <algorithm>
#include <stdexcept>
#include <string>

#include "gs.hpp"

namespace
{
  constexpr std::size_t GS_LOCAL_MEMORY_BYTES = 4 * 1024 * 1024;
  constexpr std::size_t GS_WORD_BYTES = 4;
  constexpr std::size_t GS_LOCAL_MEMORY_WORDS =
    GS_LOCAL_MEMORY_BYTES / GS_WORD_BYTES;
  constexpr std::size_t GS_PAGE_WORDS = 2048;
  constexpr std::uint16_t GS_PSMCT32_PAGE_WIDTH = 64;
  constexpr std::uint16_t GS_PSMCT32_PAGE_HEIGHT = 32;
  constexpr std::uint16_t GS_PSMCT32_BLOCK_WIDTH = 8;
  constexpr std::uint16_t GS_PSMCT32_BLOCK_HEIGHT = 8;
  constexpr std::uint16_t GS_PSMCT32_COLUMN_HEIGHT = 2;
  constexpr std::uint16_t GS_PSMCT32_WORD_PAIR_WIDTH = 2;
  constexpr std::size_t GS_PSMCT32_WORD_PAIR_STRIDE = 4;
  constexpr std::size_t GS_PSMCT32_SECOND_ROW_OFFSET = 2;
  constexpr std::size_t GS_BLOCK_WORDS = 64;
  constexpr std::size_t GS_COLUMN_WORDS = 16;
  constexpr std::uint8_t GS_FRAME_WIDTH_LIMIT = 32;
  constexpr std::int32_t GS_FIXED_POINT_ONE = 16;
  constexpr std::uint8_t GS_RED_SHIFT = 0;
  constexpr std::uint8_t GS_GREEN_SHIFT = 8;
  constexpr std::uint8_t GS_BLUE_SHIFT = 16;
  constexpr std::uint8_t GS_ALPHA_SHIFT = 24;
  constexpr std::uint64_t GS_FNV_OFFSET_BASIS =
    UINT64_C(14695981039346656037);
  constexpr std::uint64_t GS_FNV_PRIME =
    UINT64_C(1099511628211);
  constexpr std::uint8_t GS_BYTES_PER_PIXEL = 4;
  constexpr std::uint8_t GS_BITS_PER_BYTE = 8;
  constexpr std::uint32_t GS_BYTE_MASK = 0xff;

  constexpr std::uint64_t GS_PRIMITIVE_TYPE_MASK = 0x07;
  constexpr std::uint8_t GS_PRIMITIVE_GOURAUD_BIT = 3;
  constexpr std::uint8_t GS_PRIMITIVE_TEXTURE_BIT = 4;
  constexpr std::uint8_t GS_PRIMITIVE_FOG_BIT = 5;
  constexpr std::uint8_t GS_PRIMITIVE_ALPHA_BIT = 6;
  constexpr std::uint8_t GS_PRIMITIVE_ANTIALIAS_BIT = 7;
  constexpr std::uint8_t GS_PRIMITIVE_FIXED_TEXTURE_BIT = 8;
  constexpr std::uint8_t GS_PRIMITIVE_CONTEXT_BIT = 9;
  constexpr std::uint8_t GS_PRIMITIVE_FIXED_FRAGMENT_BIT = 10;

  constexpr std::uint64_t GS_TEXTURE_BASE_MASK = 0x3fff;
  constexpr std::uint64_t GS_TEXTURE_WIDTH_MASK = 0x3f;
  constexpr std::uint8_t GS_TEXTURE_WIDTH_SHIFT = 14;
  constexpr std::uint64_t GS_TEXTURE_PSM_MASK = 0x3f;
  constexpr std::uint8_t GS_TEXTURE_PSM_SHIFT = 20;
  constexpr std::uint64_t GS_TEXTURE_SIZE_MASK = 0x0f;
  constexpr std::uint8_t GS_TEXTURE_WIDTH_EXPONENT_SHIFT = 26;
  constexpr std::uint8_t GS_TEXTURE_HEIGHT_EXPONENT_SHIFT = 30;
  constexpr std::uint8_t GS_TEXTURE_RGBA_BIT = 34;
  constexpr std::uint64_t GS_TEXTURE_FUNCTION_MASK = 0x03;
  constexpr std::uint8_t GS_TEXTURE_FUNCTION_SHIFT = 35;
  constexpr std::uint64_t GS_TEXTURE_MIP_LEVEL_MASK = 0x07;
  constexpr std::uint8_t GS_TEXTURE_MIP_LEVEL_SHIFT = 2;
  constexpr std::uint8_t GS_TEXTURE_MAGNIFICATION_BIT = 5;
  constexpr std::uint64_t GS_TEXTURE_MINIFICATION_MASK = 0x07;
  constexpr std::uint8_t GS_TEXTURE_MINIFICATION_SHIFT = 6;
  constexpr std::uint8_t GS_TEXTURE_MAXIMUM_EXPONENT = 10;
  constexpr std::int32_t GS_TEXTURE_COORDINATE_ONE = 16;

  constexpr std::uint64_t GS_CLAMP_MODE_MASK = 0x03;
  constexpr std::uint8_t GS_CLAMP_VERTICAL_SHIFT = 2;
  constexpr std::uint64_t GS_CLAMP_REGION_MASK = 0x03ff;
  constexpr std::uint8_t GS_CLAMP_MINIMUM_U_SHIFT = 4;
  constexpr std::uint8_t GS_CLAMP_MAXIMUM_U_SHIFT = 14;
  constexpr std::uint8_t GS_CLAMP_MINIMUM_V_SHIFT = 24;
  constexpr std::uint8_t GS_CLAMP_MAXIMUM_V_SHIFT = 34;

  constexpr std::uint64_t GS_COLOR_MASK = 0xff;
  constexpr std::uint8_t GS_COLOR_GREEN_SHIFT = 8;
  constexpr std::uint8_t GS_COLOR_BLUE_SHIFT = 16;
  constexpr std::uint8_t GS_COLOR_ALPHA_SHIFT = 24;
  constexpr std::uint8_t GS_COLOR_Q_SHIFT = 32;

  constexpr std::uint64_t GS_VERTEX_XY_MASK = 0xffff;
  constexpr std::uint8_t GS_VERTEX_Y_SHIFT = 16;
  constexpr std::uint8_t GS_VERTEX_Z_SHIFT = 32;

  constexpr std::uint64_t GS_FRAME_BASE_MASK = 0x01ff;
  constexpr std::uint64_t GS_FRAME_WIDTH_MASK = 0x3f;
  constexpr std::uint8_t GS_FRAME_WIDTH_SHIFT = 16;
  constexpr std::uint64_t GS_FRAME_PSM_MASK = 0x3f;
  constexpr std::uint8_t GS_FRAME_PSM_SHIFT = 24;
  constexpr std::uint8_t GS_FRAME_MASK_SHIFT = 32;

  constexpr std::uint64_t GS_SCISSOR_MASK = 0x07ff;
  constexpr std::uint8_t GS_SCISSOR_X1_SHIFT = 16;
  constexpr std::uint8_t GS_SCISSOR_Y0_SHIFT = 32;
  constexpr std::uint8_t GS_SCISSOR_Y1_SHIFT = 48;

  constexpr std::uint64_t GS_OFFSET_MASK = 0xffff;
  constexpr std::uint8_t GS_OFFSET_Y_SHIFT = 32;

  constexpr std::uint8_t GS_TEST_ALPHA_ENABLE_BIT = 0;
  constexpr std::uint64_t GS_TEST_ALPHA_METHOD_MASK = 0x07;
  constexpr std::uint8_t GS_TEST_ALPHA_METHOD_SHIFT = 1;
  constexpr std::uint64_t GS_TEST_ALPHA_REFERENCE_MASK = 0xff;
  constexpr std::uint8_t GS_TEST_ALPHA_REFERENCE_SHIFT = 4;
  constexpr std::uint64_t GS_TEST_ALPHA_FAIL_MASK = 0x03;
  constexpr std::uint8_t GS_TEST_ALPHA_FAIL_SHIFT = 12;
  constexpr std::uint8_t GS_TEST_DESTINATION_ALPHA_ENABLE_BIT = 14;
  constexpr std::uint8_t GS_TEST_DESTINATION_ALPHA_MODE_BIT = 15;
  constexpr std::uint8_t GS_TEST_DEPTH_ENABLE_BIT = 16;
  constexpr std::uint64_t GS_TEST_DEPTH_METHOD_MASK = 0x03;
  constexpr std::uint8_t GS_TEST_DEPTH_METHOD_SHIFT = 17;

  constexpr std::uint64_t GS_TRANSFER_BASE_MASK = 0x3fff;
  constexpr std::uint64_t GS_TRANSFER_WIDTH_MASK = 0x3f;
  constexpr std::uint8_t GS_TRANSFER_SOURCE_WIDTH_SHIFT = 16;
  constexpr std::uint8_t GS_TRANSFER_SOURCE_PSM_SHIFT = 24;
  constexpr std::uint8_t GS_TRANSFER_DESTINATION_BASE_SHIFT = 32;
  constexpr std::uint8_t GS_TRANSFER_DESTINATION_WIDTH_SHIFT = 48;
  constexpr std::uint8_t GS_TRANSFER_DESTINATION_PSM_SHIFT = 56;
  constexpr std::uint64_t GS_TRANSFER_PSM_MASK = 0x3f;
  constexpr std::uint64_t GS_TRANSFER_COORDINATE_MASK = 0x07ff;
  constexpr std::uint8_t GS_TRANSFER_SOURCE_Y_SHIFT = 16;
  constexpr std::uint8_t GS_TRANSFER_DESTINATION_X_SHIFT = 32;
  constexpr std::uint8_t GS_TRANSFER_DESTINATION_Y_SHIFT = 48;
  constexpr std::uint64_t GS_TRANSFER_REGION_MASK = 0x0fff;
  constexpr std::uint8_t GS_TRANSFER_HEIGHT_SHIFT = 32;
  constexpr std::uint64_t GS_TRANSFER_DIRECTION_MASK = 0x03;
  constexpr std::size_t GS_TRANSFER_BASE_WORDS = 64;

  constexpr std::array<std::array<std::uint8_t, 8>, 4>
    GS_PSMCT32_BLOCKS = {{
      {{0, 1, 4, 5, 16, 17, 20, 21}},
      {{2, 3, 6, 7, 18, 19, 22, 23}},
      {{8, 9, 12, 13, 24, 25, 28, 29}},
      {{10, 11, 14, 15, 26, 27, 30, 31}}
    }};

  bool bit(std::uint64_t data, std::uint8_t index)
  {
    return ((data >> index) & 1) != 0;
  }

  struct FixedPoint
  {
    std::int32_t x = 0;
    std::int32_t y = 0;
  };

  std::int64_t edge(
    const FixedPoint &start,
    const FixedPoint &end,
    const FixedPoint &point)
  {
    return
      static_cast<std::int64_t>(end.x - start.x) *
        (point.y - start.y) -
      static_cast<std::int64_t>(end.y - start.y) *
        (point.x - start.x);
  }

  bool isTopOrLeftEdge(
    const FixedPoint &start,
    const FixedPoint &end)
  {
    const std::int32_t dx = end.x - start.x;
    const std::int32_t dy = end.y - start.y;
    return dy < 0 || (dy == 0 && dx > 0);
  }

  bool passesEdge(
    const FixedPoint &start,
    const FixedPoint &end,
    const FixedPoint &point)
  {
    const std::int64_t value = edge(start, end, point);
    return value > 0 ||
      (value == 0 && isTopOrLeftEdge(start, end));
  }

  std::int32_t floorDivide(
    std::int32_t value,
    std::int32_t divisor)
  {
    std::int32_t quotient = value / divisor;
    if (value % divisor < 0)
    {
      --quotient;
    }
    return quotient;
  }

  std::int32_t ceilDivide(
    std::int32_t value,
    std::int32_t divisor)
  {
    std::int32_t quotient = value / divisor;
    if (value % divisor > 0)
    {
      ++quotient;
    }
    return quotient;
  }

  std::int64_t floorDivide64(
    std::int64_t value,
    std::int64_t divisor)
  {
    std::int64_t quotient = value / divisor;
    if (value % divisor < 0)
    {
      --quotient;
    }
    return quotient;
  }
}

GS::GS() : localMemory(GS_LOCAL_MEMORY_WORDS, 0)
{
}

void GS::writeRegister(
  std::uint8_t address,
  std::uint64_t data)
{
  if (reverseHostInterface)
  {
    throw std::runtime_error(
      "GS general registers are unavailable while BUSDIR is reversed.");
  }
  registers[address] = data;
  switch (address)
  {
    case GSRegisterAddress::PRIM:
      decodePrimitive(data);
      break;
    case GSRegisterAddress::RGBAQ:
      decodeColor(data);
      break;
    case GSRegisterAddress::XYZ2:
      decodeVertex(data, true);
      break;
    case GSRegisterAddress::XYZ3:
      decodeVertex(data, false);
      break;
    case GSRegisterAddress::TEX0_1:
      decodeTexture(0, data);
      break;
    case GSRegisterAddress::TEX0_2:
      decodeTexture(1, data);
      break;
    case GSRegisterAddress::CLAMP_1:
      decodeTextureClamp(0, data);
      break;
    case GSRegisterAddress::CLAMP_2:
      decodeTextureClamp(1, data);
      break;
    case GSRegisterAddress::TEX1_1:
      decodeTextureSampling(0, data);
      break;
    case GSRegisterAddress::TEX1_2:
      decodeTextureSampling(1, data);
      break;
    case GSRegisterAddress::XYOFFSET_1:
      decodeOffset(0, data);
      break;
    case GSRegisterAddress::XYOFFSET_2:
      decodeOffset(1, data);
      break;
    case GSRegisterAddress::SCISSOR_1:
      decodeScissor(0, data);
      break;
    case GSRegisterAddress::SCISSOR_2:
      decodeScissor(1, data);
      break;
    case GSRegisterAddress::TEST_1:
      decodeTest(0, data);
      break;
    case GSRegisterAddress::TEST_2:
      decodeTest(1, data);
      break;
    case GSRegisterAddress::FRAME_1:
      decodeFrame(0, data);
      break;
    case GSRegisterAddress::FRAME_2:
      decodeFrame(1, data);
      break;
    case GSRegisterAddress::BITBLTBUF:
      decodeTransferBuffer(data);
      break;
    case GSRegisterAddress::TRXPOS:
      decodeTransferPosition(data);
      break;
    case GSRegisterAddress::TRXREG:
      decodeTransferRegion(data);
      break;
    case GSRegisterAddress::TRXDIR:
      startImageTransfer(data);
      break;
    case GSRegisterAddress::HWREG:
      writeTransferData(data);
      break;
    default:
      break;
  }
}

void GS::writePrivilegedRegister(
  std::uint8_t address,
  std::uint64_t data)
{
  if (address != GSPrivilegedRegisterAddress::BUSDIR)
  {
    throw std::invalid_argument(
      "GS privileged register is not implemented.");
  }
  reverseHostInterface = (data & 1) != 0;
}

std::uint64_t GS::readHostInterface()
{
  if (!reverseHostInterface)
  {
    throw std::runtime_error(
      "GS host-interface reads require BUSDIR local-to-host.");
  }
  if (!transfer.active ||
      transfer.direction != GSImageTransferDirection::LocalToHost)
  {
    throw std::runtime_error(
      "GS local-to-host transfer is not active.");
  }

  const std::uint64_t low = readTransferPixel();
  const std::uint64_t high =
    transfer.active ? readTransferPixel() : 0;
  return low | (high << 32);
}

std::uint64_t GS::registerValue(std::uint8_t address) const
{
  return registers[address];
}

void GS::decodePrimitive(std::uint64_t data)
{
  primitiveVertexCount = 0;
  primitiveRegister.type = static_cast<GSPrimitiveType>(
    data & GS_PRIMITIVE_TYPE_MASK);
  primitiveRegister.gouraudShading =
    bit(data, GS_PRIMITIVE_GOURAUD_BIT);
  primitiveRegister.textureMapping =
    bit(data, GS_PRIMITIVE_TEXTURE_BIT);
  primitiveRegister.fogging =
    bit(data, GS_PRIMITIVE_FOG_BIT);
  primitiveRegister.alphaBlending =
    bit(data, GS_PRIMITIVE_ALPHA_BIT);
  primitiveRegister.antialiasing =
    bit(data, GS_PRIMITIVE_ANTIALIAS_BIT);
  primitiveRegister.fixedTextureCoordinates =
    bit(data, GS_PRIMITIVE_FIXED_TEXTURE_BIT);
  primitiveRegister.context =
    bit(data, GS_PRIMITIVE_CONTEXT_BIT);
  primitiveRegister.fixedFragmentValue =
    bit(data, GS_PRIMITIVE_FIXED_FRAGMENT_BIT);
}

void GS::decodeColor(std::uint64_t data)
{
  colorRegister.red = data & GS_COLOR_MASK;
  colorRegister.green =
    (data >> GS_COLOR_GREEN_SHIFT) & GS_COLOR_MASK;
  colorRegister.blue =
    (data >> GS_COLOR_BLUE_SHIFT) & GS_COLOR_MASK;
  colorRegister.alpha =
    (data >> GS_COLOR_ALPHA_SHIFT) & GS_COLOR_MASK;
  colorRegister.q = data >> GS_COLOR_Q_SHIFT;
}

void GS::decodeVertex(
  std::uint64_t data,
  bool drawingKick)
{
  vertexRegister.x = data & GS_VERTEX_XY_MASK;
  vertexRegister.y =
    (data >> GS_VERTEX_Y_SHIFT) & GS_VERTEX_XY_MASK;
  vertexRegister.z = data >> GS_VERTEX_Z_SHIFT;
  submitVertex(drawingKick);
}

void GS::submitVertex(bool drawingKick)
{
  switch (primitiveRegister.type)
  {
    case GSPrimitiveType::Point:
      primitiveVertexCount = 0;
      if (drawingKick)
      {
        rasterizePoint();
      }
      return;
    case GSPrimitiveType::Line:
    case GSPrimitiveType::LineStrip:
    {
      primitiveVertices[primitiveVertexCount] = vertexRegister;
      primitiveColors[primitiveVertexCount] = colorRegister;
      ++primitiveVertexCount;
      if (primitiveVertexCount != 2)
      {
        return;
      }
      const GSVertexCoordinate firstVertex = primitiveVertices[0];
      const GSVertexCoordinate secondVertex = primitiveVertices[1];
      const GSColor firstColor = primitiveColors[0];
      const GSColor secondColor = primitiveColors[1];
      if (primitiveRegister.type == GSPrimitiveType::LineStrip)
      {
        primitiveVertices[0] = secondVertex;
        primitiveColors[0] = secondColor;
        primitiveVertexCount = 1;
      }
      else
      {
        primitiveVertexCount = 0;
      }
      if (drawingKick)
      {
        rasterizeLine(
          firstVertex,
          secondVertex,
          firstColor,
          secondColor);
      }
      return;
    }
    case GSPrimitiveType::Sprite:
      primitiveVertices[primitiveVertexCount] = vertexRegister;
      primitiveColors[primitiveVertexCount] = colorRegister;
      ++primitiveVertexCount;
      if (primitiveVertexCount != 2)
      {
        return;
      }
      primitiveVertexCount = 0;
      if (drawingKick)
      {
        rasterizeSprite();
      }
      return;
    case GSPrimitiveType::Triangle:
    case GSPrimitiveType::TriangleStrip:
    case GSPrimitiveType::TriangleFan:
      break;
    default:
      primitiveVertexCount = 0;
      return;
  }

  primitiveVertices[primitiveVertexCount] = vertexRegister;
  primitiveColors[primitiveVertexCount] = colorRegister;
  ++primitiveVertexCount;
  if (primitiveVertexCount != TRIANGLE_VERTEX_COUNT)
  {
    return;
  }

  const std::array<GSVertexCoordinate, TRIANGLE_VERTEX_COUNT>
    vertices = primitiveVertices;
  const std::array<GSColor, TRIANGLE_VERTEX_COUNT>
    colors = primitiveColors;
  if (primitiveRegister.type == GSPrimitiveType::TriangleStrip)
  {
    primitiveVertices[0] = primitiveVertices[1];
    primitiveVertices[1] = primitiveVertices[2];
    primitiveColors[0] = primitiveColors[1];
    primitiveColors[1] = primitiveColors[2];
    primitiveVertexCount = 2;
  }
  else if (primitiveRegister.type == GSPrimitiveType::TriangleFan)
  {
    primitiveVertices[1] = primitiveVertices[2];
    primitiveColors[1] = primitiveColors[2];
    primitiveVertexCount = 2;
  }
  else
  {
    primitiveVertexCount = 0;
  }
  if (drawingKick)
  {
    rasterizeTriangle(vertices, colors);
  }
}

void GS::validateBasicDrawing(
  const char *primitiveName,
  bool antialiasingUnsupported) const
{
  if (primitiveRegister.textureMapping ||
      primitiveRegister.fogging ||
      primitiveRegister.alphaBlending ||
      (antialiasingUnsupported &&
       primitiveRegister.antialiasing))
  {
    throw std::runtime_error(
      std::string("GS ") + primitiveName +
      " uses unsupported drawing attributes.");
  }

  const GSContext &drawingContext =
    checkedContext(primitiveRegister.context);
  if (drawingContext.test.alphaTestEnabled ||
      drawingContext.test.destinationAlphaTestEnabled ||
      drawingContext.test.depthTestEnabled)
  {
    throw std::runtime_error(
      std::string("GS ") + primitiveName +
      " uses unsupported pixel tests.");
  }
  psmct32WordAddress(primitiveRegister.context, 0, 0);
}

std::uint32_t GS::packedColor() const
{
  return
    (static_cast<std::uint32_t>(colorRegister.red) <<
     GS_RED_SHIFT) |
    (static_cast<std::uint32_t>(colorRegister.green) <<
     GS_GREEN_SHIFT) |
    (static_cast<std::uint32_t>(colorRegister.blue) <<
     GS_BLUE_SHIFT) |
    (static_cast<std::uint32_t>(colorRegister.alpha) <<
     GS_ALPHA_SHIFT);
}

void GS::rasterizePoint()
{
  validateBasicDrawing("point", false);
  const GSContext &drawingContext =
    checkedContext(primitiveRegister.context);
  const std::int32_t fixedX =
    static_cast<std::int32_t>(vertexRegister.x) -
    drawingContext.offset.x;
  const std::int32_t fixedY =
    static_cast<std::int32_t>(vertexRegister.y) -
    drawingContext.offset.y;
  const std::int32_t x =
    floorDivide(fixedX + GS_FIXED_POINT_ONE / 2,
                GS_FIXED_POINT_ONE);
  const std::int32_t y =
    floorDivide(fixedY + GS_FIXED_POINT_ONE / 2,
                GS_FIXED_POINT_ONE);
  ++renderedPoints;
  if (x < drawingContext.scissor.x0 ||
      x > drawingContext.scissor.x1 ||
      y < drawingContext.scissor.y0 ||
      y > drawingContext.scissor.y1)
  {
    return;
  }

  writePSMCT32(
    primitiveRegister.context,
    static_cast<std::uint16_t>(x),
    static_cast<std::uint16_t>(y),
    packedColor());
  ++writtenPixels;
}

void GS::rasterizeLine(
  const GSVertexCoordinate &firstVertex,
  const GSVertexCoordinate &secondVertex,
  const GSColor &firstColor,
  const GSColor &secondColor)
{
  validateBasicDrawing("line", true);
  const std::size_t contextIndex = primitiveRegister.context;
  const GSContext &drawingContext = checkedContext(contextIndex);
  const FixedPoint first = {
    static_cast<std::int32_t>(firstVertex.x) -
      drawingContext.offset.x,
    static_cast<std::int32_t>(firstVertex.y) -
      drawingContext.offset.y
  };
  const FixedPoint second = {
    static_cast<std::int32_t>(secondVertex.x) -
      drawingContext.offset.x,
    static_cast<std::int32_t>(secondVertex.y) -
      drawingContext.offset.y
  };
  const std::int32_t deltaX = second.x - first.x;
  const std::int32_t deltaY = second.y - first.y;
  ++renderedLines;
  if (deltaX == 0 && deltaY == 0)
  {
    return;
  }

  const bool stepX =
    std::abs(deltaX) >= std::abs(deltaY);
  const bool positiveX = deltaX >= 0;
  const bool positiveY = deltaY >= 0;
  const std::int32_t roundedFirstX =
    floorDivide(first.x + GS_FIXED_POINT_ONE / 2,
                GS_FIXED_POINT_ONE);
  const std::int32_t roundedFirstY =
    floorDivide(first.y + GS_FIXED_POINT_ONE / 2,
                GS_FIXED_POINT_ONE);
  const std::int32_t roundedSecondX =
    floorDivide(second.x + GS_FIXED_POINT_ONE / 2,
                GS_FIXED_POINT_ONE);
  const std::int32_t roundedSecondY =
    floorDivide(second.y + GS_FIXED_POINT_ONE / 2,
                GS_FIXED_POINT_ONE);
  const auto exitsDiamond =
    [stepX, positiveX, positiveY](
      std::int32_t deltaToCenterX,
      std::int32_t deltaToCenterY)
    {
      const std::int32_t distance =
        std::abs(deltaToCenterX) +
        std::abs(deltaToCenterY);
      if (distance < GS_FIXED_POINT_ONE / 2)
      {
        return false;
      }
      if (stepX)
      {
        const bool exitsInDirection =
          positiveX
            ? deltaToCenterX > 0
            : deltaToCenterX < 0;
        return exitsInDirection &&
          (distance > GS_FIXED_POINT_ONE / 2 ||
           deltaToCenterY >= 0);
      }
      const bool exitsInDirection =
        positiveY
          ? deltaToCenterY > 0
          : deltaToCenterY < 0;
      return exitsInDirection &&
        (distance > GS_FIXED_POINT_ONE / 2 ||
         deltaToCenterX >= 0);
    };

  const bool drawFirst = !exitsDiamond(
    first.x - roundedFirstX * GS_FIXED_POINT_ONE,
    first.y - roundedFirstY * GS_FIXED_POINT_ONE);
  const bool drawLast = exitsDiamond(
    second.x - roundedSecondX * GS_FIXED_POINT_ONE,
    second.y - roundedSecondY * GS_FIXED_POINT_ONE);
  const std::int32_t drivingStep =
    stepX
      ? (positiveX ? 1 : -1)
      : (positiveY ? 1 : -1);
  std::int32_t firstDriving =
    stepX ? roundedFirstX : roundedFirstY;
  std::int32_t lastDriving =
    stepX ? roundedSecondX : roundedSecondY;
  if (!drawFirst)
  {
    firstDriving += drivingStep;
  }
  if (!drawLast)
  {
    lastDriving -= drivingStep;
  }
  if ((lastDriving - firstDriving) * drivingStep < 0)
  {
    return;
  }

  const std::int32_t drivingExtent =
    std::abs(stepX ? deltaX : deltaY);
  const std::int32_t dependentDelta =
    stepX ? deltaY : deltaX;
  const std::int32_t firstDrivingFixed =
    stepX ? first.x : first.y;
  const std::int32_t firstDependentFixed =
    stepX ? first.y : first.x;
  const auto interpolate =
    [drivingExtent](
      std::uint8_t start,
      std::uint8_t end,
      std::int32_t progress)
    {
      const std::int64_t interpolated =
        (static_cast<std::int64_t>(start) * drivingExtent +
         static_cast<std::int64_t>(
           static_cast<std::int32_t>(end) - start) *
           progress) /
        drivingExtent;
      return static_cast<std::uint32_t>(
        std::min<std::int64_t>(
          std::max<std::int64_t>(interpolated, 0),
          GS_BYTE_MASK));
    };

  for (std::int32_t driving = firstDriving;
       ;
       driving += drivingStep)
  {
    const std::int32_t drivingFixed =
      driving * GS_FIXED_POINT_ONE;
    const std::int32_t progress =
      drivingStep * (drivingFixed - firstDrivingFixed);
    const std::int64_t dependentNumerator =
      static_cast<std::int64_t>(firstDependentFixed) *
        drivingExtent +
      static_cast<std::int64_t>(dependentDelta) * progress;
    const std::int32_t dependent =
      static_cast<std::int32_t>(floorDivide64(
        dependentNumerator +
          static_cast<std::int64_t>(
            GS_FIXED_POINT_ONE / 2) * drivingExtent,
        static_cast<std::int64_t>(
          GS_FIXED_POINT_ONE) * drivingExtent));
    const std::int32_t x = stepX ? driving : dependent;
    const std::int32_t y = stepX ? dependent : driving;
    if (x >= drawingContext.scissor.x0 &&
        x <= drawingContext.scissor.x1 &&
        y >= drawingContext.scissor.y0 &&
        y <= drawingContext.scissor.y1)
    {
      std::uint32_t color = packedColor();
      if (primitiveRegister.gouraudShading)
      {
        color =
          interpolate(
            firstColor.red, secondColor.red, progress) <<
            GS_RED_SHIFT |
          interpolate(
            firstColor.green, secondColor.green, progress) <<
            GS_GREEN_SHIFT |
          interpolate(
            firstColor.blue, secondColor.blue, progress) <<
            GS_BLUE_SHIFT |
          interpolate(
            firstColor.alpha, secondColor.alpha, progress) <<
            GS_ALPHA_SHIFT;
      }
      writePSMCT32(
        contextIndex,
        static_cast<std::uint16_t>(x),
        static_cast<std::uint16_t>(y),
        color);
      ++writtenPixels;
    }
    if (driving == lastDriving)
    {
      break;
    }
  }
}

void GS::rasterizeSprite()
{
  validateBasicDrawing("sprite", false);
  const GSContext &drawingContext =
    checkedContext(primitiveRegister.context);
  const std::int32_t firstX =
    static_cast<std::int32_t>(primitiveVertices[0].x) -
    drawingContext.offset.x;
  const std::int32_t firstY =
    static_cast<std::int32_t>(primitiveVertices[0].y) -
    drawingContext.offset.y;
  const std::int32_t secondX =
    static_cast<std::int32_t>(primitiveVertices[1].x) -
    drawingContext.offset.x;
  const std::int32_t secondY =
    static_cast<std::int32_t>(primitiveVertices[1].y) -
    drawingContext.offset.y;
  const std::int32_t minimumX = std::max<std::int32_t>(
    ceilDivide(std::min(firstX, secondX), GS_FIXED_POINT_ONE),
    drawingContext.scissor.x0);
  const std::int32_t maximumX = std::min<std::int32_t>(
    ceilDivide(std::max(firstX, secondX), GS_FIXED_POINT_ONE) - 1,
    drawingContext.scissor.x1);
  const std::int32_t minimumY = std::max<std::int32_t>(
    ceilDivide(std::min(firstY, secondY), GS_FIXED_POINT_ONE),
    drawingContext.scissor.y0);
  const std::int32_t maximumY = std::min<std::int32_t>(
    ceilDivide(std::max(firstY, secondY), GS_FIXED_POINT_ONE) - 1,
    drawingContext.scissor.y1);

  ++renderedSprites;
  const std::uint32_t color = packedColor();
  for (std::int32_t y = minimumY; y <= maximumY; ++y)
  {
    for (std::int32_t x = minimumX; x <= maximumX; ++x)
    {
      writePSMCT32(
        primitiveRegister.context,
        static_cast<std::uint16_t>(x),
        static_cast<std::uint16_t>(y),
        color);
      ++writtenPixels;
    }
  }
}

void GS::rasterizeTriangle(
  const std::array<GSVertexCoordinate, TRIANGLE_VERTEX_COUNT>
    &sourceVertices,
  const std::array<GSColor, TRIANGLE_VERTEX_COUNT>
    &sourceColors)
{
  if (primitiveRegister.textureMapping ||
      primitiveRegister.fogging ||
      primitiveRegister.alphaBlending ||
      primitiveRegister.antialiasing)
  {
    throw std::runtime_error(
      "GS triangle uses unsupported drawing attributes.");
  }

  const std::size_t contextIndex = primitiveRegister.context;
  const GSContext &drawingContext = checkedContext(contextIndex);
  if (drawingContext.test.alphaTestEnabled ||
      drawingContext.test.destinationAlphaTestEnabled ||
      drawingContext.test.depthTestEnabled)
  {
    throw std::runtime_error(
      "GS triangle uses unsupported pixel tests.");
  }

  std::array<FixedPoint, TRIANGLE_VERTEX_COUNT> vertices;
  std::array<GSColor, TRIANGLE_VERTEX_COUNT> colors = sourceColors;
  for (std::size_t index = 0;
       index < vertices.size();
       ++index)
  {
    vertices[index].x =
      static_cast<std::int32_t>(sourceVertices[index].x) -
      drawingContext.offset.x;
    vertices[index].y =
      static_cast<std::int32_t>(sourceVertices[index].y) -
      drawingContext.offset.y;
  }

  std::int64_t area = edge(
    vertices[0],
    vertices[1],
    vertices[2]);
  if (area == 0)
  {
    return;
  }
  if (area < 0)
  {
    std::swap(vertices[1], vertices[2]);
    std::swap(colors[1], colors[2]);
    area = -area;
  }
  psmct32WordAddress(contextIndex, 0, 0);
  ++renderedTriangles;

  const std::int32_t minimumFixedX = std::min(
    vertices[0].x,
    std::min(vertices[1].x, vertices[2].x));
  const std::int32_t maximumFixedX = std::max(
    vertices[0].x,
    std::max(vertices[1].x, vertices[2].x));
  const std::int32_t minimumFixedY = std::min(
    vertices[0].y,
    std::min(vertices[1].y, vertices[2].y));
  const std::int32_t maximumFixedY = std::max(
    vertices[0].y,
    std::max(vertices[1].y, vertices[2].y));
  const std::int32_t minimumX = std::max<std::int32_t>(
    ceilDivide(minimumFixedX, GS_FIXED_POINT_ONE),
    drawingContext.scissor.x0);
  const std::int32_t maximumX = std::min<std::int32_t>(
    floorDivide(maximumFixedX, GS_FIXED_POINT_ONE),
    drawingContext.scissor.x1);
  const std::int32_t minimumY = std::max<std::int32_t>(
    ceilDivide(minimumFixedY, GS_FIXED_POINT_ONE),
    drawingContext.scissor.y0);
  const std::int32_t maximumY = std::min<std::int32_t>(
    floorDivide(maximumFixedY, GS_FIXED_POINT_ONE),
    drawingContext.scissor.y1);

  for (std::int32_t y = minimumY; y <= maximumY; ++y)
  {
    for (std::int32_t x = minimumX; x <= maximumX; ++x)
    {
      const FixedPoint pixel = {
        x * GS_FIXED_POINT_ONE,
        y * GS_FIXED_POINT_ONE
      };
      if (!passesEdge(vertices[0], vertices[1], pixel) ||
          !passesEdge(vertices[1], vertices[2], pixel) ||
          !passesEdge(vertices[2], vertices[0], pixel))
      {
        continue;
      }

      std::uint32_t red = colorRegister.red;
      std::uint32_t green = colorRegister.green;
      std::uint32_t blue = colorRegister.blue;
      std::uint32_t alpha = colorRegister.alpha;
      if (primitiveRegister.gouraudShading)
      {
        const std::int64_t weights[] = {
          edge(vertices[1], vertices[2], pixel),
          edge(vertices[2], vertices[0], pixel),
          edge(vertices[0], vertices[1], pixel)
        };
        const auto interpolate =
          [&weights, area](
            std::uint8_t first,
            std::uint8_t second,
            std::uint8_t third)
          {
            return static_cast<std::uint32_t>(
              (static_cast<std::int64_t>(first) * weights[0] +
               static_cast<std::int64_t>(second) * weights[1] +
               static_cast<std::int64_t>(third) * weights[2]) /
              area);
          };
        red = interpolate(
          colors[0].red,
          colors[1].red,
          colors[2].red);
        green = interpolate(
          colors[0].green,
          colors[1].green,
          colors[2].green);
        blue = interpolate(
          colors[0].blue,
          colors[1].blue,
          colors[2].blue);
        alpha = interpolate(
          colors[0].alpha,
          colors[1].alpha,
          colors[2].alpha);
      }
      const std::uint32_t color =
        (red << GS_RED_SHIFT) |
        (green << GS_GREEN_SHIFT) |
        (blue << GS_BLUE_SHIFT) |
        (alpha << GS_ALPHA_SHIFT);
      writePSMCT32(
        contextIndex,
        static_cast<std::uint16_t>(x),
        static_cast<std::uint16_t>(y),
        color);
      ++writtenPixels;
    }
  }
}

void GS::decodeFrame(std::size_t index, std::uint64_t data)
{
  GSFrame &frame = mutableContext(index).frame;
  frame.basePointer = data & GS_FRAME_BASE_MASK;
  frame.width =
    (data >> GS_FRAME_WIDTH_SHIFT) & GS_FRAME_WIDTH_MASK;
  frame.pixelStorageMode =
    (data >> GS_FRAME_PSM_SHIFT) & GS_FRAME_PSM_MASK;
  frame.drawingMask = data >> GS_FRAME_MASK_SHIFT;
}

void GS::decodeScissor(std::size_t index, std::uint64_t data)
{
  GSScissor &scissor = mutableContext(index).scissor;
  scissor.x0 = data & GS_SCISSOR_MASK;
  scissor.x1 =
    (data >> GS_SCISSOR_X1_SHIFT) & GS_SCISSOR_MASK;
  scissor.y0 =
    (data >> GS_SCISSOR_Y0_SHIFT) & GS_SCISSOR_MASK;
  scissor.y1 =
    (data >> GS_SCISSOR_Y1_SHIFT) & GS_SCISSOR_MASK;
}

void GS::decodeOffset(std::size_t index, std::uint64_t data)
{
  GSXYOffset &offset = mutableContext(index).offset;
  offset.x = data & GS_OFFSET_MASK;
  offset.y = (data >> GS_OFFSET_Y_SHIFT) & GS_OFFSET_MASK;
}

void GS::decodeTest(std::size_t index, std::uint64_t data)
{
  GSTest &test = mutableContext(index).test;
  test.alphaTestEnabled =
    bit(data, GS_TEST_ALPHA_ENABLE_BIT);
  test.alphaTest =
    (data >> GS_TEST_ALPHA_METHOD_SHIFT) &
    GS_TEST_ALPHA_METHOD_MASK;
  test.alphaReference =
    (data >> GS_TEST_ALPHA_REFERENCE_SHIFT) &
    GS_TEST_ALPHA_REFERENCE_MASK;
  test.alphaFail =
    (data >> GS_TEST_ALPHA_FAIL_SHIFT) &
    GS_TEST_ALPHA_FAIL_MASK;
  test.destinationAlphaTestEnabled =
    bit(data, GS_TEST_DESTINATION_ALPHA_ENABLE_BIT);
  test.destinationAlphaMode =
    bit(data, GS_TEST_DESTINATION_ALPHA_MODE_BIT);
  test.depthTestEnabled =
    bit(data, GS_TEST_DEPTH_ENABLE_BIT);
  test.depthTest =
    (data >> GS_TEST_DEPTH_METHOD_SHIFT) &
    GS_TEST_DEPTH_METHOD_MASK;
}

void GS::decodeTexture(std::size_t index, std::uint64_t data)
{
  GSTexture &texture = mutableContext(index).texture;
  texture.basePointer = data & GS_TEXTURE_BASE_MASK;
  texture.bufferWidth =
    (data >> GS_TEXTURE_WIDTH_SHIFT) &
    GS_TEXTURE_WIDTH_MASK;
  texture.pixelStorageMode =
    (data >> GS_TEXTURE_PSM_SHIFT) &
    GS_TEXTURE_PSM_MASK;
  texture.widthExponent =
    (data >> GS_TEXTURE_WIDTH_EXPONENT_SHIFT) &
    GS_TEXTURE_SIZE_MASK;
  texture.heightExponent =
    (data >> GS_TEXTURE_HEIGHT_EXPONENT_SHIFT) &
    GS_TEXTURE_SIZE_MASK;
  texture.rgba = bit(data, GS_TEXTURE_RGBA_BIT);
  texture.function =
    (data >> GS_TEXTURE_FUNCTION_SHIFT) &
    GS_TEXTURE_FUNCTION_MASK;
}

void GS::decodeTextureSampling(
  std::size_t index,
  std::uint64_t data)
{
  GSTexture &texture = mutableContext(index).texture;
  texture.maximumMipLevel =
    (data >> GS_TEXTURE_MIP_LEVEL_SHIFT) &
    GS_TEXTURE_MIP_LEVEL_MASK;
  texture.magnificationLinear =
    bit(data, GS_TEXTURE_MAGNIFICATION_BIT);
  texture.minificationFilter =
    (data >> GS_TEXTURE_MINIFICATION_SHIFT) &
    GS_TEXTURE_MINIFICATION_MASK;
}

void GS::decodeTextureClamp(
  std::size_t index,
  std::uint64_t data)
{
  GSTextureClamp &clamp =
    mutableContext(index).textureClamp;
  clamp.horizontal = static_cast<GSTextureWrapMode>(
    data & GS_CLAMP_MODE_MASK);
  clamp.vertical = static_cast<GSTextureWrapMode>(
    (data >> GS_CLAMP_VERTICAL_SHIFT) &
    GS_CLAMP_MODE_MASK);
  clamp.minimumU =
    (data >> GS_CLAMP_MINIMUM_U_SHIFT) &
    GS_CLAMP_REGION_MASK;
  clamp.maximumU =
    (data >> GS_CLAMP_MAXIMUM_U_SHIFT) &
    GS_CLAMP_REGION_MASK;
  clamp.minimumV =
    (data >> GS_CLAMP_MINIMUM_V_SHIFT) &
    GS_CLAMP_REGION_MASK;
  clamp.maximumV =
    (data >> GS_CLAMP_MAXIMUM_V_SHIFT) &
    GS_CLAMP_REGION_MASK;
}

const GSPrimitive &GS::primitive() const
{
  return primitiveRegister;
}

const GSColor &GS::color() const
{
  return colorRegister;
}

const GSVertexCoordinate &GS::vertex() const
{
  return vertexRegister;
}

const GSContext &GS::context(std::size_t index) const
{
  return checkedContext(index);
}

const GSTexture &GS::texture(std::size_t contextIndex) const
{
  return checkedContext(contextIndex).texture;
}

const GSImageTransfer &GS::imageTransfer() const
{
  return transfer;
}

bool GS::hostInterfaceReversed() const
{
  return reverseHostInterface;
}

std::uint32_t GS::sampleTextureNearest(
  std::size_t contextIndex,
  std::int32_t fixedU,
  std::int32_t fixedV) const
{
  const GSContext &samplingContext =
    checkedContext(contextIndex);
  const GSTexture &texture = samplingContext.texture;
  if (texture.bufferWidth == 0)
  {
    throw std::runtime_error(
      "GS texture sampling requires a valid buffer width.");
  }
  if (texture.pixelStorageMode != GSPixelStorageMode::PSMCT32)
  {
    throw std::runtime_error(
      "GS texture sampling requires PSMCT32.");
  }
  if (texture.widthExponent > GS_TEXTURE_MAXIMUM_EXPONENT ||
      texture.heightExponent > GS_TEXTURE_MAXIMUM_EXPONENT)
  {
    throw std::runtime_error(
      "GS texture dimensions exceed the supported range.");
  }
  if (texture.magnificationLinear ||
      texture.minificationFilter != 0)
  {
    throw std::runtime_error(
      "GS texture sampling requires nearest filtering.");
  }
  if (texture.maximumMipLevel != 0)
  {
    throw std::runtime_error(
      "GS texture mipmapping is not implemented.");
  }

  const std::int32_t width =
    1 << texture.widthExponent;
  const std::int32_t height =
    1 << texture.heightExponent;
  const auto wrap =
    [](std::int32_t coordinate,
       std::int32_t size,
       GSTextureWrapMode mode,
       std::uint16_t minimum,
       std::uint16_t maximum)
    {
      switch (mode)
      {
        case GSTextureWrapMode::Repeat:
        {
          const std::int32_t remainder = coordinate % size;
          return remainder < 0 ? remainder + size : remainder;
        }
        case GSTextureWrapMode::Clamp:
          return std::min(
            std::max(coordinate, 0),
            size - 1);
        case GSTextureWrapMode::RegionClamp:
          return std::min(
            std::max(
              coordinate,
              static_cast<std::int32_t>(minimum)),
            static_cast<std::int32_t>(maximum));
        case GSTextureWrapMode::RegionRepeat:
          return static_cast<std::int32_t>(
            (static_cast<std::uint32_t>(coordinate) &
             minimum) |
            maximum);
      }
      return 0;
    };

  const std::int32_t integerU =
    floorDivide(fixedU, GS_TEXTURE_COORDINATE_ONE);
  const std::int32_t integerV =
    floorDivide(fixedV, GS_TEXTURE_COORDINATE_ONE);
  const std::int32_t u = wrap(
    integerU,
    width,
    samplingContext.textureClamp.horizontal,
    samplingContext.textureClamp.minimumU,
    samplingContext.textureClamp.maximumU);
  const std::int32_t v = wrap(
    integerV,
    height,
    samplingContext.textureClamp.vertical,
    samplingContext.textureClamp.minimumV,
    samplingContext.textureClamp.maximumV);
  return localMemory[psmct32WordAddress(
    texture.basePointer,
    texture.bufferWidth,
    static_cast<std::uint16_t>(u),
    static_cast<std::uint16_t>(v))];
}

GSContext &GS::mutableContext(std::size_t index)
{
  if (index >= contexts.size())
  {
    throw std::out_of_range("GS context index is outside range.");
  }
  return contexts[index];
}

const GSContext &GS::checkedContext(std::size_t index) const
{
  if (index >= contexts.size())
  {
    throw std::out_of_range("GS context index is outside range.");
  }
  return contexts[index];
}

std::size_t GS::psmct32WordAddress(
  std::size_t contextIndex,
  std::uint16_t x,
  std::uint16_t y) const
{
  const GSFrame &frame = checkedContext(contextIndex).frame;
  if (frame.width == 0 || frame.width > GS_FRAME_WIDTH_LIMIT)
  {
    throw std::runtime_error(
      "GS PSMCT32 access requires a valid frame-buffer width.");
  }
  if (frame.pixelStorageMode != GSPixelStorageMode::PSMCT32)
  {
    throw std::runtime_error(
      "GS frame buffer is not configured for PSMCT32.");
  }

  return psmct32WordAddress(
    frame.basePointer * (GS_PAGE_WORDS / GS_TRANSFER_BASE_WORDS),
    frame.width,
    x,
    y);
}

std::size_t GS::psmct32WordAddress(
  std::uint16_t basePointer,
  std::uint8_t bufferWidth,
  std::uint16_t x,
  std::uint16_t y) const
{
  const std::size_t pageX = x / GS_PSMCT32_PAGE_WIDTH;
  const std::size_t pageY = y / GS_PSMCT32_PAGE_HEIGHT;
  const std::size_t page =
    pageY * bufferWidth + pageX;
  const std::uint16_t pageLocalX =
    x % GS_PSMCT32_PAGE_WIDTH;
  const std::uint16_t pageLocalY =
    y % GS_PSMCT32_PAGE_HEIGHT;
  const std::uint8_t block =
    GS_PSMCT32_BLOCKS[
      pageLocalY / GS_PSMCT32_BLOCK_HEIGHT][
      pageLocalX / GS_PSMCT32_BLOCK_WIDTH];
  const std::size_t column =
    (pageLocalY % GS_PSMCT32_BLOCK_HEIGHT) /
    GS_PSMCT32_COLUMN_HEIGHT;
  const std::uint16_t blockX =
    pageLocalX % GS_PSMCT32_BLOCK_WIDTH;
  const std::uint16_t blockY =
    pageLocalY % GS_PSMCT32_COLUMN_HEIGHT;
  const std::size_t word =
    (blockX / GS_PSMCT32_WORD_PAIR_WIDTH) *
      GS_PSMCT32_WORD_PAIR_STRIDE +
    blockY * GS_PSMCT32_SECOND_ROW_OFFSET +
    (blockX % GS_PSMCT32_WORD_PAIR_WIDTH);
  const std::size_t address =
    basePointer * GS_TRANSFER_BASE_WORDS +
    page * GS_PAGE_WORDS +
    block * GS_BLOCK_WORDS +
    column * GS_COLUMN_WORDS +
    word;
  return address % localMemory.size();
}

void GS::decodeTransferBuffer(std::uint64_t data)
{
  transfer.sourceBasePointer =
    data & GS_TRANSFER_BASE_MASK;
  transfer.sourceBufferWidth =
    (data >> GS_TRANSFER_SOURCE_WIDTH_SHIFT) &
    GS_TRANSFER_WIDTH_MASK;
  transfer.sourcePixelStorageMode =
    (data >> GS_TRANSFER_SOURCE_PSM_SHIFT) &
    GS_TRANSFER_PSM_MASK;
  transfer.destinationBasePointer =
    (data >> GS_TRANSFER_DESTINATION_BASE_SHIFT) &
    GS_TRANSFER_BASE_MASK;
  transfer.destinationBufferWidth =
    (data >> GS_TRANSFER_DESTINATION_WIDTH_SHIFT) &
    GS_TRANSFER_WIDTH_MASK;
  transfer.destinationPixelStorageMode =
    (data >> GS_TRANSFER_DESTINATION_PSM_SHIFT) &
    GS_TRANSFER_PSM_MASK;
}

void GS::decodeTransferPosition(std::uint64_t data)
{
  transfer.sourceX =
    data & GS_TRANSFER_COORDINATE_MASK;
  transfer.sourceY =
    (data >> GS_TRANSFER_SOURCE_Y_SHIFT) &
    GS_TRANSFER_COORDINATE_MASK;
  transfer.destinationX =
    (data >> GS_TRANSFER_DESTINATION_X_SHIFT) &
    GS_TRANSFER_COORDINATE_MASK;
  transfer.destinationY =
    (data >> GS_TRANSFER_DESTINATION_Y_SHIFT) &
    GS_TRANSFER_COORDINATE_MASK;
}

void GS::decodeTransferRegion(std::uint64_t data)
{
  transfer.width = data & GS_TRANSFER_REGION_MASK;
  transfer.height =
    (data >> GS_TRANSFER_HEIGHT_SHIFT) &
    GS_TRANSFER_REGION_MASK;
}

void GS::startImageTransfer(std::uint64_t data)
{
  transfer.active = false;
  transfer.transferredPixels = 0;
  transfer.direction = static_cast<GSImageTransferDirection>(
    data & GS_TRANSFER_DIRECTION_MASK);
  if (transfer.direction ==
      GSImageTransferDirection::Deactivated)
  {
    return;
  }

  if (transfer.direction ==
      GSImageTransferDirection::HostToLocal)
  {
    if (transfer.destinationPixelStorageMode !=
        GSPixelStorageMode::PSMCT32)
    {
      throw std::runtime_error(
        "GS host-to-local transfer requires PSMCT32.");
    }
    if (transfer.destinationBufferWidth == 0)
    {
      throw std::runtime_error(
        "GS image transfer requires a valid destination width.");
    }
  }
  else if (transfer.direction ==
           GSImageTransferDirection::LocalToHost)
  {
    if (transfer.sourcePixelStorageMode !=
        GSPixelStorageMode::PSMCT32)
    {
      throw std::runtime_error(
        "GS local-to-host transfer requires PSMCT32.");
    }
    if (transfer.sourceBufferWidth == 0)
    {
      throw std::runtime_error(
        "GS image transfer requires a valid source width.");
    }
  }
  else
  {
    throw std::runtime_error(
      "GS image transfer direction is not implemented.");
  }

  if (transfer.width == 0 || transfer.height == 0)
  {
    return;
  }
  transfer.active = true;
}

void GS::writeTransferData(std::uint64_t data)
{
  if (!transfer.active ||
      transfer.direction != GSImageTransferDirection::HostToLocal)
  {
    return;
  }
  writeTransferPixel(static_cast<std::uint32_t>(data));
  if (transfer.active)
  {
    writeTransferPixel(static_cast<std::uint32_t>(data >> 32));
  }
}

std::uint32_t GS::readTransferPixel()
{
  const std::uint32_t pixelIndex = transfer.transferredPixels;
  const std::uint16_t x =
    (transfer.sourceX +
     (pixelIndex % transfer.width)) &
    GS_TRANSFER_COORDINATE_MASK;
  const std::uint16_t y =
    (transfer.sourceY +
     (pixelIndex / transfer.width)) &
    GS_TRANSFER_COORDINATE_MASK;
  const std::uint32_t value =
    localMemory[psmct32WordAddress(
      transfer.sourceBasePointer,
      transfer.sourceBufferWidth,
      x,
      y)];

  ++transfer.transferredPixels;
  const std::uint32_t totalPixels =
    static_cast<std::uint32_t>(transfer.width) *
    transfer.height;
  if (transfer.transferredPixels == totalPixels)
  {
    transfer.active = false;
  }
  return value;
}

void GS::writeTransferPixel(std::uint32_t value)
{
  const std::uint32_t pixelIndex = transfer.transferredPixels;
  const std::uint16_t x =
    (transfer.destinationX +
     (pixelIndex % transfer.width)) &
    GS_TRANSFER_COORDINATE_MASK;
  const std::uint16_t y =
    (transfer.destinationY +
     (pixelIndex / transfer.width)) &
    GS_TRANSFER_COORDINATE_MASK;
  localMemory[psmct32WordAddress(
    transfer.destinationBasePointer,
    transfer.destinationBufferWidth,
    x,
    y)] = value;

  ++transfer.transferredPixels;
  const std::uint32_t totalPixels =
    static_cast<std::uint32_t>(transfer.width) *
    transfer.height;
  if (transfer.transferredPixels == totalPixels)
  {
    transfer.active = false;
  }
}

std::uint32_t GS::readPSMCT32(
  std::size_t contextIndex,
  std::uint16_t x,
  std::uint16_t y) const
{
  return localMemory[
    psmct32WordAddress(contextIndex, x, y)];
}

void GS::writePSMCT32(
  std::size_t contextIndex,
  std::uint16_t x,
  std::uint16_t y,
  std::uint32_t value)
{
  const GSFrame &frame = checkedContext(contextIndex).frame;
  const std::size_t address =
    psmct32WordAddress(contextIndex, x, y);
  localMemory[address] =
    (localMemory[address] & frame.drawingMask) |
    (value & ~frame.drawingMask);
}

std::uint32_t GS::localMemoryWord(std::size_t address) const
{
  if (address >= localMemory.size())
  {
    throw std::out_of_range(
      "GS local-memory word address is outside range.");
  }
  return localMemory[address];
}

std::size_t GS::localMemoryWordCount() const
{
  return localMemory.size();
}

std::uint64_t GS::framebufferHash(
  std::size_t contextIndex,
  std::uint16_t width,
  std::uint16_t height) const
{
  std::uint64_t hash = GS_FNV_OFFSET_BASIS;
  for (std::uint16_t y = 0; y < height; ++y)
  {
    for (std::uint16_t x = 0; x < width; ++x)
    {
      const std::uint32_t pixel =
        readPSMCT32(contextIndex, x, y);
      for (std::uint8_t byte = 0;
           byte < GS_BYTES_PER_PIXEL;
           ++byte)
      {
        hash ^=
          (pixel >> (byte * GS_BITS_PER_BYTE)) & GS_BYTE_MASK;
        hash *= GS_FNV_PRIME;
      }
    }
  }
  return hash;
}

std::vector<std::uint8_t> GS::framebufferRGBA8(
  std::size_t contextIndex,
  std::uint16_t width,
  std::uint16_t height) const
{
  constexpr std::size_t COMPONENTS_PER_PIXEL = 4;
  constexpr std::uint32_t COMPONENT_MASK = 0xff;
  constexpr std::uint8_t GREEN_SHIFT = 8;
  constexpr std::uint8_t BLUE_SHIFT = 16;
  constexpr std::uint8_t ALPHA_SHIFT = 24;

  std::vector<std::uint8_t> pixels;
  pixels.reserve(
    static_cast<std::size_t>(width) *
    height *
    COMPONENTS_PER_PIXEL);
  for (std::uint16_t y = 0; y < height; ++y)
  {
    for (std::uint16_t x = 0; x < width; ++x)
    {
      const std::uint32_t pixel =
        readPSMCT32(contextIndex, x, y);
      pixels.push_back(pixel & COMPONENT_MASK);
      pixels.push_back(
        (pixel >> GREEN_SHIFT) & COMPONENT_MASK);
      pixels.push_back(
        (pixel >> BLUE_SHIFT) & COMPONENT_MASK);
      pixels.push_back(
        (pixel >> ALPHA_SHIFT) & COMPONENT_MASK);
    }
  }
  return pixels;
}

std::size_t GS::queuedVertexCount() const
{
  return primitiveVertexCount;
}

std::uint64_t GS::pointCount() const
{
  return renderedPoints;
}

std::uint64_t GS::lineCount() const
{
  return renderedLines;
}

std::uint64_t GS::spriteCount() const
{
  return renderedSprites;
}

std::uint64_t GS::triangleCount() const
{
  return renderedTriangles;
}

std::uint64_t GS::pixelWriteCount() const
{
  return writtenPixels;
}
