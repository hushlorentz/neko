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

#define NUM_TYPE1_OPCODES 52
uint16_t type1OpCodeList[NUM_TYPE1_OPCODES] = {VPU_ADD, VPU_ADDi, VPU_ADDq, VPU_ADDx, VPU_ADDy, VPU_ADDz, VPU_ADDw, VPU_ADDAx, VPU_ADDAy, VPU_ADDAz, VPU_ADDAw, VPU_MADD, VPU_MADDi, VPU_MADDq, VPU_MADDx, VPU_MADDy, VPU_MADDz, VPU_MADDw, VPU_MAX, VPU_MAXi, VPU_MAXx, VPU_MAXy, VPU_MAXz, VPU_MAXw, VPU_MINI, VPU_MINIi, VPU_MINIx, VPU_MINIy, VPU_MINIz, VPU_MINIw, VPU_MSUB, VPU_MSUBi, VPU_MSUBq, VPU_MSUBx, VPU_MSUBy, VPU_MSUBz, VPU_MSUBw, VPU_MUL, VPU_MULi, VPU_MULq, VPU_MULx, VPU_MULy, VPU_MULz, VPU_MULw, VPU_OPMSUB, VPU_SUB, VPU_SUBi, VPU_SUBq, VPU_SUBx, VPU_SUBy, VPU_SUBz, VPU_SUBw};

#define NUM_TYPE3_OPCODES 42
uint16_t type3OpCodeList[NUM_TYPE3_OPCODES] = {VPU_ABS, VPU_ADDA, VPU_ADDAi, VPU_ADDAq, VPU_CLIP, VPU_FTOI0, VPU_FTOI4, VPU_FTOI12, VPU_FTOI15, VPU_ITOF0, VPU_ITOF4, VPU_ITOF12, VPU_ITOF15, VPU_MADDA, VPU_MADDAi, VPU_MADDAq, VPU_MADDAx, VPU_MADDAy, VPU_MADDAz, VPU_MADDAw, VPU_MSUBA, VPU_MSUBAi, VPU_MSUBAq, VPU_MSUBAx, VPU_MSUBAy, VPU_MSUBAz, VPU_MSUBAw, VPU_MULA, VPU_MULAi, VPU_MULAq, VPU_MULAx, VPU_MULAy, VPU_MULAz, VPU_MULAw, VPU_OPMULA, VPU_SUBA, VPU_SUBAi, VPU_SUBAq, VPU_SUBAx, VPU_SUBAy, VPU_SUBAz, VPU_SUBAw};

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

void VPU::loadFPRegister(int registerID, double x, double y, double z, double w)
{
  fpRegisters[registerID].load(x, y, z, w);
}

void VPU::loadIntRegister(int registerID, int value)
{
  intRegisters[registerID] = value;
}

void VPU::loadAccumulator(double x, double y, double z, double w)
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
    case VPU_MADDA:
    case VPU_MADDAi:
    case VPU_MADDAq:
    case VPU_MADDAx:
    case VPU_MADDAy:
    case VPU_MADDAz:
    case VPU_MADDAw:
    case VPU_MSUBA:
    case VPU_MSUBAi:
    case VPU_MSUBAq:
    case VPU_MSUBAx:
    case VPU_MSUBAy:
    case VPU_MSUBAz:
    case VPU_MSUBAw:
    case VPU_SUBA:
    case VPU_SUBAi:
    case VPU_SUBAq:
    case VPU_SUBAx:
    case VPU_SUBAy:
    case VPU_SUBAz:
    case VPU_SUBAw:
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

void VPU::setFlags(FPRegister * reg, uint8_t ignoredFields)
{
  setMACFlagsFromRegister(reg, ignoredFields);
  setStatusFlagsFromMACFlags();
  setStickyFlagsFromStatusFlags();
}

