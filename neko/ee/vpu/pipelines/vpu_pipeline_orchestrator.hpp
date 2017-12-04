#ifndef PIPELINE_ORCHESTRATOR_H
#define PIPELINE_ORCHESTRATOR_H

#include <list>

#include "vpu_pipeline_handler.hpp"
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
    void initPipeline(uint8_t pipelineType, int i, float x, float y, float z, float w, uint8_t s1, uint8_t s2, uint8_t d, uint8_t fieldMask, uint8_t s2FieldMask);
    void setPipelineHandler(PipelineHandler * handler);
  private:
    list<Pipeline *> executing;
    list<Pipeline *> waiting;
    list<Pipeline *> pool;
    PipelineHandler * pipelineHandler;
    void updateExecutingPipelines();
    void updateWaitingPipelines();
    bool stallDetected(Pipeline * pipeline);
};

#endif
