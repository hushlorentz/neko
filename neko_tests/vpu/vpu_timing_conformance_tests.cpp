#include <vector>

#include "catch.hpp"
#include "vpu.hpp"
#include "vpu_flags.hpp"
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

  uint32_t addInstruction(
    uint32_t fieldMask,
    uint8_t ft,
    uint8_t fs,
    uint8_t fd,
    uint32_t flags = 0)
  {
    return
      flags |
      fieldMask |
      (ft << VPU_FT_REG_SHIFT) |
      (fs << VPU_FS_REG_SHIFT) |
      (fd << VPU_FD_REG_SHIFT) |
      VPU_ADD;
  }

  uint32_t upperInstruction(
    uint16_t opCode,
    uint32_t fieldMask,
    uint8_t ft,
    uint8_t fs,
    uint8_t fd,
    uint32_t flags = 0)
  {
    return
      flags |
      fieldMask |
      (ft << VPU_FT_REG_SHIFT) |
      (fs << VPU_FS_REG_SHIFT) |
      (fd << VPU_FD_REG_SHIFT) |
      opCode;
  }

  std::vector<VPUTraceEvent> eventsOfType(
    const std::vector<VPUTraceEvent> &events,
    VPUTraceEventType type)
  {
    std::vector<VPUTraceEvent> matches;
    for (const VPUTraceEvent &event : events)
    {
      if (event.type == type)
      {
        matches.push_back(event);
      }
    }
    return matches;
  }
}

