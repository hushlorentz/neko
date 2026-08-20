#include <stdexcept>
#include "bit_ops.hpp"
#include "floating_point_ops.hpp"
#include "vpu.hpp"
#include "vpu_flags.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"

#define VU0_MEMORY_SIZE 0x1000
#define VU1_MEMORY_SIZE 0x4000
#define NUM_FP_REGISTERS 32
#define NUM_INT_REGISTERS 16

#define NUM_TYPE1_OPCODES 52
uint16_t type1OpCodeList[NUM_TYPE1_OPCODES] = {VPU_ADD, VPU_ADDi, VPU_ADDq, VPU_ADDx, VPU_ADDy, VPU_ADDz, VPU_ADDw, VPU_ADDAx, VPU_ADDAy, VPU_ADDAz, VPU_ADDAw, VPU_MADD, VPU_MADDi, VPU_MADDq, VPU_MADDx, VPU_MADDy, VPU_MADDz, VPU_MADDw, VPU_MAX, VPU_MAXi, VPU_MAXx, VPU_MAXy, VPU_MAXz, VPU_MAXw, VPU_MINI, VPU_MINIi, VPU_MINIx, VPU_MINIy, VPU_MINIz, VPU_MINIw, VPU_MSUB, VPU_MSUBi, VPU_MSUBq, VPU_MSUBx, VPU_MSUBy, VPU_MSUBz, VPU_MSUBw, VPU_MUL, VPU_MULi, VPU_MULq, VPU_MULx, VPU_MULy, VPU_MULz, VPU_MULw, VPU_OPMSUB, VPU_SUB, VPU_SUBi, VPU_SUBq, VPU_SUBx, VPU_SUBy, VPU_SUBz, VPU_SUBw};

#define NUM_TYPE3_OPCODES 43
uint16_t type3OpCodeList[NUM_TYPE3_OPCODES] = {VPU_ABS, VPU_ADDA, VPU_ADDAi, VPU_ADDAq, VPU_CLIP, VPU_FTOI0, VPU_FTOI4, VPU_FTOI12, VPU_FTOI15, VPU_ITOF0, VPU_ITOF4, VPU_ITOF12, VPU_ITOF15, VPU_MADDA, VPU_MADDAi, VPU_MADDAq, VPU_MADDAx, VPU_MADDAy, VPU_MADDAz, VPU_MADDAw, VPU_MSUBA, VPU_MSUBAi, VPU_MSUBAq, VPU_MSUBAx, VPU_MSUBAy, VPU_MSUBAz, VPU_MSUBAw, VPU_MULA, VPU_MULAi, VPU_MULAq, VPU_MULAx, VPU_MULAy, VPU_MULAz, VPU_MULAw, VPU_NOP, VPU_OPMULA, VPU_SUBA, VPU_SUBAi, VPU_SUBAq, VPU_SUBAx, VPU_SUBAy, VPU_SUBAz, VPU_SUBAw};

using namespace std;

VPU::VPU(VPUType type) : type(type)
{
  initMemory();
  initFPRegisters();
  initIntRegisters();
  initOpCodeSets();
  initPipelineOrchestrator();
}

