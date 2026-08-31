#ifndef GIF_H
#define GIF_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

using GIFQuadword = std::array<std::uint32_t, 4>;

enum class GIFDataFormat : std::uint8_t
{
  Packed = 0,
  RegisterList = 1,
  Image = 2,
  Disabled = 3
};

namespace GIFRegisterDescriptor
{
  constexpr std::uint8_t PRIM = 0x00;
  constexpr std::uint8_t RGBAQ = 0x01;
  constexpr std::uint8_t ST = 0x02;
  constexpr std::uint8_t UV = 0x03;
  constexpr std::uint8_t XYZF2 = 0x04;
  constexpr std::uint8_t XYZ2 = 0x05;
  constexpr std::uint8_t TEX0_1 = 0x06;
  constexpr std::uint8_t TEX0_2 = 0x07;
  constexpr std::uint8_t CLAMP_1 = 0x08;
  constexpr std::uint8_t CLAMP_2 = 0x09;
  constexpr std::uint8_t FOG = 0x0a;
  constexpr std::uint8_t Reserved = 0x0b;
  constexpr std::uint8_t XYZF3 = 0x0c;
  constexpr std::uint8_t XYZ3 = 0x0d;
  constexpr std::uint8_t AD = 0x0e;
  constexpr std::uint8_t NOP = 0x0f;
}

namespace GIFRegisterAddress
{
  constexpr std::uint8_t PRIM = 0x00;
  constexpr std::uint8_t RGBAQ = 0x01;
  constexpr std::uint8_t ST = 0x02;
  constexpr std::uint8_t UV = 0x03;
  constexpr std::uint8_t XYZF2 = 0x04;
  constexpr std::uint8_t XYZ2 = 0x05;
  constexpr std::uint8_t FOG = 0x0a;
  constexpr std::uint8_t XYZF3 = 0x0c;
  constexpr std::uint8_t XYZ3 = 0x0d;
  constexpr std::uint8_t HWREG = 0x54;
}

struct GIFTag
{
  std::uint16_t loopCount = 0;
  bool endOfPacket = false;
  bool primitiveEnabled = false;
  std::uint16_t primitive = 0;
  GIFDataFormat format = GIFDataFormat::Packed;
  std::uint8_t registerCount = 0;
  std::uint64_t registers = 0;
};

struct GIFRegisterWrite
{
  std::uint8_t address = 0;
  std::uint64_t data = 0;
};

struct GIFDecodeResult
{
  bool tagDecoded = false;
  GIFTag tag;
  std::vector<GIFRegisterWrite> writes;
  bool primitiveComplete = false;
  bool packetComplete = false;
};

struct GIFDecoderState
{
  GIFTag tag;
  bool waitingForTag = true;
  bool activePacket = false;
  std::uint32_t remainingQuadwords = 0;
  std::uint32_t remainingRegisterValues = 0;
  std::uint16_t currentLoop = 0;
  std::uint8_t currentRegister = 0;
  std::uint32_t qValue = 0;
};

class GIFRegisterWriteHandler
{
  public:
    virtual ~GIFRegisterWriteHandler() = default;
    virtual void writeRegister(
      std::uint8_t address,
      std::uint64_t data) = 0;
};

GIFTag decodeGIFTag(const GIFQuadword &quadword);

class GIFDecoder
{
  public:
    GIFDecodeResult ingestQuadword(const GIFQuadword &quadword);
    void attachRegisterWriteHandler(
      GIFRegisterWriteHandler *attachedHandler);

    bool awaitingTag() const;
    bool packetInProgress() const;
    std::uint32_t quadwordsRemaining() const;
    std::uint16_t loopIndex() const;
    std::uint8_t registerIndex() const;
    const GIFTag &currentTag() const;
    GIFDecoderState suspendPacket();
    void resumePacket(const GIFDecoderState &state);

  private:
    void beginPrimitive(
      const GIFTag &tag,
      GIFDecodeResult *result);
    void consumePacked(
      const GIFQuadword &quadword,
      GIFDecodeResult *result);
    void consumeRegisterList(
      const GIFQuadword &quadword,
      GIFDecodeResult *result);
    void consumeImage(
      const GIFQuadword &quadword,
      GIFDecodeResult *result);
    void consumeRegisterListValue(
      std::uint64_t value,
      GIFDecodeResult *result);
    void advanceDescriptor();
    void completePrimitive(GIFDecodeResult *result);
    void dispatchWrites(const GIFDecodeResult &result);
    std::uint8_t descriptor() const;

    GIFTag tag;
    GIFRegisterWriteHandler *writeHandler = nullptr;
    bool waitingForTag = true;
    bool activePacket = false;
    std::uint32_t remainingQuadwords = 0;
    std::uint32_t remainingRegisterValues = 0;
    std::uint16_t currentLoop = 0;
    std::uint8_t currentRegister = 0;
    std::uint32_t qValue = 0;
};

#endif
