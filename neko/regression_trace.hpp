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
  InterruptDelivered
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