void VPU::setMACFlagsFromRegister(FPRegister * reg, uint8_t ignoredFields)
{
  if (!hasFlag(ignoredFields, FP_REGISTER_X_FIELD))
  {
    (reg->x == 0) ? setFlag(MACFlags, VPU_FLAG_ZX) : unsetFlag(MACFlags, VPU_FLAG_ZX);
    FP_REGISTER_FIELD_IS_NEGATIVE(reg->xInt) ? setFlag(MACFlags, VPU_FLAG_SX) : unsetFlag(MACFlags, VPU_FLAG_SX);
    hasFlag(reg->xResultFlags, FP_FLAG_OVERFLOW) ? setFlag(MACFlags, VPU_FLAG_OX) : unsetFlag(MACFlags, VPU_FLAG_OX);
    hasFlag(reg->xResultFlags, FP_FLAG_UNDERFLOW) ? setFlag(MACFlags, VPU_FLAG_UX) : unsetFlag(MACFlags, VPU_FLAG_UX);
  }
  if (!hasFlag(ignoredFields, FP_REGISTER_Y_FIELD))
  {
    (reg->y == 0) ? setFlag(MACFlags, VPU_FLAG_ZY) : unsetFlag(MACFlags, VPU_FLAG_ZY);
    FP_REGISTER_FIELD_IS_NEGATIVE(reg->yInt) ? setFlag(MACFlags, VPU_FLAG_SY) : unsetFlag(MACFlags, VPU_FLAG_SY);
    hasFlag(reg->yResultFlags, FP_FLAG_OVERFLOW) ? setFlag(MACFlags, VPU_FLAG_OY) : unsetFlag(MACFlags, VPU_FLAG_OY);
    hasFlag(reg->yResultFlags, FP_FLAG_UNDERFLOW) ? setFlag(MACFlags, VPU_FLAG_UY) : unsetFlag(MACFlags, VPU_FLAG_UY);
  }
  if (!hasFlag(ignoredFields, FP_REGISTER_Z_FIELD))
  {
    (reg->z == 0) ? setFlag(MACFlags, VPU_FLAG_ZZ) : unsetFlag(MACFlags, VPU_FLAG_ZZ);
    FP_REGISTER_FIELD_IS_NEGATIVE(reg->zInt) ? setFlag(MACFlags, VPU_FLAG_SZ) : unsetFlag(MACFlags, VPU_FLAG_SZ);
    hasFlag(reg->zResultFlags, FP_FLAG_OVERFLOW) ? setFlag(MACFlags, VPU_FLAG_OZ) : unsetFlag(MACFlags, VPU_FLAG_OZ);
    hasFlag(reg->zResultFlags, FP_FLAG_UNDERFLOW) ? setFlag(MACFlags, VPU_FLAG_UZ) : unsetFlag(MACFlags, VPU_FLAG_UZ);
  }
  if (!hasFlag(ignoredFields, FP_REGISTER_W_FIELD))
  {
    (reg->w == 0) ? setFlag(MACFlags, VPU_FLAG_ZW) : unsetFlag(MACFlags, VPU_FLAG_ZW);
    FP_REGISTER_FIELD_IS_NEGATIVE(reg->wInt) ? setFlag(MACFlags, VPU_FLAG_SW) : unsetFlag(MACFlags, VPU_FLAG_SW);
    hasFlag(reg->wResultFlags, FP_FLAG_OVERFLOW) ? setFlag(MACFlags, VPU_FLAG_OW) : unsetFlag(MACFlags, VPU_FLAG_OW);
    hasFlag(reg->wResultFlags, FP_FLAG_UNDERFLOW) ? setFlag(MACFlags, VPU_FLAG_UW) : unsetFlag(MACFlags, VPU_FLAG_UW);
  }
}

