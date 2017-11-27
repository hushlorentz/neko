#include "catch.hpp"
#include "vpu.hpp"

TEST_CASE("VPU FP Calculation Tests")
{
    VPU vpu;

    SECTION("Processing 0/0 returns +MAX/-MAX and sets both the I and IS flags")
    {
      WARN("Add this test");
    }

    SECTION("Processing sqrt(x < 0) returns sqrt(abs(x)) and sets both the I and IS flags")
    {
      WARN("Add this test");
    }

    SECTION("Processing x/0 returns +MAX/-MAX and sets both the D and DS flags")
    {
      WARN("Add this test");
    };

    SECTION("If an operation results in exponent overflow, the return is +MAX/-MAX and Ox, Oy, Oz, Ow, O, and OS flags are set")
    {
      WARN("Add this test");
    }

    SECTION("If an operation results in exponent underflow, the return is +0/-0 and Ux, Uy, Uz, Uw, U, Zx, Zy, Zz, Zw, Z, US, and ZS flags are set")
    {
      WARN("Add this test");
    }

    SECTION("If an operation results in conversion overflow, the return is +MAX/-MAX")
    {
      WARN("Add this test");
    }

    SECTION("Check the rounding off part of the VU manual on page 27.")
    {
      WARN("Add this test");
    }
}
