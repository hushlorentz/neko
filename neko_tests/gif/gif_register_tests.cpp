#include <cstdint>
#include <stdexcept>

#include "catch.hpp"
#include "gif_path3.hpp"
#include "gif_registers.hpp"
#include "neko_system.hpp"

namespace
{
  class IgnoredGIFWrites : public GIFRegisterWriteHandler
  {
    public:
      void writeRegister(std::uint8_t, std::uint64_t) override {}
  };

  GIFQuadword gifTag(
    std::uint16_t loopCount,
    bool endOfPacket,
    GIFDataFormat format,
    std::uint8_t registerCount = 0,
    std::uint64_t registers = 0)
  {
    const std::uint64_t low =
      loopCount |
      (static_cast<std::uint64_t>(endOfPacket) << 15) |
      (static_cast<std::uint64_t>(format) << 58) |
      (static_cast<std::uint64_t>(registerCount) << 60);
    return GIFQuadword{{
      static_cast<std::uint32_t>(low),
      static_cast<std::uint32_t>(low >> 32),
      static_cast<std::uint32_t>(registers),
      static_cast<std::uint32_t>(registers >> 32)
    }};
  }

  TEST_CASE("GIF reset discards packets and FIFO but preserves GS and observers",
    "[gif][reset]")
  {
    GIFDataFormat format = GIFDataFormat::Packed;
    SECTION("PACKED") {}
    SECTION("REGLIST") { format = GIFDataFormat::RegisterList; }
    SECTION("IMAGE") { format = GIFDataFormat::Image; }

    NekoSystem system;
    auto &arbiter = system.gifPathArbiter();
    auto &path3 = system.gifPath3();
    auto &decoder = system.gifDecoder();
    std::size_t events = 0;
    arbiter.setTraceCallback([&](const GIFTraceEvent &) { ++events; });
    arbiter.setCycleTimingEnabled(false);
    const auto empty = gifTag(0, true, GIFDataFormat::Packed);
    REQUIRE(path3.submitQuadwords(&empty, 1).packetComplete);
    arbiter.setCycleTimingEnabled(true);
    const auto tag = gifTag(3, true, format, 1, GIFRegisterDescriptor::NOP);
    REQUIRE(path3.submitQuadwords(&tag, 1).transferredQuadwords == 0);
    arbiter.clock();
    REQUIRE(path3.submitQuadwords(&tag, 1).transferredQuadwords == 1);
    REQUIRE(decoder.packetInProgress());
    REQUIRE(arbiter.idleCyclesRemaining() == 1);
    REQUIRE(!arbiter.requestPath(GIFPath::Path1));
    REQUIRE(!arbiter.requestPath(GIFPath::Path2));
    arbiter.setPath3MaskedByVIF(true);
    system.gifRegisters().writeMode(GIFMode::M3R | GIFMode::IMT);
    REQUIRE(path3.submitGuestQuadword(empty));
    system.gs().writeRegister(GSRegisterAddress::PRIM, 6);
    const auto attempts = path3.submissionAttemptCount();
    const auto transferred = path3.transferredQuadwordCount();
    const auto completed = path3.completedPacketCount();
    const auto priorEvents = events;

    system.eeBus().write32(EEMemoryMap::GIF_BASE, 1);

    REQUIRE(system.gifRegisters().readStatus() == 0);
    REQUIRE(system.gifRegisters().readPath3Count() == 0);
    REQUIRE(system.gifRegisters().readPath3Tag() == 0);
    REQUIRE(decoder.awaitingTag());
    REQUIRE(!decoder.packetInProgress());
    REQUIRE(decoder.quadwordsRemaining() == 0);
    REQUIRE(decoder.loopIndex() == 0);
    REQUIRE(decoder.registerIndex() == 0);
    REQUIRE(arbiter.pathsIdle());
    REQUIRE(arbiter.cycleTimingEnabled());
    REQUIRE(arbiter.idleCyclesRemaining() == 0);
    REQUIRE(path3.guestFIFOQuadwordCount() == 0);
    REQUIRE(path3.submissionAttemptCount() == attempts);
    REQUIRE(path3.transferredQuadwordCount() == transferred);
    REQUIRE(path3.completedPacketCount() == completed);
    REQUIRE(events == priorEvents);
    REQUIRE(system.gs().primitive().type == GSPrimitiveType::Sprite);
    path3.advanceGuestFIFO();
    REQUIRE(path3.submissionAttemptCount() == attempts);

    const auto next = gifTag(1, true, GIFDataFormat::Packed,
      1, GIFRegisterDescriptor::AD);
    REQUIRE(path3.submitQuadwords(&next, 1).transferredQuadwords == 0);
    arbiter.clock();
    REQUIRE(path3.submitQuadwords(&next, 1).transferredQuadwords == 1);
    arbiter.clock();
    const GIFQuadword write = {{0, 0, GSRegisterAddress::PRIM, 0}};
    REQUIRE(path3.submitQuadwords(&write, 1).packetComplete);
    REQUIRE(system.gs().primitive().type == GSPrimitiveType::Point);
    REQUIRE(events > priorEvents);
  }

