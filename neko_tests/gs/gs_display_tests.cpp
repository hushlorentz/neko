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
      32, 1, 0, 0, 0xffc80000);

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

TEST_CASE("GS display bases use FRAME page units for both circuits",
          "[gs][display-base]")
{
  for (const std::uint16_t base : {1, 0x46, 0x1ff})
  {
    for (const bool secondCircuit : {false, true})
    {
      NekoSystem system;
      GS &gs = system.gs();
      EEBus &bus = system.eeBus();
      gs.writeRegister(GSRegisterAddress::FRAME_1,
        base | (UINT64_C(1) << 16));
      gs.writePSMCT32(0, 63, 31, UINT32_C(0xff332211));
      gs.writePSMCT32(0, 64, 31, UINT32_C(0x80665544));
      bus.write64(secondCircuit ?
        EEMemoryMap::GS_DISPFB2 : EEMemoryMap::GS_DISPFB1,
        displayFrame(base, 1, 63, 31));
      bus.write64(secondCircuit ?
        EEMemoryMap::GS_DISPLAY2 : EEMemoryMap::GS_DISPLAY1,
        displayArea(2, 1));
      bus.write64(EEMemoryMap::GS_PMODE, secondCircuit ?
        GSDisplayMode::ENABLE_CIRCUIT_2 : GSDisplayMode::ENABLE_CIRCUIT_1);

      const GSPresentation presentation = system.gsDisplay().presentation();
      REQUIRE(presentation.width == 2);
      REQUIRE(presentation.height == 1);
      REQUIRE(presentation.rgba == std::vector<std::uint8_t>{
        0x11, 0x22, 0x33, 0xff, 0x44, 0x55, 0x66, 0x80});
    }
  }
}

TEST_CASE("GS display page selection agrees cold restored and traced",
          "[gs][display-base][conformance]")
{
  const auto prepare = [](NekoSystem &system)
  {
    GS &gs = system.gs();
    gs.writeRegister(GSRegisterAddress::FRAME_1,
      UINT64_C(0x46) | (UINT64_C(1) << 16));
    gs.writePSMCT32(0, 0, 0, UINT32_C(0xff332211));
    gs.writeRegister(GSRegisterAddress::FRAME_1,
      UINT64_C(0x118) | (UINT64_C(1) << 16));
    gs.writePSMCT32(0, 0, 0, UINT32_C(0x80665544));
    system.eeBus().write64(EEMemoryMap::GS_DISPFB2, displayFrame(0x46, 1));
    system.eeBus().write64(EEMemoryMap::GS_DISPLAY2, displayArea(1, 1));
    system.eeBus().write64(EEMemoryMap::GS_PMODE,
      GSDisplayMode::ENABLE_CIRCUIT_2);
  };
  NekoSystem origin;
  prepare(origin);
  const auto checkpoint = origin.saveState();
  origin.eeBus().write64(EEMemoryMap::GS_DISPFB2, displayFrame(0x118, 1));
  const auto expected = origin.saveState();

  for (const bool restored : {false, true})
  {
    for (const bool traced : {false, true})
    {
      NekoSystem system;
      if (restored)
      {
        system.loadState(checkpoint);
      }
      else
      {
        prepare(system);
      }
      if (traced)
      {
        system.startTrace();
      }
      REQUIRE(system.gsDisplay().presentation().rgba ==
        std::vector<std::uint8_t>{0x11, 0x22, 0x33, 0xff});
      REQUIRE(system.saveState() == checkpoint);
      system.eeBus().write64(EEMemoryMap::GS_DISPFB2, displayFrame(0x118, 1));
      REQUIRE(system.gsDisplay().presentation().rgba ==
        std::vector<std::uint8_t>{0x44, 0x55, 0x66, 0x80});
      REQUIRE(system.saveState() == expected);
    }
  }
}

