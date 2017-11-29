#include <exception>

#include "vpu_pipeline.hpp"
#include "vpu_pipeline_orchestrator.hpp"

PipelineOrchestrator::PipelineOrchestrator()
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

    //if not blocking
    iter = waiting.erase(iter);
    executing.push_back(p);
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

void PipelineOrchestrator::addPipeline(uint8_t pipelineEndStage)
{
  if (pool.size() == 0)
  {
    throw std::runtime_error("Trying to add a pipeline to the PipelineOrchestrator when the max number of pipelines is already in use!");
  }

  Pipeline * pipeline = pool.front();
  pool.pop_front();

  pipeline->setAttributes(pipelineEndStage);

  waiting.push_back(pipeline);
}
