#include "catch.hpp"
#include "vpu.hpp"
#include "vpu_opcodes.hpp"
#include "vpu_register_ids.hpp"
#include "vpu_upper_instruction_utils.hpp"

TEST_CASE("VPU Microinstruction Fixed Point Tests")
{
  VPU vpu;
  vpu.useThreads = false;
  vpu.loadFPRegister(VPU_REGISTER_VF02, -5.2f, 1.2535f, 0.0042f, -10.10f);
  vpu.loadFPRegister(VPU_REGISTER_VF03, 5.0f, -6.4f, 10.0f, -9.0f);
  vector<uint8_t> instructions;

  SECTION("FTOI0 converts floating point fields of the second source vector to fixed point and stores the result in the fields of the first vector")
  {
    executeSingleUpperInstruction(&vpu, &instructions, 0, VPU_DEST_ALL_FIELDS, VPU_REGISTER_VF03, VPU_REGISTER_VF02, 0, VPU_FTOI0);

    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->xInt == -5);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->yInt == 1);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->zInt == 0);
    REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF03)->wInt == -10);
  }
}
