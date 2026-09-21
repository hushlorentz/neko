#include <cstdint>

#include "catch.hpp"
#include "ee_instruction.hpp"

namespace
{
  std::uint32_t registerInstruction(
    std::uint8_t function,
    std::uint8_t rs,
    std::uint8_t rt,
    std::uint8_t rd,
    std::uint8_t shiftAmount = 0)
  {
    return
      (static_cast<std::uint32_t>(rs) << 21) |
      (static_cast<std::uint32_t>(rt) << 16) |
      (static_cast<std::uint32_t>(rd) << 11) |
      (static_cast<std::uint32_t>(shiftAmount) << 6) |
      function;
  }

  std::uint32_t immediateInstruction(
    std::uint8_t opcode,
    std::uint8_t rs,
    std::uint8_t rt,
    std::uint16_t immediate)
  {
    return
      (static_cast<std::uint32_t>(opcode) << 26) |
      (static_cast<std::uint32_t>(rs) << 21) |
      (static_cast<std::uint32_t>(rt) << 16) |
      immediate;
  }

  std::uint32_t regimmInstruction(
    std::uint8_t function,
    std::uint8_t rs,
    std::uint16_t immediate)
  {
    return
      (UINT32_C(0x01) << 26) |
      (static_cast<std::uint32_t>(rs) << 21) |
      (static_cast<std::uint32_t>(function) << 16) |
      immediate;
  }
}

TEST_CASE("EE instruction field decoding")
{
  SECTION("The canonical zero instruction decodes as NOP")
  {
    const EEInstruction instruction = decodeEEInstruction(0);

    REQUIRE(instruction.operation == EEOperation::Nop);
    REQUIRE(instruction.raw == 0);
  }

  SECTION("Register instruction fields are preserved")
  {
    const std::uint32_t raw =
      registerInstruction(0x21, 1, 2, 3);
    const EEInstruction instruction =
      decodeEEInstruction(raw);

    REQUIRE(
      instruction.operation ==
      EEOperation::AddUnsignedWord);
    REQUIRE(instruction.raw == raw);
    REQUIRE(instruction.opcode == 0);
    REQUIRE(instruction.sourceRegister == 1);
    REQUIRE(instruction.targetRegister == 2);
    REQUIRE(instruction.destinationRegister == 3);
    REQUIRE(instruction.shiftAmount == 0);
    REQUIRE(instruction.function == 0x21);
  }

  SECTION("Immediate instruction fields remain raw")
  {
    const std::uint32_t raw =
      immediateInstruction(0x09, 4, 5, 0xfffc);
    const EEInstruction instruction =
      decodeEEInstruction(raw);

    REQUIRE(
      instruction.operation ==
      EEOperation::AddImmediateUnsignedWord);
    REQUIRE(instruction.sourceRegister == 4);
    REQUIRE(instruction.targetRegister == 5);
    REQUIRE(instruction.immediate == 0xfffc);
  }

  SECTION("SYNC stype selects load-store or pipeline synchronization")
  {
    for (std::uint8_t stype = 0; stype < 16; ++stype)
    {
      const EEInstruction instruction =
        decodeEEInstruction(
          (static_cast<std::uint32_t>(stype) << 6) |
          UINT32_C(0x0f));

      REQUIRE(
        instruction.operation ==
        EEOperation::SynchronizeLoadStore);
      REQUIRE(instruction.shiftAmount == stype);
    }
    for (std::uint8_t stype = 16; stype < 32; ++stype)
    {
      const EEInstruction instruction =
        decodeEEInstruction(
          (static_cast<std::uint32_t>(stype) << 6) |
          UINT32_C(0x0f));

      REQUIRE(
        instruction.operation ==
        EEOperation::SynchronizePipeline);
      REQUIRE(instruction.shiftAmount == stype);
    }
  }
}

TEST_CASE("EE instruction routing classification")
{
  const auto requireRouting =
    [](EEOperation operation,
       EEInstructionCategory category,
       bool pipe0,
       bool pipe1,
       std::uint8_t pipe0PhysicalPipelines,
       std::uint8_t pipe1PhysicalPipelines)
    {
      const EEInstructionRouting routing =
        eeInstructionRouting(operation);
      REQUIRE(routing.category == category);
      REQUIRE(
        eeInstructionSupportsLogicalPipe(
          routing,
          EELogicalPipe::Pipe0) == pipe0);
      REQUIRE(
        eeInstructionSupportsLogicalPipe(
          routing,
          EELogicalPipe::Pipe1) == pipe1);
      REQUIRE(
        eeInstructionPhysicalPipelines(
          routing,
          EELogicalPipe::Pipe0) ==
        pipe0PhysicalPipelines);
      REQUIRE(
        eeInstructionPhysicalPipelines(
          routing,
          EELogicalPipe::Pipe1) ==
        pipe1PhysicalPipelines);
    };
  const auto physical =
    [](EEPhysicalPipeline pipeline)
    {
      return static_cast<std::uint8_t>(pipeline);
    };

  SECTION("Flexible categories expose both compatible integer pipes")
  {
    requireRouting(
      EEOperation::AddWord,
      EEInstructionCategory::ALU,
      true,
      true,
      physical(EEPhysicalPipeline::I0),
      physical(EEPhysicalPipeline::I1));
    requireRouting(
      EEOperation::BranchEqual,
      EEInstructionCategory::Branch,
      true,
      true,
      physical(EEPhysicalPipeline::Branch),
      physical(EEPhysicalPipeline::Branch));
  }

  SECTION("Fixed integer categories expose their documented pipe")
  {
    requireRouting(
      EEOperation::SynchronizeLoadStore,
      EEInstructionCategory::Synchronization,
      false,
      true,
      0,
      physical(EEPhysicalPipeline::I1));
    requireRouting(
      EEOperation::SynchronizePipeline,
      EEInstructionCategory::Synchronization,
      false,
      true,
      0,
      physical(EEPhysicalPipeline::I1));
    requireRouting(
      EEOperation::LoadQuadword,
      EEInstructionCategory::LoadStore,
      false,
      true,
      0,
      physical(EEPhysicalPipeline::LoadStore));
    requireRouting(
      EEOperation::ExceptionReturn,
      EEInstructionCategory::ExceptionReturn,
      false,
      true,
      0,
      physical(EEPhysicalPipeline::I1));
    requireRouting(
      EEOperation::MoveToShiftAmount,
      EEInstructionCategory::ShiftAmountOperate,
      true,
      false,
      physical(EEPhysicalPipeline::I0),
      0);
    requireRouting(
      EEOperation::MultiplyAddWord,
      EEInstructionCategory::MAC0,
      true,
      false,
      physical(EEPhysicalPipeline::I0),
      0);
    requireRouting(
      EEOperation::MultiplyAddWord1,
      EEInstructionCategory::MAC1,
      false,
      true,
      0,
      physical(EEPhysicalPipeline::I1));
    requireRouting(
      EEOperation::ParallelAnd,
      EEInstructionCategory::WideOperate,
      true,
      false,
      static_cast<std::uint8_t>(
        physical(EEPhysicalPipeline::I0) |
        physical(EEPhysicalPipeline::I1)),
      0);
  }

  SECTION("Coprocessor categories expose every required physical pipe")
  {
    requireRouting(
      EEOperation::MoveSingleCOP1,
      EEInstructionCategory::COP1Move,
      false,
      true,
      0,
      static_cast<std::uint8_t>(
        physical(EEPhysicalPipeline::LoadStore) |
        physical(EEPhysicalPipeline::COP1)));
    requireRouting(
      EEOperation::AddSingleCOP1,
      EEInstructionCategory::COP1Operate,
      true,
      false,
      static_cast<std::uint8_t>(
        physical(EEPhysicalPipeline::I0) |
        physical(EEPhysicalPipeline::COP1)),
      0);
    requireRouting(
      EEOperation::QuadwordMoveToCOP2,
      EEInstructionCategory::COP2Move,
      false,
      true,
      0,
      static_cast<std::uint8_t>(
        physical(EEPhysicalPipeline::LoadStore) |
        physical(EEPhysicalPipeline::COP2)));
    requireRouting(
      EEOperation::VectorMacroArithmetic,
      EEInstructionCategory::COP2Operate,
      true,
      false,
      static_cast<std::uint8_t>(
        physical(EEPhysicalPipeline::I0) |
        physical(EEPhysicalPipeline::COP2)),
      0);
  }

  SECTION("Every implemented operation has a complete classification")
  {
    for (std::uint8_t value = 0;
         value < EE_OPERATION_COUNT;
         ++value)
    {
      const EEInstructionRouting routing =
        eeInstructionRouting(
          static_cast<EEOperation>(value));
      REQUIRE(routing.logicalPipes != 0);
      REQUIRE(
        (routing.pipe0PhysicalPipelines != 0) ==
        eeInstructionSupportsLogicalPipe(
          routing,
          EELogicalPipe::Pipe0));
      REQUIRE(
        (routing.pipe1PhysicalPipelines != 0) ==
        eeInstructionSupportsLogicalPipe(
          routing,
          EELogicalPipe::Pipe1));
    }
  }

  REQUIRE_THROWS_WITH(
    eeInstructionRouting(
      static_cast<EEOperation>(EE_OPERATION_COUNT)),
    "Unknown EE operation routing classification.");
}

