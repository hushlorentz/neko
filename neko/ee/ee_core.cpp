#include "ee_core.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <stdexcept>

#include "ee_bus.hpp"
#include "floating_point_ops.hpp"
#include "vpu/vpu.hpp"

constexpr std::size_t EECore::GENERAL_REGISTER_COUNT;
constexpr std::size_t EECore::FLOATING_POINT_REGISTER_COUNT;
constexpr std::size_t EEAcceptanceRecords::CAPACITY;

void EEAcceptanceRecords::clear()
{
  records = {};
  count = 0;
}

void EEAcceptanceRecords::append(
  const EEAcceptanceRecord &record)
{
  if (count >= records.size())
  {
    throw std::overflow_error(
      "EE accepted more than two instructions in one cycle.");
  }
  if (record.programOrder == 0 ||
      (count != 0 &&
       record.programOrder <=
         records[count - 1].programOrder))
  {
    throw std::invalid_argument(
      "EE acceptance records are not in program order.");
  }
  records[count++] = record;
}

std::size_t EEAcceptanceRecords::size() const
{
  return count;
}

std::uint64_t EEAcceptanceRecords::instructionCount() const
{
  return count;
}

const EEAcceptanceRecord &EEAcceptanceRecords::operator[](
  std::size_t index) const
{
  if (index >= count)
  {
    throw std::out_of_range(
      "EE acceptance record index is out of range.");
  }
  return records[index];
}

void EEShiftAmountOrderingWindow::clear()
{
  recentAccesses = 0;
  recentReads = 0;
}

bool EEShiftAmountOrderingWindow::permits(
  EEOperation operation) const
{
  const bool restore =
    operation == EEOperation::MoveToShiftAmount;
  const bool calculate =
    operation == EEOperation::MoveByteCountToShiftAmount ||
    operation == EEOperation::MoveHalfwordCountToShiftAmount;
  return
    (!restore || (recentAccesses & 0x07) == 0) &&
    (!calculate || (recentReads & 0x07) == 0);
}

void EEShiftAmountOrderingWindow::accept(
  EEOperation operation)
{
  const bool reads =
    operation == EEOperation::MoveFromShiftAmount;
  const bool accesses =
    reads ||
    operation == EEOperation::MoveByteCountToShiftAmount ||
    operation == EEOperation::MoveHalfwordCountToShiftAmount;
  recentAccesses =
    static_cast<std::uint8_t>(
      ((recentAccesses << 1) |
       (accesses ? 1 : 0)) & 0x07);
  recentReads =
    static_cast<std::uint8_t>(
      ((recentReads << 1) |
       (reads ? 1 : 0)) & 0x07);
}

std::uint8_t EEShiftAmountOrderingWindow::accessHistory() const
{
  return recentAccesses;
}

std::uint8_t EEShiftAmountOrderingWindow::readHistory() const
{
  return recentReads;
}

void EEShiftAmountOrderingWindow::restore(
  std::uint8_t accesses,
  std::uint8_t reads)
{
  if ((accesses & 0xf8) != 0 ||
      (reads & 0xf8) != 0 ||
      (reads & ~accesses) != 0)
  {
    throw std::invalid_argument(
      "EE shift-amount ordering history is invalid.");
  }
  recentAccesses = accesses;
  recentReads = reads;
}

namespace
{
  constexpr std::uint64_t WORD_SIGN_BIT =
    UINT64_C(0x80000000);
  constexpr std::uint64_t DOUBLEWORD_SIGN_BIT =
    UINT64_C(0x8000000000000000);
  constexpr std::uint64_t EE_STATE_FNV_OFFSET_BASIS =
    UINT64_C(14695981039346656037);
  constexpr std::uint64_t EE_STATE_FNV_PRIME =
    UINT64_C(1099511628211);
  constexpr std::uint8_t MULTIPLY_LATENCY = 4;
  constexpr std::uint8_t DIVIDE_LATENCY = 37;

  std::uint32_t updatedCOP1Status(
    std::uint32_t status,
    std::uint8_t affectedFlags,
    std::uint8_t raisedFlags,
    std::uint8_t raisedStickyFlags)
  {
    struct FlagMapping
    {
      std::uint8_t resultFlag;
      std::uint32_t causeFlag;
      std::uint32_t stickyFlag;
    };
    constexpr FlagMapping FLAG_MAPPINGS[] = {
      {
        FP_FLAG_I_BIT,
        EECOP1Control::CAUSE_INVALID,
        EECOP1Control::STICKY_INVALID
      },
      {
        FP_FLAG_D_BIT,
        EECOP1Control::CAUSE_DIVISION_BY_ZERO,
        EECOP1Control::STICKY_DIVISION_BY_ZERO
      },
      {
        FP_FLAG_OVERFLOW,
        EECOP1Control::CAUSE_OVERFLOW,
        EECOP1Control::STICKY_OVERFLOW
      },
      {
        FP_FLAG_UNDERFLOW,
        EECOP1Control::CAUSE_UNDERFLOW,
        EECOP1Control::STICKY_UNDERFLOW
      }
    };

    for (const FlagMapping &mapping : FLAG_MAPPINGS)
    {
      if ((affectedFlags & mapping.resultFlag) == 0)
      {
        continue;
      }
      status &= ~mapping.causeFlag;
      if ((raisedFlags & mapping.resultFlag) != 0)
      {
        status |= mapping.causeFlag;
      }
      if (((raisedFlags | raisedStickyFlags) &
           mapping.resultFlag) != 0)
      {
        status |= mapping.stickyFlag;
      }
    }
    return status;
  }

  void hashEEStateValue(
    std::uint64_t *hash,
    std::uint64_t value)
  {
    for (std::uint8_t index = 0; index < 8; ++index)
    {
      *hash ^= static_cast<std::uint8_t>(value >> (index * 8));
      *hash *= EE_STATE_FNV_PRIME;
    }
  }

  std::uint64_t signExtend16(std::uint16_t value)
  {
    if ((value & 0x8000) != 0)
    {
      return UINT64_C(0xffffffffffff0000) | value;
    }
    return value;
  }

  std::uint64_t signExtendWord(std::uint32_t value)
  {
    if ((value & WORD_SIGN_BIT) != 0)
    {
      return UINT64_C(0xffffffff00000000) | value;
    }
    return value;
  }

  EERegister128 quadwordFromFPRegister(
    const FPRegister &value)
  {
    return {
      static_cast<std::uint64_t>(value.x.bits()) |
        (static_cast<std::uint64_t>(value.y.bits()) << 32),
      static_cast<std::uint64_t>(value.z.bits()) |
        (static_cast<std::uint64_t>(value.w.bits()) << 32)
    };
  }

  std::int32_t signedWord(std::uint32_t value)
  {
    std::int32_t result = 0;
    static_assert(sizeof(result) == sizeof(value), "");
    std::memcpy(&result, &value, sizeof(result));
    return result;
  }

  std::uint64_t multiplyUnsignedWords(
    std::uint32_t left,
    std::uint32_t right)
  {
    return
      static_cast<std::uint64_t>(left) *
      static_cast<std::uint64_t>(right);
  }

  std::uint64_t multiplySignedWords(
    std::uint32_t left,
    std::uint32_t right)
  {
    const std::int64_t product =
      static_cast<std::int64_t>(signedWord(left)) *
      static_cast<std::int64_t>(signedWord(right));
    return static_cast<std::uint64_t>(product);
  }

  std::uint64_t accumulatorValue(
    std::uint64_t hi,
    std::uint64_t lo)
  {
    return
      (static_cast<std::uint64_t>(
        static_cast<std::uint32_t>(hi)) << 32) |
      static_cast<std::uint32_t>(lo);
  }

  bool isWordValue(std::uint64_t value)
  {
    return value == signExtendWord(
      static_cast<std::uint32_t>(value));
  }

  bool signedLess(
    std::uint64_t left,
    std::uint64_t right)
  {
    return
      (left ^ DOUBLEWORD_SIGN_BIT) <
      (right ^ DOUBLEWORD_SIGN_BIT);
  }

  bool addOverflow32(
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t result)
  {
    return
      ((~(left ^ right) & (left ^ result)) &
       UINT32_C(0x80000000)) != 0;
  }

  bool subtractOverflow32(
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t result)
  {
    return
      (((left ^ right) & (left ^ result)) &
       UINT32_C(0x80000000)) != 0;
  }

  bool addOverflow64(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t result)
  {
    return
      ((~(left ^ right) & (left ^ result)) &
       DOUBLEWORD_SIGN_BIT) != 0;
  }

  bool subtractOverflow64(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t result)
  {
    return
      (((left ^ right) & (left ^ result)) &
       DOUBLEWORD_SIGN_BIT) != 0;
  }

  std::uint32_t arithmeticShiftRight32(
    std::uint32_t value,
    std::uint8_t amount)
  {
    if (amount == 0)
    {
      return value;
    }
    std::uint32_t result = value >> amount;
    if ((value & UINT32_C(0x80000000)) != 0)
    {
      result |= UINT32_MAX << (32 - amount);
    }
    return result;
  }

  std::uint64_t arithmeticShiftRight64(
    std::uint64_t value,
    std::uint8_t amount)
  {
    if (amount == 0)
    {
      return value;
    }
    std::uint64_t result = value >> amount;
    if ((value & DOUBLEWORD_SIGN_BIT) != 0)
    {
      result |= UINT64_MAX << (64 - amount);
    }
    return result;
  }
}

void EECore::reset()
{
  generalRegisters.fill({});
  floatingPointRegisters.fill(0);
  floatingPointAccumulatorRegister = 0;
  cop1StatusRegister = 0;
  pc = EEReset::VECTOR;
  hiRegister = 0;
  loRegister = 0;
  hi1Register = 0;
  lo1Register = 0;
  saRegister = 0;
  cop0BadVAddr = 0;
  cop0Count = 0;
  cop0Compare = 0;
  cop0Status = EECOP0Status::RESET;
  cop0Cause = 0;
  cop0EPC = 0;
  cop0ErrorEPC = 0;
  exception = EEException::None;
  faultAddress = 0;
  state = EEExecutionState::Halted;
  haltReason = EEStopReason::None;
  cycles = 0;
  lastInstructionValid = false;
  lastAddress = 0;
  lastDecodedInstruction = {};
  issueSelection = {};
  rejectedInstructionValue = 0;
  clearIssueFrontEnd();
  cancelInFlightCOP1(COP1CancellationScope::All);
  nextEEProgramOrder = 1;
  executingProgramOrder = 0;
  pendingMac0 = {};
  pendingMac1 = {};
  shiftAmountOrdering.clear();
  clearBranchDelayContinuation();
  cop1DividerPostDelayInstructions = 0;
  cop1DividerPostDelayBranchAddress = 0;
  cop1DividerPostDelayTargetAddress = 0;
  cop1DividerPostDelayTaken = false;
  cop1DividerPostTargetInstructions = 0;
  cop1DividerPostTargetAddress = 0;
  reconcileCOP1DividerOccupancy();
  acceptanceRecords.clear();
  exceptionEnteredThisCycle = false;
  cycleTraceEventCount = 0;
}

void EECore::attachBus(EEBus *newBus)
{
  if (newBus == nullptr)
  {
    throw std::invalid_argument(
      "EE Core requires a non-null bus.");
  }
  if (bus != nullptr)
  {
    throw std::logic_error(
      "EE Core bus is already attached.");
  }
  bus = newBus;
}

void EECore::attachVU0(VPU *newVU0)
{
  if (newVU0 == nullptr)
  {
    throw std::invalid_argument(
      "EE Core requires a non-null VU0.");
  }
  if (vu0 != nullptr)
  {
    throw std::logic_error(
      "EE Core VU0 is already attached.");
  }
  if (newVU0->unitType() != VPUType::VU0)
  {
    throw std::invalid_argument(
      "EE Core COP2 must attach to VU0.");
  }
  vu0 = newVU0;
}

void EECore::attachVU1(VPU *newVU1)
{
  if (newVU1 == nullptr)
  {
    throw std::invalid_argument(
      "EE Core requires a non-null VU1.");
  }
  if (vu1 != nullptr)
  {
    throw std::logic_error(
      "EE Core VU1 is already attached.");
  }
  if (newVU1->unitType() != VPUType::VU1)
  {
    throw std::invalid_argument(
      "EE Core COP2 condition must attach to VU1.");
  }
  vu1 = newVU1;
}

EEInstructionFetchResult EECore::fetchInstruction()
{
  if (frontEndContinuationActive())
  {
    throw std::logic_error(
      "EE public instruction fetch requires an empty front end.");
  }

  const std::uint32_t address = pc;
  if ((address & 3) != 0)
  {
    return raiseFetchException(
      EEException::AddressErrorLoadOrFetch,
      address);
  }

  std::uint32_t instruction = 0;
  if (!attachedBus().readInstruction32(
        address,
        &instruction))
  {
    return raiseFetchException(
      EEException::InstructionBusError,
      address);
  }

  pc += 4;
  return {true, address, instruction};
}

EECore::FrontEndFetchResult EECore::fetchIssueCandidate(
  std::uint32_t address) const
{
  if ((address & 3) != 0)
  {
    return {
      address,
      0,
      FrontEndFetchFailure::AddressError
    };
  }

  std::uint32_t instruction = 0;
  if (!attachedBus().readInstruction32(
        address,
        &instruction))
  {
    return {
      address,
      0,
      FrontEndFetchFailure::BusError
    };
  }

  return {
    address,
    instruction,
    FrontEndFetchFailure::None
  };
}

EECore::DecodedIssueLatch EECore::decodeIssueCandidate(
  const FrontEndFetchResult &fetch)
{
  DecodedIssueLatch latch;
  latch.valid = true;
  latch.address = fetch.address;
  switch (fetch.failure)
  {
    case FrontEndFetchFailure::AddressError:
      latch.failure = IssueLatchFailure::AddressError;
      return latch;
    case FrontEndFetchFailure::BusError:
      latch.failure = IssueLatchFailure::BusError;
      return latch;
    case FrontEndFetchFailure::None:
      break;
  }

  try
  {
    latch.instruction =
      decodeEEInstruction(fetch.instruction);
  }
  catch (const EEInstructionDecodeError &error)
  {
    latch.instruction.raw = fetch.instruction;
    latch.failure =
      error.failure() == EEInstructionDecodeFailure::Reserved
        ? IssueLatchFailure::ReservedInstruction
        : IssueLatchFailure::UnsupportedInstruction;
  }
  return latch;
}

void EECore::ensureIssueLatch()
{
  if (issueLatch.valid)
  {
    return;
  }
  issueLatch =
    decodeIssueCandidate(fetchIssueCandidate(pc));
}

void EECore::ensureStagingLatch()
{
  if (issueLatch.failure != IssueLatchFailure::None ||
      stagingLatch.valid ||
      branchDelayPending)
  {
    return;
  }
  stagingLatch = decodeIssueCandidate(
    fetchIssueCandidate(issueLatch.address + 4));
}

void EECore::fillIssueFrontEnd()
{
  ensureIssueLatch();
  ensureStagingLatch();
}

void EECore::promoteStagingLatch()
{
  issueLatch = stagingLatch;
  stagingLatch = {};
}

void EECore::clearIssueFrontEnd()
{
  issueLatch = {};
  stagingLatch = {};
}

void EECore::clearBranchDelayContinuation()
{
  branchDelayPending = false;
  branchDelayTarget = 0;
  branchInstructionAddress = 0;
  branchDelayFromLikely = false;
  branchDelayTaken = false;
}

EECore::ExecutionStartMode EECore::executionStartMode(
  std::uint32_t startAddress) const
{
  return
    state == EEExecutionState::Halted &&
    haltReason == EEStopReason::HostHalt &&
    startAddress == pc
      ? ExecutionStartMode::ResumeHostContinuation
      : ExecutionStartMode::Restart;
}

void EECore::resetExecutionContinuation()
{
  lastInstructionValid = false;
  lastAddress = 0;
  lastDecodedInstruction = {};
  clearBranchDelayContinuation();
  cop1DividerPostDelayInstructions = 0;
  cop1DividerPostDelayBranchAddress = 0;
  cop1DividerPostDelayTargetAddress = 0;
  cop1DividerPostDelayTaken = false;
  cop1DividerPostTargetInstructions = 0;
  cop1DividerPostTargetAddress = 0;
  pendingMac0 = {};
  pendingMac1 = {};
  shiftAmountOrdering.clear();
  clearIssueFrontEnd();
  issueSelection = {};
  cancelInFlightCOP1(COP1CancellationScope::All);
  nextEEProgramOrder = 1;
}

bool EECore::frontEndContinuationActive() const
{
  return
    issueLatch.valid ||
    stagingLatch.valid ||
    branchDelayPending;
}

EEIssueMemberOutcome EECore::resolveIssueLatchFailure()
{
  if (!issueLatch.valid ||
      issueLatch.failure == IssueLatchFailure::None)
  {
    throw std::logic_error(
      "EE issue-latch failure resolution requires a failure.");
  }

  const std::uint32_t address = issueLatch.address;
  const std::uint32_t instruction =
    issueLatch.instruction.raw;
  switch (issueLatch.failure)
  {
    case IssueLatchFailure::AddressError:
      enterException(
        EEException::AddressErrorLoadOrFetch,
        address,
        address,
        0);
      return EEIssueMemberOutcome::Faulted;
    case IssueLatchFailure::BusError:
      enterException(
        EEException::InstructionBusError,
        address,
        address,
        0);
      return EEIssueMemberOutcome::Faulted;
    case IssueLatchFailure::ReservedInstruction:
      rejectedInstructionValue = instruction;
      enterException(
        EEException::ReservedInstruction,
        address,
        address,
        instruction);
      return EEIssueMemberOutcome::Faulted;
    case IssueLatchFailure::UnsupportedInstruction:
      rejectedInstructionValue = instruction;
      pc = address;
      state = EEExecutionState::Halted;
      haltReason = EEStopReason::UnsupportedInstruction;
      clearIssueFrontEnd();
      return EEIssueMemberOutcome::Cancelled;
    case IssueLatchFailure::None:
      break;
  }
  throw std::logic_error(
    "Unknown EE issue-latch failure.");
}

void EECore::updateIssueSelection(
  std::uint32_t completedLoadRegisters)
{
  const IssueCandidates candidates =
    constructIssueCandidates();
  const IssueCandidateReadiness readiness =
    evaluateIssueCandidateReadiness(
      candidates,
      completedLoadRegisters);
  issueSelection =
    selectReadyIssueCandidates(candidates, readiness);
}

EECore::IssueCandidates EECore::constructIssueCandidates()
  const
{
  IssueCandidates candidates;
  if (!issueLatch.valid)
  {
    return candidates;
  }
  candidates.older = &issueLatch;
  if (stagingLatch.valid)
  {
    candidates.younger = &stagingLatch;
  }
  return candidates;
}

EECore::IssueCandidateReadiness
EECore::evaluateIssueCandidateReadiness(
  const IssueCandidates &candidates,
  std::uint32_t completedLoadRegisters) const
{
  IssueCandidateReadiness readiness;
  if (candidates.older == nullptr ||
      candidates.older->failure != IssueLatchFailure::None)
  {
    return readiness;
  }

  readiness.availableCOP1Slots =
    static_cast<std::size_t>(std::count_if(
      inFlightCOP1Operations.begin(),
      inFlightCOP1Operations.end(),
      [](const InFlightCOP1Operation &operation)
      {
        return !operation.active;
      }));
  readiness.older =
    issueCandidateReady(
      candidates.older->instruction,
      completedLoadRegisters,
      readiness.availableCOP1Slots);
  readiness.younger =
    candidates.younger != nullptr &&
    candidates.younger->failure == IssueLatchFailure::None &&
    issueCandidateReady(
      candidates.younger->instruction,
      completedLoadRegisters,
      readiness.availableCOP1Slots);
  return readiness;
}

EEIssueSelection EECore::selectReadyIssueCandidates(
  const IssueCandidates &candidates,
  const IssueCandidateReadiness &readiness) const
{
  if (!readiness.older)
  {
    return {};
  }
  if (!readiness.younger ||
      !issuePairStructurallySafe(
        candidates.older->instruction,
        candidates.younger->instruction,
        readiness.availableCOP1Slots))
  {
    return selectEESingleIssue(
      candidates.older->instruction);
  }
  return selectEEIssuePair(
    candidates.older->instruction,
    candidates.younger->instruction);
}

bool EECore::issueCandidateReady(
  const EEInstruction &instruction,
  std::uint32_t completedLoadRegisters,
  std::size_t availableCOP1Slots) const
{
  if (pendingMultiplyDivideActive() &&
      instruction.operation !=
        EEOperation::SynchronizePipeline)
  {
    return false;
  }
  if (isCOP1ManagedPipelineOperation(
        instruction.operation) &&
      cop1TransferReservedByStalledMove())
  {
    return false;
  }

  COP1ScoreboardHazard hazard;
  if (cop1ScoreboardBlocks(
        instruction,
        completedLoadRegisters,
        &hazard,
        COP1ScoreboardQuery::CandidateReadiness))
  {
    return false;
  }
  if (cop2ScoreboardBlocks(instruction))
  {
    return false;
  }
  return
    !isCOP1ManagedPipelineOperation(
      instruction.operation) ||
    availableCOP1Slots != 0;
}

bool EECore::cop1TransferReservedByStalledMove() const
{
  return std::any_of(
    inFlightCOP1Operations.begin(),
    inFlightCOP1Operations.end(),
    [](const InFlightCOP1Operation &operation)
    {
      return
        operation.active &&
        operation.stage == COP1PipelineStage::R &&
        isCOP1MoveOperation(operation.instruction.operation);
    });
}

bool EECore::cop1MemoryExceptionPending() const
{
  return
    cop1ScoreboardValue(
      COP1ScoreboardResource::MemoryException)
      .availability ==
        COP1ScoreboardAvailability::Unavailable;
}

bool EECore::issuePairStructurallySafe(
  const EEInstruction &older,
  const EEInstruction &younger,
  std::size_t availableCOP1Slots) const
{
  if (!memoryIssueCanJoinPair(older) ||
      !memoryIssueCanJoinPair(younger))
  {
    return false;
  }

  const std::size_t requiredCOP1Slots =
    static_cast<std::size_t>(
      isCOP1ManagedPipelineOperation(
        older.operation)) +
    static_cast<std::size_t>(
      isCOP1ManagedPipelineOperation(
        younger.operation));
  if (requiredCOP1Slots > availableCOP1Slots)
  {
    return false;
  }

  return
    !isEEBranchLikelyOperation(older.operation) ||
    branchLikelyTaken(older);
}

bool EECore::issueSelectionCanExecuteConcurrently() const
{
  if (issueSelection.instructionCount != 2 ||
      (issueSelection.pairing !=
         EEIssuePairing::Concurrent &&
       issueSelection.pairing !=
         EEIssuePairing::ConcurrentWithStall) ||
      branchDelayPending)
  {
    return false;
  }
  if (isCOP1MemoryMoveOperation(
        issueLatch.instruction.operation) &&
      issueSelection.pairing !=
        EEIssuePairing::ConcurrentWithStall)
  {
    return false;
  }

  const bool resolvedBranchPair =
    isEEBranchOperation(issueLatch.instruction.operation);
  const bool ordinaryPair =
    isActivatedOIssueOperation(
      issueLatch.instruction.operation) &&
    isActivatedOIssueOperation(
      stagingLatch.instruction.operation);
  if (!resolvedBranchPair && !ordinaryPair)
  {
    return false;
  }

  return issueSelectionUsesCompatiblePhysicalPipelines();
}

