#ifndef VPU_HPP
#define VPU_HPP

#include <set>
#include <vector>

#include "fp_register.hpp"
#include "vpu_pipeline_handler.hpp"
#include "vpu_pipeline_orchestrator.hpp"

#define VPU_STATE_READY 1
#define VPU_STATE_RUN 2
#define VPU_STATE_STOP 3
#define VPU_MODE_MICRO 1
#define VPU_MODE_MACRO 2

#define VPU_FLAG_ZW 0x1
#define VPU_FLAG_ZZ 0x2
#define VPU_FLAG_ZY 0x4
#define VPU_FLAG_ZX 0x8
#define VPU_FLAG_SW 0x10
#define VPU_FLAG_SZ 0x20
#define VPU_FLAG_SY 0x40
#define VPU_FLAG_SX 0x80

#define VPU_FLAG_Z 0x1
#define VPU_FLAG_S 0x2
#define VPU_FLAG_Z_STICKY 0x40
#define VPU_FLAG_S_STICKY 0x80

#define VPU_Z_BITS_MASK 0xf
#define VPU_S_BITS_MASK 0xf0

using namespace std;

class VPU : public PipelineHandler
{
  public:
    VPU();
    vector<uint8_t> microMem;
    vector<uint8_t> vuMem;
    bool useThreads;
    FPRegister accumulator;

    uint8_t getState();
    FPRegister *fpRegisterValue(int registerID);
    uint16_t intRegisterValue(int registerID);
    void loadFPRegister(int registerID, float x, float y, float z, float w);
    void loadIntRegister(int registerID, int value);
    uint32_t elapsedCycles();
    void resetCycles();
    void setMode(uint8_t newMode);
    void initMicroMode();
    void uploadMicroInstructions(vector<uint8_t> * instructions);
    virtual void pipelineStarted(Pipeline * p);
    virtual void pipelineFinished(Pipeline * p);
    bool hasMACFlag(uint16_t flag);
    bool hasStatusFlag(uint16_t flag);
    void loadIRegister(float value);
    void loadQRegister(float value);
  private:
    uint8_t state;
    uint32_t cycles;
    uint8_t mode;
    bool stepThrough;
    uint16_t microMemPC;
    vector<FPRegister> fpRegisters;
    vector<uint16_t> intRegisters;
    float iRegister;
    float qRegister;
    float pRegister;
    uint32_t rRegister;
    uint16_t MACFlags;
    uint16_t statusFlags;
    uint64_t clippingFlags;
    set<uint16_t> type0OpCodes;
    set<uint16_t> type1OpCodes;
    set<uint16_t> type2OpCodes;
    set<uint16_t> type3OpCodes;
    PipelineOrchestrator orchestrator;
    FPRegister virtualDestRegister;

    void initMemory();
    void initFPRegisters();
    void initIntRegisters();
    void initOpCodeSets();
    void initPipelineOrchestrator();
    void executeMicroInstructions();
    void updateMicroInstructions();
    bool stopBitSet(uint32_t instruction);
    uint32_t nextUpperInstruction();
    uint32_t nextLowerInstruction();
    void processUpperInstruction(uint32_t upperInstruction);
    uint8_t regFromInstruction(uint32_t instruction, uint8_t shift);
    uint8_t destBitsFromInstruction(uint32_t instruction);
    uint16_t processLowerInstruction(uint32_t lowerInstruction);
    void setMACFlagsFromRegister(FPRegister * reg);
    void setStatusFlagsFromMACFlags();
    void setStickyFlagsFromStatusFlags();
    void updateDestinationRegisterWithPipelineResult(FPRegister * destReg, Pipeline * p);
};

#endif
