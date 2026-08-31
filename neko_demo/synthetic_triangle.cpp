#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "gif_path1.hpp"
#include "gs.hpp"
#include "synthetic_triangle.hpp"
#include "vif.hpp"
#include "vpu.hpp"
#include "vpu_field_mask.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"

namespace
{
  constexpr std::uint16_t PACKET_ADDRESS = 128;
  constexpr std::uint16_t VERTEX_ADDRESS = 64;
  constexpr std::uint16_t TRIANGLE_PRIMITIVE = 3;
  constexpr std::size_t PROGRAM_INSTRUCTION_COUNT = 15;
  constexpr std::uint16_t PACKET_QUADWORD_COUNT = 11;
  constexpr std::int16_t FIRST_VERTEX_PACKET_OFFSET = 6;
  constexpr std::int16_t SECOND_VERTEX_PACKET_OFFSET = 8;
  constexpr std::int16_t THIRD_VERTEX_PACKET_OFFSET = 10;
  constexpr std::uint16_t VIF_UNPACK_CYCLE = 0x0404;
  constexpr std::uint16_t VU_MEMORY_OFFSET_MASK = 0x07ff;
  constexpr std::uint16_t FRAME_WIDTH_PAGES = 1;
  constexpr std::uint16_t FRAME_MAX_COORDINATE = 7;
  constexpr std::uint32_t FLOAT_ONE_BITS = 0x3f800000;
  constexpr std::uint32_t FLOAT_FOUR_BITS = 0x40800000;
  constexpr std::uint8_t COLOR_RED = 0x10;
  constexpr std::uint8_t COLOR_GREEN = 0x20;
  constexpr std::uint8_t COLOR_BLUE = 0x40;
  constexpr std::uint8_t COLOR_ALPHA = 0x80;
  constexpr std::uint32_t VU_CYCLE_BUDGET = 200;

  std::uint32_t vifCode(
    std::uint8_t command,
    std::uint8_t count = 0,
    std::uint16_t immediate = 0)
  {
    return
      (static_cast<std::uint32_t>(command) << 24) |
      (static_cast<std::uint32_t>(count) << 16) |
      immediate;
  }

