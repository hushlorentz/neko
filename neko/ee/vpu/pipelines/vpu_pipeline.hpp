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

enum class VUPipelineType : uint8_t
{
  None = VPU_PIPELINE_TYPE_NONE,
  FMAC = VPU_PIPELINE_TYPE_FMAC,
  FDIV = VPU_PIPELINE_TYPE_FDIV,
  EFU = VPU_PIPELINE_TYPE_EFU,
  IALU = VPU_PIPELINE_TYPE_IALU,
  XGKICK = VPU_PIPELINE_TYPE_XGKICK,
  LSU = VPU_PIPELINE_TYPE_LSU,
  Branch = VPU_PIPELINE_TYPE_BRANCH,
  IRegister = VPU_PIPELINE_TYPE_I_REGISTER,
  WaitQ = VPU_PIPELINE_TYPE_WAITQ,
  WaitP = VPU_PIPELINE_TYPE_WAITP,
  Flag = VPU_PIPELINE_TYPE_FLAG,
  Random = VPU_PIPELINE_TYPE_RANDOM,
  VIFControl = VPU_PIPELINE_TYPE_VIF_CONTROL
};

enum class VUPipelineIssueContext : uint8_t
{
  Micro,
  Macro
};

enum class VUPipelineWritebackDisposition : uint8_t
{
  Commit,
  Discard
};

struct VUPipelineRequest
{
  VUPipelineType type = VUPipelineType::None;
  uint16_t opCode = 0;
  uint8_t sourceRegister1 = 0;
  uint8_t sourceRegister2 = 0;
  uint8_t destinationRegister = 0;
  uint8_t destinationFieldMask = 0;
  uint8_t sourceFieldMask1 = 0;
  uint8_t sourceFieldMask2 = 0;
  uint16_t microInstructionAddress = 0;
  int16_t immediate = 0;
  VUPipelineIssueContext issueContext =
    VUPipelineIssueContext::Micro;
  VUPipelineWritebackDisposition writeback =
    VUPipelineWritebackDisposition::Commit;
};

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
    FPRegister sourceValue1;
    FPRegister sourceValue2;
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
    bool vectorSourcesSampled;
    bool xgkickStarted;
    VUPipelineWritebackDisposition writebackDisposition;

    Pipeline();
    void configure(const VUPipelineRequest &request);
    void setFPRegisterResult(FPRegister *reg);
    void setIntResult(int i);
    void advanceStage();
    bool isComplete() const;
    bool completesOnNextAdvance() const;
    bool destinationAvailableForNextTStage() const;
    bool blocksStructuralHazardFor(uint8_t requestedType) const;
    VUPipelineStage stage() const;
    uint8_t stageIndex() const;
  private:
    friend class NekoSaveStateCodec;

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