  TEST_CASE("GIF reset discards suspended IMAGE state and preserves observer failures",
    "[gif][reset]")
  {
    NekoSystem system;
    auto &arbiter = system.gifPathArbiter();
    IgnoredGIFWrites writes;
    system.gifDecoder().attachRegisterWriteHandler(&writes);
    arbiter.setCycleTimingEnabled(false);
    arbiter.setPath3IntermittentMode(true);
    REQUIRE(arbiter.transferQuadword(GIFPath::Path3,
      gifTag(10, true, GIFDataFormat::Image)).accepted);
    REQUIRE(!arbiter.requestPath(GIFPath::Path1));
    for (unsigned index = 0; index < 8; ++index)
    {
      REQUIRE(arbiter.transferQuadword(GIFPath::Path3, {}).accepted);
    }
    REQUIRE(arbiter.path3Interrupted());
    REQUIRE(arbiter.interruptedPath3Count() == 2);
    REQUIRE(arbiter.transferQuadword(GIFPath::Path1,
      gifTag(2, true, GIFDataFormat::Packed)).accepted);
    arbiter.setTraceCallback([](const GIFTraceEvent &)
      { throw std::runtime_error("reset observer"); });
    REQUIRE(!arbiter.requestPath(GIFPath::Path2));
    REQUIRE(arbiter.traceCallbackFailed());

    system.eeBus().write32(EEMemoryMap::GIF_BASE, 1);

    REQUIRE(system.gifRegisters().readStatus() == 0);
    REQUIRE(arbiter.interruptedPath3Count() == 0);
    REQUIRE(arbiter.interruptedPath3Tag() == 0);
    REQUIRE(!arbiter.path3Interrupted());
    REQUIRE(!arbiter.cycleTimingEnabled());
    REQUIRE(arbiter.traceCallbackFailed());
    REQUIRE_THROWS_WITH(arbiter.rethrowTraceCallbackFailure(), "reset observer");
    arbiter.clearTraceCallbackFailure();
    REQUIRE(arbiter.transferQuadword(GIFPath::Path2,
      gifTag(0, true, GIFDataFormat::Packed)).decodeResult.packetComplete);
    REQUIRE(arbiter.activePath() == GIFPath::Idle);
    REQUIRE(!arbiter.pathPending(GIFPath::Path3));
    REQUIRE(!arbiter.traceCallbackFailed());
  }

  TEST_CASE("GIF reset preserves PATH1 and partial VIF producers and DMA registers",
    "[gif][reset][save-state]")
  {
    NekoSystem system;
    auto &path1 = system.gifPath1();
    auto &vif = system.vif1();
    const auto next = gifTag(0, true, GIFDataFormat::Packed);
    system.vu1().writeDataQuadword(0, gifTag(4, true, GIFDataFormat::Image));
    system.vu1().writeDataQuadword(1, next);
    path1.startPath1Transfer(0);
    path1.advancePath1Transfer();
    REQUIRE(path1.currentQwordAddress() == 1);
    vif.processCode(UINT32_C(0x06008000));
    REQUIRE(vif.path3Masked());
    for (unsigned index = 0; index < 3; ++index)
    {
      vif.ingestWord(0);
    }
    vif.ingestWord(UINT32_C(0x50000001));
    vif.ingestWord(next[0]);
    REQUIRE(vif.awaitingPayload());
    REQUIRE(vif.submitQuadword({}));
    const auto words = vif.wordsIngested();
    system.eeBus().write32(EEMemoryMap::D_CTRL, DMACControl::DMA_ENABLE);
    system.eeBus().write32(EEMemoryMap::D2_MADR, 0x3000);
    system.eeBus().write32(EEMemoryMap::D2_QWC, 2);
    system.eeBus().write32(EEMemoryMap::D2_CHCR, GIFDMACChannelControl::START);
    const auto channel = system.eeBus().read32(EEMemoryMap::D2_CHCR);

    system.eeBus().write32(EEMemoryMap::GIF_BASE, 1);

    REQUIRE(path1.path1TransferActive());
    REQUIRE(path1.currentQwordAddress() == 1);
    REQUIRE(path1.transferredQuadwordCount() == 1);
    REQUIRE(system.vu1().readDataQuadword(1) == next);
    REQUIRE(!vif.path3Masked());
    REQUIRE(vif.awaitingPayload());
    REQUIRE(vif.wordsIngested() == words);
    REQUIRE(vif.lastCode() == UINT32_C(0x50000001));
    REQUIRE(vif.fifoQuadwordCount() == 1);
    REQUIRE(system.eeBus().read32(EEMemoryMap::D2_CHCR) == channel);
    REQUIRE(system.eeBus().read32(EEMemoryMap::D2_MADR) == 0x3000);
    REQUIRE(system.eeBus().read32(EEMemoryMap::D2_QWC) == 2);
    REQUIRE(system.eeBus().read32(EEMemoryMap::D_CTRL) == DMACControl::DMA_ENABLE);
    NekoSystem restored;
    restored.loadState(system.saveState());
    for (auto *machine : {&system, &restored})
    {
      REQUIRE(machine->gifPath1().path1TransferActive());
      machine->gifPath1().advancePath1Transfer();
      REQUIRE(!machine->gifPath1().path1TransferActive());
      REQUIRE(machine->gifPath1().currentQwordAddress() == 2);
      machine->gifPathArbiter().clock();
      for (unsigned index = 1; index < 4; ++index)
      {
        machine->vif1().ingestWord(next[index]);
      }
      REQUIRE(!machine->vif1().awaitingPayload());
      REQUIRE(machine->gifPathArbiter().pathsIdle());
    }
    REQUIRE(restored.saveState() == system.saveState());
  }
}

