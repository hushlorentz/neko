#include <stdexcept>
#include <vector>

#include "gif_path3.hpp"
#include "gs.hpp"
#include "primitive_scene.hpp"

namespace
{
  constexpr std::uint16_t FIXED_POINT_ONE = 16;
  constexpr std::uint16_t STAR_COUNT = 96;

  GIFQuadword gifTag(
    std::uint16_t loopCount,
    std::uint8_t registerCount,
    std::uint64_t descriptors,
    std::uint16_t primitive)
  {
    const std::uint64_t low =
      loopCount |
      (UINT64_C(1) << 15) |
      (UINT64_C(1) << 46) |
      (static_cast<std::uint64_t>(primitive) << 47) |
      (static_cast<std::uint64_t>(registerCount) << 60);
    return GIFQuadword{{
      static_cast<std::uint32_t>(low),
      static_cast<std::uint32_t>(low >> 32),
      static_cast<std::uint32_t>(descriptors),
      static_cast<std::uint32_t>(descriptors >> 32)
    }};
  }

  GIFQuadword adTag(std::uint16_t loopCount)
  {
    return gifTag(
      loopCount,
      1,
      GIFRegisterDescriptor::AD,
      0);
  }

  GIFQuadword imageTag(std::uint16_t loopCount)
  {
    const std::uint64_t low =
      loopCount |
      (UINT64_C(1) << 15) |
      (static_cast<std::uint64_t>(GIFDataFormat::Image) << 58);
    return GIFQuadword{{
      static_cast<std::uint32_t>(low),
      static_cast<std::uint32_t>(low >> 32),
      0,
      0
    }};
  }

  GIFQuadword adWrite(
    std::uint8_t address,
    std::uint64_t data)
  {
    return GIFQuadword{{
      static_cast<std::uint32_t>(data),
      static_cast<std::uint32_t>(data >> 32),
      address,
      0
    }};
  }

  GIFQuadword packedColor(
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha = 0xff)
  {
    return GIFQuadword{{red, green, blue, alpha}};
  }

  GIFQuadword packedVertex(
    std::uint16_t x,
    std::uint16_t y)
  {
    return GIFQuadword{{
      static_cast<std::uint32_t>(x) * FIXED_POINT_ONE,
      static_cast<std::uint32_t>(y) * FIXED_POINT_ONE,
      0,
      0
    }};
  }

  GIFQuadword packedUV(
    std::uint16_t u,
    std::uint16_t v)
  {
    return GIFQuadword{{u, v, 0, 0}};
  }

  void submitPacket(
    GIFPath3Transfer *path3,
    const std::vector<GIFQuadword> &packet,
    std::uint64_t *transferredQuadwords)
  {
    const GIFPath3SubmissionResult result =
      path3->submitQuadwords(packet.data(), packet.size());
    if (result.stalled ||
        result.transferredQuadwords != packet.size() ||
        !result.packetComplete)
    {
      throw std::runtime_error(
          "Primitive-scene PATH3 packet did not complete.");
    }
    *transferredQuadwords += result.transferredQuadwords;
  }

  std::vector<GIFQuadword> setupPacket()
  {
    const std::uint64_t frame =
      (UINT64_C(10) << 16) |
      (static_cast<std::uint64_t>(
        GSPixelStorageMode::PSMCT32) << 24);
    const std::uint64_t scissor =
      (UINT64_C(639) << 16) |
      (UINT64_C(447) << 48);
    return {
      adTag(3),
      adWrite(GSRegisterAddress::FRAME_1, frame),
      adWrite(GSRegisterAddress::SCISSOR_1, scissor),
      adWrite(GSRegisterAddress::XYOFFSET_1, 0)
    };
  }

