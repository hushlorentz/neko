#ifndef vpu_hpp
#define vpu_hpp

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
    void initMemory();
    void initFPRegisters();
    void initIntRegisters();
    void executeMicroInstructions();
    void updateMicroInstructions();
};

#endif