TEST_CASE("GIF Privileged Register Tests")
{
  SECTION("The register boundary validates its arbiter")
  {
    REQUIRE_THROWS_WITH(
      GIFRegisters(nullptr, nullptr),
      "GIF registers require non-null path components.");
  }

  SECTION("GIF_MODE controls independent PATH3 mode and register masks")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    GIFPath3Transfer path3(arbiter);
    GIFRegisters registers(&arbiter, &path3);

    registers.writeMode(GIFMode::M3R | GIFMode::IMT);
    REQUIRE(arbiter.path3MaskedByMode());
    REQUIRE(!arbiter.path3MaskedByVIF());
    REQUIRE(arbiter.path3IntermittentMode());
    REQUIRE(
      (registers.readStatus() &
       (GIFStatus::M3R | GIFStatus::IMT)) ==
      (GIFStatus::M3R | GIFStatus::IMT));

    REQUIRE(!arbiter.requestPath(GIFPath::Path3));
    arbiter.setPath3MaskedByVIF(true);
    registers.writeMode(0);
    REQUIRE(!arbiter.path3MaskedByMode());
    REQUIRE(arbiter.path3MaskedByVIF());
    REQUIRE(!arbiter.path3IntermittentMode());
    REQUIRE(arbiter.activePath() == GIFPath::Idle);
    REQUIRE(
      (registers.readStatus() &
       (GIFStatus::M3R | GIFStatus::M3P | GIFStatus::IMT)) ==
      GIFStatus::M3P);

    arbiter.setPath3MaskedByVIF(false);
    REQUIRE(arbiter.activePath() == GIFPath::Path3);
  }

  SECTION("GIF_STAT reports active and queued paths")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    GIFPath3Transfer path3(arbiter);
    GIFRegisters registers(&arbiter, &path3);

    REQUIRE(registers.readStatus() == 0);
    REQUIRE(arbiter.requestPath(GIFPath::Path3));
    REQUIRE(!arbiter.requestPath(GIFPath::Path2));
    REQUIRE(!arbiter.requestPath(GIFPath::Path1));

    const std::uint32_t status = registers.readStatus();
    REQUIRE((status & GIFStatus::OPH) != 0);
    REQUIRE(
      (status & GIFStatus::APATH_MASK) ==
      (static_cast<std::uint32_t>(GIFPath::Path3) <<
       GIFStatus::APATH_SHIFT));
    REQUIRE((status & GIFStatus::P1Q) != 0);
    REQUIRE((status & GIFStatus::P2Q) != 0);
    REQUIRE((status & GIFStatus::P3Q) == 0);
  }

  SECTION("Interrupted IMAGE state is exposed and retained")
  {
    GIFDecoder decoder;
    GIFPathArbiter arbiter(&decoder);
    GIFPath3Transfer path3(arbiter);
    GIFRegisters registers(&arbiter, &path3);
    registers.writeMode(GIFMode::IMT);
    const GIFQuadword tag =
      gifTag(10, true, GIFDataFormat::Image);
    const GIFQuadword payload = {{1, 2, 3, 4}};

    REQUIRE(arbiter.transferQuadword(
      GIFPath::Path3,
      tag).accepted);
    REQUIRE(!arbiter.requestPath(GIFPath::Path1));
    for (std::uint8_t index = 0; index < 8; ++index)
    {
      REQUIRE(arbiter.transferQuadword(
        GIFPath::Path3,
        payload).accepted);
    }

    const std::uint32_t interruptedStatus =
      registers.readStatus();
    REQUIRE((interruptedStatus & GIFStatus::IP3) != 0);
    REQUIRE((interruptedStatus & GIFStatus::P3Q) != 0);
    REQUIRE(
      (interruptedStatus & GIFStatus::APATH_MASK) ==
      (static_cast<std::uint32_t>(GIFPath::Path1) <<
       GIFStatus::APATH_SHIFT));
    REQUIRE(registers.readPath3Count() == 2);
    REQUIRE(registers.readPath3Tag() == (10u | (1u << 15)));

    REQUIRE(arbiter.transferQuadword(
      GIFPath::Path1,
      gifTag(0, true, GIFDataFormat::Packed)).accepted);
    REQUIRE((registers.readStatus() & GIFStatus::IP3) == 0);
    REQUIRE(registers.readPath3Count() == 2);
    REQUIRE(registers.readPath3Tag() == (10u | (1u << 15)));
  }
}
