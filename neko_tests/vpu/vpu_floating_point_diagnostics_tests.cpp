#include <sstream>
#include <vector>

#include "catch.hpp"
#include "floating_point_ops.hpp"
#include "vpu.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_program_runner.hpp"
#include "vpu_register_ids.hpp"
#include "vpu_upper_instruction_utils.hpp"

namespace
{
  VPUTraceEvent executeTracedInstruction(
    VPU *vpu,
    std::vector<std::uint8_t> *instructions,
    std::uint32_t destinationMask,
    std::uint8_t ft,
    std::uint8_t fs,
    std::uint8_t fd,
    std::uint16_t opCode)
  {
    VPUTraceEvent writeback{};
    bool found = false;
    vpu->setTraceCallback(
      [&writeback, &found, opCode](const VPUTraceEvent &event) {
        if (event.type == VPUTraceEventType::PipelineWriteback &&
            event.opCode == opCode)
        {
          writeback = event;
          found = true;
        }
      });

    executeSingleUpperInstruction(
      vpu,
      instructions,
      0,
      destinationMask,
      ft,
      fs,
      fd,
      opCode);

    REQUIRE(found);
    return writeback;
  }
}

TEST_CASE("VU compound FMAC trace diagnostics")
{
  VUFloat maximum;
  maximum.setBits(0x7fffffffu);
  const double minimumNormalized = std::ldexp(1.0, -126);

  SECTION("MADD separates multiplication underflow from its final result")
  {
    VPU vpu;
    std::vector<std::uint8_t> instructions;
    vpu.loadFPRegister(
      VPU_REGISTER_VF06,
      minimumNormalized,
      0,
      0,
      0);
    vpu.loadFPRegister(VPU_REGISTER_VF07, 0.5, 0, 0, 0);
    vpu.loadAccumulator(-maximum, 0, 0, 0);

    const VPUTraceEvent event = executeTracedInstruction(
      &vpu,
      &instructions,
      VPU_DEST_X_BIT,
      VPU_REGISTER_VF07,
      VPU_REGISTER_VF06,
      VPU_REGISTER_VF02,
      VPU_MADD);

    REQUIRE(event.arithmetic.present);
    REQUIRE(event.arithmetic.ignoredResultFields ==
            FP_REGISTER_NO_FIELDS);
    REQUIRE(event.arithmetic.lanes[0].multiplyBits == 0);
    REQUIRE(event.arithmetic.lanes[0].multiplyFlags ==
            FP_FLAG_UNDERFLOW);
    REQUIRE(event.arithmetic.lanes[0].accumulatorBits == 0xffffffffu);
    REQUIRE(event.arithmetic.lanes[0].resultBits == 0xffffffffu);
    REQUIRE(event.arithmetic.lanes[0].resultFlags == 0);
  }

  SECTION("MSUB separates final overflow from a valid multiplication")
  {
    VPU vpu;
    std::vector<std::uint8_t> instructions;
    vpu.loadFPRegister(VPU_REGISTER_VF06, maximum, 0, 0, 0);
    vpu.loadFPRegister(VPU_REGISTER_VF07, -1, 0, 0, 0);
    vpu.loadAccumulator(maximum, 0, 0, 0);

    const VPUTraceEvent event = executeTracedInstruction(
      &vpu,
      &instructions,
      VPU_DEST_X_BIT,
      VPU_REGISTER_VF07,
      VPU_REGISTER_VF06,
      VPU_REGISTER_VF02,
      VPU_MSUB);

    REQUIRE(event.arithmetic.present);
    REQUIRE(event.arithmetic.ignoredResultFields ==
            FP_REGISTER_NO_FIELDS);
    REQUIRE(event.arithmetic.lanes[0].multiplyBits == 0xffffffffu);
    REQUIRE(event.arithmetic.lanes[0].multiplyFlags == 0);
    REQUIRE(event.arithmetic.lanes[0].accumulatorBits == 0x7fffffffu);
    REQUIRE(event.arithmetic.lanes[0].resultBits == 0x7fffffffu);
    REQUIRE(event.arithmetic.lanes[0].resultFlags ==
            FP_FLAG_OVERFLOW);
  }

  SECTION("OPMSUB identifies multiplication overflow as an ignored field")
  {
    VPU vpu;
    std::vector<std::uint8_t> instructions;
    vpu.loadFPRegister(VPU_REGISTER_VF05, 0, 0, 50, 0);
    vpu.loadFPRegister(VPU_REGISTER_VF06, 0, maximum, 0, 0);

    const VPUTraceEvent event = executeTracedInstruction(
      &vpu,
      &instructions,
      VPU_DEST_XYZ_FIELDS,
      VPU_REGISTER_VF05,
      VPU_REGISTER_VF06,
      VPU_REGISTER_VF02,
      VPU_OPMSUB);

    REQUIRE(event.arithmetic.present);
    REQUIRE(event.arithmetic.ignoredResultFields ==
            FP_REGISTER_X_FIELD);
    REQUIRE(event.arithmetic.lanes[0].multiplyBits == 0x7fffffffu);
    REQUIRE(event.arithmetic.lanes[0].multiplyFlags ==
            FP_FLAG_OVERFLOW);
    REQUIRE(event.arithmetic.lanes[0].accumulatorBits == 0);
    REQUIRE(event.arithmetic.lanes[0].resultBits == 0xffffffffu);
    REQUIRE(event.arithmetic.lanes[0].resultFlags ==
            FP_FLAG_OVERFLOW);
  }

  SECTION("NDJSON includes arithmetic details only when present")
  {
    VPUTraceEvent event{};
    event.type = VPUTraceEventType::PipelineWriteback;
    event.cycle = 12;
    event.instructionAddress = 16;
    event.opCode = VPU_MADD;
    event.destinationRegister = VPU_REGISTER_VF02;
    event.destinationFieldMask = FP_REGISTER_X_FIELD;
    event.arithmetic.present = true;
    event.arithmetic.ignoredResultFields = FP_REGISTER_X_FIELD;
    event.arithmetic.lanes[0] = {
      0x7fffffffu,
      FP_FLAG_OVERFLOW,
      0x3f800000u,
      0x7fffffffu,
      FP_FLAG_OVERFLOW
    };
    std::ostringstream output;

    writeVPUTraceEventJsonLine(output, event);

    REQUIRE(output.str() ==
      "{\"type\":\"pipeline_writeback\",\"cycle\":12,"
      "\"instruction_address\":16,\"upper_instruction\":0,"
      "\"lower_instruction\":0,\"opcode\":41,"
      "\"destination_register\":2,\"destination_field_mask\":1,"
      "\"arithmetic\":{\"ignored_result_fields\":1,\"lanes\":["
      "{\"multiply_bits\":2147483647,\"multiply_flags\":1,"
      "\"accumulator_bits\":1065353216,\"result_bits\":2147483647,"
      "\"result_flags\":1},"
      "{\"multiply_bits\":0,\"multiply_flags\":0,"
      "\"accumulator_bits\":0,\"result_bits\":0,\"result_flags\":0},"
      "{\"multiply_bits\":0,\"multiply_flags\":0,"
      "\"accumulator_bits\":0,\"result_bits\":0,\"result_flags\":0},"
      "{\"multiply_bits\":0,\"multiply_flags\":0,"
      "\"accumulator_bits\":0,\"result_bits\":0,\"result_flags\":0}]}}\n");
  }
}
