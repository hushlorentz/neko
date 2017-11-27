#include "catch.hpp"
#include "vpu.hpp"
#include "vpu_register_ids.hpp"

TEST_CASE("VPU FP Register Tests")
{
    VPU vpu;

    SECTION("VF00 always returns <0,0,0,1>")
    {
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF00)->x == 0.0f);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF00)->y == 0.0f);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF00)->z == 0.0f);
      REQUIRE(vpu.fpRegisterValue(VPU_REGISTER_VF00)->w == 1.0f);
    }

    SECTION("VI00 always returns 0")
    {
      REQUIRE(vpu.intRegisterValue(VPU_REGISTER_VI00) == 0);
    }
}
