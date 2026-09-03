#ifndef EE_INSTRUCTION_HPP
#define EE_INSTRUCTION_HPP

#include <cstdint>
#include <stdexcept>

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
  AddImmediateUnsignedDoubleword,
  MoveFromHI,
  MoveToHI,
  MoveFromLO,
  MoveToLO,
  MultiplyWord,
  MultiplyUnsignedWord,
  DivideWord,
  DivideUnsignedWord,
  MultiplyAddWord,
  MultiplyAddUnsignedWord,
  MoveFromHI1,
  MoveToHI1,
  MoveFromLO1,
  MoveToLO1,
  MultiplyWord1,
  MultiplyUnsignedWord1,
  DivideWord1,
  DivideUnsignedWord1,
  MultiplyAddWord1,
  MultiplyAddUnsignedWord1,
  MoveFromShiftAmount,
  MoveToShiftAmount,
  MoveByteCountToShiftAmount,
  MoveHalfwordCountToShiftAmount,
  Jump,
  JumpAndLink,
  JumpRegister,
  JumpAndLinkRegister,
  BranchEqual,
  BranchNotEqual,
  BranchLessThanOrEqualZero,
  BranchGreaterThanZero,
  BranchLessThanZero,
  BranchGreaterThanOrEqualZero,
  BranchEqualLikely,
  BranchNotEqualLikely,
  BranchLessThanOrEqualZeroLikely,
  BranchGreaterThanZeroLikely,
  BranchLessThanZeroLikely,
  BranchGreaterThanOrEqualZeroLikely,
  BranchLessThanZeroAndLink,
  BranchGreaterThanOrEqualZeroAndLink,
  BranchLessThanZeroAndLinkLikely,
  BranchGreaterThanOrEqualZeroAndLinkLikely,
  LoadByte,
  LoadByteUnsigned,
  StoreByte,
  LoadHalfword,
  LoadHalfwordUnsigned,
  StoreHalfword,
  LoadWord,
  LoadWordUnsigned,
  StoreWord,
  LoadWordLeft,
  LoadWordRight,
  StoreWordLeft,
  StoreWordRight,
  LoadDoubleword,
  StoreDoubleword,
  LoadDoublewordLeft,
  LoadDoublewordRight,
  StoreDoublewordLeft,
  StoreDoublewordRight,
  LoadQuadword,
  StoreQuadword,
  SystemCall,
  Breakpoint,
  ExceptionReturn,
  LoadQuadwordToCOP2,
  StoreQuadwordFromCOP2,
  QuadwordMoveFromCOP2,
  QuadwordMoveToCOP2,
  ControlMoveFromCOP2,
  ControlMoveToCOP2,
  BranchCOP2False,
  BranchCOP2FalseLikely,
  BranchCOP2True,
  BranchCOP2TrueLikely
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

enum class EEInstructionDecodeFailure : std::uint8_t
{
  Reserved,
  Unsupported
};

class EEInstructionDecodeError : public std::runtime_error
{
  public:
    EEInstructionDecodeError(
      EEInstructionDecodeFailure failure,
      const char *message);
    EEInstructionDecodeFailure failure() const;

  private:
    EEInstructionDecodeFailure failureType;
};

EEInstruction decodeEEInstruction(std::uint32_t instruction);
bool isEEBranchOperation(EEOperation operation);
bool isEEBranchLikelyOperation(EEOperation operation);

#endif
