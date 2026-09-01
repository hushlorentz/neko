#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "neko_system.hpp"

namespace
{
  std::uint64_t displayArea(
    std::uint16_t width,
    std::uint16_t height)
  {
    return
      (static_cast<std::uint64_t>(width - 1) << 32) |
      (static_cast<std::uint64_t>(height - 1) << 44);
  }
}

TEST_CASE("Neko System Interface Tests")
{
  SECTION("Input is latched independently of the absent IOP")
  {
    NekoSystem system;
    NekoInputState input;
    input.buttons = NekoButton::START | NekoButton::CROSS;
    input.leftStickX = 0x10;
    input.rightStickY = 0xf0;

    system.setInput(input);

    REQUIRE(system.input().buttons == input.buttons);
    REQUIRE(system.input().leftStickX == 0x10);
    REQUIRE(system.input().rightStickY == 0xf0);
  }

  SECTION("Frame execution stops at the next presentation boundary")
  {
    NekoSystem system;
    system.gsDisplay().configureTiming({2, 3});
    system.gs().writeDisplayPSMCT32(
      0, 1, 0, 0, 0xff332211);
    system.eeBus().write64(
      EEMemoryMap::GS_DISPFB1,
      UINT64_C(1) << 9);
    system.eeBus().write64(
      EEMemoryMap::GS_DISPLAY1,
      displayArea(1, 1));
    system.eeBus().write64(
      EEMemoryMap::GS_PMODE,
      GSDisplayMode::ENABLE_CIRCUIT_1);

    const NekoFrameResult first = system.runFrame();
    const NekoFrameResult second = system.runFrame();

    REQUIRE(first.masterCycles == 2);
    REQUIRE(first.presentationBoundary == 1);
    REQUIRE(
      first.video.rgba ==
      std::vector<std::uint8_t>{0x11, 0x22, 0x33, 0xff});
    REQUIRE(first.audio.sampleRate == 48000);
    REQUIRE(first.audio.channelCount == 2);
    REQUIRE(first.audio.interleavedSamples.empty());
    REQUIRE(second.masterCycles == 3);
    REQUIRE(second.presentationBoundary == 2);
  }

  SECTION("Reset reconstructs neutral machine state and wiring")
  {
    NekoSystem system;
    system.eeBus().write32(0x100, 0x12345678);
    system.eeBus().write32(
      EEMemoryMap::INTC_MASK,
      EEInterruptSource::mask(EEInterruptSource::VIF0));
    NekoInputState input;
    input.buttons = NekoButton::SQUARE;
    input.leftStickX = 0;
    system.setInput(input);
    system.runMasterCycles(4);

    system.reset();

    REQUIRE(system.eeBus().read32(0x100) == 0);
    REQUIRE(system.interruptController().mask() == 0);
    REQUIRE(system.masterClockScheduler().currentCycle() == 0);
    REQUIRE(system.input().buttons == 0);
    REQUIRE(system.input().leftStickX == 0x80);
    REQUIRE(system.gsDisplay().presentationBoundaryCount() == 0);
    REQUIRE(system.gsDisplay().presentation().rgba.empty());
    REQUIRE(system.gifDMAC().globalControl() == 0);
  }
}