TEST_CASE("GS interlaced FRAME presentation reads only the current field",
          "[gs][display][interlace]")
{
  for (const bool secondCircuit : {false, true})
  {
    for (const bool modeFirst : {false, true})
    {
      for (const std::uint8_t magnification : {1, 2})
      {
        GS gs;
        GSDisplay display(&gs);
        display.configureTiming({1, 2, {1, 2, 1}});
        for (std::uint16_t row = 0; row < 6; ++row)
        {
          gs.writeDisplayPSMCT32(32, 2, 63, 31 + row, 0xff000010 + row);
          gs.writeDisplayPSMCT32(32, 2, 64, 31 + row, 0xff000020 + row);
        }
        if (modeFirst)
        {
          display.writePrivilegedRegister(GSDisplayPrivilegedRegister::SMODE2, 3);
        }
        display.writePrivilegedRegister(secondCircuit ?
          GSDisplayPrivilegedRegister::DISPFB2 : GSDisplayPrivilegedRegister::DISPFB1,
          displayFrame(1, 2, 63, 31));
        display.writePrivilegedRegister(secondCircuit ?
          GSDisplayPrivilegedRegister::DISPLAY2 : GSDisplayPrivilegedRegister::DISPLAY1,
          displayArea(8, 6 * magnification) |
            (UINT64_C(3) << 23) |
            (static_cast<std::uint64_t>(magnification - 1) << 27));
        if (!modeFirst)
        {
          display.writePrivilegedRegister(GSDisplayPrivilegedRegister::SMODE2, 3);
        }
        display.writePrivilegedRegister(GSDisplayPrivilegedRegister::PMODE,
          secondCircuit ? GSDisplayMode::ENABLE_CIRCUIT_2 :
            GSDisplayMode::ENABLE_CIRCUIT_1);

        const auto expected = std::vector<std::uint8_t>{
          0x10, 0, 0, 0xff, 0x20, 0, 0, 0xff,
          0x11, 0, 0, 0xff, 0x21, 0, 0, 0xff,
          0x12, 0, 0, 0xff, 0x22, 0, 0, 0xff};
        const auto field = display.presentation();
        REQUIRE(field.width == 2);
        REQUIRE(field.height == 3);
        REQUIRE(field.rgba == expected);
        display.clock();
        display.clock();
        REQUIRE((display.readPrivilegedRegister(GSDisplayPrivilegedRegister::CSR) &
          GSSystemStatus::FIELD) != 0);
        REQUIRE(display.presentation().rgba == expected);
      }
    }
  }
}

TEST_CASE("GS interlaced FIELD presentation selects source rows by field parity",
          "[gs][display][interlace]")
{
  for (const bool secondCircuit : {false, true})
  {
    for (const std::uint8_t magnification : {1, 2})
    {
      GS gs;
      GSDisplay display(&gs);
      display.configureTiming({1, 2, {1, 2, 1}});
      for (std::uint16_t row = 0; row < 6; ++row)
      {
        gs.writeDisplayPSMCT32(32, 1, 0, 31 + row, 0xff000010 + row);
      }
      display.writePrivilegedRegister(secondCircuit ?
        GSDisplayPrivilegedRegister::DISPFB2 : GSDisplayPrivilegedRegister::DISPFB1,
        displayFrame(1, 1, 0, 31));
      display.writePrivilegedRegister(secondCircuit ?
        GSDisplayPrivilegedRegister::DISPLAY2 : GSDisplayPrivilegedRegister::DISPLAY1,
        displayArea(1, 6 * magnification) |
          (static_cast<std::uint64_t>(magnification - 1) << 27));
      display.writePrivilegedRegister(GSDisplayPrivilegedRegister::PMODE,
        secondCircuit ? GSDisplayMode::ENABLE_CIRCUIT_2 :
          GSDisplayMode::ENABLE_CIRCUIT_1);
      display.writePrivilegedRegister(GSDisplayPrivilegedRegister::SMODE2, 1);

      const auto even = display.presentation();
      REQUIRE(even.width == 1);
      REQUIRE(even.height == 3);
      REQUIRE(even.rgba == std::vector<std::uint8_t>{
        0x10, 0, 0, 0xff, 0x12, 0, 0, 0xff, 0x14, 0, 0, 0xff});
      display.clock();
      display.clock();
      REQUIRE((display.readPrivilegedRegister(GSDisplayPrivilegedRegister::CSR) &
        GSSystemStatus::FIELD) != 0);
      REQUIRE(display.presentation().rgba == std::vector<std::uint8_t>{
        0x11, 0, 0, 0xff, 0x13, 0, 0, 0xff, 0x15, 0, 0, 0xff});

      for (const std::uint64_t nonInterlacedMode : {0, 2})
      {
        display.writePrivilegedRegister(
          GSDisplayPrivilegedRegister::SMODE2, nonInterlacedMode);
        REQUIRE(display.presentation().height == 6);
        REQUIRE(display.presentation().rgba == std::vector<std::uint8_t>{
          0x10, 0, 0, 0xff, 0x11, 0, 0, 0xff, 0x12, 0, 0, 0xff,
          0x13, 0, 0, 0xff, 0x14, 0, 0, 0xff, 0x15, 0, 0, 0xff});
      }
      display.writePrivilegedRegister(GSDisplayPrivilegedRegister::SMODE2, 3);
      REQUIRE(display.presentation().height == 3);
      REQUIRE(display.presentation().rgba == std::vector<std::uint8_t>{
        0x10, 0, 0, 0xff, 0x11, 0, 0, 0xff, 0x12, 0, 0, 0xff});
    }
  }
}