TEST_CASE("Every EE operation has complete shared metadata")
{
  struct ExpectedExecutionClassification
  {
    EEExecutionFamily family;
    EEExecutionDispatch dispatch;
  };
  const auto expectedExecutionClassification =
    [](EEOperation operation)
    {
      switch (operation)
      {
        case EEOperation::Nop:
          return ExpectedExecutionClassification{
            EEExecutionFamily::NoOperation,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::SynchronizeLoadStore:
          return ExpectedExecutionClassification{
            EEExecutionFamily::LoadStoreSynchronization,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::SynchronizePipeline:
          return ExpectedExecutionClassification{
            EEExecutionFamily::PipelineSynchronization,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::ExceptionReturn:
          return ExpectedExecutionClassification{
            EEExecutionFamily::ExceptionReturn,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::SystemCall:
        case EEOperation::Breakpoint:
          return ExpectedExecutionClassification{
            EEExecutionFamily::SoftwareException,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::MoveWordFromCOP1:
        case EEOperation::MoveWordToCOP1:
        case EEOperation::MoveControlWordFromCOP1:
        case EEOperation::MoveControlWordToCOP1:
        case EEOperation::MoveSingleCOP1:
          return ExpectedExecutionClassification{
            EEExecutionFamily::COP1RegisterMove,
            EEExecutionDispatch::ManagedCOP1
          };
        case EEOperation::DivideSingleCOP1:
        case EEOperation::SquareRootSingleCOP1:
        case EEOperation::ReciprocalSquareRootSingleCOP1:
          return ExpectedExecutionClassification{
            EEExecutionFamily::COP1Divider,
            EEExecutionDispatch::ManagedCOP1
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
          return ExpectedExecutionClassification{
            EEExecutionFamily::COP1StagedOperation,
            EEExecutionDispatch::ManagedCOP1
          };
        case EEOperation::BranchCOP1False:
        case EEOperation::BranchCOP1FalseLikely:
        case EEOperation::BranchCOP1True:
        case EEOperation::BranchCOP1TrueLikely:
          return ExpectedExecutionClassification{
            EEExecutionFamily::COP1Branch,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::ShiftLeftLogicalWord:
        case EEOperation::ShiftRightLogicalWord:
        case EEOperation::ShiftRightArithmeticWord:
        case EEOperation::ShiftLeftLogicalVariableWord:
        case EEOperation::ShiftRightLogicalVariableWord:
        case EEOperation::ShiftRightArithmeticVariableWord:
          return ExpectedExecutionClassification{
            EEExecutionFamily::WordShift,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::ShiftLeftLogicalVariableDoubleword:
        case EEOperation::ShiftRightLogicalVariableDoubleword:
        case EEOperation::ShiftRightArithmeticVariableDoubleword:
        case EEOperation::ShiftLeftLogicalDoubleword:
        case EEOperation::ShiftRightLogicalDoubleword:
        case EEOperation::ShiftRightArithmeticDoubleword:
        case EEOperation::ShiftLeftLogicalDoubleword32:
        case EEOperation::ShiftRightLogicalDoubleword32:
        case EEOperation::ShiftRightArithmeticDoubleword32:
          return ExpectedExecutionClassification{
            EEExecutionFamily::DoublewordShift,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::AddWord:
        case EEOperation::AddUnsignedWord:
        case EEOperation::SubtractWord:
        case EEOperation::SubtractUnsignedWord:
          return ExpectedExecutionClassification{
            EEExecutionFamily::WordArithmetic,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::AddDoubleword:
        case EEOperation::AddUnsignedDoubleword:
        case EEOperation::SubtractDoubleword:
        case EEOperation::SubtractUnsignedDoubleword:
          return ExpectedExecutionClassification{
            EEExecutionFamily::DoublewordArithmetic,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::And:
        case EEOperation::Or:
        case EEOperation::Xor:
        case EEOperation::Nor:
          return ExpectedExecutionClassification{
            EEExecutionFamily::RegisterLogical,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::SetLessThan:
        case EEOperation::SetLessThanUnsigned:
          return ExpectedExecutionClassification{
            EEExecutionFamily::RegisterCompare,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::AddImmediateWord:
        case EEOperation::AddImmediateUnsignedWord:
          return ExpectedExecutionClassification{
            EEExecutionFamily::ImmediateWordArithmetic,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::AddImmediateDoubleword:
        case EEOperation::AddImmediateUnsignedDoubleword:
          return ExpectedExecutionClassification{
            EEExecutionFamily::ImmediateDoublewordArithmetic,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::SetLessThanImmediate:
        case EEOperation::SetLessThanImmediateUnsigned:
          return ExpectedExecutionClassification{
            EEExecutionFamily::ImmediateCompare,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::AndImmediate:
        case EEOperation::OrImmediate:
        case EEOperation::XorImmediate:
        case EEOperation::LoadUpperImmediate:
          return ExpectedExecutionClassification{
            EEExecutionFamily::ImmediateLogical,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::MoveFromHI:
        case EEOperation::MoveToHI:
        case EEOperation::MoveFromLO:
        case EEOperation::MoveToLO:
        case EEOperation::MoveFromHI1:
        case EEOperation::MoveToHI1:
        case EEOperation::MoveFromLO1:
        case EEOperation::MoveToLO1:
          return ExpectedExecutionClassification{
            EEExecutionFamily::MACRegisterMove,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::MoveFromShiftAmount:
        case EEOperation::MoveToShiftAmount:
        case EEOperation::MoveByteCountToShiftAmount:
        case EEOperation::MoveHalfwordCountToShiftAmount:
          return ExpectedExecutionClassification{
            EEExecutionFamily::ShiftAmountOperation,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::LoadByte:
        case EEOperation::LoadByteUnsigned:
        case EEOperation::StoreByte:
          return ExpectedExecutionClassification{
            EEExecutionFamily::ByteMemory,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::LoadHalfword:
        case EEOperation::LoadHalfwordUnsigned:
        case EEOperation::StoreHalfword:
          return ExpectedExecutionClassification{
            EEExecutionFamily::HalfwordMemory,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::LoadWord:
        case EEOperation::LoadWordUnsigned:
        case EEOperation::StoreWord:
          return ExpectedExecutionClassification{
            EEExecutionFamily::WordMemory,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::LoadWordLeft:
        case EEOperation::LoadWordRight:
        case EEOperation::StoreWordLeft:
        case EEOperation::StoreWordRight:
          return ExpectedExecutionClassification{
            EEExecutionFamily::WordMergeMemory,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::LoadDoubleword:
        case EEOperation::StoreDoubleword:
          return ExpectedExecutionClassification{
            EEExecutionFamily::DoublewordMemory,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::LoadDoublewordLeft:
        case EEOperation::LoadDoublewordRight:
        case EEOperation::StoreDoublewordLeft:
        case EEOperation::StoreDoublewordRight:
          return ExpectedExecutionClassification{
            EEExecutionFamily::DoublewordMergeMemory,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::LoadQuadword:
        case EEOperation::StoreQuadword:
          return ExpectedExecutionClassification{
            EEExecutionFamily::QuadwordMemory,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::LoadWordToCOP1:
        case EEOperation::StoreWordFromCOP1:
          return ExpectedExecutionClassification{
            EEExecutionFamily::COP1Memory,
            EEExecutionDispatch::ManagedCOP1
          };
        case EEOperation::LoadQuadwordToCOP2:
        case EEOperation::StoreQuadwordFromCOP2:
          return ExpectedExecutionClassification{
            EEExecutionFamily::COP2Memory,
            EEExecutionDispatch::COP2Coupled
          };
        case EEOperation::QuadwordMoveFromCOP2:
        case EEOperation::QuadwordMoveToCOP2:
          return ExpectedExecutionClassification{
            EEExecutionFamily::COP2VectorMove,
            EEExecutionDispatch::COP2Coupled
          };
        case EEOperation::ControlMoveFromCOP2:
        case EEOperation::ControlMoveToCOP2:
          return ExpectedExecutionClassification{
            EEExecutionFamily::COP2ControlMove,
            EEExecutionDispatch::COP2Coupled
          };
        case EEOperation::BranchCOP2False:
        case EEOperation::BranchCOP2FalseLikely:
        case EEOperation::BranchCOP2True:
        case EEOperation::BranchCOP2TrueLikely:
          return ExpectedExecutionClassification{
            EEExecutionFamily::COP2Branch,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::VectorCallMicroSubroutine:
        case EEOperation::VectorCallMicroSubroutineRegister:
          return ExpectedExecutionClassification{
            EEExecutionFamily::COP2MicroCall,
            EEExecutionDispatch::COP2Coupled
          };
        case EEOperation::VectorMacroArithmetic:
          return ExpectedExecutionClassification{
            EEExecutionFamily::COP2Macro,
            EEExecutionDispatch::COP2Coupled
          };
        case EEOperation::Jump:
        case EEOperation::JumpAndLink:
        case EEOperation::JumpRegister:
        case EEOperation::JumpAndLinkRegister:
          return ExpectedExecutionClassification{
            EEExecutionFamily::Jump,
            EEExecutionDispatch::Immediate
          };
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
          return ExpectedExecutionClassification{
            EEExecutionFamily::IntegerBranch,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::MultiplyWord:
        case EEOperation::MultiplyUnsignedWord:
        case EEOperation::MultiplyAddWord:
        case EEOperation::MultiplyAddUnsignedWord:
          return ExpectedExecutionClassification{
            EEExecutionFamily::Multiply,
            EEExecutionDispatch::MAC0Continuation
          };
        case EEOperation::MultiplyWord1:
        case EEOperation::MultiplyUnsignedWord1:
        case EEOperation::MultiplyAddWord1:
        case EEOperation::MultiplyAddUnsignedWord1:
          return ExpectedExecutionClassification{
            EEExecutionFamily::Multiply,
            EEExecutionDispatch::MAC1Continuation
          };
        case EEOperation::DivideWord:
        case EEOperation::DivideUnsignedWord:
          return ExpectedExecutionClassification{
            EEExecutionFamily::Divide,
            EEExecutionDispatch::MAC0Continuation
          };
        case EEOperation::DivideWord1:
        case EEOperation::DivideUnsignedWord1:
          return ExpectedExecutionClassification{
            EEExecutionFamily::Divide,
            EEExecutionDispatch::MAC1Continuation
          };
        case EEOperation::ParallelAnd:
        case EEOperation::ParallelOr:
        case EEOperation::ParallelXor:
        case EEOperation::ParallelNor:
          return ExpectedExecutionClassification{
            EEExecutionFamily::PackedLogical,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::ParallelCompareEqualByte:
        case EEOperation::ParallelCompareEqualHalfword:
        case EEOperation::ParallelCompareEqualWord:
        case EEOperation::ParallelCompareGreaterThanByte:
        case EEOperation::ParallelCompareGreaterThanHalfword:
        case EEOperation::ParallelCompareGreaterThanWord:
        case EEOperation::ParallelMaximumHalfword:
        case EEOperation::ParallelMaximumWord:
        case EEOperation::ParallelMinimumHalfword:
        case EEOperation::ParallelMinimumWord:
          return ExpectedExecutionClassification{
            EEExecutionFamily::PackedCompare,
            EEExecutionDispatch::Immediate
          };
        case EEOperation::Count:
          break;
      }
      return ExpectedExecutionClassification{
        EEExecutionFamily::Unclassified,
        EEExecutionDispatch::Unclassified
      };
    };
  const auto expectedCOP1ResultDestination =
    [](EEOperation operation)
    {
      switch (operation)
      {
        case EEOperation::CompareFalseSingleCOP1:
        case EEOperation::CompareEqualSingleCOP1:
        case EEOperation::CompareLessThanSingleCOP1:
        case EEOperation::CompareLessThanOrEqualSingleCOP1:
          return EECOP1ResultDestination::Condition;
        case EEOperation::AddSingleToAccumulatorCOP1:
        case EEOperation::SubtractSingleToAccumulatorCOP1:
        case EEOperation::MultiplySingleToAccumulatorCOP1:
        case EEOperation::MultiplyAddSingleToAccumulatorCOP1:
        case EEOperation::MultiplySubtractSingleToAccumulatorCOP1:
          return EECOP1ResultDestination::Accumulator;
        case EEOperation::AbsoluteSingleCOP1:
        case EEOperation::NegateSingleCOP1:
        case EEOperation::MaximumSingleCOP1:
        case EEOperation::MinimumSingleCOP1:
        case EEOperation::ConvertWordToSingleCOP1:
        case EEOperation::ConvertSingleToWordCOP1:
        case EEOperation::AddSingleCOP1:
        case EEOperation::SubtractSingleCOP1:
        case EEOperation::MultiplySingleCOP1:
        case EEOperation::MultiplyAddSingleCOP1:
        case EEOperation::MultiplySubtractSingleCOP1:
        case EEOperation::DivideSingleCOP1:
        case EEOperation::SquareRootSingleCOP1:
        case EEOperation::ReciprocalSquareRootSingleCOP1:
          return EECOP1ResultDestination::FPR;
        default:
          return EECOP1ResultDestination::None;
      }
    };
  const auto requireOperationMetadata =
    [](EEOperation operation,
       EEMemoryAccess memoryAccess,
       EECOP1OperationFamily cop1Family,
       bool cop1ManagedPipeline,
       std::uint8_t dividerLatency = 0,
       std::uint8_t dividerInitiationInterval = 0)
    {
      const EEOperationMetadata metadata =
        eeOperationMetadata(operation);
      REQUIRE(metadata.memoryAccess == memoryAccess);
      REQUIRE(metadata.cop1Family == cop1Family);
      REQUIRE(
        metadata.cop1ManagedPipeline ==
        cop1ManagedPipeline);
      REQUIRE(
        metadata.cop1DividerLatency ==
        dividerLatency);
      REQUIRE(
        metadata.cop1DividerInitiationInterval ==
        dividerInitiationInterval);
    };

  requireOperationMetadata(
    EEOperation::AddWord,
    EEMemoryAccess::None,
    EECOP1OperationFamily::None,
    false);
  requireOperationMetadata(
    EEOperation::LoadWord,
    EEMemoryAccess::Load,
    EECOP1OperationFamily::None,
    false);
  requireOperationMetadata(
    EEOperation::StoreWord,
    EEMemoryAccess::Store,
    EECOP1OperationFamily::None,
    false);
  requireOperationMetadata(
    EEOperation::BranchCOP1True,
    EEMemoryAccess::None,
    EECOP1OperationFamily::ConditionBranch,
    false);
  requireOperationMetadata(
    EEOperation::MoveWordFromCOP1,
    EEMemoryAccess::None,
    EECOP1OperationFamily::RegisterMove,
    true);
  requireOperationMetadata(
    EEOperation::LoadWordToCOP1,
    EEMemoryAccess::Load,
    EECOP1OperationFamily::MemoryMove,
    true);
  requireOperationMetadata(
    EEOperation::AddSingleCOP1,
    EEMemoryAccess::None,
    EECOP1OperationFamily::AddSubtract,
    true);
  requireOperationMetadata(
    EEOperation::DivideSingleCOP1,
    EEMemoryAccess::None,
    EECOP1OperationFamily::Divider,
    true,
    8,
    7);
  requireOperationMetadata(
    EEOperation::ReciprocalSquareRootSingleCOP1,
    EEMemoryAccess::None,
    EECOP1OperationFamily::Divider,
    true,
    14,
    13);
  REQUIRE(
    eeOperationMetadata(
      EEOperation::MultiplyAddSingleToAccumulatorCOP1)
      .updatesCOP1ArithmeticFlags);
  REQUIRE(
    eeOperationMetadata(
      EEOperation::AddSingleToAccumulatorCOP1)
      .updatesCOP1ArithmeticFlags);
  REQUIRE(
    eeOperationMetadata(
      EEOperation::AddSingleToAccumulatorCOP1)
      .cop1ResultDestination ==
    EECOP1ResultDestination::Accumulator);
  REQUIRE_FALSE(
    eeOperationMetadata(
      EEOperation::ConvertWordToSingleCOP1)
      .updatesCOP1ArithmeticFlags);
  REQUIRE(
    eeOperationMetadata(
      EEOperation::DivideSingleCOP1)
      .cop1ResultDestination ==
    EECOP1ResultDestination::FPR);
  REQUIRE_FALSE(
    eeOperationMetadata(
      EEOperation::CompareEqualSingleCOP1)
      .updatesCOP1ArithmeticFlags);
  REQUIRE(
    eeOperationMetadata(
      EEOperation::CompareEqualSingleCOP1)
      .cop1ResultDestination ==
    EECOP1ResultDestination::Condition);

  for (std::uint8_t value = 0;
       value < EE_OPERATION_COUNT;
       ++value)
  {
    const EEOperation operation =
      static_cast<EEOperation>(value);
    const EEOperationMetadata metadata =
      eeOperationMetadata(operation);
    const ExpectedExecutionClassification
      expectedExecution =
        expectedExecutionClassification(operation);
    INFO("EE operation " << static_cast<unsigned>(value));

    REQUIRE(metadata.routing.logicalPipes != 0);
    REQUIRE(
      metadata.executionFamily !=
      EEExecutionFamily::Unclassified);
    REQUIRE(
      metadata.executionFamily ==
      expectedExecution.family);
    REQUIRE(
      metadata.executionDispatch !=
      EEExecutionDispatch::Unclassified);
    REQUIRE(
      metadata.executionDispatch ==
      expectedExecution.dispatch);
    if (metadata.routing.category ==
        EEInstructionCategory::LoadStore)
    {
      REQUIRE(
        metadata.memoryAccess != EEMemoryAccess::None);
    }
    if (metadata.memoryAccess != EEMemoryAccess::None)
    {
      REQUIRE(
        (metadata.routing.category ==
           EEInstructionCategory::LoadStore ||
         metadata.cop1Family ==
           EECOP1OperationFamily::MemoryMove ||
         metadata.routing.category ==
           EEInstructionCategory::COP2Move));
    }
    if (metadata.routing.category ==
          EEInstructionCategory::COP1Move ||
        metadata.routing.category ==
          EEInstructionCategory::COP1Operate)
    {
      REQUIRE(
        metadata.cop1Family !=
        EECOP1OperationFamily::None);
    }
    REQUIRE(
      (metadata.cop1Family ==
       EECOP1OperationFamily::Divider) ==
      (metadata.cop1DividerLatency != 0));
    REQUIRE(
      (metadata.cop1Family ==
       EECOP1OperationFamily::Divider) ==
      (metadata.cop1DividerInitiationInterval != 0));
    REQUIRE(
      metadata.cop1ManagedPipeline ==
      (metadata.cop1Family !=
         EECOP1OperationFamily::None &&
       metadata.cop1Family !=
         EECOP1OperationFamily::ConditionBranch));
    REQUIRE(
      metadata.cop1ResultDestination ==
      expectedCOP1ResultDestination(operation));

    EEInstruction instruction;
    instruction.operation = operation;
    instruction.sourceRegister = 1;
    instruction.targetRegister = 2;
    instruction.destinationRegister = 3;
    instruction.shiftAmount = 4;
    const EEInstructionMetadata instructionMetadata =
      eeInstructionMetadata(instruction);
    REQUIRE(
      instructionMetadata.operation.routing.category ==
      metadata.routing.category);
    REQUIRE(
      instructionMetadata.operation.memoryAccess ==
      metadata.memoryAccess);
    REQUIRE(
      instructionMetadata.operation.executionDispatch ==
      metadata.executionDispatch);
    REQUIRE(
      instructionMetadata.operation.cop1Family ==
      metadata.cop1Family);
    REQUIRE(
      instructionMetadata.operation.cop1ManagedPipeline ==
      metadata.cop1ManagedPipeline);
  }

  const EEOperation invalidOperation =
    static_cast<EEOperation>(EE_OPERATION_COUNT);
  REQUIRE_THROWS_WITH(
    eeOperationMetadata(invalidOperation),
    "Unknown EE operation metadata.");
  REQUIRE_THROWS_WITH(
    eeOperationMetadata(
      static_cast<EEOperation>(UINT8_MAX)),
    "Unknown EE operation metadata.");

  EEInstruction invalidInstruction;
  invalidInstruction.operation = invalidOperation;
  REQUIRE_THROWS_WITH(
    eeInstructionMetadata(invalidInstruction),
    "Unknown EE operation metadata.");

  REQUIRE_THROWS_WITH(
    eeInstructionDependencies(invalidInstruction),
    "Unknown EE operation metadata.");
}

TEST_CASE("EE pair pipe assignment preserves program order")
{
  const auto requireAssignment =
    [](EEOperation older,
       EEOperation younger,
       EELogicalPipe olderPipe,
       EELogicalPipe youngerPipe)
    {
      const EEInstructionPipeAssignment assignment =
        assignEEInstructionPairPipes(older, younger);
      REQUIRE(assignment.assignable);
      REQUIRE(assignment.olderPipe == olderPipe);
      REQUIRE(assignment.youngerPipe == youngerPipe);
    };

  SECTION("Flexible pairs prefer ascending logical pipe order")
  {
    requireAssignment(
      EEOperation::AddWord,
      EEOperation::BranchEqual,
      EELogicalPipe::Pipe0,
      EELogicalPipe::Pipe1);
  }

  SECTION("A fixed younger instruction redirects an older flexible one")
  {
    requireAssignment(
      EEOperation::AddWord,
      EEOperation::MoveToShiftAmount,
      EELogicalPipe::Pipe1,
      EELogicalPipe::Pipe0);
  }

  SECTION("A fixed older instruction redirects a younger flexible one")
  {
    requireAssignment(
      EEOperation::LoadWord,
      EEOperation::AddWord,
      EELogicalPipe::Pipe1,
      EELogicalPipe::Pipe0);
  }

  SECTION("Opposing fixed pipes may pair in either program order")
  {
    requireAssignment(
      EEOperation::MoveWordFromCOP1,
      EEOperation::AddSingleCOP1,
      EELogicalPipe::Pipe1,
      EELogicalPipe::Pipe0);
    requireAssignment(
      EEOperation::AddSingleCOP1,
      EEOperation::MoveWordFromCOP1,
      EELogicalPipe::Pipe0,
      EELogicalPipe::Pipe1);
  }

  SECTION("Instructions fixed to the same pipe cannot be assigned")
  {
    REQUIRE_FALSE(
      assignEEInstructionPairPipes(
        EEOperation::AddSingleCOP1,
        EEOperation::MoveToShiftAmount).assignable);
    REQUIRE_FALSE(
      assignEEInstructionPairPipes(
        EEOperation::LoadWord,
        EEOperation::MoveWordFromCOP1).assignable);
  }
}

TEST_CASE("EE two-wide issue selection")
{
  SECTION("Independent flexible instructions issue together")
  {
    const EEIssueSelection selection =
      selectEEIssuePair(
        decodeEEInstruction(
          immediateInstruction(0x09, 0, 2, 1)),
        decodeEEInstruction(
          immediateInstruction(0x09, 0, 3, 2)));

    REQUIRE(selection.instructionCount == 2);
    REQUIRE(
      selection.pairing ==
      EEIssuePairing::Concurrent);
    REQUIRE(
      selection.assignment.olderPipe ==
      EELogicalPipe::Pipe0);
    REQUIRE(
      selection.assignment.youngerPipe ==
      EELogicalPipe::Pipe1);
  }

  SECTION("Reverse fixed-pipe order remains pairable")
  {
    const EEIssueSelection selection =
      selectEEIssuePair(
        decodeEEInstruction(
          immediateInstruction(0x23, 1, 2, 0)),
        decodeEEInstruction(UINT32_C(0x46031040)));

    REQUIRE(selection.instructionCount == 2);
    REQUIRE(
      selection.assignment.olderPipe ==
      EELogicalPipe::Pipe1);
    REQUIRE(
      selection.assignment.youngerPipe ==
      EELogicalPipe::Pipe0);
  }

  SECTION("Same-pair GPR dependencies fall back to the older instruction")
  {
    const EEInstruction producer =
      decodeEEInstruction(
        immediateInstruction(0x09, 0, 2, 1));

    REQUIRE(
      selectEEIssuePair(
        producer,
        decodeEEInstruction(
          immediateInstruction(0x09, 2, 3, 1)))
        .instructionCount == 1);
    REQUIRE(
      selectEEIssuePair(
        producer,
        decodeEEInstruction(
          immediateInstruction(0x09, 0, 2, 2)))
        .instructionCount == 1);
  }

  SECTION("FCR31 condition aliases prevent same-pair issue")
  {
    REQUIRE(
      selectEEIssuePair(
        decodeEEInstruction(UINT32_C(0x46031032)),
        decodeEEInstruction(UINT32_C(0x4442f800)))
        .instructionCount == 1);
    REQUIRE(
      selectEEIssuePair(
        decodeEEInstruction(UINT32_C(0x44c2f800)),
        decodeEEInstruction(UINT32_C(0x45010001)))
        .instructionCount == 1);
  }

  SECTION("Conservative COP2 state blocks dependent transfer pairs")
  {
    REQUIRE(
      selectEEIssuePair(
        decodeEEInstruction(UINT32_C(0x4a0002ff)),
        decodeEEInstruction(UINT32_C(0x48220800)))
        .instructionCount == 1);
    REQUIRE(
      selectEEIssuePair(
        decodeEEInstruction(UINT32_C(0x48a20800)),
        decodeEEInstruction(UINT32_C(0x4a0002ff)))
        .instructionCount == 1);
  }

  SECTION("Table 1-3 forbidden pairs fall back to one instruction")
  {
    const EEIssueSelection selection =
      selectEEIssuePair(
        decodeEEInstruction(
          immediateInstruction(0x04, 1, 2, 1)),
        decodeEEInstruction(
          immediateInstruction(0x05, 3, 4, 1)));

    REQUIRE(selection.instructionCount == 1);
    REQUIRE(
      selection.pairing ==
      EEIssuePairing::Forbidden);
  }

  SECTION("Legal branches may issue with their delay-slot instruction")
  {
    REQUIRE(
      selectEEIssuePair(
        decodeEEInstruction(
          immediateInstruction(0x04, 1, 2, 1)),
        decodeEEInstruction(
          immediateInstruction(0x09, 0, 3, 1)))
        .instructionCount == 2);
    REQUIRE(
      selectEEIssuePair(
        decodeEEInstruction(
          immediateInstruction(0x04, 1, 2, 1)),
        decodeEEInstruction(
          registerInstruction(0x29, 4, 0, 0)))
        .instructionCount == 2);
    REQUIRE(
      selectEEIssuePair(
        decodeEEInstruction(
          immediateInstruction(0x14, 1, 2, 1)),
        decodeEEInstruction(
          registerInstruction(0x28, 0, 0, 3)))
        .instructionCount == 2);
  }

  SECTION("Illegal delay-slot sequences remain scalar for validation")
  {
    const EEInstruction branch =
      decodeEEInstruction(
        immediateInstruction(0x04, 1, 2, 1));
    const EEInstruction likely =
      decodeEEInstruction(
        immediateInstruction(0x14, 1, 2, 1));
    const EEInstruction forbiddenYounger[] = {
      decodeEEInstruction(
        immediateInstruction(0x05, 3, 4, 1)),
      decodeEEInstruction(UINT32_C(0x42000018)),
      decodeEEInstruction(UINT32_C(0x0000000f)),
      decodeEEInstruction(UINT32_C(0x0000040f))
    };
    for (const EEInstruction &younger : forbiddenYounger)
    {
      REQUIRE(
        selectEEIssuePair(
          branch,
          younger).instructionCount == 1);
    }

    const EEInstruction forbiddenLikelyYounger[] = {
      decodeEEInstruction(
        registerInstruction(0x29, 5, 0, 0)),
      decodeEEInstruction(
        regimmInstruction(0x18, 5, 0)),
      decodeEEInstruction(
        regimmInstruction(0x19, 5, 0))
    };
    for (const EEInstruction &younger :
         forbiddenLikelyYounger)
    {
      REQUIRE(
        selectEEIssuePair(
          likely,
          younger).instructionCount == 1);
    }
  }

  SECTION("A no-delay-slot redirect cannot admit its successor")
  {
    const EEInstruction eret =
      decodeEEInstruction(UINT32_C(0x42000018));
    const EEInstruction alu =
      decodeEEInstruction(
        immediateInstruction(0x09, 0, 3, 1));

    REQUIRE(
      selectEEIssuePair(
        eret,
        alu).instructionCount == 1);
    REQUIRE(
      selectEEIssuePair(
        alu,
        eret).instructionCount == 2);
  }

  SECTION("Table 1-3 delayed pairs remain two-wide selections")
  {
    const EEIssueSelection selection =
      selectEEIssuePair(
        decodeEEInstruction(UINT32_C(0x44020800)),
        decodeEEInstruction(UINT32_C(0x46031000)));

    REQUIRE(selection.instructionCount == 2);
    REQUIRE(
      selection.pairing ==
      EEIssuePairing::ConcurrentWithStall);
    REQUIRE(
      selection.continuation ==
      EEIssueContinuation::None);
  }

  SECTION("Scalar selection assigns the older instruction")
  {
    const EEInstruction older =
      decodeEEInstruction(
        immediateInstruction(0x09, 0, 2, 1));

    REQUIRE(selectEESingleIssue(older).instructionCount == 1);
  }

  SECTION("Failed pairing retains the older scalar pipe")
  {
    const EEIssueSelection selection =
      selectEEIssuePair(
        decodeEEInstruction(
          immediateInstruction(0x23, 1, 2, 0)),
        decodeEEInstruction(UINT32_C(0x44020800)));

    REQUIRE(selection.instructionCount == 1);
    REQUIRE_FALSE(selection.assignment.assignable);
    REQUIRE(
      selection.assignment.olderPipe ==
      EELogicalPipe::Pipe1);
  }
}

TEST_CASE("EE issue-pair continuation policy")
{
  const auto requirePolicy =
    [](EEInstructionCategory older,
       EELogicalPipe olderPipe,
       EEInstructionCategory younger,
       EELogicalPipe youngerPipe,
       EEIssuePairing pairing,
       EEIssueContinuation continuation)
    {
      const EEIssuePairPolicy policy =
        eeIssuePairPolicy(
          older,
          olderPipe,
          younger,
          youngerPipe);
      REQUIRE(policy.pairing == pairing);
      REQUIRE(policy.continuation == continuation);
    };

  for (const EEInstructionCategory younger : {
         EEInstructionCategory::LeadingZeroCount,
         EEInstructionCategory::ALU,
         EEInstructionCategory::MAC1})
  {
    requirePolicy(
      EEInstructionCategory::WideOperate,
      EELogicalPipe::Pipe0,
      younger,
      EELogicalPipe::Pipe1,
      EEIssuePairing::ConcurrentWithStall,
      EEIssueContinuation::YoungerAStageOneCycle);
  }

  requirePolicy(
    EEInstructionCategory::ALU,
    EELogicalPipe::Pipe1,
    EEInstructionCategory::WideOperate,
    EELogicalPipe::Pipe0,
    EEIssuePairing::ConcurrentWithStall,
    EEIssueContinuation::None);
  requirePolicy(
    EEInstructionCategory::COP1Operate,
    EELogicalPipe::Pipe0,
    EEInstructionCategory::COP1Move,
    EELogicalPipe::Pipe1,
    EEIssuePairing::ConcurrentWithStall,
    EEIssueContinuation::None);
  requirePolicy(
    EEInstructionCategory::COP2Operate,
    EELogicalPipe::Pipe0,
    EEInstructionCategory::COP2Move,
    EELogicalPipe::Pipe1,
    EEIssuePairing::ConcurrentWithStall,
    EEIssueContinuation::None);
  requirePolicy(
    EEInstructionCategory::Branch,
    EELogicalPipe::Pipe0,
    EEInstructionCategory::Branch,
    EELogicalPipe::Pipe1,
    EEIssuePairing::Forbidden,
    EEIssueContinuation::None);
}

TEST_CASE("EE delay-slot legality is centralized")
{
  const EEInstruction branch =
    decodeEEInstruction(
      immediateInstruction(0x04, 1, 2, 1));
  const EEInstruction likely =
    decodeEEInstruction(
      immediateInstruction(0x14, 1, 2, 1));
  const EEInstruction alu =
    decodeEEInstruction(
      immediateInstruction(0x09, 0, 3, 1));
  const EEInstruction shiftAmountWrite =
    decodeEEInstruction(
      registerInstruction(0x29, 4, 0, 0));

  REQUIRE(isEEDelaySlotInstructionLegal(branch, alu));
  REQUIRE(
    isEEDelaySlotInstructionLegal(
      branch,
      shiftAmountWrite));
  REQUIRE_FALSE(
    isEEDelaySlotInstructionLegal(
      likely,
      shiftAmountWrite));
  REQUIRE_FALSE(
    isEEDelaySlotInstructionLegal(
      branch,
      decodeEEInstruction(
        immediateInstruction(0x05, 3, 4, 1))));
  REQUIRE_FALSE(
    isEEDelaySlotInstructionLegal(
      branch,
      decodeEEInstruction(UINT32_C(0x42000018))));
  REQUIRE_FALSE(
    isEEDelaySlotInstructionLegal(
      branch,
      decodeEEInstruction(UINT32_C(0x0000000f))));
  REQUIRE_THROWS_AS(
    isEEDelaySlotInstructionLegal(alu, alu),
    std::invalid_argument);
}

TEST_CASE("EE base integer decoder tables")
{
  struct RegisterContract
  {
    std::uint8_t function;
    EEOperation operation;
    bool immediateShift;
  };
  const RegisterContract registerContracts[] = {
    {0x00, EEOperation::ShiftLeftLogicalWord, true},
    {0x02, EEOperation::ShiftRightLogicalWord, true},
    {0x03, EEOperation::ShiftRightArithmeticWord, true},
    {0x04, EEOperation::ShiftLeftLogicalVariableWord, false},
    {0x06, EEOperation::ShiftRightLogicalVariableWord, false},
    {0x07, EEOperation::ShiftRightArithmeticVariableWord, false},
    {0x0c, EEOperation::SystemCall, false},
    {0x0d, EEOperation::Breakpoint, false},
    {0x14, EEOperation::ShiftLeftLogicalVariableDoubleword, false},
    {0x16, EEOperation::ShiftRightLogicalVariableDoubleword, false},
    {0x17, EEOperation::ShiftRightArithmeticVariableDoubleword, false},
    {0x20, EEOperation::AddWord, false},
    {0x21, EEOperation::AddUnsignedWord, false},
    {0x22, EEOperation::SubtractWord, false},
    {0x23, EEOperation::SubtractUnsignedWord, false},
    {0x24, EEOperation::And, false},
    {0x25, EEOperation::Or, false},
    {0x26, EEOperation::Xor, false},
    {0x27, EEOperation::Nor, false},
    {0x2a, EEOperation::SetLessThan, false},
    {0x2b, EEOperation::SetLessThanUnsigned, false},
    {0x2c, EEOperation::AddDoubleword, false},
    {0x2d, EEOperation::AddUnsignedDoubleword, false},
    {0x2e, EEOperation::SubtractDoubleword, false},
    {0x2f, EEOperation::SubtractUnsignedDoubleword, false},
    {0x38, EEOperation::ShiftLeftLogicalDoubleword, true},
    {0x3a, EEOperation::ShiftRightLogicalDoubleword, true},
    {0x3b, EEOperation::ShiftRightArithmeticDoubleword, true},
    {0x3c, EEOperation::ShiftLeftLogicalDoubleword32, true},
    {0x3e, EEOperation::ShiftRightLogicalDoubleword32, true},
    {0x3f, EEOperation::ShiftRightArithmeticDoubleword32, true}
  };

  for (const RegisterContract &contract : registerContracts)
  {
    const std::uint8_t rs = contract.immediateShift ? 0 : 1;
    const std::uint8_t shift = contract.immediateShift ? 7 : 0;
    const EEInstruction instruction = decodeEEInstruction(
      registerInstruction(
        contract.function,
        rs,
        2,
        3,
        shift));
    REQUIRE(instruction.operation == contract.operation);
  }

  struct ImmediateContract
  {
    std::uint8_t opcode;
    EEOperation operation;
  };
  const ImmediateContract immediateContracts[] = {
    {0x08, EEOperation::AddImmediateWord},
    {0x09, EEOperation::AddImmediateUnsignedWord},
    {0x0a, EEOperation::SetLessThanImmediate},
    {0x0b, EEOperation::SetLessThanImmediateUnsigned},
    {0x0c, EEOperation::AndImmediate},
    {0x0d, EEOperation::OrImmediate},
    {0x0e, EEOperation::XorImmediate},
    {0x0f, EEOperation::LoadUpperImmediate},
    {0x18, EEOperation::AddImmediateDoubleword},
    {0x19, EEOperation::AddImmediateUnsignedDoubleword},
    {0x1a, EEOperation::LoadDoublewordLeft},
    {0x1b, EEOperation::LoadDoublewordRight},
    {0x1e, EEOperation::LoadQuadword},
    {0x1f, EEOperation::StoreQuadword},
    {0x2c, EEOperation::StoreDoublewordLeft},
    {0x2d, EEOperation::StoreDoublewordRight}
  };

  for (const ImmediateContract &contract : immediateContracts)
  {
    const std::uint8_t rs = contract.opcode == 0x0f ? 0 : 1;
    const EEInstruction instruction = decodeEEInstruction(
      immediateInstruction(
        contract.opcode,
        rs,
        2,
        0x3456));
    REQUIRE(instruction.operation == contract.operation);
  }
}

TEST_CASE("EE decoder rejects invalid and deferred encodings")
{
  SECTION("Reserved primary opcodes are distinguished")
  {
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(UINT32_C(0x4c000000)),
      "Reserved EE instruction encoding.");
  }

  SECTION("Architecturally unsupported primary opcodes are rejected")
  {
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(UINT32_C(0xc0000000)),
      "Unsupported EE instruction encoding.");
  }

  SECTION("Deferred valid instruction families are rejected explicitly")
  {
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(UINT32_C(0xbc000000)),
      "Unsupported EE instruction encoding.");
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(UINT32_C(0x40000000)),
      "Unsupported EE instruction encoding.");
  }

  SECTION("Reserved SPECIAL functions are distinguished")
  {
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(registerInstruction(0x01, 0, 0, 0)),
      "Reserved EE instruction encoding.");
  }

  SECTION("Fixed zero fields are validated")
  {
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(
        registerInstruction(0x00, 1, 2, 3, 4)),
      "Reserved EE instruction encoding.");
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(
        registerInstruction(0x20, 1, 2, 3, 4)),
      "Reserved EE instruction encoding.");
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(
        immediateInstruction(0x0f, 1, 2, 3)),
      "Reserved EE instruction encoding.");
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(UINT32_C(0x0000080f)),
      "Reserved EE instruction encoding.");
    REQUIRE_THROWS_WITH(
      decodeEEInstruction(UINT32_C(0x0010000f)),
      "Reserved EE instruction encoding.");
  }
}

TEST_CASE("EE ERET instruction decoding")
{
  const EEInstruction instruction =
    decodeEEInstruction(UINT32_C(0x42000018));

  REQUIRE(instruction.operation == EEOperation::ExceptionReturn);
  REQUIRE(instruction.raw == UINT32_C(0x42000018));
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(UINT32_C(0x42000019)),
    "Reserved EE instruction encoding.");
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(UINT32_C(0x42000058)),
    "Reserved EE instruction encoding.");
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(UINT32_C(0x42000001)),
    "Unsupported EE instruction encoding.");
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(UINT32_C(0x40000001)),
    "Reserved EE instruction encoding.");
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(UINT32_C(0x41040000)),
    "Reserved EE instruction encoding.");
}

TEST_CASE("EE multiply divide and SA decoder tables")
{
  struct Contract
  {
    std::uint32_t instruction;
    EEOperation operation;
  };
  const Contract contracts[] = {
    {registerInstruction(0x10, 0, 0, 3), EEOperation::MoveFromHI},
    {registerInstruction(0x11, 1, 0, 0), EEOperation::MoveToHI},
    {registerInstruction(0x12, 0, 0, 3), EEOperation::MoveFromLO},
    {registerInstruction(0x13, 1, 0, 0), EEOperation::MoveToLO},
    {registerInstruction(0x18, 1, 2, 3), EEOperation::MultiplyWord},
    {registerInstruction(0x19, 1, 2, 3), EEOperation::MultiplyUnsignedWord},
    {registerInstruction(0x1a, 1, 2, 0), EEOperation::DivideWord},
    {registerInstruction(0x1b, 1, 2, 0), EEOperation::DivideUnsignedWord},
    {registerInstruction(0x28, 0, 0, 3), EEOperation::MoveFromShiftAmount},
    {registerInstruction(0x29, 1, 0, 0), EEOperation::MoveToShiftAmount},
    {UINT32_C(0x70000000) | registerInstruction(0x00, 1, 2, 3), EEOperation::MultiplyAddWord},
    {UINT32_C(0x70000000) | registerInstruction(0x01, 1, 2, 3), EEOperation::MultiplyAddUnsignedWord},
    {UINT32_C(0x70000000) | registerInstruction(0x10, 0, 0, 3), EEOperation::MoveFromHI1},
    {UINT32_C(0x70000000) | registerInstruction(0x11, 1, 0, 0), EEOperation::MoveToHI1},
    {UINT32_C(0x70000000) | registerInstruction(0x12, 0, 0, 3), EEOperation::MoveFromLO1},
    {UINT32_C(0x70000000) | registerInstruction(0x13, 1, 0, 0), EEOperation::MoveToLO1},
    {UINT32_C(0x70000000) | registerInstruction(0x18, 1, 2, 3), EEOperation::MultiplyWord1},
    {UINT32_C(0x70000000) | registerInstruction(0x19, 1, 2, 3), EEOperation::MultiplyUnsignedWord1},
    {UINT32_C(0x70000000) | registerInstruction(0x1a, 1, 2, 0), EEOperation::DivideWord1},
    {UINT32_C(0x70000000) | registerInstruction(0x1b, 1, 2, 0), EEOperation::DivideUnsignedWord1},
    {UINT32_C(0x70000000) | registerInstruction(0x20, 1, 2, 3), EEOperation::MultiplyAddWord1},
    {UINT32_C(0x70000000) | registerInstruction(0x21, 1, 2, 3), EEOperation::MultiplyAddUnsignedWord1},
    {immediateInstruction(0x01, 1, 0x18, 5), EEOperation::MoveByteCountToShiftAmount},
    {immediateInstruction(0x01, 1, 0x19, 5), EEOperation::MoveHalfwordCountToShiftAmount}
  };

  for (const Contract &contract : contracts)
  {
    REQUIRE(
      decodeEEInstruction(contract.instruction).operation ==
      contract.operation);
  }

}

TEST_CASE("EE packed logical decoder and dependencies")
{
  struct Contract
  {
    std::uint32_t instruction;
    EEOperation operation;
  };
  const Contract contracts[] = {
    {
      UINT32_C(0x70000000) |
        registerInstruction(0x09, 1, 2, 3, 0x12),
      EEOperation::ParallelAnd
    },
    {
      UINT32_C(0x70000000) |
        registerInstruction(0x29, 1, 2, 3, 0x12),
      EEOperation::ParallelOr
    },
    {
      UINT32_C(0x70000000) |
        registerInstruction(0x09, 1, 2, 3, 0x13),
      EEOperation::ParallelXor
    },
    {
      UINT32_C(0x70000000) |
        registerInstruction(0x29, 1, 2, 3, 0x13),
      EEOperation::ParallelNor
    }
  };

  for (const Contract &contract : contracts)
  {
    const EEInstruction decoded =
      decodeEEInstruction(contract.instruction);
    const EEInstructionDependencies dependencies =
      eeInstructionDependencies(decoded);

    REQUIRE(decoded.operation == contract.operation);
    REQUIRE(dependencies.gprReads == ((UINT32_C(1) << 1) |
                                      (UINT32_C(1) << 2)));
    REQUIRE(dependencies.gprWrites == (UINT32_C(1) << 3));
    REQUIRE(dependencies.specialReads == 0);
    REQUIRE(dependencies.specialWrites == 0);
  }
}

TEST_CASE("EE packed equality decoder and dependencies")
{
  struct Contract
  {
    std::uint8_t nestedFunction;
    EEOperation operation;
  };
  const Contract contracts[] = {
    {0x0a, EEOperation::ParallelCompareEqualByte},
    {0x06, EEOperation::ParallelCompareEqualHalfword},
    {0x02, EEOperation::ParallelCompareEqualWord}
  };

  for (const Contract &contract : contracts)
  {
    const EEInstruction decoded =
      decodeEEInstruction(
        UINT32_C(0x70000000) |
        registerInstruction(
          0x28,
          1,
          2,
          3,
          contract.nestedFunction));
    const EEInstructionDependencies dependencies =
      eeInstructionDependencies(decoded);

    REQUIRE(decoded.operation == contract.operation);
    REQUIRE(dependencies.gprReads == ((UINT32_C(1) << 1) |
                                      (UINT32_C(1) << 2)));
    REQUIRE(dependencies.gprWrites == (UINT32_C(1) << 3));
    REQUIRE(dependencies.specialReads == 0);
    REQUIRE(dependencies.specialWrites == 0);
    const EEInstructionRouting routing =
      eeInstructionRouting(decoded.operation);
    REQUIRE(routing.category == EEInstructionCategory::WideOperate);
    REQUIRE(
      routing.logicalPipes ==
      static_cast<std::uint8_t>(EELogicalPipe::Pipe0));
    REQUIRE(
      routing.pipe0PhysicalPipelines ==
      static_cast<std::uint8_t>(
        static_cast<std::uint8_t>(EEPhysicalPipeline::I0) |
        static_cast<std::uint8_t>(EEPhysicalPipeline::I1)));
  }

}

TEST_CASE("EE signed packed comparison decoder and dependencies")
{
  struct Contract
  {
    std::uint8_t nestedFunction;
    EEOperation operation;
  };
  const Contract contracts[] = {
    {0x0a, EEOperation::ParallelCompareGreaterThanByte},
    {0x06, EEOperation::ParallelCompareGreaterThanHalfword},
    {0x02, EEOperation::ParallelCompareGreaterThanWord}
  };

  for (const Contract &contract : contracts)
  {
    const EEInstruction decoded =
      decodeEEInstruction(
        UINT32_C(0x70000000) |
        registerInstruction(
          0x08,
          1,
          2,
          3,
          contract.nestedFunction));
    const EEInstructionDependencies dependencies =
      eeInstructionDependencies(decoded);

    REQUIRE(decoded.operation == contract.operation);
    REQUIRE(dependencies.gprReads == ((UINT32_C(1) << 1) |
                                      (UINT32_C(1) << 2)));
    REQUIRE(dependencies.gprWrites == (UINT32_C(1) << 3));
    REQUIRE(dependencies.specialReads == 0);
    REQUIRE(dependencies.specialWrites == 0);
    const EEInstructionRouting routing =
      eeInstructionRouting(decoded.operation);
    REQUIRE(routing.category == EEInstructionCategory::WideOperate);
    REQUIRE(
      routing.logicalPipes ==
      static_cast<std::uint8_t>(EELogicalPipe::Pipe0));
    REQUIRE(
      routing.pipe0PhysicalPipelines ==
      static_cast<std::uint8_t>(
        static_cast<std::uint8_t>(EEPhysicalPipeline::I0) |
        static_cast<std::uint8_t>(EEPhysicalPipeline::I1)));
  }
}

TEST_CASE("EE signed packed min max decoder and dependencies")
{
  struct Contract
  {
    std::uint8_t function;
    std::uint8_t nestedFunction;
    EEOperation operation;
  };
  const Contract contracts[] = {
    {0x08, 0x07, EEOperation::ParallelMaximumHalfword},
    {0x08, 0x03, EEOperation::ParallelMaximumWord},
    {0x28, 0x07, EEOperation::ParallelMinimumHalfword},
    {0x28, 0x03, EEOperation::ParallelMinimumWord}
  };

  for (const Contract &contract : contracts)
  {
    const EEInstruction decoded =
      decodeEEInstruction(
        UINT32_C(0x70000000) |
        registerInstruction(
          contract.function,
          1,
          2,
          3,
          contract.nestedFunction));
    const EEInstructionDependencies dependencies =
      eeInstructionDependencies(decoded);

    REQUIRE(decoded.operation == contract.operation);
    REQUIRE(dependencies.gprReads == ((UINT32_C(1) << 1) |
                                      (UINT32_C(1) << 2)));
    REQUIRE(dependencies.gprWrites == (UINT32_C(1) << 3));
    REQUIRE(dependencies.specialReads == 0);
    REQUIRE(dependencies.specialWrites == 0);
    const EEInstructionRouting routing =
      eeInstructionRouting(decoded.operation);
    REQUIRE(routing.category == EEInstructionCategory::WideOperate);
    REQUIRE(
      routing.logicalPipes ==
      static_cast<std::uint8_t>(EELogicalPipe::Pipe0));
    REQUIRE(
      routing.pipe0PhysicalPipelines ==
      static_cast<std::uint8_t>(
        static_cast<std::uint8_t>(EEPhysicalPipeline::I0) |
        static_cast<std::uint8_t>(EEPhysicalPipeline::I1)));
  }
}

TEST_CASE("EE nested MMI tables classify every encoding")
{
  struct NestedTableContract
  {
    std::uint8_t function;
    std::uint32_t reservedMask;
    std::uint32_t enabledMask;
  };
  const NestedTableContract contracts[] = {
    {0x08, UINT32_C(0x3000f800), UINT32_C(0x000004cc)},
    {0x28, UINT32_C(0xf088fb01), UINT32_C(0x000004cc)},
    {0x09, UINT32_C(0x03c088e2), UINT32_C(0x000c0000)},
    {0x29, UINT32_C(0xb3f388f6), UINT32_C(0x000c0000)}
  };
  const auto requireDecodeFailure =
    [](std::uint32_t instruction,
       EEInstructionDecodeFailure expected)
    {
      try
      {
        static_cast<void>(
          decodeEEInstruction(instruction));
        FAIL("Deferred MMI encoding decoded successfully");
      }
      catch (const EEInstructionDecodeError &error)
      {
        REQUIRE(error.failure() == expected);
      }
    };

  for (const NestedTableContract &contract : contracts)
  {
    for (std::uint8_t nestedFunction = 0;
         nestedFunction < 32;
         ++nestedFunction)
    {
      const bool reserved =
        (contract.reservedMask &
         (UINT32_C(1) << nestedFunction)) != 0;
      const std::uint32_t instruction =
        UINT32_C(0x70000000) |
        registerInstruction(
          contract.function,
          0,
          0,
          0,
          nestedFunction);
      if ((contract.enabledMask &
           (UINT32_C(1) << nestedFunction)) != 0)
      {
        REQUIRE_NOTHROW(decodeEEInstruction(instruction));
        continue;
      }
      requireDecodeFailure(
        instruction,
        reserved
          ? EEInstructionDecodeFailure::Reserved
          : EEInstructionDecodeFailure::Unsupported);
    }
  }
}

TEST_CASE("EE MMI decoder validates fixed fields and formats")
{
  constexpr std::uint32_t sourceMask =
    UINT32_C(0x03e00000);
  constexpr std::uint32_t targetMask =
    UINT32_C(0x001f0000);
  constexpr std::uint32_t destinationMask =
    UINT32_C(0x0000f800);
  constexpr std::uint32_t shiftMask =
    UINT32_C(0x000007c0);
  const auto requireDecodeFailure =
    [](std::uint32_t instruction,
       EEInstructionDecodeFailure expected)
    {
      try
      {
        static_cast<void>(
          decodeEEInstruction(instruction));
        FAIL("Deferred MMI encoding decoded successfully");
      }
      catch (const EEInstructionDecodeError &error)
      {
        REQUIRE(error.failure() == expected);
      }
    };
  struct FixedFieldContract
  {
    std::uint32_t instruction;
    std::uint32_t requiredZeroMask;
  };
  const FixedFieldContract contracts[] = {
    {UINT32_C(0x70000000) |
       registerInstruction(0x04, 1, 0, 3, 0),
     targetMask | shiftMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x31, 1, 0, 0, 0),
     targetMask | destinationMask | shiftMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x34, 0, 2, 3, 7),
     sourceMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x36, 0, 2, 3, 7),
     sourceMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x37, 0, 2, 3, 7),
     sourceMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x3c, 0, 2, 3, 7),
     sourceMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x3e, 0, 2, 3, 7),
     sourceMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x3f, 0, 2, 3, 7),
     sourceMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x28, 0, 2, 3, 0x01),
     sourceMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x28, 0, 2, 3, 0x05),
     sourceMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x29, 0, 2, 3, 0x1b),
     sourceMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x29, 0, 2, 3, 0x1a),
     sourceMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x29, 0, 2, 3, 0x1e),
     sourceMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x09, 0, 2, 3, 0x1a),
     sourceMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x09, 0, 2, 3, 0x1e),
     sourceMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x08, 0, 2, 3, 0x1e),
     sourceMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x08, 0, 2, 3, 0x1f),
     sourceMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x09, 0, 2, 3, 0x1b),
     sourceMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x09, 0, 2, 3, 0x1f),
     sourceMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x09, 0, 0, 3, 0x08),
     sourceMask | targetMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x09, 0, 0, 3, 0x09),
     sourceMask | targetMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x29, 1, 0, 0, 0x08),
     targetMask | destinationMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x29, 1, 0, 0, 0x09),
     targetMask | destinationMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x09, 1, 2, 0, 0x1d),
     destinationMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x29, 1, 2, 0, 0x0d),
     destinationMask},
    {UINT32_C(0x70000000) |
       registerInstruction(0x09, 1, 2, 0, 0x0d),
     destinationMask}
  };
  const std::uint32_t fields[] = {
    sourceMask,
    targetMask,
    destinationMask,
    shiftMask
  };

  for (const FixedFieldContract &contract : contracts)
  {
    requireDecodeFailure(
      contract.instruction,
      EEInstructionDecodeFailure::Unsupported);
    for (const std::uint32_t field : fields)
    {
      if ((contract.requiredZeroMask & field) != 0)
      {
        requireDecodeFailure(
          contract.instruction | field,
          EEInstructionDecodeFailure::Reserved);
      }
    }
  }

  for (std::uint8_t format = 0; format < 32; ++format)
  {
    requireDecodeFailure(
      UINT32_C(0x70000000) |
        registerInstruction(0x30, 0, 0, 3, format),
      format < 5
        ? EEInstructionDecodeFailure::Unsupported
        : EEInstructionDecodeFailure::Reserved);
  }
  requireDecodeFailure(
    UINT32_C(0x70000000) |
      registerInstruction(0x30, 1, 0, 3, 0),
    EEInstructionDecodeFailure::Reserved);
  requireDecodeFailure(
    UINT32_C(0x70000000) |
      registerInstruction(0x30, 0, 1, 3, 0),
    EEInstructionDecodeFailure::Reserved);

  bool definedPrimaryFunctions[64] = {};
  for (const std::uint8_t function : {
         UINT8_C(0x00), UINT8_C(0x01),
         UINT8_C(0x04), UINT8_C(0x08),
         UINT8_C(0x09), UINT8_C(0x10),
         UINT8_C(0x11), UINT8_C(0x12),
         UINT8_C(0x13), UINT8_C(0x18),
         UINT8_C(0x19), UINT8_C(0x1a),
         UINT8_C(0x1b), UINT8_C(0x20),
         UINT8_C(0x21), UINT8_C(0x28),
         UINT8_C(0x29), UINT8_C(0x30),
         UINT8_C(0x31), UINT8_C(0x34),
         UINT8_C(0x36), UINT8_C(0x37),
         UINT8_C(0x3c), UINT8_C(0x3e),
         UINT8_C(0x3f)})
  {
    definedPrimaryFunctions[function] = true;
  }
  for (std::uint8_t function = 0;
       function < 64;
       ++function)
  {
    if (!definedPrimaryFunctions[function])
    {
      requireDecodeFailure(
        UINT32_C(0x70000000) |
          registerInstruction(function, 0, 0, 0, 0),
        EEInstructionDecodeFailure::Reserved);
    }
  }
}

