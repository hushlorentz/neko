#include <cstdint>

#include "catch.hpp"
#include "gif_path_arbiter.hpp"
#include "gif_path1.hpp"
#include "vif.hpp"
#include "vpu.hpp"
#include "vpu_opcodes.hpp"

namespace
{
  constexpr std::uint32_t WORDS_PER_QUADWORD = 4;

  std::uint32_t vifCode(
    std::uint8_t command,
    std::uint16_t immediate = 0,
    bool interrupt = false)
  {
    return
      (static_cast<std::uint32_t>(
        command |
        (interrupt ? VIFCommandEncoding::Interrupt : 0)) << 24) |
      immediate;
  }

  GIFQuadword gifTag(bool endOfPacket)
  {
    return GIFQuadword{{
      static_cast<std::uint32_t>(endOfPacket) << 15,
      0,
      0,
      0
    }};
  }

  void alignDirectCommand(VIF *vif)
  {
    while ((vif->wordsIngested() + 1) %
           WORDS_PER_QUADWORD != 0)
    {
      vif->ingestWord(vifCode(VIFCommandEncoding::NOP));
    }
  }

  void writeTerminatingProgram(VPU *vpu, std::size_t index)
  {
    vpu->writeMicroInstruction(
      index,
      VPU_LOWER_NOP,
      VPU_E_BIT | VPU_NOP);
    vpu->writeMicroInstruction(
      index + 1,
      VPU_LOWER_NOP,
      VPU_NOP);
  }
}

TEST_CASE("VIF GIF Arbitration Tests")
{
  SECTION("A blocked PATH2 qword is retried without consuming VIF state")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    VIF vif(VIFType::VIF1);
    vif.attachGIFPathArbiter(&arbiter);
    REQUIRE(arbiter.requestPath(GIFPath::Path1));
    alignDirectCommand(&vif);
    vif.ingestWord(vifCode(VIFCommandEncoding::DIRECT, 1));
    vif.ingestWord(gifTag(true)[0]);
    vif.ingestWord(gifTag(true)[1]);
    vif.ingestWord(gifTag(true)[2]);
    const std::uint64_t wordsBeforeStall = vif.wordsIngested();

    const VIFStreamWord stalled =
      vif.ingestWord(gifTag(true)[3]);

    REQUIRE(stalled.stalled);
    REQUIRE(!stalled.gifQuadwordDecoded);
    REQUIRE(vif.wordsIngested() == wordsBeforeStall);
    REQUIRE(vif.payloadWordsRemaining() == 1);

    arbiter.transferQuadword(
      GIFPath::Path1,
      gifTag(true));
    const VIFStreamWord accepted =
      vif.ingestWord(gifTag(true)[3]);

    REQUIRE(!accepted.stalled);
    REQUIRE(accepted.gifQuadwordDecoded);
    REQUIRE(accepted.gifResult.packetComplete);
    REQUIRE(accepted.packetComplete);
    REQUIRE(!vif.awaitingPayload());
  }
}

