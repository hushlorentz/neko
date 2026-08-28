#include <stdexcept>
#include "bit_ops.hpp"
#include "floating_point_ops.hpp"
#include "clock_scheduler.hpp"
#include "vpu.hpp"
#include "vpu_field_mask.hpp"
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

namespace
{
  bool isCompoundFMACOperation(uint16_t opCode)
  {
    switch (opCode)
    {
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
      case VPU_OPMSUB:
        return true;
      default:
        return false;
    }
  }

  const VUFloat &laneValue(const FPRegister &reg, size_t lane)
  {
    switch (lane)
    {
      case 0:
        return reg.x;
      case 1:
        return reg.y;
      case 2:
        return reg.z;
      default:
        return reg.w;
    }
  }

  uint8_t laneFlags(const FPRegister &reg, size_t lane)
  {
    switch (lane)
    {
      case 0:
        return reg.xResultFlags;
      case 1:
        return reg.yResultFlags;
      case 2:
        return reg.zResultFlags;
      default:
        return reg.wResultFlags;
    }
  }

  uint32_t selectedLaneBits(const FPRegister &reg, uint8_t fieldMask)
  {
    switch (fieldMask)
    {
      case FP_REGISTER_X_FIELD:
        return reg.x.bits();
      case FP_REGISTER_Y_FIELD:
        return reg.y.bits();
      case FP_REGISTER_Z_FIELD:
        return reg.z.bits();
      case FP_REGISTER_W_FIELD:
        return reg.w.bits();
      default:
        throw runtime_error("VU source must select exactly one field.");
    }
  }

  constexpr uint32_t ESIN_S2_BITS = 0xbe2aaaa4;
  constexpr uint32_t ESIN_S3_BITS = 0x3c08873e;
  constexpr uint32_t ESIN_S4_BITS = 0xb94fb21f;
  constexpr uint32_t ESIN_S5_BITS = 0x362e9c14;

  constexpr uint32_t EEXP_E1_BITS = 0x3e7fffa8;
  constexpr uint32_t EEXP_E2_BITS = 0x3d0007f4;
  constexpr uint32_t EEXP_E3_BITS = 0x3b29d3ff;
  constexpr uint32_t EEXP_E4_BITS = 0x3933e553;
  constexpr uint32_t EEXP_E5_BITS = 0x36b63510;
  constexpr uint32_t EEXP_E6_BITS = 0x353961ac;

  constexpr uint32_t EATAN_T1_BITS = 0x3f7ffff5;
  constexpr uint32_t EATAN_T2_BITS = 0xbeaaa61c;
  constexpr uint32_t EATAN_T3_BITS = 0x3e4c40a6;
  constexpr uint32_t EATAN_T4_BITS = 0xbe0e6c63;
  constexpr uint32_t EATAN_T5_BITS = 0x3dc577df;
  constexpr uint32_t EATAN_T6_BITS = 0xbd6501c4;
  constexpr uint32_t EATAN_T7_BITS = 0x3cb31652;
  constexpr uint32_t EATAN_T8_BITS = 0xbb84d7e7;
  constexpr uint32_t PI_OVER_FOUR_BITS = 0x3f490fdb;

  uint32_t multiplyVUFloatBits(uint32_t left, uint32_t right)
  {
    return mulFPRaw(left, right).bits;
  }

  uint32_t addVUFloatBits(uint32_t left, uint32_t right)
  {
    return addFPRaw(left, right).bits;
  }

  uint32_t subtractVUFloatBits(uint32_t left, uint32_t right)
  {
    return subFPRaw(left, right).bits;
  }

  uint32_t divideVUFloatBits(uint32_t numerator, uint32_t denominator)
  {
    return divFPRaw(numerator, denominator).bits;
  }

  uint32_t calculateESIN(uint32_t x)
  {
    const uint32_t x2 = multiplyVUFloatBits(x, x);
    uint32_t accumulator = multiplyVUFloatBits(x, VU_FLOAT_ONE_BITS);
    const uint32_t x3 = multiplyVUFloatBits(x2, x);
    const uint32_t x5 = multiplyVUFloatBits(x3, x2);
    accumulator = addVUFloatBits(
      accumulator,
      multiplyVUFloatBits(x3, ESIN_S2_BITS));
    const uint32_t x7 = multiplyVUFloatBits(x5, x2);
    accumulator = addVUFloatBits(
      accumulator,
      multiplyVUFloatBits(x5, ESIN_S3_BITS));
    const uint32_t x9 = multiplyVUFloatBits(x7, x2);
    accumulator = addVUFloatBits(
      accumulator,
      multiplyVUFloatBits(x7, ESIN_S4_BITS));
    return addVUFloatBits(
      accumulator,
      multiplyVUFloatBits(x9, ESIN_S5_BITS));
  }

  uint32_t calculateEEXP(uint32_t x)
  {
    const uint32_t x2 = multiplyVUFloatBits(x, x);
    uint32_t accumulator = multiplyVUFloatBits(x, EEXP_E1_BITS);
    const uint32_t x3 = multiplyVUFloatBits(x2, x);
    accumulator = addVUFloatBits(
      accumulator,
      multiplyVUFloatBits(x2, EEXP_E2_BITS));
    const uint32_t x4 = multiplyVUFloatBits(x3, x);
    accumulator = addVUFloatBits(
      accumulator,
      multiplyVUFloatBits(x3, EEXP_E3_BITS));
    const uint32_t x5 = multiplyVUFloatBits(x4, x);
    accumulator = addVUFloatBits(
      accumulator,
      multiplyVUFloatBits(x4, EEXP_E4_BITS));
    const uint32_t x6 = multiplyVUFloatBits(x5, x);
    accumulator = addVUFloatBits(
      accumulator,
      multiplyVUFloatBits(x5, EEXP_E5_BITS));
    accumulator = addVUFloatBits(accumulator, VU_FLOAT_ONE_BITS);
    uint32_t result = addVUFloatBits(
      accumulator,
      multiplyVUFloatBits(x6, EEXP_E6_BITS));
    result = multiplyVUFloatBits(result, result);
    result = multiplyVUFloatBits(result, result);
    return divFPRaw(VU_FLOAT_ONE_BITS, result).bits;
  }

  uint32_t calculateEATAN(uint32_t transformedInput)
  {
    const uint32_t inputSquared = multiplyVUFloatBits(
      transformedInput,
      transformedInput);
    uint32_t accumulator = multiplyVUFloatBits(
      EATAN_T1_BITS,
      transformedInput);
    const uint32_t inputCubed = multiplyVUFloatBits(
      inputSquared,
      transformedInput);
    const uint32_t inputToFifth = multiplyVUFloatBits(
      inputCubed,
      inputSquared);
    accumulator = addVUFloatBits(
      accumulator,
      multiplyVUFloatBits(inputCubed, EATAN_T2_BITS));
    const uint32_t inputToSeventh = multiplyVUFloatBits(
      inputToFifth,
      inputSquared);
    accumulator = addVUFloatBits(
      accumulator,
      multiplyVUFloatBits(inputToFifth, EATAN_T3_BITS));
    const uint32_t inputToNinth = multiplyVUFloatBits(
      inputToSeventh,
      inputSquared);
    accumulator = addVUFloatBits(
      accumulator,
      multiplyVUFloatBits(inputToSeventh, EATAN_T4_BITS));
    const uint32_t inputToEleventh = multiplyVUFloatBits(
      inputToNinth,
      inputSquared);
    accumulator = addVUFloatBits(
      accumulator,
      multiplyVUFloatBits(inputToNinth, EATAN_T5_BITS));
    const uint32_t inputToThirteenth = multiplyVUFloatBits(
      inputToEleventh,
      inputSquared);
    accumulator = addVUFloatBits(
      accumulator,
      multiplyVUFloatBits(inputToEleventh, EATAN_T6_BITS));
    const uint32_t inputToFifteenth = multiplyVUFloatBits(
      inputToThirteenth,
      inputSquared);
    accumulator = addVUFloatBits(
      accumulator,
      multiplyVUFloatBits(inputToThirteenth, EATAN_T7_BITS));
    accumulator = addVUFloatBits(
      accumulator,
      multiplyVUFloatBits(VU_FLOAT_ONE_BITS, PI_OVER_FOUR_BITS));
    return addVUFloatBits(
      accumulator,
      multiplyVUFloatBits(inputToFifteenth, EATAN_T8_BITS));
  }