TEST_CASE("EE branch and jump decoder tables")
{
  struct Contract
  {
    std::uint32_t instruction;
    EEOperation operation;
  };
  const Contract contracts[] = {
    {UINT32_C(0x08012345), EEOperation::Jump},
    {UINT32_C(0x0c012345), EEOperation::JumpAndLink},
    {registerInstruction(0x08, 1, 0, 0), EEOperation::JumpRegister},
    {registerInstruction(0x09, 1, 0, 31), EEOperation::JumpAndLinkRegister},
    {immediateInstruction(0x04, 1, 2, 3), EEOperation::BranchEqual},
    {immediateInstruction(0x05, 1, 2, 3), EEOperation::BranchNotEqual},
    {immediateInstruction(0x06, 1, 0, 3), EEOperation::BranchLessThanOrEqualZero},
    {immediateInstruction(0x07, 1, 0, 3), EEOperation::BranchGreaterThanZero},
    {immediateInstruction(0x14, 1, 2, 3), EEOperation::BranchEqualLikely},
    {immediateInstruction(0x15, 1, 2, 3), EEOperation::BranchNotEqualLikely},
    {immediateInstruction(0x16, 1, 0, 3), EEOperation::BranchLessThanOrEqualZeroLikely},
    {immediateInstruction(0x17, 1, 0, 3), EEOperation::BranchGreaterThanZeroLikely},
    {immediateInstruction(0x01, 1, 0x00, 3), EEOperation::BranchLessThanZero},
    {immediateInstruction(0x01, 1, 0x01, 3), EEOperation::BranchGreaterThanOrEqualZero},
    {immediateInstruction(0x01, 1, 0x02, 3), EEOperation::BranchLessThanZeroLikely},
    {immediateInstruction(0x01, 1, 0x03, 3), EEOperation::BranchGreaterThanOrEqualZeroLikely},
    {immediateInstruction(0x01, 1, 0x10, 3), EEOperation::BranchLessThanZeroAndLink},
    {immediateInstruction(0x01, 1, 0x11, 3), EEOperation::BranchGreaterThanOrEqualZeroAndLink},
    {immediateInstruction(0x01, 1, 0x12, 3), EEOperation::BranchLessThanZeroAndLinkLikely},
    {immediateInstruction(0x01, 1, 0x13, 3), EEOperation::BranchGreaterThanOrEqualZeroAndLinkLikely}
  };

  for (const Contract &contract : contracts)
  {
    REQUIRE(
      decodeEEInstruction(contract.instruction).operation ==
      contract.operation);
  }

  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      registerInstruction(0x08, 1, 0, 1)),
    "Reserved EE instruction encoding.");
  REQUIRE_THROWS_WITH(
    decodeEEInstruction(
      immediateInstruction(0x06, 1, 1, 3)),
    "Reserved EE instruction encoding.");
}

