#include <stdexcept>

#include "vpu_lower_instruction.hpp"
#include "vpu_opcodes.hpp"

namespace
{
  constexpr std::uint8_t LOWER_DEST_SHIFT = 21;
  constexpr std::uint8_t LOWER_IT_SHIFT = 16;
  constexpr std::uint8_t LOWER_IS_SHIFT = 11;
  constexpr std::uint8_t LOWER_ID_SHIFT = 6;
  constexpr std::uint32_t LOWER_REGISTER_MASK = 0x1f;
  constexpr std::uint32_t LOWER_DEST_MASK = 0xf;
  constexpr std::uint32_t LOWER_IMMEDIATE_LOW_MASK = 0x7ff;

  std::uint8_t registerField(std::uint32_t instruction, std::uint8_t shift)
  {
    return (instruction >> shift) & LOWER_REGISTER_MASK;
  }
}

LowerInstruction decodeLowerInstruction(std::uint32_t instruction)
{
  LowerInstruction decoded;

  if (instruction == VPU_LOWER_NOP)
  {
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE1_MASK) == VPU_IADD_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::IALU;
    decoded.opCode = VPU_IADD;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.sourceRegister2 = registerField(instruction, LOWER_IT_SHIFT);
    decoded.destinationRegister = registerField(instruction, LOWER_ID_SHIFT);
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE8_MASK) == VPU_ISUBIU_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::IALU;
    decoded.opCode = VPU_ISUBIU;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.destinationRegister = registerField(instruction, LOWER_IT_SHIFT);
    decoded.immediate =
      (((instruction >> LOWER_DEST_SHIFT) & LOWER_DEST_MASK) << 11) |
      (instruction & LOWER_IMMEDIATE_LOW_MASK);
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE3_MASK) == VPU_MFIR_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::FMAC;
    decoded.opCode = VPU_MFIR;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.destinationRegister = registerField(instruction, LOWER_IT_SHIFT);
    decoded.destinationFieldMask =
      (instruction >> LOWER_DEST_SHIFT) & LOWER_DEST_MASK;
    return decoded;
  }

  throw std::runtime_error("Unsupported VU lower instruction.");
}
