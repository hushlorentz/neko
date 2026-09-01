#include <cstddef>
#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "gif_dmac_channel.hpp"
#include "neko_system.hpp"
#include "vif_command.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"

namespace
{
  constexpr std::size_t SAVE_STATE_HEADER_SIZE = 28;
  constexpr std::size_t SAVE_STATE_CHECKSUM_OFFSET = 20;
  constexpr std::uint64_t SAVE_STATE_FNV_OFFSET_BASIS =
    UINT64_C(14695981039346656037);
  constexpr std::uint64_t SAVE_STATE_FNV_PRIME =
    UINT64_C(1099511628211);

  void updateChecksum(std::vector<std::uint8_t> *state)
  {
    std::uint64_t checksum = SAVE_STATE_FNV_OFFSET_BASIS;
    for (std::size_t index = SAVE_STATE_HEADER_SIZE;
         index < state->size();
         ++index)
    {
      checksum ^= (*state)[index];
      checksum *= SAVE_STATE_FNV_PRIME;
    }
    for (std::size_t index = 0; index < 8; ++index)
    {
      (*state)[SAVE_STATE_CHECKSUM_OFFSET + index] =
        static_cast<std::uint8_t>(
          checksum >> (index * 8));
    }
  }

  std::uint32_t vifCode(
    std::uint8_t command,
    std::uint16_t immediate = 0)
  {
    return
      (static_cast<std::uint32_t>(command) << 24) |
      immediate;
  }

  GIFQuadword gifTag(
    std::uint16_t loopCount,
    bool endOfPacket,
    GIFDataFormat format,
    std::uint8_t descriptor)
  {
    const std::uint64_t low =
      loopCount |
      (static_cast<std::uint64_t>(endOfPacket) << 15) |
      (static_cast<std::uint64_t>(format) << 58) |
      (UINT64_C(1) << 60);
    return GIFQuadword{{
      static_cast<std::uint32_t>(low),
      static_cast<std::uint32_t>(low >> 32),
      descriptor,
      0
    }};
  }

  GIFQuadword adWrite(
    std::uint8_t address,
    std::uint64_t value)
  {
    return GIFQuadword{{
      static_cast<std::uint32_t>(value),
      static_cast<std::uint32_t>(value >> 32),
      address,
      0
    }};
  }

  GIFQuadword dmaTag(
    GIFDMATagID id,
    std::uint16_t qwc,
    std::uint32_t address = 0)
  {
    return GIFQuadword{{
      qwc | (static_cast<std::uint32_t>(id) << 28),
      address,
      0,
      0
    }};
  }

  std::uint32_t esum(std::uint8_t source)
  {
    return
      VPU_ESUM_ENCODING |
      (static_cast<std::uint32_t>(source) <<
       VPU_FS_REG_SHIFT);
  }

  void prepareInFlightSystem(NekoSystem *system)
  {
    system->setInput({0xa55a, 1, 2, 3, 4});
    system->gsDisplay().configureTiming({3, 7});
    system->masterClockScheduler().registerComponent(
      system->gifPathArbiter(),
      3,
      1);
    system->eeBus().write32(
      EEMemoryMap::INTC_MASK,
      EEInterruptSource::mask(EEInterruptSource::GS) |
      EEInterruptSource::mask(EEInterruptSource::VIF0));

    system->vu1().loadFPRegister(
      VPU_REGISTER_VF01,
      1,
      2,
      3,
      4);
    system->vu1().writeMicroInstruction(
      0,
      esum(VPU_REGISTER_VF01),
      VPU_NOP);
    system->vu1().writeMicroInstruction(
      1,
      VPU_LOWER_NOP,
      VPU_E_BIT | VPU_NOP);
    system->vu1().writeMicroInstruction(
      2,
      VPU_LOWER_NOP,
      VPU_NOP);
    system->vu1().startMicroMode();

    system->vif0().ingestWord(
      vifCode(VIFCommandEncoding::STROW));
    system->vif0().ingestWord(0x11111111);
    system->vif0().ingestWord(0x22222222);

    system->gs().writeRegister(
      GSRegisterAddress::PRIM,
      static_cast<std::uint8_t>(GSPrimitiveType::Triangle));
    system->gs().writeRegister(
      GSRegisterAddress::RGBAQ,
      UINT64_C(0x3f800000ffffffff));
    system->gs().writeRegister(
      GSRegisterAddress::XYZ2,
      UINT64_C(0x0000000100100010));
    system->gs().writeDisplayPSMCT32(
      4,
      1,
      2,
      3,
      0xaabbccdd);
    system->eeBus().write64(EEMemoryMap::GS_BUSDIR, 1);
    system->eeBus().write32(
      EEMemoryMap::GIF_MODE,
      GIFMode::IMT);

    const GIFQuadword tag = gifTag(
      1,
      true,
      GIFDataFormat::Packed,
      GIFRegisterDescriptor::AD);
    const GIFQuadword payload = adWrite(
      GSRegisterAddress::PRIM,
      static_cast<std::uint8_t>(
        GSPrimitiveType::TriangleFan));
    REQUIRE(system->eeBus().writeQuadword(
      0x1000,
      dmaTag(GIFDMATagID::Call, 2, 0x2000)));
    REQUIRE(system->eeBus().writeQuadword(0x1010, tag));
    REQUIRE(system->eeBus().writeQuadword(0x1020, payload));
    REQUIRE(system->eeBus().writeQuadword(
      0x1030,
      dmaTag(GIFDMATagID::End, 0)));
    REQUIRE(system->eeBus().writeQuadword(
      0x2000,
      dmaTag(GIFDMATagID::Return, 0)));
    system->eeBus().write32(
      EEMemoryMap::D_CTRL,
      GIFDMACControl::DMA_ENABLE);
    system->eeBus().write32(EEMemoryMap::D2_TADR, 0x1000);
    system->eeBus().write32(
      EEMemoryMap::D_STAT,
      GIFDMACStatus::CHANNEL_2_MASK);
    system->eeBus().write32(
      EEMemoryMap::D2_CHCR,
      GIFDMACChannelControl::CHAIN_MODE |
      GIFDMACChannelControl::START);

    system->runMasterCycles(2);
    system->gsDisplay().clock();
    REQUIRE(system->vu1().getState() == VPU_STATE_RUN);
    REQUIRE(system->vif0().payloadWordsRemaining() == 2);
    REQUIRE(system->gifDecoder().packetInProgress());
    REQUIRE(system->gifDMAC().quadwordCount() == 1);
    REQUIRE(system->gifDMAC().addressStack(0) == 0x1030);
    REQUIRE(system->gs().queuedVertexCount() == 1);
    REQUIRE(system->gs().hostInterfaceReversed());
    REQUIRE(system->gsDisplay().inVerticalBlank());
    REQUIRE(
      system->masterClockScheduler().currentCycle() == 2);
  }

