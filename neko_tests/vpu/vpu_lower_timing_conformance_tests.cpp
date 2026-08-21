#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"

namespace
{
  void appendWord(std::vector<uint8_t> *instructions, uint32_t word)
  {
    instructions->push_back((word >> 24) & 0xff);
    instructions->push_back((word >> 16) & 0xff);
    instructions->push_back((word >> 8) & 0xff);
    instructions->push_back(word & 0xff);
  }

  void appendInstructionPair(
    std::vector<uint8_t> *instructions,
    uint32_t upper,
    uint32_t lower = VPU_LOWER_NOP)
  {
    appendWord(instructions, upper);
    appendWord(instructions, lower);
  }

  uint32_t iadd(uint8_t id, uint8_t is, uint8_t it)
  {
    return
      VPU_IADD_ENCODING |
      (static_cast<uint32_t>(it) << 16) |
      (static_cast<uint32_t>(is) << 11) |
      (static_cast<uint32_t>(id) << 6);
  }

  uint32_t isubiu(uint8_t it, uint8_t is, uint16_t immediate)
  {
    return
      VPU_ISUBIU_ENCODING |
      (static_cast<uint32_t>((immediate >> 11) & 0xf) << 21) |
      (static_cast<uint32_t>(it) << 16) |
      (static_cast<uint32_t>(is) << 11) |
      (immediate & 0x7ff);
  }

  uint32_t ilw(
    uint8_t fieldMask,
    uint8_t it,
    uint8_t is,
    int16_t immediate)
  {
    return
      VPU_ILW_ENCODING |
      (static_cast<uint32_t>(fieldMask) << 21) |
      (static_cast<uint32_t>(it) << 16) |
      (static_cast<uint32_t>(is) << 11) |
      (static_cast<uint16_t>(immediate) & 0x7ff);
  }

  uint32_t lq(
    uint8_t fieldMask,
    uint8_t ft,
    uint8_t is,
    int16_t immediate)
  {
    return
      VPU_LQ_ENCODING |
      (static_cast<uint32_t>(fieldMask) << 21) |
      (static_cast<uint32_t>(ft) << 16) |
      (static_cast<uint32_t>(is) << 11) |
      (static_cast<uint16_t>(immediate) & 0x7ff);
  }

  uint32_t mfir(uint8_t fieldMask, uint8_t ft, uint8_t is)
  {
    return
      VPU_MFIR_ENCODING |
      (static_cast<uint32_t>(fieldMask) << 21) |
      (static_cast<uint32_t>(ft) << 16) |
      (static_cast<uint32_t>(is) << 11);
  }

  uint32_t sqi(uint8_t fieldMask, uint8_t fs, uint8_t it)
  {
    return
      VPU_SQI_ENCODING |
      (static_cast<uint32_t>(fieldMask) << 21) |
      (static_cast<uint32_t>(it) << 16) |
      (static_cast<uint32_t>(fs) << 11);
  }

  uint32_t add(
    uint32_t destinationMask,
    uint8_t ft,
    uint8_t fs,
    uint8_t fd)
  {
    return
      destinationMask |
      (static_cast<uint32_t>(ft) << VPU_FT_REG_SHIFT) |
      (static_cast<uint32_t>(fs) << VPU_FS_REG_SHIFT) |
      (static_cast<uint32_t>(fd) << VPU_FD_REG_SHIFT) |
      VPU_ADD;
  }

