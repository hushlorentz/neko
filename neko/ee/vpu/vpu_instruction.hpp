#ifndef VPU_INSTRUCTION_HPP
#define VPU_INSTRUCTION_HPP

#include <cstdint>

enum class VUMacroInstructionKind : std::uint8_t
{
  Invalid,
  Upper,
  Lower
};

bool decodeVUUpperInstruction(
  std::uint32_t instruction,
  std::uint16_t *opCode);
VUMacroInstructionKind classifyVUMacroInstruction(
  std::uint32_t instruction);

#endif
