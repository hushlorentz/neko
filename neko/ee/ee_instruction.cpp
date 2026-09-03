#include "ee_instruction.hpp"

#include <array>
#include <stdexcept>

namespace
{
  constexpr std::uint32_t REGISTER_SOURCE_MASK =
    UINT32_C(0x03e00000);
  constexpr std::uint32_t REGISTER_SHIFT_MASK =
    UINT32_C(0x000007c0);
  constexpr std::uint32_t REGISTER_TARGET_MASK =
    UINT32_C(0x001f0000);
  constexpr std::uint32_t REGISTER_DESTINATION_MASK =
    UINT32_C(0x0000f800);

  enum class DecodeKind : std::uint8_t
  {
    Reserved,
    Unsupported,
    Direct,
    Special,
    Regimm,
    Mmi,
    Cop0,
    Cop2
  };

  struct DecodeEntry
  {
    DecodeKind kind = DecodeKind::Unsupported;
    EEOperation operation = EEOperation::Nop;
    std::uint32_t requiredZeroMask = 0;
  };

  using DecodeTable = std::array<DecodeEntry, 64>;

  void direct(
    DecodeTable *table,
    std::uint8_t encoding,
    EEOperation operation,
    std::uint32_t requiredZeroMask = 0)
  {
    (*table)[encoding] = {
      DecodeKind::Direct,
      operation,
      requiredZeroMask
    };
  }

  DecodeTable makePrimaryTable()
  {
    DecodeTable table = {};
    table.fill({
      DecodeKind::Unsupported,
      EEOperation::Nop,
      0
    });
    table[0x00].kind = DecodeKind::Special;
    table[0x01].kind = DecodeKind::Regimm;
    table[0x10].kind = DecodeKind::Cop0;
    table[0x12].kind = DecodeKind::Cop2;
    table[0x1c].kind = DecodeKind::Mmi;
    table[0x13].kind = DecodeKind::Reserved;
    table[0x1d].kind = DecodeKind::Reserved;
    table[0x3b].kind = DecodeKind::Reserved;

    direct(&table, 0x02, EEOperation::Jump);
    direct(&table, 0x03, EEOperation::JumpAndLink);
    direct(&table, 0x04, EEOperation::BranchEqual);
    direct(&table, 0x05, EEOperation::BranchNotEqual);
    direct(
      &table,
      0x06,
      EEOperation::BranchLessThanOrEqualZero,
      REGISTER_TARGET_MASK);
    direct(
      &table,
      0x07,
      EEOperation::BranchGreaterThanZero,
      REGISTER_TARGET_MASK);
    direct(&table, 0x08, EEOperation::AddImmediateWord);
    direct(
      &table,
      0x09,
      EEOperation::AddImmediateUnsignedWord);
    direct(
      &table,
      0x0a,
      EEOperation::SetLessThanImmediate);
    direct(
      &table,
      0x0b,
      EEOperation::SetLessThanImmediateUnsigned);
    direct(&table, 0x0c, EEOperation::AndImmediate);
    direct(&table, 0x0d, EEOperation::OrImmediate);
    direct(&table, 0x0e, EEOperation::XorImmediate);
    direct(
      &table,
      0x0f,
      EEOperation::LoadUpperImmediate,
      REGISTER_SOURCE_MASK);
    direct(
      &table,
      0x18,
      EEOperation::AddImmediateDoubleword);
    direct(
      &table,
      0x19,
      EEOperation::AddImmediateUnsignedDoubleword);
    direct(&table, 0x1a, EEOperation::LoadDoublewordLeft);
    direct(&table, 0x1b, EEOperation::LoadDoublewordRight);
    direct(&table, 0x1e, EEOperation::LoadQuadword);
    direct(&table, 0x1f, EEOperation::StoreQuadword);
    direct(&table, 0x20, EEOperation::LoadByte);
    direct(&table, 0x21, EEOperation::LoadHalfword);
    direct(&table, 0x22, EEOperation::LoadWordLeft);
    direct(&table, 0x23, EEOperation::LoadWord);
    direct(&table, 0x24, EEOperation::LoadByteUnsigned);
    direct(&table, 0x25, EEOperation::LoadHalfwordUnsigned);
    direct(&table, 0x26, EEOperation::LoadWordRight);
    direct(&table, 0x27, EEOperation::LoadWordUnsigned);
    direct(&table, 0x28, EEOperation::StoreByte);
    direct(&table, 0x29, EEOperation::StoreHalfword);
    direct(&table, 0x2a, EEOperation::StoreWordLeft);
    direct(&table, 0x2b, EEOperation::StoreWord);
    direct(&table, 0x2c, EEOperation::StoreDoublewordLeft);
    direct(&table, 0x2d, EEOperation::StoreDoublewordRight);
    direct(&table, 0x2e, EEOperation::StoreWordRight);
    direct(&table, 0x37, EEOperation::LoadDoubleword);
    direct(&table, 0x36, EEOperation::LoadQuadwordToCOP2);
    direct(&table, 0x3e, EEOperation::StoreQuadwordFromCOP2);
    direct(&table, 0x3f, EEOperation::StoreDoubleword);
    direct(&table, 0x14, EEOperation::BranchEqualLikely);
    direct(&table, 0x15, EEOperation::BranchNotEqualLikely);
    direct(
      &table,
      0x16,
      EEOperation::BranchLessThanOrEqualZeroLikely,
      REGISTER_TARGET_MASK);
    direct(
      &table,
      0x17,
      EEOperation::BranchGreaterThanZeroLikely,
      REGISTER_TARGET_MASK);
    return table;
  }

