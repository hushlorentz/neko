#include "vpu_instruction.hpp"

#include <array>
#include <stdexcept>

#include "vpu_lower_instruction.hpp"
#include "vpu_opcodes.hpp"

namespace
{
  constexpr std::array<std::uint16_t, 48> TYPE1_OPCODES = {{
    VPU_ADD, VPU_ADDi, VPU_ADDq, VPU_ADDx, VPU_ADDy, VPU_ADDz, VPU_ADDw,
    VPU_MADD, VPU_MADDi, VPU_MADDq, VPU_MADDx, VPU_MADDy, VPU_MADDz,
    VPU_MADDw, VPU_MAX, VPU_MAXi, VPU_MAXx, VPU_MAXy, VPU_MAXz, VPU_MAXw,
    VPU_MINI, VPU_MINIi, VPU_MINIx, VPU_MINIy, VPU_MINIz, VPU_MINIw,
    VPU_MSUB, VPU_MSUBi, VPU_MSUBq, VPU_MSUBx, VPU_MSUBy, VPU_MSUBz,
    VPU_MSUBw, VPU_MUL, VPU_MULi, VPU_MULq, VPU_MULx, VPU_MULy,
    VPU_MULz, VPU_MULw, VPU_OPMSUB, VPU_SUB, VPU_SUBi, VPU_SUBq,
    VPU_SUBx, VPU_SUBy, VPU_SUBz, VPU_SUBw
  }};

  constexpr std::array<std::uint16_t, 47> TYPE3_OPCODES = {{
    VPU_ABS, VPU_ADDA, VPU_ADDAi, VPU_ADDAq, VPU_ADDAx, VPU_ADDAy,
    VPU_ADDAz, VPU_ADDAw, VPU_CLIP, VPU_FTOI0, VPU_FTOI4, VPU_FTOI12,
    VPU_FTOI15, VPU_ITOF0, VPU_ITOF4, VPU_ITOF12, VPU_ITOF15, VPU_MADDA,
    VPU_MADDAi, VPU_MADDAq, VPU_MADDAx, VPU_MADDAy, VPU_MADDAz,
    VPU_MADDAw, VPU_MSUBA, VPU_MSUBAi, VPU_MSUBAq, VPU_MSUBAx,
    VPU_MSUBAy, VPU_MSUBAz, VPU_MSUBAw, VPU_MULA, VPU_MULAi,
    VPU_MULAq, VPU_MULAx, VPU_MULAy, VPU_MULAz, VPU_MULAw, VPU_NOP,
    VPU_OPMULA, VPU_SUBA, VPU_SUBAi, VPU_SUBAq, VPU_SUBAx, VPU_SUBAy,
    VPU_SUBAz, VPU_SUBAw
  }};

  bool upperInstructionUsesScalarRegister(std::uint16_t opCode)
  {
    switch (opCode)
    {
      case VPU_ADDi:
      case VPU_ADDAi:
      case VPU_ADDq:
      case VPU_ADDAq:
      case VPU_MADDi:
      case VPU_MADDAi:
      case VPU_MADDq:
      case VPU_MADDAq:
      case VPU_MAXi:
      case VPU_MINIi:
      case VPU_MSUBi:
      case VPU_MSUBAi:
      case VPU_MSUBq:
      case VPU_MSUBAq:
      case VPU_MULi:
      case VPU_MULAi:
      case VPU_MULq:
      case VPU_MULAq:
      case VPU_SUBi:
      case VPU_SUBAi:
      case VPU_SUBq:
      case VPU_SUBAq:
        return true;
      default:
        return false;
    }
  }

  bool isCanonicalUpperEncoding(
    std::uint16_t opCode,
    std::uint32_t instruction)
  {
    if ((instruction & VPU_UPPER_RESERVED_BITS_MASK) != 0)
    {
      return false;
    }
    if (upperInstructionUsesScalarRegister(opCode) &&
        (instruction & VPU_UPPER_FT_REGISTER_MASK) != 0)
    {
      return false;
    }
    switch (opCode)
    {
      case VPU_NOP:
        return
          (instruction & VPU_UPPER_OPERAND_FIELDS_MASK) ==
          0;
      case VPU_CLIP:
      case VPU_OPMULA:
      case VPU_OPMSUB:
        return
          (instruction & VPU_DEST_ALL_FIELDS) ==
          VPU_DEST_XYZ_FIELDS;
      default:
        return true;
    }
  }

