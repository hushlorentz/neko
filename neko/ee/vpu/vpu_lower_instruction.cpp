#include <stdexcept>

#include "vpu_field_mask.hpp"
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

  std::int16_t signedImmediate11(std::uint32_t instruction)
  {
    std::uint16_t immediate = instruction & LOWER_IMMEDIATE_LOW_MASK;
    if ((immediate & 0x400) != 0)
    {
      immediate |= 0xf800;
    }
    return static_cast<std::int16_t>(immediate);
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
    decoded.immediate = static_cast<std::int16_t>(
      (((instruction >> LOWER_DEST_SHIFT) & LOWER_DEST_MASK) << 11) |
      (instruction & LOWER_IMMEDIATE_LOW_MASK));
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE7_MASK) == VPU_IBNE_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::Branch;
    decoded.opCode = VPU_IBNE;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.sourceRegister2 = registerField(instruction, LOWER_IT_SHIFT);
    decoded.immediate = signedImmediate11(instruction);
    return decoded;
  }

  if ((instruction & VPU_LOWER_JALR_MASK) == VPU_JALR_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::Branch;
    decoded.opCode = VPU_JALR;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.destinationRegister = registerField(instruction, LOWER_IT_SHIFT);
    return decoded;
  }

  if ((instruction & VPU_LOWER_JR_MASK) == VPU_JR_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::Branch;
    decoded.opCode = VPU_JR;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE8_MASK) == VPU_ILW_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::LSU;
    decoded.opCode = VPU_ILW;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.destinationRegister = registerField(instruction, LOWER_IT_SHIFT);
    decoded.destinationFieldMask = vpuFieldMaskFromEncoding(
      (instruction >> LOWER_DEST_SHIFT) & LOWER_DEST_MASK);
    decoded.immediate = signedImmediate11(instruction);
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE8_MASK) == VPU_LQ_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::LSU;
    decoded.opCode = VPU_LQ;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.destinationRegister = registerField(instruction, LOWER_IT_SHIFT);
    decoded.destinationFieldMask = vpuFieldMaskFromEncoding(
      (instruction >> LOWER_DEST_SHIFT) & LOWER_DEST_MASK);
    decoded.immediate = signedImmediate11(instruction);
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE3_MASK) == VPU_MFIR_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::FMAC;
    decoded.opCode = VPU_MFIR;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.destinationRegister = registerField(instruction, LOWER_IT_SHIFT);
    decoded.destinationFieldMask = vpuFieldMaskFromEncoding(
      (instruction >> LOWER_DEST_SHIFT) & LOWER_DEST_MASK);
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE3_MASK) == VPU_SQI_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::LSU;
    decoded.opCode = VPU_SQI;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.sourceRegister2 = registerField(instruction, LOWER_IT_SHIFT);
    decoded.destinationRegister = decoded.sourceRegister2;
    decoded.destinationFieldMask = vpuFieldMaskFromEncoding(
      (instruction >> LOWER_DEST_SHIFT) & LOWER_DEST_MASK);
    return decoded;
  }

  throw std::runtime_error("Unsupported VU lower instruction.");
}