  DecodeTable makeSpecialTable()
  {
    DecodeTable table = {};
    table.fill({
      DecodeKind::Unsupported,
      EEOperation::Nop,
      0
    });
    const std::uint8_t reservedFunctions[] = {
      0x01, 0x05, 0x0e, 0x15, 0x35, 0x37, 0x39, 0x3d
    };
    for (std::uint8_t function : reservedFunctions)
    {
      table[function].kind = DecodeKind::Reserved;
    }

    direct(
      &table,
      0x00,
      EEOperation::ShiftLeftLogicalWord,
      REGISTER_SOURCE_MASK);
    direct(
      &table,
      0x02,
      EEOperation::ShiftRightLogicalWord,
      REGISTER_SOURCE_MASK);
    direct(
      &table,
      0x03,
      EEOperation::ShiftRightArithmeticWord,
      REGISTER_SOURCE_MASK);
    direct(
      &table,
      0x04,
      EEOperation::ShiftLeftLogicalVariableWord,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x06,
      EEOperation::ShiftRightLogicalVariableWord,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x07,
      EEOperation::ShiftRightArithmeticVariableWord,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x08,
      EEOperation::JumpRegister,
      REGISTER_TARGET_MASK |
        REGISTER_DESTINATION_MASK |
        REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x09,
      EEOperation::JumpAndLinkRegister,
      REGISTER_TARGET_MASK |
        REGISTER_SHIFT_MASK);
    direct(&table, 0x0c, EEOperation::SystemCall);
    direct(&table, 0x0d, EEOperation::Breakpoint);
    direct(
      &table,
      0x14,
      EEOperation::ShiftLeftLogicalVariableDoubleword,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x16,
      EEOperation::ShiftRightLogicalVariableDoubleword,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x17,
      EEOperation::ShiftRightArithmeticVariableDoubleword,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x10,
      EEOperation::MoveFromHI,
      REGISTER_SOURCE_MASK |
        REGISTER_TARGET_MASK |
        REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x11,
      EEOperation::MoveToHI,
      REGISTER_TARGET_MASK |
        REGISTER_DESTINATION_MASK |
        REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x12,
      EEOperation::MoveFromLO,
      REGISTER_SOURCE_MASK |
        REGISTER_TARGET_MASK |
        REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x13,
      EEOperation::MoveToLO,
      REGISTER_TARGET_MASK |
        REGISTER_DESTINATION_MASK |
        REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x18,
      EEOperation::MultiplyWord,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x19,
      EEOperation::MultiplyUnsignedWord,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x1a,
      EEOperation::DivideWord,
      REGISTER_DESTINATION_MASK |
        REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x1b,
      EEOperation::DivideUnsignedWord,
      REGISTER_DESTINATION_MASK |
        REGISTER_SHIFT_MASK);

    direct(
      &table,
      0x20,
      EEOperation::AddWord,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x21,
      EEOperation::AddUnsignedWord,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x22,
      EEOperation::SubtractWord,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x23,
      EEOperation::SubtractUnsignedWord,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x24,
      EEOperation::And,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x25,
      EEOperation::Or,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x26,
      EEOperation::Xor,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x27,
      EEOperation::Nor,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x2a,
      EEOperation::SetLessThan,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x2b,
      EEOperation::SetLessThanUnsigned,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x2c,
      EEOperation::AddDoubleword,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x2d,
      EEOperation::AddUnsignedDoubleword,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x2e,
      EEOperation::SubtractDoubleword,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x2f,
      EEOperation::SubtractUnsignedDoubleword,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x28,
      EEOperation::MoveFromShiftAmount,
      REGISTER_SOURCE_MASK |
        REGISTER_TARGET_MASK |
        REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x29,
      EEOperation::MoveToShiftAmount,
      REGISTER_TARGET_MASK |
        REGISTER_DESTINATION_MASK |
        REGISTER_SHIFT_MASK);

    direct(
      &table,
      0x38,
      EEOperation::ShiftLeftLogicalDoubleword,
      REGISTER_SOURCE_MASK);
    direct(
      &table,
      0x3a,
      EEOperation::ShiftRightLogicalDoubleword,
      REGISTER_SOURCE_MASK);
    direct(
      &table,
      0x3b,
      EEOperation::ShiftRightArithmeticDoubleword,
      REGISTER_SOURCE_MASK);
    direct(
      &table,
      0x3c,
      EEOperation::ShiftLeftLogicalDoubleword32,
      REGISTER_SOURCE_MASK);
    direct(
      &table,
      0x3e,
      EEOperation::ShiftRightLogicalDoubleword32,
      REGISTER_SOURCE_MASK);
    direct(
      &table,
      0x3f,
      EEOperation::ShiftRightArithmeticDoubleword32,
      REGISTER_SOURCE_MASK);
    return table;
  }