  std::vector<GIFQuadword> textureUploadPacket()
  {
    constexpr std::uint16_t TEXTURE_SIZE = 64;
    constexpr std::uint16_t TEXTURE_BASE = 8192;
    constexpr std::uint16_t PIXELS_PER_QUADWORD = 4;
    constexpr std::uint16_t IMAGE_QUADWORDS =
      TEXTURE_SIZE * TEXTURE_SIZE / PIXELS_PER_QUADWORD;
    const std::uint64_t transferBuffer =
      static_cast<std::uint64_t>(TEXTURE_BASE) << 32 |
      UINT64_C(1) << 48;
    const std::uint64_t transferRegion =
      TEXTURE_SIZE |
      (static_cast<std::uint64_t>(TEXTURE_SIZE) << 32);
    const std::uint64_t texture =
      TEXTURE_BASE |
      (UINT64_C(1) << 14) |
      (UINT64_C(6) << 26) |
      (UINT64_C(6) << 30) |
      (UINT64_C(1) << 34) |
      (UINT64_C(1) << 35);

    std::vector<GIFQuadword> packet;
    packet.reserve(6 + IMAGE_QUADWORDS);
    packet.push_back(adTag(4));
    packet.push_back(adWrite(
      GSRegisterAddress::BITBLTBUF,
      transferBuffer));
    packet.push_back(adWrite(
      GSRegisterAddress::TRXREG,
      transferRegion));
    packet.push_back(adWrite(GSRegisterAddress::TRXDIR, 0));
    packet.push_back(adWrite(GSRegisterAddress::TEX0_1, texture));
    packet.push_back(imageTag(IMAGE_QUADWORDS));
    for (std::uint16_t y = 0; y < TEXTURE_SIZE; ++y)
    {
      for (std::uint16_t x = 0;
           x < TEXTURE_SIZE;
           x += PIXELS_PER_QUADWORD)
      {
        GIFQuadword pixels = {};
        for (std::uint16_t index = 0;
             index < PIXELS_PER_QUADWORD;
             ++index)
        {
          const std::uint16_t textureX = x + index;
          const bool alternate =
            ((textureX / 8) + (y / 8)) % 2 != 0;
          const bool grid =
            textureX % 8 == 0 || y % 8 == 0;
          const std::uint8_t red = grid
            ? 255
            : alternate ? 20 : 105;
          const std::uint8_t green = grid
            ? 75
            : alternate ? 190 : 25;
          const std::uint8_t blue = grid
            ? 220
            : alternate ? 230 : 170;
          pixels[index] =
            red |
            (static_cast<std::uint32_t>(green) << 8) |
            (static_cast<std::uint32_t>(blue) << 16) |
            UINT32_C(0xff000000);
        }
        packet.push_back(pixels);
      }
    }
    return packet;
  }

  std::vector<GIFQuadword> texturedSpritePacket()
  {
    constexpr std::uint16_t TEXTURE_EDGE = 64 * FIXED_POINT_ONE;
    const std::uint64_t descriptors =
      GIFRegisterDescriptor::RGBAQ |
      (static_cast<std::uint64_t>(
        GIFRegisterDescriptor::UV) << 4) |
      (static_cast<std::uint64_t>(
        GIFRegisterDescriptor::XYZ2) << 8);
    std::vector<GIFQuadword> packet;
    packet.reserve(7);
    packet.push_back(gifTag(
      2,
      3,
      descriptors,
      static_cast<std::uint16_t>(GSPrimitiveType::Sprite) |
        (UINT64_C(1) << 4) |
        (UINT64_C(1) << 8)));
    packet.push_back(packedColor(0x80, 0x80, 0x80, 0x80));
    packet.push_back(packedUV(0, 0));
    packet.push_back(packedVertex(96, 96));
    packet.push_back(packedColor(0x80, 0x80, 0x80, 0x80));
    packet.push_back(packedUV(TEXTURE_EDGE, TEXTURE_EDGE));
    packet.push_back(packedVertex(544, 352));
    return packet;
  }

  std::vector<GIFQuadword> alphaSetupPacket()
  {
    return {
      adTag(1),
      adWrite(GSRegisterAddress::ALPHA_1, UINT64_C(0x44))
    };
  }

