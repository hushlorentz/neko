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
  BranchCOP2TrueLikely,
  VectorCallMicroSubroutine,
  VectorCallMicroSubroutineRegister,
  VectorMacroArithmetic,
  MoveWordFromCOP1,
  MoveWordToCOP1,
  MoveControlWordFromCOP1,
  MoveControlWordToCOP1,
  LoadWordToCOP1,
  StoreWordFromCOP1,
  AbsoluteSingleCOP1,
  MoveSingleCOP1,
  NegateSingleCOP1,
  MaximumSingleCOP1,
  MinimumSingleCOP1,
  ConvertWordToSingleCOP1,
  ConvertSingleToWordCOP1,
  AddSingleCOP1,
  SubtractSingleCOP1,
  MultiplySingleCOP1,
  DivideSingleCOP1,
  SquareRootSingleCOP1,
  ReciprocalSquareRootSingleCOP1,
  MultiplyAddSingleCOP1,
  MultiplySubtractSingleCOP1,
  AddSingleToAccumulatorCOP1,
  SubtractSingleToAccumulatorCOP1,
  MultiplySingleToAccumulatorCOP1,
  MultiplyAddSingleToAccumulatorCOP1,
  MultiplySubtractSingleToAccumulatorCOP1,
  CompareFalseSingleCOP1,
  CompareEqualSingleCOP1,
  CompareLessThanSingleCOP1,
  CompareLessThanOrEqualSingleCOP1,
  BranchCOP1False,
  BranchCOP1FalseLikely,
  BranchCOP1True,
  BranchCOP1TrueLikely,
  SynchronizeLoadStore,
  SynchronizePipeline
};

constexpr std::uint8_t EE_OPERATION_COUNT =
  static_cast<std::uint8_t>(
    EEOperation::SynchronizePipeline) + 1;

enum class EEInstructionCategory : std::uint8_t
{
  LoadStore,
  Synchronization,
  LeadingZeroCount,
  ExceptionReturn,
  ShiftAmountOperate,
  COP0,
  COP1Move,
  COP2Move,
  COP1Operate,
  COP2Operate,
  ALU,
  MAC0,
  MAC1,
  Branch,
  WideOperate
};

enum class EELogicalPipe : std::uint8_t
{
  Pipe0 = 1 << 0,
  Pipe1 = 1 << 1
};

enum class EEPhysicalPipeline : std::uint8_t
{
  I0 = 1 << 0,
  I1 = 1 << 1,
  LoadStore = 1 << 2,
  Branch = 1 << 3,
  COP1 = 1 << 4,
  COP2 = 1 << 5
};

enum EEInstructionSpecialResource : std::uint16_t
{
  RESOURCE_HI = 1 << 0,
  RESOURCE_LO = 1 << 1,
  RESOURCE_HI1 = 1 << 2,
  RESOURCE_LO1 = 1 << 3,
  RESOURCE_SA = 1 << 4,
  RESOURCE_COP1_ACCUMULATOR = 1 << 5,
  RESOURCE_COP1_FCR31 = 1 << 6,
  RESOURCE_COP2_STATE = 1 << 7
};

struct EEInstructionDependencies
{
  std::uint32_t gprReads = 0;
  std::uint32_t gprWrites = 0;
  std::uint32_t fprReads = 0;
  std::uint32_t fprWrites = 0;
  std::uint32_t cop2Reads = 0;
  std::uint32_t cop2Writes = 0;
  std::uint32_t cop2ControlReads = 0;
  std::uint32_t cop2ControlWrites = 0;
  std::uint16_t specialReads = 0;
  std::uint16_t specialWrites = 0;
};

struct EEInstructionRouting
{
  EEInstructionCategory category = EEInstructionCategory::ALU;
  std::uint8_t logicalPipes = 0;
  std::uint8_t pipe0PhysicalPipelines = 0;
  std::uint8_t pipe1PhysicalPipelines = 0;
};

struct EEInstructionPipeAssignment
{
  bool assignable = false;
  EELogicalPipe olderPipe = EELogicalPipe::Pipe0;
  EELogicalPipe youngerPipe = EELogicalPipe::Pipe1;
};

enum class EEIssuePairing : std::uint8_t
{
  Forbidden,
  Concurrent,
  ConcurrentWithStall
};

struct EEIssueSelection
{
  std::uint8_t instructionCount = 0;
  EEInstructionPipeAssignment assignment;
  EEIssuePairing pairing = EEIssuePairing::Forbidden;
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
  std::uint16_t cop2Immediate = 0;
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
EEInstructionRouting eeInstructionRouting(EEOperation operation);
bool eeInstructionSupportsLogicalPipe(
  const EEInstructionRouting &routing,
  EELogicalPipe pipe);
std::uint8_t eeInstructionPhysicalPipelines(
  const EEInstructionRouting &routing,
  EELogicalPipe pipe);
EEInstructionDependencies eeInstructionDependencies(
  const EEInstruction &instruction);
bool eeInstructionUsesPhysicalPipeline(
  const EEInstructionRouting &routing,
  EELogicalPipe pipe,
  EEPhysicalPipeline pipeline);
EEInstructionPipeAssignment assignEEInstructionPairPipes(
  EEOperation older,
  EEOperation younger);
EEIssueSelection selectEEIssueGroup(
  const EEInstruction &older,
  const EEInstruction &younger,
  bool olderReady,
  bool youngerReady);
bool isEEBranchOperation(EEOperation operation);
bool isEEBranchLikelyOperation(EEOperation operation);

#endif