TEST_CASE("GS interlaced merge clips each circuit to its field height",
          "[gs][display][interlace]")
{
  GS gs;
  GSDisplay display(&gs);
  gs.writeDisplayPSMCT32(0, 1, 0, 0, 0x00000000);
  gs.writeDisplayPSMCT32(0, 1, 0, 1, 0x00000000);
  gs.writeDisplayPSMCT32(32, 1, 0, 0, 0xff000011);
  gs.writeDisplayPSMCT32(32, 1, 0, 1, 0xff000022);
  display.writePrivilegedRegister(GSDisplayPrivilegedRegister::DISPFB1, displayFrame(0, 1));
  display.writePrivilegedRegister(GSDisplayPrivilegedRegister::DISPFB2, displayFrame(1, 1));
  display.writePrivilegedRegister(GSDisplayPrivilegedRegister::DISPLAY1, displayArea(1, 4));
  display.writePrivilegedRegister(GSDisplayPrivilegedRegister::DISPLAY2, displayArea(1, 2));
  display.writePrivilegedRegister(GSDisplayPrivilegedRegister::SMODE2, 3);
  display.writePrivilegedRegister(GSDisplayPrivilegedRegister::PMODE,
    GSDisplayMode::ENABLE_CIRCUIT_1 | GSDisplayMode::ENABLE_CIRCUIT_2);

  const auto field = display.presentation();
  REQUIRE(field.width == 1);
  REQUIRE(field.height == 2);
  REQUIRE(field.rgba == std::vector<std::uint8_t>{
    0x11, 0, 0, 0, 0, 0, 0, 0});
}

TEST_CASE("GS interlaced fields agree cold restored and traced",
          "[gs][display][interlace][conformance]")
{
  for (const std::uint64_t mode : {1, 3})
  {
    const auto prepare = [mode](NekoSystem &system)
    {
      system.gsDisplay().configureTiming({1, 2, {1, 2, 1}});
      for (std::uint16_t row = 0; row < 5; ++row)
      {
        system.gs().writeDisplayPSMCT32(0, 1, 0, row, 0xff000000 + row * 0x11);
      }
      system.eeBus().write64(EEMemoryMap::GS_DISPFB2, displayFrame(0, 1, 0, 1));
      system.eeBus().write64(EEMemoryMap::GS_DISPLAY2, displayArea(1, 4));
      system.eeBus().write64(EEMemoryMap::GS_PMODE, GSDisplayMode::ENABLE_CIRCUIT_2);
      system.eeBus().write64(EEMemoryMap::GS_SMODE2, mode);
      system.runMasterCycles(2);
    };
    const GSPresentation expectedOdd{1, 2, mode == 1 ?
      std::vector<std::uint8_t>{0x22, 0, 0, 0xff, 0x44, 0, 0, 0xff} :
      std::vector<std::uint8_t>{0x11, 0, 0, 0xff, 0x22, 0, 0, 0xff}};
    const GSPresentation expectedEven{1, 2, mode == 1 ?
      std::vector<std::uint8_t>{0x11, 0, 0, 0xff, 0x33, 0, 0, 0xff} :
      expectedOdd.rgba};
    NekoSystem origin;
    prepare(origin);
    const auto checkpoint = origin.saveState();
    origin.startTrace();
    origin.runMasterCycles(2);
    const auto expectedState = origin.saveState();
    std::uint64_t presentationBoundaries = 0;
    for (const auto &event : origin.trace())
    {
      if (event.type == NekoTraceEventType::PresentationBoundary)
      {
        ++presentationBoundaries;
        REQUIRE(event.value1 == nekoFrameHash(expectedOdd));
      }
    }
    REQUIRE(presentationBoundaries == 1);

    for (const bool restored : {false, true})
    {
      for (const bool traced : {false, true})
      {
        NekoSystem system;
        if (restored)
        {
          system.loadState(checkpoint);
        }
        else
        {
          prepare(system);
        }
        REQUIRE(system.videoOutput().width == expectedOdd.width);
        REQUIRE(system.videoOutput().height == expectedOdd.height);
        REQUIRE(system.videoOutput().rgba == expectedOdd.rgba);
        REQUIRE(system.saveState() == checkpoint);
        if (traced)
        {
          system.startTrace();
        }
        system.runMasterCycles(2);
        REQUIRE(system.videoOutput().height == expectedEven.height);
        REQUIRE(system.videoOutput().rgba == expectedEven.rgba);
        REQUIRE(system.saveState() == expectedState);
        if (traced)
        {
          REQUIRE(system.trace().size() == origin.trace().size());
          REQUIRE(system.traceHash() == origin.traceHash());
        }
      }
    }
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
