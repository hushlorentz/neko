#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "gif.hpp"
#include "gif_path1.hpp"
#include "gs.hpp"
#include "vpu.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"

namespace
{
  constexpr std::uint16_t VU1_QUADWORD_COUNT = 1024;
  constexpr std::uint16_t FIXED_POINT_ONE = 16;
  constexpr std::uint16_t TRIANGLE_PRIMITIVE = 3;

  GIFQuadword gifTag(
    std::uint16_t loopCount,
    bool endOfPacket,
    GIFDataFormat format,
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
      (static_cast<std::uint64_t>(format) << 58) |
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

  GIFQuadword packedXYZ(
    std::uint16_t x,
    std::uint16_t y)
  {
    return GIFQuadword{{x, y, 0, 0}};
  }

  std::uint64_t frameValue(
    std::uint16_t basePointer,
    std::uint8_t width)
  {
    return
      basePointer |
      (static_cast<std::uint64_t>(width) << 16) |
      (static_cast<std::uint64_t>(
        GSPixelStorageMode::PSMCT32) << 24);
  }

  std::uint64_t scissorValue(
    std::uint16_t x0,
    std::uint16_t x1,
    std::uint16_t y0,
    std::uint16_t y1)
  {
    return
      x0 |
      (static_cast<std::uint64_t>(x1) << 16) |
      (static_cast<std::uint64_t>(y0) << 32) |
      (static_cast<std::uint64_t>(y1) << 48);
  }

  std::uint32_t xgkick(std::uint8_t source)
  {
    return
      VPU_XGKICK_ENCODING |
      (static_cast<std::uint32_t>(source) << VPU_FS_REG_SHIFT);
  }

  void writeTrianglePacket(
    VPU *vpu,
    std::uint16_t startAddress)
  {
    const std::uint64_t vertexDescriptors =
      GIFRegisterDescriptor::RGBAQ |
      (static_cast<std::uint64_t>(
        GIFRegisterDescriptor::XYZ2) << 4);
    const GIFQuadword packet[] = {
      gifTag(
        3,
        false,
        GIFDataFormat::Packed,
        1,
        GIFRegisterDescriptor::AD),
      adWrite(
        GSRegisterAddress::FRAME_1,
        frameValue(0, 1)),
      adWrite(
        GSRegisterAddress::SCISSOR_1,
        scissorValue(0, 7, 0, 7)),
      adWrite(
        GSRegisterAddress::XYOFFSET_1,
        0),
      gifTag(
        3,
        true,
        GIFDataFormat::Packed,
        2,
        vertexDescriptors,
        true,
        TRIANGLE_PRIMITIVE),
      packedRGBA(0x10, 0x20, 0x40, 0x80),
      packedXYZ(1 * FIXED_POINT_ONE, 1 * FIXED_POINT_ONE),
      packedRGBA(0x10, 0x20, 0x40, 0x80),
      packedXYZ(4 * FIXED_POINT_ONE, 1 * FIXED_POINT_ONE),
      packedRGBA(0x10, 0x20, 0x40, 0x80),
      packedXYZ(1 * FIXED_POINT_ONE, 4 * FIXED_POINT_ONE)
    };

    for (std::size_t index = 0;
         index < sizeof(packet) / sizeof(packet[0]);
         ++index)
    {
      vpu->writeDataQuadword(startAddress + index, packet[index]);
    }
  }
}

TEST_CASE("GIF PATH1 Transfer Tests")
{
  SECTION("PATH1 validates its decoder and VPU attachments")
  {
    REQUIRE_THROWS_WITH(
      GIFPath1Transfer(nullptr),
      "GIF PATH1 requires a non-null decoder.");

    GIFDecoder decoder;
    GIFPath1Transfer path1(&decoder);
    VPU vu0(VPUType::VU0);
    VPU vu1(VPUType::VU1);
    VPU otherVU1(VPUType::VU1);
    REQUIRE_THROWS_WITH(
      path1.attachVPU(nullptr),
      "Cannot attach a null VPU to GIF PATH1.");
    REQUIRE_THROWS_WITH(
      path1.attachVPU(&vu0),
      "GIF PATH1 requires VU1.");
    REQUIRE_THROWS_WITH(
      path1.startPath1Transfer(0),
      "GIF PATH1 requires an attached VU1.");
    path1.attachVPU(&vu1);
    REQUIRE_THROWS_WITH(
      path1.attachVPU(&otherVU1),
      "GIF PATH1 is already attached to a VU1.");
  }

  SECTION("PATH1 reads one qword per advance and wraps VU1 memory")
  {
    GS gs;
    GIFDecoder decoder;
    decoder.attachRegisterWriteHandler(&gs);
    VPU vpu(VPUType::VU1);
    GIFPath1Transfer path1(&decoder);
    path1.attachVPU(&vpu);
    vpu.writeDataQuadword(
      VU1_QUADWORD_COUNT - 1,
      gifTag(
        1,
        true,
        GIFDataFormat::Packed,
        1,
        GIFRegisterDescriptor::AD));
    vpu.writeDataQuadword(
      0,
      adWrite(GSRegisterAddress::PRIM, TRIANGLE_PRIMITIVE));

    path1.startPath1Transfer(VU1_QUADWORD_COUNT - 1);
    REQUIRE(path1.path1TransferActive());
    REQUIRE(path1.transferredQuadwordCount() == 0);

    path1.advancePath1Transfer();
    REQUIRE(path1.path1TransferActive());
    REQUIRE(path1.currentQwordAddress() == 0);
    REQUIRE(path1.transferredQuadwordCount() == 1);

    path1.advancePath1Transfer();
    REQUIRE(!path1.path1TransferActive());
    REQUIRE(path1.currentQwordAddress() == 1);
    REQUIRE(path1.transferredQuadwordCount() == 2);
    REQUIRE(gs.primitive().type == GSPrimitiveType::Triangle);
  }

  SECTION("EOP controls transfer completion across primitives")
  {
    GIFDecoder decoder;
    VPU vpu(VPUType::VU1);
    GIFPath1Transfer path1(&decoder);
    path1.attachVPU(&vpu);
    vpu.writeDataQuadword(
      5,
      gifTag(
        0,
        false,
        GIFDataFormat::Packed,
        0,
        0));
    vpu.writeDataQuadword(
      6,
      gifTag(
        0,
        true,
        GIFDataFormat::Packed,
        0,
        0));

    path1.startPath1Transfer(5);
    path1.advancePath1Transfer();
    REQUIRE(path1.path1TransferActive());
    REQUIRE(decoder.packetInProgress());

    path1.advancePath1Transfer();
    REQUIRE(!path1.path1TransferActive());
    REQUIRE(!decoder.packetInProgress());
  }

  SECTION("PATH1 cannot start inside an existing GIF packet")
  {
    GIFDecoder decoder;
    VPU vpu(VPUType::VU1);
    GIFPath1Transfer path1(&decoder);
    path1.attachVPU(&vpu);
    decoder.ingestQuadword(gifTag(
      0,
      false,
      GIFDataFormat::Packed,
      0,
      0));

    REQUIRE_THROWS_WITH(
      path1.startPath1Transfer(0),
      "GIF PATH1 must start at a GIF packet boundary.");
  }

  SECTION("Malformed packets fail without advancing memory")
  {
    GIFDecoder decoder;
    VPU vpu(VPUType::VU1);
    GIFPath1Transfer path1(&decoder);
    path1.attachVPU(&vpu);
    vpu.writeDataQuadword(
      10,
      gifTag(
        1,
        true,
        GIFDataFormat::Packed,
        1,
        GIFRegisterDescriptor::Reserved));
    path1.startPath1Transfer(10);

    REQUIRE_THROWS_WITH(
      path1.advancePath1Transfer(),
      "GIF tag uses the reserved register descriptor.");
    REQUIRE(path1.path1TransferActive());
    REQUIRE(path1.currentQwordAddress() == 10);
    REQUIRE(path1.transferredQuadwordCount() == 0);
  }
}

TEST_CASE("VU1 XGKICK Graphics Integration Tests")
{
  SECTION("XGKICK renders the same triangle as PATH2")
  {
    constexpr std::uint16_t PACKET_ADDRESS = 100;
    GS gs;
    GIFDecoder decoder;
    decoder.attachRegisterWriteHandler(&gs);
    VPU vpu(VPUType::VU1);
    GIFPath1Transfer path1(&decoder);
    path1.attachVPU(&vpu);
    std::vector<VPUTraceEvent> trace;
    vpu.setTraceCallback([&trace](const VPUTraceEvent &event) {
      trace.push_back(event);
    });
    writeTrianglePacket(&vpu, PACKET_ADDRESS);
    vpu.loadIntRegister(VPU_REGISTER_VI01, PACKET_ADDRESS);
    vpu.writeMicroInstruction(
      0,
      xgkick(VPU_REGISTER_VI01),
      VPU_NOP);
    vpu.writeMicroInstruction(
      1,
      VPU_LOWER_NOP,
      VPU_E_BIT | VPU_NOP);
    vpu.writeMicroInstruction(
      2,
      VPU_LOWER_NOP,
      VPU_NOP);

    vpu.startMicroMode();
    vpu.run(100);

    REQUIRE(vpu.getState() == VPU_STATE_READY);
    REQUIRE(!path1.path1TransferActive());
    REQUIRE(path1.transferredQuadwordCount() == 11);
    REQUIRE(gs.triangleCount() == 1);
    REQUIRE(gs.pixelWriteCount() == 6);
    std::size_t stallCount = 0;
    for (const VPUTraceEvent &event : trace)
    {
      stallCount +=
        event.type == VPUTraceEventType::PipelineStall;
    }
    REQUIRE(stallCount == 10);
    REQUIRE(
      gs.framebufferHash(0, 8, 8) ==
      UINT64_C(0x108089dcd964d365));
  }
}
