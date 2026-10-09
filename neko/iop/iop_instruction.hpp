#ifndef IOP_INSTRUCTION_HPP
#define IOP_INSTRUCTION_HPP

#include <cstdint>

#include "iop_types.hpp"

enum class IOPOperation : std::uint8_t
{
  None,
  Nop,
  ShiftLeftLogical,
  ShiftRightLogical,
  ShiftRightArithmetic,
  ShiftLeftLogicalVariable,
  ShiftRightLogicalVariable,
  ShiftRightArithmeticVariable,
  JumpRegister,
  JumpAndLinkRegister,
  SystemCall,
  Breakpoint,
  MoveFromHI,
  MoveToHI,
  MoveFromLO,
  MoveToLO,
  Multiply,
  MultiplyUnsigned,
  Divide,
  DivideUnsigned,
  Add,
  AddUnsigned,
  Subtract,
  SubtractUnsigned,
  And,
  Or,
  Xor,
  Nor,
  SetLessThan,
  SetLessThanUnsigned,
  BranchLessThanZero,
  BranchGreaterThanOrEqualZero,
  BranchLessThanZeroAndLink,
  BranchGreaterThanOrEqualZeroAndLink,
  Jump,
  JumpAndLink,
  BranchEqual,
  BranchNotEqual,
  BranchLessThanOrEqualZero,
  BranchGreaterThanZero,
  AddImmediate,
  AddImmediateUnsigned,
  SetLessThanImmediate,
  SetLessThanImmediateUnsigned,
  AndImmediate,
  OrImmediate,
  XorImmediate,
  LoadUpperImmediate,
  MoveFromCOP0,
  MoveToCOP0,
  ReturnFromException,
  LoadByte,
  LoadHalfword,
  LoadWordLeft,
  LoadWord,
  LoadByteUnsigned,
  LoadHalfwordUnsigned,
  LoadWordRight,
  StoreByte,
  StoreHalfword,
  StoreWordLeft,
  StoreWord,
  StoreWordRight,
  CoprocessorOperation,
  LoadWordToCoprocessor,
  StoreWordFromCoprocessor
};

enum class IOPDecodeDisposition : std::uint8_t
{
  Supported,
  Reserved,
  ValidButDeferred
};

struct IOPInstruction
{
  IOPOperation operation = IOPOperation::None;
  IOPWord raw = 0;
  std::uint8_t opcode = 0;
  std::uint8_t sourceRegister = 0;
  std::uint8_t targetRegister = 0;
  std::uint8_t destinationRegister = 0;
  std::uint8_t shiftAmount = 0;
  std::uint8_t function = 0;
  std::uint16_t immediate = 0;
  std::uint32_t target = 0;
};

struct IOPDecodeResult
{
  IOPDecodeDisposition disposition =
    IOPDecodeDisposition::Reserved;
  IOPInstruction instruction;
};

IOPDecodeResult decodeIOPInstruction(IOPWord raw);

#endif