void VPU::initMemory()
{
  size_t memorySize;

  switch (type)
  {
    case VPUType::VU0:
      memorySize = VU0_MEMORY_SIZE;
      break;
    case VPUType::VU1:
      memorySize = VU1_MEMORY_SIZE;
      break;
    default:
      throw invalid_argument("Unknown VU type.");
  }

  microMem.resize(memorySize);
  vuMem.resize(memorySize);
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

uint8_t VPU::getState() const
{
  return state;
}

uint16_t VPU::programCounter() const
{
  return microMemPC;
}

bool VPU::hasTerminationPosition() const
{
  return terminationPositionValid;
}

uint16_t VPU::terminationPosition() const
{
  if (!terminationPositionValid)
  {
    throw logic_error("TPC is indeterminate before termination.");
  }

  return terminationPositionCounter;
}

bool VPU::dBitEnabled() const
{
  return dEnabled;
}

bool VPU::tBitEnabled() const
{
  return tEnabled;
}

void VPU::setDBitEnabled(bool enabled)
{
  dEnabled = enabled;
}

void VPU::setTBitEnabled(bool enabled)
{
  tEnabled = enabled;
}

void VPU::forceBreak()
{
  if (state == VPU_STATE_STOP)
  {
    return;
  }

  orchestrator.reset();
  lowerInstructionPending = false;
  pendingIntegerWrites.fill(0);
  endDelaySlotPending = false;
  terminationRequested = false;
  haltAfterDrain = false;
  terminationPositionValid = false;
  state = VPU_STATE_STOP;

  emitTrace({
    VPUTraceEventType::ForceBreak,
    cycles,
    microMemPC,
    0,
    0,
    0,
    0,
    0
  });
}

VPUType VPU::unitType() const
{
  return type;
}

size_t VPU::microMemorySize() const
{
  return microMem.size();
}

size_t VPU::dataMemorySize() const
{
  return vuMem.size();
}

const FPRegister * VPU::fpRegisterValue(int registerID) const
{
  return &fpRegisters[registerID];
}

uint16_t VPU::intRegisterValue(int registerID) const
{
  return intRegisters[registerID];
}

void VPU::loadFPRegister(int registerID, double x, double y, double z, double w)
{
  if (registerID == VPU_REGISTER_VF00)
  {
    return;
  }

  fpRegisters[registerID].load(x, y, z, w);
}

void VPU::loadIntFPRegister(int registerID, int32_t x, int32_t y, int32_t z, int32_t w)
{
  if (registerID == VPU_REGISTER_VF00)
  {
    return;
  }

  fpRegisters[registerID].x.setSignedValue(x);
  fpRegisters[registerID].y.setSignedValue(y);
  fpRegisters[registerID].z.setSignedValue(z);
  fpRegisters[registerID].w.setSignedValue(w);
}

void VPU::loadIntRegister(int registerID, int value)
{
  if (registerID == VPU_REGISTER_VI00)
  {
    return;
  }

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

uint32_t VPU::elapsedCycles() const
{
  return cycles;
}

void VPU::setMode(uint8_t newMode)
{
  mode = newMode;
}

void VPU::uploadMicroInstructions(const vector<uint8_t> &instructions)
{
  if (instructions.size() % 8 != 0)
  {
    throw invalid_argument("Microprogram size must be a multiple of 8 bytes.");
  }
  if (instructions.size() > microMem.size())
  {
    throw out_of_range("Microprogram exceeds VU micro memory.");
  }

  copy(instructions.begin(), instructions.end(), microMem.begin());
  microMemPC = 0;
}

void VPU::writeDataMemory(size_t address, const vector<uint8_t> &data)
{
  if (address > vuMem.size() || data.size() > vuMem.size() - address)
  {
    throw out_of_range("VU data-memory write is outside memory.");
  }

  copy(data.begin(), data.end(), vuMem.begin() + address);
}

vector<uint8_t> VPU::readDataMemory(size_t address, size_t byteCount) const
{
  if (address > vuMem.size() || byteCount > vuMem.size() - address)
  {
    throw out_of_range("VU data-memory read is outside memory.");
  }

  return vector<uint8_t>(vuMem.begin() + address, vuMem.begin() + address + byteCount);
}

void VPU::initMicroMode()
{
  startMicroMode();
  executeMicroInstructions();
}

void VPU::startMicroMode(uint16_t startAddress)
{
  if (state == VPU_STATE_RUN)
  {
    throw logic_error("VPU is already running.");
  }
  if (startAddress % 8 != 0)
  {
    throw invalid_argument("VU start address must be 8-byte aligned.");
  }
  if (startAddress > microMem.size() - 8)
  {
    throw out_of_range("VU start address is outside micro memory.");
  }

  orchestrator.reset();
  lowerInstructionPending = false;
  pendingIntegerWrites.fill(0);
  mode = VPU_MODE_MICRO;
  microMemPC = startAddress;
  endDelaySlotPending = false;
  terminationRequested = false;
  haltAfterDrain = false;
  state = VPU_STATE_RUN;
}

bool VPU::tick()
{
  if (state != VPU_STATE_RUN)
  {
    throw logic_error("VPU must be running before it can tick.");
  }

  bool instructionIssued = false;

  try
  {
    if (terminationRequested)
    {
      if (!orchestrator.hasNext())
      {
        terminationPositionCounter = microMemPC / 8;
        terminationPositionValid = true;
        state = haltAfterDrain ? VPU_STATE_STOP : VPU_STATE_READY;
      }
    }
    else if (orchestrator.stalling)
    {
      emitTrace({
        VPUTraceEventType::PipelineStall,
        cycles,
        microMemPC,
        0,
        0,
        0,
        0,
        0
      });
    }
    else
    {
      bool executingEndDelaySlot = endDelaySlotPending;
      uint16_t instructionAddress = microMemPC;
      uint32_t upperInstruction = nextUpperInstruction();
      uint32_t lowerInstruction = nextLowerInstruction();
      LowerInstruction decodedLowerInstruction;

      if (hasFlag(upperInstruction, VPU_I_BIT))
      {
        decodedLowerInstruction.unit = LowerExecutionUnit::Immediate;
        decodedLowerInstruction.immediateBits = lowerInstruction;
      }
      else
      {
        decodedLowerInstruction = decodeLowerInstruction(lowerInstruction);
      }

      if (executingEndDelaySlot && endBitSet(upperInstruction))
      {
        throw runtime_error("E bit cannot be set in an E-bit delay slot.");
      }
      if (executingEndDelaySlot &&
          lowerInstructionForbiddenInEndDelaySlot(decodedLowerInstruction))
      {
        throw runtime_error("VU lower instruction cannot execute in an E-bit delay slot.");
      }

      if (lowerInstructionStalls(decodedLowerInstruction))
      {
        emitTrace({
          VPUTraceEventType::PipelineStall,
          cycles,
          microMemPC,
          0,
          0,
          0,
          0,
          0
        });
      }
      else
      {
        uint16_t upperOpCode = processUpperInstruction(upperInstruction);
        queueLowerInstruction(
          decodedLowerInstruction,
          upperOpCode,
          upperInstruction,
          instructionAddress);
        microMemPC += 8;
        instructionIssued = true;

        emitTrace({
          VPUTraceEventType::InstructionIssued,
          cycles,
          instructionAddress,
          upperInstruction,
          lowerInstruction,
          0,
          0,
          0
        });

        if (haltBitSet(upperInstruction))
        {
          endDelaySlotPending = false;
          terminationRequested = true;
          haltAfterDrain = true;
        }
        else if (executingEndDelaySlot)
        {
          endDelaySlotPending = false;
          terminationRequested = true;
        }
        else if (endBitSet(upperInstruction))
        {
          endDelaySlotPending = true;
          haltAfterDrain = false;
        }
      }
    }

    orchestrator.update();
    executePendingLowerInstruction();
    cycles++;
  }
  catch (...)
  {
    lowerInstructionPending = false;
    pendingIntegerWrites.fill(0);
    state = VPU_STATE_STOP;
    throw;
  }

  return instructionIssued;
}

bool VPU::stepInstruction()
{
  if (state != VPU_STATE_RUN)
  {
    throw logic_error("VPU must be running before it can step.");
  }

  while (state == VPU_STATE_RUN)
  {
    if (tick())
    {
      return true;
    }
  }

  return false;
}

uint32_t VPU::run(uint32_t maxCycles)
{
  uint32_t executedCycles = 0;

  while (state == VPU_STATE_RUN && executedCycles < maxCycles)
  {
    tick();
    executedCycles++;
  }

  return executedCycles;
}

void VPU::executeMicroInstructions()
{
  while (state == VPU_STATE_RUN)
  {
    tick();
  }
}

void VPU::setTraceCallback(VPUTraceCallback callback)
{
  traceCallback = callback;
}

void VPU::emitTrace(const VPUTraceEvent &event) const
{
  if (traceCallback)
  {
    traceCallback(event);
  }
}

uint32_t VPU::nextUpperInstruction()
{
  if (microMemPC + 7 >= microMem.size())
  {
    throw runtime_error("Microinstruction fetch is outside micro memory.");
  }

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
  uint32_t instruction = 0;
  uint8_t shift = 24;

  for (vector<uint8_t>::iterator it = microMem.begin() + microMemPC + 4; it < microMem.begin() + (microMemPC + 8); ++it)
  {
    instruction |= *it << shift;
    shift -= 8;
  }

  return instruction;
}

uint16_t VPU::processUpperInstruction(uint32_t upperInstruction)
{
  uint16_t opCode = opCodeFromInstruction(upperInstruction);

  if (opCode == VPU_NOP)
  {
    return opCode;
  }

  uint8_t srcReg1 = src1RegFromOpCodeAndInstruction(opCode, upperInstruction);
  uint8_t srcReg2 = regFromInstruction(upperInstruction, VPU_FS_REG_SHIFT);
  uint8_t encodedFieldMask = (upperInstruction >> VPU_DEST_SHIFT) & VPU_DEST_MASK;
  uint8_t fieldMask = destinationMaskFromOpCode(opCode, encodedFieldMask);
  uint8_t destReg = destRegFromOpCodeAndInstruction(opCode, upperInstruction);
  uint8_t srcReg1Mask = srcReg1MaskFromOpCode(opCode, fieldMask);
  uint8_t srcReg2Mask = srcReg2MaskFromOpCode(opCode, fieldMask);

  orchestrator.initPipeline(VPU_PIPELINE_TYPE_FMAC, opCode, srcReg1, srcReg2, destReg, fieldMask, srcReg1Mask, srcReg2Mask, microMemPC);
  return opCode;
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
  uint16_t type3OpCode = instruction & VPU_TYPE3_MASK;
  if (type3OpCodes.find(type3OpCode) != type3OpCodes.end())
  {
    return type3OpCode;
  }

  uint16_t type1OpCode = instruction & VPU_TYPE1_MASK;
  if (type1OpCodes.find(type1OpCode) != type1OpCodes.end())
  {
    return type1OpCode;
  }

  throw runtime_error("Unsupported VU upper instruction.");
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

uint8_t VPU::destinationMaskFromOpCode(uint16_t opCode, uint8_t encodedMask)
{
  switch (opCode)
  {
    case VPU_CLIP:
      return FP_REGISTER_NO_FIELDS;
    case VPU_OPMULA:
    case VPU_OPMSUB:
      return FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD | FP_REGISTER_Z_FIELD;
    default:
      return encodedMask;
  }
}

uint8_t VPU::srcReg1MaskFromOpCode(uint16_t opCode, uint8_t destinationMask)
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
      return destinationMask;
    case VPU_ADDi:
    case VPU_ADDAi:
    case VPU_ADDq:
    case VPU_ADDAq:
    case VPU_MADDi:
    case VPU_MADDAi:
    case VPU_MADDq:
    case VPU_MADDAq:
    case VPU_MAXi:
    case VPU_MINIi:
    case VPU_MSUBi:
    case VPU_MSUBAi:
    case VPU_MSUBq:
    case VPU_MSUBAq:
    case VPU_MULi:
    case VPU_MULAi:
    case VPU_MULq:
    case VPU_MULAq:
    case VPU_SUBi:
    case VPU_SUBAi:
    case VPU_SUBq:
    case VPU_SUBAq:
      return FP_REGISTER_NO_FIELDS;
    case VPU_ADDx:
    case VPU_ADDAx:
    case VPU_MADDx:
    case VPU_MADDAx:
    case VPU_MAXx:
    case VPU_MINIx:
    case VPU_MSUBx:
    case VPU_MSUBAx:
    case VPU_MULx:
    case VPU_MULAx:
    case VPU_SUBx:
    case VPU_SUBAx:
      return FP_REGISTER_X_FIELD;
    case VPU_ADDy:
    case VPU_ADDAy:
    case VPU_MADDy:
    case VPU_MADDAy:
    case VPU_MAXy:
    case VPU_MINIy:
    case VPU_MSUBy:
    case VPU_MSUBAy:
    case VPU_MULy:
    case VPU_MULAy:
    case VPU_SUBy:
    case VPU_SUBAy:
      return FP_REGISTER_Y_FIELD;
    case VPU_ADDz:
    case VPU_ADDAz:
    case VPU_MADDz:
    case VPU_MADDAz:
    case VPU_MAXz:
    case VPU_MINIz:
    case VPU_MSUBz:
    case VPU_MSUBAz:
    case VPU_MULz:
    case VPU_MULAz:
    case VPU_SUBz:
    case VPU_SUBAz:
      return FP_REGISTER_Z_FIELD;
    case VPU_ADDw:
    case VPU_ADDAw:
    case VPU_MADDw:
    case VPU_MADDAw:
    case VPU_MAXw:
    case VPU_MINIw:
    case VPU_MSUBw:
    case VPU_MSUBAw:
    case VPU_MULw:
    case VPU_MULAw:
    case VPU_SUBw:
    case VPU_SUBAw:
      return FP_REGISTER_W_FIELD;
    case VPU_CLIP:
    case VPU_OPMULA:
    case VPU_OPMSUB:
      return FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD | FP_REGISTER_Z_FIELD;
    default:
      return destinationMask;
  }
}

uint8_t VPU::srcReg2MaskFromOpCode(uint16_t opCode, uint8_t destinationMask)
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
      return FP_REGISTER_NO_FIELDS;
    case VPU_CLIP:
      return FP_REGISTER_W_FIELD;
    case VPU_OPMULA:
    case VPU_OPMSUB:
      return FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD | FP_REGISTER_Z_FIELD;
    default:
      return destinationMask;
  }
}

