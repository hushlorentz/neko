#include "neko_system.hpp"

NekoSystem::NekoSystem() :
  vu0Component(VPUType::VU0),
  vu1Component(VPUType::VU1),
  vif0Component(VIFType::VIF0),
  vif1Component(VIFType::VIF1),
  gifPathArbiterComponent(&gifDecoderComponent),
  gifPath1Component(gifPathArbiterComponent),
  gifPath3Component(gifPathArbiterComponent)
{
  vif0Component.attachVPU(&vu0Component);
  vif1Component.attachVPU(&vu1Component);
  vif1Component.attachGIFPathArbiter(&gifPathArbiterComponent);
  gifPath1Component.attachVPU(&vu1Component);
  gifDecoderComponent.attachRegisterWriteHandler(&gsComponent);
  masterClock.registerComponent(
    vu0Component,
    VU_CLOCK_PERIOD);
  masterClock.registerComponent(
    vu1Component,
    VU_CLOCK_PERIOD);
}

VPU &NekoSystem::vu0()
{
  return vu0Component;
}

const VPU &NekoSystem::vu0() const
{
  return vu0Component;
}

VPU &NekoSystem::vu1()
{
  return vu1Component;
}

const VPU &NekoSystem::vu1() const
{
  return vu1Component;
}

VIF &NekoSystem::vif0()
{
  return vif0Component;
}

const VIF &NekoSystem::vif0() const
{
  return vif0Component;
}

VIF &NekoSystem::vif1()
{
  return vif1Component;
}

const VIF &NekoSystem::vif1() const
{
  return vif1Component;
}

GIFDecoder &NekoSystem::gifDecoder()
{
  return gifDecoderComponent;
}

const GIFDecoder &NekoSystem::gifDecoder() const
{
  return gifDecoderComponent;
}

GIFPathArbiter &NekoSystem::gifPathArbiter()
{
  return gifPathArbiterComponent;
}

const GIFPathArbiter &NekoSystem::gifPathArbiter() const
{
  return gifPathArbiterComponent;
}

GIFPath1Transfer &NekoSystem::gifPath1()
{
  return gifPath1Component;
}

const GIFPath1Transfer &NekoSystem::gifPath1() const
{
  return gifPath1Component;
}

GIFPath3Transfer &NekoSystem::gifPath3()
{
  return gifPath3Component;
}

const GIFPath3Transfer &NekoSystem::gifPath3() const
{
  return gifPath3Component;
}

GS &NekoSystem::gs()
{
  return gsComponent;
}

const GS &NekoSystem::gs() const
{
  return gsComponent;
}

MasterClockScheduler &NekoSystem::masterClockScheduler()
{
  return masterClock;
}

const MasterClockScheduler &
NekoSystem::masterClockScheduler() const
{
  return masterClock;
}
