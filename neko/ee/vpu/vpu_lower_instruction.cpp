#include <stdexcept>

#include "vpu_field_mask.hpp"
#include "vpu_lower_instruction.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"

namespace
{
  constexpr std::uint8_t LOWER_DEST_SHIFT = 21;
  constexpr std::uint8_t LOWER_IT_SHIFT = 16;
  constexpr std::uint8_t LOWER_IS_SHIFT = 11;
  constexpr std::uint8_t LOWER_ID_SHIFT = 6;
  constexpr std::uint8_t LOWER_FSF_SHIFT = 21;
  constexpr std::uint8_t LOWER_FTF_SHIFT = 23;
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

  std::int16_t signedImmediate5(std::uint32_t instruction)
  {
    std::uint8_t immediate = (instruction >> LOWER_ID_SHIFT) & 0x1f;
    if ((immediate & 0x10) != 0)
    {
      immediate |= 0xe0;
    }
    return static_cast<std::int8_t>(immediate);
  }

  std::int16_t unsignedImmediate15(std::uint32_t instruction)
  {
    return static_cast<std::int16_t>(
      (((instruction >> LOWER_DEST_SHIFT) & LOWER_DEST_MASK) << 11) |
      (instruction & LOWER_IMMEDIATE_LOW_MASK));
  }

  std::uint8_t selectedField(
    std::uint32_t instruction,
    std::uint8_t shift)
  {
    return static_cast<std::uint8_t>(
      1u << ((instruction >> shift) & 0x3));
  }
}

