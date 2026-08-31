#ifndef GIF_PATH3_HPP
#define GIF_PATH3_HPP

#include <cstddef>
#include <cstdint>
#include <memory>

#include "gif_path_arbiter.hpp"

struct GIFPath3SubmissionResult
{
  std::size_t transferredQuadwords = 0;
  bool stalled = false;
  bool packetComplete = false;
  GIFDecodeResult lastDecodeResult;
};

class GIFPath3Transfer
{
  public:
    explicit GIFPath3Transfer(GIFDecoder *decoder);
    explicit GIFPath3Transfer(GIFPathArbiter &arbiter);

    GIFPath3SubmissionResult submitQuadwords(
      const GIFQuadword *quadwords,
      std::size_t count);

    std::uint64_t submissionAttemptCount() const;
    std::uint64_t transferredQuadwordCount() const;
    std::uint64_t completedPacketCount() const;

  private:
    std::unique_ptr<GIFPathArbiter> ownedArbiter;
    GIFPathArbiter *gifPathArbiter;
    std::uint64_t submissionAttempts = 0;
    std::uint64_t transferredQuadwords = 0;
    std::uint64_t completedPackets = 0;
};

#endif
