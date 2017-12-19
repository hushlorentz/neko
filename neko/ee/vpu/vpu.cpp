#include <thread>
#include "bit_ops.hpp"
#include "floating_point_ops.hpp"
#include "vpu.hpp"
#include "vpu_flags.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"

#define MEMORY_SIZE 0x3fff
#define NUM_FP_REGISTERS 32
#define NUM_INT_REGISTERS 16

#define NUM_TYPE1_OPCODES 12
uint16_t type1OpCodeList[NUM_TYPE1_OPCODES] = {VPU_ADD, VPU_ADDi, VPU_ADDq, VPU_ADDx, VPU_ADDy, VPU_ADDz, VPU_ADDw, VPU_ADDAx, VPU_ADDAy, VPU_ADDAz, VPU_ADDAw, VPU_MADD};

#define NUM_TYPE3_OPCODES 13
uint16_t type3OpCodeList[NUM_TYPE3_OPCODES] = {VPU_ABS, VPU_ADDA, VPU_ADDAi, VPU_ADDAq, VPU_CLIP, VPU_FTOI0, VPU_FTOI4, VPU_FTOI12, VPU_FTOI15, VPU_ITOF0, VPU_ITOF4, VPU_ITOF12, VPU_ITOF15};

using namespace std;

VPU::VPU() : useThreads(true), clippingFlags(0), state(VPU_STATE_READY), cycles(0), mode(VPU_MODE_MACRO), stepThrough(true), microMemPC(0), iRegister(0), qRegister(0), rRegister(0), pRegister(0), MACFlags(0), statusFlags(0)
{
  initMemory();
  initFPRegisters();
  initIntRegisters();
  initOpCodeSets();
  initPipelineOrchestrator();
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
  for (int i = 0; i < NUM_TYPE1_OPCODES; i++)
  {
    type1OpCodes.insert(type1OpCodeList[i]);
  }

  for (int i = 0; i < NUM_TYPE3_OPCODES; i++)
  {
    type3OpCodes.insert(type3OpCodeList[i]);
  }
}

void VPU::initPipelineOrchestrator()
{
  orchestrator.setPipelineHandler(this);
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
  fpRegisters[registerID].load(x, y, z, w);
}

void VPU::loadIntRegister(int registerID, int value)
{
  intRegisters[registerID] = value;
}

void VPU::loadAccumulator(float x, float y, float z, float w)
{
  accumulator.load(x, y, z, w);
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
      if (!orchestrator.hasNext())
      {
        state = VPU_STATE_STOP;
      }
    }
    else if (!orchestrator.stalling)
    {
      uint32_t upperInstruction = nextUpperInstruction();
      uint32_t lowerInstruction = nextUpperInstruction();

      processUpperInstruction(upperInstruction);
      microMemPC = processLowerInstruction(lowerInstruction);

      if (stopBitSet(upperInstruction))
      {
        sawStopBit = true;
      }
    }

    orchestrator.update();
    cycles++;
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
  uint16_t opCode = opCodeFromInstruction(upperInstruction);
  uint8_t srcReg1 = src1RegFromOpCodeAndInstruction(opCode, upperInstruction);
  uint8_t srcReg2 = regFromInstruction(upperInstruction, VPU_FS_REG_SHIFT);
  uint8_t fieldMask = (upperInstruction >> VPU_DEST_SHIFT) & VPU_DEST_MASK;
  uint8_t destReg = destRegFromOpCodeAndInstruction(opCode, upperInstruction);
  uint8_t src2Mask = src2MaskFromOpCodeAndInstruction(opCode, upperInstruction);

  orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, opCode, srcReg1, srcReg2, destReg, fieldMask, src2Mask); 
}

uint8_t VPU::src1RegFromOpCodeAndInstruction(uint16_t opCode, uint32_t instruction)
{
  switch (opCode)
  {
    case VPU_ABS:
    case VPU_FTOI0:
    case VPU_FTOI4:
    case VPU_FTOI12:
    case VPU_FTOI15:
    case VPU_ITOF0:
    case VPU_ITOF4:
    case VPU_ITOF12:
    case VPU_ITOF15:
      return regFromInstruction(instruction, VPU_FS_REG_SHIFT);
    default:
      return regFromInstruction(instruction, VPU_FT_REG_SHIFT);
  }
}

