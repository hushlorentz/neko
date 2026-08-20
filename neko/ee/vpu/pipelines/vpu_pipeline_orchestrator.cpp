#include <stdexcept>

#include "vpu_pipeline.hpp"
#include "vpu_pipeline_orchestrator.hpp"
#include "vpu_register_ids.hpp"

PipelineOrchestrator::PipelineOrchestrator() : pipelineHandler(NULL), stalling(false)
{
  for (int i = 0; i < MAX_PIPELINES; i++)
  {
    Pipeline *pipeline = new Pipeline();
    pool.push_back(pipeline);
  }
}

PipelineOrchestrator::~PipelineOrchestrator()
{
  reset();

  while (!pool.empty())
  {
    Pipeline *pipeline = pool.front();
    delete pipeline;

    pool.pop_front();
  }
}

void PipelineOrchestrator::reset()
{
  pool.splice(pool.end(), executing);
  pool.splice(pool.end(), waiting);
  stalling = false;
}

void PipelineOrchestrator::update()
{
  updateExecutingPipelines();
  updateWaitingPipelines();
}

void PipelineOrchestrator::updateWaitingPipelines()
{
  if (waiting.size() == 0)
  {
    return;
  }

  Pipeline * p = waiting.front();
  detectStalls(p);

  if (stalling)
  {
    return;
  }

  waiting.erase(waiting.begin());
  executing.push_back(p);

  if (pipelineHandler)
  {
    pipelineHandler->pipelineStarted(p);
  }
}

void PipelineOrchestrator::detectStalls(Pipeline * pipeline)
{
  stalling = false; 

  for (list<Pipeline *>::iterator iter = executing.begin(); iter != executing.end(); ++iter)
  {
    Pipeline * checkPipeline = *iter;

    if (checkPipeline->discardWriteback ||
        checkPipeline->destReg == VPU_REGISTER_VF00)
    {
      continue;
    }

    const bool srcReg1Hazard =
      pipeline->srcReg1FieldMask != 0 &&
      pipeline->srcReg1 != VPU_REGISTER_VF00 &&
      pipeline->srcReg1 == checkPipeline->destReg &&
      (pipeline->srcReg1FieldMask & checkPipeline->destFieldMask) != 0;
    const bool srcReg2Hazard =
      pipeline->srcReg2FieldMask != 0 &&
      pipeline->srcReg2 != VPU_REGISTER_VF00 &&
      pipeline->srcReg2 == checkPipeline->destReg &&
      (pipeline->srcReg2FieldMask & checkPipeline->destFieldMask) != 0;

    if (srcReg1Hazard || srcReg2Hazard)
    {
      stalling = true;
      return;
    }
  }
}

void PipelineOrchestrator::updateExecutingPipelines()
{
  list<Pipeline *>::iterator iter = executing.begin();

  while (iter != executing.end())
  {
    Pipeline * p = (Pipeline *)*iter;

    if (p->isComplete())
    {
      if (pipelineHandler)
      {
        pipelineHandler->pipelineFinished(p);
      }

      iter = executing.erase(iter);
      pool.push_back(p);
    }
    else
    {
      p->execute();
      ++iter;
    }
  }
}

bool PipelineOrchestrator::hasNext()
{
  return executing.size() > 0 || waiting.size() > 0;
}

void PipelineOrchestrator::initPipeline(uint8_t pipelineType, uint16_t opCode, uint8_t srcReg1, uint8_t srcReg2, uint8_t destReg, uint8_t destFieldMask, uint8_t srcReg1FieldMask, uint8_t srcReg2FieldMask, uint16_t instructionAddress)
{
  waiting.push_back(configurePipeline(
    pipelineType,
    opCode,
    srcReg1,
    srcReg2,
    destReg,
    destFieldMask,
    srcReg1FieldMask,
    srcReg2FieldMask,
    instructionAddress,
    false));
}

void PipelineOrchestrator::startPipeline(uint8_t pipelineType, uint16_t opCode, uint8_t srcReg1, uint8_t srcReg2, uint8_t destReg, uint8_t destFieldMask, uint8_t srcReg1FieldMask, uint8_t srcReg2FieldMask, uint16_t instructionAddress, bool discardWriteback)
{
  Pipeline *pipeline = configurePipeline(
    pipelineType,
    opCode,
    srcReg1,
    srcReg2,
    destReg,
    destFieldMask,
    srcReg1FieldMask,
    srcReg2FieldMask,
    instructionAddress,
    discardWriteback);
  executing.push_back(pipeline);

  if (pipelineHandler)
  {
    pipelineHandler->pipelineStarted(pipeline);
  }
}

Pipeline *PipelineOrchestrator::configurePipeline(uint8_t pipelineType, uint16_t opCode, uint8_t srcReg1, uint8_t srcReg2, uint8_t destReg, uint8_t destFieldMask, uint8_t srcReg1FieldMask, uint8_t srcReg2FieldMask, uint16_t instructionAddress, bool discardWriteback)
{
  if (pool.size() == 0)
  {
    throw std::runtime_error("Trying to add a pipeline to the PipelineOrchestrator when the max number of pipelines is already in use!");
  }

  Pipeline * pipeline = pool.front();
  pool.pop_front();

  pipeline->configure(pipelineType, opCode, srcReg1, srcReg2, destReg, destFieldMask, srcReg1FieldMask, srcReg2FieldMask, instructionAddress, discardWriteback);
  return pipeline;
}

void PipelineOrchestrator::setPipelineHandler(PipelineHandler * handler)
{
  pipelineHandler = handler;
}
