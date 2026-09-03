#ifndef PIPELINE_ORCHESTRATOR_H
#define PIPELINE_ORCHESTRATOR_H

#include <array>
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
    PipelineOrchestrator(const PipelineOrchestrator &) = delete;
    PipelineOrchestrator &operator=(const PipelineOrchestrator &) = delete;
    PipelineOrchestrator(PipelineOrchestrator &&) = delete;
    PipelineOrchestrator &operator=(PipelineOrchestrator &&) = delete;
    void reset();
    void update();
    bool hasNext();
    bool hasPipelineType(uint8_t pipelineType) const;
    bool hasRegisterHazard(uint8_t srcReg1, uint8_t srcReg1FieldMask, uint8_t srcReg2, uint8_t srcReg2FieldMask) const;
    void initPipeline(uint8_t pipelineType, uint16_t opCode, uint8_t srcReg1, uint8_t srcReg2, uint8_t destReg, uint8_t destFieldMask, uint8_t srcReg1FieldMask, uint8_t srcReg2FieldMask, uint16_t instructionAddress = 0, int16_t immediate = 0);
    Pipeline *startPipeline(uint8_t pipelineType, uint16_t opCode, uint8_t srcReg1, uint8_t srcReg2, uint8_t destReg, uint8_t destFieldMask, uint8_t srcReg1FieldMask, uint8_t srcReg2FieldMask, uint16_t instructionAddress = 0, bool discardWriteback = false, int16_t immediate = 0);
    void setPipelineHandler(PipelineHandler * handler);
  private:
    friend class NekoSaveStateCodec;

    std::array<Pipeline, MAX_PIPELINES> pipelines;
    list<Pipeline *> executing;
    list<Pipeline *> waiting;
    list<Pipeline *> pool;
    PipelineHandler * pipelineHandler;
    void updateExecutingPipelines();
    void updateWaitingPipelines();
    void detectStalls(Pipeline * pipeline);
    bool hasStructuralHazard(const Pipeline *pipeline) const;
    Pipeline *configurePipeline(uint8_t pipelineType, uint16_t opCode, uint8_t srcReg1, uint8_t srcReg2, uint8_t destReg, uint8_t destFieldMask, uint8_t srcReg1FieldMask, uint8_t srcReg2FieldMask, uint16_t instructionAddress, bool discardWriteback, int16_t immediate);
};

#endif