void VPU::setStatusFlagsFromMACFlags()
{
  ((MACFlags & VPU_Z_BITS_MASK) > 0) ? setFlag(statusFlags, VPU_FLAG_Z) : unsetFlag(statusFlags, VPU_FLAG_Z);
  ((MACFlags & VPU_S_BITS_MASK) > 0) ? setFlag(statusFlags, VPU_FLAG_S) : unsetFlag(statusFlags, VPU_FLAG_S);
  ((MACFlags & VPU_O_BITS_MASK) > 0) ? setFlag(statusFlags, VPU_FLAG_O) : unsetFlag(statusFlags, VPU_FLAG_O);
  ((MACFlags & VPU_U_BITS_MASK) > 0) ? setFlag(statusFlags, VPU_FLAG_U) : unsetFlag(statusFlags, VPU_FLAG_U);
}

void VPU::setStickyFlagsFromStatusFlags()
{
  if (hasStatusFlag(VPU_FLAG_Z) || hasStatusFlag(VPU_FLAG_ZS))
  {
    setFlag(statusFlags, VPU_FLAG_ZS);
  }

  if (hasStatusFlag(VPU_FLAG_S) || hasStatusFlag(VPU_FLAG_SS))
  {
    setFlag(statusFlags, VPU_FLAG_SS);
  }

  if (hasStatusFlag(VPU_FLAG_O) || hasStatusFlag(VPU_FLAG_OS))
  {
    setFlag(statusFlags, VPU_FLAG_OS);
  }

  if (hasStatusFlag(VPU_FLAG_U) || hasStatusFlag(VPU_FLAG_US))
  {
    setFlag(statusFlags, VPU_FLAG_US);
  }
}

void VPU::loadIRegister(double value)
{
  iRegister = value;
}

