#include "vpu_pipeline.hpp"

#define FMAC_LATENCY 4

Pipeline::Pipeline() : type(0), intResult(0), xResult(0), yResult(0), zResult(0), wResult(0), sourceReg1(0), sourceReg2(0), destReg(0), destFieldMask(0), source2FieldMask(0), currentStage(0), endStage(0)
{
}

void Pipeline::configure(uint8_t pipelineType, int i, float x, float y, float z, float w, uint8_t s1, uint8_t s2, uint8_t d, uint8_t fieldMask, uint8_t s2FieldMask)
{
  type = pipelineType;
  intResult = i;
  xResult = x;
  yResult = y;
  zResult = z;
  wResult = w;
  sourceReg1 = s1;
  sourceReg2 = s2;
  destReg = d;
  destFieldMask = fieldMask;
  source2FieldMask = s2FieldMask;

  switch (type)
  {
    case VPU_PIPELINE_TYPE_FMAC:
      endStage = FMAC_LATENCY;
      break;
  }
}

void Pipeline::execute()
{
  if (currentStage < (endStage - 1))
  {
    currentStage++;
  }
}

bool Pipeline::isComplete()
{
  return currentStage == (endStage - 1);
}
