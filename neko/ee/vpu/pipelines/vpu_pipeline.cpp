#include "vpu_pipeline.hpp"
#include "vpu_opcodes.hpp"

#include <stdexcept>

Pipeline::Pipeline() : type(0), opCode(0), intResult(0), ignoredResultFields(0), srcReg1(0), srcReg2(0), destReg(0), integerDestReg(0), destFieldMask(0), srcReg1FieldMask(0), srcReg2FieldMask(0), instructionAddress(0), memoryAddress(0), immediate(0), immediateBits(0), scalarResultBits(0), scalarResultFlags(0), intSourceValue1(0), intSourceValue2(0), intSource1Sampled(false), intSource2Sampled(false), xgkickStarted(false), discardWriteback(false), currentStage(VUPipelineStage::M), currentStageIndex(0), executionStageCount(0), complete(false)
{
}

void Pipeline::configure(uint8_t pipelineType, uint16_t oc, uint8_t s1, uint8_t s2, uint8_t d, uint8_t destMask, uint8_t s1Mask, uint8_t s2Mask, uint16_t address, bool discard, int16_t immediateValue)
{
  type = pipelineType;
  opCode = oc;
  srcReg1 = s1;
  srcReg2 = s2;
  destReg = d;
  integerDestReg = 0;
  destFieldMask = destMask;
  srcReg1FieldMask = s1Mask;
  srcReg2FieldMask = s2Mask;
  instructionAddress = address;
  discardWriteback = discard;
  immediate = immediateValue;
  immediateBits = 0;
  scalarResultBits = 0;
  scalarResultFlags = 0;
  intSourceValue1 = 0;
  intSourceValue2 = 0;
  flagResult = FPRegister();
  operationResult = FPRegister();
  accumulatorValue = FPRegister();
  ignoredResultFields = FP_REGISTER_NO_FIELDS;
  intSource1Sampled = false;
  intSource2Sampled = false;
  xgkickStarted = false;
  currentStage = VUPipelineStage::M;
  currentStageIndex = 0;
  executionStageCount = 0;
  complete = false;
  configureTiming();
}

void Pipeline::configureTiming()
{
  if (type == VPU_PIPELINE_TYPE_FDIV)
  {
    switch (opCode)
    {
      case VPU_DIV:
      case VPU_SQRT:
        executionStageCount = 6;
        return;
      case VPU_RSQRT:
        executionStageCount = 12;
        return;
      default:
        throw std::runtime_error(
          "VU FDIV operation does not have defined stage timing.");
    }
  }

  if (type == VPU_PIPELINE_TYPE_EFU)
  {
    switch (opCode)
    {
      case VPU_EATAN:
      case VPU_EATANxy:
      case VPU_EATANxz:
        executionStageCount = 53;
        return;
      case VPU_EEXP:
        executionStageCount = 43;
        return;
      case VPU_ELENG:
      case VPU_ERSADD:
      case VPU_ERSQRT:
        executionStageCount = 17;
        return;
      case VPU_ERCPR:
      case VPU_ESQRT:
      case VPU_ESUM:
        executionStageCount = 11;
        return;
      case VPU_ERLENG:
        executionStageCount = 23;
        return;
      case VPU_ESADD:
        executionStageCount = 10;
        return;
      case VPU_ESIN:
        executionStageCount = 28;
        return;
      default:
        throw std::runtime_error(
          "VU EFU operation does not have defined stage timing.");
    }
  }
}

void Pipeline::setFPRegisterResult(FPRegister * reg)
{
  fpResult.copyFrom(reg);
  flagResult.copyFrom(reg);
  operationResult.copyFrom(reg);
  ignoredResultFields = FP_REGISTER_NO_FIELDS;
}

void Pipeline::setIntResult(int i)
{
  intResult = i;
}

void Pipeline::advanceStage()
{
  switch (type)
  {
    case VPU_PIPELINE_TYPE_FMAC:
    case VPU_PIPELINE_TYPE_LSU:
    case VPU_PIPELINE_TYPE_XGKICK:
      advanceSixStagePipeline();
      break;
    case VPU_PIPELINE_TYPE_FLAG:
      if (opCode == VPU_FCSET || opCode == VPU_FSSET)
      {
        advanceSixStagePipeline();
      }
      else
      {
        advanceTwoStagePipeline();
      }
      break;
    case VPU_PIPELINE_TYPE_RANDOM:
      if (opCode == VPU_RGET || opCode == VPU_RNEXT)
      {
        advanceSixStagePipeline();
      }
      else
      {
        advanceTwoStagePipeline();
      }
      break;
    case VPU_PIPELINE_TYPE_IALU:
      advanceIALUPipeline();
      break;
    case VPU_PIPELINE_TYPE_BRANCH:
    case VPU_PIPELINE_TYPE_I_REGISTER:
    case VPU_PIPELINE_TYPE_WAITQ:
    case VPU_PIPELINE_TYPE_WAITP:
    case VPU_PIPELINE_TYPE_VIF_CONTROL:
      advanceTwoStagePipeline();
      break;
    case VPU_PIPELINE_TYPE_FDIV:
      advanceFDIVPipeline();
      break;
    case VPU_PIPELINE_TYPE_EFU:
      advanceEFUPipeline();
      break;
  }
}

void Pipeline::advanceTwoStagePipeline()
{
  currentStage = VUPipelineStage::T;
  complete = true;
}

