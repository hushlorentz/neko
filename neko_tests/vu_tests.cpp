#include "catch.hpp"
#include "vu.hpp"

TEST_CASE("VU Tests")
{
    VU vu;
    
    SECTION("Hello Test")
    {
        uint8_t p = 0x4;
        
        REQUIRE(1 == 1);
    }
}
