#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include "catch.hpp"
#include "ee_bus.hpp"
#include "gs.hpp"
#include "gs_display.hpp"
#include "neko_system.hpp"

namespace
{
  std::uint64_t displayFrame(
    std::uint16_t base,
    std::uint8_t width,
    std::uint16_t x = 0,
    std::uint16_t y = 0)
  {
    return
      base |
      (static_cast<std::uint64_t>(width) << 9) |
      (static_cast<std::uint64_t>(x) << 32) |
      (static_cast<std::uint64_t>(y) << 43);
  }

  std::uint64_t displayArea(
    std::uint16_t width,
    std::uint16_t height)
  {
    return
      (static_cast<std::uint64_t>(width - 1) << 32) |
      (static_cast<std::uint64_t>(height - 1) << 44);
  }
}

TEST_CASE("GS Display Circuit Tests")
{
  SECTION("Circuit 1 scans PSMCT32 pixels from local memory")
  {
    NekoSystem system;
    EEBus &bus = system.eeBus();
    system.gs().writeDisplayPSMCT32(
      0, 1, 0, 0, 0xff332211);
    system.gs().writeDisplayPSMCT32(
      0, 1, 1, 0, 0x80665544);

    bus.write64(EEMemoryMap::GS_DISPFB1, displayFrame(0, 1));
    bus.write64(EEMemoryMap::GS_DISPLAY1, displayArea(2, 1));
    bus.write64(
      EEMemoryMap::GS_PMODE,
      GSDisplayMode::ENABLE_CIRCUIT_1);

    const GSPresentation presentation =
      system.gsDisplay().presentation();

    REQUIRE(presentation.width == 2);
    REQUIRE(presentation.height == 1);
    REQUIRE(
      presentation.rgba ==
      std::vector<std::uint8_t>{
        0x11, 0x22, 0x33, 0xff,
        0x44, 0x55, 0x66, 0x80
      });
  }

  SECTION("Circuit 1 alpha blends with circuit 2")
  {
    NekoSystem system;
    EEBus &bus = system.eeBus();
    system.gs().writeDisplayPSMCT32(
      0, 1, 0, 0, 0x40000064);
    system.gs().writeDisplayPSMCT32(
      1, 1, 0, 0, 0xffc80000);

    bus.write64(EEMemoryMap::GS_DISPFB1, displayFrame(0, 1));
    bus.write64(EEMemoryMap::GS_DISPLAY1, displayArea(1, 1));
    bus.write64(EEMemoryMap::GS_DISPFB2, displayFrame(1, 1));
    bus.write64(EEMemoryMap::GS_DISPLAY2, displayArea(1, 1));
    bus.write64(
      EEMemoryMap::GS_PMODE,
      GSDisplayMode::ENABLE_CIRCUIT_1 |
      GSDisplayMode::ENABLE_CIRCUIT_2);

    REQUIRE(
      system.gsDisplay().presentation().rgba ==
      std::vector<std::uint8_t>{50, 0, 99, 0x40});
  }
}

TEST_CASE("GS Vertical Blank Timing Tests")
{
  NekoSystem system;
  EEBus &bus = system.eeBus();
  system.gsDisplay().configureTiming({3, 5, {1, 2, 1}});
  bus.write32(
    EEMemoryMap::INTC_MASK,
    EEInterruptSource::mask(EEInterruptSource::GS));
  bus.write64(
    EEMemoryMap::GS_IMR,
    GSInterruptMask::ALL & ~GSInterruptMask::VSYNC);

  system.runMasterCycles(3);

  REQUIRE(system.gsDisplay().inVerticalBlank());
  REQUIRE(system.gsDisplay().presentationBoundaryCount() == 1);
  REQUIRE(
    (bus.read64(EEMemoryMap::GS_CSR) &
     GSSystemStatus::VSYNC_INTERRUPT) != 0);
  REQUIRE(
    (bus.read32(EEMemoryMap::INTC_STAT) &
     EEInterruptSource::mask(
       EEInterruptSource::VBLANK_START)) != 0);
  REQUIRE(
    (bus.read32(EEMemoryMap::INTC_STAT) &
     EEInterruptSource::mask(EEInterruptSource::GS)) != 0);
  REQUIRE(system.interruptPending());

  bus.write64(
    EEMemoryMap::GS_CSR,
    GSSystemStatus::VSYNC_INTERRUPT);
  system.runMasterCycles(2);

  REQUIRE_FALSE(system.gsDisplay().inVerticalBlank());
  REQUIRE(
    (bus.read32(EEMemoryMap::INTC_STAT) &
     EEInterruptSource::mask(
       EEInterruptSource::VBLANK_END)) != 0);
}