bool EECore::issueSelectionUsesCompatiblePhysicalPipelines()
  const
{
  const EEInstructionRouting olderRouting =
    eeInstructionRouting(
      issueLatch.instruction.operation);
  const EEInstructionRouting youngerRouting =
    eeInstructionRouting(
      stagingLatch.instruction.operation);
  const std::uint8_t olderPhysicalPipelines =
    eeInstructionPhysicalPipelines(
      olderRouting,
      issueSelection.assignment.olderPipe);
  const std::uint8_t youngerPhysicalPipelines =
    eeInstructionPhysicalPipelines(
      youngerRouting,
      issueSelection.assignment.youngerPipe);
  const std::uint8_t sharedPhysicalPipelines =
    olderPhysicalPipelines &
    youngerPhysicalPipelines;
  if (issueSelection.pairing ==
        EEIssuePairing::ConcurrentWithStall &&
      sharedPhysicalPipelines !=
        static_cast<std::uint8_t>(
          EEPhysicalPipeline::COP1))
  {
    return false;
  }
  const bool compatible =
    sharedPhysicalPipelines == 0 ||
    issueSelection.pairing ==
      EEIssuePairing::ConcurrentWithStall;
  assert(compatible);
  return compatible;
}

bool EECore::isActivatedOIssueOperation(
  EEOperation operation)
{
  if (isEEBranchOperation(operation))
  {
    return false;
  }

  switch (operation)
  {
    case EEOperation::Nop:
    case EEOperation::SynchronizeLoadStore:
    case EEOperation::SynchronizePipeline:
    case EEOperation::ExceptionReturn:
    case EEOperation::SystemCall:
    case EEOperation::Breakpoint:
      return false;
    default:
      return true;
  }
}

bool EECore::memoryIssueCanJoinPair(
  const EEInstruction &instruction) const
{
  if (instruction.operation != EEOperation::StoreQuadword &&
      instruction.operation !=
        EEOperation::StoreQuadwordFromCOP2)
  {
    return true;
  }

  std::uint32_t address = static_cast<std::uint32_t>(
    generalRegisters[instruction.sourceRegister].low +
    signExtend16(instruction.immediate));
  if (instruction.operation == EEOperation::StoreQuadword)
  {
    address &= ~UINT32_C(0x0f);
  }
  return attachedBus().guestData128WriteReady(address);
}

bool EECore::branchLikelyTaken(
  const EEInstruction &instruction) const
{
  switch (instruction.operation)
  {
    case EEOperation::BranchCOP1FalseLikely:
      return !scoreboardCOP1Condition();
    case EEOperation::BranchCOP1TrueLikely:
      return scoreboardCOP1Condition();
    case EEOperation::BranchCOP2FalseLikely:
      return !attachedVU1().clockActive();
    case EEOperation::BranchCOP2TrueLikely:
      return attachedVU1().clockActive();
    default:
      break;
  }

  const std::uint64_t source =
    generalRegisters[instruction.sourceRegister].low;
  const std::uint64_t target =
    generalRegisters[instruction.targetRegister].low;
  const bool negative =
    (source & DOUBLEWORD_SIGN_BIT) != 0;
  switch (instruction.operation)
  {
    case EEOperation::BranchEqualLikely:
      return source == target;
    case EEOperation::BranchNotEqualLikely:
      return source != target;
    case EEOperation::BranchLessThanOrEqualZeroLikely:
      return negative || source == 0;
    case EEOperation::BranchGreaterThanZeroLikely:
      return !negative && source != 0;
    case EEOperation::BranchLessThanZeroLikely:
    case EEOperation::BranchLessThanZeroAndLinkLikely:
      return negative;
    case EEOperation::BranchGreaterThanOrEqualZeroLikely:
    case EEOperation::BranchGreaterThanOrEqualZeroAndLinkLikely:
      return !negative;
    default:
      throw std::logic_error(
        "EE instruction is not a branch-likely operation.");
  }
}

bool EECore::cop2ScoreboardBlocks(
  const EEInstruction &instruction) const
{
  const std::uint8_t registerIndex =
    instruction.destinationRegister;
  switch (instruction.operation)
  {
    case EEOperation::LoadQuadwordToCOP2:
    case EEOperation::StoreQuadwordFromCOP2:
      return
        attachedVU0().macroRegisterNumberWritePending(
          instruction.targetRegister);
    case EEOperation::QuadwordMoveFromCOP2:
      return
        ((instruction.raw & 1) != 0 &&
         attachedVU0().microModeActive()) ||
        attachedVU0().macroRegisterNumberWritePending(
          registerIndex);
    case EEOperation::QuadwordMoveToCOP2:
      return
        ((instruction.raw & 1) != 0 &&
         !attachedVU0().cop2WriteAvailable()) ||
        attachedVU0().macroRegisterNumberWritePending(
          registerIndex);
    case EEOperation::ControlMoveFromCOP2:
      return
        ((instruction.raw & 1) != 0 &&
         attachedVU0().microModeActive()) ||
        (registerIndex < 16 &&
         attachedVU0().macroRegisterNumberWritePending(
           registerIndex)) ||
        (attachedVU0().macroModeActive() &&
         (registerIndex == 16 ||
          registerIndex == 17));
    case EEOperation::ControlMoveToCOP2:
      return
        ((instruction.raw & 1) != 0 &&
         !attachedVU0().cop2WriteAvailable()) ||
        (registerIndex < 16 &&
         attachedVU0().macroRegisterNumberWritePending(
           registerIndex)) ||
        (attachedVU0().macroModeActive() &&
         (registerIndex == 16 ||
          registerIndex == 18));
    case EEOperation::VectorCallMicroSubroutine:
    case EEOperation::VectorCallMicroSubroutineRegister:
      return !attachedVU0().macroCallReady();
    case EEOperation::VectorMacroArithmetic:
      return !attachedVU0().macroInstructionReady(
        instruction.raw & UINT32_C(0x01ffffff));
    default:
      return false;
  }
}

void EECore::startExecution(std::uint32_t startAddress)
{
  const ExecutionStartMode mode =
    executionStartMode(startAddress);
  pc = startAddress;
  clearPendingException();
  state = EEExecutionState::Running;
  haltReason = EEStopReason::None;
  if (mode == ExecutionStartMode::Restart)
  {
    resetExecutionContinuation();
  }
  executingProgramOrder = 0;
  rejectedInstructionValue = 0;
  exceptionEnteredThisCycle = false;
}

void EECore::prepareFreshExecution(
  std::uint32_t entryPoint,
  std::uint32_t stackPointer,
  std::uint32_t returnAddress)
{
  reset();
  pc = entryPoint;
  generalRegisters[29].low = stackPointer;
  generalRegisters[31].low = returnAddress;
}

void EECore::haltExecution()
{
  state = EEExecutionState::Halted;
  haltReason = EEStopReason::HostHalt;
}

void EECore::enterInterruptException()
{
  enterException(EEException::Interrupt, pc, pc, 0);
}

bool EECore::clockActive() const
{
  return state == EEExecutionState::Running;
}

void EECore::clock()
{
  acceptanceRecords.clear();
  exceptionEnteredThisCycle = false;
  cycleTraceEventCount = 0;
  issueSelection = {};
  if (!clockActive())
  {
    return;
  }

  ++cycles;
  std::uint32_t completedCOP1LoadRegisters = 0;
  advancePendingCOP1(&completedCOP1LoadRegisters);
  if (exceptionEnteredThisCycle)
  {
    return;
  }
  if (cop1ScoreboardValue(
        COP1ScoreboardResource::MemoryException)
        .availability !=
          COP1ScoreboardAvailability::Unavailable &&
      interruptDeliverable())
  {
    enterInterruptException();
    return;
  }

  fillIssueFrontEnd();
  const bool hadPendingOperation =
    pendingMultiplyDivideActive();
  advancePendingMultiplyDivide(MACPipeline::MAC0);
  advancePendingMultiplyDivide(MACPipeline::MAC1);
  updateIssueSelection(completedCOP1LoadRegisters);
  if (hadPendingOperation &&
      pendingMultiplyDivideActive() &&
      (issueLatch.failure != IssueLatchFailure::None ||
       issueLatch.instruction.operation !=
         EEOperation::SynchronizePipeline))
  {
    return;
  }

  executeIssueGroup(
    issueSelectionCanExecuteConcurrently()
      ? EEIssueWidth::Two
      : EEIssueWidth::One,
    completedCOP1LoadRegisters);
}

EEIssueGroupExecutionResult EECore::executeIssueGroup(
  EEIssueWidth width,
  std::uint32_t completedLoadRegisters)
{
  return executeEEIssueGroupMembers(
    width,
    [this, completedLoadRegisters](
      EEIssueMemberPosition position)
    {
      return executeIssueMember(
        completedLoadRegisters,
        position);
    });
}

EEIssueMemberOutcome EECore::executeIssueMember(
  std::uint32_t completedLoadRegisters,
  EEIssueMemberPosition position)
{
  if (!issueLatch.valid)
  {
    return EEIssueMemberOutcome::Cancelled;
  }
  if (issueLatch.failure != IssueLatchFailure::None)
  {
    if (cop1MemoryExceptionPending())
    {
      pc = issueLatch.address;
      return EEIssueMemberOutcome::Stalled;
    }
    return resolveIssueLatchFailure();
  }

  const std::uint32_t instructionAddress =
    issueLatch.address;
  const std::uint32_t instructionValue =
    issueLatch.instruction.raw;
  const EEInstruction decoded = issueLatch.instruction;
  const EEAcceptanceMode acceptanceMode =
    branchDelayPending
      ? EEAcceptanceMode::DelaySlot
      : EEAcceptanceMode::Ordinary;
  const std::uint32_t completedBranchTarget =
    branchDelayTarget;
  if (position == EEIssueMemberPosition::Older &&
      isCOP1ManagedPipelineOperation(decoded.operation) &&
      cop1TransferReservedByStalledMove())
  {
    pc = instructionAddress;
    return EEIssueMemberOutcome::Stalled;
  }
  COP1ScoreboardHazard scoreboardHazard;
  if (cop1ScoreboardBlocks(
        decoded,
        completedLoadRegisters,
        &scoreboardHazard,
        position == EEIssueMemberPosition::Older
          ? COP1ScoreboardQuery::CandidateReadiness
          : COP1ScoreboardQuery::
              YoungerIssueGroupMember))
  {
    if (scoreboardHazard.completedLoad)
    {
      recordCycleTrace(
        CycleTraceKind::COP1LoadInterlock,
        instructionAddress,
        instructionValue,
        scoreboardHazard.registerIndex,
        static_cast<std::uint8_t>(
          scoreboardHazard.dependency));
    }
    else
    {
      std::uint64_t resource = 0;
      if (scoreboardHazard.resource ==
          COP1ScoreboardResource::Divider)
      {
        resource = static_cast<std::uint8_t>(
          scoreboardHazard.blockingOperation);
      }
      else
      {
        switch (scoreboardHazard.resource)
        {
          case COP1ScoreboardResource::FPR:
            resource = scoreboardHazard.registerIndex;
            break;
          case COP1ScoreboardResource::Accumulator:
            resource = FLOATING_POINT_REGISTER_COUNT;
            break;
          case COP1ScoreboardResource::FCR31:
            resource = FLOATING_POINT_REGISTER_COUNT + 1;
            break;
          case COP1ScoreboardResource::Condition:
            resource = FLOATING_POINT_REGISTER_COUNT + 2;
            break;
          case COP1ScoreboardResource::GPR:
            resource = FLOATING_POINT_REGISTER_COUNT + 3;
            break;
          case COP1ScoreboardResource::MemoryException:
            resource = FLOATING_POINT_REGISTER_COUNT + 4;
            break;
          case COP1ScoreboardResource::Divider:
            break;
        }
      }
      recordCycleTrace(
        CycleTraceKind::COP1ResourceInterlock,
        instructionAddress,
        instructionValue,
        resource,
        static_cast<std::uint8_t>(
          scoreboardHazard.dependency));
    }
    pc = instructionAddress;
    return EEIssueMemberOutcome::Stalled;
  }

  pc = instructionAddress + 4;
  if (nextEEProgramOrder == UINT64_MAX)
  {
    throw std::overflow_error(
      "EE instruction program order overflow.");
  }
  const std::uint64_t instructionProgramOrder =
    nextEEProgramOrder;
  executingProgramOrder = instructionProgramOrder;
  recordCycleTrace(
    CycleTraceKind::InstructionIssued,
    instructionAddress,
    instructionValue,
    static_cast<std::uint8_t>(decoded.operation),
    acceptanceMode == EEAcceptanceMode::DelaySlot);
  const EEInstructionExecutionOutcome execution =
    executeInstruction(decoded, instructionAddress);
  executingProgramOrder = 0;
  switch (execution)
  {
    case EEInstructionExecutionOutcome::Completed:
      break;
    case EEInstructionExecutionOutcome::Delayed:
      return EEIssueMemberOutcome::Stalled;
    case EEInstructionExecutionOutcome::Faulted:
      return EEIssueMemberOutcome::Faulted;
    case EEInstructionExecutionOutcome::Halted:
    case EEInstructionExecutionOutcome::Rejected:
      clearIssueFrontEnd();
      return EEIssueMemberOutcome::Cancelled;
  }

  ++nextEEProgramOrder;
  recordInstructionAcceptance(
    instructionProgramOrder,
    instructionAddress,
    decoded,
    acceptanceMode);
  promoteStagingLatch();
  if (acceptanceMode == EEAcceptanceMode::DelaySlot)
  {
    pc = completedBranchTarget;
    clearBranchDelayContinuation();
    clearIssueFrontEnd();
  }
  else if (decoded.operation == EEOperation::ExceptionReturn)
  {
    clearIssueFrontEnd();
  }
  return EEIssueMemberOutcome::Accepted;
}

void EECore::recordInstructionAcceptance(
  std::uint64_t programOrder,
  std::uint32_t address,
  const EEInstruction &instruction,
  EEAcceptanceMode mode)
{
  acceptanceRecords.append({
    programOrder,
    address,
    instruction,
    mode
  });
  applyInstructionAcceptanceEffects(
    acceptanceRecords[
      acceptanceRecords.size() - 1]);
  lastDecodedInstruction = instruction;
  lastAddress = address;
  lastInstructionValid = true;
}

void EECore::applyInstructionAcceptanceEffects(
  const EEAcceptanceRecord &record)
{
  const EEInstruction &instruction = record.instruction;
  if (isCOP1DividerOperation(instruction.operation))
  {
    if (record.mode == EEAcceptanceMode::DelaySlot)
    {
      recordCycleTrace(
        CycleTraceKind::COP1DividerHazard,
        record.address,
        instruction.raw,
        UINT64_C(1),
        branchInstructionAddress |
          (static_cast<std::uint64_t>(
            branchDelayTaken ? branchDelayTarget : 0) << 32));
    }

    std::uint64_t proximityReasons = 0;
    bool combinedTargetReason = false;
    if (cop1DividerPostDelayInstructions != 0)
    {
      proximityReasons |= UINT64_C(1) << 1;
      if (cop1DividerPostTargetInstructions != 0 &&
          cop1DividerPostDelayTaken &&
          cop1DividerPostDelayTargetAddress ==
            cop1DividerPostTargetAddress)
      {
        proximityReasons |= UINT64_C(1) << 2;
        combinedTargetReason = true;
      }
    }
    if (proximityReasons != 0)
    {
      recordCycleTrace(
        CycleTraceKind::COP1DividerHazard,
        record.address,
        instruction.raw,
        proximityReasons,
        cop1DividerPostDelayBranchAddress |
          (static_cast<std::uint64_t>(
            cop1DividerPostDelayTargetAddress) << 32));
    }
    if (cop1DividerPostTargetInstructions != 0 &&
        !combinedTargetReason)
    {
      recordCycleTrace(
        CycleTraceKind::COP1DividerHazard,
        record.address,
        instruction.raw,
        UINT64_C(1) << 2,
        static_cast<std::uint64_t>(
          cop1DividerPostTargetAddress) << 32);
    }
  }

  if (cop1DividerPostDelayInstructions != 0)
  {
    --cop1DividerPostDelayInstructions;
    if (cop1DividerPostDelayInstructions == 0)
    {
      cop1DividerPostDelayBranchAddress = 0;
      cop1DividerPostDelayTargetAddress = 0;
      cop1DividerPostDelayTaken = false;
    }
  }
  if (cop1DividerPostTargetInstructions != 0)
  {
    --cop1DividerPostTargetInstructions;
    if (cop1DividerPostTargetInstructions == 0)
    {
      cop1DividerPostTargetAddress = 0;
    }
  }

  if (record.mode == EEAcceptanceMode::DelaySlot)
  {
    cop1DividerPostDelayInstructions = 2;
    cop1DividerPostDelayBranchAddress =
      branchInstructionAddress;
    cop1DividerPostDelayTargetAddress =
      branchDelayTaken ? branchDelayTarget : 0;
    cop1DividerPostDelayTaken = branchDelayTaken;
    if (branchDelayTaken)
    {
      cop1DividerPostTargetInstructions = 2;
      cop1DividerPostTargetAddress = branchDelayTarget;
    }
  }
  else if (isEEBranchLikelyOperation(
             instruction.operation) &&
           !branchDelayPending)
  {
    cop1DividerPostDelayInstructions = 2;
    cop1DividerPostDelayBranchAddress = record.address;
    cop1DividerPostDelayTargetAddress = 0;
    cop1DividerPostDelayTaken = false;
  }

  shiftAmountOrdering.accept(instruction.operation);
}

EEInstructionExecutionOutcome EECore::executeInstruction(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  if (!shiftAmountOrdering.permits(instruction.operation))
  {
    haltUndefinedOperation(address, instruction.raw);
    rejectedInstructionValue = instruction.raw;
    return EEInstructionExecutionOutcome::Rejected;
  }
  if (!validateDelaySlotInstruction(instruction, address))
  {
    rejectedInstructionValue = instruction.raw;
    return EEInstructionExecutionOutcome::Rejected;
  }

  switch (instruction.operation)
  {
    case EEOperation::Nop:
    case EEOperation::SynchronizeLoadStore:
    case EEOperation::SynchronizePipeline:
      return EEInstructionExecutionOutcome::Completed;
    case EEOperation::ExceptionReturn:
      return executeExceptionReturn(instruction);
    case EEOperation::SystemCall:
    case EEOperation::Breakpoint:
      return executeSoftwareException(instruction, address);
    case EEOperation::MoveWordFromCOP1:
    case EEOperation::MoveWordToCOP1:
    case EEOperation::MoveControlWordFromCOP1:
    case EEOperation::MoveControlWordToCOP1:
    case EEOperation::MoveSingleCOP1:
      return executeCOP1RegisterMove(instruction, address);
    case EEOperation::SquareRootSingleCOP1:
    case EEOperation::ReciprocalSquareRootSingleCOP1:
    case EEOperation::DivideSingleCOP1:
      return executeCOP1Divider(instruction, address);
    case EEOperation::AbsoluteSingleCOP1:
    case EEOperation::NegateSingleCOP1:
    case EEOperation::MaximumSingleCOP1:
    case EEOperation::MinimumSingleCOP1:
    case EEOperation::ConvertWordToSingleCOP1:
    case EEOperation::ConvertSingleToWordCOP1:
    case EEOperation::AddSingleCOP1:
    case EEOperation::SubtractSingleCOP1:
    case EEOperation::AddSingleToAccumulatorCOP1:
    case EEOperation::SubtractSingleToAccumulatorCOP1:
    case EEOperation::MultiplySingleCOP1:
    case EEOperation::MultiplySingleToAccumulatorCOP1:
    case EEOperation::MultiplyAddSingleCOP1:
    case EEOperation::MultiplyAddSingleToAccumulatorCOP1:
    case EEOperation::MultiplySubtractSingleCOP1:
    case EEOperation::MultiplySubtractSingleToAccumulatorCOP1:
    case EEOperation::CompareFalseSingleCOP1:
    case EEOperation::CompareEqualSingleCOP1:
    case EEOperation::CompareLessThanSingleCOP1:
    case EEOperation::CompareLessThanOrEqualSingleCOP1:
      return executeCOP1StagedOperation(instruction, address);
    case EEOperation::BranchCOP1False:
    case EEOperation::BranchCOP1FalseLikely:
    case EEOperation::BranchCOP1True:
    case EEOperation::BranchCOP1TrueLikely:
      return executeCOP1Branch(instruction, address);
    case EEOperation::ShiftLeftLogicalWord:
    case EEOperation::ShiftRightLogicalWord:
    case EEOperation::ShiftRightArithmeticWord:
    case EEOperation::ShiftLeftLogicalVariableWord:
    case EEOperation::ShiftRightLogicalVariableWord:
    case EEOperation::ShiftRightArithmeticVariableWord:
      return executeWordShift(instruction, address);
    case EEOperation::ShiftLeftLogicalVariableDoubleword:
    case EEOperation::ShiftRightLogicalVariableDoubleword:
    case EEOperation::ShiftRightArithmeticVariableDoubleword:
    case EEOperation::ShiftLeftLogicalDoubleword:
    case EEOperation::ShiftRightLogicalDoubleword:
    case EEOperation::ShiftRightArithmeticDoubleword:
    case EEOperation::ShiftLeftLogicalDoubleword32:
    case EEOperation::ShiftRightLogicalDoubleword32:
    case EEOperation::ShiftRightArithmeticDoubleword32:
      return executeDoublewordShift(instruction);
    case EEOperation::AddWord:
    case EEOperation::AddUnsignedWord:
    case EEOperation::SubtractWord:
    case EEOperation::SubtractUnsignedWord:
      return executeWordArithmetic(instruction, address);
    case EEOperation::AddDoubleword:
    case EEOperation::AddUnsignedDoubleword:
    case EEOperation::SubtractDoubleword:
    case EEOperation::SubtractUnsignedDoubleword:
      return executeDoublewordArithmetic(instruction, address);
    case EEOperation::And:
    case EEOperation::Or:
    case EEOperation::Xor:
    case EEOperation::Nor:
      return executeRegisterLogical(instruction);
    case EEOperation::SetLessThan:
    case EEOperation::SetLessThanUnsigned:
      return executeRegisterCompare(instruction);
    case EEOperation::AddImmediateWord:
    case EEOperation::AddImmediateUnsignedWord:
      return executeImmediateWordArithmetic(instruction, address);
    case EEOperation::AddImmediateDoubleword:
    case EEOperation::AddImmediateUnsignedDoubleword:
      return executeImmediateDoublewordArithmetic(
        instruction,
        address);
    case EEOperation::SetLessThanImmediate:
    case EEOperation::SetLessThanImmediateUnsigned:
      return executeImmediateCompare(instruction);
    case EEOperation::AndImmediate:
    case EEOperation::OrImmediate:
    case EEOperation::XorImmediate:
    case EEOperation::LoadUpperImmediate:
      return executeImmediateLogical(instruction);
    case EEOperation::MoveFromHI:
    case EEOperation::MoveToHI:
    case EEOperation::MoveFromLO:
    case EEOperation::MoveToLO:
    case EEOperation::MoveFromHI1:
    case EEOperation::MoveToHI1:
    case EEOperation::MoveFromLO1:
    case EEOperation::MoveToLO1:
      return executeMACRegisterMove(instruction);
    case EEOperation::MoveFromShiftAmount:
    case EEOperation::MoveToShiftAmount:
    case EEOperation::MoveByteCountToShiftAmount:
    case EEOperation::MoveHalfwordCountToShiftAmount:
      return executeShiftAmountOperation(instruction);
    case EEOperation::LoadByte:
    case EEOperation::LoadByteUnsigned:
    case EEOperation::StoreByte:
      return executeByteMemory(instruction, address);
    case EEOperation::LoadHalfword:
    case EEOperation::LoadHalfwordUnsigned:
    case EEOperation::StoreHalfword:
      return executeHalfwordMemory(instruction, address);
    case EEOperation::LoadWord:
    case EEOperation::LoadWordUnsigned:
    case EEOperation::StoreWord:
      return executeWordMemory(instruction, address);
    case EEOperation::LoadWordToCOP1:
    case EEOperation::StoreWordFromCOP1:
      return executeCOP1Memory(instruction, address);
    case EEOperation::LoadWordLeft:
    case EEOperation::LoadWordRight:
    case EEOperation::StoreWordLeft:
    case EEOperation::StoreWordRight:
      return executeWordMergeMemory(instruction, address);
    case EEOperation::LoadDoubleword:
    case EEOperation::StoreDoubleword:
      return executeDoublewordMemory(instruction, address);
    case EEOperation::LoadDoublewordLeft:
    case EEOperation::LoadDoublewordRight:
    case EEOperation::StoreDoublewordLeft:
    case EEOperation::StoreDoublewordRight:
      return executeDoublewordMergeMemory(instruction, address);
    case EEOperation::LoadQuadword:
    case EEOperation::StoreQuadword:
      return executeQuadwordMemory(instruction, address);
    case EEOperation::LoadQuadwordToCOP2:
    case EEOperation::StoreQuadwordFromCOP2:
      return executeCOP2Memory(instruction, address);
    case EEOperation::QuadwordMoveFromCOP2:
    case EEOperation::QuadwordMoveToCOP2:
      return executeCOP2VectorMove(instruction, address);
    case EEOperation::ControlMoveFromCOP2:
    case EEOperation::ControlMoveToCOP2:
      return executeCOP2ControlMove(instruction, address);
    case EEOperation::BranchCOP2False:
    case EEOperation::BranchCOP2FalseLikely:
    case EEOperation::BranchCOP2True:
    case EEOperation::BranchCOP2TrueLikely:
      return executeCOP2Branch(instruction, address);
    case EEOperation::VectorCallMicroSubroutine:
    case EEOperation::VectorCallMicroSubroutineRegister:
      return executeCOP2MicroCall(instruction, address);
    case EEOperation::VectorMacroArithmetic:
      return executeCOP2Macro(instruction, address);
    case EEOperation::Jump:
    case EEOperation::JumpAndLink:
    case EEOperation::JumpRegister:
    case EEOperation::JumpAndLinkRegister:
      return executeJump(instruction, address);
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
      return executeIntegerBranch(instruction, address);
    case EEOperation::MultiplyWord:
    case EEOperation::MultiplyUnsignedWord:
    case EEOperation::MultiplyWord1:
    case EEOperation::MultiplyUnsignedWord1:
    case EEOperation::MultiplyAddWord:
    case EEOperation::MultiplyAddUnsignedWord:
    case EEOperation::MultiplyAddWord1:
    case EEOperation::MultiplyAddUnsignedWord1:
      return executeMultiply(instruction, address);
    case EEOperation::DivideWord:
    case EEOperation::DivideUnsignedWord:
    case EEOperation::DivideWord1:
    case EEOperation::DivideUnsignedWord1:
      return executeDivide(instruction, address);
    case EEOperation::Count:
      break;
  }

  haltUndefinedOperation(address, instruction.raw);
  return EEInstructionExecutionOutcome::Rejected;
}