TEST_CASE("VIF Synchronization Tests")
{
  SECTION("MSKPATH3 controls PATH3 selection in the shared arbiter")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    VIF vif(VIFType::VIF1);
    vif.attachGIFPathArbiter(&arbiter);

    vif.ingestWord(vifCode(
      VIFCommandEncoding::MSKPATH3,
      VIFImmediateEncoding::MSKPATH3Mask));
    REQUIRE(vif.path3Masked());
    REQUIRE(arbiter.path3MaskedByVIF());
    REQUIRE(!arbiter.requestPath(GIFPath::Path3));

    vif.ingestWord(vifCode(VIFCommandEncoding::MSKPATH3));
    REQUIRE(!vif.path3Masked());
    REQUIRE(arbiter.activePath() == GIFPath::Path3);
  }

  SECTION("FLUSHE retries after the VU microprogram ends")
  {
    VPU vpu(VPUType::VU1);
    VIF vif(VIFType::VIF1);
    vif.attachVPU(&vpu);
    writeTerminatingProgram(&vpu, 0);
    vif.ingestWord(vifCode(VIFCommandEncoding::MSCAL));
    const std::uint64_t wordsBeforeStall = vif.wordsIngested();

    const VIFStreamWord stalled =
      vif.ingestWord(vifCode(VIFCommandEncoding::FLUSHE));

    REQUIRE(stalled.stalled);
    REQUIRE(vif.wordsIngested() == wordsBeforeStall);
    vpu.run(32);
    const VIFStreamWord completed =
      vif.ingestWord(vifCode(VIFCommandEncoding::FLUSHE));
    REQUIRE(!completed.stalled);
    REQUIRE(completed.packetComplete);
  }

  SECTION("FLUSH observes a PATH1 transfer through the shared arbiter")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    VPU vpu(VPUType::VU1);
    GIFPath1Transfer path1(arbiter);
    VIF vif(VIFType::VIF1);
    path1.attachVPU(&vpu);
    vif.attachVPU(&vpu);
    vif.attachGIFPathArbiter(&arbiter);
    vpu.writeDataQuadword(0, gifTag(true));
    path1.startPath1Transfer(0);

    const VIFStreamWord stalled =
      vif.ingestWord(vifCode(VIFCommandEncoding::FLUSH));
    REQUIRE(stalled.stalled);

    path1.advancePath1Transfer();
    const VIFStreamWord completed =
      vif.ingestWord(vifCode(VIFCommandEncoding::FLUSH));
    REQUIRE(!completed.stalled);
  }

  SECTION("FLUSH excludes PATH3 while FLUSHA includes it")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    VPU vpu(VPUType::VU1);
    VIF vif(VIFType::VIF1);
    vif.attachVPU(&vpu);
    vif.attachGIFPathArbiter(&arbiter);
    arbiter.requestPath(GIFPath::Path3);

    const VIFStreamWord flush =
      vif.ingestWord(vifCode(VIFCommandEncoding::FLUSH));
    REQUIRE(!flush.stalled);

    const VIFStreamWord flusha =
      vif.ingestWord(vifCode(VIFCommandEncoding::FLUSHA));
    REQUIRE(flusha.stalled);

    arbiter.transferQuadword(
      GIFPath::Path3,
      gifTag(true));
    const VIFStreamWord completed =
      vif.ingestWord(vifCode(VIFCommandEncoding::FLUSHA));
    REQUIRE(!completed.stalled);
  }

  SECTION("MSCALF waits for PATH1 and PATH2 before starting VU1")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    VPU vpu(VPUType::VU1);
    VIF vif(VIFType::VIF1);
    vif.attachVPU(&vpu);
    vif.attachGIFPathArbiter(&arbiter);
    writeTerminatingProgram(&vpu, 0);
    arbiter.requestPath(GIFPath::Path1);

    const VIFStreamWord stalled =
      vif.ingestWord(vifCode(VIFCommandEncoding::MSCALF));
    REQUIRE(stalled.stalled);
    REQUIRE(vpu.getState() == VPU_STATE_READY);

    arbiter.transferQuadword(
      GIFPath::Path1,
      gifTag(true));
    const VIFStreamWord started =
      vif.ingestWord(vifCode(VIFCommandEncoding::MSCALF));
    REQUIRE(!started.stalled);
    REQUIRE(vpu.getState() == VPU_STATE_RUN);
  }
}

TEST_CASE("VIF Interrupt Stall Tests")
{
  SECTION("An interrupt stalls non-MARK commands until acknowledged")
  {
    VIF vif(VIFType::VIF1);
    vif.ingestWord(vifCode(
      VIFCommandEncoding::NOP,
      0,
      true));
    REQUIRE(vif.interruptPending());
    const std::uint64_t wordsBeforeStall = vif.wordsIngested();

    const VIFStreamWord stalled =
      vif.ingestWord(vifCode(
        VIFCommandEncoding::STCYCL,
        0x0404));
    REQUIRE(stalled.stalled);
    REQUIRE(vif.wordsIngested() == wordsBeforeStall);
    REQUIRE(vif.cycle() == 0);

    const VIFStreamWord mark =
      vif.ingestWord(vifCode(
        VIFCommandEncoding::MARK,
        0xbeef));
    REQUIRE(!mark.stalled);
    REQUIRE(vif.mark() == 0xbeef);
    REQUIRE(vif.interruptPending());

    vif.clearInterrupt();
    const VIFStreamWord resumed =
      vif.ingestWord(vifCode(
        VIFCommandEncoding::STCYCL,
        0x0404));
    REQUIRE(!resumed.stalled);
    REQUIRE(vif.cycle() == 0x0404);
  }

  SECTION("Payload commands interrupt only after their final word")
  {
    VIF vif(VIFType::VIF1);
    vif.ingestWord(vifCode(
      VIFCommandEncoding::STMASK,
      0,
      true));
    REQUIRE(!vif.interruptPending());
    vif.ingestWord(0x12345678);
    REQUIRE(vif.interruptPending());
    REQUIRE(vif.mask() == 0x12345678);
  }
}
