#include "catch.hpp"
#include "floating_point_ops.hpp"
#include "vpu.hpp"
#include "vpu_flags.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"
#include "vpu_upper_instruction_utils.hpp"

TEST_CASE("VPU Microinstruction MADD Tests")
{
  VPU vpu;
  vpu.useThreads = false;
  vpu.loadFPRegister(VPU_REGISTER_VF03, -5.0f, -2.5f, -1.0f, 4.5f);
  vpu.loadFPRegister(VPU_REGISTER_VF04, 5.0f, -6.5f, 10.0f, -9.0f);
  vpu.loadFPRegister(VPU_REGISTER_VF05, 2, -6.5f, 0, 9.0f);
  vector<uint8_t> instructions;

  SECTION("MADD multiplies two vectors and adds the result to the accumulator and stores the result in the third vector parameter")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 50.25f, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MADD);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == 75.0f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == 91.75f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == 40.25f);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == -15.5f);
  }

  SECTION("MADD sets the correct flags if accumulator contains 0 or a normalized value and there is no exception during the multiplication")
  {
    vpu.loadAccumulator(100.0f, 75.5f, 0, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF05, VPU_REGISTER_VF02, VPU_MADD);

    REQUIRE(vpu.hasMACFlag(VPU_FLAG_ZZ));
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_SW));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_Z));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_S));
  }

  SECTION("MADD sets the correct flags if accumulator contains 0 or a normalized value and there is an overflow exception during the multiplication")
  {
    num_32bits infinity;
    infinity.float_representation = std::numeric_limits<float>::infinity();
    vpu.loadFPRegister(VPU_REGISTER_VF06, infinity.float_representation, -2.5f, -1.0f, 4.5f);

    vpu.loadAccumulator(100.0f, 75.5f, 0, 25.0f);
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF06, VPU_REGISTER_VF02, VPU_MADD);

    num_32bits num;
    num.float_representation = vpu.fpRegisterValue(VPU_REGISTER_VF02)->x;

    REQUIRE(vpu.hasMACFlag(VPU_FLAG_OX));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_OY));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_OZ));
    REQUIRE(!vpu.hasMACFlag(VPU_FLAG_OW));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_O));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_OS));
    REQUIRE(num.components.mantissa == FP_MAX_MANTISSA);
    REQUIRE(num.components.exponent == FP_MAX_EXPONENT);
  }

  SECTION("MADD sets the correct flags if accumulator contains 0 or a normalized value and there is an underflow exception during the multiplication")
  {
    num_32bits nonNormalized;
    nonNormalized.float_representation = std::numeric_limits<float>::min();

    vpu.loadFPRegister(VPU_REGISTER_VF07, 2, 0.5f, 3.4f, 9.0f);
    vpu.loadFPRegister(VPU_REGISTER_VF06, 25.5f, nonNormalized.float_representation, -2.5f, -1.0f);
    vpu.loadAccumulator(100.0f, 5, 0, 25.0f);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF07, VPU_REGISTER_VF06, VPU_REGISTER_VF02, VPU_MADD);

    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_US));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_ZS));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_SS));
    REQUIRE(!vpu.hasStatusFlag(VPU_FLAG_U));
    REQUIRE(!vpu.hasStatusFlag(VPU_FLAG_O));
  }
}
