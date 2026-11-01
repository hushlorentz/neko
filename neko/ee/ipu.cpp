#include "ipu.hpp"
#include "interrupt_controller.hpp"
#include <cassert>

IPU::IPU(EEInterruptController &interrupts) :
  interruptController(interrupts)
{
}

std::uint32_t IPU::control() const
{
  return inputFIFOCount |
    (pendingCommand == Command::Idle ? 0u : 1u << 31);
}

std::uint8_t IPU::bitPosition() const
{
  return bitPositionRegister;
}

std::uint32_t IPU::bitPositionStatus() const
{
  return bitPositionRegister |
    (static_cast<std::uint32_t>(inputFIFOCount) << 8) |
    (static_cast<std::uint32_t>(streamBufferCount) << 16);
}

bool IPU::inputWriteReady() const
{
  return inputFIFOCount < inputFIFO.size();
}

bool IPU::submitInput(const EEQuadword &value)
{
  if (!inputWriteReady())
  {
    return false;
  }
  inputFIFO[inputFIFOCount++] = value;
  progressCommand();
  return true;
}

void IPU::clearInput()
{
  inputFIFO.fill({});
  inputFIFOCount = 0;
  streamBuffer.fill({});
  streamBufferCount = 0;
}

void IPU::clearCommand()
{
  pendingCommand = Command::Idle;
  forwardBits = 0;
  tableBytesLoaded = 0;
}

bool IPU::bufferStreamBits(std::uint8_t bits)
{
  while (streamBufferCount * 128 < bitPositionRegister + bits)
  {
    if (inputFIFOCount == 0)
    {
      return false;
    }
    assert(streamBufferCount < streamBuffer.size());
    streamBuffer[streamBufferCount++] = inputFIFO[0];
    --inputFIFOCount;
    for (std::size_t index = 0; index < inputFIFOCount; ++index)
    {
      inputFIFO[index] = inputFIFO[index + 1];
    }
    inputFIFO[inputFIFOCount] = {};
  }
  return true;
}

void IPU::advanceStream(std::uint8_t bits)
{
  const auto position = bitPositionRegister + bits;
  assert(position <= streamBufferCount * 128);
  if (position >= 128)
  {
    --streamBufferCount;
    streamBuffer[0] = streamBuffer[1];
    streamBuffer[1] = {};
  }
  bitPositionRegister = static_cast<std::uint8_t>(position % 128);
}

std::uint8_t IPU::streamByte(std::size_t index) const
{
  const auto &quadword = streamBuffer[index / 16];
  const auto half = index % 16 < 8 ? quadword.low : quadword.high;
  return static_cast<std::uint8_t>(half >> ((index % 8) * 8));
}

bool IPU::readStreamByte(std::uint8_t &value)
{
  if (!bufferStreamBits(8))
  {
    return false;
  }
  const auto index = bitPositionRegister / 8;
  const auto shift = bitPositionRegister % 8;
  value = streamByte(index);
  if (shift != 0)
  {
    value = static_cast<std::uint8_t>(
      (value << shift) | (streamByte(index + 1) >> (8 - shift)));
  }
  advanceStream(8);
  return true;
}

void IPU::progressCommand()
{
  if (pendingCommand == Command::Idle)
  {
    return;
  }
  if (forwardBits != 0)
  {
    if (!bufferStreamBits(forwardBits))
    {
      return;
    }
    advanceStream(forwardBits);
    forwardBits = 0;
  }
  auto &table = pendingCommand == Command::SetIQIntra ? intraIQ : nonIntraIQ;
  while (tableBytesLoaded < table.size())
  {
    std::uint8_t value = 0;
    if (!readStreamByte(value))
    {
      return;
    }
    table[tableBytesLoaded++] = value;
  }
  clearCommand();
  // Provisional synchronous available-work completion, not physical latency.
  interruptController.setSource(EEInterruptSource::IPU, true);
}

bool IPU::writeControl(std::uint32_t value)
{
  constexpr std::uint32_t RESET = 1u << 30;
  if (value != 0 && value != RESET)
  {
    return false;
  }
  if (value == RESET)
  {
    clearInput();
    clearCommand();
    bitPositionRegister = 0;
    // Provisional reset-completion IRQ, including an already idle IPU.
    interruptController.setSource(EEInterruptSource::IPU, true);
  }
  return true;
}

bool IPU::writeCommand(std::uint32_t value)
{
  if (pendingCommand != Command::Idle)
  {
    return false;
  }
  if (value <= 0x7f)
  {
    clearInput();
    bitPositionRegister = static_cast<std::uint8_t>(value);
    interruptController.setSource(EEInterruptSource::IPU, true);
    return true;
  }
  constexpr std::uint32_t SETIQ_FIELDS = (1u << 27) | 0x3f;
  if ((value & ~SETIQ_FIELDS) != 0x50000000 || (value & 0x3f) > 32)
  {
    return false;
  }
  pendingCommand = value & (1u << 27) ?
    Command::SetIQNonIntra : Command::SetIQIntra;
  forwardBits = static_cast<std::uint8_t>(value & 0x3f);
  progressCommand();
  return true;
}