void Pipeline::advanceSixStagePipeline()
{
  switch (currentStage)
  {
    case VUPipelineStage::M:
      currentStage = VUPipelineStage::T;
      break;
    case VUPipelineStage::T:
      currentStage = VUPipelineStage::X;
      break;
    case VUPipelineStage::X:
      currentStage = VUPipelineStage::Y;
      break;
    case VUPipelineStage::Y:
      currentStage = VUPipelineStage::Z;
      break;
    case VUPipelineStage::Z:
      currentStage = VUPipelineStage::S;
      complete = true;
      break;
    case VUPipelineStage::S:
    case VUPipelineStage::IY:
    case VUPipelineStage::IZ:
    case VUPipelineStage::D:
    case VUPipelineStage::F:
    case VUPipelineStage::N:
    case VUPipelineStage::P:
      break;
  }
}

void Pipeline::advanceIALUPipeline()
{
  switch (currentStage)
  {
    case VUPipelineStage::M:
      currentStage = VUPipelineStage::T;
      break;
    case VUPipelineStage::T:
      currentStage = VUPipelineStage::X;
      break;
    case VUPipelineStage::X:
      currentStage = VUPipelineStage::IY;
      break;
    case VUPipelineStage::IY:
      currentStage = VUPipelineStage::IZ;
      break;
    case VUPipelineStage::IZ:
      currentStage = VUPipelineStage::S;
      complete = true;
      break;
    case VUPipelineStage::Y:
    case VUPipelineStage::Z:
    case VUPipelineStage::S:
    case VUPipelineStage::D:
    case VUPipelineStage::F:
    case VUPipelineStage::N:
    case VUPipelineStage::P:
      break;
  }
}

void Pipeline::advanceFDIVPipeline()
{
  if (currentStage == VUPipelineStage::M)
  {
    currentStage = VUPipelineStage::T;
    return;
  }
  if (currentStage == VUPipelineStage::T)
  {
    currentStage = VUPipelineStage::D;
    currentStageIndex = 1;
    return;
  }
  if (currentStage == VUPipelineStage::D &&
      currentStageIndex < executionStageCount)
  {
    currentStageIndex++;
    return;
  }

  currentStage = VUPipelineStage::F;
  currentStageIndex = 0;
  complete = true;
}

void Pipeline::advanceEFUPipeline()
{
  if (currentStage == VUPipelineStage::M)
  {
    currentStage = VUPipelineStage::T;
    return;
  }
  if (currentStage == VUPipelineStage::T)
  {
    currentStage = VUPipelineStage::N;
    currentStageIndex = 1;
    return;
  }
  if (currentStage == VUPipelineStage::N &&
      currentStageIndex < executionStageCount)
  {
    currentStageIndex++;
    return;
  }

  currentStage = VUPipelineStage::P;
  currentStageIndex = 0;
  complete = true;
}

bool Pipeline::isComplete() const
{
  return complete;
}

bool Pipeline::completesOnNextAdvance() const
{
  switch (type)
  {
    case VPU_PIPELINE_TYPE_FMAC:
    case VPU_PIPELINE_TYPE_LSU:
    case VPU_PIPELINE_TYPE_XGKICK:
      return currentStage == VUPipelineStage::Z;
    case VPU_PIPELINE_TYPE_IALU:
      return currentStage == VUPipelineStage::IZ;
    case VPU_PIPELINE_TYPE_FLAG:
      return
        opCode == VPU_FCSET || opCode == VPU_FSSET
          ? currentStage == VUPipelineStage::Z
          : currentStage == VUPipelineStage::M;
    case VPU_PIPELINE_TYPE_RANDOM:
      return
        opCode == VPU_RGET || opCode == VPU_RNEXT
          ? currentStage == VUPipelineStage::Z
          : currentStage == VUPipelineStage::M;
    case VPU_PIPELINE_TYPE_BRANCH:
    case VPU_PIPELINE_TYPE_I_REGISTER:
    case VPU_PIPELINE_TYPE_WAITQ:
    case VPU_PIPELINE_TYPE_WAITP:
    case VPU_PIPELINE_TYPE_VIF_CONTROL:
      return currentStage == VUPipelineStage::M;
    case VPU_PIPELINE_TYPE_FDIV:
      return
        currentStage == VUPipelineStage::D &&
        currentStageIndex == executionStageCount;
    case VPU_PIPELINE_TYPE_EFU:
      return
        currentStage == VUPipelineStage::N &&
        currentStageIndex == executionStageCount;
    default:
      return false;
  }
}

bool Pipeline::destinationAvailableForNextTStage() const
{
  return
    (type == VPU_PIPELINE_TYPE_FMAC ||
     type == VPU_PIPELINE_TYPE_LSU ||
     type == VPU_PIPELINE_TYPE_RANDOM) &&
    currentStage == VUPipelineStage::Z;
}

bool Pipeline::blocksStructuralHazardFor(uint8_t requestedType) const
{
  if (requestedType == VPU_PIPELINE_TYPE_FDIV ||
      requestedType == VPU_PIPELINE_TYPE_WAITQ)
  {
    return type == VPU_PIPELINE_TYPE_FDIV;
  }

  if (requestedType == VPU_PIPELINE_TYPE_EFU ||
      requestedType == VPU_PIPELINE_TYPE_WAITP)
  {
    // EFU throughput releases the shared unit on Nn, one stage before P.
    return
      type == VPU_PIPELINE_TYPE_EFU &&
      !(currentStage == VUPipelineStage::N &&
        currentStageIndex == executionStageCount);
  }

  return false;
}

VUPipelineStage Pipeline::stage() const
{
  return currentStage;
}

uint8_t Pipeline::stageIndex() const
{
  return currentStageIndex;
}
