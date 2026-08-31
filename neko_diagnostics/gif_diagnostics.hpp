#ifndef GIF_DIAGNOSTICS_HPP
#define GIF_DIAGNOSTICS_HPP

#include <array>
#include <cstdint>
#include <iosfwd>
#include <vector>

#include "gif_path_arbiter.hpp"

struct GIFTransferSummary
{
  std::array<std::uint64_t, 3> pathRequests = {};
  std::array<std::uint64_t, 3> pathSelections = {};
  std::array<std::uint64_t, 3> transferredQuadwords = {};
  std::array<std::uint64_t, 3> stalledTransfers = {};
  std::uint64_t decodedTags = 0;
  std::uint64_t registerWrites = 0;
  std::uint64_t completedPrimitives = 0;
  std::uint64_t completedPackets = 0;
  std::uint64_t pathReleases = 0;
  std::uint64_t path3MaskChanges = 0;
};

class GIFDiagnosticsRecorder
{
  public:
    explicit GIFDiagnosticsRecorder(bool captureEvents = true);

    void observe(const GIFTraceEvent &event);
    const GIFTransferSummary &summary() const;
    const std::vector<GIFTraceEvent> &events() const;

  private:
    bool capture;
    GIFTransferSummary transferSummary;
    std::vector<GIFTraceEvent> capturedEvents;
};

void writeGIFTraceEventJsonLine(
  std::ostream &output,
  const GIFTraceEvent &event);
void writeGIFTransferSummaryJsonLine(
  std::ostream &output,
  const GIFTransferSummary &summary);

#endif
