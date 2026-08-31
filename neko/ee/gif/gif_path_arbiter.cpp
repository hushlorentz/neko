#include <stdexcept>

#include "gif_path_arbiter.hpp"

GIFPathArbiter::GIFPathArbiter(GIFDecoder *decoder) :
  gifDecoder(decoder)
{
  if (gifDecoder == nullptr)
  {
    throw std::invalid_argument(
      "GIF path arbitration requires a non-null decoder.");
  }
}

bool GIFPathArbiter::requestPath(
  GIFPath path,
  bool canInterruptPath3)
{
  if (path == GIFPath::Idle)
  {
    throw std::invalid_argument(
      "The idle GIF path cannot request a transfer.");
  }
  emitEvent(GIFTraceEventType::PathRequested, path);

  if (currentPath == path)
  {
    return true;
  }

  const std::size_t requestedIndex = pathIndex(path);
  if (path == GIFPath::Path2)
  {
    if (!queuedPaths[requestedIndex])
    {
      queuedPath2CanInterruptPath3 = canInterruptPath3;
    }
    else
    {
      queuedPath2CanInterruptPath3 =
        queuedPath2CanInterruptPath3 ||
        canInterruptPath3;
    }
  }
  queuedPaths[requestedIndex] = true;
  if (currentPath == GIFPath::Idle)
  {
    selectQueuedPath();
  }
  return currentPath == path;
}

void GIFPathArbiter::setPath3MaskedByVIF(bool masked)
{
  if (vifPath3Mask == masked)
  {
    return;
  }
  vifPath3Mask = masked;
  GIFTraceEvent event;
  event.type = GIFTraceEventType::Path3MaskChanged;
  event.path = GIFPath::Path3;
  event.path3Masked = masked;
  if (traceCallback)
  {
    traceCallback(event);
  }
  if (!path3Masked() &&
      currentPath == GIFPath::Idle)
  {
    selectQueuedPath();
  }
}

void GIFPathArbiter::setPath3MaskedByMode(bool masked)
{
  if (modePath3Mask == masked)
  {
    return;
  }
  modePath3Mask = masked;
  GIFTraceEvent event;
  event.type = GIFTraceEventType::Path3MaskChanged;
  event.path = GIFPath::Path3;
  event.path3Masked = masked;
  if (traceCallback)
  {
    traceCallback(event);
  }
  if (!path3Masked() &&
      currentPath == GIFPath::Idle)
  {
    selectQueuedPath();
  }
}

void GIFPathArbiter::setPath3IntermittentMode(
  bool intermittent)
{
  intermittentPath3 = intermittent;
}

GIFPathTransferResult GIFPathArbiter::transferQuadword(
  GIFPath path,
  const GIFQuadword &quadword,
  bool canInterruptPath3)
{
  GIFPathTransferResult result;
  if (!requestPath(path, canInterruptPath3))
  {
    emitEvent(GIFTraceEventType::TransferStalled, path);
    return result;
  }

  const bool path3ImagePayload =
    path == GIFPath::Path3 &&
    !gifDecoder->awaitingTag() &&
    gifDecoder->currentTag().format == GIFDataFormat::Image;
  result.decodeResult = gifDecoder->ingestQuadword(quadword);
  result.accepted = true;
  if (traceCallback)
  {
    GIFTraceEvent event;
    event.type = GIFTraceEventType::QuadwordTransferred;
    event.path = path;
    event.quadword = quadword;
    traceCallback(event);
  }
  emitDecodeEvents(path, result.decodeResult);
  if (path == GIFPath::Path3 &&
      result.decodeResult.tagDecoded &&
      result.decodeResult.tag.format == GIFDataFormat::Image)
  {
    path3ImageSliceQuadwords = 0;
  }
  if (result.decodeResult.packetComplete)
  {
    if (path == GIFPath::Path3)
    {
      path3ImageSliceQuadwords = 0;
    }
    emitEvent(GIFTraceEventType::PathReleased, path);
    currentPath = GIFPath::Idle;
    selectQueuedPath();
  }
  else if (path3ImagePayload && intermittentPath3)
  {
    ++path3ImageSliceQuadwords;
    if (path3ImageSliceQuadwords == 8)
    {
      path3ImageSliceQuadwords = 0;
      const bool path1Waiting =
        queuedPaths[pathIndex(GIFPath::Path1)];
      const bool interruptiblePath2Waiting =
        queuedPaths[pathIndex(GIFPath::Path2)] &&
        queuedPath2CanInterruptPath3;
      if (path1Waiting || interruptiblePath2Waiting)
      {
        interruptPath3();
      }
    }
  }
  return result;
}

GIFPath GIFPathArbiter::activePath() const
{
  return currentPath;
}

bool GIFPathArbiter::pathPending(GIFPath path) const
{
  const std::size_t index = pathIndex(path);
  return
    currentPath == path ||
    queuedPaths[index];
}

