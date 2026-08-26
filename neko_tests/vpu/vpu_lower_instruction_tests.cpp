#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu.hpp"
#include "vpu_field_mask.hpp"
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
    uint32_t lower)
  {
    appendWord(instructions, lower);
    appendWord(instructions, upper);
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

  uint32_t mfir(uint8_t fieldMask, uint8_t ft, uint8_t is)
  {
    return
      VPU_MFIR_ENCODING |
      (static_cast<uint32_t>(vpuFieldMaskToEncoding(fieldMask)) << 21) |
      (static_cast<uint32_t>(ft) << 16) |
      (static_cast<uint32_t>(is) << 11);
  }

  uint32_t ilw(
    uint8_t fieldMask,
    uint8_t it,
    uint8_t is,
    int16_t immediate)
  {
    return
      VPU_ILW_ENCODING |
      (static_cast<uint32_t>(vpuFieldMaskToEncoding(fieldMask)) << 21) |
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
      (static_cast<uint32_t>(vpuFieldMaskToEncoding(fieldMask)) << 21) |
      (static_cast<uint32_t>(ft) << 16) |
      (static_cast<uint32_t>(is) << 11) |
      (static_cast<uint16_t>(immediate) & 0x7ff);
  }

  uint32_t sqi(uint8_t fieldMask, uint8_t fs, uint8_t it)
  {
    return
      VPU_SQI_ENCODING |
      (static_cast<uint32_t>(vpuFieldMaskToEncoding(fieldMask)) << 21) |
      (static_cast<uint32_t>(it) << 16) |
      (static_cast<uint32_t>(fs) << 11);
  }

  uint32_t fdiv(
    uint32_t encoding,
    uint8_t fs,
    uint8_t fsf,
    uint8_t ft,
    uint8_t ftf)
  {
    return
      encoding |
      (static_cast<uint32_t>(ftf) << 23) |
      (static_cast<uint32_t>(fsf) << 21) |
      (static_cast<uint32_t>(ft) << 16) |
      (static_cast<uint32_t>(fs) << 11);
  }

  uint32_t sqrt(uint8_t ft, uint8_t ftf)
  {
    return
      VPU_SQRT_ENCODING |
      (static_cast<uint32_t>(ftf) << 23) |
      (static_cast<uint32_t>(ft) << 16);
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

  uint32_t itof0(uint32_t destinationMask, uint8_t ft, uint8_t fs)
  {
    return
      destinationMask |
      (static_cast<uint32_t>(ft) << VPU_FT_REG_SHIFT) |
      (static_cast<uint32_t>(fs) << VPU_FS_REG_SHIFT) |
      VPU_ITOF0;
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
}

TEST_CASE("VU Lower Instruction Tests")
{
  SECTION("DIV and RSQRT decode both selected source fields")
  {
    for (uint32_t encoding : {VPU_DIV_ENCODING, VPU_RSQRT_ENCODING})
    {
      LowerInstruction instruction =
        decodeLowerInstruction(fdiv(encoding, 3, 1, 7, 3));

      REQUIRE(instruction.unit == LowerExecutionUnit::FDIV);
      REQUIRE(instruction.opCode == (encoding & VPU_TYPE3_MASK));
      REQUIRE(instruction.sourceRegister1 == VPU_REGISTER_VF03);
      REQUIRE(instruction.sourceRegister2 == VPU_REGISTER_VF07);
      REQUIRE(instruction.sourceFieldMask1 == FP_REGISTER_Y_FIELD);
      REQUIRE(instruction.sourceFieldMask2 == FP_REGISTER_W_FIELD);
    }
  }

  SECTION("SQRT decodes its selected ft field")
  {
    LowerInstruction instruction =
      decodeLowerInstruction(sqrt(VPU_REGISTER_VF05, 2));

    REQUIRE(instruction.unit == LowerExecutionUnit::FDIV);
    REQUIRE(instruction.opCode == VPU_SQRT);
    REQUIRE(instruction.sourceRegister1 == VPU_REGISTER_VF00);
    REQUIRE(instruction.sourceRegister2 == VPU_REGISTER_VF05);
    REQUIRE(instruction.sourceFieldMask1 == FP_REGISTER_NO_FIELDS);
    REQUIRE(instruction.sourceFieldMask2 == FP_REGISTER_Z_FIELD);
  }

  SECTION("WAITQ decodes without register operands")
  {
    LowerInstruction instruction =
      decodeLowerInstruction(VPU_WAITQ_ENCODING);

    REQUIRE(instruction.unit == LowerExecutionUnit::WaitQ);
    REQUIRE(instruction.opCode == VPU_WAITQ);
    REQUIRE(instruction.sourceRegister1 == VPU_REGISTER_VF00);
    REQUIRE(instruction.sourceRegister2 == VPU_REGISTER_VF00);
  }

  SECTION("DIV samples the selected fields and writes raw Q at F")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<VPUTraceEvent> events;
    vpu.loadFPRegister(VPU_REGISTER_VF02, 99, 6, 99, 99);
    vpu.loadFPRegister(VPU_REGISTER_VF03, 99, 99, 99, 2);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      fdiv(
        VPU_DIV_ENCODING,
        VPU_REGISTER_VF02,
        1,
        VPU_REGISTER_VF03,
        3));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      VPU_WAITQ_ENCODING);
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });
    vpu.initMicroMode();

    REQUIRE(vpu.qRegisterBits() == 0x40400000u);
    REQUIRE(vpu.elapsedCycles() == 11);

    uint32_t writebackCycle = 0;
    for (const VPUTraceEvent &event : events)
    {
      if (event.type == VPUTraceEventType::PipelineWriteback &&
          event.opCode == VPU_DIV)
      {
        writebackCycle = event.cycle;
      }
    }
    REQUIRE(writebackCycle == 8);
  }

  SECTION("SQRT and RSQRT write their exact raw results")
  {
    VPU sqrtVPU;
    std::vector<uint8_t> sqrtInstructions;
    sqrtVPU.loadFPRegister(VPU_REGISTER_VF02, 0, 0, 10, 0);
    appendInstructionPair(
      &sqrtInstructions,
      VPU_NOP,
      sqrt(VPU_REGISTER_VF02, 2));
    appendInstructionPair(
      &sqrtInstructions,
      VPU_E_BIT | VPU_NOP,
      VPU_WAITQ_ENCODING);
    appendInstructionPair(
      &sqrtInstructions,
      VPU_NOP,
      VPU_LOWER_NOP);
    sqrtVPU.uploadMicroInstructions(sqrtInstructions);
    sqrtVPU.initMicroMode();

    VPU rsqrtVPU;
    std::vector<uint8_t> rsqrtInstructions;
    rsqrtVPU.loadFPRegister(VPU_REGISTER_VF02, 6, 0, 0, 0);
    rsqrtVPU.loadFPRegister(VPU_REGISTER_VF03, 0, 4, 0, 0);
    appendInstructionPair(
      &rsqrtInstructions,
      VPU_NOP,
      fdiv(
        VPU_RSQRT_ENCODING,
        VPU_REGISTER_VF02,
        0,
        VPU_REGISTER_VF03,
        1));
    appendInstructionPair(
      &rsqrtInstructions,
      VPU_E_BIT | VPU_NOP,
      VPU_WAITQ_ENCODING);
    appendInstructionPair(
      &rsqrtInstructions,
      VPU_NOP,
      VPU_LOWER_NOP);
    rsqrtVPU.uploadMicroInstructions(rsqrtInstructions);
    rsqrtVPU.initMicroMode();

    REQUIRE(sqrtVPU.qRegisterBits() == 0x404a62c1u);
    REQUIRE(rsqrtVPU.qRegisterBits() == 0x40400000u);
    REQUIRE(rsqrtVPU.elapsedCycles() == 17);
  }

  SECTION("Q operations replace current I and D while preserving sticky flags")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadFPRegister(VPU_REGISTER_VF02, 1, 0, 0, 0);
    vpu.loadFPRegister(VPU_REGISTER_VF03, 0, 0, 0, 0);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      fdiv(
        VPU_DIV_ENCODING,
        VPU_REGISTER_VF02,
        0,
        VPU_REGISTER_VF03,
        0));
    appendInstructionPair(
      &instructions,
      VPU_NOP,
      VPU_WAITQ_ENCODING);
    appendInstructionPair(
      &instructions,
      VPU_NOP,
      fdiv(
        VPU_DIV_ENCODING,
        VPU_REGISTER_VF03,
        0,
        VPU_REGISTER_VF03,
        0));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      VPU_WAITQ_ENCODING);
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.qRegisterBits() == 0x7fffffffu);
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_I));
    REQUIRE_FALSE(vpu.hasStatusFlag(VPU_FLAG_D));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_IS));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_DS));
  }

  SECTION("A pending upper write stalls a selected DIV source lane")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<VPUTraceEvent> events;
    vpu.loadFPRegister(VPU_REGISTER_VF02, 1, 6, 0, 0);
    vpu.loadFPRegister(VPU_REGISTER_VF03, 2, 2, 0, 0);
    vpu.loadFPRegister(VPU_REGISTER_VF04, 2, 2, 0, 0);

    appendInstructionPair(
      &instructions,
      add(
        VPU_DEST_Y_BIT,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF04,
        VPU_REGISTER_VF02),
      VPU_LOWER_NOP);
    appendInstructionPair(
      &instructions,
      VPU_NOP,
      fdiv(
        VPU_DIV_ENCODING,
        VPU_REGISTER_VF02,
        1,
        VPU_REGISTER_VF03,
        0));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      VPU_WAITQ_ENCODING);
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });
    vpu.initMicroMode();

    bool stalled = false;
    for (const VPUTraceEvent &event : events)
    {
      stalled = stalled || event.type == VPUTraceEventType::PipelineStall;
    }
    REQUIRE(stalled);
    REQUIRE(vpu.qRegisterBits() == 0x40000000u);
  }

  SECTION("IADD bypasses its 16-bit result to the next instruction")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadIntRegister(VPU_REGISTER_VI01, 0xffff);
    vpu.loadIntRegister(VPU_REGISTER_VI02, 2);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      iadd(VPU_REGISTER_VI03, VPU_REGISTER_VI01, VPU_REGISTER_VI02));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      iadd(VPU_REGISTER_VI04, VPU_REGISTER_VI03, VPU_REGISTER_VI02));
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI03) == 1);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 3);
  }

  SECTION("IALU writes to VI00 are discarded")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadIntRegister(VPU_REGISTER_VI01, 10);
    vpu.loadIntRegister(VPU_REGISTER_VI02, 20);

    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      iadd(VPU_REGISTER_VI00, VPU_REGISTER_VI01, VPU_REGISTER_VI02));
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI00) == 0);
  }

  SECTION("ISUBIU decodes all 15 unsigned immediate bits and wraps")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadIntRegister(VPU_REGISTER_VI01, 0x1234);

    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      isubiu(VPU_REGISTER_VI02, VPU_REGISTER_VI01, 0x4321));
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI02) == 0xcf13);
  }

  SECTION("MFIR sign extends VI data and updates only selected fields")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadIntRegister(VPU_REGISTER_VI01, -2);
    vpu.loadIntFPRegister(VPU_REGISTER_VF02, 10, 20, 30, 40);

    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      mfir(
        FP_REGISTER_X_FIELD | FP_REGISTER_Z_FIELD,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VI01));
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    const FPRegister *result = vpu.fpRegisterValue(VPU_REGISTER_VF02);
    REQUIRE(result->x.signedValue() == -2);
    REQUIRE(result->y.signedValue() == 20);
    REQUIRE(result->z.signedValue() == -2);
    REQUIRE(result->w.signedValue() == 40);
  }

  SECTION("MFIR becomes visible only at pipeline writeback")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadIntRegister(VPU_REGISTER_VI01, 99);
    vpu.loadIntFPRegister(VPU_REGISTER_VF02, 1, 2, 3, 4);

    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      mfir(FP_REGISTER_X_FIELD, VPU_REGISTER_VF02, VPU_REGISTER_VI01));
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();

    for (int cycle = 0; cycle < 5; cycle++)
    {
      vpu.tick();
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x.signedValue() == 1);
    }

    vpu.tick();
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x.signedValue() == 99);
  }

  SECTION("An MFIR destination stalls a later floating-point consumer")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<VPUTraceEvent> events;
    vpu.loadIntRegister(VPU_REGISTER_VI01, 10);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      mfir(FP_REGISTER_X_FIELD, VPU_REGISTER_VF02, VPU_REGISTER_VI01));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT |
        itof0(
          VPU_DEST_X_BIT,
          VPU_REGISTER_VF04,
          VPU_REGISTER_VF02),
      VPU_LOWER_NOP);
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });
    vpu.initMicroMode();

    bool stalled = false;
    for (const VPUTraceEvent &event : events)
    {
      stalled = stalled || event.type == VPUTraceEventType::PipelineStall;
    }

    REQUIRE(stalled);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->x == 10);
  }

  SECTION("A stalled upper instruction stalls its paired IALU operation")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadFPRegister(VPU_REGISTER_VF02, 1, 0, 0, 0);
    vpu.loadFPRegister(VPU_REGISTER_VF03, 2, 0, 0, 0);
    vpu.loadFPRegister(VPU_REGISTER_VF05, 3, 0, 0, 0);
    vpu.loadIntRegister(VPU_REGISTER_VI01, 10);
    vpu.loadIntRegister(VPU_REGISTER_VI02, 20);

    appendInstructionPair(
      &instructions,
      add(
        VPU_DEST_X_BIT,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF01),
      VPU_LOWER_NOP);
    appendInstructionPair(
      &instructions,
      VPU_E_BIT |
        add(
          VPU_DEST_X_BIT,
          VPU_REGISTER_VF01,
          VPU_REGISTER_VF05,
          VPU_REGISTER_VF04),
      iadd(VPU_REGISTER_VI03, VPU_REGISTER_VI01, VPU_REGISTER_VI02));
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();
    vpu.tick();
    vpu.tick();
    vpu.tick();

    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI03) == 0);

    vpu.run(20);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI03) == 30);
  }

  SECTION("Upper writes discard same-cycle lower writes register-wide")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadFPRegister(VPU_REGISTER_VF03, 4, 0, 0, 0);
    vpu.loadFPRegister(VPU_REGISTER_VF04, 7, 0, 0, 0);
    vpu.loadIntRegister(VPU_REGISTER_VI01, -2);
    vpu.loadIntFPRegister(VPU_REGISTER_VF02, 1, 22, 3, 4);

    appendInstructionPair(
      &instructions,
      VPU_E_BIT |
        add(
          VPU_DEST_X_BIT,
          VPU_REGISTER_VF03,
          VPU_REGISTER_VF04,
          VPU_REGISTER_VF02),
      mfir(FP_REGISTER_Y_FIELD, VPU_REGISTER_VF02, VPU_REGISTER_VI01));
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == 11);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y.signedValue() == 22);
  }

  SECTION("The I bit treats the lower word as immediate data")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadFPRegister(VPU_REGISTER_VF02, 2, 0, 0, 0);

    appendInstructionPair(
      &instructions,
      VPU_I_BIT | VPU_NOP,
      0x3f800000);
    appendInstructionPair(
      &instructions,
      VPU_E_BIT |
        addi(
          VPU_DEST_X_BIT,
          VPU_REGISTER_VF02,
          VPU_REGISTER_VF03),
      VPU_LOWER_NOP);
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->x == 3);
  }

  SECTION("ILW loads one selected lane and hazards consumers until writeback")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<VPUTraceEvent> events;
    std::vector<uint8_t> data(16, 0);
    data[4] = 0x34;
    data[5] = 0x12;
    data[6] = 0xcd;
    data[7] = 0xab;
    vpu.writeDataMemory(0, data);
    vpu.loadIntRegister(VPU_REGISTER_VI01, 1);
    vpu.loadIntRegister(VPU_REGISTER_VI03, 1);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      ilw(
        FP_REGISTER_Y_FIELD,
        VPU_REGISTER_VI02,
        VPU_REGISTER_VI01,
        -1));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      iadd(
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI02,
        VPU_REGISTER_VI03));
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });
    vpu.initMicroMode();

    bool stalled = false;
    for (const VPUTraceEvent &event : events)
    {
      stalled = stalled || event.type == VPUTraceEventType::PipelineStall;
    }

    REQUIRE(stalled);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI02) == 0x1234);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 0x1235);
  }

  SECTION("LQ loads masked raw lane values with signed qword offsets")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<uint8_t> data(16, 0);
    data[0] = 0x78;
    data[1] = 0x56;
    data[2] = 0x34;
    data[3] = 0x12;
    data[8] = 0xef;
    data[9] = 0xcd;
    data[10] = 0xab;
    data[11] = 0x90;
    vpu.writeDataMemory(0, data);
    vpu.loadIntRegister(VPU_REGISTER_VI01, 1);
    vpu.loadIntFPRegister(VPU_REGISTER_VF02, 1, 2, 3, 4);

    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      lq(
        FP_REGISTER_X_FIELD | FP_REGISTER_Z_FIELD,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VI01,
        -1));
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    const FPRegister *result = vpu.fpRegisterValue(VPU_REGISTER_VF02);
    REQUIRE(result->x.bits() == 0x12345678);
    REQUIRE(result->y.signedValue() == 2);
    REQUIRE(result->z.bits() == 0x90abcdef);
    REQUIRE(result->w.signedValue() == 4);
  }

  SECTION("Upper writes discard a same-pair LQ write register-wide")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<uint8_t> data(16, 0);
    data[4] = 0x78;
    data[5] = 0x56;
    data[6] = 0x34;
    data[7] = 0x12;
    vpu.writeDataMemory(0, data);
    vpu.loadFPRegister(VPU_REGISTER_VF03, 4, 0, 0, 0);
    vpu.loadFPRegister(VPU_REGISTER_VF04, 7, 0, 0, 0);
    vpu.loadIntFPRegister(VPU_REGISTER_VF02, 1, 22, 3, 4);

    appendInstructionPair(
      &instructions,
      VPU_E_BIT |
        add(
          VPU_DEST_X_BIT,
          VPU_REGISTER_VF03,
          VPU_REGISTER_VF04,
          VPU_REGISTER_VF02),
      lq(
        FP_REGISTER_Y_FIELD,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VI00,
        0));
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == 11);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y.signedValue() == 22);
  }

  SECTION("SQI stores selected lanes and post-increments with VI hazards")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<VPUTraceEvent> events;
    vpu.loadIntRegister(VPU_REGISTER_VI01, 2);
    vpu.loadIntRegister(VPU_REGISTER_VI02, 3);
    vpu.loadIntFPRegister(
      VPU_REGISTER_VF03,
      0x11223344,
      0x55667788,
      0x12345678,
      -2);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      sqi(
        FP_REGISTER_X_FIELD | FP_REGISTER_W_FIELD,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VI01));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      iadd(
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI01,
        VPU_REGISTER_VI02));
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });
    vpu.initMicroMode();

    std::vector<uint8_t> stored = vpu.readDataMemory(32, 16);
    REQUIRE(stored[0] == 0x44);
    REQUIRE(stored[1] == 0x33);
    REQUIRE(stored[2] == 0x22);
    REQUIRE(stored[3] == 0x11);
    REQUIRE(stored[4] == 0);
    REQUIRE(stored[12] == 0xfe);
    REQUIRE(stored[13] == 0xff);
    REQUIRE(stored[14] == 0xff);
    REQUIRE(stored[15] == 0xff);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI01) == 3);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 6);

    bool stalled = false;
    for (const VPUTraceEvent &event : events)
    {
      stalled = stalled || event.type == VPUTraceEventType::PipelineStall;
    }
    REQUIRE(stalled);
  }

  SECTION("A pending upper write stalls an SQI source and the whole pair")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadFPRegister(VPU_REGISTER_VF02, 1, 0, 0, 0);
    vpu.loadFPRegister(VPU_REGISTER_VF03, 2, 0, 0, 0);
    vpu.loadIntRegister(VPU_REGISTER_VI01, 0);
    vpu.loadIntRegister(VPU_REGISTER_VI02, 4);
    vpu.loadIntRegister(VPU_REGISTER_VI03, 5);

    appendInstructionPair(
      &instructions,
      add(
        VPU_DEST_X_BIT,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VF04),
      VPU_LOWER_NOP);
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      sqi(
        FP_REGISTER_X_FIELD,
        VPU_REGISTER_VF04,
        VPU_REGISTER_VI01));
    appendInstructionPair(
      &instructions,
      VPU_NOP,
      iadd(
        VPU_REGISTER_VI05,
        VPU_REGISTER_VI02,
        VPU_REGISTER_VI03));

    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();
    vpu.tick();
    vpu.tick();

    REQUIRE(vpu.programCounter() == 8);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI05) == 0);

    vpu.run(20);
    REQUIRE(vpu.readDataMemory(0, 4) ==
      std::vector<uint8_t>({0, 0, 0x40, 0x40}));
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI05) == 9);
  }

  SECTION("LSU instructions are rejected in the E-bit delay slot")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      VPU_LOWER_NOP);
    appendInstructionPair(
      &instructions,
      VPU_NOP,
      lq(
        FP_REGISTER_X_FIELD,
        VPU_REGISTER_VF01,
        VPU_REGISTER_VI00,
        0));

    vpu.uploadMicroInstructions(instructions);

    REQUIRE_THROWS_WITH(
      vpu.initMicroMode(),
      "VU lower instruction cannot execute in an E-bit delay slot.");
  }
}
