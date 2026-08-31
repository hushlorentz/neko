#include "gif_diagnostics.hpp"

#include <ostream>
#include <stdexcept>

namespace
{
  std::size_t pathIndex(GIFPath path)
  {
    if (path < GIFPath::Path1 ||
        path > GIFPath::Path3)
    {
      throw std::invalid_argument(
        "GIF diagnostic event requires a transfer path.");
    }
    return static_cast<std::size_t>(path) - 1;
  }

  const char *pathName(GIFPath path)
  {
    switch (path)
    {
      case GIFPath::Idle:
        return "idle";
      case GIFPath::Path1:
        return "path1";
      case GIFPath::Path2:
        return "path2";
      case GIFPath::Path3:
        return "path3";
    }
    throw std::invalid_argument("Unknown GIF path.");
  }

  const char *eventTypeName(GIFTraceEventType type)
  {
    switch (type)
    {
      case GIFTraceEventType::PathRequested:
        return "path_requested";
      case GIFTraceEventType::PathSelected:
        return "path_selected";
      case GIFTraceEventType::TransferStalled:
        return "transfer_stalled";
      case GIFTraceEventType::QuadwordTransferred:
        return "quadword_transferred";
      case GIFTraceEventType::TagDecoded:
        return "tag_decoded";
      case GIFTraceEventType::RegisterWrite:
        return "register_write";
      case GIFTraceEventType::PrimitiveComplete:
        return "primitive_complete";
      case GIFTraceEventType::PacketComplete:
        return "packet_complete";
      case GIFTraceEventType::PathReleased:
        return "path_released";
      case GIFTraceEventType::Path3MaskChanged:
        return "path3_mask_changed";
      case GIFTraceEventType::Path3Interrupted:
        return "path3_interrupted";
      case GIFTraceEventType::Path3Resumed:
        return "path3_resumed";
    }
    throw std::invalid_argument("Unknown GIF trace event type.");
  }

  void writePathCounts(
    std::ostream &output,
    const std::array<std::uint64_t, 3> &counts)
  {
    output
      << "{\"path1\":" << counts[0]
      << ",\"path2\":" << counts[1]
      << ",\"path3\":" << counts[2]
      << "}";
  }

  void writeDescriptors(
    std::ostream &output,
    const GIFTag &tag)
  {
    output << "[";
    for (std::uint8_t index = 0;
         index < tag.registerCount;
         ++index)
    {
      if (index != 0)
      {
        output << ",";
      }
      output << ((tag.registers >> (index * 4)) & 0x0f);
    }
    output << "]";
  }
}

GIFDiagnosticsRecorder::GIFDiagnosticsRecorder(
  bool captureEvents) :
  capture(captureEvents)
{
}

void GIFDiagnosticsRecorder::observe(
  const GIFTraceEvent &event)
{
  if (capture)
  {
    capturedEvents.push_back(event);
  }

  switch (event.type)
  {
    case GIFTraceEventType::PathRequested:
      ++transferSummary.pathRequests[pathIndex(event.path)];
      break;
    case GIFTraceEventType::PathSelected:
      ++transferSummary.pathSelections[pathIndex(event.path)];
      break;
    case GIFTraceEventType::TransferStalled:
      ++transferSummary.stalledTransfers[pathIndex(event.path)];
      break;
    case GIFTraceEventType::QuadwordTransferred:
      ++transferSummary.transferredQuadwords[pathIndex(event.path)];
      break;
    case GIFTraceEventType::TagDecoded:
      ++transferSummary.decodedTags;
      break;
    case GIFTraceEventType::RegisterWrite:
      ++transferSummary.registerWrites;
      break;
    case GIFTraceEventType::PrimitiveComplete:
      ++transferSummary.completedPrimitives;
      break;
    case GIFTraceEventType::PacketComplete:
      ++transferSummary.completedPackets;
      break;
    case GIFTraceEventType::PathReleased:
      ++transferSummary.pathReleases;
      break;
    case GIFTraceEventType::Path3MaskChanged:
      ++transferSummary.path3MaskChanges;
      break;
    case GIFTraceEventType::Path3Interrupted:
      ++transferSummary.path3Interruptions;
      break;
    case GIFTraceEventType::Path3Resumed:
      ++transferSummary.path3Resumptions;
      break;
  }
}

const GIFTransferSummary &GIFDiagnosticsRecorder::summary() const
{
  return transferSummary;
}

const std::vector<GIFTraceEvent> &
GIFDiagnosticsRecorder::events() const
{
  return capturedEvents;
}

void writeGIFTraceEventJsonLine(
  std::ostream &output,
  const GIFTraceEvent &event)
{
  output
    << "{\"type\":\"" << eventTypeName(event.type)
    << "\",\"path\":\"" << pathName(event.path) << "\"";

  if (event.type == GIFTraceEventType::TagDecoded)
  {
    output
      << ",\"tag\":{\"nloop\":" << event.tag.loopCount
      << ",\"eop\":"
      << (event.tag.endOfPacket ? "true" : "false")
      << ",\"pre\":"
      << (event.tag.primitiveEnabled ? "true" : "false")
      << ",\"prim\":" << event.tag.primitive
      << ",\"flg\":"
      << static_cast<unsigned int>(event.tag.format)
      << ",\"nreg\":"
      << static_cast<unsigned int>(event.tag.registerCount)
      << ",\"registers\":" << event.tag.registers
      << ",\"descriptors\":";
    writeDescriptors(output, event.tag);
    output << "}";
  }
  else if (event.type == GIFTraceEventType::QuadwordTransferred)
  {
    output
      << ",\"words\":["
      << event.quadword[0] << ","
      << event.quadword[1] << ","
      << event.quadword[2] << ","
      << event.quadword[3] << "]";
  }
  else if (event.type == GIFTraceEventType::RegisterWrite)
  {
    output
      << ",\"address\":"
      << static_cast<unsigned int>(event.registerWrite.address)
      << ",\"data\":" << event.registerWrite.data;
  }
  else if (event.type == GIFTraceEventType::Path3MaskChanged)
  {
    output
      << ",\"masked\":"
      << (event.path3Masked ? "true" : "false");
  }
  output << "}\n";
}

void writeGIFTransferSummaryJsonLine(
  std::ostream &output,
  const GIFTransferSummary &summary)
{
  output << "{\"type\":\"gif_transfer_summary\",\"requests\":";
  writePathCounts(output, summary.pathRequests);
  output << ",\"selections\":";
  writePathCounts(output, summary.pathSelections);
  output << ",\"transferred_qwords\":";
  writePathCounts(output, summary.transferredQuadwords);
  output << ",\"stalled_transfers\":";
  writePathCounts(output, summary.stalledTransfers);
  output
    << ",\"decoded_tags\":" << summary.decodedTags
    << ",\"register_writes\":" << summary.registerWrites
    << ",\"completed_primitives\":"
    << summary.completedPrimitives
    << ",\"completed_packets\":" << summary.completedPackets
    << ",\"path_releases\":" << summary.pathReleases
    << ",\"path3_mask_changes\":"
    << summary.path3MaskChanges
    << ",\"path3_interruptions\":"
    << summary.path3Interruptions
    << ",\"path3_resumptions\":"
    << summary.path3Resumptions
    << "}\n";
}
