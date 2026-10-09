#include "iop_instruction.hpp"

#include <array>

namespace
{
  constexpr IOPWord SOURCE_REGISTER_MASK =
    UINT32_C(0x03e00000);
  constexpr IOPWord TARGET_REGISTER_MASK =
    UINT32_C(0x001f0000);
  constexpr IOPWord DESTINATION_REGISTER_MASK =
    UINT32_C(0x0000f800);
  constexpr IOPWord SHIFT_AMOUNT_MASK =
    UINT32_C(0x000007c0);
  constexpr IOPWord LOW_ELEVEN_MASK =
    UINT32_C(0x000007ff);

  enum class DecodeKind : std::uint8_t
  {
    Reserved,
    Direct,
    Deferred,
    Special,
    Regimm,
    Cop0,
    Cop0Function
  };

  struct DecodeEntry
  {
    DecodeKind kind = DecodeKind::Reserved;
    IOPOperation operation = IOPOperation::None;
    IOPWord requiredZeroMask = 0;
  };

  using DecodeTable = std::array<DecodeEntry, 64>;
  using SelectorTable = std::array<DecodeEntry, 32>;

  constexpr DecodeEntry reserved()
  {
    return {};
  }

  constexpr DecodeEntry direct(
    IOPOperation operation,
    IOPWord requiredZeroMask = 0)
  {
    return {
      DecodeKind::Direct,
      operation,
      requiredZeroMask
    };
  }

  constexpr DecodeEntry deferred(IOPOperation operation)
  {
    return {
      DecodeKind::Deferred,
      operation,
      0
    };
  }

  constexpr DecodeEntry nested(DecodeKind kind)
  {
    return {
      kind,
      IOPOperation::None,
      0
    };
  }

  const DecodeTable &primaryTable()
  {
    static const DecodeTable table = {{
      nested(DecodeKind::Special),
      nested(DecodeKind::Regimm),
      direct(IOPOperation::Jump),
      direct(IOPOperation::JumpAndLink),
      direct(IOPOperation::BranchEqual),
      direct(IOPOperation::BranchNotEqual),
      direct(
        IOPOperation::BranchLessThanOrEqualZero,
        TARGET_REGISTER_MASK),
      direct(
        IOPOperation::BranchGreaterThanZero,
        TARGET_REGISTER_MASK),
      direct(IOPOperation::AddImmediate),
      direct(IOPOperation::AddImmediateUnsigned),
      direct(IOPOperation::SetLessThanImmediate),
      direct(IOPOperation::SetLessThanImmediateUnsigned),
      direct(IOPOperation::AndImmediate),
      direct(IOPOperation::OrImmediate),
      direct(IOPOperation::XorImmediate),
      direct(
        IOPOperation::LoadUpperImmediate,
        SOURCE_REGISTER_MASK),
      nested(DecodeKind::Cop0),
      deferred(IOPOperation::CoprocessorOperation),
      deferred(IOPOperation::CoprocessorOperation),
      reserved(),
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      direct(IOPOperation::LoadByte),
      direct(IOPOperation::LoadHalfword),
      direct(IOPOperation::LoadWordLeft),
      direct(IOPOperation::LoadWord),
      direct(IOPOperation::LoadByteUnsigned),
      direct(IOPOperation::LoadHalfwordUnsigned),
      direct(IOPOperation::LoadWordRight),
      reserved(),
      direct(IOPOperation::StoreByte),
      direct(IOPOperation::StoreHalfword),
      direct(IOPOperation::StoreWordLeft),
      direct(IOPOperation::StoreWord),
      reserved(), reserved(),
      direct(IOPOperation::StoreWordRight),
      reserved(),
      reserved(),
      deferred(IOPOperation::LoadWordToCoprocessor),
      deferred(IOPOperation::LoadWordToCoprocessor),
      reserved(),
      reserved(), reserved(), reserved(), reserved(),
      reserved(),
      deferred(IOPOperation::StoreWordFromCoprocessor),
      deferred(IOPOperation::StoreWordFromCoprocessor),
      reserved(),
      reserved(), reserved(), reserved(), reserved()
    }};
    return table;
  }

