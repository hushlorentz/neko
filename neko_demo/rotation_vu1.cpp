#include <array>
#include <cstdint>
#include <vector>

#include "gif_path1.hpp"
#include "gs.hpp"
#include "rotation_vu1.hpp"
#include "vpu.hpp"

namespace
{
  constexpr std::uint32_t FLOAT_ZERO = 0x00000000;
  constexpr std::uint32_t FLOAT_ONE = 0x3f800000;
  constexpr std::uint32_t FLOAT_NEGATIVE_64 = 0xc2800000;
  constexpr std::uint32_t FLOAT_POSITIVE_96 = 0x42c00000;
  constexpr std::uint32_t FLOAT_SCREEN_X = 0x44e40000;
  constexpr std::uint32_t FLOAT_SCREEN_Y = 0x44fe0000;
  constexpr std::uint32_t FLOAT_PROJECTION = 0x45000000;
  constexpr std::uint32_t POINT_COUNT = 3;
  constexpr std::uint32_t POINT_STRIDE_QUADWORDS = 2;
  constexpr std::uint32_t ROTATE_AROUND_Z = 4;
  constexpr std::uint32_t TRIANGLE_GOURAUD_PRIMITIVE = 0x0b;
  constexpr std::uint32_t RED = 0x000000ff;
  constexpr std::uint32_t GREEN = 0x0000ff00;
  constexpr std::uint32_t BLUE = 0x00ff0000;
  constexpr std::uint32_t CYCLE_BUDGET = 2000;
  constexpr std::uint16_t FRAME_WIDTH_PAGES = 10;
  constexpr std::uint16_t FRAME_MAX_X = 639;
  constexpr std::uint16_t FRAME_MAX_Y = 447;
  constexpr std::uint16_t XY_OFFSET_X = 1728;
  constexpr std::uint16_t XY_OFFSET_Y = 1936;
  constexpr std::uint32_t FLOAT_SIGN_BIT = 0x80000000;
  constexpr std::uint32_t QUARTER_PHASE_COUNT = 16;
  constexpr std::array<std::uint32_t, QUARTER_PHASE_COUNT + 1>
    QUARTER_SINE_BITS = {{
      0x00000000,
      0x3dc8bd36,
      0x3e47c5c2,
      0x3e94a031,
      0x3ec3ef15,
      0x3ef15aea,
      0x3f0e39da,
      0x3f226799,
      0x3f3504f3,
      0x3f45e403,
      0x3f54db31,
      0x3f61c598,
      0x3f6c835e,
      0x3f74fa0b,
      0x3f7b14be,
      0x3f7ec46d,
      0x3f800000
    }};

  std::uint32_t sineBits(std::uint32_t phase)
  {
    phase %= neko_demo::ROTATION_PHASE_COUNT;
    const std::uint32_t quadrant =
      phase / QUARTER_PHASE_COUNT;
    const std::uint32_t offset =
      phase % QUARTER_PHASE_COUNT;
    std::uint32_t magnitude = 0;
    if (quadrant == 0 || quadrant == 2)
    {
      magnitude = QUARTER_SINE_BITS[offset];
    }
    else
    {
      magnitude =
        QUARTER_SINE_BITS[QUARTER_PHASE_COUNT - offset];
    }
    if (magnitude != FLOAT_ZERO && quadrant >= 2)
    {
      return magnitude | FLOAT_SIGN_BIT;
    }
    return magnitude;
  }

  void appendWord(
    std::vector<std::uint8_t> *bytes,
    std::uint32_t value)
  {
    bytes->push_back(value & 0xff);
    bytes->push_back((value >> 8) & 0xff);
    bytes->push_back((value >> 16) & 0xff);
    bytes->push_back((value >> 24) & 0xff);
  }

  void appendQuadword(
    std::vector<std::uint8_t> *bytes,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z,
    std::uint32_t w)
  {
    appendWord(bytes, x);
    appendWord(bytes, y);
    appendWord(bytes, z);
    appendWord(bytes, w);
  }

  GIFQuadword packedTag(
    std::uint16_t loopCount,
    bool endOfPacket,
    std::uint8_t registerCount,
    std::uint64_t registers)
  {
    const std::uint64_t low =
      loopCount |
      (static_cast<std::uint64_t>(endOfPacket) << 15) |
      (static_cast<std::uint64_t>(GIFDataFormat::Packed) << 58) |
      (static_cast<std::uint64_t>(registerCount) << 60);
    return GIFQuadword{{
      static_cast<std::uint32_t>(low),
      static_cast<std::uint32_t>(low >> 32),
      static_cast<std::uint32_t>(registers),
      static_cast<std::uint32_t>(registers >> 32)
    }};
  }

  void appendQuadword(
    std::vector<std::uint8_t> *bytes,
    const GIFQuadword &quadword)
  {
    for (std::uint32_t word : quadword)
    {
      appendWord(bytes, word);
    }
  }

