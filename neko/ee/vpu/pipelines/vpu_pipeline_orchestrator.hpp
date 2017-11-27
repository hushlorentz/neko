#ifndef PIPELINE_ORCHESTRATOR_H
#define PIPELINE_ORCHESTRATOR_H

#include <vector>
#include "vpu_pipeline.hpp"

using namespace std;

class PipelineOrchestrator
{
  public:
    void update();
    bool hasNext();
    void addPipeline(Pipeline * pipeline);
  private:
    vector<Pipeline *> pipelines;
};

#endif
