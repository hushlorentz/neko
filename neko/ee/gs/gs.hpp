#ifndef GS_H
#define GS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "gif.hpp"

namespace GSRegisterAddress
{
  constexpr std::uint8_t PRIM = 0x00;
  constexpr std::uint8_t RGBAQ = 0x01;
  constexpr std::uint8_t XYZ2 = 0x05;
  constexpr std::uint8_t XYZ3 = 0x0d;
  constexpr std::uint8_t XYOFFSET_1 = 0x18;
  constexpr std::uint8_t XYOFFSET_2 = 0x19;
  constexpr std::uint8_t SCISSOR_1 = 0x40;
  constexpr std::uint8_t SCISSOR_2 = 0x41;
  constexpr std::uint8_t TEST_1 = 0x47;
  constexpr std::uint8_t TEST_2 = 0x48;
  constexpr std::uint8_t FRAME_1 = 0x4c;
  constexpr std::uint8_t FRAME_2 = 0x4d;
  constexpr std::uint8_t BITBLTBUF = 0x50;
  constexpr std::uint8_t TRXPOS = 0x51;
  constexpr std::uint8_t TRXREG = 0x52;
  constexpr std::uint8_t TRXDIR = 0x53;
  constexpr std::uint8_t HWREG = 0x54;
}

namespace GSPixelStorageMode
{
  constexpr std::uint8_t PSMCT32 = 0x00;
}

namespace GSPrivilegedRegisterAddress
{
  constexpr std::uint8_t BUSDIR = 0x44;
}

enum class GSImageTransferDirection : std::uint8_t
{
  HostToLocal = 0,
  LocalToHost = 1,
  LocalToLocal = 2,
  Deactivated = 3
};

enum class GSPrimitiveType : std::uint8_t
{
  Point = 0,
  Line = 1,
  LineStrip = 2,
  Triangle = 3,
  TriangleStrip = 4,
  TriangleFan = 5,
  Sprite = 6
};

struct GSPrimitive
{
  GSPrimitiveType type = GSPrimitiveType::Point;
  bool gouraudShading = false;
  bool textureMapping = false;
  bool fogging = false;
  bool alphaBlending = false;
  bool antialiasing = false;
  bool fixedTextureCoordinates = false;
  std::uint8_t context = 0;
  bool fixedFragmentValue = false;
};

struct GSColor
{
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;
  std::uint8_t alpha = 0;
  std::uint32_t q = 0;
};

struct GSVertexCoordinate
{
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  std::uint32_t z = 0;
};

struct GSFrame
{
  std::uint16_t basePointer = 0;
  std::uint8_t width = 0;
  std::uint8_t pixelStorageMode = 0;
  std::uint32_t drawingMask = 0;
};

struct GSScissor
{
  std::uint16_t x0 = 0;
  std::uint16_t x1 = 0;
  std::uint16_t y0 = 0;
  std::uint16_t y1 = 0;
};

struct GSXYOffset
{
  std::uint16_t x = 0;
  std::uint16_t y = 0;
};

struct GSTest
{
  bool alphaTestEnabled = false;
  std::uint8_t alphaTest = 0;
  std::uint8_t alphaReference = 0;
  std::uint8_t alphaFail = 0;
  bool destinationAlphaTestEnabled = false;
  bool destinationAlphaMode = false;
  bool depthTestEnabled = false;
  std::uint8_t depthTest = 0;
};

struct GSContext
{
  GSFrame frame;
  GSScissor scissor;
  GSXYOffset offset;
  GSTest test;
};

struct GSImageTransfer
{
  std::uint16_t sourceBasePointer = 0;
  std::uint8_t sourceBufferWidth = 0;
  std::uint8_t sourcePixelStorageMode = 0;
  std::uint16_t destinationBasePointer = 0;
  std::uint8_t destinationBufferWidth = 0;
  std::uint8_t destinationPixelStorageMode = 0;
  std::uint16_t sourceX = 0;
  std::uint16_t sourceY = 0;
  std::uint16_t destinationX = 0;
  std::uint16_t destinationY = 0;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  std::uint32_t transferredPixels = 0;
  GSImageTransferDirection direction =
    GSImageTransferDirection::Deactivated;
  bool active = false;
};

