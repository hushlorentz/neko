#include "vpu.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"
#include "vpu_upper_instruction_utils.hpp"

void addInstructionToVector(vector<uint8_t> * instructions, uint32_t bitFlags, uint32_t destFlags, uint8_t ftRegID, uint8_t fsRegID, uint8_t fdRegID, uint16_t opCode)
{
  uint32_t instruction = bitFlags;
  instruction |= destFlags;
  instruction |= ftRegID << VPU_FT_REG_SHIFT;
  instruction |= fsRegID  << VPU_FS_REG_SHIFT;
  instruction |= fdRegID << VPU_FD_REG_SHIFT;
  instruction |= opCode;

  instructions->push_back((instruction >> 24) & 0xff);
  instructions->push_back((instruction >> 16) & 0xff);
  instructions->push_back((instruction >> 8) & 0xff);
  instructions->push_back(instruction & 0xff);
}

void addNOPHalfInstructionToVector(vector<uint8_t> * instructions)
{
  addInstructionToVector(instructions, 0, 0, 0, 0, 0, VPU_NOP);
}

void addNOPFullInstructionToVector(vector<uint8_t> * instructions)
{
  addNOPHalfInstructionToVector(instructions);
  addNOPHalfInstructionToVector(instructions);
}

void addSingleUpperInstruction(vector<uint8_t> * instructions, uint32_t bitFlags, uint32_t destFlags, uint8_t ftRegID, uint8_t fsRegID, uint8_t fdRegID, uint16_t opCode)
{
  addInstructionToVector(instructions, bitFlags, destFlags, ftRegID, fsRegID, fdRegID, opCode);
  addNOPHalfInstructionToVector(instructions);
}

void executeSingleUpperInstruction(VPU * vpu, vector<uint8_t> * instructions, uint32_t bitFlags, uint32_t destFlags, uint8_t ftRegID, uint8_t fsRegID, uint8_t fdRegID, uint16_t opCode)
{
  addSingleUpperInstruction(instructions, VPU_E_BIT | bitFlags, destFlags, ftRegID, fsRegID, fdRegID, opCode);
  addNOPFullInstructionToVector(instructions);
  vpu->uploadMicroInstructions(instructions);
  vpu->initMicroMode();
}
