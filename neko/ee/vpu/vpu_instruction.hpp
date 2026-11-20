#ifndef VPU_INSTRUCTION_HPP
#define VPU_INSTRUCTION_HPP

#include <cstdint>

enum class VUMacroInstructionKind : std::uint8_t
{
  Invalid,
  Upper,
  Lower
};

struct LowerInstruction;

bool decodeVUUpperInstruction(
  std::uint32_t instruction,
  std::uint16_t *opCode);
LowerInstruction decodeVUMacroLowerInstruction(std::uint32_t instruction);
VUMacroInstructionKind classifyVUMacroInstruction(
  std::uint32_t instruction);

#endif
