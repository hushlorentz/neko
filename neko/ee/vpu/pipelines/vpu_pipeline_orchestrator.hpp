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
    bool stalling;

    PipelineOrchestrator();
    ~PipelineOrchestrator();
    void update();
    bool hasNext();
    void initPipeline(uint8_t pipelineType, uint16_t opCode, uint8_t ft, uint8_t fs, uint8_t fd, uint8_t fieldMask, uint8_t s2FieldMask);
    void setPipelineHandler(PipelineHandler * handler);
  private:
    list<Pipeline *> executing;
    list<Pipeline *> waiting;
    list<Pipeline *> pool;
    PipelineHandler * pipelineHandler;
    void updateExecutingPipelines();
    void updateWaitingPipelines();
    void detectStalls(Pipeline * pipeline);
};

#endif
