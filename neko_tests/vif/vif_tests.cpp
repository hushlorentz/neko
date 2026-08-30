#include <array>
#include <stdexcept>

#include "catch.hpp"
#include "vif.hpp"
#include "vpu.hpp"

namespace
{
  constexpr std::uint16_t MAX_ENCODED_COUNT = 256;
  constexpr std::uint32_t DIRECT_ZERO_QUADWORD_COUNT = 65536;
  constexpr std::size_t MICRO_INSTRUCTION_SIZE_BYTES = 8;
  constexpr std::uint32_t WORDS_PER_QUADWORD = 4;
  constexpr std::uint8_t UNDEFINED_COMMAND = 0x08;

  std::uint32_t vifCode(
    std::uint8_t command,
    std::uint8_t count = 0,
    std::uint16_t immediate = 0,
    bool interrupt = false)
  {
    return
      (static_cast<std::uint32_t>(
        command |
        (interrupt ? VIFCommandEncoding::Interrupt : 0)) << 24) |
      (static_cast<std::uint32_t>(count) << 16) |
      immediate;
  }
}

TEST_CASE("VIF Command Decoder Tests")
{
  SECTION("Every non-UNPACK command decodes to its typed operation")
  {
    const std::array<std::pair<std::uint8_t, VIFCommandKind>, 20> commands = {{
      {VIFCommandEncoding::NOP, VIFCommandKind::NOP},
      {VIFCommandEncoding::STCYCL, VIFCommandKind::STCYCL},
      {VIFCommandEncoding::OFFSET, VIFCommandKind::OFFSET},
      {VIFCommandEncoding::BASE, VIFCommandKind::BASE},
      {VIFCommandEncoding::ITOP, VIFCommandKind::ITOP},
      {VIFCommandEncoding::STMOD, VIFCommandKind::STMOD},
      {VIFCommandEncoding::MSKPATH3, VIFCommandKind::MSKPATH3},
      {VIFCommandEncoding::MARK, VIFCommandKind::MARK},
      {VIFCommandEncoding::FLUSHE, VIFCommandKind::FLUSHE},
      {VIFCommandEncoding::FLUSH, VIFCommandKind::FLUSH},
      {VIFCommandEncoding::FLUSHA, VIFCommandKind::FLUSHA},
      {VIFCommandEncoding::MSCAL, VIFCommandKind::MSCAL},
      {VIFCommandEncoding::MSCALF, VIFCommandKind::MSCALF},
      {VIFCommandEncoding::MSCNT, VIFCommandKind::MSCNT},
      {VIFCommandEncoding::STMASK, VIFCommandKind::STMASK},
      {VIFCommandEncoding::STROW, VIFCommandKind::STROW},
      {VIFCommandEncoding::STCOL, VIFCommandKind::STCOL},
      {VIFCommandEncoding::MPG, VIFCommandKind::MPG},
      {VIFCommandEncoding::DIRECT, VIFCommandKind::DIRECT},
      {VIFCommandEncoding::DIRECTHL, VIFCommandKind::DIRECTHL}
    }};

    for (const auto &contract : commands)
    {
      const VIFCommand command = decodeVIFCommand(
        vifCode(contract.first, 7, 0x1234, true),
        VIFType::VIF1);

      REQUIRE(command.kind == contract.second);
      REQUIRE(command.command == contract.first);
      REQUIRE(command.encodedCount == 7);
      REQUIRE(command.count == 7);
      REQUIRE(command.immediate == 0x1234);
      REQUIRE(command.interrupt);
    }
  }

  SECTION("Zero NUM represents 256 entries")
  {
    const VIFCommand command = decodeVIFCommand(
      vifCode(VIFCommandEncoding::MPG),
      VIFType::VIF0);

    REQUIRE(command.encodedCount == 0);
    REQUIRE(command.count == MAX_ENCODED_COUNT);
  }

  SECTION("All thirteen defined UNPACK formats decode")
  {
    const std::array<std::pair<std::uint8_t, VIFUnpackFormat>, 13> formats = {{
      {VIFUnpackEncoding::S_32, VIFUnpackFormat::S_32},
      {VIFUnpackEncoding::S_16, VIFUnpackFormat::S_16},
      {VIFUnpackEncoding::S_8, VIFUnpackFormat::S_8},
      {VIFUnpackEncoding::V2_32, VIFUnpackFormat::V2_32},
      {VIFUnpackEncoding::V2_16, VIFUnpackFormat::V2_16},
      {VIFUnpackEncoding::V2_8, VIFUnpackFormat::V2_8},
      {VIFUnpackEncoding::V3_32, VIFUnpackFormat::V3_32},
      {VIFUnpackEncoding::V3_16, VIFUnpackFormat::V3_16},
      {VIFUnpackEncoding::V3_8, VIFUnpackFormat::V3_8},
      {VIFUnpackEncoding::V4_32, VIFUnpackFormat::V4_32},
      {VIFUnpackEncoding::V4_16, VIFUnpackFormat::V4_16},
      {VIFUnpackEncoding::V4_8, VIFUnpackFormat::V4_8},
      {VIFUnpackEncoding::V4_5, VIFUnpackFormat::V4_5}
    }};

    for (const auto &contract : formats)
    {
      const VIFCommand command = decodeVIFCommand(
        vifCode(
          VIFCommandEncoding::UNPACK |
            VIFCommandEncoding::UNPACKMask |
            contract.first,
          9,
          VIFImmediateEncoding::UNPACKAddTOPS |
            VIFImmediateEncoding::UNPACKUnsigned |
            0x02aa,
          true),
        VIFType::VIF1);

      REQUIRE(command.kind == VIFCommandKind::UNPACK);
      REQUIRE(command.unpackFormat == contract.second);
      REQUIRE(command.masked);
      REQUIRE(command.unsignedData);
      REQUIRE(command.addTops);
      REQUIRE(
        command.address ==
        (0x02aa & VIFImmediateEncoding::AddressMask));
      REQUIRE(command.count == 9);
      REQUIRE(command.interrupt);
    }
  }

  SECTION("VIF0 ignores the VIF1-only UNPACK TOPS flag")
  {
    const VIFCommand command = decodeVIFCommand(
      vifCode(
        VIFCommandEncoding::UNPACK |
          VIFUnpackEncoding::V4_32,
        1,
        VIFImmediateEncoding::UNPACKAddTOPS),
      VIFType::VIF0);

    REQUIRE(!command.addTops);
  }

  SECTION("Undefined commands and UNPACK formats are rejected")
  {
    REQUIRE_THROWS_WITH(
      decodeVIFCommand(vifCode(UNDEFINED_COMMAND), VIFType::VIF1),
      "Unsupported VIF command.");

    for (const std::uint8_t format : {0x3, 0x7, 0xb})
    {
      REQUIRE_THROWS_WITH(
        decodeVIFCommand(
          vifCode(VIFCommandEncoding::UNPACK | format),
          VIFType::VIF1),
        "Unsupported VIF UNPACK format.");
    }
  }

  SECTION("VIF1-only commands are rejected on VIF0")
  {
    for (const std::uint8_t command : {
      VIFCommandEncoding::OFFSET,
      VIFCommandEncoding::BASE,
      VIFCommandEncoding::MSKPATH3,
      VIFCommandEncoding::FLUSH,
      VIFCommandEncoding::FLUSHA,
      VIFCommandEncoding::MSCALF,
      VIFCommandEncoding::DIRECT,
      VIFCommandEncoding::DIRECTHL})
    {
      REQUIRE_THROWS_WITH(
        decodeVIFCommand(vifCode(command), VIFType::VIF0),
        "VIF command is only supported on VIF1.");
    }
  }

  SECTION("Undefined addition mode is rejected")
  {
    REQUIRE_THROWS_WITH(
      decodeVIFCommand(
        vifCode(VIFCommandEncoding::STMOD, 0, 3),
        VIFType::VIF1),
      "Unsupported VIF addition mode.");
  }
}

