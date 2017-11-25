#ifndef vpu_hpp
#define vpu_hpp

#include <vector>
#include "fp_register.hpp"

#define VPU_STATE_READY 1

using namespace std;

class VPU
{
  public:
    VPU();
    vector<uint8_t> microMem;
    vector<uint8_t> vuMem;
    uint8_t getState();
    FPRegister *fpRegisterValue(int registerID);
    uint16_t intRegisterValue(int registerID);
  private:
    uint8_t state;
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
};

#endif
