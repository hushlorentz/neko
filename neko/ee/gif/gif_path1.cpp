#include <stdexcept>

#include "gif_path1.hpp"
#include "vpu.hpp"

namespace
{
  constexpr std::size_t GIF_QUADWORD_BYTES = 16;
}

GIFPath1Transfer::GIFPath1Transfer(GIFDecoder *decoder) :
  gifDecoder(decoder)
{
  if (gifDecoder == nullptr)
  {
    throw std::invalid_argument(
      "GIF PATH1 requires a non-null decoder.");
  }
}

void GIFPath1Transfer::attachVPU(VPU *attachedVPU)
{
  if (active)
  {
    throw std::runtime_error(
      "Cannot attach a VPU during a GIF PATH1 transfer.");
  }
  if (attachedVPU == nullptr)
  {
    throw std::invalid_argument(
      "Cannot attach a null VPU to GIF PATH1.");
  }
  if (attachedVPU->unitType() != VPUType::VU1)
  {
    throw std::invalid_argument(
      "GIF PATH1 requires VU1.");
  }
  if (vpu != nullptr && vpu != attachedVPU)
  {
    throw std::runtime_error(
      "GIF PATH1 is already attached to a VU1.");
  }

  vpu = attachedVPU;
  vpu->setXGKICKHandler(this);
}

bool GIFPath1Transfer::path1TransferActive() const
{
  return active;
}

void GIFPath1Transfer::startPath1Transfer(
  std::uint16_t startQwordAddress)
{
  if (vpu == nullptr)
  {
    throw std::runtime_error(
      "GIF PATH1 requires an attached VU1.");
  }
  if (active)
  {
    throw std::runtime_error(
      "GIF PATH1 transfer is already active.");
  }
  if (!gifDecoder->awaitingTag() ||
      gifDecoder->packetInProgress())
  {
    throw std::runtime_error(
      "GIF PATH1 must start at a GIF packet boundary.");
  }

  const std::size_t qwordCount =
    vpu->dataMemorySize() / GIF_QUADWORD_BYTES;
  qwordAddress = startQwordAddress % qwordCount;
  transferredQuadwords = 0;
  active = true;
}

void GIFPath1Transfer::advancePath1Transfer()
{
  if (!active)
  {
    return;
  }

  const GIFDecodeResult result =
    gifDecoder->ingestQuadword(
      vpu->readDataQuadword(qwordAddress));
  const std::size_t qwordCount =
    vpu->dataMemorySize() / GIF_QUADWORD_BYTES;
  qwordAddress = (qwordAddress + 1) % qwordCount;
  ++transferredQuadwords;
  if (result.packetComplete)
  {
    active = false;
  }
}

std::uint16_t GIFPath1Transfer::currentQwordAddress() const
{
  return qwordAddress;
}

std::uint64_t GIFPath1Transfer::transferredQuadwordCount() const
{
  return transferredQuadwords;
}
