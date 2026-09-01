#include <cstdint>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "neko_system.hpp"
#include "regression_trace.hpp"

namespace
{
  GIFQuadword gifTag()
  {
    const std::uint64_t low =
      UINT64_C(1) |
      (UINT64_C(1) << 15) |
      (static_cast<std::uint64_t>(GIFDataFormat::Packed) << 58) |
      (UINT64_C(1) << 60);
    return GIFQuadword{{
      static_cast<std::uint32_t>(low),
      static_cast<std::uint32_t>(low >> 32),
      GIFRegisterDescriptor::AD,
      0
    }};
  }

  GIFQuadword primitiveWrite()
  {
    return GIFQuadword{{
      static_cast<std::uint8_t>(GSPrimitiveType::Sprite),
      0,
      GSRegisterAddress::PRIM,
      0
    }};
  }

  void prepareRegressionFrame(NekoSystem *system)
  {
    system->gsDisplay().configureTiming({4, 6});
    system->gs().writeDisplayPSMCT32(
      0, 1, 0, 0, 0xff332211);
    system->eeBus().write64(
      EEMemoryMap::GS_DISPFB1,
      UINT64_C(1) << 9);
    system->eeBus().write64(EEMemoryMap::GS_DISPLAY1, 0);
    system->eeBus().write64(
      EEMemoryMap::GS_PMODE,
      GSDisplayMode::ENABLE_CIRCUIT_1);
    REQUIRE(system->eeBus().writeQuadword(0x1000, gifTag()));
    REQUIRE(system->eeBus().writeQuadword(
      0x1010,
      primitiveWrite()));
    system->eeBus().write32(
      EEMemoryMap::D_CTRL,
      GIFDMACControl::DMA_ENABLE);
    system->eeBus().write32(EEMemoryMap::D2_MADR, 0x1000);
    system->eeBus().write32(EEMemoryMap::D2_QWC, 2);
    system->eeBus().write32(
      EEMemoryMap::D2_CHCR,
      GIFDMACChannelControl::START);
  }
}

TEST_CASE("Neko Frame Hash Tests")
{
  GSPresentation presentation;
  presentation.width = 1;
  presentation.height = 1;
  presentation.rgba = {1, 2, 3, 4};

  const std::uint64_t hash = nekoFrameHash(presentation);
  REQUIRE(nekoFrameHash(presentation) == hash);

  presentation.rgba[2] = 4;
  REQUIRE(nekoFrameHash(presentation) != hash);
}

TEST_CASE("Neko Subsystem Regression Trace Tests")
{
  NekoSystem first;
  NekoSystem second;
  prepareRegressionFrame(&first);
  prepareRegressionFrame(&second);
  first.startTrace();
  second.startTrace();
  NekoInputState input;
  input.buttons = NekoButton::START;
  first.setInput(input);
  second.setInput(input);

  const NekoFrameResult firstFrame = first.runFrame();
  const NekoFrameResult secondFrame = second.runFrame();

  REQUIRE(firstFrame.videoHash == secondFrame.videoHash);
  REQUIRE(firstFrame.videoHash == nekoFrameHash(firstFrame.video));
  REQUIRE(first.traceHash() == second.traceHash());
  REQUIRE(first.trace().size() == second.trace().size());
  REQUIRE(first.trace().size() >= 6);
  REQUIRE(
    first.trace().front().subsystem ==
    NekoTraceSubsystem::Input);
  REQUIRE(
    first.trace()[1].subsystem ==
    NekoTraceSubsystem::GIF);
  REQUIRE(
    first.trace()[2].subsystem ==
    NekoTraceSubsystem::GIFDMAC);
  REQUIRE(
    first.trace().back().type ==
    NekoTraceEventType::PresentationBoundary);
}

TEST_CASE("Save States Continue Identical Regression Traces")
{
  NekoSystem original;
  prepareRegressionFrame(&original);
  original.runMasterCycles(1);
  const std::vector<std::uint8_t> state = original.saveState();

  NekoSystem restored;
  restored.loadState(state);
  original.startTrace();
  restored.startTrace();

  const NekoFrameResult originalFrame = original.runFrame();
  const NekoFrameResult restoredFrame = restored.runFrame();

  REQUIRE(originalFrame.videoHash == restoredFrame.videoHash);
  REQUIRE(original.traceHash() == restored.traceHash());

  original.clearTrace();
  original.loadState(state);
  REQUIRE(original.traceEnabled());
  REQUIRE(original.trace().empty());
}
