#include <iostream>

#include "fp_register.hpp"
#include "floating_point_ops.hpp"
#include "stop_watch.hpp"
#include "vpu.hpp"

using namespace std;

void runFPPerf();
void runFPAddPerf();
void runFPSubPerf();
void runFPMulPerf();
void runFPDivPerf();
void runVPUPerf();

int main(int argc, const char * argv[])
{
  runFPPerf();
  runVPUPerf();
}

void runFPPerf()
{
  runFPAddPerf();
  runFPSubPerf();
  runFPMulPerf();
  runFPDivPerf();
}

void runFPAddPerf()
{
  FPRegister reg1(1.0f, 2.0f, 3.0f, 4.0f);
  FPRegister reg2(2.0f, 3.0f, 4.0f, 5.0f);
  FPRegister reg3;

  StopWatch watch;
  watch.start();

  for (int i = 0; i < 10000; i++)
  {
    addFPRegisters(&reg1, &reg2, &reg3);
  }

  printf("It took %f cycles to add two FP Registers\n", watch.elapsedCycles() / 10000);
}

void runFPSubPerf()
{
  FPRegister reg1(1.0f, 2.0f, 3.0f, 4.0f);
  FPRegister reg2(2.0f, 3.0f, 4.0f, 5.0f);
  FPRegister reg3;

  StopWatch watch;
  watch.start();

  for (int i = 0; i < 10000; i++)
  {
    subFPRegisters(&reg1, &reg2, &reg3);
  }

  printf("It took %f cycles to subtract two FP Registers\n", watch.elapsedCycles() / 10000);
}

void runFPMulPerf()
{
  FPRegister reg1(1.0f, 2.0f, 3.0f, 4.0f);
  FPRegister reg2(2.0f, 3.0f, 4.0f, 5.0f);
  FPRegister reg3;

  StopWatch watch;
  watch.start();

  for (int i = 0; i < 10000; i++)
  {
    addFPRegisters(&reg1, &reg2, &reg3);
  }

  printf("It took %f cycles to multiply two FP Registers\n", watch.elapsedCycles() / 10000);
}

void runFPDivPerf()
{
  FPRegister reg1(1.0f, 2.0f, 3.0f, 4.0f);
  FPRegister reg2(2.0f, 3.0f, 4.0f, 5.0f);
  FPRegister reg3;

  StopWatch watch;
  watch.start();

  for (int i = 0; i < 10000; i++)
  {
    addFPRegisters(&reg1, &reg2, &reg3);
  }

  printf("It took %f cycles to divide two FP Registers\n", watch.elapsedCycles() / 10000);
}

void runVPUPerf()
{
  VPU vpu;
}