EEInstructionExecutionOutcome EECore::executeWordShift(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::ShiftLeftLogicalWord:
    case EEOperation::ShiftRightLogicalWord:
    case EEOperation::ShiftRightArithmeticWord:
    case EEOperation::ShiftLeftLogicalVariableWord:
    case EEOperation::ShiftRightLogicalVariableWord:
    case EEOperation::ShiftRightArithmeticVariableWord:
      break;
    default:
      throw std::logic_error(
        "EE word-shift handler received an incompatible operation.");
  }
  if (!requireWordValue(
        instruction.targetRegister,
        address,
        instruction.raw))
  {
    return EEInstructionExecutionOutcome::Halted;
  }

  const std::uint64_t source =
    generalRegisters[instruction.sourceRegister].low;
  const std::uint32_t word =
    static_cast<std::uint32_t>(
      generalRegisters[instruction.targetRegister].low);
  const bool immediate =
    instruction.operation == EEOperation::ShiftLeftLogicalWord ||
    instruction.operation == EEOperation::ShiftRightLogicalWord ||
    instruction.operation == EEOperation::ShiftRightArithmeticWord;
  const std::uint8_t amount = immediate
    ? instruction.shiftAmount
    : source & 0x1f;
  std::uint32_t result = 0;
  switch (instruction.operation)
  {
    case EEOperation::ShiftLeftLogicalWord:
    case EEOperation::ShiftLeftLogicalVariableWord:
      result = word << amount;
      break;
    case EEOperation::ShiftRightLogicalWord:
    case EEOperation::ShiftRightLogicalVariableWord:
      result = word >> amount;
      break;
    case EEOperation::ShiftRightArithmeticWord:
    case EEOperation::ShiftRightArithmeticVariableWord:
      result = arithmeticShiftRight32(word, amount);
      break;
    default:
      throw std::logic_error(
        "EE word-shift handler received an incompatible operation.");
  }
  writeWord(instruction.destinationRegister, result);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeDoublewordShift(
  const EEInstruction &instruction)
{
  const std::uint64_t source =
    generalRegisters[instruction.sourceRegister].low;
  const std::uint64_t target =
    generalRegisters[instruction.targetRegister].low;
  std::uint64_t result = 0;
  switch (instruction.operation)
  {
    case EEOperation::ShiftLeftLogicalVariableDoubleword:
      result = target << (source & 0x3f);
      break;
    case EEOperation::ShiftRightLogicalVariableDoubleword:
      result = target >> (source & 0x3f);
      break;
    case EEOperation::ShiftRightArithmeticVariableDoubleword:
      result = arithmeticShiftRight64(target, source & 0x3f);
      break;
    case EEOperation::ShiftLeftLogicalDoubleword:
      result = target << instruction.shiftAmount;
      break;
    case EEOperation::ShiftRightLogicalDoubleword:
      result = target >> instruction.shiftAmount;
      break;
    case EEOperation::ShiftRightArithmeticDoubleword:
      result =
        arithmeticShiftRight64(target, instruction.shiftAmount);
      break;
    case EEOperation::ShiftLeftLogicalDoubleword32:
      result = target << (instruction.shiftAmount + 32);
      break;
    case EEOperation::ShiftRightLogicalDoubleword32:
      result = target >> (instruction.shiftAmount + 32);
      break;
    case EEOperation::ShiftRightArithmeticDoubleword32:
      result = arithmeticShiftRight64(
        target,
        instruction.shiftAmount + 32);
      break;
    default:
      throw std::logic_error(
        "EE doubleword-shift handler received an "
        "incompatible operation.");
  }
  writeLowDoubleword(instruction.destinationRegister, result);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeRegisterLogical(
  const EEInstruction &instruction)
{
  const std::uint64_t source =
    generalRegisters[instruction.sourceRegister].low;
  const std::uint64_t target =
    generalRegisters[instruction.targetRegister].low;
  std::uint64_t result = 0;
  switch (instruction.operation)
  {
    case EEOperation::And:
      result = source & target;
      break;
    case EEOperation::Or:
      result = source | target;
      break;
    case EEOperation::Xor:
      result = source ^ target;
      break;
    case EEOperation::Nor:
      result = ~(source | target);
      break;
    default:
      throw std::logic_error(
        "EE register-logical handler received an "
        "incompatible operation.");
  }
  writeLowDoubleword(instruction.destinationRegister, result);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeRegisterCompare(
  const EEInstruction &instruction)
{
  const std::uint64_t source =
    generalRegisters[instruction.sourceRegister].low;
  const std::uint64_t target =
    generalRegisters[instruction.targetRegister].low;
  std::uint64_t result = 0;
  switch (instruction.operation)
  {
    case EEOperation::SetLessThan:
      result = signedLess(source, target) ? 1 : 0;
      break;
    case EEOperation::SetLessThanUnsigned:
      result = source < target ? 1 : 0;
      break;
    default:
      throw std::logic_error(
        "EE register-compare handler received an "
        "incompatible operation.");
  }
  writeLowDoubleword(instruction.destinationRegister, result);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeImmediateCompare(
  const EEInstruction &instruction)
{
  const std::uint64_t source =
    generalRegisters[instruction.sourceRegister].low;
  const std::uint64_t immediate =
    signExtend16(instruction.immediate);
  std::uint64_t result = 0;
  switch (instruction.operation)
  {
    case EEOperation::SetLessThanImmediate:
      result = signedLess(source, immediate) ? 1 : 0;
      break;
    case EEOperation::SetLessThanImmediateUnsigned:
      result = source < immediate ? 1 : 0;
      break;
    default:
      throw std::logic_error(
        "EE immediate-compare handler received an "
        "incompatible operation.");
  }
  writeLowDoubleword(instruction.targetRegister, result);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeImmediateLogical(
  const EEInstruction &instruction)
{
  const std::uint64_t source =
    generalRegisters[instruction.sourceRegister].low;
  std::uint64_t result = 0;
  switch (instruction.operation)
  {
    case EEOperation::AndImmediate:
      result = source & instruction.immediate;
      break;
    case EEOperation::OrImmediate:
      result = source | instruction.immediate;
      break;
    case EEOperation::XorImmediate:
      result = source ^ instruction.immediate;
      break;
    case EEOperation::LoadUpperImmediate:
      result = signExtendWord(
        static_cast<std::uint32_t>(
          instruction.immediate) << 16);
      break;
    default:
      throw std::logic_error(
        "EE immediate-logical handler received an "
        "incompatible operation.");
  }
  writeLowDoubleword(instruction.targetRegister, result);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeWordArithmetic(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::AddWord:
    case EEOperation::AddUnsignedWord:
    case EEOperation::SubtractWord:
    case EEOperation::SubtractUnsignedWord:
      break;
    default:
      throw std::logic_error(
        "EE word-arithmetic handler received an incompatible "
        "operation.");
  }
  if (!requireWordValue(
        instruction.sourceRegister,
        address,
        instruction.raw) ||
      !requireWordValue(
        instruction.targetRegister,
        address,
        instruction.raw))
  {
    return EEInstructionExecutionOutcome::Halted;
  }

  const std::uint32_t left = static_cast<std::uint32_t>(
    generalRegisters[instruction.sourceRegister].low);
  const std::uint32_t right = static_cast<std::uint32_t>(
    generalRegisters[instruction.targetRegister].low);
  const bool subtract =
    instruction.operation == EEOperation::SubtractWord ||
    instruction.operation == EEOperation::SubtractUnsignedWord;
  const std::uint32_t result =
    subtract ? left - right : left + right;
  const bool trapping =
    instruction.operation == EEOperation::AddWord ||
    instruction.operation == EEOperation::SubtractWord;
  const bool overflow = subtract
    ? subtractOverflow32(left, right, result)
    : addOverflow32(left, right, result);
  if (trapping && overflow)
  {
    return raiseArithmeticOverflow(address, instruction.raw);
  }
  writeWord(instruction.destinationRegister, result);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeDoublewordArithmetic(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::AddDoubleword:
    case EEOperation::AddUnsignedDoubleword:
    case EEOperation::SubtractDoubleword:
    case EEOperation::SubtractUnsignedDoubleword:
      break;
    default:
      throw std::logic_error(
        "EE doubleword-arithmetic handler received an "
        "incompatible operation.");
  }

  const std::uint64_t left =
    generalRegisters[instruction.sourceRegister].low;
  const std::uint64_t right =
    generalRegisters[instruction.targetRegister].low;
  const bool subtract =
    instruction.operation == EEOperation::SubtractDoubleword ||
    instruction.operation ==
      EEOperation::SubtractUnsignedDoubleword;
  const std::uint64_t result =
    subtract ? left - right : left + right;
  const bool trapping =
    instruction.operation == EEOperation::AddDoubleword ||
    instruction.operation == EEOperation::SubtractDoubleword;
  const bool overflow = subtract
    ? subtractOverflow64(left, right, result)
    : addOverflow64(left, right, result);
  if (trapping && overflow)
  {
    return raiseArithmeticOverflow(address, instruction.raw);
  }
  writeLowDoubleword(instruction.destinationRegister, result);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeImmediateWordArithmetic(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::AddImmediateWord:
    case EEOperation::AddImmediateUnsignedWord:
      break;
    default:
      throw std::logic_error(
        "EE immediate-word-arithmetic handler received an "
        "incompatible operation.");
  }
  if (!requireWordValue(
        instruction.sourceRegister,
        address,
        instruction.raw))
  {
    return EEInstructionExecutionOutcome::Halted;
  }

  const std::uint32_t left = static_cast<std::uint32_t>(
    generalRegisters[instruction.sourceRegister].low);
  const std::uint32_t right = static_cast<std::uint32_t>(
    signExtend16(instruction.immediate));
  const std::uint32_t result = left + right;
  if (instruction.operation == EEOperation::AddImmediateWord &&
      addOverflow32(left, right, result))
  {
    return raiseArithmeticOverflow(address, instruction.raw);
  }
  writeWord(instruction.targetRegister, result);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome
EECore::executeImmediateDoublewordArithmetic(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::AddImmediateDoubleword:
    case EEOperation::AddImmediateUnsignedDoubleword:
      break;
    default:
      throw std::logic_error(
        "EE immediate-doubleword-arithmetic handler received an "
        "incompatible operation.");
  }

  const std::uint64_t left =
    generalRegisters[instruction.sourceRegister].low;
  const std::uint64_t right =
    signExtend16(instruction.immediate);
  const std::uint64_t result = left + right;
  if (instruction.operation ==
        EEOperation::AddImmediateDoubleword &&
      addOverflow64(left, right, result))
  {
    return raiseArithmeticOverflow(address, instruction.raw);
  }
  writeLowDoubleword(instruction.targetRegister, result);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeMACRegisterMove(
  const EEInstruction &instruction)
{
  const std::uint64_t source =
    generalRegisters[instruction.sourceRegister].low;
  switch (instruction.operation)
  {
    case EEOperation::MoveFromHI:
      writeLowDoubleword(
        instruction.destinationRegister,
        hiRegister);
      break;
    case EEOperation::MoveToHI:
      hiRegister = source;
      break;
    case EEOperation::MoveFromLO:
      writeLowDoubleword(
        instruction.destinationRegister,
        loRegister);
      break;
    case EEOperation::MoveToLO:
      loRegister = source;
      break;
    case EEOperation::MoveFromHI1:
      writeLowDoubleword(
        instruction.destinationRegister,
        hi1Register);
      break;
    case EEOperation::MoveToHI1:
      hi1Register = source;
      break;
    case EEOperation::MoveFromLO1:
      writeLowDoubleword(
        instruction.destinationRegister,
        lo1Register);
      break;
    case EEOperation::MoveToLO1:
      lo1Register = source;
      break;
    default:
      throw std::logic_error(
        "EE MAC-register-move handler received an incompatible "
        "operation.");
  }
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeShiftAmountOperation(
  const EEInstruction &instruction)
{
  const std::uint64_t source =
    generalRegisters[instruction.sourceRegister].low;
  switch (instruction.operation)
  {
    case EEOperation::MoveFromShiftAmount:
      writeLowDoubleword(
        instruction.destinationRegister,
        saRegister);
      break;
    case EEOperation::MoveToShiftAmount:
      saRegister = static_cast<std::uint32_t>(source);
      break;
    case EEOperation::MoveByteCountToShiftAmount:
      saRegister =
        ((static_cast<std::uint32_t>(source) ^
          instruction.immediate) & 0x0f) * 8;
      break;
    case EEOperation::MoveHalfwordCountToShiftAmount:
      saRegister =
        ((static_cast<std::uint32_t>(source) ^
          instruction.immediate) & 0x07) * 16;
      break;
    default:
      throw std::logic_error(
        "EE shift-amount handler received an incompatible "
        "operation.");
  }
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeByteMemory(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  const std::uint64_t source =
    generalRegisters[instruction.sourceRegister].low;
  const std::uint32_t dataAddress =
    static_cast<std::uint32_t>(
      source + signExtend16(instruction.immediate));
  switch (instruction.operation)
  {
    case EEOperation::LoadByte:
    case EEOperation::LoadByteUnsigned:
    {
      std::uint8_t value = 0;
      const bool succeeded =
        attachedBus().readData8(dataAddress, &value);
      recordMemoryTrace(
        dataAddress,
        1,
        false,
        succeeded,
        succeeded ? value : 0);
      if (!succeeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorLoad,
          address,
          dataAddress,
          instruction.raw);
      }
      writeLowDoubleword(
        instruction.targetRegister,
        instruction.operation == EEOperation::LoadByte &&
          (value & 0x80) != 0
          ? UINT64_C(0xffffffffffffff00) | value
          : value);
      return EEInstructionExecutionOutcome::Completed;
    }
    case EEOperation::StoreByte:
    {
      const std::uint8_t value =
        static_cast<std::uint8_t>(
          generalRegisters[instruction.targetRegister].low);
      const bool succeeded =
        attachedBus().writeData8(dataAddress, value);
      recordMemoryTrace(
        dataAddress,
        1,
        true,
        succeeded,
        value);
      if (!succeeded)
      {
        return raiseDataAccessException(
          EEException::DataBusErrorStore,
          address,
          dataAddress,
          instruction.raw);
      }
      return EEInstructionExecutionOutcome::Completed;
    }
    default:
      throw std::logic_error(
        "EE byte-memory handler received an "
        "incompatible operation.");
  }
}

EEInstructionExecutionOutcome EECore::executeHalfwordMemory(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::LoadHalfword:
    case EEOperation::LoadHalfwordUnsigned:
    case EEOperation::StoreHalfword:
      break;
    default:
      throw std::logic_error(
        "EE halfword-memory handler received an incompatible "
        "operation.");
  }

  const std::uint32_t dataAddress =
    static_cast<std::uint32_t>(
      generalRegisters[instruction.sourceRegister].low +
      signExtend16(instruction.immediate));
  const bool store =
    instruction.operation == EEOperation::StoreHalfword;
  if ((dataAddress & 1) != 0)
  {
    return raiseDataAccessException(
      store
        ? EEException::AddressErrorStore
        : EEException::AddressErrorLoadOrFetch,
      address,
      dataAddress,
      instruction.raw);
  }
  if (store)
  {
    const std::uint16_t value =
      static_cast<std::uint16_t>(
        generalRegisters[instruction.targetRegister].low);
    const bool succeeded =
      attachedBus().writeData16(dataAddress, value);
    recordMemoryTrace(
      dataAddress,
      2,
      true,
      succeeded,
      value);
    if (!succeeded)
    {
      return raiseDataAccessException(
        EEException::DataBusErrorStore,
        address,
        dataAddress,
        instruction.raw);
    }
    return EEInstructionExecutionOutcome::Completed;
  }

  std::uint16_t value = 0;
  const bool succeeded =
    attachedBus().readData16(dataAddress, &value);
  recordMemoryTrace(
    dataAddress,
    2,
    false,
    succeeded,
    succeeded ? value : 0);
  if (!succeeded)
  {
    return raiseDataAccessException(
      EEException::DataBusErrorLoad,
      address,
      dataAddress,
      instruction.raw);
  }
  writeLowDoubleword(
    instruction.targetRegister,
    instruction.operation == EEOperation::LoadHalfword
      ? signExtend16(value)
      : value);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeWordMemory(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::LoadWord:
    case EEOperation::LoadWordUnsigned:
    case EEOperation::StoreWord:
      break;
    default:
      throw std::logic_error(
        "EE word-memory handler received an incompatible "
        "operation.");
  }

  const std::uint32_t dataAddress =
    static_cast<std::uint32_t>(
      generalRegisters[instruction.sourceRegister].low +
      signExtend16(instruction.immediate));
  const bool store =
    instruction.operation == EEOperation::StoreWord;
  if ((dataAddress & 3) != 0)
  {
    return raiseDataAccessException(
      store
        ? EEException::AddressErrorStore
        : EEException::AddressErrorLoadOrFetch,
      address,
      dataAddress,
      instruction.raw);
  }
  if (store)
  {
    const std::uint32_t value =
      static_cast<std::uint32_t>(
        generalRegisters[instruction.targetRegister].low);
    const bool succeeded =
      attachedBus().writeData32(dataAddress, value);
    recordMemoryTrace(
      dataAddress,
      4,
      true,
      succeeded,
      value);
    if (!succeeded)
    {
      return raiseDataAccessException(
        EEException::DataBusErrorStore,
        address,
        dataAddress,
        instruction.raw);
    }
    return EEInstructionExecutionOutcome::Completed;
  }

  std::uint32_t value = 0;
  const bool succeeded =
    attachedBus().readData32(dataAddress, &value);
  recordMemoryTrace(
    dataAddress,
    4,
    false,
    succeeded,
    succeeded ? value : 0);
  if (!succeeded)
  {
    return raiseDataAccessException(
      EEException::DataBusErrorLoad,
      address,
      dataAddress,
      instruction.raw);
  }
  writeLowDoubleword(
    instruction.targetRegister,
    instruction.operation == EEOperation::LoadWord
      ? signExtendWord(value)
      : value);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeWordMergeMemory(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::LoadWordLeft:
    case EEOperation::LoadWordRight:
    case EEOperation::StoreWordLeft:
    case EEOperation::StoreWordRight:
      break;
    default:
      throw std::logic_error(
        "EE word-merge-memory handler received an incompatible "
        "operation.");
  }

  const std::uint64_t target =
    generalRegisters[instruction.targetRegister].low;
  const std::uint32_t dataAddress =
    static_cast<std::uint32_t>(
      generalRegisters[instruction.sourceRegister].low +
      signExtend16(instruction.immediate));
  const std::uint32_t alignedAddress =
    dataAddress & ~UINT32_C(3);
  const bool store =
    instruction.operation == EEOperation::StoreWordLeft ||
    instruction.operation == EEOperation::StoreWordRight;
  std::uint32_t memory = 0;
  const bool readSucceeded =
    attachedBus().readData32(alignedAddress, &memory);
  recordMemoryTrace(
    alignedAddress,
    4,
    false,
    readSucceeded,
    readSucceeded ? memory : 0);
  if (!readSucceeded)
  {
    return raiseDataAccessException(
      store
        ? EEException::DataBusErrorStore
        : EEException::DataBusErrorLoad,
      address,
      dataAddress,
      instruction.raw);
  }

  const std::uint8_t byteOffset = dataAddress & 3;
  if (!store)
  {
    std::uint32_t result =
      static_cast<std::uint32_t>(target);
    if (instruction.operation == EEOperation::LoadWordLeft)
    {
      for (std::uint8_t memoryByte = 0;
           memoryByte <= byteOffset;
           ++memoryByte)
      {
        const std::uint8_t registerByte =
          3 - byteOffset + memoryByte;
        const std::uint32_t mask =
          UINT32_C(0xff) << (registerByte * 8);
        result =
          (result & ~mask) |
          (((memory >> (memoryByte * 8)) & 0xff) <<
           (registerByte * 8));
      }
      writeLowDoubleword(
        instruction.targetRegister,
        signExtendWord(result));
    }
    else
    {
      for (std::uint8_t memoryByte = byteOffset;
           memoryByte < 4;
           ++memoryByte)
      {
        const std::uint8_t registerByte =
          memoryByte - byteOffset;
        const std::uint32_t mask =
          UINT32_C(0xff) << (registerByte * 8);
        result =
          (result & ~mask) |
          (((memory >> (memoryByte * 8)) & 0xff) <<
           (registerByte * 8));
      }
      writeLowDoubleword(
        instruction.targetRegister,
        byteOffset == 0
          ? signExtendWord(result)
          : (target & UINT64_C(0xffffffff00000000)) |
            result);
    }
    return EEInstructionExecutionOutcome::Completed;
  }

  const std::uint32_t registerValue =
    static_cast<std::uint32_t>(target);
  if (instruction.operation == EEOperation::StoreWordLeft)
  {
    for (std::uint8_t memoryByte = 0;
         memoryByte <= byteOffset;
         ++memoryByte)
    {
      const std::uint8_t registerByte =
        3 - byteOffset + memoryByte;
      const std::uint32_t mask =
        UINT32_C(0xff) << (memoryByte * 8);
      memory =
        (memory & ~mask) |
        (((registerValue >> (registerByte * 8)) & 0xff) <<
         (memoryByte * 8));
    }
  }
  else
  {
    for (std::uint8_t memoryByte = byteOffset;
         memoryByte < 4;
         ++memoryByte)
    {
      const std::uint8_t registerByte =
        memoryByte - byteOffset;
      const std::uint32_t mask =
        UINT32_C(0xff) << (memoryByte * 8);
      memory =
        (memory & ~mask) |
        (((registerValue >> (registerByte * 8)) & 0xff) <<
         (memoryByte * 8));
    }
  }
  const bool writeSucceeded =
    attachedBus().writeData32(alignedAddress, memory);
  recordMemoryTrace(
    alignedAddress,
    4,
    true,
    writeSucceeded,
    memory);
  if (!writeSucceeded)
  {
    return raiseDataAccessException(
      EEException::DataBusErrorStore,
      address,
      dataAddress,
      instruction.raw);
  }
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeDoublewordMemory(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::LoadDoubleword:
    case EEOperation::StoreDoubleword:
      break;
    default:
      throw std::logic_error(
        "EE doubleword-memory handler received an incompatible "
        "operation.");
  }

  const std::uint32_t dataAddress =
    static_cast<std::uint32_t>(
      generalRegisters[instruction.sourceRegister].low +
      signExtend16(instruction.immediate));
  const bool store =
    instruction.operation == EEOperation::StoreDoubleword;
  if ((dataAddress & 7) != 0)
  {
    return raiseDataAccessException(
      store
        ? EEException::AddressErrorStore
        : EEException::AddressErrorLoadOrFetch,
      address,
      dataAddress,
      instruction.raw);
  }
  if (store)
  {
    const std::uint64_t value =
      generalRegisters[instruction.targetRegister].low;
    const bool succeeded =
      attachedBus().writeData64(dataAddress, value);
    recordMemoryTrace(
      dataAddress,
      8,
      true,
      succeeded,
      value);
    if (!succeeded)
    {
      return raiseDataAccessException(
        EEException::DataBusErrorStore,
        address,
        dataAddress,
        instruction.raw);
    }
    return EEInstructionExecutionOutcome::Completed;
  }

  std::uint64_t value = 0;
  const bool succeeded =
    attachedBus().readData64(dataAddress, &value);
  recordMemoryTrace(
    dataAddress,
    8,
    false,
    succeeded,
    succeeded ? value : 0);
  if (!succeeded)
  {
    return raiseDataAccessException(
      EEException::DataBusErrorLoad,
      address,
      dataAddress,
      instruction.raw);
  }
  writeLowDoubleword(instruction.targetRegister, value);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeDoublewordMergeMemory(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::LoadDoublewordLeft:
    case EEOperation::LoadDoublewordRight:
    case EEOperation::StoreDoublewordLeft:
    case EEOperation::StoreDoublewordRight:
      break;
    default:
      throw std::logic_error(
        "EE doubleword-merge-memory handler received an "
        "incompatible operation.");
  }

  const std::uint64_t target =
    generalRegisters[instruction.targetRegister].low;
  const std::uint32_t dataAddress =
    static_cast<std::uint32_t>(
      generalRegisters[instruction.sourceRegister].low +
      signExtend16(instruction.immediate));
  const std::uint32_t alignedAddress =
    dataAddress & ~UINT32_C(7);
  const bool store =
    instruction.operation == EEOperation::StoreDoublewordLeft ||
    instruction.operation == EEOperation::StoreDoublewordRight;
  std::uint64_t memory = 0;
  const bool readSucceeded =
    attachedBus().readData64(alignedAddress, &memory);
  recordMemoryTrace(
    alignedAddress,
    8,
    false,
    readSucceeded,
    readSucceeded ? memory : 0);
  if (!readSucceeded)
  {
    return raiseDataAccessException(
      store
        ? EEException::DataBusErrorStore
        : EEException::DataBusErrorLoad,
      address,
      dataAddress,
      instruction.raw);
  }

  const std::uint8_t byteOffset = dataAddress & 7;
  if (!store)
  {
    std::uint64_t result = target;
    if (instruction.operation ==
        EEOperation::LoadDoublewordLeft)
    {
      for (std::uint8_t memoryByte = 0;
           memoryByte <= byteOffset;
           ++memoryByte)
      {
        const std::uint8_t registerByte =
          7 - byteOffset + memoryByte;
        const std::uint64_t mask =
          UINT64_C(0xff) << (registerByte * 8);
        result =
          (result & ~mask) |
          (((memory >> (memoryByte * 8)) & 0xff) <<
           (registerByte * 8));
      }
    }
    else
    {
      for (std::uint8_t memoryByte = byteOffset;
           memoryByte < 8;
           ++memoryByte)
      {
        const std::uint8_t registerByte =
          memoryByte - byteOffset;
        const std::uint64_t mask =
          UINT64_C(0xff) << (registerByte * 8);
        result =
          (result & ~mask) |
          (((memory >> (memoryByte * 8)) & 0xff) <<
           (registerByte * 8));
      }
    }
    writeLowDoubleword(instruction.targetRegister, result);
    return EEInstructionExecutionOutcome::Completed;
  }

  if (instruction.operation ==
      EEOperation::StoreDoublewordLeft)
  {
    for (std::uint8_t memoryByte = 0;
         memoryByte <= byteOffset;
         ++memoryByte)
    {
      const std::uint8_t registerByte =
        7 - byteOffset + memoryByte;
      const std::uint64_t mask =
        UINT64_C(0xff) << (memoryByte * 8);
      memory =
        (memory & ~mask) |
        (((target >> (registerByte * 8)) & 0xff) <<
         (memoryByte * 8));
    }
  }
  else
  {
    for (std::uint8_t memoryByte = byteOffset;
         memoryByte < 8;
         ++memoryByte)
    {
      const std::uint8_t registerByte =
        memoryByte - byteOffset;
      const std::uint64_t mask =
        UINT64_C(0xff) << (memoryByte * 8);
      memory =
        (memory & ~mask) |
        (((target >> (registerByte * 8)) & 0xff) <<
         (memoryByte * 8));
    }
  }
  const bool writeSucceeded =
    attachedBus().writeData64(alignedAddress, memory);
  recordMemoryTrace(
    alignedAddress,
    8,
    true,
    writeSucceeded,
    memory);
  if (!writeSucceeded)
  {
    return raiseDataAccessException(
      EEException::DataBusErrorStore,
      address,
      dataAddress,
      instruction.raw);
  }
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeQuadwordMemory(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::LoadQuadword:
    case EEOperation::StoreQuadword:
      break;
    default:
      throw std::logic_error(
        "EE quadword-memory handler received an incompatible "
        "operation.");
  }

  const std::uint32_t dataAddress =
    static_cast<std::uint32_t>(
      generalRegisters[instruction.sourceRegister].low +
      signExtend16(instruction.immediate)) &
    ~UINT32_C(0x0f);
  if (instruction.operation == EEOperation::StoreQuadword)
  {
    const EERegister128 &value =
      generalRegisters[instruction.targetRegister];
    const EEDataWriteResult writeResult =
      attachedBus().writeGuestData128(
        dataAddress,
        {value.low, value.high});
    const bool succeeded =
      writeResult == EEDataWriteResult::Completed;
    recordMemoryTrace(
      dataAddress,
      16,
      true,
      succeeded,
      value.low,
      value.high);
    if (writeResult == EEDataWriteResult::Stalled)
    {
      pc = address;
      return EEInstructionExecutionOutcome::Delayed;
    }
    if (!succeeded)
    {
      return raiseDataAccessException(
        EEException::DataBusErrorStore,
        address,
        dataAddress,
        instruction.raw);
    }
    return EEInstructionExecutionOutcome::Completed;
  }

  EEQuadword value = {};
  const bool succeeded =
    attachedBus().readData128(dataAddress, &value);
  recordMemoryTrace(
    dataAddress,
    16,
    false,
    succeeded,
    succeeded ? value.low : 0,
    succeeded ? value.high : 0);
  if (!succeeded)
  {
    return raiseDataAccessException(
      EEException::DataBusErrorLoad,
      address,
      dataAddress,
      instruction.raw);
  }
  if (instruction.targetRegister != 0)
  {
    generalRegisters[instruction.targetRegister] = {
      value.low,
      value.high
    };
  }
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeExceptionReturn(
  const EEInstruction &instruction)
{
  if (instruction.operation != EEOperation::ExceptionReturn)
  {
    throw std::logic_error(
      "EE exception-return handler received an "
      "incompatible operation.");
  }
  cancelInFlightCOP1(
    COP1CancellationScope::After,
    executingProgramOrder);
  if ((cop0Status & EECOP0Status::ERROR_LEVEL) != 0)
  {
    pc = cop0ErrorEPC;
    cop0Status &= ~EECOP0Status::ERROR_LEVEL;
  }
  else
  {
    pc = cop0EPC;
    cop0Status &= ~EECOP0Status::EXCEPTION_LEVEL;
  }
  clearPendingException();
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeSoftwareException(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  EEException exceptionType = EEException::None;
  switch (instruction.operation)
  {
    case EEOperation::SystemCall:
      exceptionType = EEException::SystemCall;
      break;
    case EEOperation::Breakpoint:
      exceptionType = EEException::Breakpoint;
      break;
    default:
      throw std::logic_error(
        "EE software-exception handler received an "
        "incompatible operation.");
  }
  enterException(
    exceptionType,
    address,
    address,
    instruction.raw);
  return EEInstructionExecutionOutcome::Faulted;
}

EEInstructionExecutionOutcome EECore::executeCOP1RegisterMove(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::MoveWordFromCOP1:
    case EEOperation::MoveWordToCOP1:
    case EEOperation::MoveControlWordFromCOP1:
    case EEOperation::MoveControlWordToCOP1:
    case EEOperation::MoveSingleCOP1:
      break;
    default:
      throw std::logic_error(
        "EE COP1-register-move handler received an incompatible "
        "operation.");
  }
  if (!requireCOP1Usable(address, instruction.raw))
  {
    return EEInstructionExecutionOutcome::Faulted;
  }

  InFlightCOP1Operation &operation =
    allocateInFlightCOP1(instruction, address);
  switch (instruction.operation)
  {
    case EEOperation::MoveWordFromCOP1:
      operation.destination.mask = COP1_DESTINATION_GPR;
      operation.destination.gprRegister =
        instruction.targetRegister;
      break;
    case EEOperation::MoveWordToCOP1:
      operation.capturedGPR =
        generalRegisters[instruction.targetRegister].low;
      operation.destination.mask = COP1_DESTINATION_FPR;
      operation.destination.fprRegister =
        instruction.destinationRegister;
      break;
    case EEOperation::MoveControlWordFromCOP1:
      operation.destination.mask = COP1_DESTINATION_GPR;
      operation.destination.gprRegister =
        instruction.targetRegister;
      break;
    case EEOperation::MoveControlWordToCOP1:
      operation.capturedGPR =
        generalRegisters[instruction.targetRegister].low;
      if (instruction.destinationRegister ==
          EECOP1Control::STATUS_REGISTER)
      {
        operation.destination.mask =
          COP1_DESTINATION_FCR31;
      }
      break;
    case EEOperation::MoveSingleCOP1:
      operation.destination.mask = COP1_DESTINATION_FPR;
      operation.destination.fprRegister =
        instruction.shiftAmount;
      break;
    default:
      break;
  }
  recordCOP1StageTransition(
    operation,
    UINT8_MAX,
    COP1PipelineStage::R);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeCOP1Divider(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::SquareRootSingleCOP1:
    case EEOperation::ReciprocalSquareRootSingleCOP1:
    case EEOperation::DivideSingleCOP1:
      break;
    default:
      throw std::logic_error(
        "EE COP1-divider handler received an incompatible "
        "operation.");
  }
  if (!requireCOP1Usable(address, instruction.raw))
  {
    return EEInstructionExecutionOutcome::Faulted;
  }

  const std::uint32_t ftBits =
    scoreboardFPRValue(instruction.targetRegister);
  std::uint32_t fsBits = 0;
  EEFloatResult result;
  switch (instruction.operation)
  {
    case EEOperation::SquareRootSingleCOP1:
      result = sqrtEEFloatRaw(ftBits);
      break;
    case EEOperation::ReciprocalSquareRootSingleCOP1:
      fsBits =
        scoreboardFPRValue(instruction.destinationRegister);
      result = rsqrtEEFloatRaw(fsBits, ftBits);
      break;
    case EEOperation::DivideSingleCOP1:
      fsBits =
        scoreboardFPRValue(instruction.destinationRegister);
      result = divEEFloatRaw(fsBits, ftBits);
      break;
    default:
      break;
  }
  startPendingCOP1Divider(
    instruction,
    fsBits,
    ftBits,
    result.bits,
    result.flags);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeCOP1StagedOperation(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::AbsoluteSingleCOP1:
    case EEOperation::NegateSingleCOP1:
    case EEOperation::MaximumSingleCOP1:
    case EEOperation::MinimumSingleCOP1:
    case EEOperation::ConvertWordToSingleCOP1:
    case EEOperation::ConvertSingleToWordCOP1:
    case EEOperation::AddSingleCOP1:
    case EEOperation::SubtractSingleCOP1:
    case EEOperation::AddSingleToAccumulatorCOP1:
    case EEOperation::SubtractSingleToAccumulatorCOP1:
    case EEOperation::MultiplySingleCOP1:
    case EEOperation::MultiplySingleToAccumulatorCOP1:
    case EEOperation::MultiplyAddSingleCOP1:
    case EEOperation::MultiplyAddSingleToAccumulatorCOP1:
    case EEOperation::MultiplySubtractSingleCOP1:
    case EEOperation::MultiplySubtractSingleToAccumulatorCOP1:
    case EEOperation::CompareFalseSingleCOP1:
    case EEOperation::CompareEqualSingleCOP1:
    case EEOperation::CompareLessThanSingleCOP1:
    case EEOperation::CompareLessThanOrEqualSingleCOP1:
      break;
    default:
      throw std::logic_error(
        "EE COP1-staged-operation handler received an "
        "incompatible operation.");
  }
  if (!requireCOP1Usable(address, instruction.raw))
  {
    return EEInstructionExecutionOutcome::Faulted;
  }

  InFlightCOP1Operation &operation =
    allocateInFlightCOP1(instruction, address);
  const EECOP1ResultDestination resultDestination =
    eeOperationMetadata(
      instruction.operation).cop1ResultDestination;
  if (resultDestination ==
      EECOP1ResultDestination::Condition)
  {
    operation.destination.mask =
      COP1_DESTINATION_CONDITION;
  }
  else if (resultDestination ==
           EECOP1ResultDestination::Accumulator)
  {
    operation.destination.mask =
      COP1_DESTINATION_ACCUMULATOR |
      COP1_DESTINATION_FCR31;
  }
  else
  {
    operation.destination.mask = COP1_DESTINATION_FPR;
    if (instruction.operation !=
        EEOperation::ConvertWordToSingleCOP1)
    {
      operation.destination.mask |=
        COP1_DESTINATION_FCR31;
    }
  }
  operation.destination.fprRegister =
    instruction.shiftAmount;
  if (instruction.operation ==
      EEOperation::ConvertSingleToWordCOP1)
  {
    operation.affectedFlags = FP_FLAG_I_BIT;
  }
  else if (instruction.operation !=
             EEOperation::ConvertWordToSingleCOP1 &&
           !isCOP1ComparisonOperation(
             instruction.operation))
  {
    operation.affectedFlags =
      FP_FLAG_OVERFLOW | FP_FLAG_UNDERFLOW;
  }
  recordCOP1StageTransition(
    operation,
    UINT8_MAX,
    COP1PipelineStage::R);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeCOP1Branch(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::BranchCOP1False:
    case EEOperation::BranchCOP1FalseLikely:
    case EEOperation::BranchCOP1True:
    case EEOperation::BranchCOP1TrueLikely:
      break;
    default:
      throw std::logic_error(
        "EE COP1-branch handler received an incompatible "
        "operation.");
  }
  if (!requireCOP1Usable(address, instruction.raw))
  {
    return EEInstructionExecutionOutcome::Faulted;
  }

  const bool branchOnTrue =
    instruction.operation == EEOperation::BranchCOP1True ||
    instruction.operation == EEOperation::BranchCOP1TrueLikely;
  const bool likely =
    instruction.operation == EEOperation::BranchCOP1FalseLikely ||
    instruction.operation == EEOperation::BranchCOP1TrueLikely;
  const std::uint32_t branchTarget =
    address + 4 +
    static_cast<std::uint32_t>(
      signExtend16(instruction.immediate) << 2);
  scheduleBranch(
    scoreboardCOP1Condition() == branchOnTrue,
    likely,
    branchTarget,
    address);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeCOP1Memory(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::LoadWordToCOP1:
    case EEOperation::StoreWordFromCOP1:
      break;
    default:
      throw std::logic_error(
        "EE COP1-memory handler received an "
        "incompatible operation.");
  }
  if (!requireCOP1Usable(address, instruction.raw))
  {
    return EEInstructionExecutionOutcome::Faulted;
  }

  InFlightCOP1Operation &operation =
    allocateInFlightCOP1(instruction, address);
  operation.capturedGPR =
    generalRegisters[instruction.sourceRegister].low;
  if (instruction.operation == EEOperation::LoadWordToCOP1)
  {
    operation.destination.mask = COP1_DESTINATION_FPR;
    operation.destination.fprRegister =
      instruction.targetRegister;
  }
  else
  {
    operation.destination.mask = COP1_DESTINATION_MEMORY;
  }
  recordCOP1StageTransition(
    operation,
    UINT8_MAX,
    COP1PipelineStage::R);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeCOP2Memory(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::LoadQuadwordToCOP2:
    case EEOperation::StoreQuadwordFromCOP2:
      break;
    default:
      throw std::logic_error(
        "EE COP2-memory handler received an incompatible "
        "operation.");
  }

  const std::uint32_t dataAddress =
    static_cast<std::uint32_t>(
      generalRegisters[instruction.sourceRegister].low +
      signExtend16(instruction.immediate));
  const bool store =
    instruction.operation == EEOperation::StoreQuadwordFromCOP2;
  if ((dataAddress & 0x0f) != 0)
  {
    return raiseDataAccessException(
      store
        ? EEException::AddressErrorStore
        : EEException::AddressErrorLoadOrFetch,
      address,
      dataAddress,
      instruction.raw);
  }
  if (attachedVU0().macroRegisterNumberWritePending(
        instruction.targetRegister))
  {
    pc = address;
    return EEInstructionExecutionOutcome::Delayed;
  }

  if (store)
  {
    const EERegister128 value = quadwordFromFPRegister(
      *attachedVU0().fpRegisterValue(
        instruction.targetRegister));
    const EEDataWriteResult writeResult =
      attachedBus().writeGuestData128(
        dataAddress,
        {value.low, value.high});
    const bool succeeded =
      writeResult == EEDataWriteResult::Completed;
    recordMemoryTrace(
      dataAddress,
      16,
      true,
      succeeded,
      value.low,
      value.high);
    if (writeResult == EEDataWriteResult::Stalled)
    {
      pc = address;
      return EEInstructionExecutionOutcome::Delayed;
    }
    if (!succeeded)
    {
      return raiseDataAccessException(
        EEException::DataBusErrorStore,
        address,
        dataAddress,
        instruction.raw);
    }
    return EEInstructionExecutionOutcome::Completed;
  }

  EEQuadword value = {};
  const bool succeeded =
    attachedBus().readData128(dataAddress, &value);
  recordMemoryTrace(
    dataAddress,
    16,
    false,
    succeeded,
    succeeded ? value.low : 0,
    succeeded ? value.high : 0);
  if (!succeeded)
  {
    return raiseDataAccessException(
      EEException::DataBusErrorLoad,
      address,
      dataAddress,
      instruction.raw);
  }
  attachedVU0().loadFPRegisterBits(
    instruction.targetRegister,
    static_cast<std::uint32_t>(value.low),
    static_cast<std::uint32_t>(value.low >> 32),
    static_cast<std::uint32_t>(value.high),
    static_cast<std::uint32_t>(value.high >> 32));
  attachedVU0().noteMacroTransferToVU();
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeCOP2VectorMove(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  const std::uint8_t vectorRegister =
    instruction.destinationRegister;
  switch (instruction.operation)
  {
    case EEOperation::QuadwordMoveFromCOP2:
      if (((instruction.raw & 1) != 0 &&
           attachedVU0().microModeActive()) ||
          attachedVU0().macroRegisterNumberWritePending(
            vectorRegister))
      {
        pc = address;
        return EEInstructionExecutionOutcome::Delayed;
      }
      if (instruction.targetRegister != 0)
      {
        generalRegisters[instruction.targetRegister] =
          quadwordFromFPRegister(
            *attachedVU0().fpRegisterValue(vectorRegister));
      }
      return EEInstructionExecutionOutcome::Completed;
    case EEOperation::QuadwordMoveToCOP2:
    {
      if (((instruction.raw & 1) != 0 &&
           !attachedVU0().cop2WriteAvailable()) ||
          attachedVU0().macroRegisterNumberWritePending(
            vectorRegister))
      {
        pc = address;
        return EEInstructionExecutionOutcome::Delayed;
      }
      const EERegister128 &value =
        generalRegisters[instruction.targetRegister];
      attachedVU0().loadFPRegisterBits(
        vectorRegister,
        static_cast<std::uint32_t>(value.low),
        static_cast<std::uint32_t>(value.low >> 32),
        static_cast<std::uint32_t>(value.high),
        static_cast<std::uint32_t>(value.high >> 32));
      attachedVU0().noteMacroTransferToVU();
      return EEInstructionExecutionOutcome::Completed;
    }
    default:
      throw std::logic_error(
        "EE COP2-vector-move handler received an "
        "incompatible operation.");
  }
}

EEInstructionExecutionOutcome EECore::executeCOP2ControlMove(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::ControlMoveFromCOP2:
    case EEOperation::ControlMoveToCOP2:
      break;
    default:
      throw std::logic_error(
        "EE COP2-control-move handler received an incompatible "
        "operation.");
  }

  const std::uint8_t controlRegister =
    instruction.destinationRegister;
  if (instruction.operation == EEOperation::ControlMoveFromCOP2)
  {
    if (((instruction.raw & 1) != 0 &&
         attachedVU0().microModeActive()) ||
        (controlRegister < 16 &&
         attachedVU0().macroRegisterNumberWritePending(
           controlRegister)) ||
        (attachedVU0().macroModeActive() &&
         (controlRegister == 16 || controlRegister == 17)))
    {
      pc = address;
      return EEInstructionExecutionOutcome::Delayed;
    }
    std::uint32_t value = 0;
    if (!readCOP2ControlRegister(controlRegister, &value))
    {
      haltUndefinedOperation(address, instruction.raw);
      return EEInstructionExecutionOutcome::Halted;
    }
    if (controlRegister < 16)
    {
      writeLowDoubleword(instruction.targetRegister, value);
    }
    else
    {
      writeWord(instruction.targetRegister, value);
    }
    return EEInstructionExecutionOutcome::Completed;
  }

  if (((instruction.raw & 1) != 0 &&
       !attachedVU0().cop2WriteAvailable()) ||
      (controlRegister < 16 &&
       attachedVU0().macroRegisterNumberWritePending(
         controlRegister)) ||
      (attachedVU0().macroModeActive() &&
       (controlRegister == 16 || controlRegister == 18)))
  {
    pc = address;
    return EEInstructionExecutionOutcome::Delayed;
  }
  if (!writeCOP2ControlRegister(
        controlRegister,
        static_cast<std::uint32_t>(
          generalRegisters[instruction.targetRegister].low)))
  {
    haltUndefinedOperation(address, instruction.raw);
    return EEInstructionExecutionOutcome::Halted;
  }
  attachedVU0().noteMacroTransferToVU();
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeCOP2Branch(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::BranchCOP2False:
    case EEOperation::BranchCOP2FalseLikely:
    case EEOperation::BranchCOP2True:
    case EEOperation::BranchCOP2TrueLikely:
      break;
    default:
      throw std::logic_error(
        "EE COP2-branch handler received an "
        "incompatible operation.");
  }
  const bool branchOnTrue =
    instruction.operation == EEOperation::BranchCOP2True ||
    instruction.operation == EEOperation::BranchCOP2TrueLikely;
  const bool likely =
    instruction.operation == EEOperation::BranchCOP2FalseLikely ||
    instruction.operation == EEOperation::BranchCOP2TrueLikely;
  const std::uint32_t branchTarget =
    address + 4 +
    static_cast<std::uint32_t>(
      signExtend16(instruction.immediate) << 2);
  scheduleBranch(
    attachedVU1().clockActive() == branchOnTrue,
    likely,
    branchTarget,
    address);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeCOP2MicroCall(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::VectorCallMicroSubroutine:
    case EEOperation::VectorCallMicroSubroutineRegister:
      break;
    default:
      throw std::logic_error(
        "EE COP2-micro-call handler received an incompatible "
        "operation.");
  }

  VPU &vu0 = attachedVU0();
  const std::size_t callAddress =
    static_cast<std::size_t>(
      instruction.operation ==
        EEOperation::VectorCallMicroSubroutine
        ? instruction.cop2Immediate
        : vu0.callAddressRegister()) *
    8;
  if (callAddress > vu0.microMemorySize() - 8)
  {
    haltUndefinedOperation(address, instruction.raw);
    return EEInstructionExecutionOutcome::Halted;
  }
  const std::uint16_t startAddress =
    static_cast<std::uint16_t>(callAddress);
  if (vu0.macroModeActive())
  {
    if (!vu0.startMicroModeFromMacro(startAddress))
    {
      pc = address;
      return EEInstructionExecutionOutcome::Delayed;
    }
  }
  else
  {
    if (vu0.microModeActive())
    {
      pc = address;
      return EEInstructionExecutionOutcome::Delayed;
    }
    vu0.startMicroMode(startAddress);
  }
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeCOP2Macro(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  if (instruction.operation != EEOperation::VectorMacroArithmetic)
  {
    throw std::logic_error(
      "EE COP2-macro handler received an incompatible "
      "operation.");
  }

  VPU &vu0 = attachedVU0();
  if (vu0.getState() == VPU_STATE_STOP)
  {
    haltUndefinedOperation(address, instruction.raw);
    return EEInstructionExecutionOutcome::Halted;
  }
  if (!vu0.issueMacroInstruction(
        instruction.raw & UINT32_C(0x01ffffff)))
  {
    pc = address;
    return EEInstructionExecutionOutcome::Delayed;
  }
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeJump(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  const std::uint64_t source =
    generalRegisters[instruction.sourceRegister].low;
  switch (instruction.operation)
  {
    case EEOperation::Jump:
      scheduleBranch(
        true,
        false,
        ((address + 4) & UINT32_C(0xf0000000)) |
          (instruction.target << 2),
        address);
      return EEInstructionExecutionOutcome::Completed;
    case EEOperation::JumpAndLink:
      writeLowDoubleword(31, address + 8);
      scheduleBranch(
        true,
        false,
        ((address + 4) & UINT32_C(0xf0000000)) |
          (instruction.target << 2),
        address);
      return EEInstructionExecutionOutcome::Completed;
    case EEOperation::JumpRegister:
      scheduleBranch(
        true,
        false,
        static_cast<std::uint32_t>(source),
        address);
      return EEInstructionExecutionOutcome::Completed;
    case EEOperation::JumpAndLinkRegister:
      if (instruction.sourceRegister ==
          instruction.destinationRegister)
      {
        haltUndefinedOperation(address, instruction.raw);
        return EEInstructionExecutionOutcome::Halted;
      }
      writeLowDoubleword(
        instruction.destinationRegister,
        address + 8);
      scheduleBranch(
        true,
        false,
        static_cast<std::uint32_t>(source),
        address);
      return EEInstructionExecutionOutcome::Completed;
    default:
      throw std::logic_error(
        "EE jump handler received an incompatible operation.");
  }
}

EEInstructionExecutionOutcome EECore::executeIntegerBranch(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  const std::uint64_t source =
    generalRegisters[instruction.sourceRegister].low;
  const std::uint64_t target =
    generalRegisters[instruction.targetRegister].low;
  const bool negative =
    (source & DOUBLEWORD_SIGN_BIT) != 0;
  bool condition = false;
  switch (instruction.operation)
  {
    case EEOperation::BranchEqual:
    case EEOperation::BranchEqualLikely:
      condition = source == target;
      break;
    case EEOperation::BranchNotEqual:
    case EEOperation::BranchNotEqualLikely:
      condition = source != target;
      break;
    case EEOperation::BranchLessThanOrEqualZero:
    case EEOperation::BranchLessThanOrEqualZeroLikely:
      condition = negative || source == 0;
      break;
    case EEOperation::BranchGreaterThanZero:
    case EEOperation::BranchGreaterThanZeroLikely:
      condition = !negative && source != 0;
      break;
    case EEOperation::BranchLessThanZero:
    case EEOperation::BranchLessThanZeroLikely:
    case EEOperation::BranchLessThanZeroAndLink:
    case EEOperation::BranchLessThanZeroAndLinkLikely:
      condition = negative;
      break;
    case EEOperation::BranchGreaterThanOrEqualZero:
    case EEOperation::BranchGreaterThanOrEqualZeroLikely:
    case EEOperation::BranchGreaterThanOrEqualZeroAndLink:
    case EEOperation::BranchGreaterThanOrEqualZeroAndLinkLikely:
      condition = !negative;
      break;
    default:
      throw std::logic_error(
        "EE integer-branch handler received an incompatible "
        "operation.");
  }

  const bool likely =
    isEEBranchLikelyOperation(instruction.operation);
  const bool link =
    instruction.operation ==
      EEOperation::BranchLessThanZeroAndLink ||
    instruction.operation ==
      EEOperation::BranchGreaterThanOrEqualZeroAndLink ||
    instruction.operation ==
      EEOperation::BranchLessThanZeroAndLinkLikely ||
    instruction.operation ==
      EEOperation::BranchGreaterThanOrEqualZeroAndLinkLikely;
  if (link && instruction.sourceRegister == 31)
  {
    haltUndefinedOperation(address, instruction.raw);
    return EEInstructionExecutionOutcome::Halted;
  }
  if (link)
  {
    writeLowDoubleword(31, address + 8);
  }
  const std::uint32_t branchTarget =
    address + 4 +
    static_cast<std::uint32_t>(
      signExtend16(instruction.immediate) << 2);
  scheduleBranch(
    condition,
    likely,
    branchTarget,
    address);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeMultiply(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::MultiplyWord:
    case EEOperation::MultiplyUnsignedWord:
    case EEOperation::MultiplyWord1:
    case EEOperation::MultiplyUnsignedWord1:
    case EEOperation::MultiplyAddWord:
    case EEOperation::MultiplyAddUnsignedWord:
    case EEOperation::MultiplyAddWord1:
    case EEOperation::MultiplyAddUnsignedWord1:
      break;
    default:
      throw std::logic_error(
        "EE multiply handler received an incompatible operation.");
  }
  if (!requireWordValue(
        instruction.sourceRegister,
        address,
        instruction.raw) ||
      !requireWordValue(
        instruction.targetRegister,
        address,
        instruction.raw))
  {
    return EEInstructionExecutionOutcome::Halted;
  }

  const MACPipeline pipeline =
    eeOperationMetadata(instruction.operation).
      executionDispatch ==
        EEExecutionDispatch::MAC1Continuation
      ? MACPipeline::MAC1
      : MACPipeline::MAC0;
  const bool signedOperands =
    instruction.operation == EEOperation::MultiplyWord ||
    instruction.operation == EEOperation::MultiplyWord1 ||
    instruction.operation == EEOperation::MultiplyAddWord ||
    instruction.operation == EEOperation::MultiplyAddWord1;
  const bool accumulate =
    instruction.operation == EEOperation::MultiplyAddWord ||
    instruction.operation == EEOperation::MultiplyAddUnsignedWord ||
    instruction.operation == EEOperation::MultiplyAddWord1 ||
    instruction.operation == EEOperation::MultiplyAddUnsignedWord1;
  const std::uint32_t left =
    static_cast<std::uint32_t>(
      generalRegisters[instruction.sourceRegister].low);
  const std::uint32_t right =
    static_cast<std::uint32_t>(
      generalRegisters[instruction.targetRegister].low);
  std::uint64_t result = signedOperands
    ? multiplySignedWords(left, right)
    : multiplyUnsignedWords(left, right);
  if (accumulate)
  {
    result += pipeline == MACPipeline::MAC1
      ? accumulatorValue(hi1Register, lo1Register)
      : accumulatorValue(hiRegister, loRegister);
  }
  startPendingMultiplyDivide(
    pipeline,
    MULTIPLY_LATENCY,
    signExtendWord(static_cast<std::uint32_t>(result >> 32)),
    signExtendWord(static_cast<std::uint32_t>(result)),
    instruction.destinationRegister,
    MACResultDestination::HIAndLOAndGPR);
  return EEInstructionExecutionOutcome::Completed;
}

EEInstructionExecutionOutcome EECore::executeDivide(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  switch (instruction.operation)
  {
    case EEOperation::DivideWord:
    case EEOperation::DivideUnsignedWord:
    case EEOperation::DivideWord1:
    case EEOperation::DivideUnsignedWord1:
      break;
    default:
      throw std::logic_error(
        "EE divide handler received an incompatible operation.");
  }
  if (!requireWordValue(
        instruction.sourceRegister,
        address,
        instruction.raw) ||
      !requireWordValue(
        instruction.targetRegister,
        address,
        instruction.raw))
  {
    return EEInstructionExecutionOutcome::Halted;
  }

  const std::uint32_t dividend =
    static_cast<std::uint32_t>(
      generalRegisters[instruction.sourceRegister].low);
  const std::uint32_t divisor =
    static_cast<std::uint32_t>(
      generalRegisters[instruction.targetRegister].low);
  if (divisor == 0)
  {
    haltUndefinedOperation(address, instruction.raw);
    return EEInstructionExecutionOutcome::Halted;
  }
  const MACPipeline pipeline =
    eeOperationMetadata(instruction.operation).
      executionDispatch ==
        EEExecutionDispatch::MAC1Continuation
      ? MACPipeline::MAC1
      : MACPipeline::MAC0;
  const bool signedOperands =
    instruction.operation == EEOperation::DivideWord ||
    instruction.operation == EEOperation::DivideWord1;
  std::uint32_t quotient = 0;
  std::uint32_t remainder = 0;
  if (signedOperands &&
      dividend == UINT32_C(0x80000000) &&
      divisor == UINT32_MAX)
  {
    quotient = dividend;
  }
  else if (signedOperands)
  {
    const std::int64_t signedDividend = signedWord(dividend);
    const std::int64_t signedDivisor = signedWord(divisor);
    quotient = static_cast<std::uint32_t>(
      signedDividend / signedDivisor);
    remainder = static_cast<std::uint32_t>(
      signedDividend % signedDivisor);
  }
  else
  {
    quotient = dividend / divisor;
    remainder = dividend % divisor;
  }
  startPendingMultiplyDivide(
    pipeline,
    DIVIDE_LATENCY,
    signExtendWord(remainder),
    signExtendWord(quotient),
    0,
    MACResultDestination::HIAndLO);
  return EEInstructionExecutionOutcome::Completed;
}

bool EECore::requireWordValue(
  std::uint8_t registerIndex,
  std::uint32_t address,
  std::uint32_t instruction)
{
  if (isWordValue(generalRegisters[registerIndex].low))
  {
    return true;
  }
  haltUndefinedOperation(address, instruction);
  return false;
}

void EECore::writeLowDoubleword(
  std::uint8_t registerIndex,
  std::uint64_t value)
{
  if (registerIndex != 0)
  {
    generalRegisters[registerIndex].low = value;
  }
}

void EECore::writeWord(
  std::uint8_t registerIndex,
  std::uint32_t value)
{
  writeLowDoubleword(registerIndex, signExtendWord(value));
}

EEInstructionExecutionOutcome
EECore::raiseArithmeticOverflow(
  std::uint32_t address,
  std::uint32_t instruction)
{
  enterException(
    EEException::ArithmeticOverflow,
    address,
    address,
    instruction);
  return EEInstructionExecutionOutcome::Faulted;
}

bool EECore::requireCOP1Usable(
  std::uint32_t address,
  std::uint32_t instruction)
{
  if ((cop0Status & EECOP0Status::COP1_USABLE) != 0)
  {
    return true;
  }
  cop0Cause =
    (cop0Cause & ~EECOP0Cause::COPROCESSOR_ERROR_MASK) |
    EECOP0Cause::COPROCESSOR_1;
  enterException(
    EEException::CoprocessorUnusable,
    address,
    address,
    instruction);
  return false;
}

void EECore::haltUndefinedOperation(
  std::uint32_t address,
  std::uint32_t instruction)
{
  pc = address;
  state = EEExecutionState::Halted;
  haltReason = EEStopReason::UndefinedOperation;
  rejectedInstructionValue = instruction;
}

bool EECore::pendingMultiplyDivideActive() const
{
  return pendingMac0.active || pendingMac1.active;
}

void EECore::advancePendingMultiplyDivide(
  MACPipeline pipeline)
{
  PendingMultiplyDivide *operation =
    pipeline == MACPipeline::MAC1
      ? &pendingMac1
      : &pendingMac0;
  if (!operation->active)
  {
    return;
  }
  --operation->remainingCycles;
  if (operation->remainingCycles != 0)
  {
    return;
  }

  if (pipeline == MACPipeline::MAC1)
  {
    hi1Register = operation->hiResult;
    lo1Register = operation->loResult;
  }
  else
  {
    hiRegister = operation->hiResult;
    loRegister = operation->loResult;
  }
  if (operation->resultDestination ==
      MACResultDestination::HIAndLOAndGPR)
  {
    writeLowDoubleword(
      operation->generalRegister,
      operation->generalRegisterResult);
  }
  *operation = {};
}

void EECore::startPendingMultiplyDivide(
  MACPipeline pipeline,
  std::uint8_t latency,
  std::uint64_t hiResult,
  std::uint64_t loResult,
  std::uint8_t generalRegister,
  MACResultDestination resultDestination)
{
  PendingMultiplyDivide &operation =
    pipeline == MACPipeline::MAC1
      ? pendingMac1
      : pendingMac0;
  operation.active = true;
  operation.remainingCycles = latency;
  operation.hiResult = hiResult;
  operation.loResult = loResult;
  operation.resultDestination = resultDestination;
  operation.generalRegister = generalRegister;
  operation.generalRegisterResult = loResult;
}

EECore::InFlightCOP1Operation &
EECore::allocateInFlightCOP1(
  const EEInstruction &instruction,
  std::uint32_t instructionAddress)
{
  if (executingProgramOrder == 0)
  {
    throw std::logic_error(
      "EE COP1 allocation requires assigned program order.");
  }
  if (!isCOP1ManagedPipelineOperation(instruction.operation))
  {
    throw std::logic_error(
      "EE COP1 allocation received an unmanaged operation.");
  }
  for (InFlightCOP1Operation &operation :
       inFlightCOP1Operations)
  {
    if (!operation.active)
    {
      operation = {};
      operation.active = true;
      operation.programOrder = executingProgramOrder;
      operation.stage = COP1PipelineStage::R;
      operation.instructionAddress = instructionAddress;
      operation.instruction = instruction;
      return operation;
    }
  }
  throw std::logic_error(
    "EE COP1 has no free in-flight operation slot.");
}

EECore::COP1ProgramOrderView
EECore::inFlightCOP1ProgramOrder() const
{
  COP1ProgramOrderView order;
  for (std::size_t slotIndex = 0;
       slotIndex < inFlightCOP1Operations.size();
       ++slotIndex)
  {
    if (!inFlightCOP1Operations[slotIndex].active)
    {
      continue;
    }
    std::size_t insertionIndex = order.count;
    while (insertionIndex != 0 &&
           inFlightCOP1Operations[
             order.slotIndices[insertionIndex - 1]].programOrder >
             inFlightCOP1Operations[slotIndex].programOrder)
    {
      order.slotIndices[insertionIndex] =
        order.slotIndices[insertionIndex - 1];
      --insertionIndex;
    }
    order.slotIndices[insertionIndex] = slotIndex;
    ++order.count;
  }
  return order;
}

bool EECore::cop1RetirementReady(
  const InFlightCOP1Operation &operation)
{
  return
    operation.stage == COP1PipelineStage::S1 ||
    ((isCOP1RegisterMoveOperation(
       operation.instruction.operation) ||
      isCOP1MemoryMoveOperation(
        operation.instruction.operation)) &&
     operation.stage == COP1PipelineStage::Y);
}

void EECore::completeInFlightCOP1(
  InFlightCOP1Operation *operation,
  COP1CompletionReason reason)
{
  if (!operation->active)
  {
    throw std::logic_error(
      "EE COP1 completion requires active work.");
  }
  if (cop1RetirementReady(*operation))
  {
    throw std::logic_error(
      "EE COP1 completion requires pending work.");
  }
  const bool operated =
    isCOP1StagedOperation(
      operation->instruction.operation) ||
    isCOP1DividerOperation(
      operation->instruction.operation);
  const bool moved =
    isCOP1RegisterMoveOperation(
      operation->instruction.operation) ||
    isCOP1MemoryMoveOperation(
      operation->instruction.operation);
  if (!operated && !moved)
  {
    throw std::logic_error(
      "EE COP1 completion received an unmanaged operation.");
  }
  if (reason == COP1CompletionReason::PipelineAdvance)
  {
    const bool validFinalStage =
      (isCOP1StagedOperation(
         operation->instruction.operation) &&
       operation->stage == COP1PipelineStage::Z) ||
      ((isCOP1RegisterMoveOperation(
          operation->instruction.operation) ||
        isCOP1MemoryMoveOperation(
          operation->instruction.operation)) &&
       operation->stage == COP1PipelineStage::X) ||
      (isCOP1DividerOperation(
         operation->instruction.operation) &&
       operation->remainingCycles == 0);
    if (!validFinalStage)
    {
      throw std::logic_error(
        "EE COP1 pipeline completion requires final-stage work.");
    }
  }
  if (operated)
  {
    operation->stage = COP1PipelineStage::S1;
    return;
  }
  operation->stage = COP1PipelineStage::Y;
}

void EECore::releaseInFlightCOP1(
  InFlightCOP1Operation *operation)
{
  if (!operation->active)
  {
    throw std::logic_error(
      "EE COP1 release requires active work.");
  }
  *operation = {};
}

bool EECore::drainInFlightCOP1()
{
  for (const InFlightCOP1Operation &operation :
       inFlightCOP1Operations)
  {
    if (!operation.active)
    {
      continue;
    }
    if (!isCOP1MoveOperation(
          operation.instruction.operation) &&
        !isCOP1RegisterMoveOperation(
          operation.instruction.operation) &&
        !isCOP1DividerOperation(
          operation.instruction.operation) &&
        !isCOP1StagedOperation(
          operation.instruction.operation))
    {
      throw std::logic_error(
        "ELF return cannot drain an unsupported COP1 operation.");
    }
  }
  const auto finishFailure =
    [this](bool result)
    {
      reconcileCOP1DividerOccupancy();
      return result;
    };

  const COP1ProgramOrderView programOrder =
    inFlightCOP1ProgramOrder();
  for (std::size_t orderIndex = 0;
       orderIndex < programOrder.size();
       ++orderIndex)
  {
    InFlightCOP1Operation *oldest =
      &inFlightCOP1Operations[programOrder[orderIndex]];
    if (!oldest->active)
    {
      continue;
    }
    if (isCOP1StagedOperation(
          oldest->instruction.operation))
    {
      if (oldest->stage == COP1PipelineStage::R)
      {
        oldest->capturedFS =
          floatingPointRegisters[
            oldest->instruction.destinationRegister];
        if (!isCOP1SingleSourceStagedOperation(
              oldest->instruction.operation))
        {
          oldest->capturedFT =
            floatingPointRegisters[
              oldest->instruction.targetRegister];
        }
        if (isCOP1CompoundOperation(
              oldest->instruction.operation))
        {
          oldest->capturedAccumulator =
            floatingPointAccumulatorRegister;
        }
        oldest->capturedControl =
          cop1ControlRegister(
            EECOP1Control::STATUS_REGISTER);
      }
      if (oldest->stage < COP1PipelineStage::Z)
      {
        computeInFlightCOP1StagedOperation(oldest);
      }
    }
    else if (isCOP1MemoryMoveOperation(
               oldest->instruction.operation))
    {
      if (oldest->stage == COP1PipelineStage::R)
      {
        oldest->memoryAddress =
          static_cast<std::uint32_t>(
            oldest->capturedGPR +
            signExtend16(oldest->instruction.immediate));
      }
      if ((oldest->memoryAddress & 3) != 0)
      {
        return finishFailure(
          raiseCOP1DataAccessException(
            *oldest,
            isLoadOperation(oldest->instruction.operation)
              ? EEException::AddressErrorLoadOrFetch
              : EEException::AddressErrorStore,
            oldest->memoryAddress));
      }
      if (isLoadOperation(oldest->instruction.operation))
      {
        if (oldest->stage < COP1PipelineStage::X)
        {
          std::uint32_t value = 0;
          const bool succeeded =
            attachedBus().readData32(
              oldest->memoryAddress,
              &value);
          recordMemoryTrace(
            oldest->memoryAddress,
            4,
            false,
            succeeded,
            succeeded ? value : 0);
          if (!succeeded)
          {
            return finishFailure(
              raiseCOP1DataAccessException(
                *oldest,
                EEException::DataBusErrorLoad,
                oldest->memoryAddress));
          }
          oldest->capturedMemoryValue = value;
          oldest->rawResult = value;
        }
      }
      else
      {
        if (oldest->stage < COP1PipelineStage::X)
        {
          oldest->capturedMemoryValue =
            scoreboardFPRValueForT(
              oldest->instruction.targetRegister,
              oldest->programOrder);
          oldest->rawResult =
            oldest->capturedMemoryValue;
        }
        if (oldest->stage < COP1PipelineStage::Y)
        {
          const bool succeeded =
            attachedBus().writeData32(
              oldest->memoryAddress,
              oldest->capturedMemoryValue);
          recordMemoryTrace(
            oldest->memoryAddress,
            4,
            true,
            succeeded,
            oldest->capturedMemoryValue);
          if (!succeeded)
          {
            return finishFailure(
              raiseCOP1DataAccessException(
                *oldest,
                EEException::DataBusErrorStore,
                oldest->memoryAddress));
          }
        }
      }
    }
    else if (isCOP1RegisterMoveOperation(
               oldest->instruction.operation))
    {
      const bool captureCOP1Source =
        oldest->stage == COP1PipelineStage::R;
      switch (oldest->instruction.operation)
      {
        case EEOperation::MoveWordFromCOP1:
        case EEOperation::MoveSingleCOP1:
          if (captureCOP1Source)
          {
            oldest->capturedFS =
              floatingPointRegisters[
                oldest->instruction.destinationRegister];
          }
          oldest->rawResult = oldest->capturedFS;
          break;
        case EEOperation::MoveWordToCOP1:
        case EEOperation::MoveControlWordToCOP1:
          oldest->rawResult =
            static_cast<std::uint32_t>(
              oldest->capturedGPR);
          break;
        case EEOperation::MoveControlWordFromCOP1:
          if (captureCOP1Source)
          {
            oldest->capturedControl =
              cop1ControlRegister(
                oldest->instruction.destinationRegister);
          }
          oldest->rawResult = oldest->capturedControl;
          break;
        default:
          break;
      }
    }
    if (!cop1RetirementReady(*oldest))
    {
      completeInFlightCOP1(
        oldest,
        COP1CompletionReason::Drain);
    }
    commitInFlightCOP1(oldest);
  }
  reconcileCOP1DividerOccupancy();
  return true;
}

void EECore::advancePendingCOP1(
  std::uint32_t *completedLoadRegisters)
{
  *completedLoadRegisters = 0;
  const std::size_t deferredTraceStart =
    cycleTraceEventCount;
  const COP1AdvanceEligibility eligibility =
    evaluateCOP1AdvanceEligibility();
  const COP1ProgramOrderView programOrder =
    inFlightCOP1ProgramOrder();
  const COP1AdvanceResult advanceResult =
    advanceInFlightCOP1Operations(
      programOrder,
      eligibility);

  std::array<CycleTraceEvent, CYCLE_TRACE_CAPACITY>
    deferredTraceEvents = {};
  const std::size_t deferredTraceCount =
    cycleTraceEventCount - deferredTraceStart;
  for (std::size_t index = 0;
       index < deferredTraceCount;
       ++index)
  {
    deferredTraceEvents[index] =
      cycleTraceEvents[deferredTraceStart + index];
  }
  cycleTraceEventCount = deferredTraceStart;

  recordCOP1StageTransitions(
    programOrder,
    advanceResult);
  if (!advanceResult.exceptionEntered)
  {
    recordCOP1MoveInterlocks(
      programOrder,
      eligibility);
  }
  const COP1RetirementResult retirement =
    retireReadyInFlightCOP1();
  *completedLoadRegisters =
    retirement.completedLoadRegisters;
  for (std::size_t index = 0;
       index < deferredTraceCount;
       ++index)
  {
    if (cycleTraceEventCount >= cycleTraceEvents.size())
    {
      throw std::logic_error(
        "EE produced too many trace events in one cycle.");
    }
    cycleTraceEvents[cycleTraceEventCount++] =
      deferredTraceEvents[index];
  }
}

EECore::COP1AdvanceEligibility
EECore::evaluateCOP1AdvanceEligibility() const
{
  COP1AdvanceEligibility eligibility;
  for (std::size_t moveIndex = 0;
       moveIndex < inFlightCOP1Operations.size();
       ++moveIndex)
  {
    const InFlightCOP1Operation &move =
      inFlightCOP1Operations[moveIndex];
    if (!move.active ||
        move.stage != COP1PipelineStage::R ||
        !isCOP1MoveOperation(move.instruction.operation))
    {
      continue;
    }
    eligibility.moveTStageBlockers[moveIndex] =
      cop1MoveTStageBlocker(move);
  }
  return eligibility;
}

EECore::COP1AdvanceResult
EECore::advanceInFlightCOP1Operations(
  const COP1ProgramOrderView &programOrder,
  const COP1AdvanceEligibility &eligibility)
{
  COP1AdvanceResult result;
  for (std::size_t orderIndex = 0;
       orderIndex < programOrder.size();
       ++orderIndex)
  {
    const std::size_t slotIndex = programOrder[orderIndex];
    InFlightCOP1Operation &operation =
      inFlightCOP1Operations[slotIndex];
    if (!operation.active ||
        !isCOP1ManagedPipelineOperation(
          operation.instruction.operation))
    {
      continue;
    }
    if (eligibility.moveTStageBlockers[slotIndex] != nullptr)
    {
      continue;
    }
    if (cop1RetirementReady(operation))
    {
      continue;
    }
    const COP1OperationAdvanceResult operationResult =
      advanceInFlightCOP1Operation(&operation);
    result.previousStages[slotIndex] =
      operationResult.previousStage;
    result.transitioned[slotIndex] =
      operationResult.outcome ==
        COP1OperationAdvanceOutcome::Advanced ||
      operationResult.outcome ==
        COP1OperationAdvanceOutcome::Completed;
    if (operationResult.outcome ==
        COP1OperationAdvanceOutcome::Faulted)
    {
      result.exceptionEntered = true;
      break;
    }
  }
  reconcileCOP1DividerOccupancy();
  return result;
}

void EECore::recordCOP1StageTransitions(
  const COP1ProgramOrderView &programOrder,
  const COP1AdvanceResult &result)
{
  for (std::size_t orderIndex = 0;
       orderIndex < programOrder.size();
       ++orderIndex)
  {
    const std::size_t slotIndex = programOrder[orderIndex];
    const InFlightCOP1Operation &transition =
      inFlightCOP1Operations[slotIndex];
    if (!transition.active ||
        !result.transitioned[slotIndex])
    {
      continue;
    }
    recordCOP1StageTransition(
      transition,
      static_cast<std::uint8_t>(
        result.previousStages[slotIndex]),
      transition.stage);
  }
}

void EECore::recordCOP1MoveInterlocks(
  const COP1ProgramOrderView &programOrder,
  const COP1AdvanceEligibility &eligibility)
{
  for (std::size_t orderIndex = 0;
       orderIndex < programOrder.size();
       ++orderIndex)
  {
    const std::size_t slotIndex = programOrder[orderIndex];
    const InFlightCOP1Operation &blockedMove =
      inFlightCOP1Operations[slotIndex];
    if (!blockedMove.active ||
        eligibility.moveTStageBlockers[slotIndex] == nullptr)
    {
      continue;
    }
    recordCycleTrace(
      CycleTraceKind::COP1ResourceInterlock,
      blockedMove.instructionAddress,
      blockedMove.instruction.raw,
      static_cast<std::uint8_t>(
        eligibility.moveTStageBlockers[slotIndex]->
          instruction.operation));
  }
}

EECore::COP1RetirementResult
EECore::retireReadyInFlightCOP1()
{
  COP1RetirementResult result;
  const COP1ProgramOrderView retirementOrder =
    inFlightCOP1ProgramOrder();
  for (std::size_t orderIndex = 0;
       orderIndex < retirementOrder.size();
       ++orderIndex)
  {
    InFlightCOP1Operation *oldestOperation =
      &inFlightCOP1Operations[
        retirementOrder[orderIndex]];
    if (!oldestOperation->active ||
        !isCOP1ManagedPipelineOperation(
          oldestOperation->instruction.operation))
    {
      continue;
    }
    if (!cop1RetirementReady(*oldestOperation))
    {
      break;
    }
    if (isLoadOperation(
          oldestOperation->instruction.operation))
    {
      result.completedLoadRegisters |=
        UINT32_C(1) <<
          oldestOperation->destination.fprRegister;
    }
    retireInFlightCOP1(oldestOperation);
  }
  return result;
}

const EECore::InFlightCOP1Operation *
EECore::cop1MoveTStageBlocker(
  const InFlightCOP1Operation &move) const
{
  assert(move.active);
  assert(move.stage == COP1PipelineStage::R);
  assert(isCOP1MoveOperation(move.instruction.operation));

  const COP1ProgramOrderView programOrder =
    inFlightCOP1ProgramOrder();
  for (std::size_t orderIndex = 0;
       orderIndex < programOrder.size();
       ++orderIndex)
  {
    const InFlightCOP1Operation &candidate =
      inFlightCOP1Operations[programOrder[orderIndex]];
    const bool entersT =
      (isCOP1StagedOperation(
         candidate.instruction.operation) &&
       candidate.stage == COP1PipelineStage::R) ||
      (isCOP1DividerOperation(
         candidate.instruction.operation) &&
       candidate.stage == COP1PipelineStage::R &&
       candidate.remainingCycles ==
         cop1DividerTiming(
           candidate.instruction.operation).latency);
    if (entersT)
    {
      return &candidate;
    }
  }
  return nullptr;
}

EECore::COP1OperationAdvanceResult
EECore::advanceInFlightCOP1Operation(
  InFlightCOP1Operation *operation)
{
  if (!operation->active ||
      !isCOP1ManagedPipelineOperation(
        operation->instruction.operation))
  {
    throw std::logic_error(
      "EE COP1 advancement requires active managed work.");
  }
  if (cop1RetirementReady(*operation))
  {
    throw std::logic_error(
      "EE COP1 advancement received completed work.");
  }
  COP1OperationAdvanceResult result;
  result.previousStage = operation->stage;
  if (isCOP1StagedOperation(
        operation->instruction.operation))
  {
    result.outcome =
      advanceStagedCOP1Operation(operation);
    return result;
  }
  if (isCOP1RegisterMoveOperation(
        operation->instruction.operation))
  {
    result.outcome =
      advanceRegisterMoveCOP1Operation(operation);
    return result;
  }
  if (isCOP1MemoryMoveOperation(
        operation->instruction.operation))
  {
    result.outcome =
      advanceMemoryCOP1Operation(operation);
    return result;
  }
  if (isCOP1DividerOperation(
        operation->instruction.operation))
  {
    result.outcome =
      advanceDividerCOP1Operation(operation);
    return result;
  }
  throw std::logic_error(
    "EE COP1 advancement received an unmanaged operation.");
}

EECore::COP1OperationAdvanceOutcome
EECore::advanceStagedCOP1Operation(
  InFlightCOP1Operation *operation)
{
  switch (operation->stage)
  {
    case COP1PipelineStage::R:
      operation->capturedFS =
        scoreboardFPRValueForT(
          operation->instruction.destinationRegister,
          operation->programOrder);
      if (!isCOP1SingleSourceStagedOperation(
            operation->instruction.operation))
      {
        operation->capturedFT =
          scoreboardFPRValueForT(
            operation->instruction.targetRegister,
            operation->programOrder);
      }
      if (isCOP1CompoundOperation(
            operation->instruction.operation))
      {
        operation->capturedAccumulator =
          scoreboardAccumulatorValueForT(
            operation->programOrder);
      }
      operation->capturedControl =
        cop1ControlRegister(
          EECOP1Control::STATUS_REGISTER);
      operation->stage = COP1PipelineStage::T;
      return COP1OperationAdvanceOutcome::Advanced;
    case COP1PipelineStage::T:
      operation->stage = COP1PipelineStage::X;
      return COP1OperationAdvanceOutcome::Advanced;
    case COP1PipelineStage::X:
      operation->stage = COP1PipelineStage::Y;
      return COP1OperationAdvanceOutcome::Advanced;
    case COP1PipelineStage::Y:
      computeInFlightCOP1StagedOperation(operation);
      operation->stage = COP1PipelineStage::Z;
      return COP1OperationAdvanceOutcome::Advanced;
    case COP1PipelineStage::Z:
      completeInFlightCOP1(
        operation,
        COP1CompletionReason::PipelineAdvance);
      return COP1OperationAdvanceOutcome::Completed;
    case COP1PipelineStage::S1:
    case COP1PipelineStage::S2:
      break;
  }
  throw std::logic_error(
    "EE staged COP1 advancement reached a completed stage.");
}

EECore::COP1OperationAdvanceOutcome
EECore::advanceRegisterMoveCOP1Operation(
  InFlightCOP1Operation *operation)
{
  switch (operation->stage)
  {
    case COP1PipelineStage::R:
      switch (operation->instruction.operation)
      {
        case EEOperation::MoveWordFromCOP1:
        case EEOperation::MoveSingleCOP1:
          operation->capturedFS =
            scoreboardFPRValueForT(
              operation->instruction.destinationRegister,
              operation->programOrder);
          break;
        case EEOperation::MoveControlWordFromCOP1:
          operation->capturedControl =
            operation->instruction.destinationRegister ==
                EECOP1Control::STATUS_REGISTER
              ? scoreboardFCR31ValueForT(
                  operation->programOrder)
              : cop1ControlRegister(
                  operation->instruction.destinationRegister);
          break;
        default:
          break;
      }
      operation->stage = COP1PipelineStage::T;
      return COP1OperationAdvanceOutcome::Advanced;
    case COP1PipelineStage::T:
      operation->stage = COP1PipelineStage::X;
      return COP1OperationAdvanceOutcome::Advanced;
    case COP1PipelineStage::X:
      switch (operation->instruction.operation)
      {
        case EEOperation::MoveWordFromCOP1:
        case EEOperation::MoveSingleCOP1:
          operation->rawResult = operation->capturedFS;
          break;
        case EEOperation::MoveWordToCOP1:
        case EEOperation::MoveControlWordToCOP1:
          operation->rawResult =
            static_cast<std::uint32_t>(
              operation->capturedGPR);
          break;
        case EEOperation::MoveControlWordFromCOP1:
          operation->rawResult =
            operation->capturedControl;
          break;
        default:
          break;
      }
      completeInFlightCOP1(
        operation,
        COP1CompletionReason::PipelineAdvance);
      return COP1OperationAdvanceOutcome::Completed;
    case COP1PipelineStage::Y:
    case COP1PipelineStage::Z:
    case COP1PipelineStage::S1:
    case COP1PipelineStage::S2:
      break;
  }
  throw std::logic_error(
    "EE COP1 Move advancement reached an invalid stage.");
}

EECore::COP1OperationAdvanceOutcome
EECore::advanceMemoryCOP1Operation(
  InFlightCOP1Operation *operation)
{
  const bool load =
    isLoadOperation(operation->instruction.operation);
  switch (operation->stage)
  {
    case COP1PipelineStage::R:
      operation->memoryAddress =
        static_cast<std::uint32_t>(
          operation->capturedGPR +
          signExtend16(operation->instruction.immediate));
      operation->stage = COP1PipelineStage::T;
      return COP1OperationAdvanceOutcome::Advanced;
    case COP1PipelineStage::T:
      if ((operation->memoryAddress & 3) != 0)
      {
        raiseCOP1DataAccessException(
          *operation,
          load
            ? EEException::AddressErrorLoadOrFetch
            : EEException::AddressErrorStore,
          operation->memoryAddress);
        return COP1OperationAdvanceOutcome::Faulted;
      }
      if (load)
      {
        std::uint32_t value = 0;
        const bool succeeded =
          attachedBus().readData32(
            operation->memoryAddress,
            &value);
        recordMemoryTrace(
          operation->memoryAddress,
          4,
          false,
          succeeded,
          succeeded ? value : 0);
        if (!succeeded)
        {
          raiseCOP1DataAccessException(
            *operation,
            EEException::DataBusErrorLoad,
            operation->memoryAddress);
          return COP1OperationAdvanceOutcome::Faulted;
        }
        operation->capturedMemoryValue = value;
      }
      else
      {
        operation->capturedMemoryValue =
          scoreboardFPRValueForT(
            operation->instruction.targetRegister,
            operation->programOrder);
      }
      operation->rawResult =
        operation->capturedMemoryValue;
      operation->stage = COP1PipelineStage::X;
      return COP1OperationAdvanceOutcome::Advanced;
    case COP1PipelineStage::X:
      if (!load)
      {
        const bool succeeded =
          attachedBus().writeData32(
            operation->memoryAddress,
            operation->capturedMemoryValue);
        recordMemoryTrace(
          operation->memoryAddress,
          4,
          true,
          succeeded,
          operation->capturedMemoryValue);
        if (!succeeded)
        {
          raiseCOP1DataAccessException(
            *operation,
            EEException::DataBusErrorStore,
            operation->memoryAddress);
          return COP1OperationAdvanceOutcome::Faulted;
        }
      }
      completeInFlightCOP1(
        operation,
        COP1CompletionReason::PipelineAdvance);
      return COP1OperationAdvanceOutcome::Completed;
    case COP1PipelineStage::Y:
    case COP1PipelineStage::Z:
    case COP1PipelineStage::S1:
    case COP1PipelineStage::S2:
      break;
  }
  throw std::logic_error(
    "EE COP1 memory advancement reached an invalid stage.");
}

EECore::COP1OperationAdvanceOutcome
EECore::advanceDividerCOP1Operation(
  InFlightCOP1Operation *operation)
{
  if (operation->remainingCycles == 0)
  {
    return COP1OperationAdvanceOutcome::Unchanged;
  }
  --operation->remainingCycles;
  if (operation->remainingCycles != 0)
  {
    return COP1OperationAdvanceOutcome::Unchanged;
  }
  completeInFlightCOP1(
    operation,
    COP1CompletionReason::PipelineAdvance);
  return COP1OperationAdvanceOutcome::Completed;
}

void EECore::computeInFlightCOP1StagedOperation(
  InFlightCOP1Operation *operation)
{
  switch (operation->instruction.operation)
  {
    case EEOperation::AbsoluteSingleCOP1:
      operation->rawResult =
        operation->capturedFS & UINT32_C(0x7fffffff);
      return;
    case EEOperation::NegateSingleCOP1:
      operation->rawResult =
        operation->capturedFS ^ UINT32_C(0x80000000);
      return;
    case EEOperation::MaximumSingleCOP1:
    case EEOperation::MinimumSingleCOP1:
    {
      const EEFloatResult result =
        operation->instruction.operation ==
          EEOperation::MaximumSingleCOP1
          ? maxEEFloatRaw(
              operation->capturedFS,
              operation->capturedFT)
          : minEEFloatRaw(
              operation->capturedFS,
              operation->capturedFT);
      operation->rawResult = result.bits;
      operation->raisedFlags = result.flags;
      return;
    }
    case EEOperation::ConvertWordToSingleCOP1:
      operation->rawResult =
        fixedToFloatRaw(operation->capturedFS, 0);
      return;
    case EEOperation::ConvertSingleToWordCOP1:
    {
      const EEFloatResult result =
        convertEEFloatToWordRaw(operation->capturedFS);
      operation->rawResult = result.bits;
      operation->raisedFlags = result.flags;
      return;
    }
    case EEOperation::CompareFalseSingleCOP1:
    case EEOperation::CompareEqualSingleCOP1:
    case EEOperation::CompareLessThanSingleCOP1:
    case EEOperation::CompareLessThanOrEqualSingleCOP1:
    {
      const int comparison =
        compareEEFloatRaw(
          operation->capturedFS,
          operation->capturedFT);
      switch (operation->instruction.operation)
      {
        case EEOperation::CompareEqualSingleCOP1:
          operation->conditionResult = comparison == 0;
          return;
        case EEOperation::CompareLessThanSingleCOP1:
          operation->conditionResult = comparison < 0;
          return;
        case EEOperation::CompareLessThanOrEqualSingleCOP1:
          operation->conditionResult = comparison <= 0;
          return;
        default:
          operation->conditionResult = false;
          return;
      }
    }
    case EEOperation::AddSingleCOP1:
    case EEOperation::SubtractSingleCOP1:
    case EEOperation::AddSingleToAccumulatorCOP1:
    case EEOperation::SubtractSingleToAccumulatorCOP1:
    {
      const EEFloatResult result =
        operation->instruction.operation !=
          EEOperation::SubtractSingleCOP1 &&
        operation->instruction.operation !=
          EEOperation::SubtractSingleToAccumulatorCOP1
          ? addFPRaw(
              operation->capturedFS,
              operation->capturedFT)
          : subFPRaw(
              operation->capturedFS,
              operation->capturedFT);
      operation->rawResult = result.bits;
      operation->raisedFlags = result.flags;
      return;
    }
    case EEOperation::MultiplySingleCOP1:
    case EEOperation::MultiplySingleToAccumulatorCOP1:
    {
      const EEFloatResult result =
        mulFPRaw(
          operation->capturedFS,
          operation->capturedFT);
      operation->rawResult = result.bits;
      operation->raisedFlags = result.flags;
      return;
    }
    case EEOperation::MultiplyAddSingleCOP1:
    case EEOperation::MultiplyAddSingleToAccumulatorCOP1:
    case EEOperation::MultiplySubtractSingleCOP1:
    case EEOperation::MultiplySubtractSingleToAccumulatorCOP1:
    {
      const EECompoundFloatResult result =
        operation->instruction.operation ==
          EEOperation::MultiplyAddSingleCOP1 ||
        operation->instruction.operation ==
          EEOperation::MultiplyAddSingleToAccumulatorCOP1
          ? maddEEFloatRaw(
              operation->capturedAccumulator,
              operation->capturedFS,
              operation->capturedFT)
          : msubEEFloatRaw(
              operation->capturedAccumulator,
              operation->capturedFS,
              operation->capturedFT);
      operation->rawResult = result.bits;
      operation->raisedFlags = result.flags;
      operation->raisedStickyFlags = result.stickyFlags;
      return;
    }
    default:
      throw std::logic_error(
        "Unsupported staged EE COP1 ALU operation.");
  }
}

EECore::COP1DividerOccupancy
EECore::derivedCOP1DividerOccupancy() const
{
  const InFlightCOP1Operation *occupyingDivider = nullptr;
  for (const InFlightCOP1Operation &candidate :
       inFlightCOP1Operations)
  {
    if (candidate.active &&
        isCOP1DividerOperation(
          candidate.instruction.operation) &&
        candidate.remainingCycles > 1 &&
        (occupyingDivider == nullptr ||
         candidate.programOrder >
           occupyingDivider->programOrder))
    {
      occupyingDivider = &candidate;
    }
  }
  if (occupyingDivider == nullptr)
  {
    return {};
  }
  return {
    static_cast<std::uint8_t>(
      occupyingDivider->remainingCycles - 1),
    occupyingDivider->instruction.operation
  };
}

void EECore::reconcileCOP1DividerOccupancy()
{
  const COP1DividerOccupancy occupancy =
    derivedCOP1DividerOccupancy();
  cop1DividerInitiationCycles =
    occupancy.initiationCycles;
  cop1DividerOperation = occupancy.operation;
}

void EECore::startPendingCOP1Divider(
  const EEInstruction &instruction,
  std::uint32_t capturedFS,
  std::uint32_t capturedFT,
  std::uint32_t result,
  std::uint8_t raisedFlags)
{
  const EECOP1DividerTiming timing =
    cop1DividerTiming(instruction.operation);
  InFlightCOP1Operation &operation =
    allocateInFlightCOP1(
      instruction,
      pc - 4);
  operation.capturedFS = capturedFS;
  operation.capturedFT = capturedFT;
  operation.destination.mask =
    COP1_DESTINATION_FPR |
    COP1_DESTINATION_FCR31;
  operation.destination.fprRegister =
    instruction.shiftAmount;
  operation.rawResult = result;
  operation.affectedFlags =
    FP_FLAG_I_BIT | FP_FLAG_D_BIT;
  operation.raisedFlags = raisedFlags;
  operation.remainingCycles = timing.latency;
  recordCOP1StageTransition(
    operation,
    UINT8_MAX,
    COP1PipelineStage::R);
  reconcileCOP1DividerOccupancy();
}

void EECore::commitInFlightCOP1(
  InFlightCOP1Operation *operation)
{
  if (!operation->active ||
      !cop1RetirementReady(*operation))
  {
    throw std::logic_error(
      "EE COP1 commit requires completed work.");
  }
  operation->stage = COP1PipelineStage::S2;
  if ((operation->destination.mask &
       COP1_DESTINATION_FPR) != 0)
  {
    floatingPointRegisters[
      operation->destination.fprRegister] =
        operation->rawResult;
  }
  if ((operation->destination.mask &
       COP1_DESTINATION_ACCUMULATOR) != 0)
  {
    floatingPointAccumulatorRegister =
      operation->rawResult;
  }
  if ((operation->destination.mask &
       COP1_DESTINATION_FCR31) != 0)
  {
    updateCOP1ArithmeticFlags(
      operation->affectedFlags,
      operation->raisedFlags,
      operation->raisedStickyFlags);
  }
  if ((operation->destination.mask &
       COP1_DESTINATION_CONDITION) != 0)
  {
    setCOP1Condition(operation->conditionResult);
  }
  if ((operation->destination.mask &
       COP1_DESTINATION_GPR) != 0)
  {
    writeWord(
      operation->destination.gprRegister,
      operation->rawResult);
  }
  if (operation->instruction.operation ==
        EEOperation::MoveControlWordToCOP1)
  {
    setCOP1ControlRegister(
      operation->instruction.destinationRegister,
      operation->rawResult);
  }
  releaseInFlightCOP1(operation);
}

void EECore::retireInFlightCOP1(
  InFlightCOP1Operation *operation)
{
  recordCOP1Retirement(*operation);
  commitInFlightCOP1(operation);
}

void EECore::recordCOP1StageTransition(
  const InFlightCOP1Operation &operation,
  std::uint8_t fromStage,
  COP1PipelineStage toStage)
{
  recordCycleTrace(
    CycleTraceKind::COP1StageTransition,
    operation.programOrder,
    operation.instructionAddress |
      (static_cast<std::uint64_t>(
        operation.instruction.raw) << 32),
    fromStage |
      (static_cast<std::uint64_t>(
        static_cast<std::uint8_t>(toStage)) << 8) |
      (static_cast<std::uint64_t>(
        operation.remainingCycles) << 16),
    operation.destination.mask |
      (static_cast<std::uint64_t>(
        operation.destination.fprRegister) << 8) |
      (static_cast<std::uint64_t>(
        operation.destination.gprRegister) << 16));
}

void EECore::recordCOP1Retirement(
  const InFlightCOP1Operation &operation)
{
  recordCycleTrace(
    CycleTraceKind::COP1Retired,
    operation.programOrder,
    operation.instructionAddress |
      (static_cast<std::uint64_t>(
        operation.instruction.raw) << 32),
    operation.rawResult |
      (static_cast<std::uint64_t>(
        operation.destination.mask) << 32) |
      (static_cast<std::uint64_t>(
        operation.destination.fprRegister) << 40) |
      (static_cast<std::uint64_t>(
        operation.destination.gprRegister) << 48),
    operation.affectedFlags |
      (static_cast<std::uint64_t>(
        operation.raisedFlags) << 8) |
      (static_cast<std::uint64_t>(
        operation.raisedStickyFlags) << 16) |
      (operation.conditionResult ? UINT64_C(1) << 24 : 0));
}

bool EECore::cop1ScoreboardBlocks(
  const EEInstruction &instruction,
  std::uint32_t completedLoadRegisters,
  COP1ScoreboardHazard *hazard,
  COP1ScoreboardQuery query) const
{
  const bool includeAllOlderProducers =
    query == COP1ScoreboardQuery::CandidateReadiness;
  const EEInstructionDependencies dependencies =
    eeInstructionDependencies(instruction);
  if (instruction.operation ==
      EEOperation::SynchronizeLoadStore)
  {
    for (const InFlightCOP1Operation &operation :
         inFlightCOP1Operations)
    {
      if (operation.active &&
          isCOP1MemoryMoveOperation(
            operation.instruction.operation))
      {
        *hazard = {
          COP1ScoreboardResource::MemoryException,
          0,
          COP1Dependency::None,
          false,
          operation.instruction.operation
        };
        return true;
      }
    }
  }
  if (includeAllOlderProducers &&
      cop1MemoryExceptionPending())
  {
    *hazard = {
      COP1ScoreboardResource::MemoryException,
      0,
      COP1Dependency::None,
      false,
      EEOperation::Nop
    };
    return true;
  }

  if (includeAllOlderProducers)
  {
    for (std::uint8_t registerIndex = 0;
         registerIndex < GENERAL_REGISTER_COUNT;
         ++registerIndex)
    {
      if (cop1ScoreboardValue(
            COP1ScoreboardResource::GPR,
            registerIndex)
            .availability ==
              COP1ScoreboardAvailability::Unavailable)
      {
        *hazard = {
          COP1ScoreboardResource::GPR,
          registerIndex,
          COP1Dependency::ReadWrite,
          false,
          EEOperation::Nop
        };
        return true;
      }
    }
  }

  const COP1DividerOccupancy dividerOccupancy =
    derivedCOP1DividerOccupancy();
  if (isCOP1DividerOperation(instruction.operation) &&
      dividerOccupancy.initiationCycles != 0)
  {
    *hazard = {
      COP1ScoreboardResource::Divider,
      0,
      COP1Dependency::None,
      false,
      dividerOccupancy.operation
    };
    return true;
  }

  for (std::uint8_t registerIndex = 0;
       registerIndex < FLOATING_POINT_REGISTER_COUNT;
       ++registerIndex)
  {
    if ((completedLoadRegisters &
         (UINT32_C(1) << registerIndex)) == 0)
    {
      continue;
    }
    const COP1Dependency dependency =
      dependencyForAccess(
        (dependencies.fprReads &
         (UINT32_C(1) << registerIndex)) != 0,
        (dependencies.fprWrites &
         (UINT32_C(1) << registerIndex)) != 0);
    if (dependency != COP1Dependency::None)
    {
      *hazard = {
        COP1ScoreboardResource::FPR,
        registerIndex,
        dependency,
        true,
        EEOperation::LoadWordToCOP1
      };
      return true;
    }
  }

  for (std::uint8_t registerIndex = 0;
       registerIndex < FLOATING_POINT_REGISTER_COUNT;
       ++registerIndex)
  {
    const COP1Dependency dependency =
      dependencyForAccess(
        (dependencies.fprReads &
         (UINT32_C(1) << registerIndex)) != 0,
        (dependencies.fprWrites &
         (UINT32_C(1) << registerIndex)) != 0);
    if (dependency == COP1Dependency::None)
    {
      continue;
    }
    const COP1ScoreboardValue value =
      cop1ScoreboardValue(
        COP1ScoreboardResource::FPR,
        registerIndex);
    const bool orderedStagedALUDependency =
      isCOP1StagedOperation(instruction.operation) &&
      isCOP1StagedOperation(value.producerOperation) &&
      (dependency == COP1Dependency::Write ||
       value.producerStage == COP1PipelineStage::Z ||
       value.producerStage == COP1PipelineStage::S1);
    if (value.availability ==
          COP1ScoreboardAvailability::Unavailable &&
        !orderedStagedALUDependency)
    {
      *hazard = {
        COP1ScoreboardResource::FPR,
        registerIndex,
        dependency,
        false,
        value.producerOperation
      };
      return true;
    }
  }

  const COP1Dependency accumulatorDependency =
    dependencyForAccess(
      (dependencies.specialReads &
       RESOURCE_COP1_ACCUMULATOR) != 0,
      (dependencies.specialWrites &
       RESOURCE_COP1_ACCUMULATOR) != 0);
  const COP1ScoreboardValue accumulatorValue =
    cop1ScoreboardValue(
      COP1ScoreboardResource::Accumulator);
  const bool orderedStagedAccumulatorDependency =
    isCOP1StagedOperation(instruction.operation) &&
    isCOP1StagedOperation(
      accumulatorValue.producerOperation) &&
    (accumulatorDependency == COP1Dependency::Write ||
     accumulatorValue.producerStage == COP1PipelineStage::Z ||
     accumulatorValue.producerStage == COP1PipelineStage::S1);
  if (accumulatorDependency != COP1Dependency::None &&
      !orderedStagedAccumulatorDependency &&
      accumulatorValue.availability ==
        COP1ScoreboardAvailability::Unavailable)
  {
    *hazard = {
      COP1ScoreboardResource::Accumulator,
      0,
      accumulatorDependency,
      false,
      accumulatorValue.producerOperation
    };
    return true;
  }

  const bool conditionBranch =
    isCOP1ConditionBranchOperation(
      instruction.operation);
  const COP1Dependency controlDependency =
    conditionBranch
      ? COP1Dependency::None
      : dependencyForAccess(
          (dependencies.specialReads &
           RESOURCE_COP1_FCR31) != 0,
          (dependencies.specialWrites &
           RESOURCE_COP1_FCR31) != 0);
  const COP1ScoreboardValue controlValue =
    cop1ScoreboardValue(COP1ScoreboardResource::FCR31);
  const bool orderedStagedALUWrite =
    (controlDependency == COP1Dependency::Write ||
     controlDependency == COP1Dependency::ReadWrite) &&
    isCOP1StagedOperation(instruction.operation) &&
    isCOP1StagedOperation(
      controlValue.producerOperation);
  if (!isCOP1DividerOperation(instruction.operation) &&
      !orderedStagedALUWrite &&
      controlDependency != COP1Dependency::None &&
      controlValue.availability ==
        COP1ScoreboardAvailability::Unavailable)
  {
    *hazard = {
      COP1ScoreboardResource::FCR31,
      EECOP1Control::STATUS_REGISTER,
      controlDependency,
      false,
      controlValue.producerOperation
    };
    return true;
  }

  const COP1Dependency conditionDependency =
    conditionBranch
      ? COP1Dependency::Read
      : COP1Dependency::None;
  if (conditionDependency != COP1Dependency::None &&
      cop1ScoreboardValue(
        COP1ScoreboardResource::Condition)
        .availability ==
          COP1ScoreboardAvailability::Unavailable)
  {
    *hazard = {
      COP1ScoreboardResource::Condition,
      0,
      conditionDependency,
      false,
      cop1ScoreboardValue(
        COP1ScoreboardResource::Condition)
        .producerOperation
    };
    return true;
  }
  return false;
}

EECore::COP1ScoreboardValue EECore::cop1ScoreboardValue(
  COP1ScoreboardResource resource,
  std::uint8_t registerIndex) const
{
  return cop1ScoreboardValueBefore(
    resource,
    registerIndex,
    nextEEProgramOrder);
}

EECore::COP1ScoreboardValue EECore::cop1ScoreboardValueBefore(
  COP1ScoreboardResource resource,
  std::uint8_t registerIndex,
  std::uint64_t consumerOrder) const
{
  COP1ScoreboardValue value;
  switch (resource)
  {
    case COP1ScoreboardResource::FPR:
      requireFloatingPointRegisterIndex(registerIndex);
      value.value = floatingPointRegisters[registerIndex];
      break;
    case COP1ScoreboardResource::Accumulator:
      value.value = floatingPointAccumulatorRegister;
      break;
    case COP1ScoreboardResource::FCR31:
      value.value = cop1ControlRegister(
        EECOP1Control::STATUS_REGISTER);
      break;
    case COP1ScoreboardResource::Condition:
      value.value =
        cop1Condition() ? EECOP1Control::CONDITION : 0;
      break;
    case COP1ScoreboardResource::GPR:
      requireGeneralRegisterIndex(registerIndex);
      value.value =
        static_cast<std::uint32_t>(
          generalRegisters[registerIndex].low);
      break;
    case COP1ScoreboardResource::MemoryException:
    case COP1ScoreboardResource::Divider:
      break;
  }

  const InFlightCOP1Operation *producer = nullptr;
  const COP1ProgramOrderView programOrder =
    inFlightCOP1ProgramOrder();
  for (std::size_t orderIndex = programOrder.size();
       orderIndex != 0;
       --orderIndex)
  {
    const InFlightCOP1Operation &operation =
      inFlightCOP1Operations[programOrder[orderIndex - 1]];
    if (operation.programOrder >= consumerOrder)
    {
      continue;
    }
    bool writesResource = false;
    switch (resource)
    {
      case COP1ScoreboardResource::FPR:
        writesResource =
          (operation.destination.mask &
           COP1_DESTINATION_FPR) != 0 &&
          operation.destination.fprRegister == registerIndex;
        break;
      case COP1ScoreboardResource::Accumulator:
        writesResource =
          (operation.destination.mask &
           COP1_DESTINATION_ACCUMULATOR) != 0;
        break;
      case COP1ScoreboardResource::FCR31:
        writesResource =
          (operation.destination.mask &
           (COP1_DESTINATION_FCR31 |
            COP1_DESTINATION_CONDITION)) != 0;
        break;
      case COP1ScoreboardResource::Condition:
        writesResource =
          (operation.destination.mask &
           COP1_DESTINATION_CONDITION) != 0 ||
          ((operation.destination.mask &
            COP1_DESTINATION_FCR31) != 0 &&
            operation.instruction.operation ==
              EEOperation::MoveControlWordToCOP1);
        break;
      case COP1ScoreboardResource::GPR:
        writesResource =
          (operation.destination.mask &
           COP1_DESTINATION_GPR) != 0 &&
          operation.destination.gprRegister == registerIndex;
        break;
      case COP1ScoreboardResource::MemoryException:
        writesResource =
          (isCOP1MemoryMoveOperation(
             operation.instruction.operation) &&
           isLoadOperation(operation.instruction.operation) &&
           operation.stage < COP1PipelineStage::X) ||
          (isCOP1MemoryMoveOperation(
             operation.instruction.operation) &&
           isStoreOperation(operation.instruction.operation) &&
           operation.stage < COP1PipelineStage::Y);
        break;
      case COP1ScoreboardResource::Divider:
        break;
    }
    if (writesResource)
    {
      producer = &operation;
      break;
    }
  }
  if (producer == nullptr)
  {
    return value;
  }

  value.producerOrder = producer->programOrder;
  value.producerOperation = producer->instruction.operation;
  value.producerStage = producer->stage;
  if (producer->stage < COP1PipelineStage::S1 ||
      (producer->stage == COP1PipelineStage::S1 &&
       isCOP1ManagedPipelineOperation(
         producer->instruction.operation)))
  {
    value.availability =
      COP1ScoreboardAvailability::Unavailable;
    return value;
  }
  if (producer->stage == COP1PipelineStage::S2)
  {
    return value;
  }

  value.availability =
    COP1ScoreboardAvailability::BypassReady;
  switch (resource)
  {
    case COP1ScoreboardResource::FPR:
    case COP1ScoreboardResource::Accumulator:
      value.value = producer->rawResult;
      break;
    case COP1ScoreboardResource::FCR31:
    {
      std::uint32_t status =
        producer->capturedControl &
        EECOP1Control::STATUS_WRITABLE_MASK;
      if (producer->instruction.operation ==
            EEOperation::MoveControlWordToCOP1 &&
          producer->instruction.destinationRegister ==
            EECOP1Control::STATUS_REGISTER)
      {
        status =
          static_cast<std::uint32_t>(
            producer->capturedGPR) &
          EECOP1Control::STATUS_WRITABLE_MASK;
      }
      else if ((producer->destination.mask &
                COP1_DESTINATION_FCR31) != 0)
      {
        status = updatedCOP1Status(
          status,
          producer->affectedFlags,
          producer->raisedFlags,
          producer->raisedStickyFlags);
      }
      if ((producer->destination.mask &
           COP1_DESTINATION_CONDITION) != 0)
      {
        status &= ~EECOP1Control::CONDITION;
        if (producer->conditionResult)
        {
          status |= EECOP1Control::CONDITION;
        }
      }
      value.value = status | EECOP1Control::STATUS_FIXED;
      break;
    }
    case COP1ScoreboardResource::Condition:
      if ((producer->destination.mask &
           COP1_DESTINATION_CONDITION) != 0)
      {
        value.value =
          producer->conditionResult
            ? EECOP1Control::CONDITION
            : 0;
      }
      else
      {
        value.value =
          static_cast<std::uint32_t>(
            producer->capturedGPR) &
          EECOP1Control::CONDITION;
      }
      break;
    case COP1ScoreboardResource::GPR:
      value.value = producer->rawResult;
      break;
    case COP1ScoreboardResource::MemoryException:
    case COP1ScoreboardResource::Divider:
      break;
  }
  return value;
}

std::uint32_t EECore::scoreboardFPRValue(
  std::uint8_t registerIndex) const
{
  const COP1ScoreboardValue value =
    cop1ScoreboardValue(
      COP1ScoreboardResource::FPR,
      registerIndex);
  if (value.availability ==
      COP1ScoreboardAvailability::Unavailable)
  {
    throw std::logic_error(
      "Unavailable EE COP1 FPR reached execution.");
  }
  return value.value;
}

std::uint32_t EECore::scoreboardFPRValueForT(
  std::uint8_t registerIndex,
  std::uint64_t consumerOrder) const
{
  requireFloatingPointRegisterIndex(registerIndex);
  const InFlightCOP1Operation *producer = nullptr;
  const COP1ProgramOrderView programOrder =
    inFlightCOP1ProgramOrder();
  for (std::size_t orderIndex = programOrder.size();
       orderIndex != 0;
       --orderIndex)
  {
    const InFlightCOP1Operation &operation =
      inFlightCOP1Operations[programOrder[orderIndex - 1]];
    if (operation.programOrder >= consumerOrder ||
        (operation.destination.mask &
         COP1_DESTINATION_FPR) == 0 ||
        operation.destination.fprRegister != registerIndex)
    {
      continue;
    }
    producer = &operation;
    break;
  }
  if (producer == nullptr ||
      producer->stage == COP1PipelineStage::S2)
  {
    return floatingPointRegisters[registerIndex];
  }
  if (producer->stage == COP1PipelineStage::S1 ||
      (isCOP1StagedOperation(
         producer->instruction.operation) &&
       producer->stage == COP1PipelineStage::Z))
  {
    return producer->rawResult;
  }
  throw std::logic_error(
    "Unavailable EE COP1 FPR reached 2T capture.");
}

std::uint32_t EECore::scoreboardAccumulatorValueForT(
  std::uint64_t consumerOrder) const
{
  const InFlightCOP1Operation *producer = nullptr;
  const COP1ProgramOrderView programOrder =
    inFlightCOP1ProgramOrder();
  for (std::size_t orderIndex = programOrder.size();
       orderIndex != 0;
       --orderIndex)
  {
    const InFlightCOP1Operation &operation =
      inFlightCOP1Operations[programOrder[orderIndex - 1]];
    if (operation.programOrder >= consumerOrder ||
        (operation.destination.mask &
         COP1_DESTINATION_ACCUMULATOR) == 0)
    {
      continue;
    }
    producer = &operation;
    break;
  }
  if (producer == nullptr ||
      producer->stage == COP1PipelineStage::S2)
  {
    return floatingPointAccumulatorRegister;
  }
  if (producer->stage == COP1PipelineStage::S1 ||
      (isCOP1StagedOperation(
         producer->instruction.operation) &&
       producer->stage == COP1PipelineStage::Z))
  {
    return producer->rawResult;
  }
  throw std::logic_error(
    "Unavailable EE COP1 accumulator reached 2T capture.");
}

std::uint32_t EECore::scoreboardFCR31ValueForT(
  std::uint64_t consumerOrder) const
{
  const COP1ScoreboardValue value =
    cop1ScoreboardValueBefore(
      COP1ScoreboardResource::FCR31,
      0,
      consumerOrder);
  if (value.availability ==
      COP1ScoreboardAvailability::Unavailable)
  {
    throw std::logic_error(
      "Unavailable EE FCR31 reached execution.");
  }
  return value.value;
}

bool EECore::scoreboardCOP1Condition() const
{
  const COP1ScoreboardValue value =
    cop1ScoreboardValue(
      COP1ScoreboardResource::Condition);
  if (value.availability ==
      COP1ScoreboardAvailability::Unavailable)
  {
    throw std::logic_error(
      "Unavailable EE COP1 condition reached execution.");
  }
  return (value.value & EECOP1Control::CONDITION) != 0;
}

EECore::COP1Dependency EECore::dependencyForAccess(
  bool reads,
  bool writes)
{
  return static_cast<COP1Dependency>(
    (reads ? static_cast<std::uint8_t>(
      COP1Dependency::Read) : 0) |
    (writes ? static_cast<std::uint8_t>(
      COP1Dependency::Write) : 0));
}

bool EECore::validateDelaySlotInstruction(
  const EEInstruction &instruction,
  std::uint32_t address)
{
  if (!branchDelayPending)
  {
    return true;
  }
  if (!isEEDelaySlotInstructionLegal(
         lastDecodedInstruction,
         instruction))
  {
    haltUndefinedOperation(address, instruction.raw);
    return false;
  }
  return true;
}

void EECore::scheduleBranch(
  bool condition,
  bool likely,
  std::uint32_t target,
  std::uint32_t address)
{
  recordCycleTrace(
    CycleTraceKind::BranchScheduled,
    address,
    target,
    (condition ? UINT64_C(1) : 0) |
      (likely ? UINT64_C(2) : 0));
  if (likely && !condition)
  {
    pc = address + 8;
    stagingLatch = {};
    return;
  }
  branchDelayPending = true;
  branchDelayTarget = condition ? target : address + 8;
  branchInstructionAddress = address;
  branchDelayFromLikely = likely;
  branchDelayTaken = condition;
}

void EECore::recordCycleTrace(
  CycleTraceKind kind,
  std::uint64_t value0,
  std::uint64_t value1,
  std::uint64_t value2,
  std::uint64_t value3)
{
  if (cycleTraceEventCount >= cycleTraceEvents.size())
  {
    throw std::logic_error(
      "EE produced too many trace events in one cycle.");
  }
  cycleTraceEvents[cycleTraceEventCount++] = {
    kind,
    value0,
    value1,
    value2,
    value3
  };
}

void EECore::recordMemoryTrace(
  std::uint32_t address,
  std::uint8_t width,
  bool write,
  bool succeeded,
  std::uint64_t low,
  std::uint64_t high)
{
  recordCycleTrace(
    CycleTraceKind::MemoryAccess,
    address,
    low,
    high,
    width |
      (write ? UINT64_C(1) << 8 : 0) |
      (succeeded ? UINT64_C(1) << 9 : 0));
}

EEInstructionExecutionOutcome
EECore::raiseDataAccessException(
  EEException type,
  std::uint32_t instructionAddress,
  std::uint32_t dataAddress,
  std::uint32_t instruction)
{
  enterException(
    type,
    instructionAddress,
    dataAddress,
    instruction);
  return EEInstructionExecutionOutcome::Faulted;
}

bool EECore::raiseCOP1DataAccessException(
  const InFlightCOP1Operation &operation,
  EEException type,
  std::uint32_t dataAddress)
{
  const bool alreadyExceptionLevel =
    (cop0Status & EECOP0Status::EXCEPTION_LEVEL) != 0;
  const bool delaySlot =
    cop1DividerPostDelayInstructions != 0 &&
    operation.instructionAddress ==
      cop1DividerPostDelayBranchAddress + 4;
  const std::uint32_t owningBranchAddress =
    cop1DividerPostDelayBranchAddress;
  const std::uint64_t previousProgramOrder =
    executingProgramOrder;
  executingProgramOrder = operation.programOrder;
  enterException(
    type,
    operation.instructionAddress,
    dataAddress,
    operation.instruction.raw);
  executingProgramOrder = previousProgramOrder;
  if (delaySlot && !alreadyExceptionLevel)
  {
    cop0EPC = owningBranchAddress;
    cop0Cause |= EECOP0Cause::BRANCH_DELAY;
  }
  return false;
}

void EECore::enterException(
  EEException type,
  std::uint32_t instructionAddress,
  std::uint32_t address,
  std::uint32_t instruction)
{
  const std::uint64_t exceptionBoundary =
    executingProgramOrder != 0
      ? executingProgramOrder
      : nextEEProgramOrder;
  cancelInFlightCOP1(
    COP1CancellationScope::AtOrAfter,
    exceptionBoundary);
  const bool alreadyExceptionLevel =
    (cop0Status & EECOP0Status::EXCEPTION_LEVEL) != 0;
  if (!alreadyExceptionLevel)
  {
    const bool delaySlot = branchDelayPending;
    cop0EPC = delaySlot
      ? branchInstructionAddress
      : instructionAddress;
    if (delaySlot)
    {
      cop0Cause |= EECOP0Cause::BRANCH_DELAY;
    }
    else
    {
      cop0Cause &= ~EECOP0Cause::BRANCH_DELAY;
    }
  }
  cop0Cause =
    (cop0Cause & ~EECOP0Cause::EXCEPTION_CODE_MASK) |
    (static_cast<std::uint32_t>(exceptionCode(type)) << 2);
  cop0Status |= EECOP0Status::EXCEPTION_LEVEL;
  if (type == EEException::AddressErrorLoadOrFetch ||
      type == EEException::AddressErrorStore)
  {
    cop0BadVAddr = address;
  }

  exception = type;
  faultAddress = address;
  pc = exceptionVector(type, alreadyExceptionLevel);
  state = EEExecutionState::Running;
  haltReason = EEStopReason::None;
  rejectedInstructionValue = instruction;
  exceptionEnteredThisCycle = true;
  if (type == EEException::Interrupt)
  {
    recordCycleTrace(
      CycleTraceKind::InterruptDelivered,
      instructionAddress,
      cop0Status,
      cop0Cause,
      pc);
  }
  recordCycleTrace(
    CycleTraceKind::ExceptionEntered,
    static_cast<std::uint8_t>(type),
    address,
    pc,
    cop0Cause);
  clearBranchDelayContinuation();
  cop1DividerPostDelayInstructions = 0;
  cop1DividerPostDelayBranchAddress = 0;
  cop1DividerPostDelayTargetAddress = 0;
  cop1DividerPostDelayTaken = false;
  cop1DividerPostTargetInstructions = 0;
  cop1DividerPostTargetAddress = 0;
  clearIssueFrontEnd();
}

void EECore::cancelInFlightCOP1(
  COP1CancellationScope scope,
  std::uint64_t programOrder)
{
  if (scope != COP1CancellationScope::All &&
      programOrder == 0)
  {
    throw std::logic_error(
      "EE COP1 cancellation requires assigned program order.");
  }
  const COP1ProgramOrderView order =
    inFlightCOP1ProgramOrder();
  for (std::size_t orderIndex = 0;
       orderIndex < order.size();
       ++orderIndex)
  {
    InFlightCOP1Operation &operation =
      inFlightCOP1Operations[order[orderIndex]];
    bool cancel = false;
    switch (scope)
    {
      case COP1CancellationScope::All:
        cancel = true;
        break;
      case COP1CancellationScope::AtOrAfter:
        cancel = operation.programOrder >= programOrder;
        break;
      case COP1CancellationScope::After:
        cancel = operation.programOrder > programOrder;
        break;
      default:
        throw std::logic_error(
          "EE COP1 cancellation scope is invalid.");
    }
    if (cancel)
    {
      releaseInFlightCOP1(&operation);
    }
  }
  reconcileCOP1DividerOccupancy();
}

std::uint8_t EECore::exceptionCode(EEException type)
{
  switch (type)
  {
    case EEException::Interrupt:
      return EEExceptionCode::INTERRUPT;
    case EEException::AddressErrorLoadOrFetch:
      return EEExceptionCode::ADDRESS_ERROR_LOAD_OR_FETCH;
    case EEException::AddressErrorStore:
      return EEExceptionCode::ADDRESS_ERROR_STORE;
    case EEException::InstructionBusError:
      return EEExceptionCode::INSTRUCTION_BUS_ERROR;
    case EEException::DataBusErrorLoad:
    case EEException::DataBusErrorStore:
      return EEExceptionCode::DATA_BUS_ERROR;
    case EEException::SystemCall:
      return EEExceptionCode::SYSTEM_CALL;
    case EEException::Breakpoint:
      return EEExceptionCode::BREAKPOINT;
    case EEException::ReservedInstruction:
      return EEExceptionCode::RESERVED_INSTRUCTION;
    case EEException::CoprocessorUnusable:
      return EEExceptionCode::COPROCESSOR_UNUSABLE;
    case EEException::ArithmeticOverflow:
      return EEExceptionCode::ARITHMETIC_OVERFLOW;
    case EEException::None:
      break;
  }
  throw std::invalid_argument(
    "EE exception type has no architectural code.");
}

std::uint32_t EECore::exceptionVector(
  EEException type,
  bool alreadyExceptionLevel) const
{
  const bool bootstrap =
    (cop0Status &
      EECOP0Status::BOOTSTRAP_EXCEPTION_VECTOR) != 0;
  const bool interrupt =
    type == EEException::Interrupt &&
    !alreadyExceptionLevel;
  if (bootstrap)
  {
    return interrupt
      ? EEExceptionVector::BOOTSTRAP_INTERRUPT
      : EEExceptionVector::BOOTSTRAP_GENERAL;
  }
  return interrupt
    ? EEExceptionVector::INTERRUPT
    : EEExceptionVector::GENERAL;
}

void EECore::setInterruptLines(bool intc, bool dmac)
{
  cop0Cause &=
    ~(EECOP0Cause::INTC_PENDING |
      EECOP0Cause::DMAC_PENDING);
  if (intc)
  {
    cop0Cause |= EECOP0Cause::INTC_PENDING;
  }
  if (dmac)
  {
    cop0Cause |= EECOP0Cause::DMAC_PENDING;
  }
}

bool EECore::interruptDeliverable() const
{
  constexpr std::uint32_t interruptLines =
    EECOP0Cause::INTC_PENDING |
    EECOP0Cause::DMAC_PENDING;
  constexpr std::uint32_t blockedLevels =
    EECOP0Status::EXCEPTION_LEVEL |
    EECOP0Status::ERROR_LEVEL;
  return
    (cop0Status & EECOP0Status::INTERRUPT_ENABLE) != 0 &&
    (cop0Status &
      EECOP0Status::MASTER_INTERRUPT_ENABLE) != 0 &&
    (cop0Status & blockedLevels) == 0 &&
    (cop0Cause & cop0Status & interruptLines) != 0;
}

EEExecutionState EECore::executionState() const
{
  return state;
}

EEStopReason EECore::stopReason() const
{
  return haltReason;
}

std::uint64_t EECore::elapsedCycles() const
{
  return cycles;
}

std::uint64_t EECore::stateHash() const
{
  std::uint64_t hash = EE_STATE_FNV_OFFSET_BASIS;
  for (const EERegister128 &value : generalRegisters)
  {
    hashEEStateValue(&hash, value.low);
    hashEEStateValue(&hash, value.high);
  }
  for (const std::uint32_t value : floatingPointRegisters)
  {
    hashEEStateValue(&hash, value);
  }
  hashEEStateValue(&hash, floatingPointAccumulatorRegister);
  hashEEStateValue(&hash, cop1StatusRegister);
  hashEEStateValue(&hash, pc);
  hashEEStateValue(&hash, hiRegister);
  hashEEStateValue(&hash, loRegister);
  hashEEStateValue(&hash, hi1Register);
  hashEEStateValue(&hash, lo1Register);
  hashEEStateValue(&hash, saRegister);
  hashEEStateValue(&hash, cop0BadVAddr);
  hashEEStateValue(&hash, cop0Count);
  hashEEStateValue(&hash, cop0Compare);
  hashEEStateValue(&hash, cop0Status);
  hashEEStateValue(&hash, cop0Cause);
  hashEEStateValue(&hash, cop0EPC);
  hashEEStateValue(&hash, cop0ErrorEPC);
  hashEEStateValue(
    &hash,
    static_cast<std::uint8_t>(exception));
  hashEEStateValue(&hash, faultAddress);
  hashEEStateValue(&hash, static_cast<std::uint8_t>(state));
  hashEEStateValue(
    &hash,
    static_cast<std::uint8_t>(haltReason));
  hashEEStateValue(&hash, cycles);
  hashEEStateValue(&hash, lastInstructionValid);
  hashEEStateValue(&hash, lastAddress);
  hashEEStateValue(&hash, lastDecodedInstruction.raw);
  hashEEStateValue(&hash, rejectedInstructionValue);
  hashEEStateValue(&hash, issueLatch.valid);
  hashEEStateValue(&hash, issueLatch.address);
  hashEEStateValue(&hash, issueLatch.instruction.raw);
  hashEEStateValue(
    &hash,
    static_cast<std::uint8_t>(issueLatch.failure));
  hashEEStateValue(&hash, stagingLatch.valid);
  hashEEStateValue(&hash, stagingLatch.address);
  hashEEStateValue(&hash, stagingLatch.instruction.raw);
  hashEEStateValue(
    &hash,
    static_cast<std::uint8_t>(stagingLatch.failure));
  hashEEStateValue(&hash, nextEEProgramOrder);
  for (const InFlightCOP1Operation &operation :
       inFlightCOP1Operations)
  {
    hashEEStateValue(&hash, operation.active);
    hashEEStateValue(&hash, operation.programOrder);
    hashEEStateValue(
      &hash,
      static_cast<std::uint8_t>(operation.stage));
    hashEEStateValue(&hash, operation.instructionAddress);
    hashEEStateValue(&hash, operation.instruction.raw);
    hashEEStateValue(&hash, operation.capturedFS);
    hashEEStateValue(&hash, operation.capturedFT);
    hashEEStateValue(&hash, operation.capturedAccumulator);
    hashEEStateValue(&hash, operation.capturedControl);
    hashEEStateValue(&hash, operation.capturedGPR);
    hashEEStateValue(&hash, operation.memoryAddress);
    hashEEStateValue(&hash, operation.capturedMemoryValue);
    hashEEStateValue(&hash, operation.destination.mask);
    hashEEStateValue(
      &hash,
      operation.destination.fprRegister);
    hashEEStateValue(
      &hash,
      operation.destination.gprRegister);
    hashEEStateValue(&hash, operation.rawResult);
    hashEEStateValue(&hash, operation.affectedFlags);
    hashEEStateValue(&hash, operation.raisedFlags);
    hashEEStateValue(&hash, operation.raisedStickyFlags);
    hashEEStateValue(&hash, operation.conditionResult);
    hashEEStateValue(&hash, operation.remainingCycles);
  }
  const auto hashPending =
    [&hash](const PendingMultiplyDivide &operation)
    {
      hashEEStateValue(&hash, operation.active);
      hashEEStateValue(&hash, operation.remainingCycles);
      hashEEStateValue(&hash, operation.hiResult);
      hashEEStateValue(&hash, operation.loResult);
      hashEEStateValue(
        &hash,
        operation.resultDestination ==
          MACResultDestination::HIAndLOAndGPR);
      hashEEStateValue(&hash, operation.generalRegister);
      hashEEStateValue(
        &hash,
        operation.generalRegisterResult);
    };
  hashPending(pendingMac0);
  hashPending(pendingMac1);
  const COP1DividerOccupancy dividerOccupancy =
    derivedCOP1DividerOccupancy();
  hashEEStateValue(
    &hash,
    dividerOccupancy.initiationCycles);
  hashEEStateValue(
    &hash,
    static_cast<std::uint8_t>(
      dividerOccupancy.operation));
  hashEEStateValue(
    &hash,
    shiftAmountOrdering.accessHistory());
  hashEEStateValue(
    &hash,
    shiftAmountOrdering.readHistory());
  hashEEStateValue(&hash, branchDelayPending);
  hashEEStateValue(&hash, branchDelayTarget);
  hashEEStateValue(&hash, branchInstructionAddress);
  hashEEStateValue(&hash, branchDelayFromLikely);
  hashEEStateValue(&hash, branchDelayTaken);
  hashEEStateValue(&hash, cop1DividerPostDelayInstructions);
  hashEEStateValue(&hash, cop1DividerPostDelayBranchAddress);
  hashEEStateValue(&hash, cop1DividerPostDelayTargetAddress);
  hashEEStateValue(&hash, cop1DividerPostDelayTaken);
  hashEEStateValue(&hash, cop1DividerPostTargetInstructions);
  hashEEStateValue(&hash, cop1DividerPostTargetAddress);
  return hash;
}

bool EECore::hasLastInstruction() const
{
  return lastInstructionValid;
}

std::uint32_t EECore::lastInstructionAddress() const
{
  if (!lastInstructionValid)
  {
    throw std::logic_error(
      "EE Core has no decoded instruction.");
  }
  return lastAddress;
}

const EEInstruction &EECore::lastInstruction() const
{
  if (!lastInstructionValid)
  {
    throw std::logic_error(
      "EE Core has no decoded instruction.");
  }
  return lastDecodedInstruction;
}

const EEIssueSelection &EECore::lastIssueSelection() const
{
  return issueSelection;
}

const EEAcceptanceRecords &
EECore::acceptanceRecordsThisCycle() const
{
  return acceptanceRecords;
}

std::uint32_t EECore::rejectedInstruction() const
{
  return rejectedInstructionValue;
}

const EERegister128 &EECore::generalRegister(
  std::size_t index) const
{
  requireGeneralRegisterIndex(index);
  return generalRegisters[index];
}

void EECore::setGeneralRegister(
  std::size_t index,
  const EERegister128 &value)
{
  requireGeneralRegisterIndex(index);
  if (index != 0)
  {
    generalRegisters[index] = value;
  }
}

std::uint32_t EECore::floatingPointRegister(
  std::size_t index) const
{
  requireFloatingPointRegisterIndex(index);
  return floatingPointRegisters[index];
}

void EECore::setFloatingPointRegister(
  std::size_t index,
  std::uint32_t value)
{
  requireFloatingPointRegisterIndex(index);
  floatingPointRegisters[index] = value;
}

std::uint32_t EECore::floatingPointAccumulator() const
{
  return floatingPointAccumulatorRegister;
}

void EECore::setFloatingPointAccumulator(
  std::uint32_t value)
{
  floatingPointAccumulatorRegister = value;
}

std::uint32_t EECore::cop1ControlRegister(
  std::size_t index) const
{
  switch (index)
  {
    case EECOP1Control::IMPLEMENTATION_REVISION_REGISTER:
      return EECOP1Control::IMPLEMENTATION_REVISION;
    case EECOP1Control::STATUS_REGISTER:
      return
        cop1StatusRegister |
        EECOP1Control::STATUS_FIXED;
    default:
      throw std::out_of_range(
        "EE COP1 control register is not implemented.");
  }
}

bool EECore::cop1Condition() const
{
  return
    (cop1StatusRegister & EECOP1Control::CONDITION) != 0;
}

void EECore::setCOP1ControlRegister(
  std::size_t index,
  std::uint32_t value)
{
  switch (index)
  {
    case EECOP1Control::IMPLEMENTATION_REVISION_REGISTER:
      return;
    case EECOP1Control::STATUS_REGISTER:
      cop1StatusRegister =
        value & EECOP1Control::STATUS_WRITABLE_MASK;
      return;
    default:
      throw std::out_of_range(
        "EE COP1 control register is not implemented.");
  }
}

void EECore::setCOP1Condition(bool condition)
{
  cop1StatusRegister &= ~EECOP1Control::CONDITION;
  if (condition)
  {
    cop1StatusRegister |= EECOP1Control::CONDITION;
  }
}

void EECore::updateCOP1ArithmeticFlags(
  std::uint8_t affectedFlags,
  std::uint8_t raisedFlags,
  std::uint8_t raisedStickyFlags)
{
  constexpr std::uint8_t SUPPORTED_FLAGS =
    FP_FLAG_I_BIT |
    FP_FLAG_D_BIT |
    FP_FLAG_OVERFLOW |
    FP_FLAG_UNDERFLOW;
  if ((affectedFlags & ~SUPPORTED_FLAGS) != 0 ||
      (raisedFlags & ~affectedFlags) != 0 ||
      (raisedStickyFlags & ~affectedFlags) != 0)
  {
    throw std::invalid_argument(
      "Invalid EE COP1 arithmetic flag update.");
  }

  cop1StatusRegister = updatedCOP1Status(
    cop1StatusRegister,
    affectedFlags,
    raisedFlags,
    raisedStickyFlags);
}

std::uint32_t EECore::programCounter() const
{
  return pc;
}

void EECore::setProgramCounter(std::uint32_t value)
{
  pc = value;
  clearIssueFrontEnd();
  clearBranchDelayContinuation();
  issueSelection = {};
}

std::uint64_t EECore::hi() const
{
  return hiRegister;
}

void EECore::setHI(std::uint64_t value)
{
  hiRegister = value;
}

std::uint64_t EECore::lo() const
{
  return loRegister;
}

void EECore::setLO(std::uint64_t value)
{
  loRegister = value;
}

std::uint64_t EECore::hi1() const
{
  return hi1Register;
}

void EECore::setHI1(std::uint64_t value)
{
  hi1Register = value;
}

std::uint64_t EECore::lo1() const
{
  return lo1Register;
}

void EECore::setLO1(std::uint64_t value)
{
  lo1Register = value;
}

std::uint32_t EECore::shiftAmount() const
{
  return saRegister;
}

void EECore::setShiftAmount(std::uint32_t value)
{
  saRegister = value;
}

std::uint32_t EECore::cop0Register(
  EECOP0Register registerIndex) const
{
  switch (registerIndex)
  {
    case EECOP0Register::BadVAddr:
      return cop0BadVAddr;
    case EECOP0Register::Count:
      return cop0Count;
    case EECOP0Register::Compare:
      return cop0Compare;
    case EECOP0Register::Status:
      return cop0Status;
    case EECOP0Register::Cause:
      return cop0Cause;
    case EECOP0Register::EPC:
      return cop0EPC;
    case EECOP0Register::ErrorEPC:
      return cop0ErrorEPC;
  }
  throw std::out_of_range(
    "EE COP0 register is not implemented.");
}

void EECore::setCOP0Register(
  EECOP0Register registerIndex,
  std::uint32_t value)
{
  switch (registerIndex)
  {
    case EECOP0Register::BadVAddr:
      cop0BadVAddr = value;
      return;
    case EECOP0Register::Count:
      cop0Count = value;
      return;
    case EECOP0Register::Compare:
      cop0Compare = value;
      return;
    case EECOP0Register::Status:
      cop0Status = value;
      return;
    case EECOP0Register::Cause:
      cop0Cause = value;
      return;
    case EECOP0Register::EPC:
      cop0EPC = value;
      return;
    case EECOP0Register::ErrorEPC:
      cop0ErrorEPC = value;
      return;
  }
  throw std::out_of_range(
    "EE COP0 register is not implemented.");
}

bool EECore::exceptionPending() const
{
  return exception != EEException::None;
}

EEException EECore::pendingException() const
{
  return exception;
}

std::uint32_t EECore::exceptionAddress() const
{
  return faultAddress;
}

void EECore::clearPendingException()
{
  exception = EEException::None;
  faultAddress = 0;
}

void EECore::requireFloatingPointRegisterIndex(
  std::size_t index)
{
  if (index >= FLOATING_POINT_REGISTER_COUNT)
  {
    throw std::out_of_range(
      "EE floating-point register index is out of range.");
  }
}

void EECore::requireGeneralRegisterIndex(
  std::size_t index)
{
  if (index >= GENERAL_REGISTER_COUNT)
  {
    throw std::out_of_range(
      "EE general-purpose register index is out of range.");
  }
}

EEBus &EECore::attachedBus() const
{
  if (bus == nullptr)
  {
    throw std::logic_error(
      "EE Core bus is not attached.");
  }
  return *bus;
}

VPU &EECore::attachedVU0() const
{
  if (vu0 == nullptr)
  {
    throw std::logic_error(
      "EE Core VU0 is not attached.");
  }
  return *vu0;
}

VPU &EECore::attachedVU1() const
{
  if (vu1 == nullptr)
  {
    throw std::logic_error(
      "EE Core VU1 is not attached.");
  }
  return *vu1;
}

bool EECore::readCOP2ControlRegister(
  std::uint8_t index,
  std::uint32_t *value) const
{
  if (value == nullptr)
  {
    throw std::invalid_argument(
      "COP2 control read requires an output.");
  }
  if (index < 16)
  {
    *value = attachedVU0().intRegisterValue(index);
    return true;
  }
  switch (index)
  {
    case 16:
      *value = attachedVU0().statusFlagsValue();
      return true;
    case 17:
      *value = attachedVU0().macFlagsValue();
      return true;
    case 18:
      *value = attachedVU0().clippingFlagsValue();
      return true;
    case 20:
      *value = attachedVU0().randomRegisterValue();
      return true;
    case 21:
      *value = attachedVU0().iRegisterBits();
      return true;
    case 22:
      *value = attachedVU0().qRegisterBits();
      return true;
    case 26:
      if (!attachedVU0().hasTerminationPosition())
      {
        return false;
      }
      *value = attachedVU0().terminationPosition();
      return true;
    case 27:
      *value = attachedVU0().callAddressRegister();
      return true;
    case 28:
      *value =
        (attachedVU0().dBitEnabled() ? UINT32_C(1) << 2 : 0) |
        (attachedVU0().tBitEnabled() ? UINT32_C(1) << 3 : 0) |
        (attachedVU1().dBitEnabled() ? UINT32_C(1) << 10 : 0) |
        (attachedVU1().tBitEnabled() ? UINT32_C(1) << 11 : 0);
      return true;
    case 29:
      *value = vpuStatusRegister();
      return true;
    default:
      return false;
  }
}

bool EECore::writeCOP2ControlRegister(
  std::uint8_t index,
  std::uint32_t value)
{
  if (index < 16)
  {
    attachedVU0().loadIntRegister(
      index,
      static_cast<std::uint16_t>(value));
    return true;
  }
  switch (index)
  {
    case 16:
      attachedVU0().setStatusFlagsValue(value);
      return true;
    case 18:
      attachedVU0().setClippingFlagsValue(value);
      return true;
    case 20:
      if (attachedVU0().microModeActive())
      {
        return false;
      }
      attachedVU0().setRandomRegisterValue(value);
      return true;
    case 21:
      if (attachedVU0().microModeActive())
      {
        return false;
      }
      attachedVU0().setIRegisterBits(value);
      return true;
    case 22:
      if (attachedVU0().microModeActive())
      {
        return false;
      }
      attachedVU0().setQRegisterBits(value);
      return true;
    case 27:
      attachedVU0().setCallAddressRegister(value);
      return true;
    case 28:
      if ((value & (UINT32_C(1) << 1)) != 0)
      {
        attachedVU0().resetFromControl();
      }
      if ((value & (UINT32_C(1) << 9)) != 0)
      {
        attachedVU1().resetFromControl();
      }
      if ((value & UINT32_C(1)) != 0)
      {
        attachedVU0().forceBreak();
      }
      if ((value & (UINT32_C(1) << 8)) != 0)
      {
        attachedVU1().forceBreak();
      }
      attachedVU0().setDBitEnabled(
        (value & (UINT32_C(1) << 2)) != 0);
      attachedVU0().setTBitEnabled(
        (value & (UINT32_C(1) << 3)) != 0);
      attachedVU1().setDBitEnabled(
        (value & (UINT32_C(1) << 10)) != 0);
      attachedVU1().setTBitEnabled(
        (value & (UINT32_C(1) << 11)) != 0);
      return true;
    case 31:
    {
      if (attachedVU1().clockActive())
      {
        return true;
      }
      const std::uint16_t startAddress =
        static_cast<std::uint16_t>(value);
      if ((startAddress & 7) != 0 ||
          startAddress > attachedVU1().microMemorySize() - 8)
      {
        return false;
      }
      attachedVU1().startMicroMode(startAddress);
      return true;
    }
    default:
      return false;
  }
}

std::uint32_t EECore::vpuStatusRegister() const
{
  std::uint32_t value = 0;
  const auto addUnitStatus =
    [&value](const VPU &vpu, std::uint8_t shift)
    {
      if (vpu.microModeActive())
      {
        value |= UINT32_C(1) << shift;
      }
      if (vpu.stoppedByDBit())
      {
        value |= UINT32_C(1) << (shift + 1);
      }
      if (vpu.stoppedByTBit())
      {
        value |= UINT32_C(1) << (shift + 2);
      }
      if (vpu.stoppedByForceBreak())
      {
        value |= UINT32_C(1) << (shift + 3);
      }
    };
  addUnitStatus(attachedVU0(), 0);
  addUnitStatus(attachedVU1(), 8);
  if (attachedVU0().divisionUnitBusy())
  {
    value |= UINT32_C(1) << 5;
  }
  if (attachedVU1().waitingForXGKICK())
  {
    value |= UINT32_C(1) << 12;
  }
  if (attachedVU1().divisionUnitBusy())
  {
    value |= UINT32_C(1) << 13;
  }
  if (attachedVU1().elementaryFunctionUnitBusy())
  {
    value |= UINT32_C(1) << 14;
  }
  return value;
}

EEInstructionFetchResult EECore::raiseFetchException(
  EEException type,
  std::uint32_t address)
{
  exception = type;
  faultAddress = address;
  return {false, address, 0};
}
