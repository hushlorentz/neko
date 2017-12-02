#ifndef VPU_HPP
#define VPU_HPP

#include <set>
#include <vector>
#include "fp_register.hpp"

#define VPU_STATE_READY 1
#define VPU_STATE_RUN 2
#define VPU_STATE_STOP 3
#define VPU_MODE_MICRO 1
#define VPU_MODE_MACRO 2

using namespace std;

class VPU
{
  public:
    VPU();
    vector<uint8_t> microMem;
    vector<uint8_t> vuMem;
    bool useThreads;

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
  private:
    uint8_t state;
    uint32_t cycles;
    uint8_t mode;
    bool stepThrough;
    uint16_t microMemPC;
    vector<FPRegister> fpRegisters;
    vector<uint16_t> intRegisters;
    FPRegister accRegisters;
    float iRegister;
    float qRegister;
    float pRegister;
    uint32_t rRegister;
    uint16_t macFlags;
    uint16_t statusFlags;
    uint64_t clippingFlags;
    set<uint16_t> type0OpCodes;
    set<uint16_t> type1OpCodes;
    set<uint16_t> type2OpCodes;
    set<uint16_t> type3OpCodes;
    uint8_t previousUpperInstructionType;
    uint8_t previousLowerInstructionType;

    void initMemory();
    void initFPRegisters();
    void initIntRegisters();
    void initOpCodeSets();
    void executeMicroInstructions();
    void updateMicroInstructions();
    bool stopBitSet(uint32_t instruction);
    uint32_t nextUpperInstruction();
    uint32_t nextLowerInstruction();
    void processUpperInstruction(uint32_t upperInstruction);
    void processUpperType3Instruction(uint32_t upperInstruction);
    uint8_t regFromInstruction(uint32_t instruction, uint8_t shift);
    uint8_t destBitsFromInstruction(uint32_t instruction);
    uint16_t processLowerInstruction(uint32_t lowerInstruction);
    void incrementCyclesCount(uint8_t upperInstruction, uint8_t lowerInstruction);
};

#endif