  DecodeTable makeRegimmTable()
  {
    DecodeTable table = {};
    table.fill({
      DecodeKind::Unsupported,
      EEOperation::Nop,
      0
    });
    direct(&table, 0x00, EEOperation::BranchLessThanZero);
    direct(
      &table,
      0x01,
      EEOperation::BranchGreaterThanOrEqualZero);
    direct(
      &table,
      0x02,
      EEOperation::BranchLessThanZeroLikely);
    direct(
      &table,
      0x03,
      EEOperation::BranchGreaterThanOrEqualZeroLikely);
    direct(
      &table,
      0x10,
      EEOperation::BranchLessThanZeroAndLink);
    direct(
      &table,
      0x11,
      EEOperation::BranchGreaterThanOrEqualZeroAndLink);
    direct(
      &table,
      0x12,
      EEOperation::BranchLessThanZeroAndLinkLikely);
    direct(
      &table,
      0x13,
      EEOperation::BranchGreaterThanOrEqualZeroAndLinkLikely);
    direct(
      &table,
      0x18,
      EEOperation::MoveByteCountToShiftAmount);
    direct(
      &table,
      0x19,
      EEOperation::MoveHalfwordCountToShiftAmount);
    return table;
  }

  DecodeTable makeMmiTable()
  {
    DecodeTable table = {};
    table.fill({
      DecodeKind::Unsupported,
      EEOperation::Nop,
      0
    });
    direct(
      &table,
      0x00,
      EEOperation::MultiplyAddWord,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x01,
      EEOperation::MultiplyAddUnsignedWord,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x10,
      EEOperation::MoveFromHI1,
      REGISTER_SOURCE_MASK |
        REGISTER_TARGET_MASK |
        REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x11,
      EEOperation::MoveToHI1,
      REGISTER_TARGET_MASK |
        REGISTER_DESTINATION_MASK |
        REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x12,
      EEOperation::MoveFromLO1,
      REGISTER_SOURCE_MASK |
        REGISTER_TARGET_MASK |
        REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x13,
      EEOperation::MoveToLO1,
      REGISTER_TARGET_MASK |
        REGISTER_DESTINATION_MASK |
        REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x18,
      EEOperation::MultiplyWord1,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x19,
      EEOperation::MultiplyUnsignedWord1,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x1a,
      EEOperation::DivideWord1,
      REGISTER_DESTINATION_MASK |
        REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x1b,
      EEOperation::DivideUnsignedWord1,
      REGISTER_DESTINATION_MASK |
        REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x20,
      EEOperation::MultiplyAddWord1,
      REGISTER_SHIFT_MASK);
    direct(
      &table,
      0x21,
      EEOperation::MultiplyAddUnsignedWord1,
      REGISTER_SHIFT_MASK);
    return table;
  }

  const DecodeTable &primaryTable()
  {
    static const DecodeTable table = makePrimaryTable();
    return table;
  }

  const DecodeTable &specialTable()
  {
    static const DecodeTable table = makeSpecialTable();
    return table;
  }

  const DecodeTable &regimmTable()
  {
    static const DecodeTable table = makeRegimmTable();
    return table;
  }

  const DecodeTable &mmiTable()
  {
    static const DecodeTable table = makeMmiTable();
    return table;
  }

