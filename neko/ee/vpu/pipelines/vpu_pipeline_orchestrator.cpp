#include "vpu_pipeline_orchestrator.hpp"

void PipelineOrchestrator::update()
{
  for (vector<Pipeline *>::iterator it = pipelines.begin(); it < pipelines.end(); ++it)
  {
    Pipeline *p = *it;

    if (p->isExecuting)
    {
      p->execute();
    }
  }
}

bool PipelineOrchestrator::hasNext()
{
  bool hasNext = false;
  for (vector<Pipeline *>::iterator it = pipelines.begin(); it < pipelines.end(); ++it)
  {
    Pipeline *p = *it;

    if (p->isExecuting)
    {
      hasNext = true;
    }
  }

  return hasNext;
}

void PipelineOrchestrator::addPipeline(Pipeline * pipeline)
{
  pipelines.push_back(pipeline);
  pipeline->isExecuting = true;
}
