#ifndef GS_DISPLAY_HPP
#define GS_DISPLAY_HPP

#include <array>
#include <cstdint>
#include <vector>

#include "clocked_component.hpp"

class GS;

namespace GSDisplayMode
{
  constexpr std::uint64_t ENABLE_CIRCUIT_1 = 1u;
  constexpr std::uint64_t ENABLE_CIRCUIT_2 = 1u << 1;
  constexpr std::uint64_t FIXED_ALPHA = 1u << 5;
  constexpr std::uint64_t CIRCUIT_2_ALPHA = 1u << 6;
  constexpr std::uint64_t BLEND_BACKGROUND = 1u << 7;
}

namespace GSSystemStatus
{
  constexpr std::uint64_t VSYNC_INTERRUPT = 1u << 3;
  constexpr std::uint64_t FIELD = 1u << 13;
}

namespace GSInterruptMask
{
  constexpr std::uint64_t SIGNAL = 1u << 8;
  constexpr std::uint64_t FINISH = 1u << 9;
  constexpr std::uint64_t HSYNC = 1u << 10;
  constexpr std::uint64_t VSYNC = 1u << 11;
  constexpr std::uint64_t RECTANGULAR_WRITE = 1u << 12;
  constexpr std::uint64_t ALL =
    SIGNAL | FINISH | HSYNC | VSYNC | RECTANGULAR_WRITE;
}

namespace GSDisplayPrivilegedRegister
{
  constexpr std::uint8_t PMODE = 0x00;
  constexpr std::uint8_t SMODE2 = 0x02;
  constexpr std::uint8_t DISPFB1 = 0x07;
  constexpr std::uint8_t DISPLAY1 = 0x08;
  constexpr std::uint8_t DISPFB2 = 0x09;
  constexpr std::uint8_t DISPLAY2 = 0x0a;
  constexpr std::uint8_t BGCOLOR = 0x0e;
  constexpr std::uint8_t CSR = 0x40;
  constexpr std::uint8_t IMR = 0x41;
}

struct GSDisplayTiming
{
  static constexpr std::uint64_t NTSC_ACTIVE_CYCLES = 4498390;
  static constexpr std::uint64_t NTSC_TOTAL_CYCLES = 4920115;

  std::uint64_t activeCycles = NTSC_ACTIVE_CYCLES;
  std::uint64_t totalCycles = NTSC_TOTAL_CYCLES;
};

struct GSPresentation
{
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  std::vector<std::uint8_t> rgba;
};

class GSDisplay : public ClockedComponent
{
  public:
    explicit GSDisplay(GS *gs);

    bool clockActive() const override;
    void clock() override;
    void configureTiming(const GSDisplayTiming &timing);

    void writePrivilegedRegister(
      std::uint8_t address,
      std::uint64_t value);
    std::uint64_t readPrivilegedRegister(
      std::uint8_t address) const;
    GSPresentation presentation() const;

    bool inVerticalBlank() const;
    bool interruptPending() const;
    bool takeVerticalBlankStart();
    bool takeVerticalBlankEnd();
    std::uint64_t presentationBoundaryCount() const;

  private:
    struct Circuit
    {
      std::uint16_t basePointer = 0;
      std::uint8_t bufferWidth = 0;
      std::uint8_t pixelStorageMode = 0;
      std::uint16_t sourceX = 0;
      std::uint16_t sourceY = 0;
      std::uint8_t horizontalMagnification = 1;
      std::uint8_t verticalMagnification = 1;
      std::uint16_t displayWidth = 1;
      std::uint16_t displayHeight = 1;
    };

    void decodeFrame(std::size_t index, std::uint64_t value);
    void decodeDisplay(std::size_t index, std::uint64_t value);
    std::uint32_t circuitPixel(
      std::size_t index,
      std::uint16_t x,
      std::uint16_t y) const;
    bool circuitEnabled(std::size_t index) const;

    GS *gsComponent;
    std::array<Circuit, 2> circuits = {};
    GSDisplayTiming videoTiming;
    std::uint64_t modeRegister = 0;
    std::uint64_t syncModeRegister = 0;
    std::uint64_t backgroundColor = 0;
    std::uint64_t interruptMaskRegister = GSInterruptMask::ALL;
    std::uint64_t cycleInFrame = 0;
    std::uint64_t frameBoundaries = 0;
    bool verticalBlank = false;
    bool oddField = false;
    bool vsyncInterrupt = false;
    bool verticalBlankStarted = false;
    bool verticalBlankEnded = false;
};

#endif