TEST_CASE("VU Manual Timing Conformance Tests")
{
  SECTION("Independent FMAC instructions issue every cycle and write back in order")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<VPUTraceEvent> events;
    appendInstructionPair(
      &instructions,
      addInstruction(
        VPU_DEST_ALL_FIELDS,
        VPU_REGISTER_VF01,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF10));
    appendInstructionPair(
      &instructions,
      addInstruction(
        VPU_DEST_ALL_FIELDS,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF04,
        VPU_REGISTER_VF11));
    appendInstructionPair(
      &instructions,
      addInstruction(
        VPU_DEST_ALL_FIELDS,
        VPU_REGISTER_VF05,
        VPU_REGISTER_VF06,
        VPU_REGISTER_VF12,
        VPU_E_BIT));
    appendInstructionPair(&instructions, VPU_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });

    vpu.initMicroMode();

    std::vector<VPUTraceEvent> issues =
      eventsOfType(events, VPUTraceEventType::InstructionIssued);
    std::vector<VPUTraceEvent> writebacks =
      eventsOfType(events, VPUTraceEventType::PipelineWriteback);
    std::vector<VPUTraceEvent> stalls =
      eventsOfType(events, VPUTraceEventType::PipelineStall);

    REQUIRE(issues.size() == 4);
    REQUIRE(issues[0].cycle == 0);
    REQUIRE(issues[1].cycle == 1);
    REQUIRE(issues[2].cycle == 2);
    REQUIRE(issues[3].cycle == 3);
    REQUIRE(stalls.empty());
    REQUIRE(writebacks.size() == 3);
    REQUIRE(writebacks[0].instructionAddress == 0);
    REQUIRE(writebacks[0].cycle == 5);
    REQUIRE(writebacks[1].instructionAddress == 8);
    REQUIRE(writebacks[1].cycle == 6);
    REQUIRE(writebacks[2].instructionAddress == 16);
    REQUIRE(writebacks[2].cycle == 7);
  }

  SECTION("FMAC operands are sampled when the pipeline reaches T stage")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadFPRegister(VPU_REGISTER_VF02, 1, 2, 3, 4);
    vpu.loadFPRegister(VPU_REGISTER_VF03, 10, 20, 30, 40);
    appendInstructionPair(
      &instructions,
      addInstruction(
        VPU_DEST_ALL_FIELDS,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF01));
    appendInstructionPair(&instructions, VPU_E_BIT | VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();

    REQUIRE(vpu.tick());
    vpu.loadFPRegister(VPU_REGISTER_VF02, 100, 200, 300, 400);
    REQUIRE(vpu.tick());
    vpu.run(20);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->x == 110);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->y == 220);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->z == 330);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->w == 440);
  }

  SECTION("MADD forwards a preceding in-flight accumulator result")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadAccumulator(100, 100, 100, 100);
    vpu.loadFPRegister(VPU_REGISTER_VF02, 2, 2, 2, 2);
    vpu.loadFPRegister(VPU_REGISTER_VF03, 3, 3, 3, 3);
    vpu.loadFPRegister(VPU_REGISTER_VF04, 4, 4, 4, 4);
    vpu.loadFPRegister(VPU_REGISTER_VF05, 5, 5, 5, 5);
    appendInstructionPair(
      &instructions,
      upperInstruction(
        VPU_MULA,
        VPU_DEST_ALL_FIELDS,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF00));
    appendInstructionPair(
      &instructions,
      upperInstruction(
        VPU_MADD,
        VPU_DEST_ALL_FIELDS,
        VPU_REGISTER_VF04,
        VPU_REGISTER_VF05,
        VPU_REGISTER_VF01));
    appendInstructionPair(&instructions, VPU_E_BIT | VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    vpu.uploadMicroInstructions(instructions);

    vpu.initMicroMode();

    REQUIRE(vpu.accumulator.x == 6);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->x == 26);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->y == 26);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->z == 26);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->w == 26);
  }

  SECTION("MSUB forwards a preceding in-flight accumulator result")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadAccumulator(100, 100, 100, 100);
    vpu.loadFPRegister(VPU_REGISTER_VF02, 2, 2, 2, 2);
    vpu.loadFPRegister(VPU_REGISTER_VF03, 3, 3, 3, 3);
    vpu.loadFPRegister(VPU_REGISTER_VF04, 4, 4, 4, 4);
    vpu.loadFPRegister(VPU_REGISTER_VF05, 5, 5, 5, 5);
    appendInstructionPair(
      &instructions,
      upperInstruction(
        VPU_MULA,
        VPU_DEST_ALL_FIELDS,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF00));
    appendInstructionPair(
      &instructions,
      upperInstruction(
        VPU_MSUB,
        VPU_DEST_ALL_FIELDS,
        VPU_REGISTER_VF04,
        VPU_REGISTER_VF05,
        VPU_REGISTER_VF01));
    appendInstructionPair(&instructions, VPU_E_BIT | VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    vpu.uploadMicroInstructions(instructions);

    vpu.initMicroMode();

    REQUIRE(vpu.accumulator.x == 6);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->x == -14);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->y == -14);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->z == -14);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->w == -14);
  }

  SECTION("OPMSUB forwards a preceding in-flight accumulator result")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadAccumulator(100, 100, 100, 100);
    vpu.loadFPRegister(VPU_REGISTER_VF02, 2, 2, 2, 2);
    vpu.loadFPRegister(VPU_REGISTER_VF03, 3, 3, 3, 3);
    vpu.loadFPRegister(VPU_REGISTER_VF04, 1, 2, 3, 4);
    vpu.loadFPRegister(VPU_REGISTER_VF05, 5, 6, 7, 8);
    appendInstructionPair(
      &instructions,
      upperInstruction(
        VPU_MULA,
        VPU_DEST_ALL_FIELDS,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF00));
    appendInstructionPair(
      &instructions,
      upperInstruction(
        VPU_OPMSUB,
        VPU_DEST_XYZ_FIELDS,
        VPU_REGISTER_VF05,
        VPU_REGISTER_VF04,
        VPU_REGISTER_VF01));
    appendInstructionPair(&instructions, VPU_E_BIT | VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    vpu.uploadMicroInstructions(instructions);

    vpu.initMicroMode();

    REQUIRE(vpu.accumulator.x == 6);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->x == -8);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->y == -9);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->z == 0);
  }

  SECTION("Accumulator consumers retain their pipeline-local T-stage input")
  {
    struct AccumulatorOperation
    {
      uint16_t opCode;
      double expectedX;
      double expectedY;
      double expectedZ;
      double expectedW;
    };
    const AccumulatorOperation operations[] = {
      {VPU_MADD, 105, 112, 121, 132},
      {VPU_MSUB, 95, 88, 79, 68},
      {VPU_OPMSUB, 86, 85, 94, 0}
    };

    for (const AccumulatorOperation &operation : operations)
    {
      VPU vpu;
      std::vector<uint8_t> instructions;
      vpu.loadAccumulator(100, 100, 100, 100);
      vpu.loadFPRegister(VPU_REGISTER_VF04, 1, 2, 3, 4);
      vpu.loadFPRegister(VPU_REGISTER_VF05, 5, 6, 7, 8);
      appendInstructionPair(
        &instructions,
        upperInstruction(
          operation.opCode,
          operation.opCode == VPU_OPMSUB ?
            VPU_DEST_XYZ_FIELDS :
            VPU_DEST_ALL_FIELDS,
          VPU_REGISTER_VF05,
          VPU_REGISTER_VF04,
          VPU_REGISTER_VF01,
          VPU_E_BIT));
      appendInstructionPair(&instructions, VPU_NOP);
      vpu.uploadMicroInstructions(instructions);
      vpu.startMicroMode();

      REQUIRE(vpu.tick());
      REQUIRE(vpu.tick());
      vpu.loadAccumulator(200, 200, 200, 200);
      vpu.run(20);

      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->x ==
              operation.expectedX);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->y ==
              operation.expectedY);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->z ==
              operation.expectedZ);
      if (operation.opCode != VPU_OPMSUB)
      {
        REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->w ==
                operation.expectedW);
      }
    }
  }

  SECTION("A dependent FMAC instruction waits through producer S-stage writeback")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<VPUTraceEvent> events;
    vpu.loadFPRegister(VPU_REGISTER_VF02, 1, 2, 3, 4);
    vpu.loadFPRegister(VPU_REGISTER_VF03, 10, 20, 30, 40);
    vpu.loadFPRegister(VPU_REGISTER_VF05, 100, 200, 300, 400);
    appendInstructionPair(
      &instructions,
      addInstruction(
        VPU_DEST_ALL_FIELDS,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF01));
    appendInstructionPair(
      &instructions,
      addInstruction(
        VPU_DEST_ALL_FIELDS,
        VPU_REGISTER_VF01,
        VPU_REGISTER_VF05,
        VPU_REGISTER_VF04));
    appendInstructionPair(&instructions, VPU_E_BIT | VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });

    vpu.initMicroMode();

    std::vector<VPUTraceEvent> issues =
      eventsOfType(events, VPUTraceEventType::InstructionIssued);
    std::vector<VPUTraceEvent> writebacks =
      eventsOfType(events, VPUTraceEventType::PipelineWriteback);
    std::vector<VPUTraceEvent> stalls =
      eventsOfType(events, VPUTraceEventType::PipelineStall);

    REQUIRE(issues[0].cycle == 0);
    REQUIRE(issues[1].cycle == 1);
    REQUIRE(stalls.size() == 3);
    REQUIRE(stalls.front().cycle == 2);
    REQUIRE(stalls.back().cycle == 4);
    REQUIRE(writebacks[0].instructionAddress == 0);
    REQUIRE(writebacks[0].cycle == 5);
    REQUIRE(issues[2].instructionAddress == 16);
    REQUIRE(issues[2].cycle == 5);
    REQUIRE(writebacks[1].instructionAddress == 8);
    REQUIRE(writebacks[1].cycle == 9);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->x == 111);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->y == 222);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->z == 333);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->w == 444);
  }

  SECTION("Manual FMAC spacing reaches producer S without a stall")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<VPUTraceEvent> events;
    vpu.loadFPRegister(VPU_REGISTER_VF02, 1, 2, 3, 4);
    vpu.loadFPRegister(VPU_REGISTER_VF03, 10, 20, 30, 40);
    vpu.loadFPRegister(VPU_REGISTER_VF04, 100, 200, 300, 400);
    appendInstructionPair(
      &instructions,
      addInstruction(
        VPU_DEST_ALL_FIELDS,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF01));
    appendInstructionPair(
      &instructions,
      addInstruction(
        VPU_DEST_ALL_FIELDS,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF10));
    appendInstructionPair(
      &instructions,
      addInstruction(
        VPU_DEST_ALL_FIELDS,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF11));
    appendInstructionPair(
      &instructions,
      addInstruction(
        VPU_DEST_ALL_FIELDS,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF12));
    appendInstructionPair(
      &instructions,
      addInstruction(
        VPU_DEST_ALL_FIELDS,
        VPU_REGISTER_VF01,
        VPU_REGISTER_VF04,
        VPU_REGISTER_VF05,
        VPU_E_BIT));
    appendInstructionPair(&instructions, VPU_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });

    vpu.initMicroMode();

    std::vector<VPUTraceEvent> writebacks =
      eventsOfType(events, VPUTraceEventType::PipelineWriteback);
    REQUIRE(eventsOfType(events, VPUTraceEventType::PipelineStall).empty());
    REQUIRE(writebacks.back().instructionAddress == 32);
    REQUIRE(writebacks.back().cycle == 9);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF05)->x == 111);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF05)->y == 222);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF05)->z == 333);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF05)->w == 444);
  }

  SECTION("Different fields of the same register do not create a hazard")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<VPUTraceEvent> events;
    appendInstructionPair(
      &instructions,
      addInstruction(
        VPU_DEST_X_BIT,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF01));
    appendInstructionPair(
      &instructions,
      addInstruction(
        VPU_DEST_Y_BIT,
        VPU_REGISTER_VF01,
        VPU_REGISTER_VF04,
        VPU_REGISTER_VF05,
        VPU_E_BIT));
    appendInstructionPair(&instructions, VPU_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });

    vpu.initMicroMode();

    std::vector<VPUTraceEvent> issues =
      eventsOfType(events, VPUTraceEventType::InstructionIssued);
    REQUIRE(eventsOfType(events, VPUTraceEventType::PipelineStall).empty());
    REQUIRE(issues[0].cycle == 0);
    REQUIRE(issues[1].cycle == 1);
    REQUIRE(issues[2].cycle == 2);
  }

  SECTION("VF00 never creates a floating-point register hazard")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<VPUTraceEvent> events;
    appendInstructionPair(
      &instructions,
      addInstruction(
        VPU_DEST_X_BIT,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF00));
    appendInstructionPair(
      &instructions,
      addInstruction(
        VPU_DEST_X_BIT,
        VPU_REGISTER_VF00,
        VPU_REGISTER_VF04,
        VPU_REGISTER_VF05,
        VPU_E_BIT));
    appendInstructionPair(&instructions, VPU_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });

    vpu.initMicroMode();

    REQUIRE(eventsOfType(events, VPUTraceEventType::PipelineStall).empty());
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF00)->x == 0);
  }

  SECTION("MAC and status flags become visible at S-stage writeback")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadFPRegister(VPU_REGISTER_VF02, 1, 0, 0, 0);
    vpu.loadFPRegister(VPU_REGISTER_VF03, -1, 0, 0, 0);
    appendInstructionPair(
      &instructions,
      addInstruction(
        VPU_DEST_X_BIT,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF01));
    appendInstructionPair(&instructions, VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    appendInstructionPair(&instructions, VPU_E_BIT | VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();

    REQUIRE(vpu.run(5) == 5);
    REQUIRE_FALSE(vpu.hasMACFlag(VPU_FLAG_ZX));
    REQUIRE_FALSE(vpu.hasStatusFlag(VPU_FLAG_Z));

    REQUIRE(vpu.tick());
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZX));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_Z));
  }

  SECTION("ACC becomes architecturally visible at S while forwarding from T")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadAccumulator(100, 100, 100, 100);
    vpu.loadFPRegister(VPU_REGISTER_VF02, 2, 0, 0, 0);
    vpu.loadFPRegister(VPU_REGISTER_VF03, 3, 0, 0, 0);
    appendInstructionPair(
      &instructions,
      upperInstruction(
        VPU_MULA,
        VPU_DEST_X_BIT,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF00));
    appendInstructionPair(&instructions, VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    appendInstructionPair(&instructions, VPU_E_BIT | VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();

    REQUIRE(vpu.run(5) == 5);
    REQUIRE(vpu.accumulator.x == 100);

    REQUIRE(vpu.tick());
    REQUIRE(vpu.accumulator.x == 6);
  }

  SECTION("Clipping flags become visible at FMAC S-stage writeback")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadFPRegister(VPU_REGISTER_VF02, 2, 0, 0, 0);
    vpu.loadFPRegister(VPU_REGISTER_VF03, 0, 0, 0, 1);
    appendInstructionPair(
      &instructions,
      upperInstruction(
        VPU_CLIP,
        VPU_DEST_XYZ_FIELDS,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF00));
    appendInstructionPair(&instructions, VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    appendInstructionPair(&instructions, VPU_E_BIT | VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();

    REQUIRE(vpu.run(5) == 5);
    REQUIRE(vpu.clippingFlags == 0);

    REQUIRE(vpu.tick());
    REQUIRE(vpu.clippingFlags == VPU_CLIP_FLAG_POS_X);
  }

  SECTION("E delay-slot execution drains every active FMAC pipeline")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<VPUTraceEvent> events;
    vpu.loadFPRegister(VPU_REGISTER_VF01, 1, 1, 1, 1);
    vpu.loadFPRegister(VPU_REGISTER_VF02, 2, 2, 2, 2);
    vpu.loadFPRegister(VPU_REGISTER_VF03, 3, 3, 3, 3);
    vpu.loadFPRegister(VPU_REGISTER_VF04, 4, 4, 4, 4);
    vpu.loadFPRegister(VPU_REGISTER_VF05, 5, 5, 5, 5);
    vpu.loadFPRegister(VPU_REGISTER_VF06, 6, 6, 6, 6);
    appendInstructionPair(
      &instructions,
      addInstruction(
        VPU_DEST_ALL_FIELDS,
        VPU_REGISTER_VF01,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF10));
    appendInstructionPair(
      &instructions,
      addInstruction(
        VPU_DEST_ALL_FIELDS,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF04,
        VPU_REGISTER_VF11,
        VPU_E_BIT));
    appendInstructionPair(
      &instructions,
      addInstruction(
        VPU_DEST_ALL_FIELDS,
        VPU_REGISTER_VF05,
        VPU_REGISTER_VF06,
        VPU_REGISTER_VF12));
    vpu.uploadMicroInstructions(instructions);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });

    vpu.initMicroMode();

    std::vector<VPUTraceEvent> writebacks =
      eventsOfType(events, VPUTraceEventType::PipelineWriteback);
    REQUIRE(writebacks.size() == 3);
    REQUIRE(writebacks[0].cycle == 5);
    REQUIRE(writebacks[1].cycle == 6);
    REQUIRE(writebacks[2].cycle == 7);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF10)->x == 3);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF11)->x == 7);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF12)->x == 11);
    REQUIRE(vpu.terminationPosition() == 3);
    REQUIRE(vpu.getState() == VPU_STATE_READY);
  }
}
