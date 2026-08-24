#include "vpu_pipeline.hpp"

Pipeline::Pipeline() : type(0), opCode(0), intResult(0), srcReg1(0), srcReg2(0), destReg(0), destFieldMask(0), srcReg1FieldMask(0), srcReg2FieldMask(0), instructionAddress(0), memoryAddress(0), discardWriteback(false), currentStage(VUPipelineStage::M)
{
}

void Pipeline::configure(uint8_t pipelineType, uint16_t oc, uint8_t s1, uint8_t s2, uint8_t d, uint8_t destMask, uint8_t s1Mask, uint8_t s2Mask, uint16_t address, bool discard)
{
  type = pipelineType;
  opCode = oc;
  srcReg1 = s1;
  srcReg2 = s2;
  destReg = d;
  destFieldMask = destMask;
  srcReg1FieldMask = s1Mask;
  srcReg2FieldMask = s2Mask;
  instructionAddress = address;
  discardWriteback = discard;
  currentStage = VUPipelineStage::M;
}

void Pipeline::setFPRegisterResult(FPRegister * reg)
{
  fpResult.copyFrom(reg);
}

void Pipeline::setIntResult(int i)
{
  intResult = i;
}

void Pipeline::advanceStage()
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
      break;
    case VUPipelineStage::S:
      break;
  }
}

bool Pipeline::isComplete() const
{
  return currentStage == VUPipelineStage::S;
}

VUPipelineStage Pipeline::stage() const
{
  return currentStage;
}