  template<std::size_t Size>
  bool contains(
    const std::array<std::uint16_t, Size> &values,
    std::uint16_t value)
  {
    for (std::uint16_t candidate : values)
    {
      if (candidate == value)
      {
        return true;
      }
    }
    return false;
  }

  bool isMacroLowerInstruction(const LowerInstruction &instruction)
  {
    constexpr std::uint8_t INTEGER_REGISTER_COUNT = 16;
    switch (instruction.opCode)
    {
      case VPU_IADD:
      case VPU_IADDI:
      case VPU_IAND:
      case VPU_IOR:
      case VPU_ISUB:
        return
          instruction.sourceRegister1 <
            INTEGER_REGISTER_COUNT &&
          instruction.sourceRegister2 <
            INTEGER_REGISTER_COUNT &&
          instruction.destinationRegister <
            INTEGER_REGISTER_COUNT;
      case VPU_ILWR:
        return
          instruction.sourceRegister1 <
            INTEGER_REGISTER_COUNT &&
          instruction.integerDestinationRegister <
            INTEGER_REGISTER_COUNT;
      case VPU_ISWR:
        return
          instruction.sourceRegister1 <
            INTEGER_REGISTER_COUNT &&
          instruction.sourceRegister2 <
            INTEGER_REGISTER_COUNT;
      case VPU_LQD:
      case VPU_LQI:
        return
          instruction.sourceRegister1 <
          INTEGER_REGISTER_COUNT;
      case VPU_MFIR:
        return
          instruction.sourceRegister1 <
          INTEGER_REGISTER_COUNT;
      case VPU_MTIR:
        return
          instruction.integerDestinationRegister <
          INTEGER_REGISTER_COUNT;
      case VPU_SQD:
      case VPU_SQI:
        return
          instruction.sourceRegister2 <
          INTEGER_REGISTER_COUNT;
      case VPU_DIV:
      case VPU_MOVE:
      case VPU_MR32:
      case VPU_RGET:
      case VPU_RINIT:
      case VPU_RNEXT:
      case VPU_RSQRT:
      case VPU_RXOR:
      case VPU_SQRT:
      case VPU_WAITQ:
        return true;
      default:
        return false;
    }
  }
}

bool decodeVUUpperInstruction(
  std::uint32_t instruction,
  std::uint16_t *opCode)
{
  const std::uint16_t type3 =
    instruction & VPU_TYPE3_MASK;
  if (contains(TYPE3_OPCODES, type3))
  {
    if (!isCanonicalUpperEncoding(type3, instruction))
    {
      return false;
    }
    if (opCode != nullptr)
    {
      *opCode = type3;
    }
    return true;
  }

  const std::uint16_t type1 =
    instruction & VPU_TYPE1_MASK;
  if (!contains(TYPE1_OPCODES, type1) ||
      !isCanonicalUpperEncoding(type1, instruction))
  {
    return false;
  }
  if (opCode != nullptr)
  {
    *opCode = type1;
  }
  return true;
}

VUMacroInstructionKind classifyVUMacroInstruction(
  std::uint32_t instruction)
{
  instruction &= UINT32_C(0x01ffffff);
  if (decodeVUUpperInstruction(instruction, nullptr))
  {
    return VUMacroInstructionKind::Upper;
  }

  try
  {
    const LowerInstruction lower =
      decodeLowerInstruction(instruction | VPU_I_BIT);
    if (isMacroLowerInstruction(lower))
    {
      return VUMacroInstructionKind::Lower;
    }
  }
  catch (const std::runtime_error &)
  {
  }
  return VUMacroInstructionKind::Invalid;
}
