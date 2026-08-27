#include <cstdint>
#include <vector>

#include "catch.hpp"
#include "vpu.hpp"
#include "vpu_lower_instruction.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"
#include "vpu_xgkick_handler.hpp"

namespace
{
  class TestXGKICKHandler : public VUXGKICKHandler
  {
    public:
      bool active = false;
      std::vector<std::uint16_t> addresses;

      bool path1TransferActive() const override
      {
        return active;
      }

      void startPath1Transfer(std::uint16_t qwordAddress) override
      {
        addresses.push_back(qwordAddress);
        active = true;
      }

      void complete()
      {
        active = false;
      }
  };

  void appendWord(std::vector<std::uint8_t> *instructions, std::uint32_t word)
  {
    instructions->push_back(word & 0xff);
    instructions->push_back((word >> 8) & 0xff);
    instructions->push_back((word >> 16) & 0xff);
    instructions->push_back((word >> 24) & 0xff);
  }

  void appendInstructionPair(
    std::vector<std::uint8_t> *instructions,
    std::uint32_t upper,
    std::uint32_t lower = VPU_LOWER_NOP)
  {
    appendWord(instructions, lower);
    appendWord(instructions, upper);
  }

  std::uint32_t xgkick(std::uint8_t is)
  {
    return
      VPU_XGKICK_ENCODING |
      (static_cast<std::uint32_t>(is) << VPU_FS_REG_SHIFT);
  }

  std::uint32_t add(
    std::uint8_t ft,
    std::uint8_t fs,
    std::uint8_t fd)
  {
    return
      VPU_DEST_X_BIT |
      (static_cast<std::uint32_t>(ft) << VPU_FT_REG_SHIFT) |
      (static_cast<std::uint32_t>(fs) << VPU_FS_REG_SHIFT) |
      (static_cast<std::uint32_t>(fd) << VPU_FD_REG_SHIFT) |
      VPU_ADD;
  }
}

TEST_CASE("VU XGKICK Tests")
{
  SECTION("XGKICK decodes its integer address source")
  {
    LowerInstruction instruction =
      decodeLowerInstruction(xgkick(VPU_REGISTER_VI07));

    REQUIRE(instruction.unit == LowerExecutionUnit::XGKICK);
    REQUIRE(instruction.opCode == VPU_XGKICK);
    REQUIRE(instruction.sourceRegister1 == VPU_REGISTER_VI07);
  }

  SECTION("XGKICK is rejected on VU0")
  {
    VPU vpu(VPUType::VU0);
    std::vector<std::uint8_t> instructions;
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      xgkick(VPU_REGISTER_VI01));
    appendInstructionPair(&instructions, VPU_NOP);
    vpu.uploadMicroInstructions(instructions);

    REQUIRE_THROWS_WITH(
      vpu.initMicroMode(),
      "XGKICK is only supported on VU1.");
  }

  SECTION("A handler receives the wrapped VU1 qword address")
  {
    VPU vpu(VPUType::VU1);
    TestXGKICKHandler handler;
    std::vector<std::uint8_t> instructions;
    vpu.setXGKICKHandler(&handler);
    vpu.loadIntRegister(VPU_REGISTER_VI01, 0x401);
    appendInstructionPair(
      &instructions,
      VPU_E_BIT | VPU_NOP,
      xgkick(VPU_REGISTER_VI01));
    appendInstructionPair(&instructions, VPU_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();

    REQUIRE(vpu.tick());
    REQUIRE(vpu.tick());
    REQUIRE(handler.addresses == std::vector<std::uint16_t>({1}));
    vpu.forceBreak();
  }

  SECTION("PATH1 stalls the instruction after a continuously issued XGKICK")
  {
    VPU vpu(VPUType::VU1);
    TestXGKICKHandler handler;
    std::vector<std::uint8_t> instructions;
    std::vector<VPUTraceEvent> events;
    vpu.setXGKICKHandler(&handler);
    vpu.setTraceCallback([&events](const VPUTraceEvent &event) {
      events.push_back(event);
    });
    vpu.loadIntRegister(VPU_REGISTER_VI01, 1);
    vpu.loadIntRegister(VPU_REGISTER_VI02, 2);
    vpu.loadFPRegister(VPU_REGISTER_VF01, 1, 0, 0, 0);
    vpu.loadFPRegister(VPU_REGISTER_VF02, 2, 0, 0, 0);

    appendInstructionPair(
      &instructions,
      VPU_NOP,
      xgkick(VPU_REGISTER_VI01));
    appendInstructionPair(
      &instructions,
      add(
        VPU_REGISTER_VF01,
        VPU_REGISTER_VF02,
        VPU_REGISTER_VF03),
      xgkick(VPU_REGISTER_VI02));
    appendInstructionPair(
      &instructions,
      VPU_E_BIT |
        add(
          VPU_REGISTER_VF02,
          VPU_REGISTER_VF02,
          VPU_REGISTER_VF04));
    appendInstructionPair(&instructions, VPU_NOP);
    vpu.uploadMicroInstructions(instructions);
    vpu.startMicroMode();

    REQUIRE(vpu.tick());
    REQUIRE(vpu.tick());
    REQUIRE_FALSE(vpu.tick());
    handler.complete();
    REQUIRE_FALSE(vpu.tick());
    REQUIRE(handler.addresses ==
      std::vector<std::uint16_t>({1, 2}));
    handler.complete();
    REQUIRE(vpu.tick());
    vpu.run(20);

    std::vector<std::uint32_t> stallCycles;
    for (const VPUTraceEvent &event : events)
    {
      if (event.type == VPUTraceEventType::PipelineStall)
      {
        stallCycles.push_back(event.cycle);
      }
    }
    REQUIRE(stallCycles == std::vector<std::uint32_t>({2, 3}));
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->x == 3);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF04)->x == 4);
  }

  SECTION("XGKICK cannot execute in an E-bit delay slot")
  {
    VPU vpu(VPUType::VU1);
    std::vector<std::uint8_t> instructions;
    appendInstructionPair(&instructions, VPU_E_BIT | VPU_NOP);
    appendInstructionPair(
      &instructions,
      VPU_NOP,
      xgkick(VPU_REGISTER_VI01));
    vpu.uploadMicroInstructions(instructions);

    REQUIRE_THROWS_WITH(
      vpu.initMicroMode(),
      "VU lower instruction cannot execute in an E-bit delay slot.");
  }
}
