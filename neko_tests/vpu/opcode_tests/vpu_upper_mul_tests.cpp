#include "catch.hpp"
#include "floating_point_ops.hpp"
#include "vpu.hpp"
#include "vpu_flags.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"
#include "vpu_upper_instruction_utils.hpp"

TEST_CASE("VPU Microinstruction MUL Tests")
{
  VPU vpu;
  vpu.useThreads = false;
  vpu.loadFPRegister(VPU_REGISTER_VF02, 1, 1, 1, 1);
  vpu.loadFPRegister(VPU_REGISTER_VF03, -5.0, -2.5, -1.0, 4.5);
  vpu.loadFPRegister(VPU_REGISTER_VF04, 5.0, -6.5, 10.0, -9.0);
  vpu.loadFPRegister(VPU_REGISTER_VF05, 2, -6.5, 0, 9.0);
  vpu.loadFPRegister(VPU_REGISTER_VF10, 10, 20, 30, 40);
  vector<uint8_t> instructions;

  SECTION("MUL multiplies two vectors and stores the result in the third vector parameter")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MUL);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == -25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == 16.25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == -10);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == -40.5);
  }

  SECTION("MUL sets the correct flags if there is an overflow")
  {
    Double num;
    num.mantissa = FP_MAX_MANTISSA;
    num.exponent = FP_MAX_EXPONENT_WITH_BIAS;

    vpu.loadFPRegister(VPU_REGISTER_VF06, 0, num.d, 0, num.d);
    vpu.loadFPRegister(VPU_REGISTER_VF07, 0, num.d, 0, num.d);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF06, VPU_REGISTER_VF07, VPU_REGISTER_VF02, VPU_MUL);
    
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == 0);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == num.d);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == 0);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == num.d);
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_OY));
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_OW));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_O));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_OS));
  }

  SECTION("MUL sets the correct flags if there is an underflow")
  {
    Double num;
    num.d = std::numeric_limits<double>::min();

    vpu.loadFPRegister(VPU_REGISTER_VF06, num.d, 0, 0.5, 0);
    vpu.loadFPRegister(VPU_REGISTER_VF07, 0.5, 0, num.d, 0);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF06, VPU_REGISTER_VF07, VPU_REGISTER_VF02, VPU_MUL);
    
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == 0);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == 0);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == 0);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == 0);
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_UX));
    REQUIRE(vpu.hasMACFlag(VPU_FLAG_UZ));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_U));
    REQUIRE(vpu.hasStatusFlag(VPU_FLAG_US));
  }

  SECTION("MULi multiplies the fields of the fs vector with the I register and stores the result in the fd register")
  {
    vpu.loadIRegister(0.25);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, 0, VPU_REGISTER_VF10, VPU_REGISTER_VF02, VPU_MULi);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == 2.5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == 5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == 7.5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == 10);
  }

  SECTION("MULq multiplies the fields of the fs vector with the Q register and stores the result in the fd register")
  {
    vpu.loadQRegister(0.5);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, 0, VPU_REGISTER_VF10, VPU_REGISTER_VF02, VPU_MULq);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == 5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == 10);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == 15);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == 20);
  }
}
