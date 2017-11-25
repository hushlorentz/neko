#include "catch.hpp"
#include "vpu.hpp"

TEST_CASE("VPU State Tests")
{
    VPU vpu;
    
    SECTION("VPU starts in the Ready state ")
    {
        REQUIRE(vpu.getState() == VPU_STATE_READY);
    }

    SECTION("VPU transitions to Ready state on reset")
    {
      WARN("Add this test");
    }

    SECTION("VPU control register is initialised on reset")
    {
      WARN("Add this test");
    }

    SECTION("VPU transitions from Ready to Run when a micro subroutine is started")
    {
      WARN("Add this test");
    }

    SECTION("VPU transitions from Ready to Run when a macro instruction is executed")
    {
      WARN("Add this test");
    }

    SECTION("VPU transitions from Ready to Stop when a ForceBreak occurs")
    {
      WARN("Add this test");
    }

    SECTION("VPU cannot receive micro subroutine startup from the EE core while in Ready state")
    {
      WARN("Add this test");
    }

    SECTION("VPU cannot receive macro instructions from the EE core in Ready state")
    {
      WARN("Add this test");
    }

    SECTION("VPU transitions from Run to Ready at micro subroutine E bit termination")
    {
      WARN("Add this test");
    }

    SECTION("VPU transitions from Run to Ready at macro instruction execution termination")
    {
      WARN("Add this test");
    }

    SECTION("VPU transitions from Run to Stop when a D bit halt occurs")
    {
      WARN("Add this test");
    }

    SECTION("VPU transitions from Run to Stop when a T bit halt occurs")
    {
      WARN("Add this test");
    }

    SECTION("VPU transitions from Run to Stop when a ForceBreak occurs")
    {
      WARN("Add this test");
    }

    SECTION("VPU cannot receive micro program startup from the VIF while in Stop state")
    {
      WARN("Add this test");
    }

    SECTION("VPU transitions from Stop to Run when the VCALLMS instruction is executed")
    {
      WARN("Add this test");
    }

    SECTION("VPU transitions from Stop to Run when the VCALLMSR instruction is executed")
    {
      WARN("Add this test");
    }

    SECTION("VPU transitions from Stop to Run when the CMSAR1 register is written to")
    {
      WARN("Add this test");
    }
}