TEST_CASE("EE byte memory instruction decoding")
{
  REQUIRE(
    decodeEEInstruction(
      immediateInstruction(0x20, 1, 2, 3)).operation ==
    EEOperation::LoadByte);
  REQUIRE(
    decodeEEInstruction(
      immediateInstruction(0x24, 1, 2, 3)).operation ==
    EEOperation::LoadByteUnsigned);
  REQUIRE(
    decodeEEInstruction(
      immediateInstruction(0x28, 1, 2, 3)).operation ==
    EEOperation::StoreByte);
  REQUIRE(
    decodeEEInstruction(
      immediateInstruction(0x21, 1, 2, 3)).operation ==
    EEOperation::LoadHalfword);
  REQUIRE(
    decodeEEInstruction(
      immediateInstruction(0x25, 1, 2, 3)).operation ==
    EEOperation::LoadHalfwordUnsigned);
  REQUIRE(
    decodeEEInstruction(
      immediateInstruction(0x29, 1, 2, 3)).operation ==
    EEOperation::StoreHalfword);
  REQUIRE(
    decodeEEInstruction(
      immediateInstruction(0x23, 1, 2, 3)).operation ==
    EEOperation::LoadWord);
  REQUIRE(
    decodeEEInstruction(
      immediateInstruction(0x27, 1, 2, 3)).operation ==
    EEOperation::LoadWordUnsigned);
  REQUIRE(
    decodeEEInstruction(
      immediateInstruction(0x2b, 1, 2, 3)).operation ==
    EEOperation::StoreWord);
  REQUIRE(
    decodeEEInstruction(
      immediateInstruction(0x22, 1, 2, 3)).operation ==
    EEOperation::LoadWordLeft);
  REQUIRE(
    decodeEEInstruction(
      immediateInstruction(0x26, 1, 2, 3)).operation ==
    EEOperation::LoadWordRight);
  REQUIRE(
    decodeEEInstruction(
      immediateInstruction(0x2a, 1, 2, 3)).operation ==
    EEOperation::StoreWordLeft);
  REQUIRE(
    decodeEEInstruction(
      immediateInstruction(0x2e, 1, 2, 3)).operation ==
    EEOperation::StoreWordRight);
  REQUIRE(
    decodeEEInstruction(
      immediateInstruction(0x37, 1, 2, 3)).operation ==
    EEOperation::LoadDoubleword);
  REQUIRE(
    decodeEEInstruction(
      immediateInstruction(0x3f, 1, 2, 3)).operation ==
    EEOperation::StoreDoubleword);
}