class GS : public GIFRegisterWriteHandler
{
  public:
    GS();

    void writeRegister(
      std::uint8_t address,
      std::uint64_t data) override;
    void writePrivilegedRegister(
      std::uint8_t address,
      std::uint64_t data);
    std::uint64_t readHostInterface();
    std::uint64_t registerValue(std::uint8_t address) const;

    const GSPrimitive &primitive() const;
    const GSColor &color() const;
    const GSVertexCoordinate &vertex() const;
    const GSContext &context(std::size_t index) const;
    const GSImageTransfer &imageTransfer() const;
    bool hostInterfaceReversed() const;

    std::size_t psmct32WordAddress(
      std::size_t contextIndex,
      std::uint16_t x,
      std::uint16_t y) const;
    std::uint32_t readPSMCT32(
      std::size_t contextIndex,
      std::uint16_t x,
      std::uint16_t y) const;
    void writePSMCT32(
      std::size_t contextIndex,
      std::uint16_t x,
      std::uint16_t y,
      std::uint32_t value);
    std::uint32_t localMemoryWord(std::size_t address) const;
    std::size_t localMemoryWordCount() const;
    std::uint64_t framebufferHash(
      std::size_t contextIndex,
      std::uint16_t width,
      std::uint16_t height) const;
    std::vector<std::uint8_t> framebufferRGBA8(
      std::size_t contextIndex,
      std::uint16_t width,
      std::uint16_t height) const;
    std::size_t queuedVertexCount() const;
    std::uint64_t pointCount() const;
    std::uint64_t spriteCount() const;
    std::uint64_t triangleCount() const;
    std::uint64_t pixelWriteCount() const;

  private:
    static constexpr std::size_t REGISTER_COUNT = 256;
    static constexpr std::size_t CONTEXT_COUNT = 2;
    static constexpr std::size_t TRIANGLE_VERTEX_COUNT = 3;

    GSContext &mutableContext(std::size_t index);
    const GSContext &checkedContext(std::size_t index) const;
    void decodePrimitive(std::uint64_t data);
    void decodeColor(std::uint64_t data);
    void decodeVertex(
      std::uint64_t data,
      bool drawingKick);
    void decodeFrame(std::size_t index, std::uint64_t data);
    void decodeScissor(std::size_t index, std::uint64_t data);
    void decodeOffset(std::size_t index, std::uint64_t data);
    void decodeTest(std::size_t index, std::uint64_t data);
    void decodeTransferBuffer(std::uint64_t data);
    void decodeTransferPosition(std::uint64_t data);
    void decodeTransferRegion(std::uint64_t data);
    void startImageTransfer(std::uint64_t data);
    void writeTransferData(std::uint64_t data);
    void writeTransferPixel(std::uint32_t value);
    std::uint32_t readTransferPixel();
    std::size_t psmct32WordAddress(
      std::uint16_t basePointer,
      std::uint8_t bufferWidth,
      std::uint16_t x,
      std::uint16_t y) const;
    void submitVertex(bool drawingKick);
    void rasterizePoint();
    void rasterizeSprite();
    void rasterizeTriangle();
    void validateBasicDrawing(
      const char *primitiveName,
      bool antialiasingUnsupported) const;
    std::uint32_t packedColor() const;

    std::array<std::uint64_t, REGISTER_COUNT> registers = {};
    GSPrimitive primitiveRegister;
    GSColor colorRegister;
    GSVertexCoordinate vertexRegister;
    std::array<GSVertexCoordinate, TRIANGLE_VERTEX_COUNT>
      triangleVertices = {};
    std::array<GSColor, TRIANGLE_VERTEX_COUNT>
      triangleColors = {};
    std::size_t triangleVertexCount = 0;
    std::uint64_t renderedPoints = 0;
    std::uint64_t renderedSprites = 0;
    std::uint64_t renderedTriangles = 0;
    std::uint64_t writtenPixels = 0;
    std::array<GSContext, CONTEXT_COUNT> contexts = {};
    GSImageTransfer transfer;
    bool reverseHostInterface = false;
    std::vector<std::uint32_t> localMemory;
};

#endif
