#include "vpu_pipeline.hpp"

Pipeline::Pipeline() : currentStage(0), endStage(0)
{
}

void Pipeline::setAttributes(uint8_t updatedEndStage)
{
  endStage = updatedEndStage;
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
