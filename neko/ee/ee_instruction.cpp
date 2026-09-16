#include "ee_instruction.hpp"

#include <array>
#include <stdexcept>

#include "vpu_instruction.hpp"

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
    Cop1,
    Cop2
  };

  struct DecodeEntry
  {
    DecodeKind kind = DecodeKind::Unsupported;
    EEOperation operation = EEOperation::Nop;
    std::uint32_t requiredZeroMask = 0;
  };

  using DecodeTable = std::array<DecodeEntry, 64>;

  bool updatesCOP1ArithmeticFlags(EEOperation operation);

  std::uint32_t registerMask(std::uint8_t index)
  {
    return index == 0 ? 0 : UINT32_C(1) << index;
  }

  std::uint32_t coprocessorRegisterMask(std::uint8_t index)
  {
    return UINT32_C(1) << index;
  }

  EEInstructionDependencies buildInstructionDependencies(
    const EEInstruction &instruction)
  {
    EEInstructionDependencies dependencies;
    const std::uint32_t source =
      registerMask(instruction.sourceRegister);
    const std::uint32_t target =
      registerMask(instruction.targetRegister);
    const std::uint32_t destination =
      registerMask(instruction.destinationRegister);
    const std::uint32_t coprocessorTarget =
      coprocessorRegisterMask(instruction.targetRegister);
    const std::uint32_t coprocessorDestination =
      coprocessorRegisterMask(
        instruction.destinationRegister);
    const std::uint32_t coprocessorResult =
      coprocessorRegisterMask(instruction.shiftAmount);

    switch (instruction.operation)
    {
      case EEOperation::Nop:
      case EEOperation::SynchronizeLoadStore:
      case EEOperation::SynchronizePipeline:
      case EEOperation::SystemCall:
      case EEOperation::Breakpoint:
      case EEOperation::ExceptionReturn:
      case EEOperation::Jump:
        break;
      case EEOperation::ShiftLeftLogicalWord:
      case EEOperation::ShiftRightLogicalWord:
      case EEOperation::ShiftRightArithmeticWord:
      case EEOperation::ShiftLeftLogicalDoubleword:
      case EEOperation::ShiftRightLogicalDoubleword:
      case EEOperation::ShiftRightArithmeticDoubleword:
      case EEOperation::ShiftLeftLogicalDoubleword32:
      case EEOperation::ShiftRightLogicalDoubleword32:
      case EEOperation::ShiftRightArithmeticDoubleword32:
        dependencies.gprReads = target;
        dependencies.gprWrites = destination;
        break;
      case EEOperation::ShiftLeftLogicalVariableWord:
      case EEOperation::ShiftRightLogicalVariableWord:
      case EEOperation::ShiftRightArithmeticVariableWord:
      case EEOperation::ShiftLeftLogicalVariableDoubleword:
      case EEOperation::ShiftRightLogicalVariableDoubleword:
      case EEOperation::ShiftRightArithmeticVariableDoubleword:
      case EEOperation::AddWord:
      case EEOperation::AddUnsignedWord:
      case EEOperation::SubtractWord:
      case EEOperation::SubtractUnsignedWord:
      case EEOperation::And:
      case EEOperation::Or:
      case EEOperation::Xor:
      case EEOperation::Nor:
      case EEOperation::SetLessThan:
      case EEOperation::SetLessThanUnsigned:
      case EEOperation::AddDoubleword:
      case EEOperation::AddUnsignedDoubleword:
      case EEOperation::SubtractDoubleword:
      case EEOperation::SubtractUnsignedDoubleword:
        dependencies.gprReads = source | target;
        dependencies.gprWrites = destination;
        break;
      case EEOperation::AddImmediateWord:
      case EEOperation::AddImmediateUnsignedWord:
      case EEOperation::SetLessThanImmediate:
      case EEOperation::SetLessThanImmediateUnsigned:
      case EEOperation::AndImmediate:
      case EEOperation::OrImmediate:
      case EEOperation::XorImmediate:
      case EEOperation::AddImmediateDoubleword:
      case EEOperation::AddImmediateUnsignedDoubleword:
        dependencies.gprReads = source;
        dependencies.gprWrites = target;
        break;
      case EEOperation::LoadUpperImmediate:
        dependencies.gprWrites = target;
        break;
      case EEOperation::MoveFromHI:
        dependencies.gprWrites = destination;
        dependencies.specialReads = RESOURCE_HI;
        break;
      case EEOperation::MoveToHI:
        dependencies.gprReads = source;
        dependencies.specialWrites = RESOURCE_HI;
        break;
      case EEOperation::MoveFromLO:
        dependencies.gprWrites = destination;
        dependencies.specialReads = RESOURCE_HI | RESOURCE_LO;
        break;
      case EEOperation::MoveToLO:
        dependencies.gprReads = source;
        dependencies.specialWrites = RESOURCE_LO;
        break;
      case EEOperation::MultiplyWord:
      case EEOperation::MultiplyUnsignedWord:
        dependencies.gprReads = source | target;
        dependencies.gprWrites = destination;
        dependencies.specialWrites = RESOURCE_HI | RESOURCE_LO;
        break;
      case EEOperation::DivideWord:
      case EEOperation::DivideUnsignedWord:
        dependencies.gprReads = source | target;
        dependencies.specialWrites = RESOURCE_HI | RESOURCE_LO;
        break;
      case EEOperation::MultiplyAddWord:
      case EEOperation::MultiplyAddUnsignedWord:
        dependencies.gprReads = source | target;
        dependencies.gprWrites = destination;
        dependencies.specialReads = RESOURCE_LO;
        dependencies.specialWrites = RESOURCE_HI | RESOURCE_LO;
        break;
      case EEOperation::MoveFromHI1:
        dependencies.gprWrites = destination;
        dependencies.specialReads = RESOURCE_HI1;
        break;
      case EEOperation::MoveToHI1:
        dependencies.gprReads = source;
        dependencies.specialWrites = RESOURCE_HI1;
        break;
      case EEOperation::MoveFromLO1:
        dependencies.gprWrites = destination;
        dependencies.specialReads = RESOURCE_HI1 | RESOURCE_LO1;
        break;
      case EEOperation::MoveToLO1:
        dependencies.gprReads = source;
        dependencies.specialWrites = RESOURCE_LO1;
        break;
      case EEOperation::MultiplyWord1:
      case EEOperation::MultiplyUnsignedWord1:
        dependencies.gprReads = source | target;
        dependencies.gprWrites = destination;
        dependencies.specialWrites =
          RESOURCE_HI1 | RESOURCE_LO1;
        break;
      case EEOperation::DivideWord1:
      case EEOperation::DivideUnsignedWord1:
        dependencies.gprReads = source | target;
        dependencies.specialWrites =
          RESOURCE_HI1 | RESOURCE_LO1;
        break;
      case EEOperation::MultiplyAddWord1:
      case EEOperation::MultiplyAddUnsignedWord1:
        dependencies.gprReads = source | target;
        dependencies.gprWrites = destination;
        dependencies.specialReads = RESOURCE_LO1;
        dependencies.specialWrites =
          RESOURCE_HI1 | RESOURCE_LO1;
        break;
      case EEOperation::MoveFromShiftAmount:
        dependencies.gprWrites = destination;
        dependencies.specialReads = RESOURCE_SA;
        break;
      case EEOperation::MoveToShiftAmount:
      case EEOperation::MoveByteCountToShiftAmount:
      case EEOperation::MoveHalfwordCountToShiftAmount:
        dependencies.gprReads = source;
        dependencies.specialWrites = RESOURCE_SA;
        break;
      case EEOperation::JumpAndLink:
        dependencies.gprWrites = registerMask(31);
        break;
      case EEOperation::JumpRegister:
        dependencies.gprReads = source;
        break;
      case EEOperation::JumpAndLinkRegister:
        dependencies.gprReads = source;
        dependencies.gprWrites = destination;
        break;
      case EEOperation::BranchEqual:
      case EEOperation::BranchNotEqual:
      case EEOperation::BranchEqualLikely:
      case EEOperation::BranchNotEqualLikely:
        dependencies.gprReads = source | target;
        break;
      case EEOperation::BranchLessThanOrEqualZero:
      case EEOperation::BranchGreaterThanZero:
      case EEOperation::BranchLessThanZero:
      case EEOperation::BranchGreaterThanOrEqualZero:
      case EEOperation::BranchLessThanOrEqualZeroLikely:
      case EEOperation::BranchGreaterThanZeroLikely:
      case EEOperation::BranchLessThanZeroLikely:
      case EEOperation::BranchGreaterThanOrEqualZeroLikely:
        dependencies.gprReads = source;
        break;
      case EEOperation::BranchLessThanZeroAndLink:
      case EEOperation::BranchGreaterThanOrEqualZeroAndLink:
      case EEOperation::BranchLessThanZeroAndLinkLikely:
      case EEOperation::BranchGreaterThanOrEqualZeroAndLinkLikely:
        dependencies.gprReads = source;
        dependencies.gprWrites = registerMask(31);
        break;
      case EEOperation::LoadByte:
      case EEOperation::LoadByteUnsigned:
      case EEOperation::LoadHalfword:
      case EEOperation::LoadHalfwordUnsigned:
      case EEOperation::LoadWord:
      case EEOperation::LoadWordUnsigned:
      case EEOperation::LoadWordLeft:
      case EEOperation::LoadWordRight:
      case EEOperation::LoadDoubleword:
      case EEOperation::LoadDoublewordLeft:
      case EEOperation::LoadDoublewordRight:
      case EEOperation::LoadQuadword:
        dependencies.gprReads = source;
        dependencies.gprWrites = target;
        break;
      case EEOperation::StoreByte:
      case EEOperation::StoreHalfword:
      case EEOperation::StoreWord:
      case EEOperation::StoreWordLeft:
      case EEOperation::StoreWordRight:
      case EEOperation::StoreDoubleword:
      case EEOperation::StoreDoublewordLeft:
      case EEOperation::StoreDoublewordRight:
      case EEOperation::StoreQuadword:
        dependencies.gprReads = source | target;
        break;
      case EEOperation::MoveWordFromCOP1:
        dependencies.gprWrites = target;
        dependencies.fprReads = coprocessorDestination;
        break;
      case EEOperation::MoveWordToCOP1:
        dependencies.gprReads = target;
        dependencies.fprWrites = coprocessorDestination;
        break;
      case EEOperation::MoveControlWordFromCOP1:
        dependencies.gprWrites = target;
        if (instruction.destinationRegister == 31)
        {
          dependencies.specialReads = RESOURCE_COP1_FCR31;
        }
        break;
      case EEOperation::MoveControlWordToCOP1:
        dependencies.gprReads = target;
        if (instruction.destinationRegister == 31)
        {
          dependencies.specialWrites = RESOURCE_COP1_FCR31;
        }
        break;
      case EEOperation::LoadWordToCOP1:
        dependencies.gprReads = source;
        dependencies.fprWrites = coprocessorTarget;
        break;
      case EEOperation::StoreWordFromCOP1:
        dependencies.gprReads = source;
        dependencies.fprReads = coprocessorTarget;
        break;
      case EEOperation::MoveSingleCOP1:
      case EEOperation::AbsoluteSingleCOP1:
      case EEOperation::NegateSingleCOP1:
      case EEOperation::ConvertSingleToWordCOP1:
        dependencies.fprReads = coprocessorDestination;
        dependencies.fprWrites = coprocessorResult;
        break;
      case EEOperation::ConvertWordToSingleCOP1:
        dependencies.fprReads = coprocessorDestination;
        dependencies.fprWrites = coprocessorResult;
        break;
      case EEOperation::AddSingleCOP1:
      case EEOperation::SubtractSingleCOP1:
      case EEOperation::MultiplySingleCOP1:
      case EEOperation::DivideSingleCOP1:
      case EEOperation::ReciprocalSquareRootSingleCOP1:
      case EEOperation::MaximumSingleCOP1:
      case EEOperation::MinimumSingleCOP1:
        dependencies.fprReads =
          coprocessorDestination | coprocessorTarget;
        dependencies.fprWrites = coprocessorResult;
        break;
      case EEOperation::SquareRootSingleCOP1:
        dependencies.fprReads = coprocessorTarget;
        dependencies.fprWrites = coprocessorResult;
        break;
      case EEOperation::AddSingleToAccumulatorCOP1:
      case EEOperation::SubtractSingleToAccumulatorCOP1:
      case EEOperation::MultiplySingleToAccumulatorCOP1:
        dependencies.fprReads =
          coprocessorDestination | coprocessorTarget;
        dependencies.specialWrites =
          RESOURCE_COP1_ACCUMULATOR |
          RESOURCE_COP1_FCR31;
        dependencies.specialReads = RESOURCE_COP1_FCR31;
        break;
      case EEOperation::MultiplyAddSingleCOP1:
      case EEOperation::MultiplySubtractSingleCOP1:
        dependencies.fprReads =
          coprocessorDestination | coprocessorTarget;
        dependencies.fprWrites = coprocessorResult;
        dependencies.specialReads =
          RESOURCE_COP1_ACCUMULATOR |
          RESOURCE_COP1_FCR31;
        dependencies.specialWrites = RESOURCE_COP1_FCR31;
        break;
      case EEOperation::MultiplyAddSingleToAccumulatorCOP1:
      case EEOperation::MultiplySubtractSingleToAccumulatorCOP1:
        dependencies.fprReads =
          coprocessorDestination | coprocessorTarget;
        dependencies.specialReads =
          RESOURCE_COP1_ACCUMULATOR |
          RESOURCE_COP1_FCR31;
        dependencies.specialWrites =
          RESOURCE_COP1_ACCUMULATOR |
          RESOURCE_COP1_FCR31;
        break;
      case EEOperation::CompareFalseSingleCOP1:
      case EEOperation::CompareEqualSingleCOP1:
      case EEOperation::CompareLessThanSingleCOP1:
      case EEOperation::CompareLessThanOrEqualSingleCOP1:
        dependencies.fprReads =
          coprocessorDestination | coprocessorTarget;
        dependencies.specialWrites = RESOURCE_COP1_FCR31;
        break;
      case EEOperation::BranchCOP1False:
      case EEOperation::BranchCOP1FalseLikely:
      case EEOperation::BranchCOP1True:
      case EEOperation::BranchCOP1TrueLikely:
        dependencies.specialReads = RESOURCE_COP1_FCR31;
        break;
      case EEOperation::BranchCOP2False:
      case EEOperation::BranchCOP2FalseLikely:
      case EEOperation::BranchCOP2True:
      case EEOperation::BranchCOP2TrueLikely:
        dependencies.specialReads = RESOURCE_COP2_STATE;
        break;
      case EEOperation::LoadQuadwordToCOP2:
        dependencies.gprReads = source;
        dependencies.cop2Writes = coprocessorTarget;
        dependencies.specialWrites = RESOURCE_COP2_STATE;
        break;
      case EEOperation::StoreQuadwordFromCOP2:
        dependencies.gprReads = source;
        dependencies.cop2Reads = coprocessorTarget;
        dependencies.specialReads = RESOURCE_COP2_STATE;
        break;
      case EEOperation::QuadwordMoveFromCOP2:
        dependencies.gprWrites = target;
        dependencies.cop2Reads = coprocessorDestination;
        dependencies.specialReads = RESOURCE_COP2_STATE;
        break;
      case EEOperation::QuadwordMoveToCOP2:
        dependencies.gprReads = target;
        dependencies.cop2Writes = coprocessorDestination;
        dependencies.specialWrites = RESOURCE_COP2_STATE;
        break;
      case EEOperation::ControlMoveFromCOP2:
        dependencies.gprWrites = target;
        dependencies.cop2ControlReads = coprocessorDestination;
        dependencies.specialReads = RESOURCE_COP2_STATE;
        break;
      case EEOperation::ControlMoveToCOP2:
        dependencies.gprReads = target;
        dependencies.cop2ControlWrites = coprocessorDestination;
        dependencies.specialWrites = RESOURCE_COP2_STATE;
        break;
      case EEOperation::VectorCallMicroSubroutine:
      case EEOperation::VectorCallMicroSubroutineRegister:
      case EEOperation::VectorMacroArithmetic:
        dependencies.specialReads = RESOURCE_COP2_STATE;
        dependencies.specialWrites = RESOURCE_COP2_STATE;
        break;
      case EEOperation::Count:
      default:
        throw std::invalid_argument(
          "Unknown EE operation dependency classification.");
    }

    if (updatesCOP1ArithmeticFlags(instruction.operation))
    {
      dependencies.specialReads |= RESOURCE_COP1_FCR31;
      dependencies.specialWrites |= RESOURCE_COP1_FCR31;
    }
    return dependencies;
  }

  bool hasSamePairDependency(
    const EEInstruction &older,
    const EEInstruction &younger)
  {
    const EEInstructionDependencies olderDependencies =
      eeInstructionDependencies(older);
    const EEInstructionDependencies youngerDependencies =
      eeInstructionDependencies(younger);
    return
      (olderDependencies.gprWrites &
       (youngerDependencies.gprReads |
        youngerDependencies.gprWrites)) != 0 ||
      (olderDependencies.fprWrites &
       (youngerDependencies.fprReads |
        youngerDependencies.fprWrites)) != 0 ||
      (olderDependencies.cop2Writes &
       (youngerDependencies.cop2Reads |
        youngerDependencies.cop2Writes)) != 0 ||
      (olderDependencies.cop2ControlWrites &
       (youngerDependencies.cop2ControlReads |
        youngerDependencies.cop2ControlWrites)) != 0 ||
      (olderDependencies.specialWrites &
       (youngerDependencies.specialReads |
        youngerDependencies.specialWrites)) != 0;
  }

  bool isIllegalProgramOrderPair(
    const EEInstruction &older,
    const EEInstruction &younger)
  {
    if (older.operation == EEOperation::ExceptionReturn)
    {
      return true;
    }
    if (!isEEBranchOperation(older.operation))
    {
      return false;
    }
    if (isEEBranchOperation(younger.operation) ||
         younger.operation == EEOperation::ExceptionReturn ||
         younger.operation ==
           EEOperation::SynchronizeLoadStore ||
         younger.operation ==
           EEOperation::SynchronizePipeline)
    {
      return true;
    }
    if (!isEEBranchLikelyOperation(older.operation))
    {
      return false;
    }
    return
      younger.operation == EEOperation::MoveToShiftAmount ||
      younger.operation ==
         EEOperation::MoveByteCountToShiftAmount ||
      younger.operation ==
         EEOperation::MoveHalfwordCountToShiftAmount;
  }

  EEIssuePairing issuePairing(
    EEInstructionCategory pipe0,
    EEInstructionCategory pipe1)
  {
    if ((pipe0 == EEInstructionCategory::Branch &&
         (pipe1 == EEInstructionCategory::ExceptionReturn ||
          pipe1 == EEInstructionCategory::Branch)))
    {
      return EEIssuePairing::Forbidden;
    }
    if ((pipe0 == EEInstructionCategory::WideOperate &&
         (pipe1 == EEInstructionCategory::LeadingZeroCount ||
          pipe1 == EEInstructionCategory::ALU ||
          pipe1 == EEInstructionCategory::MAC1)) ||
        (pipe0 == EEInstructionCategory::COP1Operate &&
         pipe1 == EEInstructionCategory::COP1Move) ||
        (pipe0 == EEInstructionCategory::COP2Operate &&
         pipe1 == EEInstructionCategory::COP2Move))
    {
      return EEIssuePairing::ConcurrentWithStall;
    }
    return EEIssuePairing::Concurrent;
  }

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
    table[0x11].kind = DecodeKind::Cop1;
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
    direct(&table, 0x31, EEOperation::LoadWordToCOP1);
    direct(&table, 0x37, EEOperation::LoadDoubleword);
    direct(&table, 0x36, EEOperation::LoadQuadwordToCOP2);
    direct(&table, 0x39, EEOperation::StoreWordFromCOP1);
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
      0x0f,
      EEOperation::SynchronizeLoadStore,
      REGISTER_SOURCE_MASK |
        REGISTER_TARGET_MASK |
        REGISTER_DESTINATION_MASK);
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
      if (classifyVUMacroInstruction(
            instruction->raw) !=
          VUMacroInstructionKind::Invalid)
      {
        instruction->operation =
          EEOperation::VectorMacroArithmetic;
        return;
      }
      reject(DecodeKind::Reserved);
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

  void applyCop1(EEInstruction *instruction)
  {
    if (instruction->sourceRegister == 0x00 ||
        instruction->sourceRegister == 0x02 ||
        instruction->sourceRegister == 0x04 ||
        instruction->sourceRegister == 0x06)
    {
      if ((instruction->raw & UINT32_C(0x000007ff)) != 0)
      {
        reject(DecodeKind::Reserved);
      }
      if ((instruction->sourceRegister == 0x02 ||
           instruction->sourceRegister == 0x06) &&
          instruction->destinationRegister != 0 &&
          instruction->destinationRegister != 31)
      {
        reject(DecodeKind::Reserved);
      }
      switch (instruction->sourceRegister)
      {
        case 0x00:
          instruction->operation =
            EEOperation::MoveWordFromCOP1;
          break;
        case 0x02:
          instruction->operation =
            EEOperation::MoveControlWordFromCOP1;
          break;
        case 0x04:
          instruction->operation =
            EEOperation::MoveWordToCOP1;
          break;
        default:
          instruction->operation =
            EEOperation::MoveControlWordToCOP1;
          break;
      }
      return;
    }
    if (instruction->sourceRegister == 0x08)
    {
      switch (instruction->targetRegister)
      {
        case 0x00:
          instruction->operation = EEOperation::BranchCOP1False;
          return;
        case 0x01:
          instruction->operation = EEOperation::BranchCOP1True;
          return;
        case 0x02:
          instruction->operation =
            EEOperation::BranchCOP1FalseLikely;
          return;
        case 0x03:
          instruction->operation =
            EEOperation::BranchCOP1TrueLikely;
          return;
        default:
          reject(DecodeKind::Reserved);
      }
    }
    if (instruction->sourceRegister == 0x10)
    {
      switch (instruction->function)
      {
        case 0x00:
          instruction->operation =
            EEOperation::AddSingleCOP1;
          return;
        case 0x01:
          instruction->operation =
            EEOperation::SubtractSingleCOP1;
          return;
        case 0x02:
          instruction->operation =
            EEOperation::MultiplySingleCOP1;
          return;
        case 0x03:
          instruction->operation =
            EEOperation::DivideSingleCOP1;
          return;
        case 0x04:
          if (instruction->destinationRegister != 0)
          {
            reject(DecodeKind::Reserved);
          }
          instruction->operation =
            EEOperation::SquareRootSingleCOP1;
          return;
        case 0x05:
          if (instruction->targetRegister != 0)
          {
            reject(DecodeKind::Reserved);
          }
          instruction->operation =
            EEOperation::AbsoluteSingleCOP1;
          return;
        case 0x06:
          if (instruction->targetRegister != 0)
          {
            reject(DecodeKind::Reserved);
          }
          instruction->operation =
            EEOperation::MoveSingleCOP1;
          return;
        case 0x07:
          if (instruction->targetRegister != 0)
          {
            reject(DecodeKind::Reserved);
          }
          instruction->operation =
            EEOperation::NegateSingleCOP1;
          return;
        case 0x16:
          instruction->operation =
            EEOperation::ReciprocalSquareRootSingleCOP1;
          return;
        case 0x18:
          if (instruction->shiftAmount != 0)
          {
            reject(DecodeKind::Reserved);
          }
          instruction->operation =
            EEOperation::AddSingleToAccumulatorCOP1;
          return;
        case 0x19:
          if (instruction->shiftAmount != 0)
          {
            reject(DecodeKind::Reserved);
          }
          instruction->operation =
            EEOperation::SubtractSingleToAccumulatorCOP1;
          return;
        case 0x1a:
          if (instruction->shiftAmount != 0)
          {
            reject(DecodeKind::Reserved);
          }
          instruction->operation =
            EEOperation::MultiplySingleToAccumulatorCOP1;
          return;
        case 0x1c:
          instruction->operation =
            EEOperation::MultiplyAddSingleCOP1;
          return;
        case 0x1d:
          instruction->operation =
            EEOperation::MultiplySubtractSingleCOP1;
          return;
        case 0x1e:
          if (instruction->shiftAmount != 0)
          {
            reject(DecodeKind::Reserved);
          }
          instruction->operation =
            EEOperation::MultiplyAddSingleToAccumulatorCOP1;
          return;
        case 0x1f:
          if (instruction->shiftAmount != 0)
          {
            reject(DecodeKind::Reserved);
          }
          instruction->operation =
            EEOperation::MultiplySubtractSingleToAccumulatorCOP1;
          return;
        case 0x24:
          if (instruction->targetRegister != 0)
          {
            reject(DecodeKind::Reserved);
          }
          instruction->operation =
            EEOperation::ConvertSingleToWordCOP1;
          return;
        case 0x28:
          instruction->operation =
            EEOperation::MaximumSingleCOP1;
          return;
        case 0x29:
          instruction->operation =
            EEOperation::MinimumSingleCOP1;
          return;
        case 0x30:
        case 0x32:
        case 0x34:
        case 0x36:
          if (instruction->shiftAmount != 0)
          {
            reject(DecodeKind::Reserved);
          }
          switch (instruction->function)
          {
            case 0x30:
              instruction->operation =
                EEOperation::CompareFalseSingleCOP1;
              return;
            case 0x32:
              instruction->operation =
                EEOperation::CompareEqualSingleCOP1;
              return;
            case 0x34:
              instruction->operation =
                EEOperation::CompareLessThanSingleCOP1;
              return;
            default:
              instruction->operation =
                EEOperation::CompareLessThanOrEqualSingleCOP1;
              return;
          }
        default:
          break;
      }
      reject(DecodeKind::Unsupported);
    }
    if (instruction->sourceRegister == 0x14)
    {
      if (instruction->function != 0x20)
      {
        reject(DecodeKind::Unsupported);
      }
      if (instruction->targetRegister != 0)
      {
        reject(DecodeKind::Reserved);
      }
      instruction->operation =
        EEOperation::ConvertWordToSingleCOP1;
      return;
    }
    reject(DecodeKind::Reserved);
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

EEInstructionDependencies eeInstructionDependencies(
  const EEInstruction &instruction)
{
  eeOperationMetadata(instruction.operation);
  return buildInstructionDependencies(instruction);
}

namespace
{
EEInstructionRouting buildOperationRouting(EEOperation operation)
{
  constexpr std::uint8_t PIPE_0 =
    static_cast<std::uint8_t>(EELogicalPipe::Pipe0);
  constexpr std::uint8_t PIPE_1 =
    static_cast<std::uint8_t>(EELogicalPipe::Pipe1);
  constexpr std::uint8_t PHYSICAL_I0 =
    static_cast<std::uint8_t>(EEPhysicalPipeline::I0);
  constexpr std::uint8_t PHYSICAL_I1 =
    static_cast<std::uint8_t>(EEPhysicalPipeline::I1);
  constexpr std::uint8_t PHYSICAL_LS =
    static_cast<std::uint8_t>(
      EEPhysicalPipeline::LoadStore);
  constexpr std::uint8_t PHYSICAL_BRANCH =
    static_cast<std::uint8_t>(
      EEPhysicalPipeline::Branch);
  constexpr std::uint8_t PHYSICAL_COP1 =
    static_cast<std::uint8_t>(EEPhysicalPipeline::COP1);
  constexpr std::uint8_t PHYSICAL_COP2 =
    static_cast<std::uint8_t>(EEPhysicalPipeline::COP2);

  switch (operation)
  {
    case EEOperation::LoadByte:
    case EEOperation::LoadByteUnsigned:
    case EEOperation::StoreByte:
    case EEOperation::LoadHalfword:
    case EEOperation::LoadHalfwordUnsigned:
    case EEOperation::StoreHalfword:
    case EEOperation::LoadWord:
    case EEOperation::LoadWordUnsigned:
    case EEOperation::StoreWord:
    case EEOperation::LoadWordLeft:
    case EEOperation::LoadWordRight:
    case EEOperation::StoreWordLeft:
    case EEOperation::StoreWordRight:
    case EEOperation::LoadDoubleword:
    case EEOperation::StoreDoubleword:
    case EEOperation::LoadDoublewordLeft:
    case EEOperation::LoadDoublewordRight:
    case EEOperation::StoreDoublewordLeft:
    case EEOperation::StoreDoublewordRight:
    case EEOperation::LoadQuadword:
    case EEOperation::StoreQuadword:
      return {
        EEInstructionCategory::LoadStore,
        PIPE_1,
        0,
        PHYSICAL_LS
      };
    case EEOperation::SynchronizeLoadStore:
    case EEOperation::SynchronizePipeline:
      return {
        EEInstructionCategory::Synchronization,
        PIPE_1,
        0,
        PHYSICAL_I1
      };
    case EEOperation::ExceptionReturn:
      return {
        EEInstructionCategory::ExceptionReturn,
        PIPE_1,
        0,
        PHYSICAL_I1
      };
    case EEOperation::MoveFromShiftAmount:
    case EEOperation::MoveToShiftAmount:
    case EEOperation::MoveByteCountToShiftAmount:
    case EEOperation::MoveHalfwordCountToShiftAmount:
      return {
        EEInstructionCategory::ShiftAmountOperate,
        PIPE_0,
        PHYSICAL_I0,
        0
      };
    case EEOperation::MoveWordFromCOP1:
    case EEOperation::MoveWordToCOP1:
    case EEOperation::MoveControlWordFromCOP1:
    case EEOperation::MoveControlWordToCOP1:
    case EEOperation::LoadWordToCOP1:
    case EEOperation::StoreWordFromCOP1:
    case EEOperation::MoveSingleCOP1:
      return {
        EEInstructionCategory::COP1Move,
        PIPE_1,
        0,
        static_cast<std::uint8_t>(
          PHYSICAL_LS | PHYSICAL_COP1)
      };
    case EEOperation::LoadQuadwordToCOP2:
    case EEOperation::StoreQuadwordFromCOP2:
    case EEOperation::QuadwordMoveFromCOP2:
    case EEOperation::QuadwordMoveToCOP2:
    case EEOperation::ControlMoveFromCOP2:
    case EEOperation::ControlMoveToCOP2:
      return {
        EEInstructionCategory::COP2Move,
        PIPE_1,
        0,
        static_cast<std::uint8_t>(
          PHYSICAL_LS | PHYSICAL_COP2)
      };
    case EEOperation::AbsoluteSingleCOP1:
    case EEOperation::NegateSingleCOP1:
    case EEOperation::MaximumSingleCOP1:
    case EEOperation::MinimumSingleCOP1:
    case EEOperation::ConvertWordToSingleCOP1:
    case EEOperation::ConvertSingleToWordCOP1:
    case EEOperation::AddSingleCOP1:
    case EEOperation::SubtractSingleCOP1:
    case EEOperation::MultiplySingleCOP1:
    case EEOperation::DivideSingleCOP1:
    case EEOperation::SquareRootSingleCOP1:
    case EEOperation::ReciprocalSquareRootSingleCOP1:
    case EEOperation::MultiplyAddSingleCOP1:
    case EEOperation::MultiplySubtractSingleCOP1:
    case EEOperation::AddSingleToAccumulatorCOP1:
    case EEOperation::SubtractSingleToAccumulatorCOP1:
    case EEOperation::MultiplySingleToAccumulatorCOP1:
    case EEOperation::MultiplyAddSingleToAccumulatorCOP1:
    case EEOperation::MultiplySubtractSingleToAccumulatorCOP1:
    case EEOperation::CompareFalseSingleCOP1:
    case EEOperation::CompareEqualSingleCOP1:
    case EEOperation::CompareLessThanSingleCOP1:
    case EEOperation::CompareLessThanOrEqualSingleCOP1:
      return {
        EEInstructionCategory::COP1Operate,
        PIPE_0,
        static_cast<std::uint8_t>(
          PHYSICAL_I0 | PHYSICAL_COP1),
        0
      };
    case EEOperation::VectorCallMicroSubroutine:
    case EEOperation::VectorCallMicroSubroutineRegister:
    case EEOperation::VectorMacroArithmetic:
      return {
        EEInstructionCategory::COP2Operate,
        PIPE_0,
        static_cast<std::uint8_t>(
          PHYSICAL_I0 | PHYSICAL_COP2),
        0
      };
    case EEOperation::MoveFromHI:
    case EEOperation::MoveToHI:
    case EEOperation::MoveFromLO:
    case EEOperation::MoveToLO:
    case EEOperation::MultiplyWord:
    case EEOperation::MultiplyUnsignedWord:
    case EEOperation::DivideWord:
    case EEOperation::DivideUnsignedWord:
    case EEOperation::MultiplyAddWord:
    case EEOperation::MultiplyAddUnsignedWord:
      return {
        EEInstructionCategory::MAC0,
        PIPE_0,
        PHYSICAL_I0,
        0
      };
    case EEOperation::MoveFromHI1:
    case EEOperation::MoveToHI1:
    case EEOperation::MoveFromLO1:
    case EEOperation::MoveToLO1:
    case EEOperation::MultiplyWord1:
    case EEOperation::MultiplyUnsignedWord1:
    case EEOperation::DivideWord1:
    case EEOperation::DivideUnsignedWord1:
    case EEOperation::MultiplyAddWord1:
    case EEOperation::MultiplyAddUnsignedWord1:
      return {
        EEInstructionCategory::MAC1,
        PIPE_1,
        0,
        PHYSICAL_I1
      };
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
    case EEOperation::BranchCOP1False:
    case EEOperation::BranchCOP1FalseLikely:
    case EEOperation::BranchCOP1True:
    case EEOperation::BranchCOP1TrueLikely:
    case EEOperation::BranchCOP2False:
    case EEOperation::BranchCOP2FalseLikely:
    case EEOperation::BranchCOP2True:
    case EEOperation::BranchCOP2TrueLikely:
      return {
        EEInstructionCategory::Branch,
        static_cast<std::uint8_t>(PIPE_0 | PIPE_1),
        PHYSICAL_BRANCH,
        PHYSICAL_BRANCH
      };
    case EEOperation::Nop:
    case EEOperation::ShiftLeftLogicalWord:
    case EEOperation::ShiftRightLogicalWord:
    case EEOperation::ShiftRightArithmeticWord:
    case EEOperation::ShiftLeftLogicalVariableWord:
    case EEOperation::ShiftRightLogicalVariableWord:
    case EEOperation::ShiftRightArithmeticVariableWord:
    case EEOperation::ShiftLeftLogicalVariableDoubleword:
    case EEOperation::ShiftRightLogicalVariableDoubleword:
    case EEOperation::ShiftRightArithmeticVariableDoubleword:
    case EEOperation::AddWord:
    case EEOperation::AddUnsignedWord:
    case EEOperation::SubtractWord:
    case EEOperation::SubtractUnsignedWord:
    case EEOperation::And:
    case EEOperation::Or:
    case EEOperation::Xor:
    case EEOperation::Nor:
    case EEOperation::SetLessThan:
    case EEOperation::SetLessThanUnsigned:
    case EEOperation::AddDoubleword:
    case EEOperation::AddUnsignedDoubleword:
    case EEOperation::SubtractDoubleword:
    case EEOperation::SubtractUnsignedDoubleword:
    case EEOperation::ShiftLeftLogicalDoubleword:
    case EEOperation::ShiftRightLogicalDoubleword:
    case EEOperation::ShiftRightArithmeticDoubleword:
    case EEOperation::ShiftLeftLogicalDoubleword32:
    case EEOperation::ShiftRightLogicalDoubleword32:
    case EEOperation::ShiftRightArithmeticDoubleword32:
    case EEOperation::AddImmediateWord:
    case EEOperation::AddImmediateUnsignedWord:
    case EEOperation::SetLessThanImmediate:
    case EEOperation::SetLessThanImmediateUnsigned:
    case EEOperation::AndImmediate:
    case EEOperation::OrImmediate:
    case EEOperation::XorImmediate:
    case EEOperation::LoadUpperImmediate:
    case EEOperation::AddImmediateDoubleword:
    case EEOperation::AddImmediateUnsignedDoubleword:
    case EEOperation::SystemCall:
    case EEOperation::Breakpoint:
      return {
        EEInstructionCategory::ALU,
        static_cast<std::uint8_t>(PIPE_0 | PIPE_1),
        PHYSICAL_I0,
        PHYSICAL_I1
      };
    case EEOperation::Count:
      break;
  }

  throw std::invalid_argument(
    "Unknown EE operation routing classification.");
}

EEMemoryAccess memoryAccessFor(EEOperation operation)
{
  switch (operation)
  {
    case EEOperation::LoadByte:
    case EEOperation::LoadByteUnsigned:
    case EEOperation::LoadHalfword:
    case EEOperation::LoadHalfwordUnsigned:
    case EEOperation::LoadWord:
    case EEOperation::LoadWordUnsigned:
    case EEOperation::LoadWordLeft:
    case EEOperation::LoadWordRight:
    case EEOperation::LoadDoubleword:
    case EEOperation::LoadDoublewordLeft:
    case EEOperation::LoadDoublewordRight:
    case EEOperation::LoadQuadword:
    case EEOperation::LoadWordToCOP1:
    case EEOperation::LoadQuadwordToCOP2:
      return EEMemoryAccess::Load;
    case EEOperation::StoreByte:
    case EEOperation::StoreHalfword:
    case EEOperation::StoreWord:
    case EEOperation::StoreWordLeft:
    case EEOperation::StoreWordRight:
    case EEOperation::StoreDoubleword:
    case EEOperation::StoreDoublewordLeft:
    case EEOperation::StoreDoublewordRight:
    case EEOperation::StoreQuadword:
    case EEOperation::StoreWordFromCOP1:
    case EEOperation::StoreQuadwordFromCOP2:
      return EEMemoryAccess::Store;
    default:
      return EEMemoryAccess::None;
  }
}

EECOP1OperationFamily cop1FamilyFor(EEOperation operation)
{
  switch (operation)
  {
    case EEOperation::BranchCOP1False:
    case EEOperation::BranchCOP1FalseLikely:
    case EEOperation::BranchCOP1True:
    case EEOperation::BranchCOP1TrueLikely:
      return EECOP1OperationFamily::ConditionBranch;
    case EEOperation::MoveWordFromCOP1:
    case EEOperation::MoveWordToCOP1:
    case EEOperation::MoveControlWordFromCOP1:
    case EEOperation::MoveControlWordToCOP1:
    case EEOperation::MoveSingleCOP1:
      return EECOP1OperationFamily::RegisterMove;
    case EEOperation::LoadWordToCOP1:
    case EEOperation::StoreWordFromCOP1:
      return EECOP1OperationFamily::MemoryMove;
    case EEOperation::AbsoluteSingleCOP1:
    case EEOperation::NegateSingleCOP1:
      return EECOP1OperationFamily::Unary;
    case EEOperation::ConvertWordToSingleCOP1:
    case EEOperation::ConvertSingleToWordCOP1:
      return EECOP1OperationFamily::Conversion;
    case EEOperation::AddSingleCOP1:
    case EEOperation::SubtractSingleCOP1:
    case EEOperation::AddSingleToAccumulatorCOP1:
    case EEOperation::SubtractSingleToAccumulatorCOP1:
      return EECOP1OperationFamily::AddSubtract;
    case EEOperation::MultiplySingleCOP1:
    case EEOperation::MultiplySingleToAccumulatorCOP1:
      return EECOP1OperationFamily::Multiply;
    case EEOperation::MultiplyAddSingleCOP1:
    case EEOperation::MultiplySubtractSingleCOP1:
    case EEOperation::MultiplyAddSingleToAccumulatorCOP1:
    case EEOperation::MultiplySubtractSingleToAccumulatorCOP1:
      return EECOP1OperationFamily::Compound;
    case EEOperation::MaximumSingleCOP1:
    case EEOperation::MinimumSingleCOP1:
      return EECOP1OperationFamily::MinMax;
    case EEOperation::CompareFalseSingleCOP1:
    case EEOperation::CompareEqualSingleCOP1:
    case EEOperation::CompareLessThanSingleCOP1:
    case EEOperation::CompareLessThanOrEqualSingleCOP1:
      return EECOP1OperationFamily::Comparison;
    case EEOperation::DivideSingleCOP1:
    case EEOperation::SquareRootSingleCOP1:
    case EEOperation::ReciprocalSquareRootSingleCOP1:
      return EECOP1OperationFamily::Divider;
    default:
      return EECOP1OperationFamily::None;
  }
}

bool updatesCOP1ArithmeticFlags(EEOperation operation)
{
  switch (operation)
  {
    case EEOperation::AbsoluteSingleCOP1:
    case EEOperation::NegateSingleCOP1:
    case EEOperation::ConvertSingleToWordCOP1:
    case EEOperation::AddSingleCOP1:
    case EEOperation::SubtractSingleCOP1:
    case EEOperation::AddSingleToAccumulatorCOP1:
    case EEOperation::SubtractSingleToAccumulatorCOP1:
    case EEOperation::MultiplySingleCOP1:
    case EEOperation::MultiplySingleToAccumulatorCOP1:
    case EEOperation::MultiplyAddSingleCOP1:
    case EEOperation::MultiplySubtractSingleCOP1:
    case EEOperation::MultiplyAddSingleToAccumulatorCOP1:
    case EEOperation::MultiplySubtractSingleToAccumulatorCOP1:
    case EEOperation::DivideSingleCOP1:
    case EEOperation::SquareRootSingleCOP1:
    case EEOperation::ReciprocalSquareRootSingleCOP1:
    case EEOperation::MaximumSingleCOP1:
    case EEOperation::MinimumSingleCOP1:
      return true;
    default:
      return false;
  }
}

EEOperationMetadata buildOperationMetadata(
  EEOperation operation)
{
  EEOperationMetadata metadata;
  metadata.routing = buildOperationRouting(operation);
  metadata.memoryAccess = memoryAccessFor(operation);
  metadata.cop1Family = cop1FamilyFor(operation);
  metadata.cop1ManagedPipeline =
    metadata.cop1Family != EECOP1OperationFamily::None &&
    metadata.cop1Family !=
      EECOP1OperationFamily::ConditionBranch;
  metadata.updatesCOP1ArithmeticFlags =
    updatesCOP1ArithmeticFlags(operation);
  if (metadata.cop1Family ==
      EECOP1OperationFamily::Divider)
  {
    if (operation ==
        EEOperation::ReciprocalSquareRootSingleCOP1)
    {
      metadata.cop1DividerLatency = 14;
      metadata.cop1DividerInitiationInterval = 13;
    }
    else
    {
      metadata.cop1DividerLatency = 8;
      metadata.cop1DividerInitiationInterval = 7;
    }
  }
  return metadata;
}

const std::array<EEOperationMetadata, EE_OPERATION_COUNT> &
operationMetadataTable()
{
  static const std::array<
    EEOperationMetadata,
    EE_OPERATION_COUNT> table = []()
    {
      std::array<
        EEOperationMetadata,
        EE_OPERATION_COUNT> result = {};
      for (std::uint8_t value = 0;
           value < EE_OPERATION_COUNT;
           ++value)
      {
        result[value] = buildOperationMetadata(
          static_cast<EEOperation>(value));
      }
      return result;
    }();
  return table;
}
}

const EEOperationMetadata &eeOperationMetadata(
  EEOperation operation)
{
  if (static_cast<std::uint8_t>(operation) >=
      EE_OPERATION_COUNT)
  {
    throw std::invalid_argument(
      "Unknown EE operation metadata.");
  }

  return operationMetadataTable()[
    static_cast<std::uint8_t>(operation)];
}

EEInstructionMetadata eeInstructionMetadata(
  const EEInstruction &instruction)
{
  EEInstructionMetadata metadata;
  metadata.operation =
    eeOperationMetadata(instruction.operation);
  metadata.dependencies =
    buildInstructionDependencies(instruction);
  return metadata;
}

bool isCOP1ConditionBranchOperation(EEOperation operation)
{
  return
    eeOperationMetadata(operation).cop1Family ==
    EECOP1OperationFamily::ConditionBranch;
}

bool isCOP1MoveOperation(EEOperation operation)
{
  const EECOP1OperationFamily family =
    eeOperationMetadata(operation).cop1Family;
  return family == EECOP1OperationFamily::RegisterMove ||
    family == EECOP1OperationFamily::MemoryMove;
}

bool isCOP1RegisterMoveOperation(EEOperation operation)
{
  return
    eeOperationMetadata(operation).cop1Family ==
    EECOP1OperationFamily::RegisterMove;
}

bool isCOP1OperateOperation(EEOperation operation)
{
  const EECOP1OperationFamily family =
    eeOperationMetadata(operation).cop1Family;
  return
    family != EECOP1OperationFamily::None &&
    family != EECOP1OperationFamily::ConditionBranch &&
    family != EECOP1OperationFamily::RegisterMove &&
    family != EECOP1OperationFamily::MemoryMove;
}

bool isCOP1DividerOperation(EEOperation operation)
{
  return
    eeOperationMetadata(operation).cop1Family ==
    EECOP1OperationFamily::Divider;
}

bool isCOP1AddSubtractOperation(EEOperation operation)
{
  return
    eeOperationMetadata(operation).cop1Family ==
    EECOP1OperationFamily::AddSubtract;
}

bool isCOP1MultiplyOperation(EEOperation operation)
{
  return
    eeOperationMetadata(operation).cop1Family ==
    EECOP1OperationFamily::Multiply;
}

bool isCOP1CompoundOperation(EEOperation operation)
{
  return
    eeOperationMetadata(operation).cop1Family ==
    EECOP1OperationFamily::Compound;
}

bool isCOP1UnaryOperation(EEOperation operation)
{
  return
    eeOperationMetadata(operation).cop1Family ==
    EECOP1OperationFamily::Unary;
}

bool isCOP1SingleSourceStagedOperation(
  EEOperation operation)
{
  const EECOP1OperationFamily family =
    eeOperationMetadata(operation).cop1Family;
  return family == EECOP1OperationFamily::Unary ||
    family == EECOP1OperationFamily::Conversion;
}

bool isCOP1ComparisonOperation(EEOperation operation)
{
  return
    eeOperationMetadata(operation).cop1Family ==
    EECOP1OperationFamily::Comparison;
}

bool isCOP1StagedOperation(EEOperation operation)
{
  const EECOP1OperationFamily family =
    eeOperationMetadata(operation).cop1Family;
  return
    family == EECOP1OperationFamily::Unary ||
    family == EECOP1OperationFamily::Conversion ||
    family == EECOP1OperationFamily::AddSubtract ||
    family == EECOP1OperationFamily::Multiply ||
    family == EECOP1OperationFamily::Compound ||
    family == EECOP1OperationFamily::MinMax ||
    family == EECOP1OperationFamily::Comparison;
}

bool isCOP1ManagedPipelineOperation(EEOperation operation)
{
  return eeOperationMetadata(operation).cop1ManagedPipeline;
}

EECOP1DividerTiming cop1DividerTiming(
  EEOperation operation)
{
  const EEOperationMetadata &metadata =
    eeOperationMetadata(operation);
  if (metadata.cop1Family !=
      EECOP1OperationFamily::Divider)
  {
    throw std::invalid_argument(
      "EE operation does not use the COP1 divider.");
  }
  return {
    metadata.cop1DividerLatency,
    metadata.cop1DividerInitiationInterval
  };
}

EEInstructionRouting eeInstructionRouting(EEOperation operation)
{
  if (static_cast<std::uint8_t>(operation) >=
      EE_OPERATION_COUNT)
  {
    throw std::invalid_argument(
      "Unknown EE operation routing classification.");
  }
  return eeOperationMetadata(operation).routing;
}

bool eeInstructionSupportsLogicalPipe(
  const EEInstructionRouting &routing,
  EELogicalPipe pipe)
{
  return
    (routing.logicalPipes &
     static_cast<std::uint8_t>(pipe)) != 0;
}

bool eeInstructionUsesPhysicalPipeline(
  const EEInstructionRouting &routing,
  EELogicalPipe pipe,
  EEPhysicalPipeline pipeline)
{
  return
    (eeInstructionPhysicalPipelines(routing, pipe) &
     static_cast<std::uint8_t>(pipeline)) != 0;
}

std::uint8_t eeInstructionPhysicalPipelines(
  const EEInstructionRouting &routing,
  EELogicalPipe pipe)
{
  return
    pipe == EELogicalPipe::Pipe0
      ? routing.pipe0PhysicalPipelines
      : routing.pipe1PhysicalPipelines;
}

EEInstructionPipeAssignment assignEEInstructionPairPipes(
  EEOperation older,
  EEOperation younger)
{
  const EEInstructionRouting olderRouting =
    eeInstructionRouting(older);
  const EEInstructionRouting youngerRouting =
    eeInstructionRouting(younger);

  if (eeInstructionSupportsLogicalPipe(
        olderRouting,
        EELogicalPipe::Pipe0) &&
      eeInstructionSupportsLogicalPipe(
        youngerRouting,
        EELogicalPipe::Pipe1))
  {
    return {
      true,
      EELogicalPipe::Pipe0,
      EELogicalPipe::Pipe1
    };
  }
  if (eeInstructionSupportsLogicalPipe(
        olderRouting,
        EELogicalPipe::Pipe1) &&
      eeInstructionSupportsLogicalPipe(
        youngerRouting,
        EELogicalPipe::Pipe0))
  {
    return {
      true,
      EELogicalPipe::Pipe1,
      EELogicalPipe::Pipe0
    };
  }

  return {};
}

EEIssueSelection selectEEIssueGroup(
  const EEInstruction &older,
  const EEInstruction &younger,
  bool olderReady,
  bool youngerReady)
{
  EEIssueSelection selection;
  if (!olderReady)
  {
    return selection;
  }

  selection.instructionCount = 1;
  const EEInstructionRouting olderRouting =
    eeInstructionRouting(older.operation);
  selection.assignment.olderPipe =
    eeInstructionSupportsLogicalPipe(
      olderRouting,
      EELogicalPipe::Pipe0)
      ? EELogicalPipe::Pipe0
      : EELogicalPipe::Pipe1;
  if (!youngerReady)
  {
    return selection;
  }
  if (isIllegalProgramOrderPair(older, younger))
  {
    return selection;
  }

  const EEInstructionPipeAssignment pairAssignment =
    assignEEInstructionPairPipes(
      older.operation,
      younger.operation);
  if (!pairAssignment.assignable ||
      hasSamePairDependency(older, younger))
  {
    return selection;
  }
  selection.assignment = pairAssignment;

  const EEInstructionRouting youngerRouting =
    eeInstructionRouting(younger.operation);
  const EEInstructionCategory pipe0Category =
    selection.assignment.olderPipe == EELogicalPipe::Pipe0
      ? olderRouting.category
      : youngerRouting.category;
  const EEInstructionCategory pipe1Category =
    selection.assignment.olderPipe == EELogicalPipe::Pipe1
      ? olderRouting.category
      : youngerRouting.category;
  selection.pairing =
    issuePairing(pipe0Category, pipe1Category);
  if (selection.pairing != EEIssuePairing::Forbidden)
  {
    selection.instructionCount = 2;
  }
  return selection;
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
    case EEOperation::BranchCOP1False:
    case EEOperation::BranchCOP1FalseLikely:
    case EEOperation::BranchCOP1True:
    case EEOperation::BranchCOP1TrueLikely:
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
    operation == EEOperation::BranchCOP1FalseLikely ||
    operation == EEOperation::BranchCOP1TrueLikely ||
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
    if (instruction.operation ==
          EEOperation::SynchronizeLoadStore &&
        (instruction.shiftAmount & 0x10) != 0)
    {
      instruction.operation =
        EEOperation::SynchronizePipeline;
    }
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
  if (primary.kind == DecodeKind::Cop1)
  {
    applyCop1(&instruction);
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
