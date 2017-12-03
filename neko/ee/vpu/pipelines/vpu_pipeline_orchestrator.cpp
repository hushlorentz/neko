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
  list<Pipeline *>::iterator iter = waiting.begin();

  while (iter != waiting.end())
  {
    Pipeline * p = (Pipeline *)*iter;

    if (stallDetected(p))
    {
      break;
    }

    iter = waiting.erase(iter);
    executing.push_back(p);
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
    if (pipeline->sourceReg2 == checkPipeline->destReg && (pipeline->destFieldMask & checkPipeline->destFieldMask))
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
        pipelineHandler->pipelineComplete(p);
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

void PipelineOrchestrator::initPipeline(uint8_t pipelineType, int i, float x, float y, float z, float w, uint8_t s1, uint8_t s2, uint8_t d, uint8_t bc, uint8_t fieldMask)
{
  if (pool.size() == 0)
  {
    throw std::runtime_error("Trying to add a pipeline to the PipelineOrchestrator when the max number of pipelines is already in use!");
  }

  Pipeline * pipeline = pool.front();
  pool.pop_front();

  pipeline->configure(pipelineType, i, x, y, z, w, s1, s2, d, bc, fieldMask);

  waiting.push_back(pipeline);
}

void PipelineOrchestrator::setPipelineHandler(CompletedPipelineHandler * handler)
{
  pipelineHandler = handler;
}