  uint32_t addi(
    uint32_t destinationMask,
    uint8_t fs,
    uint8_t fd)
  {
    return
      destinationMask |
      (static_cast<uint32_t>(fs) << VPU_FS_REG_SHIFT) |
      (static_cast<uint32_t>(fd) << VPU_FD_REG_SHIFT) |
      VPU_ADDi;
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

  std::vector<uint8_t> wordBytes(uint32_t value)
  {
    return {
      static_cast<uint8_t>(value & 0xff),
      static_cast<uint8_t>((value >> 8) & 0xff),
      static_cast<uint8_t>((value >> 16) & 0xff),
      static_cast<uint8_t>((value >> 24) & 0xff)
    };
  }
}

TEST_CASE("VU Lower Timing Conformance Tests")
{
  SECTION("IALU results bypass immediately but write registers at S-stage")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<VPUTraceEvent> events;
    vpu.loadIntRegister(VPU_REGISTER_VI01, 4);
    vpu.loadIntRegister(VPU_REGISTER_VI02, 5);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      iadd(
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI01,
        VPU_REGISTER_VI02));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      iadd(
        VPU_REGISTER_VI05,
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI01));
    appendInstructionPair(&instructions, VPU_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });
    vpu.startMicroMode();

    REQUIRE(vpu.tick());
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 0);
    REQUIRE(vpu.tick());
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI05) == 0);

    vpu.tick();
    vpu.tick();
    vpu.tick();
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 0);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI05) == 0);

    vpu.tick();
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 9);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI05) == 0);

    vpu.tick();
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI05) == 13);

    std::vector<VPUTraceEvent> writebacks =
      eventsOfType(events, VPUTraceEventType::PipelineWriteback);
    REQUIRE(writebacks.size() == 2);
    REQUIRE(writebacks[0].cycle == 5);
    REQUIRE(writebacks[0].instructionAddress == 0);
    REQUIRE(writebacks[0].opCode == VPU_IADD);
    REQUIRE(writebacks[0].destinationRegister == VPU_REGISTER_VI04);
    REQUIRE(writebacks[0].destinationFieldMask == FP_REGISTER_NO_FIELDS);
    REQUIRE(writebacks[1].cycle == 6);
    REQUIRE(writebacks[1].instructionAddress == 8);
    REQUIRE(writebacks[1].opCode == VPU_IADD);
    REQUIRE(writebacks[1].destinationRegister == VPU_REGISTER_VI05);
    REQUIRE(writebacks[1].destinationFieldMask == FP_REGISTER_NO_FIELDS);
  }

  SECTION("ISUBIU results bypass to the next IALU instruction")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadIntRegister(VPU_REGISTER_VI01, 0x1234);
    vpu.loadIntRegister(VPU_REGISTER_VI02, 2);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      isubiu(VPU_REGISTER_VI04, VPU_REGISTER_VI01, 0x1001));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      iadd(
        VPU_REGISTER_VI05,
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI02));
    appendInstructionPair(&instructions, VPU_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 0x0233);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI05) == 0x0235);
  }

  SECTION("A younger IALU write retires after an older ILW to the same register")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.writeDataMemory(0, wordBytes(7));
    vpu.loadIntRegister(VPU_REGISTER_VI01, 6);
    vpu.loadIntRegister(VPU_REGISTER_VI02, 7);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      ilw(
        FP_REGISTER_X_FIELD,
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI00,
        0));
    appendInstructionPair(
      &instructions,
      VPU_NOP,
      iadd(
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI01,
        VPU_REGISTER_VI02));
    appendInstructionPair(&instructions, VPU_E_BIT | VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 13);
  }

  SECTION("A younger ILW retires after an older IALU write to the same register")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.writeDataMemory(0, wordBytes(0x1234));
    vpu.loadIntRegister(VPU_REGISTER_VI01, 6);
    vpu.loadIntRegister(VPU_REGISTER_VI02, 7);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      iadd(
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI01,
        VPU_REGISTER_VI02));
    appendInstructionPair(
      &instructions,
      VPU_NOP,
      ilw(
        FP_REGISTER_X_FIELD,
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI00,
        0));
    appendInstructionPair(&instructions, VPU_E_BIT | VPU_NOP);
    appendInstructionPair(&instructions, VPU_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 0x1234);
  }

  SECTION("The newest overlapping IALU result is used for bypass")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadIntRegister(VPU_REGISTER_VI01, 1);
    vpu.loadIntRegister(VPU_REGISTER_VI02, 2);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      iadd(
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI01,
        VPU_REGISTER_VI02));
    appendInstructionPair(
      &instructions,
      VPU_NOP,
      iadd(
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI02));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      iadd(
        VPU_REGISTER_VI05,
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI01));
    appendInstructionPair(&instructions, VPU_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 5);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI05) == 6);
  }

  SECTION("A pending IALU result bypasses to MFIR")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadIntRegister(VPU_REGISTER_VI01, 4);
    vpu.loadIntRegister(VPU_REGISTER_VI02, 5);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      iadd(
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI01,
        VPU_REGISTER_VI02));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      mfir(
        FP_REGISTER_X_FIELD,
        VPU_REGISTER_VF01,
        VPU_REGISTER_VI04));
    appendInstructionPair(&instructions, VPU_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->x.bits() == 9);
  }

  SECTION("A pending IALU result bypasses to an LSU address")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.writeDataMemory(0, wordBytes(0x11111111));
    vpu.writeDataMemory(16, wordBytes(0x22222222));
    vpu.loadIntRegister(VPU_REGISTER_VI01, 1);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      iadd(
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI00,
        VPU_REGISTER_VI01));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      lq(
        FP_REGISTER_X_FIELD,
        VPU_REGISTER_VF01,
        VPU_REGISTER_VI04,
        0));
    appendInstructionPair(&instructions, VPU_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(
      vpu.fpRegisterValue(VPU_REGISTER_VF01)->x.bits() ==
      0x22222222);
  }

  SECTION("A pending IALU result bypasses to SQI address and post-increment")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadIntRegister(VPU_REGISTER_VI01, 1);
    vpu.loadIntFPRegister(VPU_REGISTER_VF01, 0x12345678, 0, 0, 0);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      iadd(
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI00,
        VPU_REGISTER_VI01));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      sqi(
        FP_REGISTER_X_FIELD,
        VPU_REGISTER_VF01,
        VPU_REGISTER_VI04));
    appendInstructionPair(&instructions, VPU_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.readDataMemory(0, 4) == wordBytes(0));
    REQUIRE(vpu.readDataMemory(16, 4) == wordBytes(0x12345678));
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 2);
  }

  SECTION("Force Break discards pending IALU writes and bypass state")
  {
    VPU vpu;
    std::vector<uint8_t> interruptedProgram;
    std::vector<uint8_t> recoveryProgram;
    vpu.loadIntRegister(VPU_REGISTER_VI01, 4);
    vpu.loadIntRegister(VPU_REGISTER_VI02, 5);

    appendInstructionPair(
      &interruptedProgram,
      VPU_NOP,
      iadd(
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI01,
        VPU_REGISTER_VI02));
    vpu.uploadMicroInstructions(interruptedProgram);
    vpu.startMicroMode();
    REQUIRE(vpu.tick());
    vpu.forceBreak();
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 0);

    appendInstructionPair(
      &recoveryProgram,
      VPU_E_BIT | VPU_NOP,
      iadd(
        VPU_REGISTER_VI05,
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI01));
    appendInstructionPair(&recoveryProgram, VPU_NOP);
    vpu.uploadMicroInstructions(recoveryProgram);
    vpu.initMicroMode();

    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI05) == 4);
  }

  SECTION("VI00 never retains an IALU bypass or writeback")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadIntRegister(VPU_REGISTER_VI01, 4);
    vpu.loadIntRegister(VPU_REGISTER_VI02, 5);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      iadd(
        VPU_REGISTER_VI00,
        VPU_REGISTER_VI01,
        VPU_REGISTER_VI02));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      iadd(
        VPU_REGISTER_VI03,
        VPU_REGISTER_VI00,
        VPU_REGISTER_VI01));
    appendInstructionPair(&instructions, VPU_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI00) == 0);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI03) == 4);
  }

  SECTION("Upper and IALU instructions sustain dual issue every cycle")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<VPUTraceEvent> events;
    vpu.loadFPRegister(VPU_REGISTER_VF20, 1, 0, 0, 0);
    vpu.loadFPRegister(VPU_REGISTER_VF21, 2, 0, 0, 0);
    vpu.loadIntRegister(VPU_REGISTER_VI13, 4);
    vpu.loadIntRegister(VPU_REGISTER_VI14, 5);

    for (uint8_t index = 1; index <= 7; index++)
    {
      uint32_t upper = add(
        VPU_DEST_X_BIT,
        VPU_REGISTER_VF20,
        VPU_REGISTER_VF21,
        index);
      if (index == 7)
      {
        upper |= VPU_E_BIT;
      }
      appendInstructionPair(
        &instructions,
        upper,
        iadd(index, VPU_REGISTER_VI13, VPU_REGISTER_VI14));
    }
    appendInstructionPair(&instructions, VPU_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });
    vpu.initMicroMode();

    std::vector<VPUTraceEvent> issues =
      eventsOfType(events, VPUTraceEventType::InstructionIssued);
    REQUIRE(issues.size() == 8);
    for (uint8_t index = 0; index < 7; index++)
    {
      REQUIRE(issues[index].cycle == index);
      REQUIRE(vpu.fpRegisterValue(index + 1)->x == 3);
      REQUIRE(vpu.intRegisterValue(index + 1) == 9);
    }
    REQUIRE(eventsOfType(events, VPUTraceEventType::PipelineStall).empty());
  }

  SECTION("ILW writes at S-stage and stalls an integer consumer until the following cycle")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<VPUTraceEvent> events;
    vpu.writeDataMemory(0, {0x34, 0x12, 0, 0});
    vpu.loadIntRegister(VPU_REGISTER_VI03, 1);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      ilw(
        FP_REGISTER_X_FIELD,
        VPU_REGISTER_VI02,
        VPU_REGISTER_VI00,
        0));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      iadd(
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI02,
        VPU_REGISTER_VI03));
    appendInstructionPair(&instructions, VPU_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });
    vpu.initMicroMode();

    std::vector<VPUTraceEvent> issues =
      eventsOfType(events, VPUTraceEventType::InstructionIssued);
    std::vector<VPUTraceEvent> stalls =
      eventsOfType(events, VPUTraceEventType::PipelineStall);
    std::vector<VPUTraceEvent> writebacks =
      eventsOfType(events, VPUTraceEventType::PipelineWriteback);

    REQUIRE(issues.size() == 3);
    REQUIRE(issues[0].cycle == 0);
    REQUIRE(issues[1].cycle == 6);
    REQUIRE(stalls.size() == 5);
    REQUIRE(stalls.front().cycle == 1);
    REQUIRE(stalls.back().cycle == 5);
    REQUIRE(writebacks[0].opCode == VPU_ILW);
    REQUIRE(writebacks[0].cycle == 5);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 0x1235);
  }

  SECTION("Force Break cancels LSU loads, stores, post-increments, and hazards")
  {
    VPU vpu;
    std::vector<uint8_t> loadProgram;
    std::vector<uint8_t> storeProgram;
    std::vector<uint8_t> recoveryProgram;
    vpu.writeDataMemory(0, wordBytes(0x12345678));
    vpu.loadIntRegister(VPU_REGISTER_VI01, 0);
    vpu.loadIntRegister(VPU_REGISTER_VI02, 7);
    vpu.loadIntFPRegister(VPU_REGISTER_VF03, 0x11223344, 0, 0, 0);

    appendInstructionPair(
      &loadProgram,
      VPU_NOP,
      ilw(
        FP_REGISTER_X_FIELD,
        VPU_REGISTER_VI02,
        VPU_REGISTER_VI00,
        0));
    vpu.uploadMicroInstructions(loadProgram);
    vpu.startMicroMode();
    vpu.tick();
    vpu.forceBreak();
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI02) == 7);

    appendInstructionPair(
      &storeProgram,
      VPU_NOP,
      sqi(
        FP_REGISTER_X_FIELD,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VI01));
    vpu.uploadMicroInstructions(storeProgram);
    vpu.startMicroMode();
    vpu.tick();
    vpu.forceBreak();
    REQUIRE(vpu.readDataMemory(0, 4) == wordBytes(0x12345678));
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI01) == 0);

    appendInstructionPair(
      &recoveryProgram,
      VPU_E_BIT | VPU_NOP,
      iadd(
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI02,
        VPU_REGISTER_VI01));
    appendInstructionPair(&recoveryProgram, VPU_NOP);
    vpu.uploadMicroInstructions(recoveryProgram);
    vpu.startMicroMode();
    REQUIRE(vpu.tick());
    vpu.run(10);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 7);
  }

  SECTION("VU0 and VU1 qword addresses wrap within their own memories")
  {
    for (VPUType type : {VPUType::VU0, VPUType::VU1})
    {
      VPU vpu(type);
      std::vector<uint8_t> instructions;
      uint16_t qwordCount = vpu.dataMemorySize() / 16;
      vpu.writeDataMemory(0, wordBytes(0x89abcdef));
      vpu.loadIntRegister(VPU_REGISTER_VI01, qwordCount);

      appendInstructionPair(
        &instructions,
        VPU_E_BIT | VPU_NOP,
        lq(
          FP_REGISTER_X_FIELD,
          VPU_REGISTER_VF02,
          VPU_REGISTER_VI01,
          0));
      appendInstructionPair(&instructions, VPU_NOP);

      vpu.uploadMicroInstructions(instructions);
      vpu.initMicroMode();

      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x.bits() == 0x89abcdef);
    }
  }

  SECTION("ILW and SQI cannot modify or hazard VI00")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<VPUTraceEvent> events;
    vpu.writeDataMemory(0, wordBytes(0x12345678));
    vpu.loadIntFPRegister(VPU_REGISTER_VF02, 0x11223344, 0, 0, 0);
    vpu.loadIntRegister(VPU_REGISTER_VI01, 3);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      ilw(
        FP_REGISTER_X_FIELD,
        VPU_REGISTER_VI00,
        VPU_REGISTER_VI00,
        0));
    appendInstructionPair(
      &instructions,
      VPU_NOP,
      sqi(
        FP_REGISTER_X_FIELD,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VI00));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      iadd(
        VPU_REGISTER_VI03,
        VPU_REGISTER_VI00,
        VPU_REGISTER_VI01));
    appendInstructionPair(&instructions, VPU_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });
    vpu.initMicroMode();

    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI00) == 0);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI03) == 3);
    REQUIRE(eventsOfType(events, VPUTraceEventType::PipelineStall).empty());
    REQUIRE(vpu.readDataMemory(0, 4) == wordBytes(0x11223344));
  }

  SECTION("LQ hazards only the destination lanes it loads")
  {
    VPU nonHazardVpu;
    VPU hazardVpu;
    std::vector<uint8_t> nonHazardProgram;
    std::vector<uint8_t> hazardProgram;
    std::vector<VPUTraceEvent> nonHazardEvents;
    std::vector<VPUTraceEvent> hazardEvents;
    nonHazardVpu.writeDataMemory(0, wordBytes(0x3f800000));
    hazardVpu.writeDataMemory(0, wordBytes(0x3f800000));
    nonHazardVpu.loadFPRegister(VPU_REGISTER_VF02, 0, 2, 0, 0);
    hazardVpu.loadFPRegister(VPU_REGISTER_VF02, 0, 2, 0, 0);
    nonHazardVpu.loadFPRegister(VPU_REGISTER_VF03, 0, 3, 0, 0);
    hazardVpu.loadFPRegister(VPU_REGISTER_VF03, 3, 0, 0, 0);

    appendInstructionPair(
      &nonHazardProgram,
      VPU_NOP,
      lq(
        FP_REGISTER_X_FIELD,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VI00,
        0));
    appendInstructionPair(
      &nonHazardProgram,
      VPU_E_BIT |
        add(
          VPU_DEST_Y_BIT,
          VPU_REGISTER_VF02,
          VPU_REGISTER_VF03,
          VPU_REGISTER_VF04));
    appendInstructionPair(&nonHazardProgram, VPU_NOP);

    appendInstructionPair(
      &hazardProgram,
      VPU_NOP,
      lq(
        FP_REGISTER_X_FIELD,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VI00,
        0));
    appendInstructionPair(
      &hazardProgram,
      VPU_E_BIT |
        add(
          VPU_DEST_X_BIT,
          VPU_REGISTER_VF02,
          VPU_REGISTER_VF03,
          VPU_REGISTER_VF04));
    appendInstructionPair(&hazardProgram, VPU_NOP);

    nonHazardVpu.uploadMicroInstructions(nonHazardProgram);
    nonHazardVpu.setTraceCallback([&nonHazardEvents](const VPUTraceEvent &event) {
      nonHazardEvents.push_back(event);
    });
    nonHazardVpu.initMicroMode();
    hazardVpu.uploadMicroInstructions(hazardProgram);
    hazardVpu.setTraceCallback([&hazardEvents](const VPUTraceEvent &event) {
      hazardEvents.push_back(event);
    });
    hazardVpu.initMicroMode();

    REQUIRE(eventsOfType(
      nonHazardEvents,
      VPUTraceEventType::PipelineStall).empty());
    REQUIRE_FALSE(eventsOfType(
      hazardEvents,
      VPUTraceEventType::PipelineStall).empty());
    REQUIRE(nonHazardVpu.fpRegisterValue(VPU_REGISTER_VF04)->y == 5);
    REQUIRE(hazardVpu.fpRegisterValue(VPU_REGISTER_VF04)->x == 4);
  }

  SECTION("SQI ignores pending writes to unselected source lanes")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<VPUTraceEvent> events;
    vpu.loadFPRegister(VPU_REGISTER_VF02, 1, 2, 0, 0);
    vpu.loadFPRegister(VPU_REGISTER_VF03, 3, 4, 0, 0);

    appendInstructionPair(
      &instructions,
      add(
        VPU_DEST_Y_BIT,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF04));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      sqi(
        FP_REGISTER_X_FIELD,
        VPU_REGISTER_VF04,
        VPU_REGISTER_VI00));
    appendInstructionPair(&instructions, VPU_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });
    vpu.initMicroMode();

    REQUIRE(eventsOfType(events, VPUTraceEventType::PipelineStall).empty());
  }

  SECTION("Independent LSU pipelines overlap and write back in issue order")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<VPUTraceEvent> events;
    vpu.writeDataMemory(0, wordBytes(0x11111111));
    vpu.writeDataMemory(16, wordBytes(0x22222222));

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      lq(
        FP_REGISTER_X_FIELD,
        VPU_REGISTER_VF01,
        VPU_REGISTER_VI00,
        0));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      lq(
        FP_REGISTER_X_FIELD,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VI00,
        1));
    appendInstructionPair(&instructions, VPU_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });
    vpu.initMicroMode();

    std::vector<VPUTraceEvent> writebacks =
      eventsOfType(events, VPUTraceEventType::PipelineWriteback);
    REQUIRE(writebacks.size() == 2);
    REQUIRE(writebacks[0].opCode == VPU_LQ);
    REQUIRE(writebacks[0].instructionAddress == 0);
    REQUIRE(writebacks[0].cycle == 5);
    REQUIRE(writebacks[1].instructionAddress == 8);
    REQUIRE(writebacks[1].cycle == 6);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF01)->x.bits() == 0x11111111);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x.bits() == 0x22222222);
  }

  SECTION("ILW rejects zero or multiple destination fields")
  {
    for (uint8_t mask : {
      static_cast<uint8_t>(FP_REGISTER_NO_FIELDS),
      static_cast<uint8_t>(FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD)})
    {
      VPU vpu;
      std::vector<uint8_t> instructions;
      appendInstructionPair(
        &instructions,
        VPU_E_BIT | VPU_NOP,
        ilw(
          mask,
          VPU_REGISTER_VI01,
          VPU_REGISTER_VI00,
          0));
      appendInstructionPair(&instructions, VPU_NOP);
      vpu.uploadMicroInstructions(instructions);

      REQUIRE_THROWS_WITH(
        vpu.initMicroMode(),
        "ILW requires exactly one destination field.");
    }
  }

  SECTION("I-bit data waits for a stalled upper instruction to issue")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadFPRegister(VPU_REGISTER_VF02, 1, 0, 0, 0);
    vpu.loadFPRegister(VPU_REGISTER_VF03, 2, 0, 0, 0);
    vpu.loadFPRegister(VPU_REGISTER_VF05, 3, 0, 0, 0);

    appendInstructionPair(
      &instructions,
      add(
        VPU_DEST_X_BIT,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF01));
    appendInstructionPair(
      &instructions,
      VPU_I_BIT |
        add(
          VPU_DEST_X_BIT,
          VPU_REGISTER_VF01,
          VPU_REGISTER_VF05,
          VPU_REGISTER_VF04),
      0x40000000);
    appendInstructionPair(
      &instructions,
      VPU_E_BIT |
        addi(
          VPU_DEST_X_BIT,
          VPU_REGISTER_VF02,
          VPU_REGISTER_VF06));
    appendInstructionPair(&instructions, VPU_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();
    vpu.tick();
    vpu.tick();
    vpu.tick();

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF06)->x == 0);

    vpu.run(30);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF06)->x == 3);
  }
}
