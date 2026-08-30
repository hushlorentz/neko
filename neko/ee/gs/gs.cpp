#include <stdexcept>

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

  constexpr std::uint64_t GS_PRIMITIVE_TYPE_MASK = 0x07;
  constexpr std::uint8_t GS_PRIMITIVE_GOURAUD_BIT = 3;
  constexpr std::uint8_t GS_PRIMITIVE_TEXTURE_BIT = 4;
  constexpr std::uint8_t GS_PRIMITIVE_FOG_BIT = 5;
  constexpr std::uint8_t GS_PRIMITIVE_ALPHA_BIT = 6;
  constexpr std::uint8_t GS_PRIMITIVE_ANTIALIAS_BIT = 7;
  constexpr std::uint8_t GS_PRIMITIVE_FIXED_TEXTURE_BIT = 8;
  constexpr std::uint8_t GS_PRIMITIVE_CONTEXT_BIT = 9;
  constexpr std::uint8_t GS_PRIMITIVE_FIXED_FRAGMENT_BIT = 10;

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
}

GS::GS() : localMemory(GS_LOCAL_MEMORY_WORDS, 0)
{
}

void GS::writeRegister(
  std::uint8_t address,
  std::uint64_t data)
{
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
    case GSRegisterAddress::XYZ3:
      decodeVertex(data);
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
    default:
      break;
  }
}

std::uint64_t GS::registerValue(std::uint8_t address) const
{
  return registers[address];
}

void GS::decodePrimitive(std::uint64_t data)
{
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

void GS::decodeVertex(std::uint64_t data)
{
  vertexRegister.x = data & GS_VERTEX_XY_MASK;
  vertexRegister.y =
    (data >> GS_VERTEX_Y_SHIFT) & GS_VERTEX_XY_MASK;
  vertexRegister.z = data >> GS_VERTEX_Z_SHIFT;
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

  const std::size_t pageX = x / GS_PSMCT32_PAGE_WIDTH;
  const std::size_t pageY = y / GS_PSMCT32_PAGE_HEIGHT;
  const std::size_t page =
    pageY * frame.width + pageX;
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
    frame.basePointer * GS_PAGE_WORDS +
    page * GS_PAGE_WORDS +
    block * GS_BLOCK_WORDS +
    column * GS_COLUMN_WORDS +
    word;
  return address % localMemory.size();
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
