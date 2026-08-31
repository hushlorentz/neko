#include <stdexcept>

#include "gif_registers.hpp"

GIFRegisters::GIFRegisters(GIFPathArbiter *arbiter) :
  gifPathArbiter(arbiter)
{
  if (gifPathArbiter == nullptr)
  {
    throw std::invalid_argument(
      "GIF registers require a non-null path arbiter.");
  }
}

void GIFRegisters::writeMode(std::uint32_t value)
{
  gifPathArbiter->setPath3MaskedByMode(
    (value & GIFMode::M3R) != 0);
  gifPathArbiter->setPath3IntermittentMode(
    (value & GIFMode::IMT) != 0);
}

std::uint32_t GIFRegisters::readStatus() const
{
  std::uint32_t status = 0;
  if (gifPathArbiter->path3MaskedByMode())
  {
    status |= GIFStatus::M3R;
  }
  if (gifPathArbiter->path3MaskedByVIF())
  {
    status |= GIFStatus::M3P;
  }
  if (gifPathArbiter->path3IntermittentMode())
  {
    status |= GIFStatus::IMT;
  }
  if (gifPathArbiter->path3Interrupted())
  {
    status |= GIFStatus::IP3;
  }
  if (gifPathArbiter->pathQueued(GIFPath::Path3))
  {
    status |= GIFStatus::P3Q;
  }
  if (gifPathArbiter->pathQueued(GIFPath::Path2))
  {
    status |= GIFStatus::P2Q;
  }
  if (gifPathArbiter->pathQueued(GIFPath::Path1))
  {
    status |= GIFStatus::P1Q;
  }

  const GIFPath activePath = gifPathArbiter->activePath();
  if (activePath != GIFPath::Idle)
  {
    status |= GIFStatus::OPH;
    status |=
      static_cast<std::uint32_t>(activePath) <<
      GIFStatus::APATH_SHIFT;
  }
  return status;
}

std::uint32_t GIFRegisters::readPath3Count() const
{
  return gifPathArbiter->interruptedPath3Count();
}

std::uint32_t GIFRegisters::readPath3Tag() const
{
  return gifPathArbiter->interruptedPath3Tag();
}
