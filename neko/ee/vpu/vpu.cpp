#include <thread>
#include "bit_ops.hpp"
#include "floating_point_ops.hpp"
#include "vpu.hpp"
#include "vpu_opcodes.hpp"

#define MEMORY_SIZE 0x3fff
#define NUM_FP_REGISTERS 32
#define NUM_INT_REGISTERS 16

#define VPU_PIPELINE_TYPE_NONE 0
#define VPU_PIPELINE_TYPE_FMAC 1
#define VPU_PIPELINE_FMAC_LATENCY 6
#define VPU_PIPELINE_FMAC_STALL 3
#define VPU_PIPELINE_FDIV 2
#define VPU_PIPELINE_EFU 3
#define VPU_PIPELINE_IALU 4
#define VPU_PIPELINE_XGKICK 5

using namespace std;

VPU::VPU() : useThreads(true), state(VPU_STATE_READY), cycles(0), mode(VPU_MODE_MACRO), stepThrough(true), microMemPC(0), iRegister(0), qRegister(0), rRegister(0), pRegister(0), macFlags(0), statusFlags(0), clippingFlags(0), previousUpperInstructionType(VPU_PIPELINE_TYPE_NONE), previousLowerInstructionType(VPU_PIPELINE_TYPE_NONE)
{
  initMemory();
  initFPRegisters();
  initIntRegisters();
  initOpCodeSets();
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

void VPU::initOpCodeSets()
{
  type3OpCodes.insert(VPU_ABS);
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

void VPU::loadFPRegister(int registerID, float x, float y, float z, float w)
{
  fpRegisters[registerID].x = x;
  fpRegisters[registerID].y = y;
  fpRegisters[registerID].z = z;
  fpRegisters[registerID].w = w;
}

void VPU::loadIntRegister(int registerID, int value)
{
  intRegisters[registerID] = value;
}

void VPU::resetCycles()
{
  cycles = 0;
}

uint32_t VPU::elapsedCycles()
{
  return cycles;
}

void VPU::setMode(uint8_t newMode)
{
  mode = newMode;
}

void VPU::uploadMicroInstructions(vector<uint8_t> * instructions)
{
  copy(instructions->begin(), instructions->end(), microMem.begin());
  microMemPC = 0;
}

void VPU::initMicroMode()
{
  mode = VPU_MODE_MICRO;
  state = VPU_STATE_RUN;

  if (useThreads)
  {
    thread t;
  }
  else
  {
    executeMicroInstructions();
  }
}

void VPU::executeMicroInstructions()
{
  bool sawStopBit = false;

  while (state == VPU_STATE_RUN)
  {
    if (sawStopBit)
    {
      state = VPU_STATE_STOP;
    }

    uint32_t upperInstruction = nextUpperInstruction();
    uint32_t lowerInstruction = nextUpperInstruction();

    processUpperInstruction(upperInstruction);
    microMemPC = processLowerInstruction(lowerInstruction);

    if (stopBitSet(upperInstruction))
    {
      sawStopBit = true;
    }

    incrementCyclesCount(upperInstruction, lowerInstruction);
  }
}

uint32_t VPU::nextUpperInstruction()
{
  uint32_t instruction = 0;
  uint8_t shift = 24;

  for (vector<uint8_t>::iterator it = microMem.begin() + microMemPC; it < microMem.begin() + (microMemPC + 4); ++it)
  {
    instruction |= *it << shift;
    shift -= 8;
  }

  return instruction;
}

uint32_t VPU::nextLowerInstruction()
{
  return 0;
}

void VPU::processUpperInstruction(uint32_t upperInstruction)
{
  if (type3OpCodes.find(upperInstruction & VPU_TYPE3_MASK) != type3OpCodes.end())
  {
    processUpperType3Instruction(upperInstruction);
  }
}

void VPU::processUpperType3Instruction(uint32_t upperInstruction)
{
  uint16_t opCode = upperInstruction & VPU_TYPE3_MASK;
  uint8_t ftReg = regFromInstruction(upperInstruction, VPU_FT_REG_SHIFT);
  uint8_t fsReg = regFromInstruction(upperInstruction, VPU_FS_REG_SHIFT);
  uint8_t fieldMask = (upperInstruction >> VPU_DEST_SHIFT) & VPU_DEST_MASK;

  switch (opCode)
  {
    case VPU_ABS:
      absFPRegisters(&fpRegisters[ftReg], &fpRegisters[fsReg], fieldMask);
      break;
  }
}

uint8_t VPU::regFromInstruction(uint32_t instruction, uint8_t shift)
{
  return (instruction >> shift) & VPU_REG_MASK;
}

uint16_t VPU::processLowerInstruction(uint32_t lowerInstruction)
{
  return 8;
}

bool VPU::stopBitSet(uint32_t instruction)
{
  return hasFlag(instruction, VPU_E_BIT_MASK) ||
          hasFlag(instruction, VPU_D_BIT_MASK) || 
          hasFlag(instruction, VPU_T_BIT_MASK);
}

void VPU::incrementCyclesCount(uint8_t upperInstruction, uint8_t lowerInstruction)
{
  if (previousUpperInstructionType == VPU_PIPELINE_TYPE_NONE)
  {
    cycles += VPU_PIPELINE_FMAC_LATENCY;
  }
  else
  {
    cycles++;
  }

  previousUpperInstructionType = VPU_PIPELINE_TYPE_FMAC;
}
