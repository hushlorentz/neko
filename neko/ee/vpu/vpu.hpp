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

using namespace std;

class VPU : public PipelineHandler
{
  public:
    VPU();
    vector<uint8_t> microMem;
    vector<uint8_t> vuMem;
    bool useThreads;
    FPRegister accumulator;
    uint64_t clippingFlags;

    uint8_t getState();
    FPRegister *fpRegisterValue(int registerID);
    uint16_t intRegisterValue(int registerID);
    void loadFPRegister(int registerID, double x, double y, double z, double w);
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
    void loadIRegister(double value);
    void loadQRegister(double value);
    void loadAccumulator(double x, double y, double z, double w);
  private:
    uint8_t state;
    uint32_t cycles;
    uint8_t mode;
    bool stepThrough;
    uint16_t microMemPC;
    vector<FPRegister> fpRegisters;
    vector<uint16_t> intRegisters;
    double iRegister;
    double qRegister;
    double pRegister;
    uint32_t rRegister;
    uint16_t MACFlags;
    uint16_t statusFlags;
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
    uint16_t opCodeFromInstruction(uint32_t instruction);
    uint8_t regFromInstruction(uint32_t instruction, uint8_t shift);
    uint8_t src1RegFromOpCodeAndInstruction(uint16_t opCode, uint32_t instruction);
    uint8_t destRegFromOpCodeAndInstruction(uint16_t opCode, uint32_t instruction);
    uint8_t src2MaskFromOpCodeAndInstruction(uint16_t opCode, uint32_t upperInstruction);
    uint16_t processLowerInstruction(uint32_t lowerInstruction);
    void setFlags(FPRegister * reg);
    void setMACFlagsFromRegister(FPRegister * reg);
    void setStatusFlagsFromMACFlags();
    void setStickyFlagsFromStatusFlags();
    void updateDestinationRegisterWithPipelineResult(FPRegister * destReg, Pipeline * p);
    void updateClippingFlags(uint32_t clip);
    int calculateNewClippingFlags(FPRegister * fsReg, FPRegister * ftReg);
    FPRegister * destinationRegisterFromPipeline(Pipeline * p);
    void handleMADDInstruction(Pipeline * p);
};

#endif