void VPU::loadQRegister(double value)
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
    case VPU_ADDA:
      dest.storeAdd(&fpRegisters[ft], &fpRegisters[fs], fieldMask);
      break;
    case VPU_ADDi:
    case VPU_ADDAi:
      dest.storeAddDouble(&fpRegisters[fs], iRegister, fieldMask);
      break;
    case VPU_ADDq:
    case VPU_ADDAq:
      dest.storeAddDouble(&fpRegisters[fs], qRegister, fieldMask);
      break;
    case VPU_ADDx:
    case VPU_ADDAx:
      dest.storeAddDouble(&fpRegisters[fs], fpRegisters[ft].x, fieldMask);
      break;
    case VPU_ADDy:
    case VPU_ADDAy:
      dest.storeAddDouble(&fpRegisters[fs], fpRegisters[ft].y, fieldMask);
      break;
    case VPU_ADDz:
    case VPU_ADDAz:
      dest.storeAddDouble(&fpRegisters[fs], fpRegisters[ft].z, fieldMask);
      break;
    case VPU_ADDw:
    case VPU_ADDAw:
      dest.storeAddDouble(&fpRegisters[fs], fpRegisters[ft].w, fieldMask);
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
      dest.toDouble0(&fpRegisters[fs], fieldMask);
      break;
    case VPU_ITOF4:
      dest.toDouble4(&fpRegisters[fs], fieldMask);
      break;
    case VPU_ITOF12:
      dest.toDouble12(&fpRegisters[fs], fieldMask);
      break;
    case VPU_ITOF15:
      dest.toDouble15(&fpRegisters[fs], fieldMask);
      break;
    case VPU_MADD:
    case VPU_MSUB:
    case VPU_MUL:
      dest.storeMul(&fpRegisters[ft], &fpRegisters[fs], fieldMask);
      break;
    case VPU_MADDi:
    case VPU_MSUBi:
    case VPU_MULi:
      dest.storeMulDouble(&fpRegisters[fs], iRegister, fieldMask);
      break;
    case VPU_MADDq:
    case VPU_MSUBq:
    case VPU_MULq:
      dest.storeMulDouble(&fpRegisters[fs], qRegister, fieldMask);
      break;
    case VPU_MADDx:
    case VPU_MSUBx:
    case VPU_MULx:
      dest.storeMulDouble(&fpRegisters[fs], fpRegisters[ft].x, fieldMask);
      break;
    case VPU_MADDy:
    case VPU_MSUBy:
    case VPU_MULy:
      dest.storeMulDouble(&fpRegisters[fs], fpRegisters[ft].y, fieldMask);
      break;
    case VPU_MADDz:
    case VPU_MSUBz:
    case VPU_MULz:
      dest.storeMulDouble(&fpRegisters[fs], fpRegisters[ft].z, fieldMask);
      break;
    case VPU_MADDw:
    case VPU_MSUBw:
    case VPU_MULw:
      dest.storeMulDouble(&fpRegisters[fs], fpRegisters[ft].w, fieldMask);
      break;
    case VPU_MADDA:
    case VPU_MSUBA:
    case VPU_MULA:
      dest.storeMul(&fpRegisters[ft], &fpRegisters[fs], fieldMask);
      break;
    case VPU_MADDAi:
    case VPU_MSUBAi:
    case VPU_MULAi:
      dest.storeMulDouble(&fpRegisters[fs], iRegister, fieldMask);
      break;
    case VPU_MADDAq:
    case VPU_MSUBAq:
    case VPU_MULAq:
      dest.storeMulDouble(&fpRegisters[fs], qRegister, fieldMask);
      break;
    case VPU_MADDAx:
    case VPU_MSUBAx:
    case VPU_MULAx:
      dest.storeMulDouble(&fpRegisters[fs], fpRegisters[ft].x, fieldMask);
      break;
    case VPU_MADDAy:
    case VPU_MSUBAy:
    case VPU_MULAy:
      dest.storeMulDouble(&fpRegisters[fs], fpRegisters[ft].y, fieldMask);
      break;
    case VPU_MADDAz:
    case VPU_MSUBAz:
    case VPU_MULAz:
      dest.storeMulDouble(&fpRegisters[fs], fpRegisters[ft].z, fieldMask);
      break;
    case VPU_MADDAw:
    case VPU_MSUBAw:
    case VPU_MULAw:
      dest.storeMulDouble(&fpRegisters[fs], fpRegisters[ft].w, fieldMask);
      break;
    case VPU_MAX:
      dest.storeMax(&fpRegisters[fs], &fpRegisters[ft], fieldMask);
      break;
    case VPU_MAXi:
      dest.storeMaxDouble(&fpRegisters[fs], iRegister, fieldMask);
      break;
    case VPU_MAXx:
      dest.storeMaxDouble(&fpRegisters[fs], fpRegisters[ft].x, fieldMask);
      break;
    case VPU_MAXy:
      dest.storeMaxDouble(&fpRegisters[fs], fpRegisters[ft].y, fieldMask);
      break;
    case VPU_MAXz:
      dest.storeMaxDouble(&fpRegisters[fs], fpRegisters[ft].z, fieldMask);
      break;
    case VPU_MAXw:
      dest.storeMaxDouble(&fpRegisters[fs], fpRegisters[ft].w, fieldMask);
      break;
    case VPU_MINI:
      dest.storeMin(&fpRegisters[fs], &fpRegisters[ft], fieldMask);
      break;
    case VPU_MINIi:
      dest.storeMinDouble(&fpRegisters[fs], iRegister, fieldMask);
      break;
    case VPU_MINIx:
      dest.storeMinDouble(&fpRegisters[fs], fpRegisters[ft].x, fieldMask);
      break;
    case VPU_MINIy:
      dest.storeMinDouble(&fpRegisters[fs], fpRegisters[ft].y, fieldMask);
      break;
    case VPU_MINIz:
      dest.storeMinDouble(&fpRegisters[fs], fpRegisters[ft].z, fieldMask);
      break;
    case VPU_MINIw:
      dest.storeMinDouble(&fpRegisters[fs], fpRegisters[ft].w, fieldMask);
      break;
    case VPU_OPMULA:
      dest.storeOuterProduct(&fpRegisters[fs], &fpRegisters[ft]);
      break;
    case VPU_OPMSUB:
      dest.storeOuterProduct(&fpRegisters[fs], &fpRegisters[ft]);
      break;
    case VPU_SUB:
    case VPU_SUBA:
      dest.storeSub(&fpRegisters[fs], &fpRegisters[ft], fieldMask);
      break;
    case VPU_SUBi:
    case VPU_SUBAi:
      dest.storeSubDouble(&fpRegisters[fs], iRegister, fieldMask);
      break;
    case VPU_SUBq:
    case VPU_SUBAq:
      dest.storeSubDouble(&fpRegisters[fs], qRegister, fieldMask);
      break;
    case VPU_SUBx:
    case VPU_SUBAx:
      dest.storeSubDouble(&fpRegisters[fs], fpRegisters[ft].x, fieldMask);
      break;
    case VPU_SUBy:
    case VPU_SUBAy:
      dest.storeSubDouble(&fpRegisters[fs], fpRegisters[ft].y, fieldMask);
      break;
    case VPU_SUBz:
    case VPU_SUBAz:
      dest.storeSubDouble(&fpRegisters[fs], fpRegisters[ft].z, fieldMask);
      break;
    case VPU_SUBw:
    case VPU_SUBAw:
      dest.storeSubDouble(&fpRegisters[fs], fpRegisters[ft].w, fieldMask);
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
    case VPU_MADDA:
    case VPU_MADDAi:
    case VPU_MADDAq:
    case VPU_MADDAx:
    case VPU_MADDAy:
    case VPU_MADDAz:
    case VPU_MADDAw:
    case VPU_MSUBA:
    case VPU_MSUBAi:
    case VPU_MSUBAq:
    case VPU_MSUBAx:
    case VPU_MSUBAy:
    case VPU_MSUBAz:
    case VPU_MSUBAw:
    case VPU_MULA:
    case VPU_MULAi:
    case VPU_MULAq:
    case VPU_MULAx:
    case VPU_MULAy:
    case VPU_MULAz:
    case VPU_MULAw:
    case VPU_OPMULA:
    case VPU_SUBA:
    case VPU_SUBAi:
    case VPU_SUBAq:
    case VPU_SUBAx:
    case VPU_SUBAy:
    case VPU_SUBAz:
    case VPU_SUBAw:
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
    case VPU_MADDi:
    case VPU_MADDq:
    case VPU_MADDx:
    case VPU_MADDy:
    case VPU_MADDz:
    case VPU_MADDw:
    case VPU_MADDA:
    case VPU_MADDAi:
    case VPU_MADDAq:
    case VPU_MADDAx:
    case VPU_MADDAy:
    case VPU_MADDAz:
    case VPU_MADDAw:
      handleMADDInstruction(p);
      break;
    case VPU_MSUB:
    case VPU_MSUBi:
    case VPU_MSUBq:
    case VPU_MSUBx:
    case VPU_MSUBy:
    case VPU_MSUBz:
    case VPU_MSUBw:
    case VPU_MSUBA:
    case VPU_MSUBAi:
    case VPU_MSUBAq:
    case VPU_MSUBAx:
    case VPU_MSUBAy:
    case VPU_MSUBAz:
    case VPU_MSUBAw:
      handleMSUBInstruction(p);
      break;
    case VPU_OPMSUB:
      handleOPMSUBInstruction(p);
      break;
    default:
      updateDestinationRegisterWithPipelineResult(destReg, p);
      setFlags(destReg, FP_REGISTER_NO_FIELDS);
      break;
  }
}

