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
  for (vector<Pipeline *>::iterator it = pipelines.begin(); it < pipelines.end(); ++it)
  {
    Pipeline *p = *it;

    if (p->isExecuting)
    {
      return true;
    }
  }

  return false;
}

void PipelineOrchestrator::addPipeline(Pipeline * pipeline)
{
  pipelines.push_back(pipeline);
  pipeline->isExecuting = true;
}
