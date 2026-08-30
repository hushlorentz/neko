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
#define VPU_PIPELINE_TYPE_BRANCH 7
#define VPU_PIPELINE_TYPE_I_REGISTER 8
#define VPU_PIPELINE_TYPE_WAITQ 9
#define VPU_PIPELINE_TYPE_WAITP 10
#define VPU_PIPELINE_TYPE_FLAG 11
#define VPU_PIPELINE_TYPE_RANDOM 12
#define VPU_PIPELINE_TYPE_VIF_CONTROL 13

enum class VUPipelineStage : uint8_t
{
  M,
  T,
  X,
  Y,
  Z,
  S,
  IY,
  IZ,
  D,
  F,
  N,
  P
};

class Pipeline
{
  public: 
    uint8_t type;
    uint16_t opCode;
    int intResult;
    FPRegister fpResult;
    FPRegister flagResult;
    FPRegister operationResult;
    FPRegister accumulatorValue;
    uint8_t ignoredResultFields;
    uint8_t srcReg1;
    uint8_t srcReg2;
    uint8_t destReg;
    uint8_t integerDestReg;
    uint8_t destFieldMask;
    uint8_t srcReg1FieldMask;
    uint8_t srcReg2FieldMask;
    uint16_t instructionAddress;
    uint16_t memoryAddress;
    int16_t immediate;
    uint32_t immediateBits;
    uint32_t scalarResultBits;
    uint8_t scalarResultFlags;
    uint16_t intSourceValue1;
    uint16_t intSourceValue2;
    bool intSource1Sampled;
    bool intSource2Sampled;
    bool xgkickStarted;
    bool discardWriteback;

    Pipeline();
    void configure(uint8_t pipelineType, uint16_t oc, uint8_t s1, uint8_t s2, uint8_t d, uint8_t destMask, uint8_t s1Mask, uint8_t s2Mask, uint16_t address, bool discard = false, int16_t immediateValue = 0);
    void setFPRegisterResult(FPRegister * reg);
    void setIntResult(int i);
    void advanceStage();
    bool isComplete() const;
    bool destinationAvailableForNextTStage() const;
    bool blocksStructuralHazardFor(uint8_t requestedType) const;
    VUPipelineStage stage() const;
    uint8_t stageIndex() const;
  private:
    VUPipelineStage currentStage;
    uint8_t currentStageIndex;
    uint8_t executionStageCount;
    bool complete;
    void configureTiming();
    void advanceSixStagePipeline();
    void advanceTwoStagePipeline();
    void advanceIALUPipeline();
    void advanceFDIVPipeline();
    void advanceEFUPipeline();
};

#endif