  [[noreturn]] void reject(DecodeKind kind)
  {
    if (kind == DecodeKind::Reserved)
    {
      throw EEInstructionDecodeError(
        EEInstructionDecodeFailure::Reserved,
        "Reserved EE instruction encoding.");
    }
    throw EEInstructionDecodeError(
      EEInstructionDecodeFailure::Unsupported,
      "Unsupported EE instruction encoding.");
  }

  EEInstruction fields(std::uint32_t raw)
  {
    EEInstruction instruction;
    instruction.raw = raw;
    instruction.opcode = (raw >> 26) & 0x3f;
    instruction.sourceRegister = (raw >> 21) & 0x1f;
    instruction.targetRegister = (raw >> 16) & 0x1f;
    instruction.destinationRegister = (raw >> 11) & 0x1f;
    instruction.shiftAmount = (raw >> 6) & 0x1f;
    instruction.function = raw & 0x3f;
    instruction.immediate = raw & 0xffff;
    instruction.target = raw & 0x03ffffff;
    instruction.cop2Immediate = (raw >> 6) & 0x7fff;
    return instruction;
  }

  void applyEntry(
    const DecodeEntry &entry,
    EEInstruction *instruction)
  {
    if (entry.kind != DecodeKind::Direct)
    {
      reject(entry.kind);
    }
    if ((instruction->raw & entry.requiredZeroMask) != 0)
    {
      reject(DecodeKind::Reserved);
    }
    instruction->operation = entry.operation;
  }

  void applyCop0(EEInstruction *instruction)
  {
    if (instruction->raw == UINT32_C(0x42000018))
    {
      instruction->operation = EEOperation::ExceptionReturn;
      return;
    }

    if (instruction->sourceRegister == 0x00 ||
        instruction->sourceRegister == 0x04)
    {
      reject(
        (instruction->raw & UINT32_C(0x000007ff)) == 0
          ? DecodeKind::Unsupported
          : DecodeKind::Reserved);
    }
    if (instruction->sourceRegister == 0x08)
    {
      reject(
        instruction->targetRegister <= 0x03
          ? DecodeKind::Unsupported
          : DecodeKind::Reserved);
    }
    if (instruction->sourceRegister != 0x10)
    {
      reject(DecodeKind::Reserved);
    }

    const bool fixedFieldsAreZero =
      (instruction->raw & UINT32_C(0x001fffc0)) == 0;
    const bool deferredOperation =
      instruction->function == 0x01 ||
      instruction->function == 0x02 ||
      instruction->function == 0x06 ||
      instruction->function == 0x08 ||
      instruction->function == 0x38 ||
      instruction->function == 0x39;
    reject(
      fixedFieldsAreZero && deferredOperation
        ? DecodeKind::Unsupported
        : DecodeKind::Reserved);
  }

  void applyCop2(EEInstruction *instruction)
  {
    if ((instruction->raw & UINT32_C(0xffe0003f)) ==
        UINT32_C(0x4a000038))
    {
      instruction->operation =
        EEOperation::VectorCallMicroSubroutine;
      return;
    }
    if (instruction->raw == UINT32_C(0x4a00d839))
    {
      instruction->operation =
        EEOperation::VectorCallMicroSubroutineRegister;
      return;
    }
    if ((instruction->raw & UINT32_C(0xfe000000)) ==
        UINT32_C(0x4a000000))
    {
      const bool fullVector =
        instruction->function == 0x28 ||
        instruction->function == 0x2c;
      const bool scalar =
        instruction->function == 0x20 ||
        instruction->function == 0x22 ||
        instruction->function == 0x24 ||
        instruction->function == 0x26;
      const bool broadcast =
        instruction->function <= 0x07;
      if (fullVector || scalar || broadcast)
      {
        if (scalar && instruction->targetRegister != 0)
        {
          reject(DecodeKind::Reserved);
        }
        instruction->operation =
          EEOperation::VectorMacroArithmetic;
        return;
      }
    }
    if (instruction->sourceRegister >= 0x10 &&
        (instruction->function == 0x38 ||
         instruction->function == 0x39))
    {
      reject(DecodeKind::Reserved);
    }
    if (instruction->sourceRegister == 0x01 ||
        instruction->sourceRegister == 0x02 ||
        instruction->sourceRegister == 0x05 ||
        instruction->sourceRegister == 0x06)
    {
      if ((instruction->raw & UINT32_C(0x000007fe)) != 0)
      {
        reject(DecodeKind::Reserved);
      }
      switch (instruction->sourceRegister)
      {
        case 0x01:
          instruction->operation =
            EEOperation::QuadwordMoveFromCOP2;
          break;
        case 0x02:
          instruction->operation =
            EEOperation::ControlMoveFromCOP2;
          break;
        case 0x05:
          instruction->operation =
            EEOperation::QuadwordMoveToCOP2;
          break;
        default:
          instruction->operation =
            EEOperation::ControlMoveToCOP2;
          break;
      }
      return;
    }
    if (instruction->sourceRegister == 0x08)
    {
      switch (instruction->targetRegister)
      {
        case 0x00:
          instruction->operation = EEOperation::BranchCOP2False;
          return;
        case 0x01:
          instruction->operation = EEOperation::BranchCOP2True;
          return;
        case 0x02:
          instruction->operation =
            EEOperation::BranchCOP2FalseLikely;
          return;
        case 0x03:
          instruction->operation =
            EEOperation::BranchCOP2TrueLikely;
          return;
        default:
          reject(DecodeKind::Reserved);
      }
    }
    reject(DecodeKind::Unsupported);
  }
}