TEST_CASE("GS horizontal timing profiles use exact line periods and provisional blank duty",
  "[gs][timing]")
{
  for (const bool pal : {false, true})
  {
    GS gs;
    GSDisplay display(&gs);
    if (pal)
    {
      GSDisplayTiming timing;
      timing.horizontal = GSDisplayHorizontalTiming::pal();
      display.configureTiming(timing);
    }
    const std::uint64_t pixels = pal ? 864 : 858;
    bool previous = false;
    unsigned int starts = 0;
    unsigned int ends = 0;
    REQUIRE_FALSE(display.inHorizontalBlank());
    for (std::uint64_t cycle = 1; cycle <= 50000; ++cycle)
    {
      display.clock();
      const std::uint64_t phase =
        cycle * 375 * 18876 % (pixels * 8192 * 18876);
      const bool blank = phase >= pixels * 8192 * 15796;
      REQUIRE(display.inHorizontalBlank() == blank);
      starts += blank && !previous;
      ends += !blank && previous;
      previous = blank;
      REQUIRE_FALSE(display.inVerticalBlank());
    }
    REQUIRE(starts == 2);
    REQUIRE(ends == 2);
  }
}

TEST_CASE("GS horizontal phase continues across vertical boundaries and reconfigures explicitly",
  "[gs][timing]")
{
  GS gs;
  GSDisplay display(&gs);
  display.configureTiming({3, 5, {3, 7, 2}});
  for (std::uint64_t cycle = 1; cycle <= 100; ++cycle)
  {
    display.clock();
    REQUIRE(display.inHorizontalBlank() == (cycle * 2 % 7 >= 3));
    REQUIRE(display.inVerticalBlank() == (cycle % 5 >= 3));
    REQUIRE(display.takeVerticalBlankStart() == (cycle % 5 == 3));
    REQUIRE_FALSE(display.takeVerticalBlankStart());
    REQUIRE(display.takeVerticalBlankEnd() == (cycle % 5 == 0));
    REQUIRE_FALSE(display.takeVerticalBlankEnd());
  }
  REQUIRE(display.presentationBoundaryCount() == 20);
  display.configureTiming({2, 4, {1, 2, 1}});
  REQUIRE_FALSE(display.inHorizontalBlank());
  display.clock();
  REQUIRE(display.inHorizontalBlank());
  REQUIRE_THROWS_AS(display.configureTiming({2, 4, {1, 2, 1}}),
    std::logic_error);
}

TEST_CASE("GS horizontal timing rejects invalid waveforms atomically and avoids phase overflow",
  "[gs][timing]")
{
  const GSDisplayHorizontalTiming invalid[] = {
    {0, 2, 1}, {2, 2, 1}, {3, 2, 1}, {1, 2, 0},
    {1, 3, 2}, {2, 3, 2}, {1, 2, 3}
  };
  GS gs;
  for (const auto &horizontal : invalid)
  {
    GSDisplay display(&gs);
    display.configureTiming({3, 5, {3, 7, 2}});
    const auto before = display;
    REQUIRE_THROWS_AS(display.configureTiming({2, 4, horizontal}),
      std::invalid_argument);
    auto expected = before;
    for (unsigned int cycle = 0; cycle < 20; ++cycle)
    {
      display.clock();
      expected.clock();
      REQUIRE(display.inHorizontalBlank() == expected.inHorizontalBlank());
      REQUIRE(display.inVerticalBlank() == expected.inVerticalBlank());
    }
  }
  GSDisplay display(&gs);
  const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
  display.configureTiming({3, 5, {maximum / 2, maximum, maximum / 2}});
  display.clock();
  REQUIRE(display.inHorizontalBlank());
  display.clock();
  REQUIRE(display.inHorizontalBlank());
  display.clock();
  REQUIRE_FALSE(display.inHorizontalBlank());
}
