#include "catch.hpp"
#include "floating_point_ops.hpp"
#include "fp_register.hpp"

TEST_CASE("Testing the floating point conventions")
{
    SECTION("Two floating point numbers can be added together")
    {
      float a1 = 3.5f;
      float a2 = 7.25f;

      REQUIRE(addFP(a1, a2) == a1 + a2);
    }

    SECTION("Two floating point numbers can be multiplied together")
    {
      float a1 = 3.5f;
      float a2 = 7.0f;

      REQUIRE(mulFP(a1, a2) == a1 * a2);
    }
    
    SECTION("Two floating point numbers can be subtracted")
    {
      float a1 = 5.6;
      float a2 = 7.8;

      REQUIRE(subFP(a1, a2) == a1 - a2);
    }

    SECTION("Two floating point numbers can be divided")
    {
      float a1 = 10.5f;
      float a2 = 3.44f;

      REQUIRE(divFP(a1, a2) == a1 / a2);
    }

    SECTION("Multiplying two floating point numbers that result in INF returns 2^(128)")
    {
      float a1 = std::numeric_limits<float>::infinity();
      float a2 = 7.0f;
      float_cast fu;

      fu.f = mulFP(a1, a2);

      REQUIRE(fu.parts.mantissa == 2);
      REQUIRE(fu.parts.exponent == 0x80);
    }

    SECTION("Dividing 0 by 0 returns 2^(128)")
    {
      float a1 = 0;
      float a2 = 0;
      float_cast fu;

      fu.f = divFP(a1, a2);

      REQUIRE(fu.parts.mantissa == 2);
      REQUIRE(fu.parts.exponent == 0x80);
    }

    SECTION("Dividing the smallest normalized value by 5 returns 0")
    {
      float a1 = std::numeric_limits<float>::min();
      float a2 = 5;

      REQUIRE(divFP(a1, a2) == 0.0f);
    }

}