void VPU::handleMADDInstruction(Pipeline * p)
{
  FPRegister tempReg;
  FPRegister *destReg = destinationRegisterFromPipeline(p);
  uint8_t fieldMask = p->destFieldMask;
  uint8_t ignoredFields = FP_REGISTER_NO_FIELDS;

  updateDestinationRegisterWithPipelineResult(&tempReg, p);
  setFlags(&tempReg, FP_REGISTER_NO_FIELDS);

  if (hasMACFlag(VPU_FLAG_OX))
  {
    unsetFlag(fieldMask, FP_REGISTER_X_FIELD);
    setFlag(ignoredFields, FP_REGISTER_X_FIELD);
  }
  if (hasMACFlag(VPU_FLAG_OY))
  {
    unsetFlag(fieldMask, FP_REGISTER_Y_FIELD);
    setFlag(ignoredFields, FP_REGISTER_Y_FIELD);
  }
  if (hasMACFlag(VPU_FLAG_OZ))
  {
    unsetFlag(fieldMask, FP_REGISTER_Z_FIELD);
    setFlag(ignoredFields, FP_REGISTER_Z_FIELD);
  }
  if (hasMACFlag(VPU_FLAG_OW))
  {
    unsetFlag(fieldMask, FP_REGISTER_W_FIELD);
    setFlag(ignoredFields, FP_REGISTER_W_FIELD);
  }

  setFlags(&tempReg, FP_REGISTER_NO_FIELDS);
  tempReg.storeAdd(&tempReg, &accumulator, fieldMask);
  destReg->copyFrom(&tempReg);
  setFlags(destReg, ignoredFields);
}