  GIFQuadword gifTag(
    std::uint16_t loopCount,
    bool endOfPacket,
    std::uint8_t registerCount,
    std::uint64_t registers,
    bool primitiveEnabled = false,
    std::uint16_t primitive = 0)
  {
    const std::uint64_t low =
      loopCount |
      (static_cast<std::uint64_t>(endOfPacket) << 15) |
      (static_cast<std::uint64_t>(primitiveEnabled) << 46) |
      (static_cast<std::uint64_t>(primitive) << 47) |
      (static_cast<std::uint64_t>(GIFDataFormat::Packed) << 58) |
      (static_cast<std::uint64_t>(registerCount) << 60);
    return GIFQuadword{{
      static_cast<std::uint32_t>(low),
      static_cast<std::uint32_t>(low >> 32),
      static_cast<std::uint32_t>(registers),
      static_cast<std::uint32_t>(registers >> 32)
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

  GIFQuadword packedRGBA()
  {
    return GIFQuadword{{
      COLOR_RED,
      COLOR_GREEN,
      COLOR_BLUE,
      COLOR_ALPHA
    }};
  }

  std::uint32_t iaddiu(
    std::uint8_t destination,
    std::uint8_t source,
    std::uint16_t immediate)
  {
    constexpr std::uint8_t IMMEDIATE_HIGH_SHIFT = 21;
    constexpr std::uint8_t DESTINATION_SHIFT = 16;
    constexpr std::uint8_t SOURCE_SHIFT = 11;
    constexpr std::uint16_t IMMEDIATE_HIGH_MASK = 0x7800;
    constexpr std::uint16_t IMMEDIATE_LOW_MASK = 0x07ff;
    return
      VPU_IADDIU_ENCODING |
      (static_cast<std::uint32_t>(
        immediate & IMMEDIATE_HIGH_MASK) <<
       (IMMEDIATE_HIGH_SHIFT - SOURCE_SHIFT)) |
      (static_cast<std::uint32_t>(destination) <<
       DESTINATION_SHIFT) |
      (static_cast<std::uint32_t>(source) << SOURCE_SHIFT) |
      (immediate & IMMEDIATE_LOW_MASK);
  }

  std::uint32_t lq(
    std::uint8_t destination,
    std::uint8_t addressRegister,
    std::int16_t offset)
  {
    return
      VPU_LQ_ENCODING |
      (static_cast<std::uint32_t>(
        vpuFieldMaskToEncoding(FP_REGISTER_ALL_FIELDS)) << 21) |
      (static_cast<std::uint32_t>(destination) << 16) |
      (static_cast<std::uint32_t>(addressRegister) << 11) |
      (static_cast<std::uint16_t>(offset) &
       VU_MEMORY_OFFSET_MASK);
  }

  std::uint32_t sq(
    std::uint8_t source,
    std::uint8_t addressRegister,
    std::int16_t offset)
  {
    return
      VPU_SQ_ENCODING |
      (static_cast<std::uint32_t>(
        vpuFieldMaskToEncoding(FP_REGISTER_ALL_FIELDS)) << 21) |
      (static_cast<std::uint32_t>(addressRegister) << 16) |
      (static_cast<std::uint32_t>(source) << 11) |
      (static_cast<std::uint16_t>(offset) &
       VU_MEMORY_OFFSET_MASK);
  }

  std::uint32_t ftoi4(
    std::uint8_t destination,
    std::uint8_t source)
  {
    return
      VPU_DEST_X_BIT |
      VPU_DEST_Y_BIT |
      (static_cast<std::uint32_t>(destination) <<
       VPU_FT_REG_SHIFT) |
      (static_cast<std::uint32_t>(source) <<
       VPU_FS_REG_SHIFT) |
      VPU_FTOI4;
  }

  std::uint32_t xgkick(std::uint8_t source)
  {
    return
      VPU_XGKICK_ENCODING |
      (static_cast<std::uint32_t>(source) << VPU_FS_REG_SHIFT);
  }

  void requireUnstalled(const VIFStreamWord &word)
  {
    if (word.stalled)
    {
      throw std::runtime_error(
        "Synthetic triangle VIF stream stalled unexpectedly.");
    }
  }

  void ingestQuadword(
    VIF *vif,
    const GIFQuadword &quadword)
  {
    for (std::uint32_t word : quadword)
    {
      requireUnstalled(vif->ingestWord(word));
    }
  }

  void unpackQuadwords(
    VIF *vif,
    std::uint16_t address,
    const std::vector<GIFQuadword> &quadwords)
  {
    const std::uint8_t command =
      VIFCommandEncoding::UNPACK |
      VIFUnpackEncoding::V4_32;
    requireUnstalled(vif->ingestWord(vifCode(
      command,
      static_cast<std::uint8_t>(quadwords.size()),
      address)));
    for (const GIFQuadword &quadword : quadwords)
    {
      ingestQuadword(vif, quadword);
    }
  }

  std::array<std::array<std::uint32_t, 2>,
    PROGRAM_INSTRUCTION_COUNT> triangleProgram()
  {
    return {{
      {{iaddiu(VPU_REGISTER_VI01, VPU_REGISTER_VI00, VERTEX_ADDRESS),
        VPU_NOP}},
      {{iaddiu(VPU_REGISTER_VI02, VPU_REGISTER_VI00, PACKET_ADDRESS),
        VPU_NOP}},
      {{lq(VPU_REGISTER_VF01, VPU_REGISTER_VI01, 0), VPU_NOP}},
      {{VPU_LOWER_NOP, ftoi4(VPU_REGISTER_VF02, VPU_REGISTER_VF01)}},
      {{sq(VPU_REGISTER_VF02, VPU_REGISTER_VI02,
          FIRST_VERTEX_PACKET_OFFSET), VPU_NOP}},
      {{lq(VPU_REGISTER_VF03, VPU_REGISTER_VI01, 1), VPU_NOP}},
      {{VPU_LOWER_NOP, ftoi4(VPU_REGISTER_VF04, VPU_REGISTER_VF03)}},
      {{sq(VPU_REGISTER_VF04, VPU_REGISTER_VI02,
          SECOND_VERTEX_PACKET_OFFSET), VPU_NOP}},
      {{lq(VPU_REGISTER_VF05, VPU_REGISTER_VI01, 2), VPU_NOP}},
      {{VPU_LOWER_NOP, ftoi4(VPU_REGISTER_VF06, VPU_REGISTER_VF05)}},
      {{sq(VPU_REGISTER_VF06, VPU_REGISTER_VI02,
          THIRD_VERTEX_PACKET_OFFSET), VPU_NOP}},
      {{iaddiu(VPU_REGISTER_VI03, VPU_REGISTER_VI00, PACKET_ADDRESS),
        VPU_NOP}},
      {{xgkick(VPU_REGISTER_VI03), VPU_NOP}},
      {{VPU_LOWER_NOP, VPU_E_BIT | VPU_NOP}},
      {{VPU_LOWER_NOP, VPU_NOP}}
    }};
  }

  void uploadProgram(VIF *vif)
  {
    requireUnstalled(
      vif->ingestWord(vifCode(VIFCommandEncoding::NOP)));
    requireUnstalled(vif->ingestWord(vifCode(
      VIFCommandEncoding::MPG,
      PROGRAM_INSTRUCTION_COUNT)));
    for (const auto &instruction : triangleProgram())
    {
      requireUnstalled(vif->ingestWord(instruction[0]));
      requireUnstalled(vif->ingestWord(instruction[1]));
    }
  }

  std::vector<GIFQuadword> packetTemplate()
  {
    const std::uint64_t frame =
      static_cast<std::uint64_t>(FRAME_WIDTH_PAGES) << 16 |
      static_cast<std::uint64_t>(
        GSPixelStorageMode::PSMCT32) << 24;
    const std::uint64_t scissor =
      static_cast<std::uint64_t>(FRAME_MAX_COORDINATE) << 16 |
      static_cast<std::uint64_t>(FRAME_MAX_COORDINATE) << 48;
    const std::uint64_t vertexDescriptors =
      GIFRegisterDescriptor::RGBAQ |
      (static_cast<std::uint64_t>(
        GIFRegisterDescriptor::XYZ2) << 4);
    return {
      gifTag(3, false, 1, GIFRegisterDescriptor::AD),
      adWrite(GSRegisterAddress::FRAME_1, frame),
      adWrite(GSRegisterAddress::SCISSOR_1, scissor),
      adWrite(GSRegisterAddress::XYOFFSET_1, 0),
      gifTag(
        3,
        true,
        2,
        vertexDescriptors,
        true,
        TRIANGLE_PRIMITIVE),
      packedRGBA(),
      GIFQuadword{{0, 0, 0, 0}},
      packedRGBA(),
      GIFQuadword{{0, 0, 0, 0}},
      packedRGBA(),
      GIFQuadword{{0, 0, 0, 0}}
    };
  }
}

neko_demo::SyntheticTriangleResult
neko_demo::renderSyntheticTriangle()
{
  GS gs;
  GIFDecoder decoder;
  decoder.attachRegisterWriteHandler(&gs);
  GIFPathArbiter arbiter(&decoder);
  VPU vpu(VPUType::VU1);
  GIFPath1Transfer path1(arbiter);
  VIF vif(VIFType::VIF1);
  path1.attachVPU(&vpu);
  vif.attachVPU(&vpu);
  vif.attachGIFPathArbiter(&arbiter);

  uploadProgram(&vif);
  requireUnstalled(vif.ingestWord(vifCode(
    VIFCommandEncoding::STCYCL,
    0,
    VIF_UNPACK_CYCLE)));
  unpackQuadwords(&vif, PACKET_ADDRESS, packetTemplate());
  unpackQuadwords(
    &vif,
    VERTEX_ADDRESS,
    {
      GIFQuadword{{FLOAT_ONE_BITS, FLOAT_ONE_BITS, 0, 0}},
      GIFQuadword{{FLOAT_FOUR_BITS, FLOAT_ONE_BITS, 0, 0}},
      GIFQuadword{{FLOAT_ONE_BITS, FLOAT_FOUR_BITS, 0, 0}}
    });
  requireUnstalled(vif.ingestWord(
    vifCode(VIFCommandEncoding::MSCAL)));
  vpu.run(VU_CYCLE_BUDGET);

  SyntheticTriangleResult result;
  result.rgbaPixels = gs.framebufferRGBA8(
    0,
    SYNTHETIC_FRAME_WIDTH,
    SYNTHETIC_FRAME_HEIGHT);
  result.transformedVertices = {{
    vpu.readDataQuadword(
      PACKET_ADDRESS + FIRST_VERTEX_PACKET_OFFSET),
    vpu.readDataQuadword(
      PACKET_ADDRESS + SECOND_VERTEX_PACKET_OFFSET),
    vpu.readDataQuadword(
      PACKET_ADDRESS + THIRD_VERTEX_PACKET_OFFSET)
  }};
  result.framebufferHash = gs.framebufferHash(
    0,
    SYNTHETIC_FRAME_WIDTH,
    SYNTHETIC_FRAME_HEIGHT);
  result.transferredQuadwords =
    path1.transferredQuadwordCount();
  result.triangleCount = gs.triangleCount();
  result.pixelWriteCount = gs.pixelWriteCount();
  result.vpuCompleted = vpu.getState() == VPU_STATE_READY;
  result.path1Completed = !path1.path1TransferActive();
  return result;
}
