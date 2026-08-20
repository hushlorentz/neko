#ifndef VPU_LOWER_INSTRUCTION_HPP
#define VPU_LOWER_INSTRUCTION_HPP

#include <cstdint>

enum class LowerExecutionUnit : std::uint8_t
{
  None,
  IALU,
  FMAC
};

struct LowerInstruction
{
  LowerExecutionUnit unit = LowerExecutionUnit::None;
  std::uint32_t opCode = 0;
  std::uint8_t sourceRegister1 = 0;
  std::uint8_t sourceRegister2 = 0;
  std::uint8_t destinationRegister = 0;
  std::uint8_t destinationFieldMask = 0;
  std::uint16_t immediate = 0;
};

LowerInstruction decodeLowerInstruction(std::uint32_t instruction);

#endif
