#ifndef PIPELINE_ORCHESTRATOR_H
#define PIPELINE_ORCHESTRATOR_H

#include <list>

#include "vpu_pipeline_handler.hpp"
#include "vpu_pipeline.hpp"

#define MAX_PIPELINES 12

using namespace std;

class PipelineOrchestrator
{
  public:
    bool stalling;

    PipelineOrchestrator();
    ~PipelineOrchestrator();
    void reset();
    void update();
    bool hasNext();
    bool hasRegisterHazard(uint8_t srcReg1, uint8_t srcReg1FieldMask, uint8_t srcReg2, uint8_t srcReg2FieldMask) const;
    void initPipeline(uint8_t pipelineType, uint16_t opCode, uint8_t srcReg1, uint8_t srcReg2, uint8_t destReg, uint8_t destFieldMask, uint8_t srcReg1FieldMask, uint8_t srcReg2FieldMask, uint16_t instructionAddress = 0);
    void startPipeline(uint8_t pipelineType, uint16_t opCode, uint8_t srcReg1, uint8_t srcReg2, uint8_t destReg, uint8_t destFieldMask, uint8_t srcReg1FieldMask, uint8_t srcReg2FieldMask, uint16_t instructionAddress = 0, bool discardWriteback = false);
    void setPipelineHandler(PipelineHandler * handler);
  private:
    list<Pipeline *> executing;
    list<Pipeline *> waiting;
    list<Pipeline *> pool;
    PipelineHandler * pipelineHandler;
    void updateExecutingPipelines();
    void updateWaitingPipelines();
    void detectStalls(Pipeline * pipeline);
    Pipeline *configurePipeline(uint8_t pipelineType, uint16_t opCode, uint8_t srcReg1, uint8_t srcReg2, uint8_t destReg, uint8_t destFieldMask, uint8_t srcReg1FieldMask, uint8_t srcReg2FieldMask, uint16_t instructionAddress, bool discardWriteback);
};

#endif
