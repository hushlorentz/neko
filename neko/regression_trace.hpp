#ifndef REGRESSION_TRACE_HPP
#define REGRESSION_TRACE_HPP

#include <cstdint>
#include <vector>

#include "gs_display.hpp"

enum class NekoTraceSubsystem : std::uint8_t
{
  VU0,
  VU1,
  VIF0,
  VIF1,
  GIF,
  GIFDMAC,
  GS,
  InterruptController,
  Display,
  Input,
  EE
};

enum class NekoTraceEventType : std::uint8_t
{
  Progress,
  StateChanged,
  TransferCompleted,
  InterruptChanged,
  PresentationBoundary,
  InstructionIssued,
  BranchScheduled,
  MemoryAccess,
  ExceptionEntered,
  InterruptDelivered,
  StateSnapshot,
  COP1LoadInterlock,
  COP1ResourceInterlock,
  COP1DividerHazard,
  COP1StageTransition,
  COP1Retired
};

namespace NekoEETraceBranch
{
  constexpr std::uint64_t TAKEN = UINT64_C(1);
  constexpr std::uint64_t LIKELY = UINT64_C(1) << 1;
}

namespace NekoEETraceMemory
{
  constexpr std::uint64_t WIDTH_MASK = UINT64_C(0xff);
  constexpr std::uint64_t WRITE = UINT64_C(1) << 8;
  constexpr std::uint64_t SUCCEEDED = UINT64_C(1) << 9;
}

namespace NekoEETraceCOP1Interlock
{
  constexpr std::uint64_t READ = UINT64_C(1);
  constexpr std::uint64_t WRITE = UINT64_C(1) << 1;
}

namespace NekoEETraceCOP1DividerHazard
{
  constexpr std::uint64_t BRANCH_DELAY_SLOT = UINT64_C(1);
  constexpr std::uint64_t AFTER_BRANCH_DELAY_SLOT =
    UINT64_C(1) << 1;
  constexpr std::uint64_t AFTER_BRANCH_TARGET =
    UINT64_C(1) << 2;
}

namespace NekoEETraceCOP1Stage
{
  constexpr std::uint64_t R = 0;
  constexpr std::uint64_t T = 1;
  constexpr std::uint64_t X = 2;
  constexpr std::uint64_t Y = 3;
  constexpr std::uint64_t Z = 4;
  constexpr std::uint64_t S1 = 5;
  constexpr std::uint64_t S2 = 6;
  constexpr std::uint64_t NONE = UINT64_C(0xff);
  constexpr std::uint64_t FROM_MASK = UINT64_C(0xff);
  constexpr std::uint64_t TO_SHIFT = 8;
  constexpr std::uint64_t REMAINING_CYCLES_SHIFT = 16;
}

namespace NekoEETraceCOP1Result
{
  constexpr std::uint64_t DESTINATION_FPR = UINT64_C(1);
  constexpr std::uint64_t DESTINATION_ACCUMULATOR =
    UINT64_C(1) << 1;
  constexpr std::uint64_t DESTINATION_FCR31 =
    UINT64_C(1) << 2;
  constexpr std::uint64_t DESTINATION_CONDITION =
    UINT64_C(1) << 3;
  constexpr std::uint64_t DESTINATION_GPR =
    UINT64_C(1) << 4;
  constexpr std::uint64_t DESTINATION_MEMORY =
    UINT64_C(1) << 5;
  constexpr std::uint64_t DESTINATION_MASK_SHIFT = 32;
  constexpr std::uint64_t FPR_REGISTER_SHIFT = 40;
  constexpr std::uint64_t GPR_REGISTER_SHIFT = 48;
  constexpr std::uint64_t AFFECTED_FLAGS_MASK = UINT64_C(0xff);
  constexpr std::uint64_t RAISED_FLAGS_SHIFT = 8;
  constexpr std::uint64_t RAISED_STICKY_FLAGS_SHIFT = 16;
  constexpr std::uint64_t CONDITION = UINT64_C(1) << 24;
}

struct NekoTraceEvent
{
  std::uint64_t masterCycle = 0;
  NekoTraceSubsystem subsystem = NekoTraceSubsystem::VU0;
  NekoTraceEventType type = NekoTraceEventType::Progress;
  std::uint64_t value0 = 0;
  std::uint64_t value1 = 0;
  std::uint64_t value2 = 0;
  std::uint64_t value3 = 0;
};

std::uint64_t nekoFrameHash(const GSPresentation &presentation);
std::uint64_t nekoTraceHash(
  const std::vector<NekoTraceEvent> &events);

#endif