TEST_CASE("VIF State Tests")
{
  SECTION("VIF0 and VIF1 start with independent reset state")
  {
    VIF vif0(VIFType::VIF0);
    VIF vif1(VIFType::VIF1);

    REQUIRE(vif0.unitType() == VIFType::VIF0);
    REQUIRE(vif1.unitType() == VIFType::VIF1);
    REQUIRE(vif0.cycle() == 0);
    REQUIRE(vif1.cycle() == 0);
    REQUIRE(vif0.lastCode() == 0);
    REQUIRE(vif1.lastCode() == 0);
  }

  SECTION("Single-word configuration commands update their registers")
  {
    VIF vif(VIFType::VIF1);
    const std::uint32_t cycleCode = vifCode(
      VIFCommandEncoding::STCYCL,
      0xcc,
      0x0804);
    vif.processCode(cycleCode);
    vif.processCode(vifCode(VIFCommandEncoding::BASE, 0, 0xffff));
    vif.processCode(vifCode(VIFCommandEncoding::OFFSET, 0, 0x0555));
    vif.processCode(vifCode(VIFCommandEncoding::ITOP, 0, 0xf321));
    vif.processCode(vifCode(VIFCommandEncoding::STMOD, 0, 2));
    vif.processCode(vifCode(VIFCommandEncoding::MSKPATH3, 0, 0x8000));
    vif.processCode(vifCode(VIFCommandEncoding::MARK, 0, 0xbeef));

    REQUIRE(vif.cycle() == 0x0804);
    REQUIRE(vif.cycleLength() == 4);
    REQUIRE(vif.writeLength() == 8);
    REQUIRE(
      vif.base() ==
      (0xffff & VIFImmediateEncoding::AddressMask));
    REQUIRE(
      vif.offset() ==
      (0x0555 & VIFImmediateEncoding::AddressMask));
    REQUIRE(vif.tops() == vif.base());
    REQUIRE(!vif.doubleBufferFlag());
    REQUIRE(
      vif.itops() ==
      (0xf321 & VIFImmediateEncoding::AddressMask));
    REQUIRE(vif.mode() == 2);
    REQUIRE(vif.path3Masked());
    REQUIRE(vif.mark() == 0xbeef);
    REQUIRE(vif.markDetected());
    REQUIRE(
      vif.lastCode() ==
      vifCode(VIFCommandEncoding::MARK, 0, 0xbeef));
  }

  SECTION("NOP changes only the most recently processed code")
  {
    VIF vif(VIFType::VIF0);
    vif.processCode(vifCode(VIFCommandEncoding::STCYCL, 0, 0x0404));
    const std::uint32_t nop = vifCode(
      VIFCommandEncoding::NOP,
      9,
      0xabcd,
      true);

    const VIFCommand decoded = vif.processCode(nop);

    REQUIRE(decoded.kind == VIFCommandKind::NOP);
    REQUIRE(vif.cycle() == 0x0404);
    REQUIRE(vif.lastCode() == nop);
  }

  SECTION("Commands owned by later graphics steps are not silently executed")
  {
    VIF vif(VIFType::VIF1);

    REQUIRE_THROWS_WITH(
      vif.processCode(vifCode(VIFCommandEncoding::MPG, 1, 0)),
      "VIF command execution requires its owning subsystem.");
    REQUIRE(
      vif.lastCode() ==
      vifCode(VIFCommandEncoding::MPG, 1, 0));
  }
}

