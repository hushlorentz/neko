#include "catch.hpp"
#include "vpu.hpp"

TEST_CASE("VPU Microinstruction Tests")
{
    VPU vpu;

    SECTION("Microinstruction programs can be started by specifying the execution address in the VIFcode MSCAL")
    {
      WARN("Add this test");
    }

    SECTION("Microinstruction programs can be started by specifying the execution address in the VIFcode MSCALF")
    {
      WARN("Add this test");
    }

    SECTION("VPU1 Microinstruction programs can be started by writing the execution address to the control register CMSAR1")
    {
      WARN("Add this test");
    }

    SECTION("VPU0 Microinstruction programs can be started by executing the VCALLMS instruction")
    {
      WARN("Add this test");
    }

    SECTION("VPU0 Microinstruction programs can be started by executing the VCALLMSR instruction")
    {
      WARN("Add this test");
    }
}
