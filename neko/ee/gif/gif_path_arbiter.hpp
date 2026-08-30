#ifndef GIF_PATH_ARBITER_HPP
#define GIF_PATH_ARBITER_HPP

#include <array>
#include <cstdint>

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

class GIFPathArbiter
{
  public:
    explicit GIFPathArbiter(GIFDecoder *decoder);

    bool requestPath(GIFPath path);
    void setPath3MaskedByVIF(bool masked);
    GIFPathTransferResult transferQuadword(
      GIFPath path,
      const GIFQuadword &quadword);

    GIFPath activePath() const;
    bool pathPending(GIFPath path) const;
    bool pathsIdle(bool includePath3 = true) const;
    bool path3MaskedByVIF() const;
    bool decoderAwaitingTag() const;
    bool decoderPacketInProgress() const;

  private:
    static std::size_t pathIndex(GIFPath path);
    void selectQueuedPath();

    GIFDecoder *gifDecoder;
    GIFPath currentPath = GIFPath::Idle;
    std::array<bool, 3> queuedPaths = {};
    bool vifPath3Mask = false;
};

#endif