  void prepareSuspendedPath3(NekoSystem *system)
  {
    system->gs().writeRegister(
      GSRegisterAddress::BITBLTBUF,
      UINT64_C(1) << 48);
    system->gs().writeRegister(
      GSRegisterAddress::TRXPOS,
      0);
    system->gs().writeRegister(
      GSRegisterAddress::TRXREG,
      UINT64_C(40) | (UINT64_C(1) << 32));
    system->gs().writeRegister(
      GSRegisterAddress::TRXDIR,
      static_cast<std::uint8_t>(
        GSImageTransferDirection::HostToLocal));
    system->gifPathArbiter().setPath3IntermittentMode(true);

    const GIFQuadword tag = gifTag(
      10,
      true,
      GIFDataFormat::Image,
      GIFRegisterDescriptor::NOP);
    REQUIRE(
      system->gifPath3().submitQuadwords(&tag, 1)
        .transferredQuadwords == 1);
    system->vu1().writeDataQuadword(
      0,
      gifTag(
        0,
        true,
        GIFDataFormat::Packed,
        GIFRegisterDescriptor::NOP));
    system->gifPath1().startPath1Transfer(0);

    const GIFQuadword image = {{
      0x11223344,
      0x55667788,
      0x99aabbcc,
      0xddeeff00
    }};
    for (std::size_t index = 0; index < 8; ++index)
    {
      REQUIRE(
        system->gifPath3().submitQuadwords(&image, 1)
          .transferredQuadwords == 1);
    }
    REQUIRE(system->gifPathArbiter().path3Interrupted());
    REQUIRE(
      system->gifPathArbiter().activePath() ==
      GIFPath::Path1);
    REQUIRE(system->gifPath1().path1TransferActive());
    REQUIRE(system->gifDecoder().awaitingTag());
  }

  void finishSuspendedPath3(NekoSystem *system)
  {
    system->gifPath1().advancePath1Transfer();
    REQUIRE_FALSE(system->gifPath1().path1TransferActive());
    REQUIRE_FALSE(system->gifPathArbiter().path3Interrupted());

    const GIFQuadword image = {{
      0x01234567,
      0x89abcdef,
      0xfedcba98,
      0x76543210
    }};
    REQUIRE(
      system->gifPath3().submitQuadwords(&image, 1)
        .transferredQuadwords == 1);
    REQUIRE(
      system->gifPath3().submitQuadwords(&image, 1)
        .transferredQuadwords == 1);
    REQUIRE_FALSE(system->gifDecoder().packetInProgress());
    REQUIRE_FALSE(system->gs().imageTransfer().active);
  }
}

