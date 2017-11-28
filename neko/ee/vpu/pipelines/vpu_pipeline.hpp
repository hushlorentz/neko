#ifndef PIPELINE_H
#define PIPELINE_H

#include <cstdint>

#define VPU_PIPELINE_FMAC_EXECUTIONS 4
#define VPU_PIPELINE_FDIV_EXECUTIONS 7
#define VPU_PIPELINE_RSQRT_EXECUTIONS 13
#define VPU_PIPELINE_IALU_EXECUTIONS 4

class Pipeline
{
  public: 
    Pipeline(uint8_t maxStage);
    void execute();
    bool isExecuting;
  private:
    uint8_t currentStage;
    uint8_t maxStage;
};

#endif
