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

void addSingleUpperInstruction(vector<uint8_t> * instructions, uint32_t bitFlags, uint32_t destFlags, uint8_t ftRegID, uint8_t fsRegID, uint8_t fdRegID, uint16_t opCode, uint8_t bcFlags)
{
  addInstructionToVector(instructions, bitFlags, destFlags, ftRegID, fsRegID, fdRegID, opCode, bcFlags);
  addNOPHalfInstructionToVector(instructions);
}

void executeSingleUpperInstruction(VPU * vpu, vector<uint8_t> * instructions, uint32_t bitFlags, uint32_t destFlags, uint8_t ftRegID, uint8_t fsRegID, uint8_t fdRegID, uint16_t opCode, uint8_t bcFlags)
{
  addSingleUpperInstruction(instructions, VPU_E_BIT | bitFlags, destFlags, ftRegID, fsRegID, fdRegID, opCode, bcFlags);
  addNOPFullInstructionToVector(instructions);
  vpu->uploadMicroInstructions(instructions);
  vpu->initMicroMode();
}

TEST_CASE("VPU Microinstruction Operation Tests")
{
    VPU vpu;
    vpu.useThreads = false;
    vpu.loadFPRegister(VPU_REGISTER_VF03, -5.0f, -2.4f, -1.0f, 4.5f);
    vpu.loadFPRegister(VPU_REGISTER_VF10, -2.5f, 2.4f, 10.0f, 9.0f);
    vpu.loadFPRegister(VPU_REGISTER_VF05, 5.0f, -6.4f, 10.0f, -9.0f);
    vpu.loadFPRegister(VPU_REGISTER_VF06, -5.0f, 6.4f, -10.0f, 9.0f);
    vector<uint8_t> instructions;
    
    SECTION("ABS stores the absolute value of src vector in dest vector")
    {
      executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, 0, VPU_ABS, 0);

      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->x == 5.0f);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->y == 2.4f);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->z == 1.0f);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->w == 4.5f);
      REQUIRE(vpu.elapsedCycles() == 7);
      REQUIRE(vpu.getState() == VPU_STATE_STOP);
    }

    SECTION("When an ABS reads from the destination of the previous instruction, a stall occurs")
    {
      addSingleUpperInstruction(&instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, 0, VPU_ABS, 0);
      executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF05, VPU_REGISTER_VF04, 0, VPU_ABS, 0);

      REQUIRE(vpu.elapsedCycles() == 11);
      REQUIRE(vpu.getState() == VPU_STATE_STOP);
    }

    SECTION("ABS three stalls")
    {
      addSingleUpperInstruction(&instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, 0, VPU_ABS, 0);
      addSingleUpperInstruction(&instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF05, VPU_REGISTER_VF04, 0, VPU_ABS, 0);
      addSingleUpperInstruction(&instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF06, VPU_REGISTER_VF05, 0, VPU_ABS, 0);
      executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF07, VPU_REGISTER_VF06, 0, VPU_ABS, 0);

      REQUIRE(vpu.elapsedCycles() == 19);
      REQUIRE(vpu.getState() == VPU_STATE_STOP);
    }

    SECTION("ADD stores the addition of the two src vectors in dest vector")
    {
      executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF10, VPU_REGISTER_VF03, VPU_REGISTER_VF04, VPU_ADD, 0);

      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->x == -7.5f);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->y == 0);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->z == 9);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->w == 13.5f);
      REQUIRE(vpu.elapsedCycles() == 7);
      REQUIRE(vpu.getState() == VPU_STATE_STOP);
    }

    SECTION("ADDing vectors of opposite direction, but equal magnitude sets the zero flags.")
    {
      executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF06, VPU_REGISTER_VF05, VPU_REGISTER_VF15, VPU_ADD, 0);
      REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZX));
      REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZY));
      REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZZ));
      REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZW));
    }

    SECTION("ADDing vectors of opposite direction, but equal magnitude and then doing a second addition unsets the zero flags.")
    {
      addSingleUpperInstruction(&instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF06, VPU_REGISTER_VF05, VPU_REGISTER_VF15, VPU_ADD, 0);
      executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF03, VPU_REGISTER_VF06, VPU_REGISTER_VF04, VPU_ADD, 0);

      REQUIRE(!vpu.hasMACFlag(VPU_FLAG_ZX));
      REQUIRE(!vpu.hasMACFlag(VPU_FLAG_ZY));
      REQUIRE(!vpu.hasMACFlag(VPU_FLAG_ZZ));
      REQUIRE(!vpu.hasMACFlag(VPU_FLAG_ZW));
    }
}
