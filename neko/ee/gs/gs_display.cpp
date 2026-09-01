#include <algorithm>
#include <stdexcept>

#include "gs.hpp"
#include "gs_display.hpp"

namespace
{
  constexpr std::uint8_t COMPONENTS_PER_PIXEL = 4;

  std::uint8_t component(
    std::uint32_t pixel,
    std::uint8_t shift)
  {
    return static_cast<std::uint8_t>(pixel >> shift);
  }
}

GSDisplay::GSDisplay(GS *gs) :
  gsComponent(gs)
{
  if (gsComponent == nullptr)
  {
    throw std::invalid_argument(
      "GS display requires a non-null GS.");
  }
}

bool GSDisplay::clockActive() const
{
  return true;
}

void GSDisplay::clock()
{
  ++cycleInFrame;
  if (!verticalBlank &&
      cycleInFrame == videoTiming.activeCycles)
  {
    verticalBlank = true;
    verticalBlankStarted = true;
    vsyncInterrupt = true;
    ++frameBoundaries;
  }
  if (cycleInFrame == videoTiming.totalCycles)
  {
    cycleInFrame = 0;
    verticalBlank = false;
    verticalBlankEnded = true;
    if ((syncModeRegister & 1) != 0)
    {
      oddField = !oddField;
    }
  }
}

void GSDisplay::configureTiming(const GSDisplayTiming &timing)
{
  if (timing.activeCycles == 0 ||
      timing.activeCycles >= timing.totalCycles)
  {
    throw std::invalid_argument(
      "GS display timing requires active cycles before total cycles.");
  }
  if (cycleInFrame != 0)
  {
    throw std::logic_error(
      "GS display timing can change only at a frame boundary.");
  }
  videoTiming = timing;
}

void GSDisplay::writePrivilegedRegister(
  std::uint8_t address,
  std::uint64_t value)
{
  switch (address)
  {
    case GSDisplayPrivilegedRegister::PMODE:
      modeRegister = value & 0xffff;
      return;
    case GSDisplayPrivilegedRegister::SMODE2:
      syncModeRegister = value & 0x0f;
      return;
    case GSDisplayPrivilegedRegister::DISPFB1:
      decodeFrame(0, value);
      return;
    case GSDisplayPrivilegedRegister::DISPLAY1:
      decodeDisplay(0, value);
      return;
    case GSDisplayPrivilegedRegister::DISPFB2:
      decodeFrame(1, value);
      return;
    case GSDisplayPrivilegedRegister::DISPLAY2:
      decodeDisplay(1, value);
      return;
    case GSDisplayPrivilegedRegister::BGCOLOR:
      backgroundColor = value & 0x00ffffff;
      return;
    case GSDisplayPrivilegedRegister::CSR:
      if ((value & GSSystemStatus::VSYNC_INTERRUPT) != 0)
      {
        vsyncInterrupt = false;
      }
      return;
    case GSDisplayPrivilegedRegister::IMR:
      interruptMaskRegister = value & GSInterruptMask::ALL;
      return;
    default:
      throw std::invalid_argument(
        "GS display privileged register is not implemented.");
  }
}

std::uint64_t GSDisplay::readPrivilegedRegister(
  std::uint8_t address) const
{
  switch (address)
  {
    case GSDisplayPrivilegedRegister::CSR:
      return
        (vsyncInterrupt
          ? GSSystemStatus::VSYNC_INTERRUPT
          : 0) |
        (oddField ? GSSystemStatus::FIELD : 0);
    case GSDisplayPrivilegedRegister::IMR:
      return interruptMaskRegister;
    default:
      throw std::invalid_argument(
        "GS display privileged register is write-only.");
  }
}