  const DecodeTable &specialTable()
  {
    static const DecodeTable table = {{
      direct(
        IOPOperation::ShiftLeftLogical,
        SOURCE_REGISTER_MASK),
      reserved(),
      direct(
        IOPOperation::ShiftRightLogical,
        SOURCE_REGISTER_MASK),
      direct(
        IOPOperation::ShiftRightArithmetic,
        SOURCE_REGISTER_MASK),
      direct(
        IOPOperation::ShiftLeftLogicalVariable,
        SHIFT_AMOUNT_MASK),
      reserved(),
      direct(
        IOPOperation::ShiftRightLogicalVariable,
        SHIFT_AMOUNT_MASK),
      direct(
        IOPOperation::ShiftRightArithmeticVariable,
        SHIFT_AMOUNT_MASK),
      direct(
        IOPOperation::JumpRegister,
        TARGET_REGISTER_MASK |
        DESTINATION_REGISTER_MASK |
        SHIFT_AMOUNT_MASK),
      direct(
        IOPOperation::JumpAndLinkRegister,
        TARGET_REGISTER_MASK |
        SHIFT_AMOUNT_MASK),
      reserved(), reserved(),
      direct(IOPOperation::SystemCall),
      direct(IOPOperation::Breakpoint),
      reserved(), reserved(),
      direct(
        IOPOperation::MoveFromHI,
        SOURCE_REGISTER_MASK |
        TARGET_REGISTER_MASK |
        SHIFT_AMOUNT_MASK),
      direct(
        IOPOperation::MoveToHI,
        TARGET_REGISTER_MASK |
        DESTINATION_REGISTER_MASK |
        SHIFT_AMOUNT_MASK),
      direct(
        IOPOperation::MoveFromLO,
        SOURCE_REGISTER_MASK |
        TARGET_REGISTER_MASK |
        SHIFT_AMOUNT_MASK),
      direct(
        IOPOperation::MoveToLO,
        TARGET_REGISTER_MASK |
        DESTINATION_REGISTER_MASK |
        SHIFT_AMOUNT_MASK),
      reserved(), reserved(), reserved(), reserved(),
      direct(
        IOPOperation::Multiply,
        DESTINATION_REGISTER_MASK |
        SHIFT_AMOUNT_MASK),
      direct(
        IOPOperation::MultiplyUnsigned,
        DESTINATION_REGISTER_MASK |
        SHIFT_AMOUNT_MASK),
      direct(
        IOPOperation::Divide,
        DESTINATION_REGISTER_MASK |
        SHIFT_AMOUNT_MASK),
      direct(
        IOPOperation::DivideUnsigned,
        DESTINATION_REGISTER_MASK |
        SHIFT_AMOUNT_MASK),
      reserved(), reserved(), reserved(), reserved(),
      direct(IOPOperation::Add, SHIFT_AMOUNT_MASK),
      direct(IOPOperation::AddUnsigned, SHIFT_AMOUNT_MASK),
      direct(IOPOperation::Subtract, SHIFT_AMOUNT_MASK),
      direct(IOPOperation::SubtractUnsigned, SHIFT_AMOUNT_MASK),
      direct(IOPOperation::And, SHIFT_AMOUNT_MASK),
      direct(IOPOperation::Or, SHIFT_AMOUNT_MASK),
      direct(IOPOperation::Xor, SHIFT_AMOUNT_MASK),
      direct(IOPOperation::Nor, SHIFT_AMOUNT_MASK),
      reserved(), reserved(),
      direct(IOPOperation::SetLessThan, SHIFT_AMOUNT_MASK),
      direct(
        IOPOperation::SetLessThanUnsigned,
        SHIFT_AMOUNT_MASK),
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved()
    }};
    return table;
  }

