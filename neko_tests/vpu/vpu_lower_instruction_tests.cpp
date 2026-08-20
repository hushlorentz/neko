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
    uint32_t lower)
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

  uint32_t mfir(uint8_t fieldMask, uint8_t ft, uint8_t is)
  {
    return
      VPU_MFIR_ENCODING |
      (static_cast<uint32_t>(fieldMask) << 21) |
      (static_cast<uint32_t>(ft) << 16) |
      (static_cast<uint32_t>(is) << 11);
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
}

TEST_CASE("VU Lower Instruction Tests")
{
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
}
