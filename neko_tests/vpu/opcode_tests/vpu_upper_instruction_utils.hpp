#ifndef VPU_UPPER_INSTRUCTION_UTILS_HPP
#define VPU_UPPER_INSTRUCTION_UTILS_HPP

void addInstructionToVector(vector<uint8_t> * instructions, uint32_t bitFlags, uint32_t destFlags, uint8_t ftRegID, uint8_t fsRegID, uint8_t fdRegID, uint16_t opCode);
void addNOPHalfInstructionToVector(vector<uint8_t> * instructions);
void addNOPFullInstructionToVector(vector<uint8_t> * instructions);
void addSingleUpperInstruction(vector<uint8_t> * instructions, uint32_t bitFlags, uint32_t destFlags, uint8_t ftRegID, uint8_t fsRegID, uint8_t fdRegID, uint16_t opCode);
void executeSingleUpperInstruction(VPU * vpu, vector<uint8_t> * instructions, uint32_t bitFlags, uint32_t destFlags, uint8_t ftRegID, uint8_t fsRegID, uint8_t fdRegID, uint16_t opCode);

#endif
