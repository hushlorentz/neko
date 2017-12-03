#ifndef PIPELINE_ORCHESTRATOR_H
#define PIPELINE_ORCHESTRATOR_H

#include <list>

#include "vpu_completed_pipeline_handler.hpp"
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
    void initPipeline(uint8_t pipelineType, int i, float x, float y, float z, float w, uint8_t s1, uint8_t s2, uint8_t d, uint8_t bc, uint8_t fieldMask);
    void setPipelineHandler(CompletedPipelineHandler * handler);
  private:
    list<Pipeline *> executing;
    list<Pipeline *> waiting;
    list<Pipeline *> pool;
    CompletedPipelineHandler * pipelineHandler;
    void updateExecutingPipelines();
    void updateWaitingPipelines();
    bool stallDetected(Pipeline * pipeline);
};

#endif