bool GIFPathArbiter::pathsIdle(bool includePath3) const
{
  if (currentPath != GIFPath::Idle)
  {
    if (includePath3 || currentPath != GIFPath::Path3)
    {
      return false;
    }
  }

  const std::size_t pathCount = includePath3 ? 3 : 2;
  for (std::size_t index = 0; index < pathCount; ++index)
  {
    if (queuedPaths[index])
    {
      return false;
    }
  }
  return true;
}

bool GIFPathArbiter::path3MaskedByVIF() const
{
  return vifPath3Mask;
}

bool GIFPathArbiter::path3MaskedByMode() const
{
  return modePath3Mask;
}

bool GIFPathArbiter::path3Masked() const
{
  return vifPath3Mask || modePath3Mask;
}

bool GIFPathArbiter::path3IntermittentMode() const
{
  return intermittentPath3;
}

bool GIFPathArbiter::path3Interrupted() const
{
  return interruptedPath3;
}

bool GIFPathArbiter::pathQueued(GIFPath path) const
{
  return queuedPaths[pathIndex(path)];
}

std::uint16_t GIFPathArbiter::interruptedPath3Count() const
{
  return path3Count;
}

std::uint16_t GIFPathArbiter::interruptedPath3Tag() const
{
  return path3Tag;
}

bool GIFPathArbiter::decoderAwaitingTag() const
{
  return gifDecoder->awaitingTag();
}

bool GIFPathArbiter::decoderPacketInProgress() const
{
  return gifDecoder->packetInProgress();
}

void GIFPathArbiter::setTraceCallback(
  GIFTraceCallback callback)
{
  traceCallback = callback;
}

std::size_t GIFPathArbiter::pathIndex(GIFPath path)
{
  if (path < GIFPath::Path1 ||
      path > GIFPath::Path3)
  {
    throw std::invalid_argument(
      "Invalid GIF transfer path.");
  }
  return static_cast<std::size_t>(path) - 1;
}

void GIFPathArbiter::selectQueuedPath()
{
  for (std::size_t index = 0;
       index < queuedPaths.size();
       ++index)
  {
    if (!queuedPaths[index])
    {
      continue;
    }
    if (index == pathIndex(GIFPath::Path3) &&
        path3Masked())
    {
      continue;
    }
    if (interruptedPath3 &&
        index == pathIndex(GIFPath::Path2) &&
        !queuedPath2CanInterruptPath3)
    {
      continue;
    }

    queuedPaths[index] = false;
    currentPath =
      static_cast<GIFPath>(index + 1);
    if (currentPath == GIFPath::Path2)
    {
      queuedPath2CanInterruptPath3 = false;
    }
    if (currentPath == GIFPath::Path3 &&
        interruptedPath3)
    {
      gifDecoder->resumePacket(suspendedPath3State);
      interruptedPath3 = false;
      emitEvent(GIFTraceEventType::PathSelected, currentPath);
      emitEvent(GIFTraceEventType::Path3Resumed, currentPath);
    }
    else
    {
      emitEvent(GIFTraceEventType::PathSelected, currentPath);
    }
    return;
  }
}

void GIFPathArbiter::interruptPath3()
{
  suspendedPath3State = gifDecoder->suspendPacket();
  path3Count =
    suspendedPath3State.remainingQuadwords & 0x7fff;
  path3Tag =
    suspendedPath3State.tag.loopCount |
    (static_cast<std::uint16_t>(
      suspendedPath3State.tag.endOfPacket) << 15);
  interruptedPath3 = true;
  queuedPaths[pathIndex(GIFPath::Path3)] = true;
  emitEvent(GIFTraceEventType::Path3Interrupted, GIFPath::Path3);
  currentPath = GIFPath::Idle;
  selectQueuedPath();
}

void GIFPathArbiter::emitEvent(
  GIFTraceEventType type,
  GIFPath path)
{
  if (!traceCallback)
  {
    return;
  }
  GIFTraceEvent event;
  event.type = type;
  event.path = path;
  traceCallback(event);
}

void GIFPathArbiter::emitDecodeEvents(
  GIFPath path,
  const GIFDecodeResult &result)
{
  if (!traceCallback)
  {
    return;
  }
  if (result.tagDecoded)
  {
    GIFTraceEvent event;
    event.type = GIFTraceEventType::TagDecoded;
    event.path = path;
    event.tag = result.tag;
    traceCallback(event);
  }
  for (const GIFRegisterWrite &write : result.writes)
  {
    GIFTraceEvent event;
    event.type = GIFTraceEventType::RegisterWrite;
    event.path = path;
    event.registerWrite = write;
    traceCallback(event);
  }
  if (result.primitiveComplete)
  {
    emitEvent(GIFTraceEventType::PrimitiveComplete, path);
  }
  if (result.packetComplete)
  {
    emitEvent(GIFTraceEventType::PacketComplete, path);
  }
}
