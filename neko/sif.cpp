#include "sif.hpp"

#include <stdexcept>

constexpr std::size_t SIF::WORD_CAPACITY;
constexpr std::uint8_t SIF::END;

SIF::SIF()
{
  reset();
}

void SIF::reset()
{
  mscom = 0;
  smcom = 0;
  msflag = 0;
  smflag = 0;
  controlRegister = 0;
  iopDoorbellRegister = 0;
  storage = {};
  for (std::size_t index = 0; index < WORD_CAPACITY; ++index)
  {
    next[index] = static_cast<std::uint8_t>(index + 1);
  }
  streams = {};
  freeHead = 0;
  packingLatch = {};
  packingCursor = 0;
}

std::uint32_t SIF::readRegister(SIFRegister reg) const
{
  switch (reg)
  {
    case SIFRegister::MSCOM: return mscom;
    case SIFRegister::SMCOM: return smcom;
    case SIFRegister::MSFLAG: return msflag;
    case SIFRegister::SMFLAG: return smflag;
    case SIFRegister::CTRL: return controlRegister;
  }
  throw std::out_of_range("Unsupported SIF register.");
}

std::uint32_t SIF::readEE(SIFRegister reg) const
{
  const std::uint32_t value = readRegister(reg);
  return reg == SIFRegister::CTRL ? value | UINT32_C(0xf0000102) : value;
}

std::uint32_t SIF::readIOP(SIFRegister reg) const
{
  const std::uint32_t value = readRegister(reg);
  return reg == SIFRegister::CTRL ? value | UINT32_C(0xf0000002) : value;
}

SIFWriteResult SIF::writeEE(SIFRegister reg, std::uint32_t value)
{
  switch (reg)
  {
    case SIFRegister::MSCOM:
      mscom = value;
      return {};
    case SIFRegister::SMCOM:
      return {};
    case SIFRegister::MSFLAG:
      msflag |= value;
      return {};
    case SIFRegister::SMFLAG:
      smflag &= ~value;
      return {};
    case SIFRegister::CTRL:
      if ((value & 0x80000) != 0)
      {
        return {SIFWriteOutcome::UnsupportedControl, SIFSignal::None};
      }
      controlRegister = (controlRegister & ~UINT32_C(0x100)) | (value & 0x100);
      return {SIFWriteOutcome::Completed,
        (value & 0x40000) != 0 ? SIFSignal::RequestIOP : SIFSignal::None};
  }
  throw std::out_of_range("Unsupported SIF register.");
}

SIFWriteResult SIF::writeIOP(SIFRegister reg, std::uint32_t value)
{
  switch (reg)
  {
    case SIFRegister::MSCOM:
      return {};
    case SIFRegister::SMCOM:
      smcom = value;
      return {};
    case SIFRegister::MSFLAG:
      msflag &= ~value;
      return {};
    case SIFRegister::SMFLAG:
      smflag |= value;
      return {};
    case SIFRegister::CTRL:
    {
      const std::uint32_t selected = value & 0xf0;
      if ((value & 0xa0) != 0)
      {
        controlRegister = (controlRegister & ~UINT32_C(0xf000)) | 0x2000;
      }
      if ((controlRegister & selected) != 0)
      {
        controlRegister &= ~selected;
      }
      else
      {
        controlRegister |= selected;
      }
      return {};
    }
  }
  throw std::out_of_range("Unsupported SIF register.");
}

std::uint32_t SIF::iopDoorbell() const
{
  return iopDoorbellRegister;
}

SIFWriteResult SIF::writeIOPDoorbell(std::uint32_t value)
{
  iopDoorbellRegister = value;
  return {SIFWriteOutcome::Completed,
    (value & 2) != 0 ? SIFSignal::RequestEE : SIFSignal::None};
}

std::uint32_t SIF::control() const
{
  return controlRegister;
}

std::size_t SIF::directionIndex(SIFDirection direction)
{
  switch (direction)
  {
    case SIFDirection::SIF0: return 0;
    case SIFDirection::SIF1: return 1;
  }
  throw std::out_of_range("Unsupported SIF stream direction.");
}

void SIF::validateBeatSize(std::size_t count)
{
  if (count == 0 || count > 4)
  {
    throw std::out_of_range("SIF beat must contain one to four words.");
  }
}

