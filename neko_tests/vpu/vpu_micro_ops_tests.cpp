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

void addNOPHalfInstructionToVector(vector<uint8_t> * instructions)
{
  for (int i = 0; i < 4; i++)
  {
    instructions->push_back(0);
  }
}

void addNOPFullInstructionToVector(vector<uint8_t> * instructions)
{
  addNOPHalfInstructionToVector(instructions);
  addNOPHalfInstructionToVector(instructions);
}

void executeSingleUpperInstruction(VPU * vpu, vector<uint8_t> * instructions, uint32_t bitFlags, uint32_t destFlags, uint8_t ftRegID, uint8_t fsRegID, uint8_t fdRegID, uint16_t opCode, uint8_t bcFlags)
{
  addInstructionToVector(instructions, VPU_E_BIT_MASK | bitFlags, destFlags, ftRegID, fsRegID, fdRegID, opCode, bcFlags);
  addNOPHalfInstructionToVector(instructions);
  addNOPFullInstructionToVector(instructions);
  vpu->uploadMicroInstructions(instructions);
  vpu->initMicroMode();
}

TEST_CASE("VPU Microinstruction Operation Tests")
{
    VPU vpu;
    vpu.useThreads = false;
    vpu.loadFPRegister(VPU_REGISTER_VF03, -5.0f, -2.4f, -1.0f, 4.5f);
    vector<uint8_t> instructions;
    
    SECTION("ABS stores the absolute value of src vector in dest vector")
    {
      executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, 0, VPU_ABS, 0);

      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->x == 5.0f);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->y == 2.4f);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->z == 1.0f);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->w == 4.5f);
      REQUIRE(vpu.elapsedCycles() == 4);
      REQUIRE(vpu.getState() == VPU_STATE_STOP);
    }
}
