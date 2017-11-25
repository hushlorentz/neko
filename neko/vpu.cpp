#include "vpu.hpp"

#define MEMORY_SIZE 0x3fff
#define NUM_FP_REGISTERS 32
#define NUM_INT_REGISTERS 16

VPU::VPU() : state(VPU_STATE_READY), iRegister(0), qRegister(0), rRegister(0), pRegister(0), macFlags(0), statusFlags(0), clippingFlags(0)
{
  initMemory();
  initFPRegisters();
  initIntRegisters();
}

void VPU::initMemory()
{
  microMem.resize(MEMORY_SIZE);
  vuMem.resize(MEMORY_SIZE);
}

void VPU::initFPRegisters()
{
  fpRegisters.resize(NUM_FP_REGISTERS);
  fpRegisters[0].w = 1.0f;
}

void VPU::initIntRegisters()
{
  intRegisters.resize(NUM_INT_REGISTERS);
}

uint8_t VPU::getState()
{
  return state;
}

FPRegister * VPU::fpRegisterValue(int registerID)
{
  return &fpRegisters[registerID];
}

uint16_t VPU::intRegisterValue(int registerID)
{
  return intRegisters[registerID];
}