  void appendSprite(
    std::vector<GIFQuadword> *packet,
    std::uint16_t x0,
    std::uint16_t y0,
    std::uint16_t x1,
    std::uint16_t y1,
    const GIFQuadword &color);

  std::vector<GIFQuadword> alphaGlowPacket(
    std::uint32_t phase)
  {
    constexpr std::uint16_t GLOW_COUNT = 6;
    const std::uint64_t descriptors =
      GIFRegisterDescriptor::RGBAQ |
      (static_cast<std::uint64_t>(
        GIFRegisterDescriptor::XYZ2) << 4);
    std::vector<GIFQuadword> packet;
    packet.reserve(1 + GLOW_COUNT * 4);
    packet.push_back(gifTag(
      GLOW_COUNT * 2,
      2,
      descriptors,
      static_cast<std::uint16_t>(GSPrimitiveType::Sprite) |
        (UINT64_C(1) << 6)));

    const std::uint16_t drift =
      static_cast<std::uint16_t>(
        phase < 64 ? phase : 127 - phase);
    for (std::uint16_t index = 0;
         index < GLOW_COUNT;
         ++index)
    {
      const std::uint16_t inset = 24 + index * 18;
      const std::uint16_t offset =
        (index % 2 == 0 ? drift : 64 - drift) / 2;
      const GIFQuadword color = packedColor(
        static_cast<std::uint8_t>(40 + index * 34),
        static_cast<std::uint8_t>(210 - index * 22),
        static_cast<std::uint8_t>(245 - index * 13),
        static_cast<std::uint8_t>(24 + index * 8));
      appendSprite(
        &packet,
        static_cast<std::uint16_t>(inset + offset),
        static_cast<std::uint16_t>(112 + index * 24),
        static_cast<std::uint16_t>(640 - inset + offset / 2),
        static_cast<std::uint16_t>(336 - index * 16),
        color);
    }
    return packet;
  }

  std::vector<GIFQuadword> pointPacket(std::uint32_t phase)
  {
    const std::uint64_t descriptors =
      GIFRegisterDescriptor::RGBAQ |
      (static_cast<std::uint64_t>(
        GIFRegisterDescriptor::XYZ2) << 4);
    std::vector<GIFQuadword> packet;
    packet.reserve(1 + STAR_COUNT * 2);
    packet.push_back(gifTag(
      STAR_COUNT,
      2,
      descriptors,
      static_cast<std::uint16_t>(GSPrimitiveType::Point)));
    for (std::uint16_t index = 0;
         index < STAR_COUNT;
         ++index)
    {
      const std::uint16_t x =
        (index * 83 + phase * (1 + index % 3)) %
        neko_demo::PRIMITIVE_FRAME_WIDTH;
      const std::uint16_t y =
        (index * 47 + index * index * 3) %
        neko_demo::PRIMITIVE_FRAME_HEIGHT;
      const std::uint8_t intensity =
        static_cast<std::uint8_t>(
          96 + ((index * 29 + phase * 5) % 160));
      packet.push_back(packedColor(
        intensity,
        intensity,
        static_cast<std::uint8_t>(
          160 + intensity / 3)));
      packet.push_back(packedVertex(x, y));
    }
    return packet;
  }