void VPU::queueLowerInstruction(const LowerInstruction &lowerInstruction, uint16_t upperOpCode, uint32_t upperInstruction, uint16_t instructionAddress)
{
  if (lowerInstruction.unit == LowerExecutionUnit::None)
  {
    return;
  }

  pendingLowerInstruction = lowerInstruction;
  pendingLowerInstructionAddress = instructionAddress;
  lowerInstructionPending = true;
  pendingLowerInstructionReady = upperOpCode == VPU_NOP;
  pendingLowerWritebackDiscarded = false;

  if ((lowerInstruction.opCode == VPU_MFIR ||
       lowerInstruction.opCode == VPU_LQ) &&
      upperOpCode != VPU_NOP)
  {
    uint8_t upperDestination =
      destRegFromOpCodeAndInstruction(upperOpCode, upperInstruction);
    uint8_t upperFieldMask = destinationMaskFromOpCode(
      upperOpCode,
      (upperInstruction >> VPU_DEST_SHIFT) & VPU_DEST_MASK);

    pendingLowerWritebackDiscarded =
      upperDestination < NUM_FP_REGISTERS &&
      upperFieldMask != FP_REGISTER_NO_FIELDS &&
      upperDestination == lowerInstruction.destinationRegister;
  }
}

void VPU::executePendingLowerInstruction()
{
  if (!lowerInstructionPending || !pendingLowerInstructionReady)
  {
    return;
  }

  LowerInstruction instruction = pendingLowerInstruction;
  lowerInstructionPending = false;
  pendingLowerInstructionReady = false;

  switch (instruction.unit)
  {
    case LowerExecutionUnit::Immediate:
      iRegister.setBits(instruction.immediateBits);
      break;
    case LowerExecutionUnit::IALU:
      executeIALUInstruction(instruction);
      break;
    case LowerExecutionUnit::LSU:
      startLSUInstruction(instruction);
      break;
    case LowerExecutionUnit::FMAC:
      startLowerFMACInstruction(instruction);
      break;
    case LowerExecutionUnit::Branch:
      throw runtime_error("VU branch execution is not implemented.");
    case LowerExecutionUnit::None:
      break;
  }
}

