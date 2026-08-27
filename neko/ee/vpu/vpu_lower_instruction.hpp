#ifndef VPU_LOWER_INSTRUCTION_HPP
#define VPU_LOWER_INSTRUCTION_HPP

#include <cstdint>

enum class LowerExecutionUnit : std::uint8_t
{
  None,
  Immediate,
  IALU,
  LSU,
  FMAC,
  FDIV,
  WaitQ,
  XGKICK,
  Branch
};

struct LowerInstruction
{
  LowerExecutionUnit unit = LowerExecutionUnit::None;
  std::uint32_t opCode = 0;
  std::uint8_t sourceRegister1 = 0;
  std::uint8_t sourceRegister2 = 0;
  std::uint8_t destinationRegister = 0;
  std::uint8_t integerDestinationRegister = 0;
  std::uint8_t destinationFieldMask = 0;
  std::uint8_t sourceFieldMask1 = 0;
  std::uint8_t sourceFieldMask2 = 0;
  std::int16_t immediate = 0;
  std::uint32_t immediateBits = 0;
};

LowerInstruction decodeLowerInstruction(std::uint32_t instruction);

#endif
