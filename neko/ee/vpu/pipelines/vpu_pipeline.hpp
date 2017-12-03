#ifndef PIPELINE_H
#define PIPELINE_H

#include <cstdint>

#define VPU_PIPELINE_TYPE_NONE 0
#define VPU_PIPELINE_TYPE_FMAC 1
#define VPU_PIPELINE_FMAC_STALL 3
#define VPU_PIPELINE_FDIV 2
#define VPU_PIPELINE_EFU 3
#define VPU_PIPELINE_IALU 4
#define VPU_PIPELINE_XGKICK 5

class Pipeline
{
  public: 
    int intResult;
    float xResult;
    float yResult;
    float zResult;
    float wResult;
    uint8_t sourceReg1;
    uint8_t sourceReg2;
    uint8_t destReg;
    uint8_t bcField;
    uint8_t type;
    uint8_t destFieldMask;

    Pipeline();
    void configure(uint8_t pipelineType, int i, float x, float y, float z, float w, uint8_t s1, uint8_t s2, uint8_t d, uint8_t bc, uint8_t fieldMask);
    void execute();
    bool isComplete();
  private:
    uint8_t currentStage;
    uint8_t endStage;
};

#endif