TEST_CASE("Neko save states are canonical and deterministic")
{
  NekoSystem first;
  NekoSystem second;
  prepareInFlightSystem(&first);
  prepareInFlightSystem(&second);

  const std::vector<std::uint8_t> firstState =
    first.saveState();

  REQUIRE(firstState == first.saveState());
  REQUIRE(firstState == second.saveState());

  std::size_t traceCount = 0;
  first.vu1().setTraceCallback(
    [&traceCount](const VPUTraceEvent &)
    {
      ++traceCount;
    });
  first.gifPathArbiter().setTraceCallback(
    [&traceCount](const GIFTraceEvent &)
    {
      ++traceCount;
    });
  REQUIRE(firstState == first.saveState());

  first.loadState(firstState);
  first.vu1().forceBreak();
  REQUIRE(traceCount == 1);
}

TEST_CASE("Active system save states round trip and continue identically")
{
  NekoSystem original;
  prepareInFlightSystem(&original);
  const std::vector<std::uint8_t> state =
    original.saveState();

  NekoSystem restored;
  restored.loadState(state);
  REQUIRE(restored.saveState() == state);

  original.eeBus().write64(EEMemoryMap::GS_BUSDIR, 0);
  restored.eeBus().write64(EEMemoryMap::GS_BUSDIR, 0);
  original.runMasterCycles(64);
  restored.runMasterCycles(64);

  REQUIRE(
    original.gs().primitive().type ==
    GSPrimitiveType::TriangleFan);
  REQUIRE(original.vu1().getState() == VPU_STATE_READY);
  REQUIRE(original.vif0().payloadWordsRemaining() == 2);
  REQUIRE(original.saveState() == restored.saveState());
}

TEST_CASE("Suspended PATH3 and PATH1 progress survive save states")
{
  NekoSystem original;
  prepareSuspendedPath3(&original);
  const std::vector<std::uint8_t> state =
    original.saveState();

  NekoSystem restored;
  restored.loadState(state);
  REQUIRE(restored.saveState() == state);

  finishSuspendedPath3(&original);
  finishSuspendedPath3(&restored);

  REQUIRE(
    original.gifPath1().transferredQuadwordCount() == 1);
  REQUIRE(
    original.gifPath3().transferredQuadwordCount() == 11);
  REQUIRE(original.saveState() == restored.saveState());
}

TEST_CASE("Stalled GIF DMA state resumes identically after load")
{
  NekoSystem original;
  const GIFQuadword tag = gifTag(
    1,
    true,
    GIFDataFormat::Packed,
    GIFRegisterDescriptor::AD);
  const GIFQuadword payload = adWrite(
    GSRegisterAddress::PRIM,
    static_cast<std::uint8_t>(GSPrimitiveType::Sprite));
  REQUIRE(original.eeBus().writeQuadword(0x3000, tag));
  REQUIRE(original.eeBus().writeQuadword(0x3010, payload));
  original.eeBus().write32(
    EEMemoryMap::D_CTRL,
    GIFDMACControl::DMA_ENABLE);
  original.eeBus().write32(EEMemoryMap::D2_MADR, 0x3000);
  original.eeBus().write32(EEMemoryMap::D2_QWC, 2);
  original.eeBus().write32(
    EEMemoryMap::D2_CHCR,
    GIFDMACChannelControl::START);
  original.eeBus().write32(
    EEMemoryMap::GIF_MODE,
    GIFMode::M3R);
  original.clockMasterCycle();
  REQUIRE(original.gifDMAC().stalledByPATH3());

  NekoSystem restored;
  restored.loadState(original.saveState());
  REQUIRE(restored.gifDMAC().stalledByPATH3());

  original.eeBus().write32(EEMemoryMap::GIF_MODE, 0);
  restored.eeBus().write32(EEMemoryMap::GIF_MODE, 0);
  original.runMasterCycles(2);
  restored.runMasterCycles(2);

  REQUIRE(
    original.gs().primitive().type ==
    GSPrimitiveType::Sprite);
  REQUIRE(original.saveState() == restored.saveState());
}

TEST_CASE("Reset machines can load prior save states")
{
  NekoSystem system;
  prepareInFlightSystem(&system);
  const std::vector<std::uint8_t> state =
    system.saveState();

  system.reset();
  REQUIRE(system.saveState() != state);

  system.loadState(state);
  REQUIRE(system.saveState() == state);
}

TEST_CASE("Invalid save states are rejected transactionally")
{
  NekoSystem system;
  prepareInFlightSystem(&system);
  const std::vector<std::uint8_t> before =
    system.saveState();

  std::vector<std::uint8_t> invalid = before;
  invalid[0] ^= 0xff;
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid[8] = 2;
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid[12] ^= 1;
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid.resize(invalid.size() - 1);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid.push_back(0);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid[46] = 0xff;
  updateChecksum(&invalid);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid[139] ^= 1;
  updateChecksum(&invalid);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);

  invalid = before;
  invalid.back() = 2;
  updateChecksum(&invalid);
  REQUIRE_THROWS(system.loadState(invalid));
  REQUIRE(system.saveState() == before);
}
