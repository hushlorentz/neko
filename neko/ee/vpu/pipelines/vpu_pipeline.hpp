#ifndef PIPELINE_H
#define PIPELINE_H

#include <cstdint>

#define VPU_PIPELINE_TYPE_NONE 0
#define VPU_PIPELINE_TYPE_FMAC 1
#define VPU_PIPELINE_FDIV 2
#define VPU_PIPELINE_EFU 3
#define VPU_PIPELINE_IALU 4
#define VPU_PIPELINE_XGKICK 5

class Pipeline
{
  public: 
    uint8_t type;
    uint16_t opCode;
    int intResult;
    float xResult;
    float yResult;
    float zResult;
    float wResult;
    uint8_t sourceReg1;
    uint8_t sourceReg2;
    uint8_t destReg;
    uint8_t destFieldMask;
    uint8_t source2FieldMask;

    Pipeline();
    void configure(uint8_t pipelineType, uint16_t oc, uint8_t s1, uint8_t s2, uint8_t d, uint8_t fieldMask, uint8_t s2FieldMask);
    void setFloatResult(float x, float y, float z, float w);
    void setIntResult(int i);
    void execute();
    bool isComplete();
  private:
    uint8_t currentStage;
    uint8_t endStage;
};

#endif
