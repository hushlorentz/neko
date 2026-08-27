#include <cstdint>
#include <utility>
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

  uint32_t type1IALU(
    uint32_t encoding,
    uint8_t id,
    uint8_t is,
    uint8_t it)
  {
    return
      encoding |
      (static_cast<uint32_t>(it) << 16) |
      (static_cast<uint32_t>(is) << 11) |
      (static_cast<uint32_t>(id) << 6);
  }

  uint32_t iaddi(uint8_t it, uint8_t is, int8_t immediate)
  {
    return
      VPU_IADDI_ENCODING |
      (static_cast<uint32_t>(it) << 16) |
      (static_cast<uint32_t>(is) << 11) |
      ((static_cast<uint8_t>(immediate) & 0x1f) << 6);
  }

  uint32_t iaddiu(uint8_t it, uint8_t is, uint16_t immediate)
  {
    return
      VPU_IADDIU_ENCODING |
      (static_cast<uint32_t>((immediate >> 11) & 0xf) << 21) |
      (static_cast<uint32_t>(it) << 16) |
      (static_cast<uint32_t>(is) << 11) |
      (immediate & 0x7ff);
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

  uint32_t branch(
    uint32_t encoding,
    uint8_t it,
    uint8_t is,
    int16_t immediate)
  {
    return
      encoding |
      (static_cast<uint32_t>(it) << 16) |
      (static_cast<uint32_t>(is) << 11) |
      (static_cast<uint16_t>(immediate) & 0x7ff);
  }

  uint32_t mfir(uint8_t fieldMask, uint8_t ft, uint8_t is)
  {
    return
      VPU_MFIR_ENCODING |
      (static_cast<uint32_t>(vpuFieldMaskToEncoding(fieldMask)) << 21) |
      (static_cast<uint32_t>(ft) << 16) |
      (static_cast<uint32_t>(is) << 11);
  }

  uint32_t movement(
    uint32_t encoding,
    uint8_t fieldMask,
    uint8_t ft,
    uint8_t fs)
  {
    return
      encoding |
      (static_cast<uint32_t>(vpuFieldMaskToEncoding(fieldMask)) << 21) |
      (static_cast<uint32_t>(ft) << 16) |
      (static_cast<uint32_t>(fs) << 11);
  }

  uint32_t mtir(uint8_t it, uint8_t fs, uint8_t field)
  {
    return
      VPU_MTIR_ENCODING |
      (static_cast<uint32_t>(field) << 21) |
      (static_cast<uint32_t>(it) << 16) |
      (static_cast<uint32_t>(fs) << 11);
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

  uint32_t type3Memory(
    uint32_t encoding,
    uint8_t fieldMask,
    uint8_t it,
    uint8_t is)
  {
    return
      encoding |
      (static_cast<uint32_t>(vpuFieldMaskToEncoding(fieldMask)) << 21) |
      (static_cast<uint32_t>(it) << 16) |
      (static_cast<uint32_t>(is) << 11);
  }

  uint32_t offsetMemory(
    uint32_t encoding,
    uint8_t fieldMask,
    uint8_t it,
    uint8_t is,
    int16_t immediate)
  {
    return
      encoding |
      (static_cast<uint32_t>(vpuFieldMaskToEncoding(fieldMask)) << 21) |
      (static_cast<uint32_t>(it) << 16) |
      (static_cast<uint32_t>(is) << 11) |
      (static_cast<uint16_t>(immediate) & 0x7ff);
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
  SECTION("The complete relative branch family decodes its operand layouts")
  {
    LowerInstruction b =
      decodeLowerInstruction(branch(VPU_B_ENCODING, 0, 0, -1024));
    LowerInstruction bal =
      decodeLowerInstruction(branch(
        VPU_BAL_ENCODING, VPU_REGISTER_VI15, 0, 1023));
    LowerInstruction ibeq =
      decodeLowerInstruction(branch(
        VPU_IBEQ_ENCODING,
        VPU_REGISTER_VI02,
        VPU_REGISTER_VI03,
        -7));
    LowerInstruction ibne =
      decodeLowerInstruction(branch(
        VPU_IBNE_ENCODING,
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI05,
        9));

    REQUIRE(b.unit == LowerExecutionUnit::Branch);
    REQUIRE(b.opCode == VPU_B);
    REQUIRE(b.immediate == -1024);
    REQUIRE(bal.opCode == VPU_BAL);
    REQUIRE(bal.destinationRegister == VPU_REGISTER_VI15);
    REQUIRE(bal.immediate == 1023);
    REQUIRE(ibeq.opCode == VPU_IBEQ);
    REQUIRE(ibeq.sourceRegister1 == VPU_REGISTER_VI03);
    REQUIRE(ibeq.sourceRegister2 == VPU_REGISTER_VI02);
    REQUIRE(ibeq.immediate == -7);
    REQUIRE(ibne.opCode == VPU_IBNE);
    REQUIRE(ibne.sourceRegister1 == VPU_REGISTER_VI05);
    REQUIRE(ibne.sourceRegister2 == VPU_REGISTER_VI04);
    REQUIRE(ibne.immediate == 9);

    for (const auto &contract : {
      std::make_pair(VPU_IBGEZ_ENCODING, VPU_IBGEZ),
      std::make_pair(VPU_IBGTZ_ENCODING, VPU_IBGTZ),
      std::make_pair(VPU_IBLEZ_ENCODING, VPU_IBLEZ),
      std::make_pair(VPU_IBLTZ_ENCODING, VPU_IBLTZ)})
    {
      LowerInstruction decoded = decodeLowerInstruction(branch(
        contract.first, 0, VPU_REGISTER_VI06, -1));
      REQUIRE(decoded.opCode == contract.second);
      REQUIRE(decoded.sourceRegister1 == VPU_REGISTER_VI06);
      REQUIRE(decoded.sourceRegister2 == VPU_REGISTER_VI00);
      REQUIRE(decoded.immediate == -1);
    }
  }

  SECTION("Remaining IALU encodings decode their register fields")
  {
    for (uint32_t encoding : {
           VPU_IAND_ENCODING,
           VPU_IOR_ENCODING,
           VPU_ISUB_ENCODING})
    {
      LowerInstruction instruction = decodeLowerInstruction(
        type1IALU(
          encoding,
          VPU_REGISTER_VI03,
          VPU_REGISTER_VI01,
          VPU_REGISTER_VI02));

      REQUIRE(instruction.unit == LowerExecutionUnit::IALU);
      REQUIRE(instruction.opCode == (encoding & VPU_TYPE1_MASK));
      REQUIRE(instruction.sourceRegister1 == VPU_REGISTER_VI01);
      REQUIRE(instruction.sourceRegister2 == VPU_REGISTER_VI02);
      REQUIRE(instruction.destinationRegister == VPU_REGISTER_VI03);
    }
  }

  SECTION("IADDI decodes signed five-bit boundary values")
  {
    LowerInstruction negative = decodeLowerInstruction(
      iaddi(VPU_REGISTER_VI03, VPU_REGISTER_VI02, -16));
    LowerInstruction positive = decodeLowerInstruction(
      iaddi(VPU_REGISTER_VI05, VPU_REGISTER_VI04, 15));

    REQUIRE(negative.opCode == VPU_IADDI);
    REQUIRE(negative.sourceRegister1 == VPU_REGISTER_VI02);
    REQUIRE(negative.destinationRegister == VPU_REGISTER_VI03);
    REQUIRE(negative.immediate == -16);
    REQUIRE(positive.immediate == 15);
  }

  SECTION("IADDIU decodes all fifteen unsigned immediate bits")
  {
    LowerInstruction instruction = decodeLowerInstruction(
      iaddiu(VPU_REGISTER_VI03, VPU_REGISTER_VI02, 0x7fff));

    REQUIRE(instruction.unit == LowerExecutionUnit::IALU);
    REQUIRE(instruction.opCode == VPU_IADDIU);
    REQUIRE(instruction.sourceRegister1 == VPU_REGISTER_VI02);
    REQUIRE(instruction.destinationRegister == VPU_REGISTER_VI03);
    REQUIRE(instruction.immediate == 0x7fff);
  }

  SECTION("Common IALU operations wrap to sixteen bits and bypass")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadIntRegister(VPU_REGISTER_VI01, 0xfff8);
    vpu.loadIntRegister(VPU_REGISTER_VI02, 0x0ff0);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      iaddi(VPU_REGISTER_VI03, VPU_REGISTER_VI01, -16));
    appendInstructionPair(
      &instructions,
      VPU_NOP,
      iaddiu(VPU_REGISTER_VI04, VPU_REGISTER_VI03, 0x7fff));
    appendInstructionPair(
      &instructions,
      VPU_NOP,
      type1IALU(
        VPU_IAND_ENCODING,
        VPU_REGISTER_VI05,
        VPU_REGISTER_VI04,
        VPU_REGISTER_VI02));
    appendInstructionPair(
      &instructions,
      VPU_NOP,
      type1IALU(
        VPU_IOR_ENCODING,
        VPU_REGISTER_VI06,
        VPU_REGISTER_VI05,
        VPU_REGISTER_VI01));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      type1IALU(
        VPU_ISUB_ENCODING,
        VPU_REGISTER_VI07,
        VPU_REGISTER_VI06,
        VPU_REGISTER_VI02));
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI03) == 0xffe8);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 0x7fe7);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI05) == 0x0fe0);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI06) == 0xfff8);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI07) == 0xf008);
  }

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

  SECTION("MOVE and MR32 decode destination and required source lanes")
  {
    LowerInstruction move = decodeLowerInstruction(movement(
      VPU_MOVE_ENCODING,
      FP_REGISTER_X_FIELD | FP_REGISTER_Z_FIELD,
      VPU_REGISTER_VF02,
      VPU_REGISTER_VF01));
    LowerInstruction mr32 = decodeLowerInstruction(movement(
      VPU_MR32_ENCODING,
      FP_REGISTER_X_FIELD | FP_REGISTER_Z_FIELD,
      VPU_REGISTER_VF04,
      VPU_REGISTER_VF03));

    REQUIRE(move.unit == LowerExecutionUnit::FMAC);
    REQUIRE(move.opCode == VPU_MOVE);
    REQUIRE(move.sourceRegister1 == VPU_REGISTER_VF01);
    REQUIRE(move.destinationRegister == VPU_REGISTER_VF02);
    REQUIRE(move.destinationFieldMask ==
      (FP_REGISTER_X_FIELD | FP_REGISTER_Z_FIELD));
    REQUIRE(move.sourceFieldMask1 ==
      (FP_REGISTER_X_FIELD | FP_REGISTER_Z_FIELD));

    REQUIRE(mr32.unit == LowerExecutionUnit::FMAC);
    REQUIRE(mr32.opCode == VPU_MR32);
    REQUIRE(mr32.sourceRegister1 == VPU_REGISTER_VF03);
    REQUIRE(mr32.destinationRegister == VPU_REGISTER_VF04);
    REQUIRE(mr32.destinationFieldMask ==
      (FP_REGISTER_X_FIELD | FP_REGISTER_Z_FIELD));
    REQUIRE(mr32.sourceFieldMask1 ==
      (FP_REGISTER_Y_FIELD | FP_REGISTER_W_FIELD));
  }

  SECTION("MTIR decodes its selected source lane and integer destination")
  {
    LowerInstruction instruction = decodeLowerInstruction(mtir(
      VPU_REGISTER_VI02,
      VPU_REGISTER_VF03,
      2));

    REQUIRE(instruction.unit == LowerExecutionUnit::FMAC);
    REQUIRE(instruction.opCode == VPU_MTIR);
    REQUIRE(instruction.sourceRegister1 == VPU_REGISTER_VF03);
    REQUIRE(instruction.integerDestinationRegister == VPU_REGISTER_VI02);
    REQUIRE(instruction.sourceFieldMask1 == FP_REGISTER_Z_FIELD);
  }

  SECTION("MOVE updates selected lanes without changing their bits")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadIntFPRegister(VPU_REGISTER_VF01, 10, 20, 30, 40);
    vpu.loadIntFPRegister(VPU_REGISTER_VF02, 1, 2, 3, 4);

    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      movement(
        VPU_MOVE_ENCODING,
        FP_REGISTER_X_FIELD | FP_REGISTER_Z_FIELD,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF01));
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    const FPRegister *result = vpu.fpRegisterValue(VPU_REGISTER_VF02);
    REQUIRE(result->x.signedValue() == 10);
    REQUIRE(result->y.signedValue() == 2);
    REQUIRE(result->z.signedValue() == 30);
    REQUIRE(result->w.signedValue() == 4);
  }

  SECTION("MR32 rotates lanes from one source snapshot")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadIntFPRegister(VPU_REGISTER_VF01, 10, 20, 30, 40);

    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      movement(
        VPU_MR32_ENCODING,
        FP_REGISTER_ALL_FIELDS,
        VPU_REGISTER_VF01,
        VPU_REGISTER_VF01));
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    const FPRegister *result = vpu.fpRegisterValue(VPU_REGISTER_VF01);
    REQUIRE(result->x.signedValue() == 20);
    REQUIRE(result->y.signedValue() == 30);
    REQUIRE(result->z.signedValue() == 40);
    REQUIRE(result->w.signedValue() == 10);
  }

  SECTION("MTIR transfers only the selected lane's low sixteen bits")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadIntFPRegister(
      VPU_REGISTER_VF01,
      0x12345678,
      static_cast<int32_t>(0x89abcdef),
      0x76543210,
      static_cast<int32_t>(0xfedcba98));

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      mtir(VPU_REGISTER_VI01, VPU_REGISTER_VF01, 0));
    appendInstructionPair(
      &instructions,
      VPU_NOP,
      mtir(VPU_REGISTER_VI02, VPU_REGISTER_VF01, 1));
    appendInstructionPair(
      &instructions,
      VPU_NOP,
      mtir(VPU_REGISTER_VI03, VPU_REGISTER_VF01, 2));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      mtir(VPU_REGISTER_VI04, VPU_REGISTER_VF01, 3));
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI01) == 0x5678);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI02) == 0xcdef);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI03) == 0x3210);
    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI04) == 0xba98);
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

  SECTION("Remaining memory variants decode registers masks and offsets")
  {
    LowerInstruction ilwr = decodeLowerInstruction(type3Memory(
      VPU_ILWR_ENCODING,
      FP_REGISTER_Z_FIELD,
      VPU_REGISTER_VI02,
      VPU_REGISTER_VI01));
    LowerInstruction isw = decodeLowerInstruction(offsetMemory(
      VPU_ISW_ENCODING,
      FP_REGISTER_X_FIELD | FP_REGISTER_W_FIELD,
      VPU_REGISTER_VI02,
      VPU_REGISTER_VI01,
      -7));
    LowerInstruction iswr = decodeLowerInstruction(type3Memory(
      VPU_ISWR_ENCODING,
      FP_REGISTER_Y_FIELD,
      VPU_REGISTER_VI02,
      VPU_REGISTER_VI01));
    LowerInstruction lqd = decodeLowerInstruction(type3Memory(
      VPU_LQD_ENCODING,
      FP_REGISTER_X_FIELD | FP_REGISTER_Z_FIELD,
      VPU_REGISTER_VF02,
      VPU_REGISTER_VI01));
    LowerInstruction lqi = decodeLowerInstruction(type3Memory(
      VPU_LQI_ENCODING,
      FP_REGISTER_Y_FIELD | FP_REGISTER_W_FIELD,
      VPU_REGISTER_VF03,
      VPU_REGISTER_VI04));
    LowerInstruction sq = decodeLowerInstruction(offsetMemory(
      VPU_SQ_ENCODING,
      FP_REGISTER_X_FIELD | FP_REGISTER_Y_FIELD,
      VPU_REGISTER_VI05,
      VPU_REGISTER_VF04,
      9));
    LowerInstruction sqd = decodeLowerInstruction(type3Memory(
      VPU_SQD_ENCODING,
      FP_REGISTER_Z_FIELD | FP_REGISTER_W_FIELD,
      VPU_REGISTER_VI06,
      VPU_REGISTER_VF05));

    REQUIRE(ilwr.opCode == VPU_ILWR);
    REQUIRE(ilwr.sourceRegister1 == VPU_REGISTER_VI01);
    REQUIRE(ilwr.destinationRegister == VPU_REGISTER_VI02);
    REQUIRE(ilwr.destinationFieldMask == FP_REGISTER_Z_FIELD);
    REQUIRE(isw.opCode == VPU_ISW);
    REQUIRE(isw.sourceRegister1 == VPU_REGISTER_VI01);
    REQUIRE(isw.sourceRegister2 == VPU_REGISTER_VI02);
    REQUIRE(isw.immediate == -7);
    REQUIRE(iswr.opCode == VPU_ISWR);
    REQUIRE(iswr.destinationFieldMask == FP_REGISTER_Y_FIELD);
    REQUIRE(lqd.opCode == VPU_LQD);
    REQUIRE(lqd.sourceRegister1 == VPU_REGISTER_VI01);
    REQUIRE(lqd.destinationRegister == VPU_REGISTER_VF02);
    REQUIRE(lqi.opCode == VPU_LQI);
    REQUIRE(lqi.sourceRegister1 == VPU_REGISTER_VI04);
    REQUIRE(lqi.destinationRegister == VPU_REGISTER_VF03);
    REQUIRE(sq.opCode == VPU_SQ);
    REQUIRE(sq.sourceRegister1 == VPU_REGISTER_VF04);
    REQUIRE(sq.sourceRegister2 == VPU_REGISTER_VI05);
    REQUIRE(sq.immediate == 9);
    REQUIRE(sqd.opCode == VPU_SQD);
    REQUIRE(sqd.sourceRegister1 == VPU_REGISTER_VF05);
    REQUIRE(sqd.sourceRegister2 == VPU_REGISTER_VI06);
    REQUIRE(sqd.integerDestinationRegister == VPU_REGISTER_VI06);
  }

  SECTION("ILWR loads the selected lower halfword")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<uint8_t> data(32, 0);
    data[24] = 0xcd;
    data[25] = 0xab;
    data[26] = 0x78;
    data[27] = 0x56;
    vpu.writeDataMemory(0, data);
    vpu.loadIntRegister(VPU_REGISTER_VI01, 1);

    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      type3Memory(
        VPU_ILWR_ENCODING,
        FP_REGISTER_Z_FIELD,
        VPU_REGISTER_VI02,
        VPU_REGISTER_VI01));
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI02) == 0xabcd);
  }

  SECTION("ISW and ISWR zero-extend integer stores into selected words")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<uint8_t> data(48, 0xff);
    vpu.writeDataMemory(0, data);
    vpu.loadIntRegister(VPU_REGISTER_VI01, 1);
    vpu.loadIntRegister(VPU_REGISTER_VI02, 0xabcd);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      offsetMemory(
        VPU_ISW_ENCODING,
        FP_REGISTER_X_FIELD | FP_REGISTER_W_FIELD,
        VPU_REGISTER_VI02,
        VPU_REGISTER_VI01,
        1));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      type3Memory(
        VPU_ISWR_ENCODING,
        FP_REGISTER_Y_FIELD,
        VPU_REGISTER_VI02,
        VPU_REGISTER_VI01));
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.readDataMemory(20, 4) ==
      std::vector<uint8_t>({0xcd, 0xab, 0x00, 0x00}));
    REQUIRE(vpu.readDataMemory(32, 4) ==
      std::vector<uint8_t>({0xcd, 0xab, 0x00, 0x00}));
    REQUIRE(vpu.readDataMemory(44, 4) ==
      std::vector<uint8_t>({0xcd, 0xab, 0x00, 0x00}));
  }

  SECTION("LQD and LQI update addresses around masked loads")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    std::vector<uint8_t> data({
      0x11, 0x11, 0x11, 0x11,
      0x22, 0x22, 0x22, 0x22,
      0x33, 0x33, 0x33, 0x33,
      0x44, 0x44, 0x44, 0x44,
      0xaa, 0xaa, 0xaa, 0xaa,
      0xbb, 0xbb, 0xbb, 0xbb,
      0xcc, 0xcc, 0xcc, 0xcc,
      0xdd, 0xdd, 0xdd, 0xdd
    });
    vpu.writeDataMemory(0, data);
    vpu.loadIntRegister(VPU_REGISTER_VI01, 1);
    vpu.loadIntFPRegister(VPU_REGISTER_VF02, 1, 2, 3, 4);
    vpu.loadIntFPRegister(VPU_REGISTER_VF03, 5, 6, 7, 8);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      type3Memory(
        VPU_LQD_ENCODING,
        FP_REGISTER_X_FIELD | FP_REGISTER_Z_FIELD,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VI01));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      type3Memory(
        VPU_LQI_ENCODING,
        FP_REGISTER_Y_FIELD | FP_REGISTER_W_FIELD,
        VPU_REGISTER_VF03,
        VPU_REGISTER_VI01));
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI01) == 1);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x.bits() == 0x11111111);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y.signedValue() == 2);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z.bits() == 0x33333333);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->x.signedValue() == 5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->y.bits() == 0x22222222);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->w.bits() == 0x44444444);
  }

  SECTION("SQ and SQD store masked fields and pre-decrement addresses")
  {
    VPU vpu;
    std::vector<uint8_t> instructions;
    vpu.loadIntRegister(VPU_REGISTER_VI01, 2);
    vpu.loadIntFPRegister(
      VPU_REGISTER_VF02,
      0x11223344,
      0x55667788,
      0x12345678,
      -2);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      offsetMemory(
        VPU_SQ_ENCODING,
        FP_REGISTER_X_FIELD | FP_REGISTER_Z_FIELD,
        VPU_REGISTER_VI01,
        VPU_REGISTER_VF02,
        -2));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      type3Memory(
        VPU_SQD_ENCODING,
        FP_REGISTER_Y_FIELD | FP_REGISTER_W_FIELD,
        VPU_REGISTER_VI01,
        VPU_REGISTER_VF02));
    appendInstructionPair(&instructions, VPU_NOP, VPU_LOWER_NOP);

    vpu.uploadMicroInstructions(instructions);
    vpu.initMicroMode();

    REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI01) == 1);
    REQUIRE(vpu.readDataMemory(0, 16) ==
      std::vector<uint8_t>({
        0x44, 0x33, 0x22, 0x11,
        0x00, 0x00, 0x00, 0x00,
        0x78, 0x56, 0x34, 0x12,
        0x00, 0x00, 0x00, 0x00
      }));
    REQUIRE(vpu.readDataMemory(16, 16) ==
      std::vector<uint8_t>({
        0x00, 0x00, 0x00, 0x00,
        0x88, 0x77, 0x66, 0x55,
        0x00, 0x00, 0x00, 0x00,
        0xfe, 0xff, 0xff, 0xff
      }));
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
