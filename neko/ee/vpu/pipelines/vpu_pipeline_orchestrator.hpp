#ifndef PIPELINE_ORCHESTRATOR_H
#define PIPELINE_ORCHESTRATOR_H

#include <list>

#include "vpu_pipeline.hpp"

#define MAX_PIPELINES 10

using namespace std;

class PipelineOrchestrator
{
  public:
    PipelineOrchestrator();
    ~PipelineOrchestrator();
    void update();
    bool hasNext();
    void addPipeline(uint8_t pipelineEndStage);
  private:
    list<Pipeline *> executing;
    list<Pipeline *> waiting;
    list<Pipeline *> pool;
    void updateExecutingPipelines();
    void updateWaitingPipelines();
};

#endif
