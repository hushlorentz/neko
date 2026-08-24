#include "clock_scheduler.hpp"
#include "vpu_program_runner.hpp"

#include <ostream>
#include <stdexcept>

namespace
{
  const char *traceEventTypeName(VPUTraceEventType type)
  {
    switch (type)
    {
      case VPUTraceEventType::InstructionIssued:
        return "instruction_issued";
      case VPUTraceEventType::PipelineStall:
        return "pipeline_stall";
      case VPUTraceEventType::PipelineWriteback:
        return "pipeline_writeback";
      case VPUTraceEventType::ForceBreak:
        return "force_break";
    }

    throw std::invalid_argument("Unknown VU trace event type.");
  }

  class TraceCallbackReset
  {
    public:
      TraceCallbackReset(VPU *vpu, bool enabled) :
        vpu(vpu),
        enabled(enabled)
      {
      }

      ~TraceCallbackReset()
      {
        if (enabled)
        {
          vpu->setTraceCallback(VPUTraceCallback());
        }
      }

    private:
      VPU *vpu;
      bool enabled;
  };
}

VPUProgramRunResult runVPUProgram(
  VPU *vpu,
  const VPUProgramRunConfig &config)
{
  if (vpu == nullptr)
  {
    throw std::invalid_argument("VPU program runner requires a VPU.");
  }
  if (config.traceStartCycle > config.traceEndCycle)
  {
    throw std::invalid_argument(
      "VU trace start cycle cannot exceed its end cycle.");
  }

  VPUProgramRunResult result;
  bool traceEnabled = config.captureTrace || config.traceOutput != nullptr;
  TraceCallbackReset traceCallbackReset(vpu, traceEnabled);
  if (traceEnabled)
  {
    vpu->setTraceCallback(
      [&config, &result](const VPUTraceEvent &event) {
        if (event.cycle < config.traceStartCycle ||
            event.cycle > config.traceEndCycle)
        {
          return;
        }
        if (config.captureTrace)
        {
          result.traceEvents.push_back(event);
        }
        if (config.traceOutput != nullptr)
        {
          writeVPUTraceEventJsonLine(*config.traceOutput, event);
        }
      });
  }

  vpu->uploadMicroInstructions(config.microProgram);
  vpu->resetCycles();
  vpu->startMicroMode(config.startAddress);
  ClockScheduler().run(*vpu, config.cycleBudget);

  result.state = vpu->getState();
  result.programCounter = vpu->programCounter();
  result.elapsedCycles = vpu->elapsedCycles();
  result.hasTerminationPosition = vpu->hasTerminationPosition();
  if (result.hasTerminationPosition)
  {
    result.terminationPosition = vpu->terminationPosition();
  }
  result.outputMemory =
    vpu->readDataMemory(config.outputAddress, config.outputSize);
  return result;
}

void writeVPUTraceEventJsonLine(
  std::ostream &output,
  const VPUTraceEvent &event)
{
  output
    << "{\"type\":\"" << traceEventTypeName(event.type)
    << "\",\"cycle\":" << event.cycle
    << ",\"instruction_address\":" << event.instructionAddress
    << ",\"upper_instruction\":" << event.upperInstruction
    << ",\"lower_instruction\":" << event.lowerInstruction
    << ",\"opcode\":" << event.opCode
    << ",\"destination_register\":"
    << static_cast<unsigned int>(event.destinationRegister)
    << ",\"destination_field_mask\":"
    << static_cast<unsigned int>(event.destinationFieldMask)
    << "}\n";
}
