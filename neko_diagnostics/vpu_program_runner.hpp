#ifndef VPU_PROGRAM_RUNNER_H
#define VPU_PROGRAM_RUNNER_H

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <limits>
#include <vector>

#include "vpu.hpp"

struct VPUProgramRunConfig
{
  std::vector<std::uint8_t> microProgram;
  std::uint16_t startAddress = 0;
  std::uint32_t cycleBudget = 0;
  std::size_t outputAddress = 0;
  std::size_t outputSize = 0;
  bool captureTrace = false;
  std::uint32_t traceStartCycle = 0;
  std::uint32_t traceEndCycle = std::numeric_limits<std::uint32_t>::max();
  std::ostream *traceOutput = nullptr;
};

struct VPUProgramRunResult
{
  std::uint8_t state = VPU_STATE_READY;
  std::uint16_t programCounter = 0;
  std::uint32_t elapsedCycles = 0;
  bool hasTerminationPosition = false;
  std::uint16_t terminationPosition = 0;
  std::vector<std::uint8_t> outputMemory;
  std::vector<VPUTraceEvent> traceEvents;
};

VPUProgramRunResult runVPUProgram(
  VPU *vpu,
  const VPUProgramRunConfig &config);

void writeVPUTraceEventJsonLine(
  std::ostream &output,
  const VPUTraceEvent &event);

#endif