uint16_t VPU::opCodeFromInstruction(uint32_t instruction)
{
  if (type3OpCodes.find(instruction & VPU_TYPE3_MASK) != type3OpCodes.end())
  {
    return instruction & VPU_TYPE3_MASK;
  }
  else // if (type1OpCodes.find(instruction & VPU_TYPE1_MASK) != type1OpCodes.end())
  {
    return instruction & VPU_TYPE1_MASK;
  }
}

uint8_t VPU::regFromInstruction(uint32_t instruction, uint8_t shift)
{
  return (instruction >> shift) & VPU_REG_MASK;
}

uint8_t VPU::destRegFromOpCodeAndInstruction(uint16_t opCode, uint32_t instruction)
{
  switch (opCode)
  {
    case VPU_ADDA:
    case VPU_ADDAi:
    case VPU_ADDAq:
      return VPU_REGISTER_ACCUMULATOR;
    case VPU_ABS:
    case VPU_FTOI0:
    case VPU_FTOI4:
    case VPU_FTOI12:
    case VPU_FTOI15:
    case VPU_ITOF0:
    case VPU_ITOF4:
    case VPU_ITOF12:
    case VPU_ITOF15:
      return regFromInstruction(instruction, VPU_FT_REG_SHIFT);
    default:
      return regFromInstruction(instruction, VPU_FD_REG_SHIFT);
  }
}

uint8_t VPU::src2MaskFromOpCodeAndInstruction(uint16_t opCode, uint32_t upperInstruction)
{
  switch (opCode)
  {
    case VPU_ADDx:
    case VPU_ADDAx:
      return FP_REGISTER_X_FIELD;
    case VPU_ADDy:
    case VPU_ADDAy:
      return FP_REGISTER_Y_FIELD;
    case VPU_ADDz:
    case VPU_ADDAz:
      return FP_REGISTER_Z_FIELD;
    case VPU_ADDw:
    case VPU_ADDAw:
      return FP_REGISTER_W_FIELD;
    default:
      return 0;
  }
}

uint16_t VPU::processLowerInstruction(uint32_t lowerInstruction)
{
  return microMemPC + 8;
}

bool VPU::stopBitSet(uint32_t instruction)
{
  return hasFlag(instruction, VPU_E_BIT) ||
          hasFlag(instruction, VPU_D_BIT) ||
          hasFlag(instruction, VPU_T_BIT);
}

void VPU::updateDestinationRegisterWithPipelineResult(FPRegister * destReg, Pipeline * p)
{
  destReg->copyFrom(&(p->fpResult));
}

bool VPU::hasMACFlag(uint16_t flag)
{
  return hasFlag(MACFlags, flag);
}

bool VPU::hasStatusFlag(uint16_t flag)
{
  return hasFlag(statusFlags, flag);
}

void VPU::setFlags(FPRegister * reg)
{
  setMACFlagsFromRegister(reg);
  setStatusFlagsFromMACFlags();
  setStickyFlagsFromStatusFlags();
}

void VPU::setMACFlagsFromRegister(FPRegister * reg)
{
  (reg->x == 0) ? setFlag(MACFlags, VPU_FLAG_ZX) : unsetFlag(MACFlags, VPU_FLAG_ZX);
  (reg->y == 0) ? setFlag(MACFlags, VPU_FLAG_ZY) : unsetFlag(MACFlags, VPU_FLAG_ZY);
  (reg->z == 0) ? setFlag(MACFlags, VPU_FLAG_ZZ) : unsetFlag(MACFlags, VPU_FLAG_ZZ);
  (reg->w == 0) ? setFlag(MACFlags, VPU_FLAG_ZW) : unsetFlag(MACFlags, VPU_FLAG_ZW);
  (reg->x < 0) ? setFlag(MACFlags, VPU_FLAG_SX) : unsetFlag(MACFlags, VPU_FLAG_SX);
  (reg->y < 0) ? setFlag(MACFlags, VPU_FLAG_SY) : unsetFlag(MACFlags, VPU_FLAG_SY);
  (reg->z < 0) ? setFlag(MACFlags, VPU_FLAG_SZ) : unsetFlag(MACFlags, VPU_FLAG_SZ);
  (reg->w < 0) ? setFlag(MACFlags, VPU_FLAG_SW) : unsetFlag(MACFlags, VPU_FLAG_SW);
  hasFlag(reg->xResultFlags, FP_FLAG_OVERFLOW) ? setFlag(MACFlags, VPU_FLAG_OX) : unsetFlag(MACFlags, VPU_FLAG_OX);
  hasFlag(reg->yResultFlags, FP_FLAG_OVERFLOW) ? setFlag(MACFlags, VPU_FLAG_OY) : unsetFlag(MACFlags, VPU_FLAG_OY);
  hasFlag(reg->zResultFlags, FP_FLAG_OVERFLOW) ? setFlag(MACFlags, VPU_FLAG_OZ) : unsetFlag(MACFlags, VPU_FLAG_OZ);
  hasFlag(reg->wResultFlags, FP_FLAG_OVERFLOW) ? setFlag(MACFlags, VPU_FLAG_OW) : unsetFlag(MACFlags, VPU_FLAG_OW);
}

