#include "vpu_pipeline.hpp"

#define FMAC_STAGES 6

Pipeline::Pipeline() : type(0), opCode(0), intResult(0), xResult(0), yResult(0), zResult(0), wResult(0), ftReg(0), fsReg(0), fdReg(0), destFieldMask(0), source2FieldMask(0), currentStage(1), endStage(0)
{
}

void Pipeline::configure(uint8_t pipelineType, uint16_t oc, uint8_t ft, uint8_t fs, uint8_t fd, uint8_t fieldMask, uint8_t s2FieldMask)
{
  type = pipelineType;
  opCode = oc;
  ftReg = ft;
  fsReg = fs;
  fdReg = fd;
  destFieldMask = fieldMask;
  source2FieldMask = s2FieldMask;
  currentStage = 1;

  switch (type)
  {
    case VPU_PIPELINE_TYPE_FMAC:
      endStage = FMAC_STAGES;
      break;
  }
}

void Pipeline::setFloatResult(float x, float y, float z, float w)
{
  xResult = x;
  yResult = y;
  zResult = z;
  wResult = w;
}

void Pipeline::setIntResult(int i)
{
  intResult = i;
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