SIFTransferOutcome SIF::pushWords(
  SIFDirection direction, const SIFQuadword &words, std::size_t count)
{
  validateBeatSize(count);
  Stream &stream = streams[directionIndex(direction)];
  if (freeWordCount() < count)
  {
    return SIFTransferOutcome::Wait;
  }
  for (std::size_t index = 0; index < count; ++index)
  {
    const std::uint8_t slot = freeHead;
    freeHead = next[slot];
    storage[slot] = words[index];
    next[slot] = END;
    if (stream.count == 0)
    {
      stream.head = slot;
    }
    else
    {
      next[stream.tail] = slot;
    }
    stream.tail = slot;
    ++stream.count;
  }
  return SIFTransferOutcome::Completed;
}

SIFTransferOutcome SIF::pushSIF1Quadword(const SIFQuadword &words)
{
  return pushWords(SIFDirection::SIF1, words, 4);
}

SIFTransferOutcome SIF::pushSIF0Prologue(const SIFQuadword &words)
{
  const auto outcome = pushWords(SIFDirection::SIF0, words, 4);
  if (outcome == SIFTransferOutcome::Completed)
  {
    packingLatch = words;
    packingCursor = 0;
  }
  return outcome;
}

SIFTransferOutcome SIF::pushSIF0PayloadWord(
  std::uint32_t word, SIFPayloadBoundary boundary)
{
  std::size_t count;
  switch (boundary)
  {
    case SIFPayloadBoundary::Continuing: count = 1; break;
    case SIFPayloadBoundary::Final: count = 4 - packingCursor; break;
    default:
      throw std::out_of_range("Unsupported SIF payload boundary.");
  }
  SIFQuadword output = {{word, 0, 0, 0}};
  for (std::size_t index = 1; index < count; ++index)
  {
    output[index] = packingLatch[packingCursor + index];
  }
  const auto outcome = pushWords(SIFDirection::SIF0, output, count);
  if (outcome == SIFTransferOutcome::Completed)
  {
    packingLatch[packingCursor] = word;
    packingCursor = (packingCursor + 1) % 4;
  }
  return outcome;
}

SIFTransferOutcome SIF::peekWords(
  SIFDirection direction, SIFQuadword &output, std::size_t count) const
{
  validateBeatSize(count);
  const Stream &stream = streams[directionIndex(direction)];
  if (stream.count < count)
  {
    return SIFTransferOutcome::Wait;
  }
  std::uint8_t slot = stream.head;
  for (std::size_t index = 0; index < count; ++index)
  {
    output[index] = storage[slot];
    slot = next[slot];
  }
  return SIFTransferOutcome::Completed;
}

SIFTransferOutcome SIF::consumeWords(SIFDirection direction, std::size_t count)
{
  validateBeatSize(count);
  Stream &stream = streams[directionIndex(direction)];
  if (stream.count < count)
  {
    return SIFTransferOutcome::Wait;
  }
  for (std::size_t index = 0; index < count; ++index)
  {
    const std::uint8_t slot = stream.head;
    stream.head = next[slot];
    next[slot] = freeHead;
    freeHead = slot;
    storage[slot] = 0;
  }
  stream.count -= count;
  if (stream.count == 0)
  {
    stream.tail = END;
  }
  return SIFTransferOutcome::Completed;
}

std::size_t SIF::wordCount(SIFDirection direction) const
{
  return streams[directionIndex(direction)].count;
}

std::size_t SIF::freeWordCount() const
{
  return WORD_CAPACITY - streams[0].count - streams[1].count;
}

std::uint32_t SIF::wordAt(SIFDirection direction, std::size_t index) const
{
  const Stream &stream = streams[directionIndex(direction)];
  if (index >= stream.count)
  {
    throw std::out_of_range("SIF inspection index must address a live word.");
  }
  std::uint8_t slot = stream.head;
  for (std::size_t offset = 0; offset < index; ++offset)
  {
    slot = next[slot];
  }
  return storage[slot];
}

const SIFQuadword &SIF::sif0PackingLatch() const
{
  return packingLatch;
}

std::size_t SIF::sif0PackingCursor() const
{
  return packingCursor;
}
