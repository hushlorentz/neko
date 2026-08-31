#ifndef GIF_PATH_ARBITER_HPP
#define GIF_PATH_ARBITER_HPP

#include <array>
#include <cstdint>
#include <functional>

#include "gif.hpp"

enum class GIFPath : std::uint8_t
{
  Idle = 0,
  Path1 = 1,
  Path2 = 2,
  Path3 = 3
};

struct GIFPathTransferResult
{
  bool accepted = false;
  GIFDecodeResult decodeResult;
};

enum class GIFTraceEventType : std::uint8_t
{
  PathRequested,
  PathSelected,
  TransferStalled,
  QuadwordTransferred,
  TagDecoded,
  RegisterWrite,
  PrimitiveComplete,
  PacketComplete,
  PathReleased,
  Path3MaskChanged,
  Path3Interrupted,
  Path3Resumed
};

struct GIFTraceEvent
{
  GIFTraceEventType type = GIFTraceEventType::PathRequested;
  GIFPath path = GIFPath::Idle;
  GIFQuadword quadword = {};
  GIFTag tag;
  GIFRegisterWrite registerWrite;
  bool path3Masked = false;
};

using GIFTraceCallback =
  std::function<void(const GIFTraceEvent &)>;

class GIFPathArbiter
{
  public:
    explicit GIFPathArbiter(GIFDecoder *decoder);

    bool requestPath(
      GIFPath path,
      bool canInterruptPath3 = true);
    void setPath3MaskedByVIF(bool masked);
    void setPath3IntermittentMode(bool intermittent);
    GIFPathTransferResult transferQuadword(
      GIFPath path,
      const GIFQuadword &quadword,
      bool canInterruptPath3 = true);

    GIFPath activePath() const;
    bool pathPending(GIFPath path) const;
    bool pathsIdle(bool includePath3 = true) const;
    bool path3MaskedByVIF() const;
    bool path3IntermittentMode() const;
    bool path3Interrupted() const;
    bool decoderAwaitingTag() const;
    bool decoderPacketInProgress() const;
    void setTraceCallback(GIFTraceCallback callback);

  private:
    static std::size_t pathIndex(GIFPath path);
    void selectQueuedPath();
    void emitEvent(
      GIFTraceEventType type,
      GIFPath path);
    void emitDecodeEvents(
      GIFPath path,
      const GIFDecodeResult &result);
    void interruptPath3();

    GIFDecoder *gifDecoder;
    GIFPath currentPath = GIFPath::Idle;
    std::array<bool, 3> queuedPaths = {};
    bool vifPath3Mask = false;
    bool intermittentPath3 = false;
    bool interruptedPath3 = false;
    bool queuedPath2CanInterruptPath3 = false;
    std::uint8_t path3ImageSliceQuadwords = 0;
    GIFDecoderState suspendedPath3State;
    GIFTraceCallback traceCallback;
};

#endif