void VPU::executeIALUInstruction(const LowerInstruction &instruction)
{
  uint16_t result;

  switch (instruction.opCode)
  {
    case VPU_IADD:
      result =
        intRegisters[instruction.sourceRegister1] +
        intRegisters[instruction.sourceRegister2];
      break;
    case VPU_ISUBIU:
      result =
        intRegisters[instruction.sourceRegister1] -
        instruction.immediate;
      break;
    default:
      throw runtime_error("Unsupported VU IALU instruction.");
  }

  if (instruction.destinationRegister != VPU_REGISTER_VI00)
  {
    intRegisters[instruction.destinationRegister] = result;
  }
}

void VPU::startLowerFMACInstruction(const LowerInstruction &instruction)
{
  orchestrator.startPipeline(
    VPU_PIPELINE_TYPE_FMAC,
    instruction.opCode,
    instruction.sourceRegister1,
    0,
    instruction.destinationRegister,
    instruction.destinationFieldMask,
    FP_REGISTER_NO_FIELDS,
    FP_REGISTER_NO_FIELDS,
    pendingLowerInstructionAddress,
    pendingLowerWritebackDiscarded);
}

void VPU::startLSUInstruction(const LowerInstruction &instruction)
{
  orchestrator.startPipeline(
    VPU_PIPELINE_TYPE_LSU,
    instruction.opCode,
    instruction.sourceRegister1,
    instruction.sourceRegister2,
    instruction.destinationRegister,
    instruction.destinationFieldMask,
    instruction.destinationFieldMask,
    0,
    pendingLowerInstructionAddress,
    pendingLowerWritebackDiscarded);

  uint8_t integerDestination = VPU_REGISTER_VI00;
  if (instruction.opCode == VPU_ILW ||
      instruction.opCode == VPU_SQI)
  {
    integerDestination = instruction.destinationRegister;
  }

  if (integerDestination != VPU_REGISTER_VI00)
  {
    pendingIntegerWrites[integerDestination]++;
  }
}