EEInstructionDecodeError::EEInstructionDecodeError(
  EEInstructionDecodeFailure failure,
  const char *message) :
  std::runtime_error(message),
  failureType(failure)
{
}

EEInstructionDecodeFailure
EEInstructionDecodeError::failure() const
{
  return failureType;
}

bool isEEBranchOperation(EEOperation operation)
{
  switch (operation)
  {
    case EEOperation::Jump:
    case EEOperation::JumpAndLink:
    case EEOperation::JumpRegister:
    case EEOperation::JumpAndLinkRegister:
    case EEOperation::BranchEqual:
    case EEOperation::BranchNotEqual:
    case EEOperation::BranchLessThanOrEqualZero:
    case EEOperation::BranchGreaterThanZero:
    case EEOperation::BranchLessThanZero:
    case EEOperation::BranchGreaterThanOrEqualZero:
    case EEOperation::BranchEqualLikely:
    case EEOperation::BranchNotEqualLikely:
    case EEOperation::BranchLessThanOrEqualZeroLikely:
    case EEOperation::BranchGreaterThanZeroLikely:
    case EEOperation::BranchLessThanZeroLikely:
    case EEOperation::BranchGreaterThanOrEqualZeroLikely:
    case EEOperation::BranchLessThanZeroAndLink:
    case EEOperation::BranchGreaterThanOrEqualZeroAndLink:
    case EEOperation::BranchLessThanZeroAndLinkLikely:
    case EEOperation::BranchGreaterThanOrEqualZeroAndLinkLikely:
    case EEOperation::BranchCOP2False:
    case EEOperation::BranchCOP2FalseLikely:
    case EEOperation::BranchCOP2True:
    case EEOperation::BranchCOP2TrueLikely:
      return true;
    default:
      return false;
  }
}

bool isEEBranchLikelyOperation(EEOperation operation)
{
  return
    operation == EEOperation::BranchEqualLikely ||
    operation == EEOperation::BranchNotEqualLikely ||
    operation == EEOperation::BranchLessThanOrEqualZeroLikely ||
    operation == EEOperation::BranchGreaterThanZeroLikely ||
    operation == EEOperation::BranchLessThanZeroLikely ||
    operation == EEOperation::BranchGreaterThanOrEqualZeroLikely ||
    operation == EEOperation::BranchLessThanZeroAndLinkLikely ||
    operation == EEOperation::BranchGreaterThanOrEqualZeroAndLinkLikely ||
    operation == EEOperation::BranchCOP2FalseLikely ||
    operation == EEOperation::BranchCOP2TrueLikely;
}

EEInstruction decodeEEInstruction(std::uint32_t raw)
{
  EEInstruction instruction = fields(raw);
  const DecodeEntry &primary =
    primaryTable()[instruction.opcode];
  if (primary.kind == DecodeKind::Special)
  {
    const DecodeEntry &special =
      specialTable()[instruction.function];
    applyEntry(special, &instruction);
    if (raw == 0)
    {
      instruction.operation = EEOperation::Nop;
    }
    return instruction;
  }
  if (primary.kind == DecodeKind::Regimm)
  {
    applyEntry(
      regimmTable()[instruction.targetRegister],
      &instruction);
    return instruction;
  }
  if (primary.kind == DecodeKind::Mmi)
  {
    applyEntry(
      mmiTable()[instruction.function],
      &instruction);
    return instruction;
  }
  if (primary.kind == DecodeKind::Cop0)
  {
    applyCop0(&instruction);
    return instruction;
  }
  if (primary.kind == DecodeKind::Cop2)
  {
    applyCop2(&instruction);
    return instruction;
  }

  applyEntry(primary, &instruction);
  return instruction;
}