  VPUArithmeticTrace arithmeticTraceForPipeline(const Pipeline &pipeline)
  {
    VPUArithmeticTrace trace;
    if (!isCompoundFMACOperation(pipeline.opCode))
    {
      return trace;
    }

    trace.present = true;
    trace.ignoredResultFields = pipeline.ignoredResultFields;
    const uint8_t laneMasks[] = {
      FP_REGISTER_X_FIELD,
      FP_REGISTER_Y_FIELD,
      FP_REGISTER_Z_FIELD,
      FP_REGISTER_W_FIELD
    };
    for (size_t lane = 0; lane < trace.lanes.size(); lane++)
    {
      const bool multiplicationResultUsed =
        hasFlag(pipeline.ignoredResultFields, laneMasks[lane]);
      trace.lanes[lane] = {
        laneValue(pipeline.fpResult, lane).bits(),
        laneFlags(pipeline.fpResult, lane),
        laneValue(pipeline.accumulatorValue, lane).bits(),
        laneValue(pipeline.operationResult, lane).bits(),
        multiplicationResultUsed ?
          laneFlags(pipeline.fpResult, lane) :
          laneFlags(pipeline.operationResult, lane)
      };
    }
    return trace;
  }
}

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
  pendingIALUWrites.fill(0);
  bypassedIntegerValues.fill(0);
  pendingAccumulatorWrites = 0;
  accumulatorForwardValid = false;
  xgkickWaiting = false;
  xgkickTransferStarted = false;
  endDelaySlotPending = false;
  branchDelaySlotPending = false;
  pendingBranchTaken = false;
  pendingBranchLinkValid = false;
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

void VPU::setXGKICKHandler(VUXGKICKHandler *handler)
{
  xgkickHandler = handler;
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
  pendingIALUWrites.fill(0);
  bypassedIntegerValues.fill(0);
  pendingAccumulatorWrites = 0;
  accumulatorForwardValid = false;
  xgkickWaiting = false;
  xgkickTransferStarted = false;
  mode = VPU_MODE_MICRO;
  microMemPC = startAddress;
  endDelaySlotPending = false;
  branchDelaySlotPending = false;
  pendingBranchTaken = false;
  pendingBranchLinkValid = false;
  terminationRequested = false;
  haltAfterDrain = false;
  state = VPU_STATE_RUN;
}

bool VPU::clockActive() const
{
  return state == VPU_STATE_RUN;
}

void VPU::clock()
{
  tick();
}

bool VPU::tick()
{
  if (state != VPU_STATE_RUN)
  {
    throw logic_error("VPU must be running before it can tick.");
  }

  bool instructionIssued = false;
  bool branchDelaySlotIssued = false;

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
    else if (orchestrator.stalling || xgkickStallsIssue())
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
      bool executingBranchDelaySlot = branchDelaySlotPending;
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

        if (executingBranchDelaySlot)
        {
          branchDelaySlotIssued = true;
        }
      }
    }

    orchestrator.update();
    executePendingLowerInstruction();
    if (branchDelaySlotIssued)
    {
      completeBranchDelaySlot();
    }
    cycles++;
  }
  catch (...)
  {
    lowerInstructionPending = false;
    pendingIntegerWrites.fill(0);
    pendingIALUWrites.fill(0);
    bypassedIntegerValues.fill(0);
    pendingAccumulatorWrites = 0;
    accumulatorForwardValid = false;
    xgkickWaiting = false;
    xgkickTransferStarted = false;
    branchDelaySlotPending = false;
    pendingBranchTaken = false;
    pendingBranchLinkValid = false;
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
  return ClockScheduler().run(*this, maxCycles);
}

void VPU::executeMicroInstructions()
{
  ClockScheduler().runUntilInactive(*this);
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

  return
    static_cast<uint32_t>(microMem[microMemPC + 4]) |
    (static_cast<uint32_t>(microMem[microMemPC + 5]) << 8) |
    (static_cast<uint32_t>(microMem[microMemPC + 6]) << 16) |
    (static_cast<uint32_t>(microMem[microMemPC + 7]) << 24);
}

uint32_t VPU::nextLowerInstruction()
{
  return
    static_cast<uint32_t>(microMem[microMemPC]) |
    (static_cast<uint32_t>(microMem[microMemPC + 1]) << 8) |
    (static_cast<uint32_t>(microMem[microMemPC + 2]) << 16) |
    (static_cast<uint32_t>(microMem[microMemPC + 3]) << 24);
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
  if (destReg == VPU_REGISTER_ACCUMULATOR)
  {
    pendingAccumulatorWrites++;
  }
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
    case VPU_OPMULA:
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
      return vpuFieldMaskFromEncoding(encodedMask);
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
       lowerInstruction.opCode == VPU_MFP ||
       lowerInstruction.opCode == VPU_MOVE ||
       lowerInstruction.opCode == VPU_MR32 ||
       lowerInstruction.opCode == VPU_RGET ||
       lowerInstruction.opCode == VPU_RNEXT ||
       lowerInstruction.opCode == VPU_LQ ||
       lowerInstruction.opCode == VPU_LQD ||
       lowerInstruction.opCode == VPU_LQI) &&
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
      startIRegisterInstruction(instruction);
      break;
    case LowerExecutionUnit::IALU:
      startIALUInstruction(instruction);
      break;
    case LowerExecutionUnit::LSU:
      startLSUInstruction(instruction);
      break;
    case LowerExecutionUnit::FMAC:
      startLowerFMACInstruction(instruction);
      break;
    case LowerExecutionUnit::FDIV:
      startFDIVInstruction(instruction);
      break;
    case LowerExecutionUnit::EFU:
      startEFUInstruction(instruction);
      break;
    case LowerExecutionUnit::WaitQ:
      startWaitQInstruction(instruction);
      break;
    case LowerExecutionUnit::WaitP:
      startWaitPInstruction(instruction);
      break;
    case LowerExecutionUnit::Flag:
      startFlagInstruction(instruction);
      break;
    case LowerExecutionUnit::Random:
      startRandomInstruction(instruction);
      break;
    case LowerExecutionUnit::XGKICK:
      startXGKICKInstruction(instruction);
      break;
    case LowerExecutionUnit::Branch:
      startBranchInstruction(instruction);
      break;
    case LowerExecutionUnit::None:
      break;
  }
}

void VPU::startIRegisterInstruction(const LowerInstruction &instruction)
{
  Pipeline *pipeline = orchestrator.startPipeline(
    VPU_PIPELINE_TYPE_I_REGISTER,
    0,
    0,
    0,
    0,
    FP_REGISTER_NO_FIELDS,
    FP_REGISTER_NO_FIELDS,
    FP_REGISTER_NO_FIELDS,
    pendingLowerInstructionAddress);
  pipeline->immediateBits = instruction.immediateBits;
}