GSPresentation GSDisplay::presentation() const
{
  const std::size_t dimensionsCircuit =
    circuitEnabled(0) ? 0 : 1;
  if (!circuitEnabled(dimensionsCircuit))
  {
    return {};
  }
  const Circuit &dimensions = circuits[dimensionsCircuit];
  GSPresentation result;
  result.width = dimensions.displayWidth;
  result.height = dimensions.displayHeight;
  result.rgba.reserve(
    static_cast<std::size_t>(result.width) *
    result.height *
    COMPONENTS_PER_PIXEL);

  const std::uint32_t background =
    static_cast<std::uint32_t>(backgroundColor) |
    UINT32_C(0xff000000);
  for (std::uint16_t y = 0; y < result.height; ++y)
  {
    for (std::uint16_t x = 0; x < result.width; ++x)
    {
      std::uint32_t destination =
        circuitEnabled(1)
          ? circuitPixel(1, x, y)
          : static_cast<std::uint32_t>(backgroundColor);
      std::uint32_t output = destination;
      if (circuitEnabled(0))
      {
        const std::uint32_t source = circuitPixel(0, x, y);
        const std::uint8_t alpha =
          (modeRegister & GSDisplayMode::FIXED_ALPHA) != 0
            ? static_cast<std::uint8_t>(modeRegister >> 8)
            : static_cast<std::uint8_t>(
              std::min<std::uint32_t>(
                component(source, 24) * 2,
                0xff));
        if ((modeRegister &
             GSDisplayMode::BLEND_BACKGROUND) != 0)
        {
          destination = background;
        }
        std::uint32_t blended = 0;
        for (std::uint8_t shift : {0, 8, 16})
        {
          const std::uint32_t value =
            (component(source, shift) * alpha +
             component(destination, shift) * (0xff - alpha)) /
            0xff;
          blended |= value << shift;
        }
        const std::uint8_t outputAlpha =
          (modeRegister &
           GSDisplayMode::CIRCUIT_2_ALPHA) != 0
            ? component(destination, 24)
            : component(source, 24);
        output = blended |
          (static_cast<std::uint32_t>(outputAlpha) << 24);
      }
      result.rgba.push_back(component(output, 0));
      result.rgba.push_back(component(output, 8));
      result.rgba.push_back(component(output, 16));
      result.rgba.push_back(component(output, 24));
    }
  }
  return result;
}

bool GSDisplay::inVerticalBlank() const
{
  return verticalBlank;
}

bool GSDisplay::interruptPending() const
{
  return
    vsyncInterrupt &&
    (interruptMaskRegister & GSInterruptMask::VSYNC) == 0;
}

bool GSDisplay::takeVerticalBlankStart()
{
  const bool event = verticalBlankStarted;
  verticalBlankStarted = false;
  return event;
}

bool GSDisplay::takeVerticalBlankEnd()
{
  const bool event = verticalBlankEnded;
  verticalBlankEnded = false;
  return event;
}

std::uint64_t GSDisplay::presentationBoundaryCount() const
{
  return frameBoundaries;
}

void GSDisplay::decodeFrame(
  std::size_t index,
  std::uint64_t value)
{
  Circuit &circuit = circuits[index];
  const std::uint8_t pixelStorageMode =
    (value >> 15) & 0x1f;
  if (pixelStorageMode != GSPixelStorageMode::PSMCT32)
  {
    throw std::invalid_argument(
      "GS display currently supports only PSMCT32.");
  }
  circuit.basePointer = value & 0x1ff;
  circuit.bufferWidth = (value >> 9) & 0x3f;
  circuit.pixelStorageMode = pixelStorageMode;
  circuit.sourceX = (value >> 32) & 0x7ff;
  circuit.sourceY = (value >> 43) & 0x7ff;
}

void GSDisplay::decodeDisplay(
  std::size_t index,
  std::uint64_t value)
{
  Circuit &circuit = circuits[index];
  circuit.horizontalMagnification =
    static_cast<std::uint8_t>(((value >> 23) & 0x0f) + 1);
  circuit.verticalMagnification =
    static_cast<std::uint8_t>(((value >> 27) & 0x03) + 1);
  circuit.displayWidth = static_cast<std::uint16_t>(
    (((value >> 32) & 0x0fff) + 1) /
    circuit.horizontalMagnification);
  circuit.displayHeight = static_cast<std::uint16_t>(
    (((value >> 44) & 0x07ff) + 1) /
    circuit.verticalMagnification);
}

std::uint32_t GSDisplay::circuitPixel(
  std::size_t index,
  std::uint16_t x,
  std::uint16_t y) const
{
  const Circuit &circuit = circuits[index];
  if (x >= circuit.displayWidth ||
      y >= circuit.displayHeight)
  {
    return static_cast<std::uint32_t>(backgroundColor);
  }
  return gsComponent->readDisplayPSMCT32(
    circuit.basePointer,
    circuit.bufferWidth,
    circuit.sourceX + x,
    circuit.sourceY + y);
}

bool GSDisplay::circuitEnabled(std::size_t index) const
{
  return
    (modeRegister & (UINT64_C(1) << index)) != 0 &&
    ((syncModeRegister >> 2) & 0x03) != 0x03;
}
