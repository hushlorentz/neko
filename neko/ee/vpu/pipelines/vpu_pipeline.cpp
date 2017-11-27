#include "vpu_pipeline.hpp"

Pipeline::Pipeline(uint8_t maxStage) : isExecuting(false), currentStage(0), maxStage(maxStage)
{
}

void Pipeline::execute()
{
  currentStage++;

  if (currentStage == maxStage)
  {
    isExecuting = false;
  }
}