  const SelectorTable &regimmTable()
  {
    static const SelectorTable table = {{
      direct(IOPOperation::BranchLessThanZero),
      direct(IOPOperation::BranchGreaterThanOrEqualZero),
      reserved(), reserved(), reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(),
      direct(IOPOperation::BranchLessThanZeroAndLink),
      direct(IOPOperation::BranchGreaterThanOrEqualZeroAndLink),
      reserved(), reserved(), reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved()
    }};
    return table;
  }

  const SelectorTable &cop0Table()
  {
    static const SelectorTable table = {{
      direct(IOPOperation::MoveFromCOP0, LOW_ELEVEN_MASK),
      reserved(), reserved(), reserved(),
      direct(IOPOperation::MoveToCOP0, LOW_ELEVEN_MASK),
      reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      nested(DecodeKind::Cop0Function),
      reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved()
    }};
    return table;
  }

  const DecodeTable &cop0FunctionTable()
  {
    static const DecodeTable table = {{
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      direct(
        IOPOperation::ReturnFromException,
        TARGET_REGISTER_MASK |
        DESTINATION_REGISTER_MASK |
        SHIFT_AMOUNT_MASK),
      reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved(),
      reserved(), reserved(), reserved(), reserved()
    }};
    return table;
  }

  IOPInstruction fields(IOPWord raw)
  {
    IOPInstruction instruction;
    instruction.raw = raw;
    instruction.opcode =
      static_cast<std::uint8_t>(raw >> 26);
    instruction.sourceRegister =
      static_cast<std::uint8_t>((raw >> 21) & 0x1f);
    instruction.targetRegister =
      static_cast<std::uint8_t>((raw >> 16) & 0x1f);
    instruction.destinationRegister =
      static_cast<std::uint8_t>((raw >> 11) & 0x1f);
    instruction.shiftAmount =
      static_cast<std::uint8_t>((raw >> 6) & 0x1f);
    instruction.function =
      static_cast<std::uint8_t>(raw & 0x3f);
    instruction.immediate =
      static_cast<std::uint16_t>(raw);
    instruction.target = raw & UINT32_C(0x03ffffff);
    return instruction;
  }

  IOPDecodeResult apply(
    const DecodeEntry &entry,
    IOPInstruction instruction)
  {
    if (entry.kind == DecodeKind::Reserved ||
        (instruction.raw & entry.requiredZeroMask) != 0)
    {
      return {
        IOPDecodeDisposition::Reserved,
        instruction
      };
    }

    instruction.operation = entry.operation;
    return {
      entry.kind == DecodeKind::Deferred
        ? IOPDecodeDisposition::ValidButDeferred
        : IOPDecodeDisposition::Supported,
      instruction
    };
  }
}

IOPDecodeResult decodeIOPInstruction(IOPWord raw)
{
  IOPInstruction instruction = fields(raw);
  const DecodeEntry &primary =
    primaryTable()[instruction.opcode];
  switch (primary.kind)
  {
    case DecodeKind::Special:
    {
      IOPDecodeResult result =
        apply(specialTable()[instruction.function], instruction);
      if (result.disposition == IOPDecodeDisposition::Supported &&
          raw == 0)
      {
        result.instruction.operation = IOPOperation::Nop;
      }
      return result;
    }
    case DecodeKind::Regimm:
      return apply(
        regimmTable()[instruction.targetRegister],
        instruction);
    case DecodeKind::Cop0:
    {
      const DecodeEntry &cop0 =
        cop0Table()[instruction.sourceRegister];
      if (cop0.kind == DecodeKind::Cop0Function)
      {
        return apply(
          cop0FunctionTable()[instruction.function],
          instruction);
      }
      return apply(cop0, instruction);
    }
    case DecodeKind::Reserved:
    case DecodeKind::Direct:
    case DecodeKind::Deferred:
      return apply(primary, instruction);
    case DecodeKind::Cop0Function:
      break;
  }
  return {
    IOPDecodeDisposition::Reserved,
    instruction
  };
}
