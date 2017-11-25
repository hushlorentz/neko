#include "catch.hpp"
#include "vpu.hpp"
#include "vpu_register_ids.hpp"

TEST_CASE("VPU Flag Tests")
{
    VPU vpu;

    SECTION("FSSET instruction sets the status flags upper instruction flag result is ignored")
    {
      WARN("Add the test");
    }

    SECTION("FCSET instruction sets the clipping flags upper instruction flag result is ignored")
    {
      WARN("Add the test");
    }
}