bool VPU::lowerInstructionStalls(const LowerInstruction &instruction) const
{
  const auto integerRegisterPending = [this](uint8_t registerID) {
    return
      registerID != VPU_REGISTER_VI00 &&
      pendingIntegerWrites[registerID] != 0;
  };

  switch (instruction.opCode)
  {
    case VPU_IADD:
      return
        integerRegisterPending(instruction.sourceRegister1) ||
        integerRegisterPending(instruction.sourceRegister2);
    case VPU_ISUBIU:
    case VPU_MFIR:
    case VPU_ILW:
    case VPU_LQ:
      return integerRegisterPending(instruction.sourceRegister1);
    case VPU_SQI:
      return
        integerRegisterPending(instruction.sourceRegister2) ||
        orchestrator.hasRegisterHazard(
          instruction.sourceRegister1,
          instruction.destinationFieldMask,
          VPU_REGISTER_VF00,
          FP_REGISTER_NO_FIELDS);
    default:
      return false;
  }
}

bool VPU::lowerInstructionForbiddenInEndDelaySlot(const LowerInstruction &instruction) const
{
  return
    instruction.unit == LowerExecutionUnit::LSU ||
    instruction.unit == LowerExecutionUnit::Branch;
}

uint16_t VPU::qwordAddress(uint16_t base, int16_t offset) const
{
  int32_t qwordIndex = static_cast<int32_t>(base) + offset;
  uint32_t qwordMask = static_cast<uint32_t>(vuMem.size() / 16) - 1;
  return (static_cast<uint32_t>(qwordIndex) & qwordMask) * 16;
}

