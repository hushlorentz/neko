#ifndef EE_INSTRUCTION_HPP
#define EE_INSTRUCTION_HPP

#include <cstdint>

enum class EEOperation : std::uint8_t
{
  Nop,
  ShiftLeftLogicalWord,
  ShiftRightLogicalWord,
  ShiftRightArithmeticWord,
  ShiftLeftLogicalVariableWord,
  ShiftRightLogicalVariableWord,
  ShiftRightArithmeticVariableWord,
  ShiftLeftLogicalVariableDoubleword,
  ShiftRightLogicalVariableDoubleword,
  ShiftRightArithmeticVariableDoubleword,
  AddWord,
  AddUnsignedWord,
  SubtractWord,
  SubtractUnsignedWord,
  And,
  Or,
  Xor,
  Nor,
  SetLessThan,
  SetLessThanUnsigned,
  AddDoubleword,
  AddUnsignedDoubleword,
  SubtractDoubleword,
  SubtractUnsignedDoubleword,
  ShiftLeftLogicalDoubleword,
  ShiftRightLogicalDoubleword,
  ShiftRightArithmeticDoubleword,
  ShiftLeftLogicalDoubleword32,
  ShiftRightLogicalDoubleword32,
  ShiftRightArithmeticDoubleword32,
  AddImmediateWord,
  AddImmediateUnsignedWord,
  SetLessThanImmediate,
  SetLessThanImmediateUnsigned,
  AndImmediate,
  OrImmediate,
  XorImmediate,
  LoadUpperImmediate,
  AddImmediateDoubleword,
  AddImmediateUnsignedDoubleword
};

struct EEInstruction
{
  EEOperation operation = EEOperation::Nop;
  std::uint32_t raw = 0;
  std::uint8_t opcode = 0;
  std::uint8_t sourceRegister = 0;
  std::uint8_t targetRegister = 0;
  std::uint8_t destinationRegister = 0;
  std::uint8_t shiftAmount = 0;
  std::uint8_t function = 0;
  std::uint16_t immediate = 0;
  std::uint32_t target = 0;
};

EEInstruction decodeEEInstruction(std::uint32_t instruction);

#endif
