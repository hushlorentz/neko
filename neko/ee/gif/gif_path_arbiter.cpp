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

bool GIFPathArbiter::requestPath(GIFPath path)
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

  queuedPaths[pathIndex(path)] = true;
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
  if (!vifPath3Mask &&
      currentPath == GIFPath::Idle)
  {
    selectQueuedPath();
  }
}

GIFPathTransferResult GIFPathArbiter::transferQuadword(
  GIFPath path,
  const GIFQuadword &quadword)
{
  GIFPathTransferResult result;
  if (!requestPath(path))
  {
    emitEvent(GIFTraceEventType::TransferStalled, path);
    return result;
  }

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
  if (result.decodeResult.packetComplete)
  {
    emitEvent(GIFTraceEventType::PathReleased, path);
    currentPath = GIFPath::Idle;
    selectQueuedPath();
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
        vifPath3Mask)
    {
      continue;
    }

    queuedPaths[index] = false;
    currentPath =
      static_cast<GIFPath>(index + 1);
    emitEvent(GIFTraceEventType::PathSelected, currentPath);
    return;
  }
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