  std::vector<GIFQuadword> linePacket(std::uint32_t phase)
  {
    constexpr std::uint16_t LINE_COUNT = 4;
    const std::uint64_t descriptors =
      GIFRegisterDescriptor::RGBAQ |
      (static_cast<std::uint64_t>(
        GIFRegisterDescriptor::XYZ2) << 4);
    std::vector<GIFQuadword> packet;
    packet.reserve(1 + LINE_COUNT * 4);
    packet.push_back(gifTag(
      LINE_COUNT * 2,
      2,
      descriptors,
      static_cast<std::uint16_t>(GSPrimitiveType::Line) |
        (UINT64_C(1) << 3)));

    packet.push_back(packedColor(40, 80, 180));
    packet.push_back(packedVertex(104, 112));
    packet.push_back(packedColor(240, 80, 160));
    packet.push_back(packedVertex(536, 336));
    packet.push_back(packedColor(80, 220, 240));
    packet.push_back(packedVertex(536, 112));
    packet.push_back(packedColor(250, 190, 50));
    packet.push_back(packedVertex(104, 336));

    const std::uint16_t movingX =
      112 + (phase * 3) % 416;
    packet.push_back(packedColor(120, 60, 255));
    packet.push_back(packedVertex(movingX, 112));
    packet.push_back(packedColor(60, 255, 180));
    packet.push_back(packedVertex(movingX, 336));
    const std::uint16_t movingY =
      120 + (phase * 2) % 208;
    packet.push_back(packedColor(255, 90, 60));
    packet.push_back(packedVertex(112, movingY));
    packet.push_back(packedColor(70, 130, 255));
    packet.push_back(packedVertex(528, movingY));
    return packet;
  }

  std::vector<GIFQuadword> lineStripPacket(std::uint32_t phase)
  {
    constexpr std::uint16_t VERTEX_COUNT = 9;
    const std::uint64_t descriptors =
      GIFRegisterDescriptor::RGBAQ |
      (static_cast<std::uint64_t>(
        GIFRegisterDescriptor::XYZ2) << 4);
    std::vector<GIFQuadword> packet;
    packet.reserve(1 + VERTEX_COUNT * 2);
    packet.push_back(gifTag(
      VERTEX_COUNT,
      2,
      descriptors,
      static_cast<std::uint16_t>(GSPrimitiveType::LineStrip) |
        (UINT64_C(1) << 3)));
    for (std::uint16_t index = 0;
         index < VERTEX_COUNT;
         ++index)
    {
      std::uint16_t wave =
        (index * 23 + phase * 2) % 96;
      if (wave > 48)
      {
        wave = 96 - wave;
      }
      packet.push_back(packedColor(
        static_cast<std::uint8_t>(70 + index * 20),
        static_cast<std::uint8_t>(230 - index * 12),
        static_cast<std::uint8_t>(120 + index * 14)));
      packet.push_back(packedVertex(
        112 + index * 52,
        176 + wave * 2));
    }
    return packet;
  }

  std::vector<GIFQuadword> triangleStripPacket(
    std::uint32_t phase)
  {
    constexpr std::uint16_t COLUMN_COUNT = 6;
    constexpr std::uint16_t VERTEX_COUNT = COLUMN_COUNT * 2;
    const std::uint64_t descriptors =
      GIFRegisterDescriptor::RGBAQ |
      (static_cast<std::uint64_t>(
        GIFRegisterDescriptor::XYZ2) << 4);
    std::vector<GIFQuadword> packet;
    packet.reserve(1 + VERTEX_COUNT * 2);
    packet.push_back(gifTag(
      VERTEX_COUNT,
      2,
      descriptors,
      static_cast<std::uint16_t>(
        GSPrimitiveType::TriangleStrip) |
        (UINT64_C(1) << 3)));
    for (std::uint16_t column = 0;
         column < COLUMN_COUNT;
         ++column)
    {
      std::uint16_t wave =
        (column * 19 + phase) % 48;
      if (wave > 24)
      {
        wave = 48 - wave;
      }
      const std::uint16_t x = 120 + column * 80;
      packet.push_back(packedColor(
        static_cast<std::uint8_t>(25 + column * 12),
        static_cast<std::uint8_t>(35 + wave * 2),
        static_cast<std::uint8_t>(100 + column * 18)));
      packet.push_back(packedVertex(x, 142 + wave));
      packet.push_back(packedColor(
        static_cast<std::uint8_t>(90 + column * 15),
        static_cast<std::uint8_t>(25 + column * 8),
        static_cast<std::uint8_t>(120 + wave * 3)));
      packet.push_back(packedVertex(x, 306 - wave));
    }
    return packet;
  }

