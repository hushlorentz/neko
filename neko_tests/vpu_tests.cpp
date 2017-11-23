#include "catch.hpp"
#include "vpu.hpp"

TEST_CASE("VU Tests")
{
    VPU vpu;
    
    SECTION("Hello Test")
    {
        uint8_t p = 0x4;
        
        REQUIRE(1 == 1);
    }
}