void VPU::startIALUInstruction(const LowerInstruction &instruction)
{
  Pipeline *pipeline = orchestrator.startPipeline(
    VPU_PIPELINE_TYPE_IALU,
    instruction.opCode,
    instruction.sourceRegister1,
    instruction.sourceRegister2,
    instruction.destinationRegister,
    FP_REGISTER_NO_FIELDS,
    FP_REGISTER_NO_FIELDS,
    FP_REGISTER_NO_FIELDS,
    pendingLowerInstructionAddress,
    false,
    instruction.immediate);

  if (branchDelaySlotPending && pendingBranchLinkValid)
  {
    // Delay-slot reads occur before JALR makes its link write visible.
    if (instruction.sourceRegister1 == pendingBranchLinkRegister)
    {
      pipeline->intSourceValue1 =
        integerValueForExecution(instruction.sourceRegister1);
      pipeline->intSource1Sampled = true;
    }
    if (instruction.sourceRegister2 == pendingBranchLinkRegister)
    {
      pipeline->intSourceValue2 =
        integerValueForExecution(instruction.sourceRegister2);
      pipeline->intSource2Sampled = true;
    }
  }

  if (instruction.destinationRegister != VPU_REGISTER_VI00)
  {
    pendingIALUWrites[instruction.destinationRegister]++;
  }
}

void VPU::startBranchInstruction(const LowerInstruction &instruction)
{
  orchestrator.startPipeline(
    VPU_PIPELINE_TYPE_BRANCH,
    instruction.opCode,
    instruction.sourceRegister1,
    instruction.sourceRegister2,
    instruction.destinationRegister,
    FP_REGISTER_NO_FIELDS,
    FP_REGISTER_NO_FIELDS,
    FP_REGISTER_NO_FIELDS,
    pendingLowerInstructionAddress,
    false,
    instruction.immediate);

  branchDelaySlotPending = true;
}

void VPU::evaluateBranchPipeline(Pipeline *pipeline)
{
  pendingBranchLinkValid = false;
  pendingBranchTarget =
    static_cast<uint16_t>(
      (static_cast<uint32_t>(pipeline->instructionAddress + 8) +
       static_cast<int32_t>(pipeline->immediate) * 8) &
      (microMem.size() - 1));

  switch (pipeline->opCode)
  {
    case VPU_B:
      pendingBranchTaken = true;
      break;
    case VPU_BAL:
      pendingBranchTaken = true;
      if (pipeline->destReg != VPU_REGISTER_VI00)
      {
        pendingBranchLinkValid = true;
        pendingBranchLinkRegister = pipeline->destReg;
        pendingBranchLinkValue = pipeline->instructionAddress + 16;
      }
      break;
    case VPU_IBEQ:
      pendingBranchTaken =
        integerValueForExecution(pipeline->srcReg1) ==
        integerValueForExecution(pipeline->srcReg2);
      break;
    case VPU_IBNE:
      pendingBranchTaken =
        integerValueForExecution(pipeline->srcReg1) !=
        integerValueForExecution(pipeline->srcReg2);
      break;
    case VPU_IBGEZ:
      pendingBranchTaken =
        static_cast<int16_t>(
          integerValueForExecution(pipeline->srcReg1)) >= 0;
      break;
    case VPU_IBGTZ:
      pendingBranchTaken =
        static_cast<int16_t>(
          integerValueForExecution(pipeline->srcReg1)) > 0;
      break;
    case VPU_IBLEZ:
      pendingBranchTaken =
        static_cast<int16_t>(
          integerValueForExecution(pipeline->srcReg1)) <= 0;
      break;
    case VPU_IBLTZ:
      pendingBranchTaken =
        static_cast<int16_t>(
          integerValueForExecution(pipeline->srcReg1)) < 0;
      break;
    case VPU_JALR:
      pendingBranchTaken = true;
      pendingBranchTarget =
        integerValueForExecution(pipeline->srcReg1) &
        (microMem.size() - 1);
      if (pipeline->destReg != VPU_REGISTER_VI00)
      {
        pendingBranchLinkValid = true;
        pendingBranchLinkRegister = pipeline->destReg;
        pendingBranchLinkValue = pipeline->instructionAddress + 16;
      }
      break;
    case VPU_JR:
      pendingBranchTaken = true;
      pendingBranchTarget =
        integerValueForExecution(pipeline->srcReg1) &
        (microMem.size() - 1);
      break;
    default:
      throw runtime_error("Unsupported VU branch instruction.");
  }
}

void VPU::completeBranchDelaySlot()
{
  if (pendingBranchTaken)
  {
    microMemPC = pendingBranchTarget;
  }
  if (pendingBranchLinkValid)
  {
    intRegisters[pendingBranchLinkRegister] = pendingBranchLinkValue;
  }

  branchDelaySlotPending = false;
  pendingBranchTaken = false;
  pendingBranchLinkValid = false;
}

void VPU::startLowerFMACInstruction(const LowerInstruction &instruction)
{
  if (instruction.opCode == VPU_MFP && type != VPUType::VU1)
  {
    throw runtime_error("MFP is only supported on VU1.");
  }

  Pipeline *pipeline = orchestrator.startPipeline(
    VPU_PIPELINE_TYPE_FMAC,
    instruction.opCode,
    instruction.sourceRegister1,
    0,
    instruction.destinationRegister,
    instruction.destinationFieldMask,
    instruction.sourceFieldMask1,
    FP_REGISTER_NO_FIELDS,
    pendingLowerInstructionAddress,
    pendingLowerWritebackDiscarded,
    instruction.immediate);
  pipeline->integerDestReg = instruction.integerDestinationRegister;
  if (instruction.opCode == VPU_MTIR &&
      pipeline->integerDestReg != VPU_REGISTER_VI00)
  {
    pendingIntegerWrites[pipeline->integerDestReg]++;
  }
}

void VPU::startFDIVInstruction(const LowerInstruction &instruction)
{
  orchestrator.startPipeline(
    VPU_PIPELINE_TYPE_FDIV,
    instruction.opCode,
    instruction.sourceRegister1,
    instruction.sourceRegister2,
    VPU_REGISTER_VF00,
    FP_REGISTER_NO_FIELDS,
    instruction.sourceFieldMask1,
    instruction.sourceFieldMask2,
    pendingLowerInstructionAddress);
}

void VPU::startEFUInstruction(const LowerInstruction &instruction)
{
  if (type != VPUType::VU1)
  {
    throw runtime_error("EFU instructions are only supported on VU1.");
  }

  orchestrator.startPipeline(
    VPU_PIPELINE_TYPE_EFU,
    instruction.opCode,
    instruction.sourceRegister1,
    0,
    VPU_REGISTER_VF00,
    FP_REGISTER_NO_FIELDS,
    instruction.sourceFieldMask1,
    FP_REGISTER_NO_FIELDS,
    pendingLowerInstructionAddress);
}

void VPU::startWaitQInstruction(const LowerInstruction &instruction)
{
  orchestrator.startPipeline(
    VPU_PIPELINE_TYPE_WAITQ,
    instruction.opCode,
    0,
    0,
    VPU_REGISTER_VF00,
    FP_REGISTER_NO_FIELDS,
    FP_REGISTER_NO_FIELDS,
    FP_REGISTER_NO_FIELDS,
    pendingLowerInstructionAddress);
}

void VPU::startWaitPInstruction(const LowerInstruction &instruction)
{
  if (type != VPUType::VU1)
  {
    throw runtime_error("WAITP is only supported on VU1.");
  }

  orchestrator.startPipeline(
    VPU_PIPELINE_TYPE_WAITP,
    instruction.opCode,
    0,
    0,
    VPU_REGISTER_VF00,
    FP_REGISTER_NO_FIELDS,
    FP_REGISTER_NO_FIELDS,
    FP_REGISTER_NO_FIELDS,
    pendingLowerInstructionAddress);
}

void VPU::startFlagInstruction(const LowerInstruction &instruction)
{
  Pipeline *pipeline = orchestrator.startPipeline(
    VPU_PIPELINE_TYPE_FLAG,
    instruction.opCode,
    instruction.sourceRegister1,
    0,
    0,
    FP_REGISTER_NO_FIELDS,
    FP_REGISTER_NO_FIELDS,
    FP_REGISTER_NO_FIELDS,
    pendingLowerInstructionAddress);
  pipeline->integerDestReg = instruction.integerDestinationRegister;
  pipeline->immediateBits = instruction.immediateBits;
}

