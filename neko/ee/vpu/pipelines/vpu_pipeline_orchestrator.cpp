#include <exception>

#include "vpu_pipeline.hpp"
#include "vpu_pipeline_orchestrator.hpp"

PipelineOrchestrator::PipelineOrchestrator() : pipelineHandler(NULL)
{
  for (int i = 0; i < MAX_PIPELINES; i++)
  {
    Pipeline *pipeline = new Pipeline();
    pool.push_back(pipeline);
  }
}

PipelineOrchestrator::~PipelineOrchestrator()
{
  while (!pool.empty())
  {
    Pipeline *pipeline = pool.front();
    delete pipeline;

    pool.pop_front();
  }
}

void PipelineOrchestrator::update()
{
  updateWaitingPipelines();
  updateExecutingPipelines();
}

void PipelineOrchestrator::updateWaitingPipelines()
{
  if (waiting.size() == 0)
  {
    return;
  }

  Pipeline * p = waiting.front();
  if (stallDetected(p))
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

bool PipelineOrchestrator::stallDetected(Pipeline * pipeline)
{
  for (list<Pipeline *>::iterator iter = executing.begin(); iter != executing.end(); ++iter)
  {
    Pipeline * checkPipeline = *iter;

    if (pipeline->sourceReg1 == checkPipeline->destReg && (pipeline->destFieldMask & checkPipeline->destFieldMask))
    {
      return true;
    }
    if (pipeline->sourceReg2 == checkPipeline->destReg && (pipeline->source2FieldMask & checkPipeline->destFieldMask))
    {
      return true;
    }
  }

  return false;
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

void PipelineOrchestrator::initPipeline(uint8_t pipelineType, int i, float x, float y, float z, float w, uint8_t s1, uint8_t s2, uint8_t d, uint8_t fieldMask, uint8_t s2FieldMask)
{
  if (pool.size() == 0)
  {
    throw std::runtime_error("Trying to add a pipeline to the PipelineOrchestrator when the max number of pipelines is already in use!");
  }

  Pipeline * pipeline = pool.front();
  pool.pop_front();

  pipeline->configure(pipelineType, i, x, y, z, w, s1, s2, d, fieldMask, s2FieldMask);

  waiting.push_back(pipeline);
}

void PipelineOrchestrator::setPipelineHandler(PipelineHandler * handler)
{
  pipelineHandler = handler;
}
