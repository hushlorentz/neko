#include "catch.hpp"
#include "vpu.hpp"
#include "vpu_register_ids.hpp"
#include "vpu_opcodes.hpp"

void addInstructionToVector(vector<uint8_t> * instructions, uint32_t bitFlags, uint32_t destFlags, uint8_t ftRegID, uint8_t fsRegID, uint8_t fdRegID, uint16_t opCode, uint8_t bcFlags)
{
  uint32_t instruction = bitFlags;
  instruction |= destFlags;
  instruction |= ftRegID << VPU_FT_REG_SHIFT;
  instruction |= fsRegID  << VPU_FS_REG_SHIFT;
  instruction |= fdRegID << VPU_FD_REG_SHIFT;
  instruction |= opCode;
  instruction |= bcFlags;

  instructions->push_back((instruction >> 24) & 0xff);
  instructions->push_back((instruction >> 16) & 0xff);
  instructions->push_back((instruction >> 8) & 0xff);
  instructions->push_back(instruction & 0xff);
}

void addNOPLowerInstructionToVector(vector<uint8_t> * instructions)
{
  uint32_t instruction = 0;
  instructions->push_back(instruction);
}

void addTerminationInstructionToVector(vector<uint8_t> * instructions)
{
  uint32_t instruction = VPU_E_BIT_MASK;
  instructions->push_back(instruction);

  addNOPLowerInstructionToVector(instructions);
}

TEST_CASE("VPU Microinstruction Operation Tests")
{
    VPU vpu;
    vpu.useThreads = false;
    vpu.setMode(VPU_MODE_MICRO);
    vector<uint8_t> instructions;
    
    SECTION("ABS stores the absolute value of src vector in dest vector")
    {
      vpu.loadFPRegister(VPU_REGISTER_VF03, -5.0f, -2.4f, -1.0f, 4.5f);

      addInstructionToVector(&instructions, 0, 0, VPU_REGISTER_VF04, VPU_REGISTER_VF03, 0, VPU_ABS, 0);
      addNOPLowerInstructionToVector(&instructions);
      addTerminationInstructionToVector(&instructions);

      vpu.uploadMicroInstructions(&instructions);
      vpu.initMicroMode();

      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->x == 5.0f);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->y == 2.4f);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->z == 1.0f);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->w == 4.5f);
      REQUIRE(vpu.elapsedCycles() == 4);
      REQUIRE(vpu.getState() == VPU_STATE_STOP);
    }
}
