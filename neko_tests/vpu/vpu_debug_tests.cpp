#include <vector>

#include "catch.hpp"
#include "vpu.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"

namespace
{
  void appendWord(std::vector<uint8_t> *instructions, uint32_t word)
  {
    instructions->push_back(word & 0xff);
    instructions->push_back((word >> 8) & 0xff);
    instructions->push_back((word >> 16) & 0xff);
    instructions->push_back((word >> 24) & 0xff);
  }

  void appendInstructionPair(
    std::vector<uint8_t> *instructions,
    uint32_t upper,
    uint32_t lower = VPU_LOWER_NOP)
  {
    appendWord(instructions, lower);
    appendWord(instructions, upper);
  }

  uint32_t addInstruction(uint8_t ft, uint8_t fs, uint8_t fd)
  {
    return
      VPU_DEST_ALL_FIELDS |
      (ft << VPU_FT_REG_SHIFT) |
      (fs << VPU_FS_REG_SHIFT) |
      (fd << VPU_FD_REG_SHIFT) |
      VPU_ADD;
  }
}

TEST_CASE("VPU Debug Execution Tests")
{
  SECTION("A tick advances exactly one VU cycle")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    appendInstructionPair(&instructions, VPU_NOP);
    appendInstructionPair(&instructions, VPU_E_BIT | VPU_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();

    REQUIRE(vpu.programCounter() == 0);
    REQUIRE(vpu.tick());
    REQUIRE(vpu.programCounter() == 8);
    REQUIRE(vpu.elapsedCycles() == 1);
    REQUIRE(vpu.getState() == VPU_STATE_RUN);
  }

  SECTION("A bounded run pauses a still-running program at its cycle budget")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    appendInstructionPair(&instructions, VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();

    REQUIRE(vpu.run(3) == 3);
    REQUIRE(vpu.elapsedCycles() == 3);
    REQUIRE(vpu.programCounter() == 24);
    REQUIRE(vpu.getState() == VPU_STATE_RUN);
    REQUIRE_FALSE(vpu.hasTerminationPosition());
  }

  SECTION("A paused run preserves the previous termination position")
  {
    VPU vpu;
    std::vector<uint8_t> terminatingProgram;
    std::vector<uint8_t> runningProgram;
    appendInstructionPair(&terminatingProgram, VPU_E_BIT | VPU_NOP);
    appendInstructionPair(&terminatingProgram, VPU_NOP);
    appendInstructionPair(&runningProgram, VPU_NOP);
    appendInstructionPair(&runningProgram, VPU_NOP);
    appendInstructionPair(&runningProgram, VPU_NOP);
    vpu.uploadMicroInstructions(terminatingProgram);
    vpu.initMicroMode();
    REQUIRE(vpu.terminationPosition() == 2);

    vpu.uploadMicroInstructions(runningProgram);
    vpu.startMicroMode();
    REQUIRE(vpu.run(2) == 2);

    REQUIRE(vpu.getState() == VPU_STATE_RUN);
    REQUIRE(vpu.terminationPosition() == 2);
  }

  SECTION("Instruction stepping waits through pipeline stalls")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    appendInstructionPair(
      &instructions,
      addInstruction(VPU_REGISTER_VF02, VPU_REGISTER_VF03, VPU_REGISTER_VF01));
    appendInstructionPair(
      &instructions,
      addInstruction(VPU_REGISTER_VF01, VPU_REGISTER_VF03, VPU_REGISTER_VF04));
    appendInstructionPair(&instructions, VPU_E_BIT | VPU_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();

    REQUIRE(vpu.stepInstruction());
    REQUIRE(vpu.programCounter() == 8);
    REQUIRE(vpu.stepInstruction());
    REQUIRE(vpu.programCounter() == 16);
    REQUIRE(vpu.stepInstruction());
    REQUIRE(vpu.programCounter() == 24);
    REQUIRE(vpu.elapsedCycles() > 3);
  }

  SECTION("Execution can start at an aligned nonzero address")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    appendInstructionPair(&instructions, 0x30);
    appendInstructionPair(&instructions, VPU_E_BIT | VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    vpu.uploadMicroInstructions(instructions);

    vpu.startMicroMode(8);

    REQUIRE(vpu.programCounter() == 8);
    REQUIRE(vpu.run(10) < 10);
    REQUIRE(vpu.getState() == VPU_STATE_READY);
    REQUIRE(vpu.terminationPosition() == 3);
  }

  SECTION("Invalid execution start addresses are rejected")
  {
    VPU vpu;

    REQUIRE_THROWS_WITH(
      vpu.startMicroMode(1),
      "VU start address must be 8-byte aligned.");
    REQUIRE_THROWS_WITH(
      vpu.startMicroMode(static_cast<uint16_t>(vpu.microMemorySize())),
      "VU start address is outside micro memory.");
  }

  SECTION("Trace callbacks report issue, stall, and writeback events")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<VPUTraceEvent> events;
    appendInstructionPair(
      &instructions,
      addInstruction(VPU_REGISTER_VF02, VPU_REGISTER_VF03, VPU_REGISTER_VF01));
    appendInstructionPair(
      &instructions,
      addInstruction(VPU_REGISTER_VF01, VPU_REGISTER_VF03, VPU_REGISTER_VF04));
    appendInstructionPair(&instructions, VPU_E_BIT | VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });

    vpu.initMicroMode();

    bool sawIssueAtZero = false;
    bool sawStall = false;
    bool sawWritebackAtZero = false;
    for (const VPUTraceEvent &event : events)
    {
      sawIssueAtZero =
        sawIssueAtZero ||
        (event.type == VPUTraceEventType::InstructionIssued &&
         event.instructionAddress == 0);
      sawStall = sawStall || event.type == VPUTraceEventType::PipelineStall;
      sawWritebackAtZero =
        sawWritebackAtZero ||
        (event.type == VPUTraceEventType::PipelineWriteback &&
         event.instructionAddress == 0 &&
         event.opCode == VPU_ADD &&
         event.destinationRegister == VPU_REGISTER_VF01 &&
         event.destinationFieldMask == FP_REGISTER_ALL_FIELDS);
    }

    REQUIRE(sawIssueAtZero);
    REQUIRE(sawStall);
    REQUIRE(sawWritebackAtZero);
  }

  SECTION("Restarting after an execution error discards queued pipeline work")
  {
    VPU vpu;
    std::vector<uint8_t> invalidProgram;
    std::vector<uint8_t> terminatingProgram;
    std::vector<VPUTraceEvent> events;
    appendInstructionPair(
      &invalidProgram,
      addInstruction(VPU_REGISTER_VF02, VPU_REGISTER_VF03, VPU_REGISTER_VF01),
      0x04000000);
    appendInstructionPair(&terminatingProgram, VPU_E_BIT | VPU_NOP);
    appendInstructionPair(&terminatingProgram, VPU_NOP);
    vpu.uploadMicroInstructions(invalidProgram);
    vpu.startMicroMode();

    REQUIRE_THROWS_WITH(vpu.tick(), "Unsupported VU lower instruction.");
    REQUIRE(vpu.getState() == VPU_STATE_STOP);

    vpu.uploadMicroInstructions(terminatingProgram);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });
    vpu.startMicroMode();
    vpu.run(10);

    for (const VPUTraceEvent &event : events)
    {
      REQUIRE(event.type != VPUTraceEventType::PipelineWriteback);
    }
    REQUIRE(vpu.getState() == VPU_STATE_READY);
  }

  SECTION("Force Break cancels in-flight pipelines and emits a trace event")
  {
    VPU vpu;
    std::vector<uint8_t> runningProgram;
    std::vector<uint8_t> terminatingProgram;
    std::vector<VPUTraceEvent> events;
    appendInstructionPair(
      &runningProgram,
      addInstruction(VPU_REGISTER_VF02, VPU_REGISTER_VF03, VPU_REGISTER_VF01));
    appendInstructionPair(&runningProgram, VPU_NOP);
    appendInstructionPair(&terminatingProgram, VPU_E_BIT | VPU_NOP);
    appendInstructionPair(&terminatingProgram, VPU_NOP);
    vpu.loadFPRegister(VPU_REGISTER_VF01, 100, 100, 100, 100);
    vpu.loadFPRegister(VPU_REGISTER_VF02, 1, 2, 3, 4);
    vpu.loadFPRegister(VPU_REGISTER_VF03, 10, 20, 30, 40);
    vpu.uploadMicroInstructions(runningProgram);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });
    vpu.startMicroMode();
    vpu.tick();

    vpu.forceBreak();

    REQUIRE(vpu.getState() == VPU_STATE_STOP);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->x == 100);
    REQUIRE_FALSE(vpu.hasTerminationPosition());

    vpu.uploadMicroInstructions(terminatingProgram);
    vpu.startMicroMode();
    vpu.run(10);

    bool sawForceBreak = false;
    bool sawCancelledWriteback = false;
    for (const VPUTraceEvent &event : events)
    {
      sawForceBreak = sawForceBreak || event.type == VPUTraceEventType::ForceBreak;
      sawCancelledWriteback =
        sawCancelledWriteback ||
        (event.type == VPUTraceEventType::PipelineWriteback &&
         event.opCode == VPU_ADD);
    }

    REQUIRE(sawForceBreak);
    REQUIRE_FALSE(sawCancelledWriteback);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->x == 100);
  }

  SECTION("Force Break invalidates an existing TPC")
  {
    VPU vpu;
    std::vector<uint8_t> terminatingProgram;
    std::vector<uint8_t> runningProgram;
    appendInstructionPair(&terminatingProgram, VPU_E_BIT | VPU_NOP);
    appendInstructionPair(&terminatingProgram, VPU_NOP);
    appendInstructionPair(&runningProgram, VPU_NOP);
    appendInstructionPair(&runningProgram, VPU_NOP);
    vpu.uploadMicroInstructions(terminatingProgram);
    vpu.initMicroMode();
    REQUIRE(vpu.terminationPosition() == 2);

    vpu.uploadMicroInstructions(runningProgram);
    vpu.startMicroMode();
    vpu.tick();
    vpu.forceBreak();

    REQUIRE(vpu.getState() == VPU_STATE_STOP);
    REQUIRE_FALSE(vpu.hasTerminationPosition());
    REQUIRE_THROWS_WITH(
      vpu.terminationPosition(),
      "TPC is indeterminate before termination.");
  }
}