LowerInstruction decodeLowerInstruction(std::uint32_t instruction)
{
  LowerInstruction decoded;

  if (instruction == VPU_LOWER_NOP)
  {
    return decoded;
  }

  const std::uint32_t type1Encoding = instruction & VPU_LOWER_TYPE1_MASK;
  if (type1Encoding == VPU_IADD_ENCODING ||
      type1Encoding == VPU_IAND_ENCODING ||
      type1Encoding == VPU_IOR_ENCODING ||
      type1Encoding == VPU_ISUB_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::IALU;
    decoded.opCode = instruction & VPU_TYPE1_MASK;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.sourceRegister2 = registerField(instruction, LOWER_IT_SHIFT);
    decoded.destinationRegister = registerField(instruction, LOWER_ID_SHIFT);
    return decoded;
  }

  if (type1Encoding == VPU_IADDI_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::IALU;
    decoded.opCode = VPU_IADDI;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.destinationRegister = registerField(instruction, LOWER_IT_SHIFT);
    decoded.immediate = signedImmediate5(instruction);
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE8_MASK) == VPU_IADDIU_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::IALU;
    decoded.opCode = VPU_IADDIU;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.destinationRegister = registerField(instruction, LOWER_IT_SHIFT);
    decoded.immediate = unsignedImmediate15(instruction);
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE8_MASK) == VPU_ISUBIU_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::IALU;
    decoded.opCode = VPU_ISUBIU;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.destinationRegister = registerField(instruction, LOWER_IT_SHIFT);
    decoded.immediate = unsignedImmediate15(instruction);
    return decoded;
  }

  const std::uint32_t type9Encoding = instruction & 0xff000000;
  if (type9Encoding == VPU_FCEQ_ENCODING ||
      type9Encoding == VPU_FCSET_ENCODING ||
      type9Encoding == VPU_FCAND_ENCODING ||
      type9Encoding == VPU_FCOR_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::Flag;
    if (type9Encoding == VPU_FCEQ_ENCODING)
    {
      decoded.opCode = VPU_FCEQ;
    }
    else if (type9Encoding == VPU_FCSET_ENCODING)
    {
      decoded.opCode = VPU_FCSET;
    }
    else if (type9Encoding == VPU_FCAND_ENCODING)
    {
      decoded.opCode = VPU_FCAND;
    }
    else
    {
      decoded.opCode = VPU_FCOR;
    }
    decoded.integerDestinationRegister =
      decoded.opCode == VPU_FCSET
        ? VPU_REGISTER_VI00
        : VPU_REGISTER_VI01;
    decoded.immediateBits = instruction & 0x00ffffff;
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE8_MASK) == VPU_FCGET_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::Flag;
    decoded.opCode = VPU_FCGET;
    decoded.integerDestinationRegister =
      registerField(instruction, LOWER_IT_SHIFT);
    return decoded;
  }

  const std::uint32_t type8Encoding =
    instruction & VPU_LOWER_TYPE8_MASK;
  if (type8Encoding == VPU_FSEQ_ENCODING ||
      type8Encoding == VPU_FSSET_ENCODING ||
      type8Encoding == VPU_FSAND_ENCODING ||
      type8Encoding == VPU_FSOR_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::Flag;
    if (type8Encoding == VPU_FSEQ_ENCODING)
    {
      decoded.opCode = VPU_FSEQ;
    }
    else if (type8Encoding == VPU_FSSET_ENCODING)
    {
      decoded.opCode = VPU_FSSET;
    }
    else if (type8Encoding == VPU_FSAND_ENCODING)
    {
      decoded.opCode = VPU_FSAND;
    }
    else
    {
      decoded.opCode = VPU_FSOR;
    }
    decoded.integerDestinationRegister =
      decoded.opCode == VPU_FSSET
        ? VPU_REGISTER_VI00
        : registerField(instruction, LOWER_IT_SHIFT);
    decoded.immediateBits =
      (((instruction >> LOWER_DEST_SHIFT) & 1) << 11) |
      (instruction & LOWER_IMMEDIATE_LOW_MASK);
    return decoded;
  }

  if (type8Encoding == VPU_FMEQ_ENCODING ||
      type8Encoding == VPU_FMAND_ENCODING ||
      type8Encoding == VPU_FMOR_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::Flag;
    if (type8Encoding == VPU_FMEQ_ENCODING)
    {
      decoded.opCode = VPU_FMEQ;
    }
    else if (type8Encoding == VPU_FMAND_ENCODING)
    {
      decoded.opCode = VPU_FMAND;
    }
    else
    {
      decoded.opCode = VPU_FMOR;
    }
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.integerDestinationRegister =
      registerField(instruction, LOWER_IT_SHIFT);
    return decoded;
  }

  const std::uint32_t type7Encoding =
    instruction & VPU_LOWER_TYPE7_MASK;
  if (type7Encoding == VPU_IBEQ_ENCODING ||
      type7Encoding == VPU_IBNE_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::Branch;
    decoded.opCode = type7Encoding == VPU_IBEQ_ENCODING
      ? VPU_IBEQ
      : VPU_IBNE;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.sourceRegister2 = registerField(instruction, LOWER_IT_SHIFT);
    decoded.immediate = signedImmediate11(instruction);
    return decoded;
  }

  if (type7Encoding == VPU_IBGEZ_ENCODING ||
      type7Encoding == VPU_IBGTZ_ENCODING ||
      type7Encoding == VPU_IBLEZ_ENCODING ||
      type7Encoding == VPU_IBLTZ_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::Branch;
    if (type7Encoding == VPU_IBGEZ_ENCODING)
    {
      decoded.opCode = VPU_IBGEZ;
    }
    else if (type7Encoding == VPU_IBGTZ_ENCODING)
    {
      decoded.opCode = VPU_IBGTZ;
    }
    else if (type7Encoding == VPU_IBLEZ_ENCODING)
    {
      decoded.opCode = VPU_IBLEZ;
    }
    else
    {
      decoded.opCode = VPU_IBLTZ;
    }
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.immediate = signedImmediate11(instruction);
    return decoded;
  }

  if (type7Encoding == VPU_B_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::Branch;
    decoded.opCode = VPU_B;
    decoded.immediate = signedImmediate11(instruction);
    return decoded;
  }

  if (type7Encoding == VPU_BAL_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::Branch;
    decoded.opCode = VPU_BAL;
    decoded.destinationRegister =
      registerField(instruction, LOWER_IT_SHIFT);
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
    decoded.integerDestinationRegister = decoded.destinationRegister;
    decoded.destinationFieldMask = vpuFieldMaskFromEncoding(
      (instruction >> LOWER_DEST_SHIFT) & LOWER_DEST_MASK);
    decoded.immediate = signedImmediate11(instruction);
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE3_MASK) == VPU_ILWR_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::LSU;
    decoded.opCode = VPU_ILWR;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.destinationRegister = registerField(instruction, LOWER_IT_SHIFT);
    decoded.integerDestinationRegister = decoded.destinationRegister;
    decoded.destinationFieldMask = vpuFieldMaskFromEncoding(
      (instruction >> LOWER_DEST_SHIFT) & LOWER_DEST_MASK);
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE8_MASK) == VPU_ISW_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::LSU;
    decoded.opCode = VPU_ISW;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.sourceRegister2 = registerField(instruction, LOWER_IT_SHIFT);
    decoded.destinationFieldMask = vpuFieldMaskFromEncoding(
      (instruction >> LOWER_DEST_SHIFT) & LOWER_DEST_MASK);
    decoded.immediate = signedImmediate11(instruction);
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE3_MASK) == VPU_ISWR_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::LSU;
    decoded.opCode = VPU_ISWR;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.sourceRegister2 = registerField(instruction, LOWER_IT_SHIFT);
    decoded.destinationFieldMask = vpuFieldMaskFromEncoding(
      (instruction >> LOWER_DEST_SHIFT) & LOWER_DEST_MASK);
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

  if ((instruction & VPU_LOWER_TYPE3_MASK) == VPU_LQD_ENCODING ||
      (instruction & VPU_LOWER_TYPE3_MASK) == VPU_LQI_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::LSU;
    decoded.opCode = instruction & VPU_TYPE3_MASK;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.destinationRegister = registerField(instruction, LOWER_IT_SHIFT);
    decoded.integerDestinationRegister = decoded.sourceRegister1;
    decoded.destinationFieldMask = vpuFieldMaskFromEncoding(
      (instruction >> LOWER_DEST_SHIFT) & LOWER_DEST_MASK);
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE8_MASK) == VPU_SQ_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::LSU;
    decoded.opCode = VPU_SQ;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.sourceRegister2 = registerField(instruction, LOWER_IT_SHIFT);
    decoded.destinationFieldMask = vpuFieldMaskFromEncoding(
      (instruction >> LOWER_DEST_SHIFT) & LOWER_DEST_MASK);
    decoded.immediate = signedImmediate11(instruction);
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE3_MASK) == VPU_DIV_ENCODING ||
      (instruction & VPU_LOWER_TYPE3_MASK) == VPU_RSQRT_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::FDIV;
    decoded.opCode = instruction & VPU_TYPE3_MASK;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.sourceRegister2 = registerField(instruction, LOWER_IT_SHIFT);
    decoded.sourceFieldMask1 = selectedField(instruction, LOWER_FSF_SHIFT);
    decoded.sourceFieldMask2 = selectedField(instruction, LOWER_FTF_SHIFT);
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE3_MASK) == VPU_SQRT_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::FDIV;
    decoded.opCode = VPU_SQRT;
    decoded.sourceRegister2 = registerField(instruction, LOWER_IT_SHIFT);
    decoded.sourceFieldMask2 = selectedField(instruction, LOWER_FTF_SHIFT);
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE3_MASK) == VPU_WAITQ_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::WaitQ;
    decoded.opCode = VPU_WAITQ;
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE3_MASK) == VPU_XGKICK_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::XGKICK;
    decoded.opCode = VPU_XGKICK;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
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

  if ((instruction & VPU_LOWER_TYPE3_MASK) == VPU_MOVE_ENCODING ||
      (instruction & VPU_LOWER_TYPE3_MASK) == VPU_MR32_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::FMAC;
    decoded.opCode = instruction & VPU_TYPE3_MASK;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.destinationRegister = registerField(instruction, LOWER_IT_SHIFT);
    decoded.destinationFieldMask = vpuFieldMaskFromEncoding(
      (instruction >> LOWER_DEST_SHIFT) & LOWER_DEST_MASK);
    if (decoded.opCode == VPU_MOVE)
    {
      decoded.sourceFieldMask1 = decoded.destinationFieldMask;
    }
    else
    {
      decoded.sourceFieldMask1 =
        ((decoded.destinationFieldMask << 1) |
         (decoded.destinationFieldMask >> 3)) &
        LOWER_DEST_MASK;
    }
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE3_MASK) == VPU_MTIR_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::FMAC;
    decoded.opCode = VPU_MTIR;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.integerDestinationRegister =
      registerField(instruction, LOWER_IT_SHIFT);
    decoded.sourceFieldMask1 = selectedField(instruction, LOWER_FSF_SHIFT);
    return decoded;
  }

  if ((instruction & VPU_LOWER_RANDOM_DEST_MASK) == VPU_RGET_ENCODING ||
      (instruction & VPU_LOWER_RANDOM_DEST_MASK) == VPU_RNEXT_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::Random;
    decoded.opCode = instruction & VPU_TYPE3_MASK;
    decoded.destinationRegister = registerField(instruction, LOWER_IT_SHIFT);
    decoded.destinationFieldMask = vpuFieldMaskFromEncoding(
      (instruction >> LOWER_DEST_SHIFT) & LOWER_DEST_MASK);
    return decoded;
  }

  if ((instruction & VPU_LOWER_RANDOM_SOURCE_MASK) == VPU_RINIT_ENCODING ||
      (instruction & VPU_LOWER_RANDOM_SOURCE_MASK) == VPU_RXOR_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::Random;
    decoded.opCode = instruction & VPU_TYPE3_MASK;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.sourceFieldMask1 = selectedField(instruction, LOWER_FSF_SHIFT);
    return decoded;
  }

  if ((instruction & VPU_LOWER_TYPE3_MASK) == VPU_SQD_ENCODING ||
      (instruction & VPU_LOWER_TYPE3_MASK) == VPU_SQI_ENCODING)
  {
    decoded.unit = LowerExecutionUnit::LSU;
    decoded.opCode = instruction & VPU_TYPE3_MASK;
    decoded.sourceRegister1 = registerField(instruction, LOWER_IS_SHIFT);
    decoded.sourceRegister2 = registerField(instruction, LOWER_IT_SHIFT);
    decoded.integerDestinationRegister = decoded.sourceRegister2;
    decoded.destinationFieldMask = vpuFieldMaskFromEncoding(
      (instruction >> LOWER_DEST_SHIFT) & LOWER_DEST_MASK);
    return decoded;
  }

  throw std::runtime_error("Unsupported VU lower instruction.");
}
