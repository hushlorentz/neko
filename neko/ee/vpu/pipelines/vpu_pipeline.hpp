#ifndef PIPELINE_H
#define PIPELINE_H

#include <cstdint>

#include "fp_register.hpp"

#define VPU_PIPELINE_TYPE_NONE 0
#define VPU_PIPELINE_TYPE_FMAC 1
#define VPU_PIPELINE_TYPE_FDIV 2
#define VPU_PIPELINE_TYPE_EFU 3
#define VPU_PIPELINE_TYPE_IALU 4
#define VPU_PIPELINE_TYPE_XGKICK 5
#define VPU_PIPELINE_TYPE_LSU 6

class Pipeline
{
  public: 
    uint8_t type;
    uint16_t opCode;
    int intResult;
    FPRegister fpResult;
    uint8_t srcReg1;
    uint8_t srcReg2;
    uint8_t destReg;
    uint8_t destFieldMask;
    uint8_t srcReg1FieldMask;
    uint8_t srcReg2FieldMask;
    uint16_t instructionAddress;
    uint16_t memoryAddress;
    bool discardWriteback;

    Pipeline();
    void configure(uint8_t pipelineType, uint16_t oc, uint8_t s1, uint8_t s2, uint8_t d, uint8_t destMask, uint8_t s1Mask, uint8_t s2Mask, uint16_t address, bool discard = false);
    void setFPRegisterResult(FPRegister * reg);
    void setIntResult(int i);
    void execute();
    bool isComplete();
  private:
    uint8_t currentStage;
    uint8_t endStage;
};

#endif
