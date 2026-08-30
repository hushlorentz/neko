#include <array>
#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "gif_path1.hpp"
#include "gs.hpp"
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
  constexpr std::uint16_t PROGRAM_INSTRUCTION_COUNT = 15;
  constexpr std::uint16_t PACKET_QUADWORD_COUNT = 11;
  constexpr std::uint16_t VERTEX_QUADWORD_COUNT = 3;
  constexpr std::int16_t FIRST_VERTEX_PACKET_OFFSET = 6;
  constexpr std::int16_t SECOND_VERTEX_PACKET_OFFSET = 8;
  constexpr std::int16_t THIRD_VERTEX_PACKET_OFFSET = 10;
  constexpr std::uint16_t VIF_UNPACK_CYCLE = 0x0404;
  constexpr std::uint16_t VU_MEMORY_OFFSET_MASK = 0x07ff;
  constexpr std::uint16_t FRAME_WIDTH_PAGES = 1;
  constexpr std::uint16_t FRAME_MAX_COORDINATE = 7;
  constexpr std::uint16_t FRAME_DIMENSION = 8;
  constexpr std::uint8_t COLOR_RED = 0x10;
  constexpr std::uint8_t COLOR_GREEN = 0x20;
  constexpr std::uint8_t COLOR_BLUE = 0x40;
  constexpr std::uint8_t COLOR_ALPHA = 0x80;
  constexpr std::uint64_t EXPECTED_PIXEL_WRITES = 6;
  constexpr std::uint32_t FLOAT_ONE_BITS = 0x3f800000;
  constexpr std::uint32_t FLOAT_FOUR_BITS = 0x40800000;
  constexpr std::uint32_t FIXED_POINT_ONE = 16;
  constexpr std::uint32_t FIXED_POINT_FOUR = 64;
  constexpr std::uint32_t WORDS_PER_QUADWORD = 4;
  constexpr std::uint64_t EXPECTED_FRAMEBUFFER_HASH =
    UINT64_C(0x108089dcd964d365);

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

  GIFQuadword packedRGBA(
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha)
  {
    return GIFQuadword{{red, green, blue, alpha}};
  }

  std::uint64_t frameValue()
  {
    return
      static_cast<std::uint64_t>(FRAME_WIDTH_PAGES) << 16 |
      static_cast<std::uint64_t>(
        GSPixelStorageMode::PSMCT32) << 24;
  }

  std::uint64_t scissorValue()
  {
    return
      (static_cast<std::uint64_t>(FRAME_MAX_COORDINATE) << 16) |
      (static_cast<std::uint64_t>(FRAME_MAX_COORDINATE) << 48);
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

  std::uint32_t memoryInstruction(
    std::uint32_t encoding,
    std::uint8_t vectorRegister,
    std::uint8_t addressRegister,
    std::int16_t offset)
  {
    return
      encoding |
      (static_cast<std::uint32_t>(
        vpuFieldMaskToEncoding(FP_REGISTER_ALL_FIELDS)) << 21) |
      (static_cast<std::uint32_t>(vectorRegister) << 16) |
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

  void ingestQuadword(
    VIF *vif,
    const GIFQuadword &quadword)
  {
    for (std::uint32_t word : quadword)
    {
      const VIFStreamWord result = vif->ingestWord(word);
      REQUIRE(!result.stalled);
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
    vif->ingestWord(vifCode(
      command,
      static_cast<std::uint8_t>(quadwords.size()),
      address));
    for (const GIFQuadword &quadword : quadwords)
    {
      ingestQuadword(vif, quadword);
    }
  }

  void uploadProgram(
    VIF *vif,
    const std::array<std::array<std::uint32_t, 2>,
      PROGRAM_INSTRUCTION_COUNT> &program)
  {
    vif->ingestWord(vifCode(VIFCommandEncoding::NOP));
    vif->ingestWord(vifCode(
      VIFCommandEncoding::MPG,
      PROGRAM_INSTRUCTION_COUNT));
    for (const auto &instruction : program)
    {
      vif->ingestWord(instruction[0]);
      vif->ingestWord(instruction[1]);
    }
  }
}

TEST_CASE("VIF to VU1 to GS Graphics Integration Tests")
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

  const std::array<std::array<std::uint32_t, 2>,
    PROGRAM_INSTRUCTION_COUNT> program = {{
      {{iaddiu(VPU_REGISTER_VI01, VPU_REGISTER_VI00, VERTEX_ADDRESS),
        VPU_NOP}},
      {{iaddiu(VPU_REGISTER_VI02, VPU_REGISTER_VI00, PACKET_ADDRESS),
        VPU_NOP}},
      {{memoryInstruction(
          VPU_LQ_ENCODING,
          VPU_REGISTER_VF01,
          VPU_REGISTER_VI01,
          0),
        VPU_NOP}},
      {{VPU_LOWER_NOP, ftoi4(VPU_REGISTER_VF02, VPU_REGISTER_VF01)}},
      {{sq(
          VPU_REGISTER_VF02,
          VPU_REGISTER_VI02,
          FIRST_VERTEX_PACKET_OFFSET),
        VPU_NOP}},
      {{memoryInstruction(
          VPU_LQ_ENCODING,
          VPU_REGISTER_VF03,
          VPU_REGISTER_VI01,
          1),
        VPU_NOP}},
      {{VPU_LOWER_NOP, ftoi4(VPU_REGISTER_VF04, VPU_REGISTER_VF03)}},
      {{sq(
          VPU_REGISTER_VF04,
          VPU_REGISTER_VI02,
          SECOND_VERTEX_PACKET_OFFSET),
        VPU_NOP}},
      {{memoryInstruction(
          VPU_LQ_ENCODING,
          VPU_REGISTER_VF05,
          VPU_REGISTER_VI01,
          2),
        VPU_NOP}},
      {{VPU_LOWER_NOP, ftoi4(VPU_REGISTER_VF06, VPU_REGISTER_VF05)}},
      {{sq(
          VPU_REGISTER_VF06,
          VPU_REGISTER_VI02,
          THIRD_VERTEX_PACKET_OFFSET),
        VPU_NOP}},
      {{iaddiu(VPU_REGISTER_VI03, VPU_REGISTER_VI00, PACKET_ADDRESS),
        VPU_NOP}},
      {{xgkick(VPU_REGISTER_VI03), VPU_NOP}},
      {{VPU_LOWER_NOP, VPU_E_BIT | VPU_NOP}},
      {{VPU_LOWER_NOP, VPU_NOP}}
    }};
  uploadProgram(&vif, program);

  vif.ingestWord(vifCode(
    VIFCommandEncoding::STCYCL,
    0,
    VIF_UNPACK_CYCLE));
  const std::uint64_t vertexDescriptors =
    GIFRegisterDescriptor::RGBAQ |
    (static_cast<std::uint64_t>(
      GIFRegisterDescriptor::XYZ2) << 4);
  const std::vector<GIFQuadword> packetTemplate = {
    gifTag(3, false, 1, GIFRegisterDescriptor::AD),
    adWrite(GSRegisterAddress::FRAME_1, frameValue()),
    adWrite(GSRegisterAddress::SCISSOR_1, scissorValue()),
    adWrite(GSRegisterAddress::XYOFFSET_1, 0),
    gifTag(
      3,
      true,
      2,
      vertexDescriptors,
      true,
      TRIANGLE_PRIMITIVE),
    packedRGBA(COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_ALPHA),
    GIFQuadword{{0, 0, 0, 0}},
    packedRGBA(COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_ALPHA),
    GIFQuadword{{0, 0, 0, 0}},
    packedRGBA(COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_ALPHA),
    GIFQuadword{{0, 0, 0, 0}}
  };
  REQUIRE(packetTemplate.size() == PACKET_QUADWORD_COUNT);
  unpackQuadwords(&vif, PACKET_ADDRESS, packetTemplate);

  const std::vector<GIFQuadword> vertices = {
    GIFQuadword{{FLOAT_ONE_BITS, FLOAT_ONE_BITS, 0, 0}},
    GIFQuadword{{FLOAT_FOUR_BITS, FLOAT_ONE_BITS, 0, 0}},
    GIFQuadword{{FLOAT_ONE_BITS, FLOAT_FOUR_BITS, 0, 0}}
  };
  REQUIRE(vertices.size() == VERTEX_QUADWORD_COUNT);
  unpackQuadwords(&vif, VERTEX_ADDRESS, vertices);

  const VIFStreamWord start =
    vif.ingestWord(vifCode(VIFCommandEncoding::MSCAL));
  REQUIRE(!start.stalled);
  vpu.run(200);

  REQUIRE(vpu.getState() == VPU_STATE_READY);
  REQUIRE(!path1.path1TransferActive());
  REQUIRE(path1.transferredQuadwordCount() == PACKET_QUADWORD_COUNT);
  REQUIRE(
    vpu.readDataQuadword(
      PACKET_ADDRESS + FIRST_VERTEX_PACKET_OFFSET) ==
    GIFQuadword{{FIXED_POINT_ONE, FIXED_POINT_ONE, 0, 0}});
  REQUIRE(
    vpu.readDataQuadword(
      PACKET_ADDRESS + SECOND_VERTEX_PACKET_OFFSET) ==
    GIFQuadword{{FIXED_POINT_FOUR, FIXED_POINT_ONE, 0, 0}});
  REQUIRE(
    vpu.readDataQuadword(
      PACKET_ADDRESS + THIRD_VERTEX_PACKET_OFFSET) ==
    GIFQuadword{{FIXED_POINT_ONE, FIXED_POINT_FOUR, 0, 0}});
  REQUIRE(gs.triangleCount() == 1);
  REQUIRE(gs.pixelWriteCount() == EXPECTED_PIXEL_WRITES);
  REQUIRE(
    gs.framebufferHash(0, FRAME_DIMENSION, FRAME_DIMENSION) ==
    EXPECTED_FRAMEBUFFER_HASH);
}
