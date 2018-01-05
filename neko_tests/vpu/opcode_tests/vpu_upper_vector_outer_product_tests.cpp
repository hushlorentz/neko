#include "catch.hpp"
#include "floating_point_ops.hpp"
#include "vpu.hpp"
#include "vpu_flags.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"
#include "vpu_upper_instruction_utils.hpp"

TEST_CASE("VPU Microinstruction OPMULA Tests")
{
  VPU vpu;
  vpu.useThreads = false;
  Double max;
  max.exponent = FP_MAX_EXPONENT_WITH_BIAS;
  max.mantissa = FP_MAX_MANTISSA;

  Double min;
  min.d = std::numeric_limits<double>::min();

  vpu.loadFPRegister(VPU_REGISTER_VF03, -5.0, -2.5, -1.0, 4.5);
  vpu.loadFPRegister(VPU_REGISTER_VF04, 5.0, -6.5, 10.0, -9.0);
  vpu.loadFPRegister(VPU_REGISTER_VF05, 0.25, 0.25, 50, 0.25);
  vpu.loadFPRegister(VPU_REGISTER_VF06, 0, max.d, min.d, 30);
  vector<uint8_t> instructions;

  SECTION("OPMULA calculates the second part of the vector outer product of the xyz fields of the ft and fs vectors and stores the result in the accumulator")
  {
    vpu.loadAccumulator(100, 100, 100, 100);
    executeSingleUpperInstruction(&vpu, &instructions, 0, 0, VPU_REGISTER_VF04, VPU_REGISTER_VF03, 0, VPU_OPMULA);

    REQUIRE(vpu.accumulator.x == -25);
    REQUIRE(vpu.accumulator.y == -5);
    REQUIRE(vpu.accumulator.z == 32.5);
    REQUIRE(vpu.accumulator.w == 100);

    REQUIRE(vpu.hasMACFlag(VPU_FLAG_SX));
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_SY));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_SZ));
  }

  SECTION("OPMULA sets the correct flags for overflow, zero and underflow")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, 0, VPU_REGISTER_VF05, VPU_REGISTER_VF06, 0, VPU_OPMULA);

    REQUIRE(vpu.accumulator.x == max.d);
    REQUIRE(vpu.accumulator.y == 0);
    REQUIRE(vpu.accumulator.z == 0);

    REQUIRE(vpu.hasMACFlag(VPU_FLAG_OX));
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_UY));
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZZ));
  }

  SECTION("OPMSUB calculates the first part of the vector outer product of the xyz fields of the ft and fs vectors and stores the result in the fd vector")
  {
    vpu.loadAccumulator(100, 100, 100, 100);
    executeSingleUpperInstruction(&vpu, &instructions, 0, 0, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_OPMSUB);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == 125);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y  == 105);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z  == 67.5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w  == 0);
  }


  SECTION("OPMSUB sets the correct flags for overflow, zero and underflow")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, 0, VPU_REGISTER_VF05, VPU_REGISTER_VF06, VPU_REGISTER_VF02, VPU_OPMSUB);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == -max.d);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == 0);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == 0);

    REQUIRE(vpu.hasMACFlag(VPU_FLAG_OX));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_US));
  }
}