  std::vector<GIFQuadword> triangleFanPacket(
    std::uint32_t phase)
  {
    constexpr std::uint16_t VERTEX_COUNT = 10;
    constexpr std::uint16_t PERIMETER[][2] = {
      {320, 126},
      {390, 154},
      {418, 224},
      {390, 294},
      {320, 322},
      {250, 294},
      {222, 224},
      {250, 154},
      {320, 126}
    };
    const std::uint64_t descriptors =
      GIFRegisterDescriptor::RGBAQ |
      (static_cast<std::uint64_t>(
        GIFRegisterDescriptor::XYZ2) << 4);
    std::vector<GIFQuadword> packet;
    packet.reserve(1 + VERTEX_COUNT * 2);
    packet.push_back(gifTag(
      VERTEX_COUNT,
      2,
      descriptors,
      static_cast<std::uint16_t>(
        GSPrimitiveType::TriangleFan) |
        (UINT64_C(1) << 3)));

    const std::uint16_t centerX =
      288 + (phase < 64 ? phase : 127 - phase);
    packet.push_back(packedColor(65, 35, 115));
    packet.push_back(packedVertex(centerX, 224));
    for (std::uint16_t index = 0; index < 9; ++index)
    {
      packet.push_back(packedColor(
        static_cast<std::uint8_t>(20 + index * 12),
        static_cast<std::uint8_t>(30 + (index % 3) * 20),
        static_cast<std::uint8_t>(75 + index * 8)));
      packet.push_back(packedVertex(
        PERIMETER[index][0],
        PERIMETER[index][1]));
    }
    return packet;
  }

  void appendSprite(
    std::vector<GIFQuadword> *packet,
    std::uint16_t x0,
    std::uint16_t y0,
    std::uint16_t x1,
    std::uint16_t y1,
    const GIFQuadword &color)
  {
    packet->push_back(color);
    packet->push_back(packedVertex(x0, y0));
    packet->push_back(color);
    packet->push_back(packedVertex(x1, y1));
  }

  std::vector<GIFQuadword> spritePacket(std::uint32_t phase)
  {
    constexpr std::uint16_t SPRITE_COUNT = 7;
    const std::uint64_t descriptors =
      GIFRegisterDescriptor::RGBAQ |
      (static_cast<std::uint64_t>(
        GIFRegisterDescriptor::XYZ2) << 4);
    std::vector<GIFQuadword> packet;
    packet.reserve(1 + SPRITE_COUNT * 4);
    packet.push_back(gifTag(
      SPRITE_COUNT * 2,
      2,
      descriptors,
      static_cast<std::uint16_t>(GSPrimitiveType::Sprite)));

    appendSprite(
      &packet, 80, 80, 560, 88,
      packedColor(30, 60, 120));
    appendSprite(
      &packet, 80, 360, 560, 368,
      packedColor(30, 60, 120));
    appendSprite(
      &packet, 80, 88, 88, 360,
      packedColor(30, 60, 120));
    appendSprite(
      &packet, 552, 88, 560, 360,
      packedColor(30, 60, 120));

    const std::uint16_t movingX =
      104 + (phase * 3) % 400;
    appendSprite(
      &packet, movingX, 180, movingX + 32, 212,
      packedColor(255, 80, 70));
    const std::uint16_t mirroredX =
      504 - (phase * 3) % 400;
    appendSprite(
      &packet, mirroredX, 236, mirroredX + 32, 268,
      packedColor(70, 210, 255));
    const std::uint16_t pulse =
      static_cast<std::uint16_t>(
        20 + (phase < 64 ? phase : 127 - phase) / 2);
    appendSprite(
      &packet,
      320 - pulse,
      224 - pulse,
      320 + pulse,
      224 + pulse,
      packedColor(240, 190, 40));
    return packet;
  }
}