void VPU::startRandomInstruction(const LowerInstruction &instruction)
{
  orchestrator.startPipeline(
    VPU_PIPELINE_TYPE_RANDOM,
    instruction.opCode,
    instruction.sourceRegister1,
    0,
    instruction.destinationRegister,
    instruction.destinationFieldMask,
    instruction.sourceFieldMask1,
    FP_REGISTER_NO_FIELDS,
    pendingLowerInstructionAddress,
    pendingLowerWritebackDiscarded);
}

void VPU::startXGKICKInstruction(const LowerInstruction &instruction)
{
  if (type != VPUType::VU1)
  {
    throw runtime_error("XGKICK is only supported on VU1.");
  }

  orchestrator.startPipeline(
    VPU_PIPELINE_TYPE_XGKICK,
    instruction.opCode,
    instruction.sourceRegister1,
    VPU_REGISTER_VI00,
    VPU_REGISTER_VI00,
    FP_REGISTER_NO_FIELDS,
    FP_REGISTER_NO_FIELDS,
    FP_REGISTER_NO_FIELDS,
    pendingLowerInstructionAddress);
}

bool VPU::startXGKICKTransfer(Pipeline *pipeline)
{
  if (!pipeline->intSource1Sampled)
  {
    pipeline->intSourceValue1 =
      integerSourceValueForPipeline(pipeline, pipeline->srcReg1);
    pipeline->intSource1Sampled = true;
  }
  if (pipeline->xgkickStarted)
  {
    return true;
  }
  if (xgkickHandler && xgkickHandler->path1TransferActive())
  {
    xgkickWaiting = true;
    return false;
  }

  if (xgkickHandler)
  {
    const uint16_t qwordMask =
      static_cast<uint16_t>(vuMem.size() / 16 - 1);
    xgkickHandler->startPath1Transfer(
      pipeline->intSourceValue1 & qwordMask);
    xgkickTransferStarted = true;
  }
  pipeline->xgkickStarted = true;
  xgkickWaiting = false;
  return true;
}

bool VPU::xgkickStallsIssue()
{
  if (xgkickWaiting)
  {
    return true;
  }
  if (!xgkickTransferStarted || !xgkickHandler)
  {
    return false;
  }
  if (xgkickHandler->path1TransferActive())
  {
    return true;
  }

  xgkickTransferStarted = false;
  return false;
}

void VPU::startLSUInstruction(const LowerInstruction &instruction)
{
  const bool readsVectorRegister =
    instruction.opCode == VPU_SQ ||
    instruction.opCode == VPU_SQD ||
    instruction.opCode == VPU_SQI;
  Pipeline *pipeline = orchestrator.startPipeline(
    VPU_PIPELINE_TYPE_LSU,
    instruction.opCode,
    instruction.sourceRegister1,
    instruction.sourceRegister2,
    instruction.destinationRegister,
    instruction.destinationFieldMask,
    readsVectorRegister
      ? instruction.destinationFieldMask
      : FP_REGISTER_NO_FIELDS,
    0,
    pendingLowerInstructionAddress,
    pendingLowerWritebackDiscarded,
    instruction.immediate);
  pipeline->integerDestReg = instruction.integerDestinationRegister;

  if (pipeline->integerDestReg != VPU_REGISTER_VI00)
  {
    pendingIntegerWrites[pipeline->integerDestReg]++;
  }
}

bool VPU::hasPendingIntegerWrite(uint8_t registerID) const
{
  return
    registerID != VPU_REGISTER_VI00 &&
    pendingIntegerWrites[registerID] != 0;
}

uint16_t VPU::integerValueForExecution(uint8_t registerID) const
{
  if (registerID == VPU_REGISTER_VI00)
  {
    return 0;
  }
  if (pendingIALUWrites[registerID] != 0)
  {
    return bypassedIntegerValues[registerID];
  }
  return intRegisters[registerID];
}

uint16_t VPU::integerSourceValueForPipeline(
  const Pipeline *pipeline,
  uint8_t registerID) const
{
  if (registerID == VPU_REGISTER_VI00)
  {
    return 0;
  }

  uint8_t olderPendingWrites = pendingIALUWrites[registerID];
  if (pipeline->type == VPU_PIPELINE_TYPE_IALU &&
      pipeline->destReg == registerID &&
      olderPendingWrites != 0)
  {
    olderPendingWrites--;
  }

  if (olderPendingWrites != 0)
  {
    return bypassedIntegerValues[registerID];
  }
  return intRegisters[registerID];
}

bool VPU::lowerInstructionStalls(const LowerInstruction &instruction) const
{
  switch (instruction.unit)
  {
    case LowerExecutionUnit::None:
    case LowerExecutionUnit::Immediate:
      return false;
    case LowerExecutionUnit::IALU:
      switch (instruction.opCode)
      {
        case VPU_IADD:
        case VPU_IAND:
        case VPU_IOR:
        case VPU_ISUB:
          return
            hasPendingIntegerWrite(instruction.sourceRegister1) ||
            hasPendingIntegerWrite(instruction.sourceRegister2);
        case VPU_IADDI:
        case VPU_IADDIU:
        case VPU_ISUBIU:
          return hasPendingIntegerWrite(instruction.sourceRegister1);
        default:
          throw runtime_error("Unsupported VU IALU hazard check.");
      }
    case LowerExecutionUnit::LSU:
      switch (instruction.opCode)
      {
        case VPU_ILW:
        case VPU_ILWR:
        case VPU_LQ:
        case VPU_LQD:
        case VPU_LQI:
          return hasPendingIntegerWrite(instruction.sourceRegister1);
        case VPU_ISW:
        case VPU_ISWR:
          return
            hasPendingIntegerWrite(instruction.sourceRegister1) ||
            hasPendingIntegerWrite(instruction.sourceRegister2);
        case VPU_SQ:
        case VPU_SQD:
        case VPU_SQI:
          return
            hasPendingIntegerWrite(instruction.sourceRegister2) ||
            orchestrator.hasRegisterHazard(
              instruction.sourceRegister1,
              instruction.destinationFieldMask,
              VPU_REGISTER_VF00,
              FP_REGISTER_NO_FIELDS);
        default:
          throw runtime_error("Unsupported VU LSU hazard check.");
      }
    case LowerExecutionUnit::FMAC:
      if (instruction.opCode == VPU_MFIR)
      {
        return hasPendingIntegerWrite(instruction.sourceRegister1);
      }
      if (instruction.opCode == VPU_MFP)
      {
        return false;
      }
      if (instruction.opCode == VPU_MOVE ||
          instruction.opCode == VPU_MR32 ||
          instruction.opCode == VPU_MTIR)
      {
        return orchestrator.hasRegisterHazard(
          instruction.sourceRegister1,
          instruction.sourceFieldMask1,
          VPU_REGISTER_VF00,
          FP_REGISTER_NO_FIELDS);
      }
      throw runtime_error("Unsupported VU lower FMAC hazard check.");
    case LowerExecutionUnit::FDIV:
      return orchestrator.hasRegisterHazard(
        instruction.sourceRegister1,
        instruction.sourceFieldMask1,
        instruction.sourceRegister2,
        instruction.sourceFieldMask2);
    case LowerExecutionUnit::EFU:
      return orchestrator.hasRegisterHazard(
        instruction.sourceRegister1,
        instruction.sourceFieldMask1,
        VPU_REGISTER_VF00,
        FP_REGISTER_NO_FIELDS);
    case LowerExecutionUnit::WaitQ:
    case LowerExecutionUnit::WaitP:
      return false;
    case LowerExecutionUnit::Flag:
      switch (instruction.opCode)
      {
        case VPU_FMAND:
        case VPU_FMEQ:
        case VPU_FMOR:
          return hasPendingIntegerWrite(instruction.sourceRegister1);
        default:
          return false;
      }
    case LowerExecutionUnit::Random:
      if (instruction.opCode == VPU_RINIT ||
          instruction.opCode == VPU_RXOR)
      {
        return orchestrator.hasRegisterHazard(
          instruction.sourceRegister1,
          instruction.sourceFieldMask1,
          VPU_REGISTER_VF00,
          FP_REGISTER_NO_FIELDS);
      }
      return false;
    case LowerExecutionUnit::XGKICK:
      return hasPendingIntegerWrite(instruction.sourceRegister1);
    case LowerExecutionUnit::Branch:
      switch (instruction.opCode)
      {
        case VPU_IBNE:
        case VPU_IBEQ:
          return
            hasPendingIntegerWrite(instruction.sourceRegister1) ||
            hasPendingIntegerWrite(instruction.sourceRegister2);
        case VPU_IBGEZ:
        case VPU_IBGTZ:
        case VPU_IBLEZ:
        case VPU_IBLTZ:
        case VPU_JALR:
        case VPU_JR:
          return hasPendingIntegerWrite(instruction.sourceRegister1);
        case VPU_B:
        case VPU_BAL:
          return false;
        default:
          throw runtime_error("Unsupported VU branch hazard check.");
      }
  }

  throw runtime_error("Unsupported VU lower execution unit.");
}