TEST_CASE("VIF Packet Stream Tests")
{
  SECTION("Single-word commands remain packed in one continuous stream")
  {
    VIF vif(VIFType::VIF1);

    const VIFStreamWord cycle = vif.ingestWord(
      vifCode(VIFCommandEncoding::STCYCL, 0, 0x0402));
    const VIFStreamWord base = vif.ingestWord(
      vifCode(VIFCommandEncoding::BASE, 0, 0x0123));

    REQUIRE(cycle.kind == VIFStreamWordKind::Command);
    REQUIRE(cycle.command.kind == VIFCommandKind::STCYCL);
    REQUIRE(cycle.payloadWordCount == 0);
    REQUIRE(cycle.packetComplete);
    REQUIRE(base.kind == VIFStreamWordKind::Command);
    REQUIRE(base.command.kind == VIFCommandKind::BASE);
    REQUIRE(base.packetComplete);
    REQUIRE(vif.cycle() == 0x0402);
    REQUIRE(vif.base() == 0x0123);
    REQUIRE(vif.wordsIngested() == 2);
  }

  SECTION("Payload words are not decoded as commands across fragments")
  {
    VIF vif(VIFType::VIF1);
    const std::uint32_t rowCode =
      vifCode(VIFCommandEncoding::STROW);

    const VIFStreamWord command = vif.ingestWord(rowCode);

    REQUIRE(command.kind == VIFStreamWordKind::Command);
    REQUIRE(command.command.kind == VIFCommandKind::STROW);
    REQUIRE(command.payloadWordCount == 4);
    REQUIRE(!command.packetComplete);
    REQUIRE(vif.awaitingPayload());
    REQUIRE(vif.payloadWordsRemaining() == 4);
    REQUIRE(vif.lastCode() == rowCode);

    for (std::uint32_t index = 0; index < 3; ++index)
    {
      const VIFStreamWord payload = vif.ingestWord(0x80000000 | index);
      REQUIRE(payload.kind == VIFStreamWordKind::Payload);
      REQUIRE(payload.command.kind == VIFCommandKind::STROW);
      REQUIRE(payload.payloadIndex == index);
      REQUIRE(payload.payloadWordCount == 4);
      REQUIRE(!payload.packetComplete);
    }

    REQUIRE(vif.awaitingPayload());
    REQUIRE(vif.payloadWordsRemaining() == 1);

    const VIFStreamWord finalPayload = vif.ingestWord(
      vifCode(UNDEFINED_COMMAND));
    REQUIRE(finalPayload.kind == VIFStreamWordKind::Payload);
    REQUIRE(finalPayload.payloadIndex == 3);
    REQUIRE(finalPayload.packetComplete);
    REQUIRE(!vif.awaitingPayload());

    const VIFStreamWord nextCommand = vif.ingestWord(
      vifCode(VIFCommandEncoding::NOP));
    REQUIRE(nextCommand.kind == VIFStreamWordKind::Command);
    REQUIRE(nextCommand.command.kind == VIFCommandKind::NOP);
  }

  SECTION("MPG payloads require 64-bit alignment and track partial input")
  {
    VIF misaligned(VIFType::VIF0);
    REQUIRE_THROWS_WITH(
      misaligned.ingestWord(vifCode(VIFCommandEncoding::MPG, 2)),
      "VIF MPG payload is not 64-bit aligned.");
    REQUIRE(misaligned.wordsIngested() == 0);
    REQUIRE(!misaligned.awaitingPayload());

    VIF aligned(VIFType::VIF0);
    VPU vpu;
    aligned.attachVPU(&vpu);
    aligned.ingestWord(vifCode(VIFCommandEncoding::NOP));
    const VIFStreamWord mpg = aligned.ingestWord(
      vifCode(VIFCommandEncoding::MPG, 2, 7));

    REQUIRE(mpg.payloadWordCount == 4);
    REQUIRE(aligned.payloadWordsRemaining() == 4);

    aligned.ingestWord(0x11111111);
    aligned.ingestWord(0x22222222);
    REQUIRE(aligned.payloadWordsRemaining() == 2);

    aligned.ingestWord(0x33333333);
    const VIFStreamWord finalPayload =
      aligned.ingestWord(0x44444444);
    REQUIRE(finalPayload.packetComplete);
    REQUIRE(aligned.wordsIngested() == 6);
  }

  SECTION("DIRECT payloads require 128-bit alignment")
  {
    VIF vif(VIFType::VIF1);
    vif.ingestWord(vifCode(VIFCommandEncoding::NOP));

    REQUIRE_THROWS_WITH(
      vif.ingestWord(vifCode(VIFCommandEncoding::DIRECT, 0, 1)),
      "VIF DIRECT payload is not 128-bit aligned.");
    REQUIRE(vif.wordsIngested() == 1);

    vif.ingestWord(vifCode(VIFCommandEncoding::NOP));
    vif.ingestWord(vifCode(VIFCommandEncoding::NOP));
    GIFDecoder decoder;
    vif.attachGIFDecoder(&decoder);
    const VIFStreamWord direct = vif.ingestWord(
      vifCode(VIFCommandEncoding::DIRECT, 0, 1));

    REQUIRE(direct.payloadWordCount == 4);
    REQUIRE(vif.payloadWordsRemaining() == 4);
  }

  SECTION("Zero DIRECT size represents 65536 quadwords")
  {
    VIF vif(VIFType::VIF1);
    GIFDecoder decoder;
    vif.attachGIFDecoder(&decoder);
    for (int index = 0; index < 3; ++index)
    {
      vif.ingestWord(vifCode(VIFCommandEncoding::NOP));
    }

    const VIFStreamWord direct = vif.ingestWord(
      vifCode(VIFCommandEncoding::DIRECT));

    REQUIRE(
      direct.payloadWordCount ==
      DIRECT_ZERO_QUADWORD_COUNT * WORDS_PER_QUADWORD);
    REQUIRE(
      vif.payloadWordsRemaining() ==
      DIRECT_ZERO_QUADWORD_COUNT * WORDS_PER_QUADWORD);
  }

  SECTION("UNPACK payload size follows format and skipping mode")
  {
    VPU vpu(VPUType::VU1);
    VIF vif(VIFType::VIF1);
    vif.attachVPU(&vpu);
    vif.ingestWord(vifCode(VIFCommandEncoding::STCYCL, 0, 0x0204));

    const VIFStreamWord unpack = vif.ingestWord(vifCode(
      VIFCommandEncoding::UNPACK | VIFUnpackEncoding::V2_16,
      3));

    REQUIRE(unpack.payloadWordCount == 3);
    REQUIRE(vif.payloadWordsRemaining() == 3);
  }

  SECTION("UNPACK payload size follows filling mode and word padding")
  {
    VPU vpu(VPUType::VU1);
    VIF vif(VIFType::VIF1);
    vif.attachVPU(&vpu);
    vif.ingestWord(vifCode(VIFCommandEncoding::STCYCL, 0, 0x0402));

    const VIFStreamWord unpack = vif.ingestWord(vifCode(
      VIFCommandEncoding::UNPACK | VIFUnpackEncoding::V3_16,
      9));

    REQUIRE(unpack.payloadWordCount == 8);
    REQUIRE(vif.payloadWordsRemaining() == 8);
  }

  SECTION("Filling mode can produce output without consuming payload")
  {
    VPU vpu(VPUType::VU1);
    VIF vif(VIFType::VIF1);
    vif.attachVPU(&vpu);
    vif.ingestWord(vifCode(VIFCommandEncoding::STCYCL, 0, 0x0400));

    const VIFStreamWord unpack = vif.ingestWord(vifCode(
      VIFCommandEncoding::UNPACK | VIFUnpackEncoding::V4_32,
      8));

    REQUIRE(unpack.payloadWordCount == 0);
    REQUIRE(unpack.packetComplete);
    REQUIRE(!vif.awaitingPayload());
  }

  SECTION("Direct code processing cannot bypass an active payload")
  {
    VIF vif(VIFType::VIF1);
    vif.ingestWord(vifCode(VIFCommandEncoding::STMASK));

    REQUIRE_THROWS_WITH(
      vif.processCode(vifCode(VIFCommandEncoding::NOP)),
      "Cannot process a VIFcode while a payload is in progress.");
  }
}