  void appendRegisterWrite(
    std::vector<std::uint8_t> *bytes,
    std::uint8_t address,
    std::uint64_t value)
  {
    appendQuadword(
      bytes,
      static_cast<std::uint32_t>(value),
      static_cast<std::uint32_t>(value >> 32),
      address,
      0);
  }

  std::vector<std::uint8_t> rotationInput(
    std::uint32_t phase)
  {
    std::vector<std::uint8_t> memory;

    appendQuadword(
      &memory,
      FLOAT_ZERO,
      FLOAT_ONE,
      FLOAT_ZERO,
      FLOAT_ONE);
    appendQuadword(
      &memory,
      sineBits(phase),
      sineBits(phase + QUARTER_PHASE_COUNT),
      0,
      0);
    appendQuadword(
      &memory,
      FLOAT_SCREEN_X,
      FLOAT_SCREEN_Y,
      FLOAT_PROJECTION,
      0);
    appendQuadword(
      &memory,
      POINT_COUNT,
      ROTATE_AROUND_Z,
      POINT_STRIDE_QUADWORDS,
      0);

    appendQuadword(
      &memory,
      packedTag(
        1,
        false,
        1,
        GIFRegisterDescriptor::AD));
    appendRegisterWrite(
      &memory,
      GSRegisterAddress::PRIM,
      TRIANGLE_GOURAUD_PRIMITIVE);

    const std::uint64_t vertexRegisters =
      GIFRegisterDescriptor::AD |
      (static_cast<std::uint64_t>(
        GIFRegisterDescriptor::XYZ2) << 4);
    appendQuadword(
      &memory,
      packedTag(3, true, 2, vertexRegisters));

    const std::uint64_t qValue =
      static_cast<std::uint64_t>(FLOAT_ONE) << 32;
    appendRegisterWrite(
      &memory,
      GSRegisterAddress::RGBAQ,
      qValue | RED);
    appendQuadword(
      &memory,
      FLOAT_NEGATIVE_64,
      FLOAT_NEGATIVE_64,
      0,
      0);
    appendRegisterWrite(
      &memory,
      GSRegisterAddress::RGBAQ,
      qValue | GREEN);
    appendQuadword(
      &memory,
      FLOAT_NEGATIVE_64,
      FLOAT_POSITIVE_96,
      0,
      0);
    appendRegisterWrite(
      &memory,
      GSRegisterAddress::RGBAQ,
      qValue | BLUE);
    appendQuadword(
      &memory,
      FLOAT_ZERO,
      FLOAT_POSITIVE_96,
      0,
      0);
    return memory;
  }

  void configureFrame(GS *gs)
  {
    gs->writeRegister(
      GSRegisterAddress::FRAME_1,
      (static_cast<std::uint64_t>(FRAME_WIDTH_PAGES) << 16) |
      (static_cast<std::uint64_t>(
        GSPixelStorageMode::PSMCT32) << 24));
    gs->writeRegister(
      GSRegisterAddress::XYOFFSET_1,
      (static_cast<std::uint64_t>(XY_OFFSET_Y) << 36) |
      (static_cast<std::uint64_t>(XY_OFFSET_X) << 4));
    gs->writeRegister(
      GSRegisterAddress::SCISSOR_1,
      (static_cast<std::uint64_t>(FRAME_MAX_X) << 16) |
      (static_cast<std::uint64_t>(FRAME_MAX_Y) << 48));
  }
}

neko_demo::RotationVU1Result neko_demo::renderRotationVU1(
  const std::vector<std::uint8_t> &microprogram,
  std::uint32_t phase)
{
  GS gs;
  configureFrame(&gs);
  GIFDecoder decoder;
  decoder.attachRegisterWriteHandler(&gs);
  GIFPathArbiter arbiter(&decoder);
  GIFPath1Transfer path1(arbiter);
  VPU vpu(VPUType::VU1);
  path1.attachVPU(&vpu);

  vpu.uploadMicroInstructions(microprogram);
  vpu.writeDataMemory(0, rotationInput(phase));
  vpu.startMicroMode();

  RotationVU1Result result;
  result.cycleCount = vpu.run(CYCLE_BUDGET);
  result.rgbaPixels = gs.framebufferRGBA8(
    0,
    ROTATION_FRAME_WIDTH,
    ROTATION_FRAME_HEIGHT);
  result.framebufferHash = gs.framebufferHash(
    0,
    ROTATION_FRAME_WIDTH,
    ROTATION_FRAME_HEIGHT);
  result.transferredQuadwords =
    path1.transferredQuadwordCount();
  result.triangleCount = gs.triangleCount();
  result.pixelWriteCount = gs.pixelWriteCount();
  result.vpuCompleted = vpu.getState() == VPU_STATE_READY;
  result.path1Completed = !path1.path1TransferActive();
  return result;
}