void VPU::setStatusFlagsFromMACFlags()
{
  ((MACFlags & VPU_Z_BITS_MASK) > 0) ? setFlag(statusFlags, VPU_FLAG_Z) : unsetFlag(statusFlags, VPU_FLAG_Z);
  ((MACFlags & VPU_S_BITS_MASK) > 0) ? setFlag(statusFlags, VPU_FLAG_S) : unsetFlag(statusFlags, VPU_FLAG_S);
  ((MACFlags & VPU_O_BITS_MASK) > 0) ? setFlag(statusFlags, VPU_FLAG_O) : unsetFlag(statusFlags, VPU_FLAG_O);
}

void VPU::setStickyFlagsFromStatusFlags()
{
  if (hasFlag(statusFlags, VPU_FLAG_Z) || hasFlag(statusFlags, VPU_FLAG_Z_STICKY))
  {
    setFlag(statusFlags, VPU_FLAG_Z_STICKY);
  }

  if (hasFlag(statusFlags, VPU_FLAG_S) || hasFlag(statusFlags, VPU_FLAG_S_STICKY))
  {
    setFlag(statusFlags, VPU_FLAG_S_STICKY);
  }
}

void VPU::loadIRegister(float value)
{
  iRegister = value;
}

void VPU::loadQRegister(float value)
{
  qRegister = value;
}

void VPU::updateClippingFlags(uint32_t clip)
{
  clippingFlags <<= VPU_CLIPPING_FLAG_SHIFT;
  clippingFlags |= (clip & VPU_CLIP_MASK);
}

int VPU::calculateNewClippingFlags(FPRegister * fsReg, FPRegister * ftReg)
{
  int newClipFlags = 0;
  if (fsReg->x > abs(ftReg->w))
  {
    newClipFlags |= VPU_CLIP_FLAG_POS_X;
  }
  else if (fsReg->x < -abs(ftReg->w))
  {
    newClipFlags |= VPU_CLIP_FLAG_NEG_X;
  }
  if (fsReg->y > abs(ftReg->w))
  {
    newClipFlags |= VPU_CLIP_FLAG_POS_Y;
  }
  else if (fsReg->y < -abs(ftReg->w))
  {
    newClipFlags |= VPU_CLIP_FLAG_NEG_Y;
  }
  if (fsReg->z > abs(ftReg->w))
  {
    newClipFlags |= VPU_CLIP_FLAG_POS_Z;
  }
  else if (fsReg->z < -abs(ftReg->w))
  {
    newClipFlags |= VPU_CLIP_FLAG_NEG_Z;
  }

  return newClipFlags;
}