TEST_CASE("VIF MPG Upload Tests")
{
  SECTION("VIF0 uploads fragmented lower-upper instruction pairs")
  {
    VPU vpu(VPUType::VU0);
    VIF vif(VIFType::VIF0);
    vif.attachVPU(&vpu);
    vif.ingestWord(vifCode(VIFCommandEncoding::NOP));
    vif.ingestWord(vifCode(VIFCommandEncoding::MPG, 2, 7));

    vif.ingestWord(0x11223344);
    REQUIRE(vpu.readMicroInstruction(7) == 0);
    REQUIRE(vif.payloadWordsRemaining() == 3);

    vif.ingestWord(0xaabbccdd);
    REQUIRE(
      vpu.readMicroInstruction(7) ==
      UINT64_C(0xaabbccdd11223344));

    vif.ingestWord(0x55667788);
    const VIFStreamWord finalWord = vif.ingestWord(0x12345678);

    REQUIRE(
      vpu.readMicroInstruction(8) ==
      UINT64_C(0x1234567855667788));
    REQUIRE(finalWord.packetComplete);
    REQUIRE(!vif.awaitingPayload());
  }

  SECTION("VIF1 uploads to its larger MicroMem")
  {
    VPU vpu(VPUType::VU1);
    VIF vif(VIFType::VIF1);
    vif.attachVPU(&vpu);
    vif.ingestWord(vifCode(VIFCommandEncoding::NOP));
    vif.ingestWord(vifCode(VIFCommandEncoding::MPG, 1, 1024));
    vif.ingestWord(0x89abcdef);
    vif.ingestWord(0x01234567);

    REQUIRE(
      vpu.readMicroInstruction(1024) ==
      UINT64_C(0x0123456789abcdef));
  }

  SECTION("MPG validates attachment and unit correspondence")
  {
    VIF vif0(VIFType::VIF0);
    VPU vu0(VPUType::VU0);
    VPU vu1(VPUType::VU1);

    REQUIRE_THROWS_WITH(
      vif0.attachVPU(nullptr),
      "Cannot attach a null VPU.");
    REQUIRE_THROWS_WITH(
      vif0.attachVPU(&vu1),
      "VIF and VPU unit types must match.");

    vif0.ingestWord(vifCode(VIFCommandEncoding::NOP));
    REQUIRE_THROWS_WITH(
      vif0.ingestWord(vifCode(VIFCommandEncoding::MPG, 1)),
      "VIF MPG requires an attached VPU.");
    REQUIRE(vif0.wordsIngested() == 1);

    REQUIRE_NOTHROW(vif0.attachVPU(&vu0));
  }

  SECTION("MPG rejects the complete transfer before an overflow write")
  {
    VPU vpu(VPUType::VU0);
    VIF vif(VIFType::VIF0);
    vif.attachVPU(&vpu);
    vif.ingestWord(vifCode(VIFCommandEncoding::NOP));

    const std::uint16_t finalInstruction =
      vpu.microMemorySize() / MICRO_INSTRUCTION_SIZE_BYTES - 1;

    vif.ingestWord(vifCode(
      VIFCommandEncoding::MPG,
      1,
      finalInstruction));
    vif.ingestWord(0x01234567);
    vif.ingestWord(0x89abcdef);
    REQUIRE(
      vpu.readMicroInstruction(finalInstruction) ==
      UINT64_C(0x89abcdef01234567));

    vif.ingestWord(vifCode(VIFCommandEncoding::NOP));
    REQUIRE_THROWS_WITH(
      vif.ingestWord(vifCode(
        VIFCommandEncoding::MPG,
        2,
        finalInstruction)),
      "VIF MPG transfer exceeds VU micro memory.");
    REQUIRE(
      vpu.readMicroInstruction(finalInstruction) ==
      UINT64_C(0x89abcdef01234567));
    REQUIRE(!vif.awaitingPayload());
  }

  SECTION("MPG zero NUM uploads 256 instructions")
  {
    VPU vpu(VPUType::VU1);
    VIF vif(VIFType::VIF1);
    vif.attachVPU(&vpu);
    vif.ingestWord(vifCode(VIFCommandEncoding::NOP));
    vif.ingestWord(vifCode(VIFCommandEncoding::MPG, 0, 32));

    for (std::uint32_t instruction = 0;
         instruction < MAX_ENCODED_COUNT;
         ++instruction)
    {
      vif.ingestWord(instruction);
      vif.ingestWord(0x80000000 | instruction);
    }

    REQUIRE(
      vpu.readMicroInstruction(32) ==
      UINT64_C(0x8000000000000000));
    REQUIRE(
      vpu.readMicroInstruction(32 + MAX_ENCODED_COUNT - 1) ==
      UINT64_C(0x800000ff000000ff));
    REQUIRE(!vif.awaitingPayload());
  }
}
