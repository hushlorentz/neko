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

  SECTION("MULx multiplies the x field of the ft vector with every field of the fs vector and stores the result in the fd vector.")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MULx);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == -25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == -12.5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == -5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == 22.5);
  }

  SECTION("MULy multiplies the y field of the ft vector with every field of the fs vector and stores the result in the fd vector.")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MULy);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == 32.5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == 16.25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == 6.5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == -29.25);
  }

  SECTION("MULz multiplies the z field of the ft vector with every field of the fs vector and stores the result in the fd vector.")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MULz);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == -50);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == -25);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == -10);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == 45);
  }

  SECTION("MULw multiplies the w field of the ft vector with every field of the fs vector and stores the result in the fd vector.")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, VPU_REGISTER_VF02, VPU_MULw);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->x == 45);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->y == 22.5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->z == 9);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF02)->w == -40.5);
  }

  SECTION("MULA multiplies two vectors and stores the result in the accumulator")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, 0, VPU_MULA);

    REQUIRE(vpu.accumulator.x == -25);
    REQUIRE(vpu.accumulator.y == 16.25);
    REQUIRE(vpu.accumulator.z == -10);
    REQUIRE(vpu.accumulator.w == -40.5);
  }

  SECTION("MULAi multiplies the fields of the fs vector with the I register and stores the result in the fd register")
  {
    vpu.loadIRegister(0.25);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, 0, VPU_REGISTER_VF10, 0, VPU_MULAi);

    REQUIRE(vpu.accumulator.x == 2.5);
    REQUIRE(vpu.accumulator.y == 5);
    REQUIRE(vpu.accumulator.z == 7.5);
    REQUIRE(vpu.accumulator.w == 10);
  }

  SECTION("MULAq multiplies the fields of the fs vector with the Q register and stores the result in the accumulator")
  {
    vpu.loadQRegister(0.5);

    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, 0, VPU_REGISTER_VF10, 0, VPU_MULAq);

    REQUIRE(vpu.accumulator.x == 5);
    REQUIRE(vpu.accumulator.y == 10);
    REQUIRE(vpu.accumulator.z == 15);
    REQUIRE(vpu.accumulator.w == 20);
  }

  SECTION("MULAx multiplies the x field of the ft vector with every field of the fs vector and stores the result in the accumulator.")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, 0, VPU_MULAx);

    REQUIRE(vpu.accumulator.x == -25);
    REQUIRE(vpu.accumulator.y == -12.5);
    REQUIRE(vpu.accumulator.z == -5);
    REQUIRE(vpu.accumulator.w == 22.5);
  }

  SECTION("MULAy multiplies the y field of the ft vector with every field of the fs vector and stores the result in the accumulator.")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, 0, VPU_MULAy);

    REQUIRE(vpu.accumulator.x == 32.5);
    REQUIRE(vpu.accumulator.y == 16.25);
    REQUIRE(vpu.accumulator.z == 6.5);
    REQUIRE(vpu.accumulator.w == -29.25);
  }

  SECTION("MULAz multiplies the z field of the ft vector with every field of the fs vector and stores the result in the accumulator.")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, 0, VPU_MULAz);

    REQUIRE(vpu.accumulator.x == -50);
    REQUIRE(vpu.accumulator.y == -25);
    REQUIRE(vpu.accumulator.z == -10);
    REQUIRE(vpu.accumulator.w == 45);
  }

  SECTION("MULAw multiplies the w field of the ft vector with every field of the fs vector and stores the result in the accumulator.")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF04, VPU_REGISTER_VF03, 0, VPU_MULAw);

    REQUIRE(vpu.accumulator.x == 45);
    REQUIRE(vpu.accumulator.y == 22.5);
    REQUIRE(vpu.accumulator.z == 9);
    REQUIRE(vpu.accumulator.w == -40.5);
  }
}