uint32_t VPU::readDataWord(uint16_t address) const
{
  return
    static_cast<uint32_t>(vuMem[address]) |
    (static_cast<uint32_t>(vuMem[address + 1]) << 8) |
    (static_cast<uint32_t>(vuMem[address + 2]) << 16) |
    (static_cast<uint32_t>(vuMem[address + 3]) << 24);
}

void VPU::writeDataWord(uint16_t address, uint32_t value)
{
  vuMem[address] = value & 0xff;
  vuMem[address + 1] = (value >> 8) & 0xff;
  vuMem[address + 2] = (value >> 16) & 0xff;
  vuMem[address + 3] = (value >> 24) & 0xff;
}

bool VPU::endBitSet(uint32_t instruction)
{
  return hasFlag(instruction, VPU_E_BIT);
}

bool VPU::haltBitSet(uint32_t instruction)
{
  return
    (dEnabled && hasFlag(instruction, VPU_D_BIT)) ||
    (tEnabled && hasFlag(instruction, VPU_T_BIT));
}

void VPU::updateDestinationRegisterWithPipelineResult(FPRegister * destReg, Pipeline * p)
{
  if (destReg != &fpRegisters[VPU_REGISTER_VF00])
  {
    destReg->copyFieldsFrom(&(p->fpResult), p->destFieldMask);
  }
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
    reg->x.isNegative() ? setFlag(MACFlags, VPU_FLAG_SX) : unsetFlag(MACFlags, VPU_FLAG_SX);
    hasFlag(reg->xResultFlags, FP_FLAG_OVERFLOW) ? setFlag(MACFlags, VPU_FLAG_OX) : unsetFlag(MACFlags, VPU_FLAG_OX);
    hasFlag(reg->xResultFlags, FP_FLAG_UNDERFLOW) ? setFlag(MACFlags, VPU_FLAG_UX) : unsetFlag(MACFlags, VPU_FLAG_UX);
  }
  if (!hasFlag(ignoredFields, FP_REGISTER_Y_FIELD))
  {
    (reg->y == 0) ? setFlag(MACFlags, VPU_FLAG_ZY) : unsetFlag(MACFlags, VPU_FLAG_ZY);
    reg->y.isNegative() ? setFlag(MACFlags, VPU_FLAG_SY) : unsetFlag(MACFlags, VPU_FLAG_SY);
    hasFlag(reg->yResultFlags, FP_FLAG_OVERFLOW) ? setFlag(MACFlags, VPU_FLAG_OY) : unsetFlag(MACFlags, VPU_FLAG_OY);
    hasFlag(reg->yResultFlags, FP_FLAG_UNDERFLOW) ? setFlag(MACFlags, VPU_FLAG_UY) : unsetFlag(MACFlags, VPU_FLAG_UY);
  }
  if (!hasFlag(ignoredFields, FP_REGISTER_Z_FIELD))
  {
    (reg->z == 0) ? setFlag(MACFlags, VPU_FLAG_ZZ) : unsetFlag(MACFlags, VPU_FLAG_ZZ);
    reg->z.isNegative() ? setFlag(MACFlags, VPU_FLAG_SZ) : unsetFlag(MACFlags, VPU_FLAG_SZ);
    hasFlag(reg->zResultFlags, FP_FLAG_OVERFLOW) ? setFlag(MACFlags, VPU_FLAG_OZ) : unsetFlag(MACFlags, VPU_FLAG_OZ);
    hasFlag(reg->zResultFlags, FP_FLAG_UNDERFLOW) ? setFlag(MACFlags, VPU_FLAG_UZ) : unsetFlag(MACFlags, VPU_FLAG_UZ);
  }
  if (!hasFlag(ignoredFields, FP_REGISTER_W_FIELD))
  {
    (reg->w == 0) ? setFlag(MACFlags, VPU_FLAG_ZW) : unsetFlag(MACFlags, VPU_FLAG_ZW);
    reg->w.isNegative() ? setFlag(MACFlags, VPU_FLAG_SW) : unsetFlag(MACFlags, VPU_FLAG_SW);
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
  if (lowerInstructionPending &&
      pendingLowerInstructionAddress == p->instructionAddress)
  {
    pendingLowerInstructionReady = true;
  }

  if (p->type == VPU_PIPELINE_TYPE_LSU)
  {
    startLSUPipeline(p);
    return;
  }

  uint16_t opCode = p->opCode;
  uint8_t ft = p->srcReg1;
  uint8_t fs = p->srcReg2;
  uint8_t fieldMask = p->destFieldMask;
  FPRegister *destReg = destinationRegisterFromPipeline(p);
  FPRegister dest(destReg->x, destReg->y, destReg->z, destReg->w);

  if (opCode == VPU_MFIR)
  {
    int32_t value =
      static_cast<int16_t>(intRegisters[p->srcReg1]);

    if (hasFlag(fieldMask, FP_REGISTER_X_FIELD))
    {
      dest.x.setSignedValue(value);
    }
    if (hasFlag(fieldMask, FP_REGISTER_Y_FIELD))
    {
      dest.y.setSignedValue(value);
    }
    if (hasFlag(fieldMask, FP_REGISTER_Z_FIELD))
    {
      dest.z.setSignedValue(value);
    }
    if (hasFlag(fieldMask, FP_REGISTER_W_FIELD))
    {
      dest.w.setSignedValue(value);
    }

    p->setFPRegisterResult(&dest);
    return;
  }

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

void VPU::startLSUPipeline(Pipeline *pipeline)
{
  switch (pipeline->opCode)
  {
    case VPU_ILW:
    {
      uint16_t address = qwordAddress(
        intRegisters[pipeline->srcReg1],
        pendingLowerInstruction.immediate);
      uint8_t fieldOffset;

      switch (pipeline->destFieldMask)
      {
        case FP_REGISTER_X_FIELD:
          fieldOffset = 0;
          break;
        case FP_REGISTER_Y_FIELD:
          fieldOffset = 4;
          break;
        case FP_REGISTER_Z_FIELD:
          fieldOffset = 8;
          break;
        case FP_REGISTER_W_FIELD:
          fieldOffset = 12;
          break;
        default:
          throw runtime_error("ILW requires exactly one destination field.");
      }

      pipeline->memoryAddress = address + fieldOffset;
      pipeline->setIntResult(readDataWord(pipeline->memoryAddress) & 0xffff);
      break;
    }
    case VPU_LQ:
    {
      pipeline->memoryAddress = qwordAddress(
        intRegisters[pipeline->srcReg1],
        pendingLowerInstruction.immediate);
      FPRegister result = fpRegisters[pipeline->destReg];
      if (hasFlag(pipeline->destFieldMask, FP_REGISTER_X_FIELD))
      {
        result.x.setBits(readDataWord(pipeline->memoryAddress));
      }
      if (hasFlag(pipeline->destFieldMask, FP_REGISTER_Y_FIELD))
      {
        result.y.setBits(readDataWord(pipeline->memoryAddress + 4));
      }
      if (hasFlag(pipeline->destFieldMask, FP_REGISTER_Z_FIELD))
      {
        result.z.setBits(readDataWord(pipeline->memoryAddress + 8));
      }
      if (hasFlag(pipeline->destFieldMask, FP_REGISTER_W_FIELD))
      {
        result.w.setBits(readDataWord(pipeline->memoryAddress + 12));
      }
      pipeline->setFPRegisterResult(&result);
      break;
    }
    case VPU_SQI:
      pipeline->memoryAddress =
        qwordAddress(intRegisters[pipeline->srcReg2]);
      pipeline->setFPRegisterResult(&fpRegisters[pipeline->srcReg1]);
      pipeline->setIntResult(intRegisters[pipeline->srcReg2] + 1);
      break;
    default:
      throw runtime_error("Unsupported VU LSU instruction.");
  }
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
  if (p->type == VPU_PIPELINE_TYPE_LSU)
  {
    finishLSUPipeline(p);
    emitTrace({
      VPUTraceEventType::PipelineWriteback,
      cycles,
      p->instructionAddress,
      0,
      0,
      p->opCode,
      p->destReg,
      p->destFieldMask
    });
    return;
  }

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
    case VPU_MFIR:
      if (!p->discardWriteback)
      {
        updateDestinationRegisterWithPipelineResult(destReg, p);
      }
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

  emitTrace({
    VPUTraceEventType::PipelineWriteback,
    cycles,
    p->instructionAddress,
    0,
    0,
    p->opCode,
    p->destReg,
    p->destFieldMask
  });
}

void VPU::finishLSUPipeline(Pipeline *pipeline)
{
  switch (pipeline->opCode)
  {
    case VPU_ILW:
      if (pipeline->destReg != VPU_REGISTER_VI00)
      {
        intRegisters[pipeline->destReg] = pipeline->intResult;
        pendingIntegerWrites[pipeline->destReg]--;
      }
      break;
    case VPU_LQ:
      if (!pipeline->discardWriteback)
      {
        updateDestinationRegisterWithPipelineResult(
          &fpRegisters[pipeline->destReg],
          pipeline);
      }
      break;
    case VPU_SQI:
      if (hasFlag(pipeline->destFieldMask, FP_REGISTER_X_FIELD))
      {
        writeDataWord(pipeline->memoryAddress, pipeline->fpResult.x.bits());
      }
      if (hasFlag(pipeline->destFieldMask, FP_REGISTER_Y_FIELD))
      {
        writeDataWord(pipeline->memoryAddress + 4, pipeline->fpResult.y.bits());
      }
      if (hasFlag(pipeline->destFieldMask, FP_REGISTER_Z_FIELD))
      {
        writeDataWord(pipeline->memoryAddress + 8, pipeline->fpResult.z.bits());
      }
      if (hasFlag(pipeline->destFieldMask, FP_REGISTER_W_FIELD))
      {
        writeDataWord(pipeline->memoryAddress + 12, pipeline->fpResult.w.bits());
      }
      if (pipeline->destReg != VPU_REGISTER_VI00)
      {
        intRegisters[pipeline->destReg] = pipeline->intResult;
        pendingIntegerWrites[pipeline->destReg]--;
      }
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
  if (destReg != &fpRegisters[VPU_REGISTER_VF00])
  {
    destReg->copyFieldsFrom(&tempReg, p->destFieldMask);
  }
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
    tempReg.x.toggleSign();
    unsetFlag(fieldMask, FP_REGISTER_X_FIELD);
    setFlag(ignoredFields, FP_REGISTER_X_FIELD);
  }
  if (hasMACFlag(VPU_FLAG_OY))
  {
    tempReg.y.toggleSign();
    unsetFlag(fieldMask, FP_REGISTER_Y_FIELD);
    setFlag(ignoredFields, FP_REGISTER_Y_FIELD);
  }
  if (hasMACFlag(VPU_FLAG_OZ))
  {
    tempReg.z.toggleSign();
    unsetFlag(fieldMask, FP_REGISTER_Z_FIELD);
    setFlag(ignoredFields, FP_REGISTER_Z_FIELD);
  }
  if (hasMACFlag(VPU_FLAG_OW))
  {
    tempReg.w.toggleSign();
    unsetFlag(fieldMask, FP_REGISTER_W_FIELD);
    setFlag(ignoredFields, FP_REGISTER_W_FIELD);
  }

  setFlags(&tempReg, FP_REGISTER_NO_FIELDS);
  tempReg.storeSub(&accumulator, &tempReg, fieldMask);
  if (destReg != &fpRegisters[VPU_REGISTER_VF00])
  {
    destReg->copyFieldsFrom(&tempReg, p->destFieldMask);
  }
  setFlags(destReg, ignoredFields);
}

void VPU::handleOPMSUBInstruction(Pipeline * p)
{
  FPRegister tempReg;
  FPRegister *destReg = destinationRegisterFromPipeline(p);

  updateDestinationRegisterWithPipelineResult(&tempReg, p);
  setFlags(&tempReg, FP_REGISTER_NO_FIELDS);

  tempReg.storeSub(&accumulator, &tempReg, FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD | FP_REGISTER_Z_FIELD);
  if (destReg != &fpRegisters[VPU_REGISTER_VF00])
  {
    destReg->copyFieldsFrom(&tempReg, p->destFieldMask);
  }
  setFlags(destReg, FP_REGISTER_NO_FIELDS);
}
