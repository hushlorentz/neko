#include <stdexcept>

#include "gif_path3.hpp"

GIFPath3Transfer::GIFPath3Transfer(GIFDecoder *decoder) :
  ownedArbiter(
    decoder == nullptr
      ? nullptr
      : new GIFPathArbiter(decoder)),
  gifPathArbiter(ownedArbiter.get())
{
  if (gifPathArbiter == nullptr)
  {
    throw std::invalid_argument(
      "GIF PATH3 requires a non-null decoder.");
  }
}

GIFPath3Transfer::GIFPath3Transfer(
  GIFPathArbiter &arbiter) :
  gifPathArbiter(&arbiter)
{
}

GIFPath3SubmissionResult GIFPath3Transfer::submitQuadwords(
  const GIFQuadword *quadwords,
  std::size_t count)
{
  if (quadwords == nullptr && count != 0)
  {
    throw std::invalid_argument(
      "GIF PATH3 requires non-null qword input.");
  }

  GIFPath3SubmissionResult result;
  for (std::size_t index = 0; index < count; ++index)
  {
    ++submissionAttempts;
    const GIFPathTransferResult transfer =
      gifPathArbiter->transferQuadword(
        GIFPath::Path3,
        quadwords[index]);
    if (!transfer.accepted)
    {
      result.stalled = true;
      break;
    }

    ++result.transferredQuadwords;
    ++transferredQuadwords;
    result.lastDecodeResult = transfer.decodeResult;
    if (transfer.decodeResult.packetComplete)
    {
      result.packetComplete = true;
      ++completedPackets;
    }
  }
  return result;
}

bool GIFPath3Transfer::submitGuestQuadword(
  const GIFQuadword &quadword)
{
  constexpr std::size_t GIF_FIFO_CAPACITY = 16;
  if (guestFIFO.size() == GIF_FIFO_CAPACITY)
  {
    return false;
  }
  guestFIFO.push_back(quadword);
  return true;
}

void GIFPath3Transfer::advanceGuestFIFO()
{
  while (!guestFIFO.empty())
  {
    const GIFPath3SubmissionResult result =
      submitQuadwords(&guestFIFO.front(), 1);
    if (result.transferredQuadwords == 0)
    {
      return;
    }
    guestFIFO.pop_front();
  }
}

std::size_t GIFPath3Transfer::guestFIFOQuadwordCount() const
{
  return guestFIFO.size();
}

std::uint64_t GIFPath3Transfer::submissionAttemptCount() const
{
  return submissionAttempts;
}

std::uint64_t GIFPath3Transfer::transferredQuadwordCount() const
{
  return transferredQuadwords;
}

std::uint64_t GIFPath3Transfer::completedPacketCount() const
{
  return completedPackets;
}