bool VPU::lowerInstructionForbiddenInEndDelaySlot(const LowerInstruction &instruction) const
{
  return
    instruction.unit == LowerExecutionUnit::LSU ||
    instruction.unit == LowerExecutionUnit::XGKICK ||
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

uint32_t VPU::qRegisterBits() const
{
  return qRegister.bits();
}

uint32_t VPU::pRegisterBits() const
{
  return pRegister.bits();
}

uint32_t VPU::rRegisterBits() const
{
  return VU_FLOAT_ONE_BITS | (rRegister & FP_MAX_MANTISSA);
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

void VPU::loadPRegister(double value)
{
  pRegister = value;
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
}

bool VPU::pipelineCanAdvance(Pipeline *pipeline)
{
  if (pipeline->type == VPU_PIPELINE_TYPE_XGKICK &&
      pipeline->stage() == VUPipelineStage::T)
  {
    return startXGKICKTransfer(pipeline);
  }
  return true;
}

void VPU::pipelineAdvanced(Pipeline *p)
{
  if (p->stage() == VUPipelineStage::T)
  {
    switch (p->type)
    {
      case VPU_PIPELINE_TYPE_FMAC:
        startFMACPipeline(p);
        break;
      case VPU_PIPELINE_TYPE_IALU:
        startIALUPipeline(p);
        break;
      case VPU_PIPELINE_TYPE_LSU:
        startLSUPipeline(p);
        break;
      case VPU_PIPELINE_TYPE_XGKICK:
        startXGKICKTransfer(p);
        break;
      case VPU_PIPELINE_TYPE_BRANCH:
        evaluateBranchPipeline(p);
        break;
      case VPU_PIPELINE_TYPE_I_REGISTER:
        iRegister.setBits(p->immediateBits);
        break;
      case VPU_PIPELINE_TYPE_FDIV:
        executeFDIVPipeline(p);
        break;
      case VPU_PIPELINE_TYPE_EFU:
        executeEFUPipeline(p);
        break;
      case VPU_PIPELINE_TYPE_RANDOM:
        executeRandomPipeline(p);
        break;
    }
  }
  else if (p->type == VPU_PIPELINE_TYPE_IALU &&
           p->stage() == VUPipelineStage::X)
  {
    executeIALUPipeline(p);
  }
}

void VPU::executeFDIVPipeline(Pipeline *pipeline)
{
  auto selectedBits = [](const FPRegister &reg, uint8_t fieldMask) {
    switch (fieldMask)
    {
      case FP_REGISTER_X_FIELD:
        return reg.x.bits();
      case FP_REGISTER_Y_FIELD:
        return reg.y.bits();
      case FP_REGISTER_Z_FIELD:
        return reg.z.bits();
      case FP_REGISTER_W_FIELD:
        return reg.w.bits();
      default:
        throw runtime_error("VU FDIV source must select exactly one field.");
    }
  };

  VUFloatResult result;
  switch (pipeline->opCode)
  {
    case VPU_DIV:
      result = divFPRaw(
        selectedBits(
          fpRegisters[pipeline->srcReg1],
          pipeline->srcReg1FieldMask),
        selectedBits(
          fpRegisters[pipeline->srcReg2],
          pipeline->srcReg2FieldMask));
      break;
    case VPU_SQRT:
      result = sqrtFPRaw(
        selectedBits(
          fpRegisters[pipeline->srcReg2],
          pipeline->srcReg2FieldMask));
      break;
    case VPU_RSQRT:
      result = rsqrtFPRaw(
        selectedBits(
          fpRegisters[pipeline->srcReg1],
          pipeline->srcReg1FieldMask),
        selectedBits(
          fpRegisters[pipeline->srcReg2],
          pipeline->srcReg2FieldMask));
      break;
    default:
      throw runtime_error("Unsupported VU FDIV instruction.");
  }

  pipeline->scalarResultBits = result.bits;
  pipeline->scalarResultFlags = result.flags;
}

void VPU::executeRandomPipeline(Pipeline *pipeline)
{
  constexpr uint32_t RANDOM_MANTISSA_MASK = FP_MAX_MANTISSA;

  if (pipeline->opCode == VPU_RINIT ||
      pipeline->opCode == VPU_RXOR)
  {
    const uint32_t source =
      selectedLaneBits(
        fpRegisters[pipeline->srcReg1],
        pipeline->srcReg1FieldMask) &
      RANDOM_MANTISSA_MASK;
    if (pipeline->opCode == VPU_RINIT)
    {
      rRegister = source;
    }
    else
    {
      rRegister = (rRegister ^ source) & RANDOM_MANTISSA_MASK;
    }
    return;
  }

  if (pipeline->opCode == VPU_RNEXT)
  {
    const uint32_t feedback =
      ((rRegister >> 4) ^ (rRegister >> 22)) & 1;
    rRegister =
      ((rRegister << 1) | feedback) & RANDOM_MANTISSA_MASK;
  }
  else if (pipeline->opCode != VPU_RGET)
  {
    throw runtime_error("Unsupported VU random instruction.");
  }

  FPRegister result(fpRegisters[pipeline->destReg]);
  const uint32_t randomBits = rRegisterBits();
  if (hasFlag(pipeline->destFieldMask, FP_REGISTER_X_FIELD))
  {
    result.x.setBits(randomBits);
  }
  if (hasFlag(pipeline->destFieldMask, FP_REGISTER_Y_FIELD))
  {
    result.y.setBits(randomBits);
  }
  if (hasFlag(pipeline->destFieldMask, FP_REGISTER_Z_FIELD))
  {
    result.z.setBits(randomBits);
  }
  if (hasFlag(pipeline->destFieldMask, FP_REGISTER_W_FIELD))
  {
    result.w.setBits(randomBits);
  }
  pipeline->setFPRegisterResult(&result);
}

void VPU::finishFDIVPipeline(Pipeline *pipeline)
{
  qRegister.setBits(pipeline->scalarResultBits);

  unsetFlag(statusFlags, VPU_FLAG_I);
  unsetFlag(statusFlags, VPU_FLAG_D);
  if (hasFlag(pipeline->scalarResultFlags, FP_FLAG_I_BIT))
  {
    setFlag(statusFlags, VPU_FLAG_I);
    setFlag(statusFlags, VPU_FLAG_IS);
  }
  if (hasFlag(pipeline->scalarResultFlags, FP_FLAG_D_BIT))
  {
    setFlag(statusFlags, VPU_FLAG_D);
    setFlag(statusFlags, VPU_FLAG_DS);
  }
}

void VPU::executeEFUPipeline(Pipeline *pipeline)
{
  const FPRegister &source = fpRegisters[pipeline->srcReg1];
  if (pipeline->opCode == VPU_ESUM)
  {
    VUFloatResult sum = addFPRaw(source.x.bits(), source.y.bits());
    sum = addFPRaw(sum.bits, source.z.bits());
    sum = addFPRaw(sum.bits, source.w.bits());
    pipeline->scalarResultBits = sum.bits;
    return;
  }
  if (pipeline->opCode == VPU_ESADD ||
      pipeline->opCode == VPU_ELENG ||
      pipeline->opCode == VPU_ERLENG ||
      pipeline->opCode == VPU_ERSADD)
  {
    const VUFloatResult xSquared =
      mulFPRaw(source.x.bits(), source.x.bits());
    const VUFloatResult ySquared =
      mulFPRaw(source.y.bits(), source.y.bits());
    const VUFloatResult zSquared =
      mulFPRaw(source.z.bits(), source.z.bits());
    VUFloatResult sum = addFPRaw(xSquared.bits, ySquared.bits);
    sum = addFPRaw(sum.bits, zSquared.bits);
    if (pipeline->opCode == VPU_ESADD)
    {
      pipeline->scalarResultBits = sum.bits;
    }
    else if (pipeline->opCode == VPU_ELENG)
    {
      pipeline->scalarResultBits = sqrtFPRaw(sum.bits).bits;
    }
    else if (pipeline->opCode == VPU_ERLENG)
    {
      pipeline->scalarResultBits =
        rsqrtFPRaw(VU_FLOAT_ONE_BITS, sum.bits).bits;
    }
    else
    {
      pipeline->scalarResultBits =
        divFPRaw(VU_FLOAT_ONE_BITS, sum.bits).bits;
    }
    return;
  }
  if (pipeline->opCode == VPU_EATANxy ||
      pipeline->opCode == VPU_EATANxz)
  {
    const uint32_t coordinateBits =
      pipeline->opCode == VPU_EATANxy
        ? source.y.bits()
        : source.z.bits();
    const uint32_t numeratorBits = subtractVUFloatBits(
      coordinateBits,
      source.x.bits());
    const uint32_t denominatorBits = addVUFloatBits(
      coordinateBits,
      source.x.bits());
    pipeline->scalarResultBits = calculateEATAN(
      divideVUFloatBits(numeratorBits, denominatorBits));
    return;
  }
  const uint32_t sourceBits = selectedLaneBits(
    source,
    pipeline->srcReg1FieldMask);
  if (pipeline->opCode == VPU_ESQRT)
  {
    pipeline->scalarResultBits = sqrtFPRaw(sourceBits).bits;
    return;
  }
  if (pipeline->opCode == VPU_ERSQRT)
  {
    pipeline->scalarResultBits =
      rsqrtFPRaw(VU_FLOAT_ONE_BITS, sourceBits).bits;
    return;
  }
  if (pipeline->opCode == VPU_ERCPR)
  {
    pipeline->scalarResultBits =
      divFPRaw(VU_FLOAT_ONE_BITS, sourceBits).bits;
    return;
  }
  if (pipeline->opCode == VPU_ESIN)
  {
    pipeline->scalarResultBits = calculateESIN(sourceBits);
    return;
  }
  if (pipeline->opCode == VPU_EEXP)
  {
    pipeline->scalarResultBits = calculateEEXP(sourceBits);
    return;
  }
  if (pipeline->opCode == VPU_EATAN)
  {
    const uint32_t numeratorBits = subtractVUFloatBits(
      sourceBits,
      VU_FLOAT_ONE_BITS);
    const uint32_t denominatorBits = addVUFloatBits(
      sourceBits,
      VU_FLOAT_ONE_BITS);
    pipeline->scalarResultBits = calculateEATAN(
      divideVUFloatBits(numeratorBits, denominatorBits));
    return;
  }
  throw runtime_error("Unsupported VU EFU instruction.");
}

void VPU::finishEFUPipeline(Pipeline *pipeline)
{
  pRegister.setBits(pipeline->scalarResultBits);
}

void VPU::startFMACPipeline(Pipeline *p)
{
  uint16_t opCode = p->opCode;
  uint8_t ft = p->srcReg1;
  uint8_t fs = p->srcReg2;
  uint8_t fieldMask = p->destFieldMask;
  FPRegister *destReg = destinationRegisterFromPipeline(p);
  FPRegister *accumulatorInput =
    accumulatorForwardValid ? &accumulatorForwardValue : &accumulator;
  p->accumulatorValue.copyFrom(accumulatorInput);
  FPRegister dest =
    destReg == &accumulator
      ? p->accumulatorValue
      : FPRegister(destReg->x, destReg->y, destReg->z, destReg->w);

  if (opCode == VPU_MFIR)
  {
    int32_t value =
      static_cast<int16_t>(integerValueForExecution(p->srcReg1));

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

  if (opCode == VPU_MFP)
  {
    if (hasFlag(fieldMask, FP_REGISTER_X_FIELD))
    {
      dest.x = pRegister;
    }
    if (hasFlag(fieldMask, FP_REGISTER_Y_FIELD))
    {
      dest.y = pRegister;
    }
    if (hasFlag(fieldMask, FP_REGISTER_Z_FIELD))
    {
      dest.z = pRegister;
    }
    if (hasFlag(fieldMask, FP_REGISTER_W_FIELD))
    {
      dest.w = pRegister;
    }

    p->setFPRegisterResult(&dest);
    return;
  }

  if (opCode == VPU_MOVE || opCode == VPU_MR32)
  {
    const FPRegister &source = fpRegisters[p->srcReg1];
    if (opCode == VPU_MOVE)
    {
      if (hasFlag(fieldMask, FP_REGISTER_X_FIELD))
      {
        dest.x = source.x;
      }
      if (hasFlag(fieldMask, FP_REGISTER_Y_FIELD))
      {
        dest.y = source.y;
      }
      if (hasFlag(fieldMask, FP_REGISTER_Z_FIELD))
      {
        dest.z = source.z;
      }
      if (hasFlag(fieldMask, FP_REGISTER_W_FIELD))
      {
        dest.w = source.w;
      }
    }
    else
    {
      if (hasFlag(fieldMask, FP_REGISTER_X_FIELD))
      {
        dest.x = source.y;
      }
      if (hasFlag(fieldMask, FP_REGISTER_Y_FIELD))
      {
        dest.y = source.z;
      }
      if (hasFlag(fieldMask, FP_REGISTER_Z_FIELD))
      {
        dest.z = source.w;
      }
      if (hasFlag(fieldMask, FP_REGISTER_W_FIELD))
      {
        dest.w = source.x;
      }
    }
    p->setFPRegisterResult(&dest);
    return;
  }

  if (opCode == VPU_MTIR)
  {
    const FPRegister &source = fpRegisters[p->srcReg1];
    switch (p->srcReg1FieldMask)
    {
      case FP_REGISTER_X_FIELD:
        p->setIntResult(source.x.bits() & 0xffff);
        break;
      case FP_REGISTER_Y_FIELD:
        p->setIntResult(source.y.bits() & 0xffff);
        break;
      case FP_REGISTER_Z_FIELD:
        p->setIntResult(source.z.bits() & 0xffff);
        break;
      case FP_REGISTER_W_FIELD:
        p->setIntResult(source.w.bits() & 0xffff);
        break;
      default:
        throw runtime_error("MTIR source must select exactly one field.");
    }
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
  prepareAccumulatorOperation(p);
  if (p->destReg == VPU_REGISTER_ACCUMULATOR)
  {
    accumulatorForwardValue.copyFrom(&p->operationResult);
    accumulatorForwardValid = true;
  }
}

uint8_t VPU::multiplicationOverflowFields(
  const Pipeline *pipeline) const
{
  uint8_t fields = FP_REGISTER_NO_FIELDS;
  if (hasFlag(pipeline->fpResult.xResultFlags, FP_FLAG_OVERFLOW))
  {
    setFlag(fields, FP_REGISTER_X_FIELD);
  }
  if (hasFlag(pipeline->fpResult.yResultFlags, FP_FLAG_OVERFLOW))
  {
    setFlag(fields, FP_REGISTER_Y_FIELD);
  }
  if (hasFlag(pipeline->fpResult.zResultFlags, FP_FLAG_OVERFLOW))
  {
    setFlag(fields, FP_REGISTER_Z_FIELD);
  }
  if (hasFlag(pipeline->fpResult.wResultFlags, FP_FLAG_OVERFLOW))
  {
    setFlag(fields, FP_REGISTER_W_FIELD);
  }
  return fields;
}

void VPU::prepareAccumulatorOperation(Pipeline *pipeline)
{
  switch (pipeline->opCode)
  {
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
    {
      pipeline->ignoredResultFields =
        multiplicationOverflowFields(pipeline);
      uint8_t calculatedFields =
        pipeline->destFieldMask & ~pipeline->ignoredResultFields;
      pipeline->operationResult.storeAdd(
        &pipeline->operationResult,
        &pipeline->accumulatorValue,
        calculatedFields);
      break;
    }
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
    case VPU_OPMSUB:
    {
      pipeline->ignoredResultFields =
        multiplicationOverflowFields(pipeline);
      if (hasFlag(pipeline->ignoredResultFields, FP_REGISTER_X_FIELD))
      {
        pipeline->flagResult.x.toggleSign();
        pipeline->operationResult.x.toggleSign();
      }
      if (hasFlag(pipeline->ignoredResultFields, FP_REGISTER_Y_FIELD))
      {
        pipeline->flagResult.y.toggleSign();
        pipeline->operationResult.y.toggleSign();
      }
      if (hasFlag(pipeline->ignoredResultFields, FP_REGISTER_Z_FIELD))
      {
        pipeline->flagResult.z.toggleSign();
        pipeline->operationResult.z.toggleSign();
      }
      if (hasFlag(pipeline->ignoredResultFields, FP_REGISTER_W_FIELD))
      {
        pipeline->flagResult.w.toggleSign();
        pipeline->operationResult.w.toggleSign();
      }
      uint8_t calculatedFields =
        pipeline->destFieldMask & ~pipeline->ignoredResultFields;
      pipeline->operationResult.storeSub(
        &pipeline->accumulatorValue,
        &pipeline->operationResult,
        calculatedFields);
      break;
    }
  }
}

void VPU::startLSUPipeline(Pipeline *pipeline)
{
  switch (pipeline->opCode)
  {
    case VPU_ILW:
    case VPU_ILWR:
    {
      uint16_t address = qwordAddress(
        integerValueForExecution(pipeline->srcReg1),
        pipeline->opCode == VPU_ILW ? pipeline->immediate : 0);
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
    case VPU_ISW:
    case VPU_ISWR:
      pipeline->memoryAddress = qwordAddress(
        integerValueForExecution(pipeline->srcReg1),
        pipeline->opCode == VPU_ISW ? pipeline->immediate : 0);
      pipeline->setIntResult(integerValueForExecution(pipeline->srcReg2));
      break;
    case VPU_LQ:
    case VPU_LQD:
    case VPU_LQI:
    {
      uint16_t base = integerValueForExecution(pipeline->srcReg1);
      if (pipeline->opCode == VPU_LQD &&
          pipeline->integerDestReg != VPU_REGISTER_VI00)
      {
        base--;
      }
      pipeline->memoryAddress = qwordAddress(
        base,
        pipeline->opCode == VPU_LQ ? pipeline->immediate : 0);
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
      if (pipeline->opCode == VPU_LQD)
      {
        pipeline->setIntResult(base);
      }
      else if (pipeline->opCode == VPU_LQI)
      {
        pipeline->setIntResult(base + 1);
      }
      break;
    }
    case VPU_SQ:
    case VPU_SQD:
    case VPU_SQI:
    {
      uint16_t base = integerValueForExecution(pipeline->srcReg2);
      if (pipeline->opCode == VPU_SQD &&
          pipeline->integerDestReg != VPU_REGISTER_VI00)
      {
        base--;
      }
      pipeline->memoryAddress =
        qwordAddress(base, pipeline->opCode == VPU_SQ
          ? pipeline->immediate
          : 0);
      pipeline->setFPRegisterResult(&fpRegisters[pipeline->srcReg1]);
      if (pipeline->opCode == VPU_SQD)
      {
        pipeline->setIntResult(base);
      }
      else if (pipeline->opCode == VPU_SQI)
      {
        pipeline->setIntResult(base + 1);
      }
      break;
    }
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
  if (p->type == VPU_PIPELINE_TYPE_FDIV)
  {
    finishFDIVPipeline(p);
    emitTrace({
      VPUTraceEventType::PipelineWriteback,
      cycles,
      p->instructionAddress,
      0,
      0,
      p->opCode,
      VPU_REGISTER_VF00,
      FP_REGISTER_NO_FIELDS
    });
    return;
  }
  if (p->type == VPU_PIPELINE_TYPE_EFU)
  {
    finishEFUPipeline(p);
    emitTrace({
      VPUTraceEventType::PipelineWriteback,
      cycles,
      p->instructionAddress,
      0,
      0,
      p->opCode,
      VPU_REGISTER_VF00,
      FP_REGISTER_NO_FIELDS
    });
    return;
  }
  if (p->type == VPU_PIPELINE_TYPE_WAITQ)
  {
    return;
  }
  if (p->type == VPU_PIPELINE_TYPE_WAITP)
  {
    return;
  }
  if (p->type == VPU_PIPELINE_TYPE_FLAG)
  {
    switch (p->opCode)
    {
      case VPU_FCAND:
        intRegisters[VPU_REGISTER_VI01] =
          (clippingFlags & p->immediateBits) != 0;
        break;
      case VPU_FCEQ:
        intRegisters[VPU_REGISTER_VI01] =
          (clippingFlags & 0x00ffffff) == p->immediateBits;
        break;
      case VPU_FCGET:
        if (p->integerDestReg != VPU_REGISTER_VI00)
        {
          intRegisters[p->integerDestReg] = clippingFlags & 0x0fff;
        }
        break;
      case VPU_FCOR:
        intRegisters[VPU_REGISTER_VI01] =
          ((clippingFlags | p->immediateBits) & 0x00ffffff) ==
          0x00ffffff;
        break;
      case VPU_FCSET:
        clippingFlags = p->immediateBits;
        break;
      case VPU_FSAND:
        if (p->integerDestReg != VPU_REGISTER_VI00)
        {
          intRegisters[p->integerDestReg] =
            statusFlags & p->immediateBits;
        }
        break;
      case VPU_FSEQ:
        if (p->integerDestReg != VPU_REGISTER_VI00)
        {
          intRegisters[p->integerDestReg] =
            statusFlags == p->immediateBits;
        }
        break;
      case VPU_FSOR:
        if (p->integerDestReg != VPU_REGISTER_VI00)
        {
          intRegisters[p->integerDestReg] =
            statusFlags | p->immediateBits;
        }
        break;
      case VPU_FSSET:
        statusFlags =
          (statusFlags & 0x003f) |
          (p->immediateBits & 0x0fc0);
        break;
      case VPU_FMAND:
        if (p->integerDestReg != VPU_REGISTER_VI00)
        {
          intRegisters[p->integerDestReg] =
            MACFlags & integerValueForExecution(p->srcReg1);
        }
        break;
      case VPU_FMEQ:
        if (p->integerDestReg != VPU_REGISTER_VI00)
        {
          intRegisters[p->integerDestReg] =
            MACFlags == integerValueForExecution(p->srcReg1);
        }
        break;
      case VPU_FMOR:
        if (p->integerDestReg != VPU_REGISTER_VI00)
        {
          intRegisters[p->integerDestReg] =
            MACFlags | integerValueForExecution(p->srcReg1);
        }
        break;
      default:
        throw runtime_error("Unsupported VU clipping flag instruction.");
    }
    emitTrace({
      VPUTraceEventType::PipelineWriteback,
      cycles,
      p->instructionAddress,
      0,
      0,
      p->opCode,
      p->integerDestReg,
      FP_REGISTER_NO_FIELDS
    });
    return;
  }
  if (p->type == VPU_PIPELINE_TYPE_RANDOM)
  {
    if ((p->opCode == VPU_RGET || p->opCode == VPU_RNEXT) &&
        !p->discardWriteback)
    {
      updateDestinationRegisterWithPipelineResult(
        &fpRegisters[p->destReg],
        p);
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
    return;
  }
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
  if (p->type == VPU_PIPELINE_TYPE_IALU)
  {
    finishIALUPipeline(p);
    emitTrace({
      VPUTraceEventType::PipelineWriteback,
      cycles,
      p->instructionAddress,
      0,
      0,
      p->opCode,
      p->destReg,
      FP_REGISTER_NO_FIELDS
    });
    return;
  }
  if (p->type == VPU_PIPELINE_TYPE_BRANCH ||
      p->type == VPU_PIPELINE_TYPE_I_REGISTER)
  {
    return;
  }

  if (p->opCode == VPU_MTIR)
  {
    if (p->integerDestReg != VPU_REGISTER_VI00)
    {
      intRegisters[p->integerDestReg] = p->intResult;
      pendingIntegerWrites[p->integerDestReg]--;
    }
    emitTrace({
      VPUTraceEventType::PipelineWriteback,
      cycles,
      p->instructionAddress,
      0,
      0,
      p->opCode,
      p->integerDestReg,
      FP_REGISTER_NO_FIELDS
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
    case VPU_MFP:
    case VPU_MOVE:
    case VPU_MR32:
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

  finishAccumulatorWrite(p);
  VPUTraceEvent event{
    VPUTraceEventType::PipelineWriteback,
    cycles,
    p->instructionAddress,
    0,
    0,
    p->opCode,
    p->destReg,
    p->destFieldMask
  };
  if (traceCallback)
  {
    event.arithmetic = arithmeticTraceForPipeline(*p);
  }
  emitTrace(event);
}

void VPU::startIALUPipeline(Pipeline *pipeline)
{
  if (!pipeline->intSource1Sampled)
  {
    pipeline->intSourceValue1 =
      integerSourceValueForPipeline(pipeline, pipeline->srcReg1);
    pipeline->intSource1Sampled = true;
  }
  if (!pipeline->intSource2Sampled)
  {
    pipeline->intSourceValue2 =
      integerSourceValueForPipeline(pipeline, pipeline->srcReg2);
    pipeline->intSource2Sampled = true;
  }
}

void VPU::executeIALUPipeline(Pipeline *pipeline)
{
  uint16_t result;

  switch (pipeline->opCode)
  {
    case VPU_IADD:
      result =
        pipeline->intSourceValue1 +
        pipeline->intSourceValue2;
      break;
    case VPU_IADDI:
    case VPU_IADDIU:
      result =
        pipeline->intSourceValue1 +
        pipeline->immediate;
      break;
    case VPU_IAND:
      result =
        pipeline->intSourceValue1 &
        pipeline->intSourceValue2;
      break;
    case VPU_IOR:
      result =
        pipeline->intSourceValue1 |
        pipeline->intSourceValue2;
      break;
    case VPU_ISUB:
      result =
        pipeline->intSourceValue1 -
        pipeline->intSourceValue2;
      break;
    case VPU_ISUBIU:
      result =
        pipeline->intSourceValue1 -
        pipeline->immediate;
      break;
    default:
      throw runtime_error("Unsupported VU IALU instruction.");
  }

  pipeline->setIntResult(result);
  if (pipeline->destReg != VPU_REGISTER_VI00)
  {
    bypassedIntegerValues[pipeline->destReg] = result;
  }
}

void VPU::finishIALUPipeline(Pipeline *pipeline)
{
  if (pipeline->destReg == VPU_REGISTER_VI00)
  {
    return;
  }

  intRegisters[pipeline->destReg] = pipeline->intResult;
  pendingIALUWrites[pipeline->destReg]--;
  if (pendingIALUWrites[pipeline->destReg] == 0)
  {
    bypassedIntegerValues[pipeline->destReg] = 0;
  }
}

void VPU::finishLSUPipeline(Pipeline *pipeline)
{
  switch (pipeline->opCode)
  {
    case VPU_ILW:
    case VPU_ILWR:
      if (pipeline->integerDestReg != VPU_REGISTER_VI00)
      {
        intRegisters[pipeline->integerDestReg] = pipeline->intResult;
        pendingIntegerWrites[pipeline->integerDestReg]--;
      }
      break;
    case VPU_LQ:
    case VPU_LQD:
    case VPU_LQI:
      if (!pipeline->discardWriteback)
      {
        updateDestinationRegisterWithPipelineResult(
          &fpRegisters[pipeline->destReg],
          pipeline);
      }
      if (pipeline->integerDestReg != VPU_REGISTER_VI00)
      {
        intRegisters[pipeline->integerDestReg] = pipeline->intResult;
        pendingIntegerWrites[pipeline->integerDestReg]--;
      }
      break;
    case VPU_ISW:
    case VPU_ISWR:
      if (hasFlag(pipeline->destFieldMask, FP_REGISTER_X_FIELD))
      {
        writeDataWord(pipeline->memoryAddress, pipeline->intResult);
      }
      if (hasFlag(pipeline->destFieldMask, FP_REGISTER_Y_FIELD))
      {
        writeDataWord(pipeline->memoryAddress + 4, pipeline->intResult);
      }
      if (hasFlag(pipeline->destFieldMask, FP_REGISTER_Z_FIELD))
      {
        writeDataWord(pipeline->memoryAddress + 8, pipeline->intResult);
      }
      if (hasFlag(pipeline->destFieldMask, FP_REGISTER_W_FIELD))
      {
        writeDataWord(pipeline->memoryAddress + 12, pipeline->intResult);
      }
      break;
    case VPU_SQ:
    case VPU_SQD:
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
      if (pipeline->integerDestReg != VPU_REGISTER_VI00)
      {
        intRegisters[pipeline->integerDestReg] = pipeline->intResult;
        pendingIntegerWrites[pipeline->integerDestReg]--;
      }
      break;
  }
}

void VPU::handleMADDInstruction(Pipeline * p)
{
  FPRegister *destReg = destinationRegisterFromPipeline(p);
  setFlags(&p->flagResult, FP_REGISTER_NO_FIELDS);
  if (destReg != &fpRegisters[VPU_REGISTER_VF00])
  {
    destReg->copyFieldsFrom(&p->operationResult, p->destFieldMask);
  }
  setFlags(destReg, p->ignoredResultFields);
}

void VPU::handleMSUBInstruction(Pipeline * p)
{
  FPRegister *destReg = destinationRegisterFromPipeline(p);
  setFlags(&p->flagResult, FP_REGISTER_NO_FIELDS);
  if (destReg != &fpRegisters[VPU_REGISTER_VF00])
  {
    destReg->copyFieldsFrom(&p->operationResult, p->destFieldMask);
  }
  setFlags(destReg, p->ignoredResultFields);
}

void VPU::handleOPMSUBInstruction(Pipeline * p)
{
  FPRegister *destReg = destinationRegisterFromPipeline(p);

  setFlags(&p->flagResult, FP_REGISTER_NO_FIELDS);
  if (destReg != &fpRegisters[VPU_REGISTER_VF00])
  {
    destReg->copyFieldsFrom(&p->operationResult, p->destFieldMask);
  }
  setFlags(destReg, p->ignoredResultFields);
}

void VPU::finishAccumulatorWrite(Pipeline *pipeline)
{
  if (pipeline->destReg != VPU_REGISTER_ACCUMULATOR)
  {
    return;
  }

  pendingAccumulatorWrites--;
  if (pendingAccumulatorWrites == 0)
  {
    accumulatorForwardValid = false;
  }
}
