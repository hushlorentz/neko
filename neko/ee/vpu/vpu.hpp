#ifndef VPU_HPP
#define VPU_HPP

#include <cstddef>
#include <cstdint>
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

enum class VPUType : uint8_t
{
  VU0,
  VU1
};

class VPU : public PipelineHandler
{
  public:
    explicit VPU(VPUType type = VPUType::VU0);
    FPRegister accumulator;
    uint64_t clippingFlags;

    VPUType unitType() const;
    size_t microMemorySize() const;
    size_t dataMemorySize() const;
    uint8_t getState();
    const FPRegister *fpRegisterValue(int registerID) const;
    uint16_t intRegisterValue(int registerID);
    void loadFPRegister(int registerID, double x, double y, double z, double w);
    void loadIntFPRegister(int registerID, int32_t x, int32_t y, int32_t z, int32_t w);
    void loadIntRegister(int registerID, int value);
    uint32_t elapsedCycles();
    void resetCycles();
    void setMode(uint8_t newMode);
    void initMicroMode();
    void uploadMicroInstructions(const vector<uint8_t> &instructions);
    void writeDataMemory(size_t address, const vector<uint8_t> &data);
    vector<uint8_t> readDataMemory(size_t address, size_t byteCount) const;
    virtual void pipelineStarted(Pipeline * p);
    virtual void pipelineFinished(Pipeline * p);
    bool hasMACFlag(uint16_t flag);
    bool hasStatusFlag(uint16_t flag);
    void loadIRegister(double value);
    void loadQRegister(double value);
    void loadAccumulator(double x, double y, double z, double w);
  private:
    VPUType type;
    vector<uint8_t> microMem;
    vector<uint8_t> vuMem;
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
    bool endBitSet(uint32_t instruction);
    bool haltBitSet(uint32_t instruction);
    uint32_t nextUpperInstruction();
    uint32_t nextLowerInstruction();
    void processUpperInstruction(uint32_t upperInstruction);
    uint16_t opCodeFromInstruction(uint32_t instruction);
    uint8_t regFromInstruction(uint32_t instruction, uint8_t shift);
    uint8_t src1RegFromOpCodeAndInstruction(uint16_t opCode, uint32_t instruction);
    uint8_t destRegFromOpCodeAndInstruction(uint16_t opCode, uint32_t instruction);
    uint8_t destinationMaskFromOpCode(uint16_t opCode, uint8_t encodedMask);
    uint8_t srcReg1MaskFromOpCode(uint16_t opCode, uint8_t destinationMask);
    uint8_t srcReg2MaskFromOpCode(uint16_t opCode, uint8_t destinationMask);
    uint16_t processLowerInstruction(uint32_t lowerInstruction);
    void setFlags(FPRegister * reg, uint8_t ignoredFields);
    void setMACFlagsFromRegister(FPRegister * reg, uint8_t ignoredFields);
    void setStatusFlagsFromMACFlags();
    void setStickyFlagsFromStatusFlags();
    void updateDestinationRegisterWithPipelineResult(FPRegister * destReg, Pipeline * p);
    void updateClippingFlags(uint32_t clip);
    int calculateNewClippingFlags(FPRegister * fsReg, FPRegister * ftReg);
    FPRegister * destinationRegisterFromPipeline(Pipeline * p);
    void handleMADDInstruction(Pipeline * p);
    void handleMSUBInstruction(Pipeline * p);
    void handleOPMSUBInstruction(Pipeline * p);
};

#endif