void VPU::pipelineStarted(Pipeline * p)
{
  uint16_t opCode = p->opCode;
  uint8_t ft = p->srcReg1;
  uint8_t fs = p->srcReg2;
  uint8_t fieldMask = p->destFieldMask;
  FPRegister *destReg = destinationRegisterFromPipeline(p);
  FPRegister dest(destReg->x, destReg->y, destReg->z, destReg->w);

  switch (opCode)
  {
    case VPU_ABS:
      dest.storeAbs(&fpRegisters[fs], fieldMask);
      break;
    case VPU_ADD:
      dest.storeAdd(&fpRegisters[ft], &fpRegisters[fs], fieldMask, &MACFlags);
      break;
    case VPU_ADDi:
      dest.storeAddFloat(&fpRegisters[fs], iRegister, fieldMask, &MACFlags);
      break;
    case VPU_ADDq:
      dest.storeAddFloat(&fpRegisters[fs], qRegister, fieldMask, &MACFlags);
      break;
    case VPU_ADDx:
    case VPU_ADDAx:
      dest.storeAddFloat(&fpRegisters[fs], fpRegisters[ft].x, fieldMask, &MACFlags);
      break;
    case VPU_ADDy:
    case VPU_ADDAy:
      dest.storeAddFloat(&fpRegisters[fs], fpRegisters[ft].y, fieldMask, &MACFlags);
      break;
    case VPU_ADDz:
    case VPU_ADDAz:
      dest.storeAddFloat(&fpRegisters[fs], fpRegisters[ft].z, fieldMask, &MACFlags);
      break;
    case VPU_ADDw:
    case VPU_ADDAw:
      dest.storeAddFloat(&fpRegisters[fs], fpRegisters[ft].w, fieldMask, &MACFlags);
      break;
    case VPU_ADDA:
      dest.storeAdd(&fpRegisters[ft], &fpRegisters[fs], fieldMask, &MACFlags);
      break;
    case VPU_ADDAi:
      dest.storeAddFloat(&fpRegisters[fs], iRegister, fieldMask, &MACFlags);
      break;
    case VPU_ADDAq:
      dest.storeAddFloat(&fpRegisters[fs], qRegister, fieldMask, &MACFlags);
      break;
    case VPU_CLIP:
      p->setIntResult(calculateNewClippingFlags(&fpRegisters[ft], &fpRegisters[fs]));
      break;
    case VPU_FTOI0:
      dest.toInt0(&fpRegisters[fs], fieldMask);
      break;
    case VPU_FTOI4:
      dest.toInt4(&fpRegisters[fs], fieldMask);
      break;
    case VPU_FTOI12:
      dest.toInt12(&fpRegisters[fs], fieldMask);
      break;
    case VPU_FTOI15:
      dest.toInt15(&fpRegisters[fs], fieldMask);
      break;
    case VPU_ITOF0:
      dest.toFloat0(&fpRegisters[fs], fieldMask);
      break;
    case VPU_ITOF4:
      dest.toFloat4(&fpRegisters[fs], fieldMask);
      break;
    case VPU_ITOF12:
      dest.toFloat12(&fpRegisters[fs], fieldMask);
      break;
    case VPU_ITOF15:
      dest.toFloat15(&fpRegisters[fs], fieldMask);
      break;
    case VPU_MADD:
      dest.storeMul(&fpRegisters[ft], &fpRegisters[fs], fieldMask, &MACFlags);
      break;
  }

  p->setFPRegisterResult(&dest);
}

FPRegister * VPU::destinationRegisterFromPipeline(Pipeline * p)
{
  FPRegister *destReg;

  switch (p->opCode)
  {
    case VPU_ADDA:
    case VPU_ADDAi:
    case VPU_ADDAq:
    case VPU_ADDAx:
    case VPU_ADDAy:
    case VPU_ADDAz:
    case VPU_ADDAw:
      destReg =  &accumulator;
      break;
    default:
      destReg = &fpRegisters[p->destReg];
      break;
  }

  return destReg;
}

void VPU::pipelineFinished(Pipeline * p)
{
  FPRegister *destReg = destinationRegisterFromPipeline(p);

  switch (p->opCode)
  {
    case VPU_CLIP:
      updateClippingFlags(p->intResult);
      break;
    case VPU_ABS:
    case VPU_FTOI0:
    case VPU_FTOI4:
    case VPU_FTOI12:
    case VPU_FTOI15:
    case VPU_ITOF0:
    case VPU_ITOF4:
    case VPU_ITOF12:
    case VPU_ITOF15:
      updateDestinationRegisterWithPipelineResult(destReg, p);
      break;
    case VPU_MADD:
      updateDestinationRegisterWithPipelineResult(destReg, p);
      setFlags(destReg);
      destReg->storeAdd(&(p->fpResult), &accumulator, p->destFieldMask, &MACFlags);
      setFlags(destReg);
      break;
    default:
      updateDestinationRegisterWithPipelineResult(destReg, p);
      setFlags(destReg);
      break;
  }
}