void VPU::handleMSUBInstruction(Pipeline * p)
{
  FPRegister tempReg;
  FPRegister *destReg = destinationRegisterFromPipeline(p);
  uint8_t fieldMask = p->destFieldMask;
  uint8_t ignoredFields = FP_REGISTER_NO_FIELDS;

  updateDestinationRegisterWithPipelineResult(&tempReg, p);
  setFlags(&tempReg, FP_REGISTER_NO_FIELDS);

  if (hasMACFlag(VPU_FLAG_OX))
  {
    tempReg.xInt ^= FP_SIGN_BIT;
    unsetFlag(fieldMask, FP_REGISTER_X_FIELD);
    setFlag(ignoredFields, FP_REGISTER_X_FIELD);
  }
  if (hasMACFlag(VPU_FLAG_OY))
  {
    tempReg.yInt ^= FP_SIGN_BIT;
    unsetFlag(fieldMask, FP_REGISTER_Y_FIELD);
    setFlag(ignoredFields, FP_REGISTER_Y_FIELD);
  }
  if (hasMACFlag(VPU_FLAG_OZ))
  {
    tempReg.zInt ^= FP_SIGN_BIT;
    unsetFlag(fieldMask, FP_REGISTER_Z_FIELD);
    setFlag(ignoredFields, FP_REGISTER_Z_FIELD);
  }
  if (hasMACFlag(VPU_FLAG_OW))
  {
    tempReg.wInt ^= FP_SIGN_BIT;
    unsetFlag(fieldMask, FP_REGISTER_W_FIELD);
    setFlag(ignoredFields, FP_REGISTER_W_FIELD);
  }

  setFlags(&tempReg, FP_REGISTER_NO_FIELDS);
  tempReg.storeSub(&tempReg, &accumulator, fieldMask);
  destReg->copyFrom(&tempReg);
  setFlags(destReg, ignoredFields);
}

void VPU::handleOPMSUBInstruction(Pipeline * p)
{
  FPRegister tempReg;
  FPRegister *destReg = destinationRegisterFromPipeline(p);

  updateDestinationRegisterWithPipelineResult(&tempReg, p);
  setFlags(&tempReg, FP_REGISTER_NO_FIELDS);

  tempReg.storeSub(&accumulator, &tempReg, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD | FP_REGISTER_Z_FIELD);
  destReg->copyFrom(&tempReg);
  setFlags(destReg, FP_REGISTER_NO_FIELDS);
}