namespace
{
  enum class SceneVersion
  {
    PointsAndSprites,
    PointsLinesAndSprites,
    AllPrimitives,
    TexturedPrimitives,
    AlphaPrimitives
  };

  neko_demo::PrimitiveSceneResult renderScene(
    std::uint32_t phase,
    SceneVersion version)
  {
    GS gs;
    GIFDecoder decoder;
    decoder.attachRegisterWriteHandler(&gs);
    GIFPath3Transfer path3(&decoder);
    std::uint64_t transferredQuadwords = 0;
    phase %= neko_demo::PRIMITIVE_PHASE_COUNT;

    submitPacket(
      &path3,
      setupPacket(),
      &transferredQuadwords);
    if (version == SceneVersion::TexturedPrimitives ||
        version == SceneVersion::AlphaPrimitives)
    {
      submitPacket(
        &path3,
        textureUploadPacket(),
        &transferredQuadwords);
      submitPacket(
        &path3,
        texturedSpritePacket(),
        &transferredQuadwords);
    }
    if (version == SceneVersion::AlphaPrimitives)
    {
      submitPacket(
        &path3,
        alphaSetupPacket(),
        &transferredQuadwords);
      submitPacket(
        &path3,
        alphaGlowPacket(phase),
        &transferredQuadwords);
    }
    if (version == SceneVersion::AllPrimitives ||
        version == SceneVersion::TexturedPrimitives ||
        version == SceneVersion::AlphaPrimitives)
    {
      submitPacket(
        &path3,
        triangleStripPacket(phase),
        &transferredQuadwords);
      submitPacket(
        &path3,
        triangleFanPacket(phase),
        &transferredQuadwords);
    }
    submitPacket(
      &path3,
      pointPacket(phase),
      &transferredQuadwords);
    if (version != SceneVersion::PointsAndSprites)
    {
      submitPacket(
        &path3,
        linePacket(phase),
        &transferredQuadwords);
      submitPacket(
        &path3,
        lineStripPacket(phase),
        &transferredQuadwords);
    }
    submitPacket(
      &path3,
      spritePacket(phase),
      &transferredQuadwords);

    neko_demo::PrimitiveSceneResult result;
    result.rgbaPixels = gs.framebufferRGBA8(
      0,
      neko_demo::PRIMITIVE_FRAME_WIDTH,
      neko_demo::PRIMITIVE_FRAME_HEIGHT);
    result.framebufferHash = gs.framebufferHash(
      0,
      neko_demo::PRIMITIVE_FRAME_WIDTH,
      neko_demo::PRIMITIVE_FRAME_HEIGHT);
    result.pointCount = gs.pointCount();
    result.lineCount = gs.lineCount();
    result.spriteCount = gs.spriteCount();
    result.triangleCount = gs.triangleCount();
    result.pixelWriteCount = gs.pixelWriteCount();
    result.transferredQuadwords = transferredQuadwords;
    return result;
  }
}

neko_demo::PrimitiveSceneResult
neko_demo::renderPrimitiveScene(std::uint32_t phase)
{
  return renderScene(phase, SceneVersion::AlphaPrimitives);
}

neko_demo::PrimitiveSceneResult
neko_demo::renderUntexturedPrimitiveScene(std::uint32_t phase)
{
  return renderScene(phase, SceneVersion::AllPrimitives);
}

neko_demo::PrimitiveSceneResult
neko_demo::renderTexturedPrimitiveScene(std::uint32_t phase)
{
  return renderScene(phase, SceneVersion::TexturedPrimitives);
}

neko_demo::PrimitiveSceneResult
neko_demo::renderPointSpriteScene(std::uint32_t phase)
{
  return renderScene(phase, SceneVersion::PointsAndSprites);
}

neko_demo::PrimitiveSceneResult
neko_demo::renderPointLineSpriteScene(std::uint32_t phase)
{
  return renderScene(
    phase,
    SceneVersion::PointsLinesAndSprites);
}
